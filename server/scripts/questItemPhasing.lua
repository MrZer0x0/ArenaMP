-- ArenaMP X013 - per-player quest item phasing.
--
-- Changes against X012:
--   * classification comes from questIndexStore (server-owned), never from a
--     flag inside the packet the client sent;
--   * source ids are derived here, so a client cannot pick the key its claim is
--     recorded under;
--   * eligibility is stamped when the canonical inventory is established, not
--     when somebody takes something - a player can no longer turn an ordinary
--     stack into a phaseable source by picking it up;
--   * claims are bucketed per cell instead of one flat table, so a take and a
--     cell load are O(claims in that cell) rather than O(all claims ever).

local questIndexStore = require("questIndexStore")

local questItemPhasing = {}

local legacyHintsLoaded = false
local legacyWorldHints = {}

local function lower(value)
    if value == nil then return "" end
    return string.lower(tostring(value))
end

local function num(value, fallback)
    return math.floor(tonumber(value) or fallback)
end

local function cellKey(cellDescription)
    return lower(cellDescription)
end

local function itemSignature(item)
    if item == nil then return "" end
    return table.concat({
        lower(item.refId),
        string.format("%d", num(item.charge, -1)),
        string.format("%.3f", tonumber(item.enchantmentCharge) or -1),
        lower(item.soul),
        lower(item.poisonId),
        string.format("%d", num(item.poisonCharges, 0))
    }, "|")
end

local function worldSourceId(cellDescription, uniqueIndex, refId)
    return "qiw:" .. questIndexStore.Hash(cellKey(cellDescription) .. "|" ..
        tostring(uniqueIndex) .. "|" .. lower(refId))
end

local function containerSourceId(cellDescription, uniqueIndex, item)
    return "qic:" .. questIndexStore.Hash(cellKey(cellDescription) .. "|" ..
        tostring(uniqueIndex) .. "|" .. itemSignature(item))
end

local function quicksavePlayer(pid)
    local player = Players[pid]
    if player == nil then return end
    if player.QuicksaveToDrive ~= nil then
        player:QuicksaveToDrive()
    elseif player.SaveToDrive ~= nil then
        player:SaveToDrive()
    end
end

-- Rebuild every claim key from the identity fields it already stores, and move
-- it into its cell bucket. Needed because X012 keys were hashed on the client
-- with a different function; without this migration a character who already took
-- a quest item would be handed a second copy after the upgrade.
local function migrate(data)
    if data.version == 2 then return false end

    local flat = data.claims or {}
    local claims = {}

    for _, claim in pairs(flat) do
        if type(claim) == "table" and claim.cell ~= nil and claim.uniqueIndex ~= nil and claim.refId ~= nil then
            local bucket = cellKey(claim.cell)
            claims[bucket] = claims[bucket] or {}

            local sourceId
            if claim.kind == "world" then
                sourceId = worldSourceId(claim.cell, claim.uniqueIndex, claim.refId)
            else
                claim.kind = "container"
                sourceId = containerSourceId(claim.cell, claim.uniqueIndex, claim)
            end

            local existing = claims[bucket][sourceId]
            if existing == nil then
                claims[bucket][sourceId] = claim
            else
                -- Two legacy keys collapsing onto one canonical source: keep the
                -- larger claim rather than handing out the difference again.
                existing.count = math.max(num(existing.count, 1), num(claim.count, 1))
            end
        end
    end

    data.version = 2
    data.claims = claims
    return true
end

local function ensurePlayerData(pid)
    local player = Players[pid]
    if player == nil then return nil end

    if player.data.questItemPhasing == nil then
        player.data.questItemPhasing = { version = 2, claims = {} }
    end

    local data = player.data.questItemPhasing
    if data.claims == nil then data.claims = {} end

    if migrate(data) then
        quicksavePlayer(pid)
        tes3mp.LogAppend(enumerations.log.INFO,
            "[ArenaMP Core] Migrated claims of " .. logicHandler.GetChatName(pid) .. " to layout 2")
    end

    return data
end

local function getBucket(data, cellDescription, create)
    if data == nil then return nil end
    local key = cellKey(cellDescription)
    if data.claims[key] == nil then
        if not create then return nil end
        data.claims[key] = {}
    end
    return data.claims[key]
end

local function loadLegacyHints()
    if legacyHintsLoaded then return end
    legacyHintsLoaded = true

    local path = tes3mp.GetModDir() .. "/custom/LAS_quest_items.json"
    if not tes3mp.DoesFileExist(path) then return end

    local ok, data = pcall(jsonInterface.load, "custom/LAS_quest_items.json")
    if not ok or type(data) ~= "table" then return end

    local count = 0
    for _, object in pairs(data) do
        if type(object) == "table" and object.cell ~= nil and object.uniqueIndex ~= nil and object.refId ~= nil then
            local key = lower(object.cell) .. "|" .. tostring(object.uniqueIndex) .. "|" .. lower(object.refId)
            legacyWorldHints[key] = true
            count = count + 1
        end
    end

    if count > 0 then
        tes3mp.LogMessage(enumerations.log.INFO,
            "[ArenaMP Core] Imported " .. count .. " legacy LAS QIP source hints for migration")
    end
end

function questItemPhasing.EnsurePlayerData(pid)
    return ensurePlayerData(pid)
end

function questItemPhasing.IsLegacyWorldSource(cellDescription, object)
    loadLegacyHints()
    if object == nil then return false end
    local key = lower(cellDescription) .. "|" .. tostring(object.uniqueIndex) .. "|" .. lower(object.refId)
    return legacyWorldHints[key] == true
end

-- ---------------------------------------------------------------------------
-- World sources
-- ---------------------------------------------------------------------------

function questItemPhasing.IsWorldQuestSource(cellDescription, object)
    if object == nil then return false end
    if not questIndexStore.IsTrusted() then return false end

    -- X058: a world-source decision must be tied to the exact authored reference.
    -- eventHandler normalizes this before calling us; keep the guard here as well
    -- so any future caller cannot accidentally phase an identity-less deletion.
    if object.uniqueIndex == nil then return false end

    local legacySource = questItemPhasing.IsLegacyWorldSource(cellDescription, object)

    -- A quest record dropped by a player is ordinary shared loot. Without this
    -- guard a player could drop a claimed quest item, pick it back up and create
    -- another personal copy forever. Legacy LAS hints are exact authored source
    -- identities and intentionally remain eligible for migration.
    local cell = LoadedCells ~= nil and LoadedCells[cellDescription] or nil
    if not legacySource and cell ~= nil and cell.data ~= nil and cell.data.objectData ~= nil then
        local stored = cell.data.objectData[object.uniqueIndex]
        if stored ~= nil and stored.droppedByPlayer == true then
            return false
        end
    end

    if questIndexStore.IsQuestItem(object.refId) then
        return true
    end
    return legacySource
end

function questItemPhasing.ClaimWorld(pid, cellDescription, object)
    local data = ensurePlayerData(pid)
    if data == nil or object == nil then return false end
    if not questItemPhasing.IsWorldQuestSource(cellDescription, object) then return false end

    local sourceId = worldSourceId(cellDescription, object.uniqueIndex, object.refId)
    local bucket = getBucket(data, cellDescription, true)

    if bucket[sourceId] == nil then
        bucket[sourceId] = {
            kind = "world",
            cell = cellDescription,
            uniqueIndex = object.uniqueIndex,
            refId = object.refId,
            count = math.max(1, num(object.count, 1))
        }
        quicksavePlayer(pid)
        tes3mp.LogAppend(enumerations.log.INFO,
            "[ArenaMP Core] " .. logicHandler.GetChatName(pid) .. " claimed world source " ..
            object.refId .. " " .. object.uniqueIndex)
    end
    return true
end

function questItemPhasing.SendWorldDelete(pid, cellDescription, object)
    if Players[pid] == nil or object == nil then return end
    local splitIndex = tostring(object.uniqueIndex):split("-")
    if splitIndex[1] == nil or splitIndex[2] == nil then return end

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(cellDescription)
    tes3mp.SetObjectRefId(object.refId)
    tes3mp.SetObjectRefNum(splitIndex[1])
    tes3mp.SetObjectMpNum(splitIndex[2])
    tes3mp.AddObject()
    tes3mp.SendObjectDelete(false)
end

function questItemPhasing.LoadWorldClaims(pid, cellDescription)
    local data = ensurePlayerData(pid)
    local bucket = getBucket(data, cellDescription, false)
    if bucket == nil then return end

    local count = 0
    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(cellDescription)

    for _, claim in pairs(bucket) do
        if type(claim) == "table" and claim.kind == "world" and
            claim.uniqueIndex ~= nil and claim.refId ~= nil then
            local splitIndex = tostring(claim.uniqueIndex):split("-")
            if splitIndex[1] ~= nil and splitIndex[2] ~= nil then
                tes3mp.SetObjectRefId(claim.refId)
                tes3mp.SetObjectRefNum(splitIndex[1])
                tes3mp.SetObjectMpNum(splitIndex[2])
                tes3mp.AddObject()
                count = count + 1
            end
        end
    end

    if count > 0 then
        tes3mp.SendObjectDelete(false)
    end
end

-- ---------------------------------------------------------------------------
-- Container sources
-- ---------------------------------------------------------------------------

local function getPhaseCount(item)
    if item == nil then return 0 end
    if item.questPhaseCount ~= nil then
        return math.max(0, math.min(num(item.count, 0), num(item.questPhaseCount, 0)))
    end
    if item.questPhaseEligible == true then
        return math.max(0, num(item.count, 0))
    end
    return 0
end

function questItemPhasing.GetContainerPhaseCount(item)
    return getPhaseCount(item)
end

function questItemPhasing.IsContainerQuestSource(item)
    if item == nil then return false end
    return item.questPhaseEligible == true and getPhaseCount(item) > 0
end

--- Establish (or extend) the canonical phaseable subset of a stack.
--- Only ever called for authored/scripted content, never for a player action.
function questItemPhasing.MarkContainerSource(item, phaseCount)
    if item == nil then return end

    -- Capture the old canonical subset before toggling eligibility. Otherwise a
    -- previously shared stack would instantly become 100% phaseable when a
    -- scripted quest item is later added to that same stack.
    local currentPhase = getPhaseCount(item)

    item.questPhaseEligible = true
    item.questPhaseExplicitShared = false
    item.questSourceId = nil

    local requested = math.max(0, num(phaseCount, 0))
    item.questPhaseCount = math.min(math.max(0, num(item.count, 0)), currentPhase + requested)
end

--- A player put a quest record into a container: shared loot, not a new source.
function questItemPhasing.MarkContainerSharedDrop(item)
    if item == nil then return end
    if item.questPhaseEligible == true or getPhaseCount(item) > 0 then
        -- Mixed stack: preserve the canonical phaseable portion while the newly
        -- dropped units remain part of the ordinary shared portion.
        item.questPhaseCount = getPhaseCount(item)
        return
    end

    item.questPhaseEligible = false
    item.questPhaseCount = 0
    item.questSourceId = nil
    item.questPhaseExplicitShared = true
end

--- One-time stamping for inventories recorded before X013, and a safety net for
--- any path that adds authored content without going through SET. Runs only on
--- items the server itself classifies as quest records.
function questItemPhasing.StampInventory(cellDescription, uniqueIndex, inventory)
    if not questIndexStore.IsTrusted() or type(inventory) ~= "table" then return end

    for _, item in pairs(inventory) do
        if type(item) == "table" and item.questPhaseEligible == nil and
            item.questPhaseExplicitShared ~= true and questIndexStore.IsQuestItem(item.refId) then
            questItemPhasing.MarkContainerSource(item, num(item.count, 0))
            tes3mp.LogAppend(enumerations.log.INFO,
                "[ArenaMP Core] Stamped pre-X013 quest source " .. tostring(item.refId) ..
                " in " .. tostring(uniqueIndex) .. " of " .. tostring(cellDescription))
        end
    end
end

function questItemPhasing.GetContainerClaimCount(pid, cellDescription, uniqueIndex, item)
    local data = ensurePlayerData(pid)
    local bucket = getBucket(data, cellDescription, false)
    if bucket == nil or item == nil then return 0 end

    local claim = bucket[containerSourceId(cellDescription, uniqueIndex, item)]
    if claim == nil then return 0 end
    return math.max(0, num(claim.count, 0))
end

--- Consume up to actionCount units of a phaseable source for one player.
--- Returns how many canonical (non-shared) units were claimed.
function questItemPhasing.ClaimContainer(pid, cellDescription, uniqueIndex, item, actionCount)
    local data = ensurePlayerData(pid)
    if data == nil or item == nil then return 0 end
    if not questItemPhasing.IsContainerQuestSource(item) then return 0 end

    local sourceId = containerSourceId(cellDescription, uniqueIndex, item)
    local bucket = getBucket(data, cellDescription, true)
    local current = bucket[sourceId]

    local alreadyClaimed = current ~= nil and math.max(0, num(current.count, 0)) or 0
    local available = math.max(0, getPhaseCount(item) - alreadyClaimed)
    local accepted = math.min(math.max(0, num(actionCount, 0)), available)

    if accepted <= 0 then return 0 end

    if current == nil then
        current = {
            kind = "container",
            cell = cellDescription,
            uniqueIndex = uniqueIndex,
            refId = item.refId,
            charge = item.charge,
            enchantmentCharge = item.enchantmentCharge,
            soul = item.soul,
            poisonId = item.poisonId,
            poisonCharges = item.poisonCharges,
            count = 0
        }
        bucket[sourceId] = current
    end

    current.count = alreadyClaimed + accepted
    quicksavePlayer(pid)

    tes3mp.LogAppend(enumerations.log.INFO,
        "[ArenaMP Core] " .. logicHandler.GetChatName(pid) .. " claimed " .. accepted .. "x " ..
        tostring(item.refId) .. " from container " .. tostring(uniqueIndex))

    return accepted
end

-- Y107: pure variants for a caller's transaction copy. No profile save or packet
-- is emitted here: business state and claims must reach the same durable commit.
local function copyClaimData(value)
    if type(value)~="table" then return value end
    local out={};for k,v in pairs(value) do out[k]=copyClaimData(v) end;return out
end
local function profileClaims(profile,create)
    local data=profile.questItemPhasing
    if data==nil then
        if not create then return nil end
        data={version=2,claims={}};profile.questItemPhasing=data
    end
    if data.version~=2 then
        if not create then data=copyClaimData(data) end
        migrate(data)
    end
    if data.claims==nil then
        if not create then return nil end
        data.claims={}
    end
    return data
end
function questItemPhasing.GetVisibleContainerCountForData(profile,cellDescription,uniqueIndex,item)
    if item==nil then return 0 end
    local count=math.max(0,num(item.count,0));local phase=getPhaseCount(item)
    if phase<=0 then return count end
    local bucket=getBucket(profileClaims(profile,false),cellDescription,false)
    local claim=bucket and bucket[containerSourceId(cellDescription,uniqueIndex,item)]
    return math.max(0,count-math.min(phase,math.max(0,num(claim and claim.count,0))))
end
function questItemPhasing.ClaimContainerForData(profile,cellDescription,uniqueIndex,item,requested)
    if not questItemPhasing.IsContainerQuestSource(item) then return 0 end
    local data=profileClaims(profile,true);local bucket=getBucket(data,cellDescription,true)
    local sourceId=containerSourceId(cellDescription,uniqueIndex,item);local current=bucket[sourceId]
    local claimed=math.max(0,num(current and current.count,0))
    local accepted=math.min(math.max(0,num(requested,0)),math.max(0,getPhaseCount(item)-claimed))
    if accepted<=0 then return 0 end
    if current==nil then
        current={kind="container",cell=cellDescription,uniqueIndex=uniqueIndex,refId=item.refId,
            charge=item.charge,enchantmentCharge=item.enchantmentCharge,soul=item.soul,
            poisonId=item.poisonId,poisonCharges=item.poisonCharges,count=0}
        bucket[sourceId]=current
    end
    current.count=claimed+accepted
    return accepted
end

function questItemPhasing.GetVisibleContainerCount(pid, cellDescription, uniqueIndex, item)
    if item == nil then return 0 end
    local count = math.max(0, num(item.count, 0))
    if count == 0 then return 0 end

    local phaseCount = getPhaseCount(item)
    if phaseCount <= 0 then return count end

    local claimed = questItemPhasing.GetContainerClaimCount(pid, cellDescription, uniqueIndex, item)
    if claimed <= 0 then return count end

    -- Only the canonical phaseable subset can be hidden. Shared units that were
    -- later dropped into a mixed stack remain visible to every player.
    return math.max(0, count - math.min(claimed, phaseCount))
end

return questItemPhasing
