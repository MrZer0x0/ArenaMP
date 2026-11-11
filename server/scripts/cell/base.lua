require("patterns")

contentFixer = require("contentFixer")
tableHelper = require("tableHelper")
inventoryHelper = require("inventoryHelper")
packetBuilder = require("packetBuilder")
local questItemPhasing = require("questItemPhasing")
local questIndexStore = require("questIndexStore")
local actorLootRepair = require("actorLootRepair")
local actorApparelGuard = require("actorApparelGuard")
local arenaSummonRules = require("arenaSummonRules")

local BaseCell = class("BaseCell")

-- Y092-EQ: NPC apparel (armor/clothing/jewelry, slots 0..15) must survive the
-- short authority/equipment resync that follows ActorCellChange. Weapon, shield
-- and ammunition slots 16..18 remain fully dynamic for combat.
local APPAREL_SLOT_MAX = enumerations.equipment.BELT or 15

local function copyEquipmentItem(item)
    if type(item) ~= "table" then return nil end
    return {
        refId = item.refId,
        count = item.count,
        charge = item.charge,
        enchantmentCharge = item.enchantmentCharge,
        poisonId = item.poisonId,
        poisonCharges = item.poisonCharges
    }
end

local function makeApparelSnapshot(equipment)
    local snapshot = {}
    for slot = 0, APPAREL_SLOT_MAX do
        if equipment ~= nil and equipment[slot] ~= nil then
            snapshot[slot] = copyEquipmentItem(equipment[slot])
        end
    end
    return snapshot
end

-- X022: OpenMW uses INT_MIN exterior coordinates internally while a CellStore is
-- in a transitional/unloaded state. They are never legitimate TES3 cells.
local function isTransitionalExteriorCell(cellDescription)
    local gridX, gridY = string.match(cellDescription or "", "^%s*(-?%d+)%s*,%s*(-?%d+)%s*$")
    return gridX ~= nil and
        (tonumber(gridX) == -2147483648 or tonumber(gridY) == -2147483648)
end

function BaseCell:__init(cellDescription)

    self.data =
    {
        entry = {
            description = cellDescription,
            creationTime = os.time()
        },
        loadState = {
            hasFullActorList = false,
            hasFullContainerData = false
        },
        lastVisit = {},
        objectData = {},
        packets = {},
        recordLinks = {}
    }

    self:EnsurePacketTables()

    self.description = cellDescription
    self.visitors = {}
    -- Y100: delivery markers live only for the current loaded-cell visit.
    self.initialDataSentTo = {}
    self.authority = nil

    self.isRequestingContainerData = false
    self.containerRequestPid = nil
    self.containerRequestTime = nil
    -- A corpse/player-created container snapshot is not a FULL cell snapshot.
    -- Keep its retry state separate from the initial cell handshake.
    self.pendingContainerRefreshes = {}
    self.deferredContainerRequests = {}

    self.isRequestingActorList = false
    self.actorListRequestPid = nil

    self.isResetting = false

    self.unusableContainerUniqueIndexes = {}

    -- Y092-EQ: transient only; never serialize hand-off snapshots into cell JSON.
    self.actorEquipmentTransferGuards = {}

    self.isExterior = false

    if string.match(cellDescription, patterns.exteriorCell) then
        self.isExterior = true

        local _, _, gridX, gridY = string.find(cellDescription, patterns.exteriorCell)

        self.gridX = tonumber(gridX)
        self.gridY = tonumber(gridY)
    end
end

function BaseCell:ContainsPosition(posX, posY)

    local cellSize = 8192

    if self.isExterior then
        local correctGridX = math.floor(posX / cellSize)
        local correctGridY = math.floor(posY / cellSize)

        if self.gridX ~= correctGridX or self.gridY ~= correctGridY then
            return false
        end
    end

    return true
end

function BaseCell:HasEntry()
    return self.hasEntry
end

-- Iterate through the packets table and ensure all packet types are included in it
function BaseCell:EnsurePacketTables()

    if self.data.packets == nil then self.data.packets = {} end

    for _, packetType in pairs(config.cellPacketTypes or {}) do
        if self.data.packets[packetType] == nil then
            self.data.packets[packetType] = {}
        end
    end

    -- Y020: move/rotate were added by the RP placement layer after many cell
    -- save files had already been created.  A server can also accidentally run
    -- a newer cell/base.lua with an older config.lua.  Do not let either case
    -- abort LoadInitialCellData before actor list/AI/position packets are sent.
    -- These arrays are mandatory for the loader regardless of config version.
    if type(self.data.packets.move) ~= "table" then self.data.packets.move = {} end
    if type(self.data.packets.rotate) ~= "table" then self.data.packets.rotate = {} end
end

-- Iterate through saved packets and ensure the object uniqueIndexes they refer to
-- actually exist
function BaseCell:EnsurePacketValidity()

    for packetType, packetArray in pairs(self.data.packets) do
        for arrayIndex, uniqueIndex in pairs(self.data.packets[packetType]) do
            if self.data.objectData[uniqueIndex] == nil then
                tableHelper.removeValue(self.data.packets[packetType], uniqueIndex)
            end
        end
    end
end

-- Adding record links to cells is special because we'll keep track of the uniqueIndex
-- of every object that uses a particular generated record
function BaseCell:AddLinkToRecord(storeType, recordId, uniqueIndex)

    if self.data.recordLinks == nil then self.data.recordLinks = {} end

    local recordStore = RecordStores[storeType]

    if recordStore ~= nil then

        local recordLinks = self.data.recordLinks

        if recordLinks[storeType] == nil then recordLinks[storeType] = {} end
        if recordLinks[storeType][recordId] == nil then recordLinks[storeType][recordId] = {} end

        if not tableHelper.containsValue(self.data.recordLinks[storeType][recordId], uniqueIndex) then
            table.insert(self.data.recordLinks[storeType][recordId], uniqueIndex)
        end

        recordStore:AddLinkToCell(recordId, self)
        recordStore:QuicksaveToDrive()
    end
end

function BaseCell:RemoveLinkToRecord(storeType, recordId, uniqueIndex)

    local recordStore = RecordStores[storeType]

    if recordStore ~= nil then

        local recordLinks = self.data.recordLinks

        if recordLinks ~= nil and recordLinks[storeType] ~= nil and recordLinks[storeType][recordId] ~= nil then

            local linkIndex = tableHelper.getIndexByValue(recordLinks[storeType][recordId], uniqueIndex)

            if linkIndex ~= nil then
                recordLinks[storeType][recordId][linkIndex] = nil
            end

            local remainingIndexCount = tableHelper.getCount(recordLinks[storeType][recordId])

            if remainingIndexCount == 0 then
                recordLinks[storeType][recordId] = nil

                recordStore:RemoveLinkToCell(recordId, self)
                recordStore:QuicksaveToDrive()
            end
        end
    end
end

function BaseCell:ClearRecordLinks()

    for storeType, storeLinksTable in pairs(self.data.recordLinks) do
        
        local recordStore = RecordStores[storeType]

        if recordStore ~= nil then

            for recordId, uniqueIndexes in pairs(storeLinksTable) do

                for _, uniqueIndex in pairs(uniqueIndexes) do

                    self:RemoveLinkToRecord(storeType, recordId, uniqueIndex)
                end
            end
        end
    end
end

function BaseCell:GetVisitorCount()
    return tableHelper.getCount(self.visitors)
end

-- Y029: FOLLOW/ESCORT has one network target, but a party escort should not
-- become inert merely because the original quest taker is not the member who
-- currently owns/loads this cell. Prefer the authored target while it is an
-- actual visitor of this cell; otherwise fail over to the nearest online member
-- of the same ArenaMP group. excludedPid is used for an immediately dead/leaving
-- player before all engine-side state has caught up.
-- Y030: work out which ArenaMP group a saved escort belongs to, at the moment
-- the question is actually asked.
--
-- packetReader stamps ai.groupId while reading the ActorAI packet, from whatever
-- group the target player happened to be in right then. An ActorAI packet is
-- sent once, when the quest script issues AiFollow/AiEscort, and is never
-- reissued - so the usual order of play (take the quest, then invite a friend)
-- leaves groupId nil forever and the whole group failover below never runs. A
-- player who later switches group keeps a stale id for the same reason.
--
-- The saved id is therefore treated as a hint: if it is missing, fall back to
-- the target player's current group.
function BaseCell:ResolveEscortGroupId(ai)
    if ai == nil then return nil end

    if ai.groupId ~= nil then
        return tostring(ai.groupId)
    end

    if ai.targetPlayer ~= nil and logicHandler.IsPlayerNameLoggedIn(ai.targetPlayer) then
        local owner = logicHandler.GetPlayerByName(ai.targetPlayer)
        local customVariables = owner ~= nil and owner.data ~= nil and owner.data.customVariables
        local groupPrefs = customVariables and customVariables.arenampGroup
        if groupPrefs ~= nil and groupPrefs.id ~= nil then
            return tostring(groupPrefs.id)
        end
    end

    return nil
end

-- Y030: returns targetPid, wantedGroupId. The second value lets the caller tell
-- "this escort has a group and nobody from it is here" from "this escort has no
-- group at all", which need different treatment.
function BaseCell:ResolveSavedAITargetPid(ai, actorUniqueIndex, excludedPid)
    if ai == nil then return nil, nil end

    local function isCurrentVisitor(candidatePid)
        return candidatePid ~= nil and candidatePid ~= excludedPid and
            Players[candidatePid] ~= nil and Players[candidatePid]:IsLoggedIn() and
            tableHelper.containsValue(self.visitors, candidatePid)
    end

    local originalPid = nil
    if ai.targetPlayer ~= nil and logicHandler.IsPlayerNameLoggedIn(ai.targetPlayer) then
        originalPid = logicHandler.GetPlayerByName(ai.targetPlayer).pid
        if isCurrentVisitor(originalPid) then
            return originalPid, self:ResolveEscortGroupId(ai)
        end
    end

    local wantedGroupId = self:ResolveEscortGroupId(ai)
    if wantedGroupId ~= nil then
        local actorLocation = nil
        if actorUniqueIndex ~= nil and self.data.objectData[actorUniqueIndex] ~= nil then
            actorLocation = self.data.objectData[actorUniqueIndex].location
        end

        local bestPid = nil
        local bestDistanceSquared = nil
        for _, candidatePid in pairs(self.visitors) do
            local player = Players[candidatePid]
            if isCurrentVisitor(candidatePid) and player ~= nil then
                local customVariables = player.data.customVariables
                local groupPrefs = customVariables and customVariables.arenampGroup
                if groupPrefs ~= nil and tostring(groupPrefs.id or "") == wantedGroupId then
                    local distanceSquared = 0
                    if actorLocation ~= nil then
                        local dx = (tonumber(tes3mp.GetPosX(candidatePid)) or 0) - (tonumber(actorLocation.posX) or 0)
                        local dy = (tonumber(tes3mp.GetPosY(candidatePid)) or 0) - (tonumber(actorLocation.posY) or 0)
                        local dz = (tonumber(tes3mp.GetPosZ(candidatePid)) or 0) - (tonumber(actorLocation.posZ) or 0)
                        distanceSquared = dx * dx + dy * dy + dz * dz
                    end

                    if bestPid == nil or distanceSquared < bestDistanceSquared or
                        (distanceSquared == bestDistanceSquared and candidatePid < bestPid) then
                        bestPid = candidatePid
                        bestDistanceSquared = distanceSquared
                    end
                end
            end
        end

        -- A group escort with no group member in this cell should wait here. Do
        -- not retarget it to the original player in another cell; the saved
        -- authored package remains intact and will be restored when the group
        -- returns.
        return bestPid, wantedGroupId
    end

    -- Y030: originalPid has already failed isCurrentVisitor() by this point, so
    -- returning it would hand out a FOLLOW aimed at a player who is not in this
    -- cell. That is the shape of desync this whole mechanism exists to avoid, so
    -- report "no usable target" instead and let the caller skip the packet.
    return nil, nil
end

-- Reissue saved group FOLLOW/ESCORT packages when their effective player target
-- can change (death or visitor leaving). This is server-originated only: a
-- temporary CANCEL makes the NPC wait but never erases the persisted authored
-- package in objectData.
function BaseCell:RefreshSavedGroupEscortTargets(excludedPid)
    if tableHelper.getCount(self.visitors) == 0 or self.data.packets == nil or
        type(self.data.packets.ai) ~= "table" then
        return
    end

    local attachedPid = nil
    for _, visitorPid in pairs(self.visitors) do
        attachedPid = visitorPid
        break
    end
    if attachedPid == nil then return end

    local actorCount = 0
    tes3mp.ClearActorList()
    tes3mp.SetActorListPid(attachedPid)
    tes3mp.SetActorListCell(self.description)

    for _, uniqueIndex in pairs(self.data.packets.ai) do
        local objectData = self.data.objectData[uniqueIndex]
        local ai = objectData and objectData.ai
        -- Y030: the gate is the package type, not the presence of a stored
        -- groupId, because that id is now resolved lazily (see
        -- ResolveEscortGroupId). Whether a group is in play is decided below.
        if ai ~= nil and
            (ai.action == enumerations.ai.FOLLOW or ai.action == enumerations.ai.ESCORT) then

            local targetPid, wantedGroupId = self:ResolveSavedAITargetPid(ai, uniqueIndex, excludedPid)

            if targetPid ~= nil then
                packetBuilder.AddAIActor(uniqueIndex, targetPid, ai)
                tes3mp.LogAppend(enumerations.log.INFO, "- Group escort " .. uniqueIndex ..
                    " now follows " .. logicHandler.GetChatName(targetPid))
                actorCount = actorCount + 1
            elseif wantedGroupId ~= nil then
                packetBuilder.AddAIActor(uniqueIndex, nil, {
                    action = enumerations.ai.CANCEL,
                    shouldRepeat = false
                })
                tes3mp.LogAppend(enumerations.log.INFO, "- Group escort " .. uniqueIndex ..
                    " is waiting for a group member in " .. self.description)
                actorCount = actorCount + 1
            end
            -- No group at all: this is an ordinary single-player escort whose
            -- owner is simply walking out of the cell with the NPC in tow.
            -- Cancelling it here would break the follower mid-transition, so the
            -- saved package is left completely alone.
        end
    end

    if actorCount > 0 then
        -- Include the attached visitor as well as the rest of the cell so every
        -- client switches target in the same server tick.
        tes3mp.SendActorAI(true)
    end
end

function BaseCell:AddVisitor(pid)

    -- Only add new visitor if we don't already have them
    if not tableHelper.containsValue(self.visitors, pid) then
        table.insert(self.visitors, pid)
        self.initialDataSentTo = self.initialDataSentTo or {}
        self.initialDataSentTo[pid] = nil

        -- Also add a record to the player's list of loaded cells
        Players[pid]:AddCellLoaded(self.description)

        self:LoadGeneratedRecords(pid)

        -- H020: generated home cells may have been reconstructed from baseId.
        -- Reapply the house-owned authored-furniture tombstones before any
        -- initial/persistent state is sent, so removed fixtures never flash or
        -- reappear when the player reconnects.
        local housing = rawget(_G, "houseHelper")
        if housing and type(housing.RestoreDeletedFurniture) == "function" then
            local ok, err = pcall(housing.RestoreDeletedFurniture, self)
            if not ok then
                tes3mp.LogAppend(enumerations.log.ERROR,
                    "- Failed to restore home furniture tombstones: " .. tostring(err))
            end
        end
        -- Y089: portable-bedroll scripts leave disabled dynamic source objects
        -- behind. Purge those and any exact co-located activator copies before
        -- the first instance payload reaches the client.
        if housing and type(housing.RepairBedrollArtifacts) == "function" then
            local ok, err = pcall(housing.RepairBedrollArtifacts, self, pid)
            if not ok then
                tes3mp.LogAppend(enumerations.log.ERROR,
                    "- Failed to repair home bedroll artifacts: " .. tostring(err))
            end
        end

        local shouldSendInfo = false
        local lastVisitTimestamp = self.data.lastVisit[Players[pid].accountName]

        -- If this player has never been in this cell, they should be
        -- sent its cell data
        if lastVisitTimestamp == nil then
            shouldSendInfo = true
        -- Otherwise, send them the cell data only if they haven't
        -- visited since last connecting to the server
        elseif Players[pid].data.timestamps.lastLogin > lastVisitTimestamp then
            shouldSendInfo = true
        end

        if shouldSendInfo == true then
            -- First, fix whatever quest problems exist in this cell
            contentFixer.FixCell(pid, self.description)

            self:LoadInitialCellData(pid)
        else
            -- Y029: the previous same-login optimization skipped every saved
            -- persistent change when a player revisited a cell. Changes made by
            -- another visitor while this client was away could therefore leave
            -- one client with an alive/visible NPC and another with none. Replay
            -- only actor-relevant persistent state here; do not resend containers
            -- or player-placed clutter.
            self:LoadPersistentActorState(pid)
        end

        self:LoadMomentaryCellData(pid)

        self:EnsureFullContainerData(pid)
        self:RetryUnusableContainers(pid)

        if not self:HasFullActorList() and not self.isRequestingActorList then
            tes3mp.LogAppend(enumerations.log.INFO, "- Requesting actor list")
            self:RequestActorList(pid)
        end
    end
end

function BaseCell:RemoveVisitor(pid)

    -- Only remove visitor if they are actually recorded as one
    if tableHelper.containsValue(self.visitors, pid) then

        tableHelper.removeValue(self.visitors, pid)
        if self.initialDataSentTo then self.initialDataSentTo[pid] = nil end

        -- Also remove the record from the player's list of loaded cells
        Players[pid]:RemoveCellLoaded(self.description)

        -- Remember when this visitor left
        self:SaveLastVisit(Players[pid].accountName)

        -- Were we waiting on a container request from this pid?
        if self.isRequestingContainerData == true and self.containerRequestPid == pid then
            self.isRequestingContainerData = false
            self.containerRequestPid = nil
            self.containerRequestTime = nil
        end

        -- Were we waiting on an actorList request from this pid?
        if self.isRequestingActorList == true and self.actorListRequestPid == pid then
            self.isRequestingActorList = false
            self.actorListRequestPid = nil
        end

        -- Y029: if this visitor was the effective target of a persisted group
        -- escort, immediately hand the follower to another member still in the
        -- cell instead of waiting for an authority change or reload.
        self:RefreshSavedGroupEscortTargets(pid)
    end
end

function BaseCell:GetAuthority()
    return self.authority
end

function BaseCell:SetAuthority(pid)
    -- A respawn-locked peer cannot publish NPC simulation packets. Hand the
    -- cell to another visitor rather than freezing NPCs for living players.
    if bountyRespawn and bountyRespawn.Pending(Players[pid]) then
        local replacement
        for _, candidate in pairs(self.visitors) do
            if Players[candidate] and Players[candidate]:IsLoggedIn() and not bountyRespawn.Pending(Players[candidate]) then
                replacement = candidate
                break
            end
        end
        if replacement == nil then return end
        pid = replacement
    end
    self.authority = pid
    tes3mp.LogMessage(enumerations.log.INFO, "Authority of cell " .. self.data.entry.description ..
        " is now " .. logicHandler.GetChatName(pid))

    -- The new authority is already a live observer of this loaded cell and has
    -- been receiving position/stats continuously. Avoid replaying a second full
    -- momentary snapshot here: on busy cells that burst competed with combat and
    -- movement traffic. SendActorAuthority still replays the cached AI state
    -- directly before switching ownership.
    self:LoadActorAuthority(pid)
end

-- Check whether an object is in this cell
function BaseCell:ContainsObject(uniqueIndex)
    if self.data.objectData[uniqueIndex] ~= nil and self.data.objectData[uniqueIndex].refId ~= nil then
        return true
    end

    return false
end

function BaseCell:HasFullContainerData()

    if self.data.loadState.hasFullContainerData == true then
        return true
    end

    return false
end

-- On a missed client reply (unloaded client cell / interrupted deferred reply),
-- the former one-shot flag could strand this cell in "synchronizing" forever.
-- Retry only when there is a live visitor, with a per-cell rate limit. Never
-- interpret an arbitrary inventory transaction as the missing full snapshot.
function BaseCell:EnsureFullContainerData(pid)
    if self:HasFullContainerData() then return false end
    if pid == nil or not tableHelper.containsValue(self.visitors, pid) then return false end

    local now = os.time()
    local lastRequest = self.containerRequestTime
    if self.isRequestingContainerData and lastRequest ~= nil and now - lastRequest < 4 then
        return false
    end

    tes3mp.LogAppend(enumerations.log.INFO,
        "- Requesting full container snapshot for " .. self.description .. " from " .. tostring(pid))
    self:RequestContainers(pid)
    return true
end

-- Death and placed-container refreshes may be lost if the original requester
-- transitions cells. Their individual retry clocks must not block the full
-- snapshot request or turn an actor's reply into a completed cell handshake.
function BaseCell:RetryUnusableContainers(pid, requestedIndex)
    if not self:HasFullContainerData() then return false end
    if pid == nil or not tableHelper.containsValue(self.visitors, pid) then return false end
    local now = os.time()
    local pending = {}
    self.pendingContainerRefreshes = self.pendingContainerRefreshes or {}
    for _, uniqueIndex in ipairs(self.unusableContainerUniqueIndexes) do
        if requestedIndex == nil or requestedIndex == uniqueIndex then
            local lastRequest = self.pendingContainerRefreshes[uniqueIndex]
            if lastRequest == nil or now - lastRequest >= 4 then
                self.pendingContainerRefreshes[uniqueIndex] = now
                table.insert(pending, uniqueIndex)
            end
        end
    end
    if #pending == 0 then return false end
    self:RequestContainers(pid, pending)
    return true
end

function BaseCell:HasFullActorList()

    if self.data.loadState.hasFullActorList == true then
        return true
    end

    return false
end

function BaseCell:InitializeObjectData(uniqueIndex, refId)

    if uniqueIndex ~= nil and refId ~= nil and self.data.objectData[uniqueIndex] == nil then
        self.data.objectData[uniqueIndex] = {}
        self.data.objectData[uniqueIndex].refId = refId
    end
end

function BaseCell:DeleteObjectData(uniqueIndex)

    if self.data.objectData[uniqueIndex] == nil then
        return
    end

    -- Is this a player's summon? If so, remove it from the summons tracked
    -- for the player
    local summon = self.data.objectData[uniqueIndex].summon

    if summon ~= nil then
        if summon.summoner.playerName ~= nil and logicHandler.IsPlayerNameLoggedIn(summon.summoner.playerName) then
            logicHandler.GetPlayerByName(summon.summoner.playerName).summons[uniqueIndex] = nil
        end
    end

    -- Delete all packets associated with an object
    for packetIndex, packetType in pairs(self.data.packets) do
        tableHelper.removeValue(self.data.packets[packetIndex], uniqueIndex)
    end

    -- Delete all object data
    self.data.objectData[uniqueIndex] = nil
end

function BaseCell:MoveObjectData(uniqueIndex, newCell)
    if self.description == newCell.description then return false end
    local moved = self.data.objectData[uniqueIndex]
    if moved == nil then return false end -- stale moves must never erase the destination

    -- The destination may still hold the original-cell redirect or an old
    -- deletion marker. Replace those packet memberships, not just objectData.
    for _, packet in pairs(newCell.data.packets) do
        tableHelper.removeValue(packet, uniqueIndex)
    end
    for packetIndex, packet in pairs(self.data.packets) do
        if tableHelper.containsValue(packet, uniqueIndex) then
            newCell.data.packets[packetIndex] = newCell.data.packets[packetIndex] or {}
            tableHelper.insertValueIfMissing(newCell.data.packets[packetIndex], uniqueIndex)
            tableHelper.removeValue(packet, uniqueIndex)
        end
    end
    newCell.data.objectData[uniqueIndex] = moved
    self.data.objectData[uniqueIndex] = nil
    return true
end

function BaseCell:SaveLastVisit(playerName)
    self.data.lastVisit[playerName] = os.time()
end

function BaseCell:SaveObjectsDeleted(objects)

    local temporaryLoadedCells = {}

    for uniqueIndex, object in pairs(objects) do

        local refId = object.refId

        -- Check whether this object was moved to this cell from another one
        local wasMovedHere = tableHelper.containsValue(self.data.packets.cellChangeFrom, uniqueIndex)

        if wasMovedHere == true then

            local originalCellDescription = self.data.objectData[uniqueIndex].cellChangeFrom

            -- If the new cell is not loaded, load it temporarily
            if LoadedCells[originalCellDescription] == nil then
                logicHandler.LoadCell(originalCellDescription)
                table.insert(temporaryLoadedCells, originalCellDescription)
            end

            local originalCell = LoadedCells[originalCellDescription]

            originalCell:DeleteObjectData(uniqueIndex)
            table.insert(originalCell.data.packets.delete, uniqueIndex)
            originalCell:InitializeObjectData(uniqueIndex, refId)

            self:DeleteObjectData(uniqueIndex)

        else
            -- Check whether this is a placed or spawned object
            local wasPlacedHere = tableHelper.containsValue(self.data.packets.place, uniqueIndex) or
                tableHelper.containsValue(self.data.packets.spawn, uniqueIndex)

            self:DeleteObjectData(uniqueIndex)

            -- If this is an object from the game's data files, we should keep sending ObjectDelete
            -- packets for it to visitors
            if not wasPlacedHere then

                table.insert(self.data.packets.delete, uniqueIndex)
                self:InitializeObjectData(uniqueIndex, refId)

            -- If this is an object based on a generated record, we need to remove the link to it
            elseif logicHandler.IsGeneratedRecord(refId) then
                local recordStore = logicHandler.GetRecordStoreByRecordId(refId)

                if recordStore ~= nil then
                    self:RemoveLinkToRecord(recordStore.storeType, refId, uniqueIndex)
                end
            end
        end
    end

    -- Go through every temporary loaded cell and unload it
    for arrayIndex, originalCellDescription in pairs(temporaryLoadedCells) do
        logicHandler.UnloadCell(originalCellDescription)
    end
end

function BaseCell:SaveObjectsPlaced(objects)

    for uniqueIndex, object in pairs(objects) do

        local location = object.location

        -- Ensure data integrity before proceeeding
        if tableHelper.getCount(location) == 6 and tableHelper.usesNumericalValues(location) and
            self:ContainsPosition(location.posX, location.posY) then

            local refId = object.refId
            self:InitializeObjectData(uniqueIndex, refId)

            local count = object.count
            local charge = object.charge
            local enchantmentCharge = object.enchantmentCharge
            local soul = object.soul
            local poisonId = object.poisonId or ""
            local poisonCharges = object.poisonCharges or 0
            local goldValue = object.goldValue

            -- Only save count if it isn't the default value of 1
            if count ~= 1 then
                self.data.objectData[uniqueIndex].count = count
            end

            -- Only save charge if it isn't the default value of -1
            if charge ~= -1 then
                self.data.objectData[uniqueIndex].charge = charge
            end

            -- Only save enchantment charge if it isn't the default value of -1
            if enchantmentCharge ~= -1 then
                self.data.objectData[uniqueIndex].enchantmentCharge = enchantmentCharge
            end

            if soul ~= "" then
               self.data.objectData[uniqueIndex].soul = soul
            end

            if poisonId ~= "" and poisonCharges > 0 then
                self.data.objectData[uniqueIndex].poisonId = poisonId
                self.data.objectData[uniqueIndex].poisonCharges = poisonCharges
            else
                self.data.objectData[uniqueIndex].poisonId = nil
                self.data.objectData[uniqueIndex].poisonCharges = nil
            end

            -- Only save goldValue if it isn't the default value of 1
            if goldValue ~= 1 then
                self.data.objectData[uniqueIndex].goldValue = goldValue
            end

            self.data.objectData[uniqueIndex].location = location
            -- Preserve provenance so a quest-record item dropped by a player can
            -- never be mistaken for a reusable canonical quest source later.
            self.data.objectData[uniqueIndex].droppedByPlayer = object.droppedByPlayer == true

            tes3mp.LogAppend(enumerations.log.INFO, "- " .. uniqueIndex .. ", refId: " .. refId ..
                ", count: " .. count .. ", charge: " .. charge .. ", enchantmentCharge: " .. enchantmentCharge ..
                ", soul: " .. soul .. ", poisonId: " .. poisonId .. ", poisonCharges: " .. poisonCharges ..
                ", goldValue: " .. goldValue)

            table.insert(self.data.packets.place, uniqueIndex)

            if logicHandler.IsGeneratedRecord(refId) then
                local recordStore = logicHandler.GetRecordStoreByRecordId(refId)

                if recordStore ~= nil then
                    self:AddLinkToRecord(recordStore.storeType, refId, uniqueIndex)
                end
            end
        end
    end

    self:QuicksaveToDrive()
end

function BaseCell:SaveObjectsSpawned(objects)

    for uniqueIndex, object in pairs(objects) do

        local location = object.location

        -- Ensure data integrity before proceeeding
        if tableHelper.getCount(location) == 6 and tableHelper.usesNumericalValues(location) and
            self:ContainsPosition(location.posX, location.posY) then

            local refId = object.refId
            self:InitializeObjectData(uniqueIndex, refId)

            tes3mp.LogAppend(enumerations.log.INFO, "- " .. uniqueIndex .. ", refId: " .. refId)

            self.data.objectData[uniqueIndex].location = location

            if object.summon ~= nil then
                local summonDuration = object.summon.duration

                if summonDuration > 0 then
                    local summon = {}
                    summon.duration = object.summon.duration
                    summon.effectId = object.summon.effectId
                    summon.spellId = object.summon.spellId
                    summon.startTime = object.summon.startTime
                    summon.summoner = {}

                    local hasPlayerSummoner = object.summon.hasPlayerSummoner

                    if hasPlayerSummoner then
                        local summonerPid = object.summon.summoner.pid
                        tes3mp.LogAppend(enumerations.log.INFO, "- summoned by player " ..
                            logicHandler.GetChatName(summonerPid))

                        -- Track the player and the summon for each other
                        summon.summoner.playerName = object.summon.summoner.playerName

                        Players[summonerPid].summons[uniqueIndex] = refId
                    else
                        summon.summoner.refId = object.summon.summoner.refId
                        summon.summoner.uniqueIndex = object.summon.summoner.uniqueIndex
                        tes3mp.LogAppend(enumerations.log.INFO, "- summoned by actor " .. summon.summoner.uniqueIndex ..
                            ", refId: " .. summon.summoner.refId)
                    end

                    self.data.objectData[uniqueIndex].summon = summon
                end
            end

            table.insert(self.data.packets.spawn, uniqueIndex)
            table.insert(self.data.packets.actorList, uniqueIndex)

            if logicHandler.IsGeneratedRecord(refId) then
                local recordStore = logicHandler.GetRecordStoreByRecordId(refId)

                if recordStore ~= nil then
                    self:AddLinkToRecord(recordStore.storeType, refId, uniqueIndex)
                end
            end
        end
    end
end

function BaseCell:SaveObjectsLocked(objects)

    for uniqueIndex, object in pairs(objects) do

        local refId = object.refId
        local lockLevel = object.lockLevel

        self:InitializeObjectData(uniqueIndex, refId)
        self.data.objectData[uniqueIndex].lockLevel = lockLevel

        tes3mp.LogAppend(enumerations.log.INFO, "- " .. uniqueIndex .. ", refId: " .. refId ..
            ", lockLevel: " .. lockLevel)

        tableHelper.insertValueIfMissing(self.data.packets.lock, uniqueIndex)
    end
end

function BaseCell:SaveObjectsMiscellaneous(objects)

    for uniqueIndex, object in pairs(objects) do

        local refId = object.refId

        self:InitializeObjectData(uniqueIndex, refId)
        self.data.objectData[uniqueIndex].goldPool = object.goldPool
        self.data.objectData[uniqueIndex].lastGoldRestockHour = object.lastGoldRestockHour
        self.data.objectData[uniqueIndex].lastGoldRestockDay = object.lastGoldRestockDay

        tes3mp.LogAppend(enumerations.log.INFO, "- " .. uniqueIndex .. ", refId: " .. refId ..
            ", goldPool: " .. object.goldPool .. ", lastGoldRestockHour: " .. object.lastGoldRestockHour ..
            ", lastGoldRestockDay: " .. object.lastGoldRestockDay)

        tableHelper.insertValueIfMissing(self.data.packets.miscellaneous, uniqueIndex)
    end
end

function BaseCell:SaveObjectTrapsTriggered(objects)

    for uniqueIndex, object in pairs(objects) do

        local refId = object.refId

        self:InitializeObjectData(uniqueIndex, refId)

        tes3mp.LogAppend(enumerations.log.INFO, "- " .. uniqueIndex .. ", refId: " .. refId)

        tableHelper.insertValueIfMissing(self.data.packets.trap, uniqueIndex)
    end
end

function BaseCell:SaveObjectsMoved(objects)

    for uniqueIndex, object in pairs(objects) do
        self:InitializeObjectData(uniqueIndex, object.refId)
        local stored = self.data.objectData[uniqueIndex]
        stored.location = stored.location or {}
        stored.location.posX = object.location.posX
        stored.location.posY = object.location.posY
        stored.location.posZ = object.location.posZ
        tableHelper.insertValueIfMissing(self.data.packets.move, uniqueIndex)
    end
end

function BaseCell:SaveObjectsRotated(objects)

    for uniqueIndex, object in pairs(objects) do
        self:InitializeObjectData(uniqueIndex, object.refId)
        local stored = self.data.objectData[uniqueIndex]
        stored.location = stored.location or {}
        stored.location.rotX = object.location.rotX
        stored.location.rotY = object.location.rotY
        stored.location.rotZ = object.location.rotZ
        tableHelper.insertValueIfMissing(self.data.packets.rotate, uniqueIndex)
    end
end

function BaseCell:SaveObjectsScaled(objects)

    for uniqueIndex, object in pairs(objects) do

        local refId = object.refId
        local scale = object.scale

        self:InitializeObjectData(uniqueIndex, refId)
        self.data.objectData[uniqueIndex].scale = scale

        tes3mp.LogAppend(enumerations.log.INFO, "- " .. uniqueIndex .. ", refId: " .. refId ..
            ", scale: " .. scale)

        tableHelper.insertValueIfMissing(self.data.packets.scale, uniqueIndex)
    end
end

function BaseCell:SaveObjectStates(objects)

    for uniqueIndex, object in pairs(objects) do

        local refId = object.refId
        local state = object.state

        self:InitializeObjectData(uniqueIndex, refId)
        self.data.objectData[uniqueIndex].state = state

        -- Changes are replicated as before, but avoid an INFO disk log for
        -- every CharGen collision/object state at player login.
        tableHelper.insertValueIfMissing(self.data.packets.state, uniqueIndex)
    end
end

function BaseCell:SaveDoorStates(objects)

    for uniqueIndex, object in pairs(objects) do

        local refId = object.refId
        local doorState = object.doorState

        self:InitializeObjectData(uniqueIndex, refId)
        self.data.objectData[uniqueIndex].doorState = doorState

        tableHelper.insertValueIfMissing(self.data.packets.doorState, uniqueIndex)
    end
end

-- Start of AMP change
--
-- Records which script the stored variables belong to, and drops everything previously
-- stored for an object whose script has changed.
--
-- Local variable indices are positions inside one particular script, so once the content
-- files change and a script gains or loses a variable, every index saved under the old
-- version points at the wrong thing. Keeping the script ID lets us notice that and clear
-- the stale values instead of quietly corrupting quest state.
function BaseCell:SaveClientScriptLocals(objects)

    for uniqueIndex, object in pairs(objects) do

        local refId = object.refId

        if refId ~= nil and type(object.variables) == "table" then

            self:InitializeObjectData(uniqueIndex, refId)

            local objectData = self.data.objectData[uniqueIndex]

            if objectData ~= nil then

                local clientScriptId = object.clientScriptId

                if clientScriptId ~= nil and clientScriptId ~= "" then

                    if objectData.clientScriptId ~= nil and
                        string.lower(objectData.clientScriptId) ~= string.lower(clientScriptId) then

                        tes3mp.LogMessage(enumerations.log.WARN, "Clearing saved script locals for " ..
                            uniqueIndex .. " in " .. self.description .. " because its script changed from " ..
                            objectData.clientScriptId .. " to " .. clientScriptId)

                        objectData.variables = nil
                    end

                    objectData.clientScriptId = clientScriptId
                end

                if objectData.variables == nil then
                    objectData.variables = {}
                end

                for variableType, variableTable in pairs(object.variables) do

                    if type(variableTable) == "table" then

                        if objectData.variables[variableType] == nil then
                            objectData.variables[variableType] = {}
                        end

                        for internalIndex, value in pairs(variableTable) do
                            objectData.variables[variableType][internalIndex] = value
                        end
                    end
                end

                tableHelper.insertValueIfMissing(self.data.packets.clientScriptLocal, uniqueIndex)
            end
        end
    end
end
-- End of AMP change

function BaseCell:SaveContainers(pid)

    tes3mp.ReadReceivedObjectList()

    -- Copy exactly once. Any actionCount corrections below are applied to the
    -- writable copy and must survive until the reply is sent to the attached
    -- player. Re-copying the received packet later would silently undo them.
    tes3mp.CopyReceivedObjectListToStore()

    local packetOrigin = tes3mp.GetObjectListOrigin()
    local logDetails = packetOrigin ~= enumerations.packetOrigin.CLIENT_SCRIPT_GLOBAL
    if logDetails then
        tes3mp.LogMessage(enumerations.log.INFO, "Saving Container from " .. logicHandler.GetChatName(pid) ..
            " about " .. self.description)
    end
    local action = tes3mp.GetObjectListAction()
    local subAction = tes3mp.GetObjectListContainerSubAction()
    local affectedContainers = {}

    -- A delta is never a baseline. Global scripts can touch an NPC in an
    -- unloaded cell before its first container snapshot (Arrille in particular).
    -- Building {} + ADD and broadcasting it as SET destroys every original item.
    -- Request the sender's complete current state instead; unilateral script
    -- changes are already included in that state and must not be replayed twice.
    if action ~= enumerations.container.SET then
        local missing = {}
        local snapshotIndexes = {}
        for objectIndex = 0, tes3mp.GetObjectListSize() - 1 do
            local uniqueIndex = tes3mp.GetObjectRefNum(objectIndex) .. "-" .. tes3mp.GetObjectMpNum(objectIndex)
            table.insert(snapshotIndexes, uniqueIndex)
            local object = self.data.objectData[uniqueIndex]
            if object == nil or type(object.inventory) ~= "table" then
                table.insert(missing, uniqueIndex)
            end
        end
        if #missing > 0 then
            tes3mp.LogAppend(enumerations.log.WARN,
                "- Container delta has no baseline; requesting snapshot for " .. self.description)
            if self:HasFullContainerData() then
                -- The whole packet was deferred, including any known objects
                -- mutated on this client. Snapshot all of them together.
                self:RequestContainers(pid, snapshotIndexes)
            elseif not tableHelper.containsValue(self.visitors, pid) then
                -- Global scripts may legitimately reference inactive cells.
                -- EnsureFullContainerData is visitor-only; ask this script's
                -- client directly while leaving inventory uninitialized.
                self:RequestContainers(pid)
            else
                self:EnsureFullContainerData(pid)
            end
            return
        end
    end

    for objectIndex = 0, tes3mp.GetObjectListSize() - 1 do

        local uniqueIndex = tes3mp.GetObjectRefNum(objectIndex) .. "-" .. tes3mp.GetObjectMpNum(objectIndex)
        local refId = tes3mp.GetObjectRefId(objectIndex)

        if logDetails then
            tes3mp.LogAppend(enumerations.log.INFO, "- " .. uniqueIndex .. ", refId: " .. refId)
        end

        self:InitializeObjectData(uniqueIndex, refId)
        tableHelper.insertValueIfMissing(self.data.packets.container, uniqueIndex)
        tableHelper.insertValueIfMissing(affectedContainers, uniqueIndex)

        local inventory = self.data.objectData[uniqueIndex].inventory

        -- SET is an authoritative snapshot. This is normally the reply to the
        -- server's first container-data request for an authored cell.
        if inventory == nil or action == enumerations.container.SET then
            inventory = {}
        else
            -- Cell data written before X013 carries no phase metadata. Stamp it
            -- once from the server-owned index, before any REMOVE is processed,
            -- so the migration never depends on who happens to open the chest.
            questItemPhasing.StampInventory(self.description, uniqueIndex, inventory)
        end

        for itemIndex = 0, tes3mp.GetContainerChangesSize(objectIndex) - 1 do

            local itemRefId = tes3mp.GetContainerItemRefId(objectIndex, itemIndex)
            local itemCount = math.max(0, tonumber(tes3mp.GetContainerItemCount(objectIndex, itemIndex)) or 0)
            local itemCharge = tes3mp.GetContainerItemCharge(objectIndex, itemIndex)
            local itemEnchantmentCharge = tes3mp.GetContainerItemEnchantmentCharge(objectIndex, itemIndex)
            local itemSoul = tes3mp.GetContainerItemSoul(objectIndex, itemIndex)
            local itemPoisonId = tes3mp.GetContainerItemPoisonId(objectIndex, itemIndex)
            local itemPoisonCharges = tes3mp.GetContainerItemPoisonCharges(objectIndex, itemIndex)
            local actionCount = math.max(0, tonumber(tes3mp.GetContainerItemActionCount(objectIndex, itemIndex)) or 0)
            -- X013: the packet still carries the client's own guess, but it is
            -- only ever logged. Classification is questIndexStore's job now.
            local isQuestRecord = questIndexStore.IsQuestItem(itemRefId)

            local foundIndex = nil
            if inventoryHelper.containsItem(inventory, itemRefId, itemCharge, itemEnchantmentCharge,
                itemSoul, itemPoisonId, itemPoisonCharges) then
                foundIndex = inventoryHelper.getItemIndex(inventory, itemRefId, itemCharge,
                    itemEnchantmentCharge, itemSoul, itemPoisonId, itemPoisonCharges)
            end

            -- Y109: discard transient bound entries, not the full snapshot.
            -- They must never become persistent chest stock or be granted by
            -- an ACK if a trusted script calls SaveContainers without validators.
            if arenaSummonRules.IsBound(itemRefId) then
                if action == enumerations.container.REMOVE then
                    tes3mp.SetContainerItemActionCountByIndex(objectIndex, itemIndex, 0)
                end
            elseif action == enumerations.container.REMOVE then

                if foundIndex == nil then
                    -- Y050: scripted REMOVE is idempotent. MFR local scripts can
                    -- legitimately replay the same removal after authority/cell
                    -- synchronization. Rejecting it with actionCount=0 leaves the
                    -- client believing the operation failed and it retries every
                    -- frame (the log showed >22k helm_ash_msk_01 retries). If the
                    -- authoritative server state already has no such item, ACK a
                    -- CLIENT_SCRIPT_LOCAL removal as already satisfied. Gameplay
                    -- takes remain rejected.
                    if packetOrigin == enumerations.packetOrigin.CLIENT_SCRIPT_LOCAL then
                        tes3mp.SetContainerItemActionCountByIndex(objectIndex, itemIndex, actionCount)
                        tes3mp.LogAppend(enumerations.log.INFO, "- Idempotent scripted REMOVE already satisfied for " .. itemRefId)
                    else
                        tes3mp.LogAppend(enumerations.log.INFO, "- Rejected stale container REMOVE for missing " .. itemRefId)
                        tes3mp.SetContainerItemActionCountByIndex(objectIndex, itemIndex, 0)
                    end
                else
                    local item = inventory[foundIndex]
                    local requested = actionCount
                    local phaseAccepted = 0

                    -- X013: a take never establishes a source. Eligibility was
                    -- stamped when the canonical inventory was recorded, so the
                    -- player taking the item cannot decide that it is phaseable.
                    local isPersonalTake = packetOrigin == enumerations.packetOrigin.CLIENT_GAMEPLAY or
                        subAction == enumerations.containerSub.DRAG or subAction == enumerations.containerSub.TAKE_ALL

                    if isPersonalTake then
                        phaseAccepted = questItemPhasing.ClaimContainer(pid, self.description, uniqueIndex,
                            item, requested)
                    end

                    -- Only the non-canonical part of a mixed stack is globally
                    -- removable. The canonical quest portion stays in cell data so
                    -- every other character can still claim it once.
                    local phaseCount = questItemPhasing.GetContainerPhaseCount(item)
                    local sharedCount = math.max(0, (tonumber(item.count) or 0) - phaseCount)
                    local sharedRequested = math.max(0, requested - phaseAccepted)
                    local sharedAccepted = math.min(sharedRequested, sharedCount)
                    local accepted = phaseAccepted + sharedAccepted
                    if isPersonalTake and accepted>0 and houseItemMarket then
                        houseItemMarket.AllowInventoryGain(pid,itemRefId,accepted)
                    end

                    if sharedAccepted > 0 then
                        item.count = math.max(phaseCount, (tonumber(item.count) or 0) - sharedAccepted)
                    end

                    tes3mp.SetContainerItemActionCountByIndex(objectIndex, itemIndex, accepted)

                    if accepted < requested then
                        tes3mp.LogAppend(enumerations.log.INFO, "- Clamped container REMOVE for " .. itemRefId ..
                            " from " .. requested .. " to " .. accepted .. " (personal phase/shared availability)")
                    end

                    -- An inventory entry may disappear only if it has no canonical
                    -- phased units left. In normal X012 operation phaseCount never
                    -- reaches zero merely because one player takes the quest item.
                    if (tonumber(item.count) or 0) <= 0 then
                        inventory[foundIndex] = nil

                        if logicHandler.IsGeneratedRecord(itemRefId) then
                            local recordStore = logicHandler.GetRecordStoreByRecordId(itemRefId)
                            if recordStore ~= nil then
                                self:RemoveLinkToRecord(recordStore.storeType, itemRefId, uniqueIndex)
                            end
                        end
                    end
                end

            elseif action == enumerations.container.ADD then

                if foundIndex ~= nil then
                    local item = inventory[foundIndex]
                    item.count = (tonumber(item.count) or 0) + itemCount

                    if packetOrigin == enumerations.packetOrigin.CLIENT_GAMEPLAY then
                        -- A player putting a quest-record item back into a chest is
                        -- adding shared loot, not minting a new canonical source.
                        if isQuestRecord or item.questPhaseEligible == true then
                            questItemPhasing.MarkContainerSharedDrop(item)
                        end
                    elseif isQuestRecord then
                        -- Scripted/authored additions can legitimately create a new
                        -- phaseable quest source in an existing container stack.
                        questItemPhasing.MarkContainerSource(item, itemCount)
                    end
                else
                    inventoryHelper.addItem(inventory, itemRefId, itemCount,
                        itemCharge, itemEnchantmentCharge, itemSoul, itemPoisonId, itemPoisonCharges)
                    foundIndex = inventoryHelper.getItemIndex(inventory, itemRefId, itemCharge,
                        itemEnchantmentCharge, itemSoul, itemPoisonId, itemPoisonCharges)
                    local item = inventory[foundIndex]

                    if packetOrigin == enumerations.packetOrigin.CLIENT_GAMEPLAY then
                        if isQuestRecord then questItemPhasing.MarkContainerSharedDrop(item) end
                    elseif isQuestRecord then
                        questItemPhasing.MarkContainerSource(item, itemCount)
                    end

                    if logicHandler.IsGeneratedRecord(itemRefId) then
                        local recordStore = logicHandler.GetRecordStoreByRecordId(itemRefId)
                        if recordStore ~= nil then
                            self:AddLinkToRecord(recordStore.storeType, itemRefId, uniqueIndex)
                        end
                    end
                end

            else -- SET

                inventoryHelper.addItem(inventory, itemRefId, itemCount,
                    itemCharge, itemEnchantmentCharge, itemSoul, itemPoisonId, itemPoisonCharges)
                foundIndex = inventoryHelper.getItemIndex(inventory, itemRefId, itemCharge,
                    itemEnchantmentCharge, itemSoul, itemPoisonId, itemPoisonCharges)

                -- SET is the canonical inventory: this is the one and only place
                -- where a container stack becomes a phaseable quest source.
                if foundIndex ~= nil and isQuestRecord then
                    questItemPhasing.MarkContainerSource(inventory[foundIndex], itemCount)
                end

                if logicHandler.IsGeneratedRecord(itemRefId) then
                    local recordStore = logicHandler.GetRecordStoreByRecordId(itemRefId)
                    if recordStore ~= nil then
                        self:AddLinkToRecord(recordStore.storeType, itemRefId, uniqueIndex)
                    end
                end
            end
        end

        tableHelper.cleanNils(inventory)
        self.data.objectData[uniqueIndex].inventory = inventory

        -- Y019: a SET is the authoritative snapshot taken from whichever client
        -- currently owns the actor, and that client's copy of an NPC accumulates
        -- one extra copy of the worn equipment per condition change it received.
        -- Collapse those before the snapshot is persisted and broadcast, or the
        -- duplicated set becomes real, lootable, shared state.
        if action == enumerations.container.SET then
            actorLootRepair.Repair(self, uniqueIndex)
        end
    end

    -- A purchased private home inherits the original ESM container snapshot.
    -- Convert that initial snapshot to empty inventories before *any* SET is
    -- published, without touching containers in the original public cell.
    local housing = rawget(_G, "houseHelper")
    if housing and type(housing.PreparePurchasedContainers) == "function" then
        housing.PreparePurchasedContainers(self, action, subAction)
    end

    -- REMOVE carries recipient semantics, so the attached player gets the
    -- corrected original packet first. Every client (including the taker) then
    -- receives an authoritative personalized SET of each affected container.
    if action == enumerations.container.REMOVE then
        tes3mp.SetObjectListPid(pid)
        tes3mp.SendContainer(false, false)
    end

    for _, uniqueIndex in pairs(affectedContainers) do
        packetBuilder.UpdateContainerForRelevantPlayers(self, uniqueIndex, nil)
        if action == enumerations.container.REMOVE
            and actorApparelGuard.Prune(self.data.objectData[uniqueIndex]) then
            for _, visitorPid in pairs(self.visitors or {}) do
                self:LoadActorEquipment(visitorPid, self.data.objectData, { uniqueIndex })
            end
        end
    end

    self:QuicksaveToDrive()

    -- Were we waiting on a full container data request from this pid?
    if self.isRequestingContainerData == true and self.containerRequestPid == pid and
        action == enumerations.container.SET and
        subAction == enumerations.containerSub.REPLY_TO_REQUEST then
        self.isRequestingContainerData = false
        self.containerRequestPid = nil
        self.containerRequestTime = nil
        self.data.loadState.hasFullContainerData = true
        if housing and type(housing.TryFinalizePurge) == "function" then
            housing.TryFinalizePurge(self.description)
        end

        tes3mp.LogAppend(enumerations.log.INFO, "- " .. self.description ..
            " is now recorded as having full container data")

        -- Full reply has finished processing. A partial request sent while the
        -- handshake was outstanding could have completed it incorrectly, so
        -- issue all deferred per-object requests only AFTER the full reply.
        local deferred = self.deferredContainerRequests
        self.deferredContainerRequests = {}
        local requested = {}
        for uniqueIndex in pairs(deferred or {}) do
            table.insert(requested, uniqueIndex)
        end
        if #requested > 0 then
            self:RequestContainers(pid, requested)
        end
        self:RetryUnusableContainers(pid)
    end
end

function BaseCell:SaveActorsByPacketType(packetType, actors)

    if packetType == "ActorList" then
        self:SaveActorList(actors)
    elseif packetType == "ActorEquipment" then
        self:SaveActorEquipment(actors)
    elseif packetType == "ActorSpellsActive" then
        self:SaveActorSpellsActive(actors)
    elseif packetType == "ActorDeath" then
        self:SaveActorDeath(actors)
    elseif packetType == "ActorAI" then
        self:SaveActorAI(actors)
    end    
end

function BaseCell:SaveObjectsByPacketType(packetType, objects)

    if packetType == "ObjectPlace" then
        self:SaveObjectsPlaced(objects)
    elseif packetType == "ObjectSpawn" then
        self:SaveObjectsSpawned(objects)
    elseif packetType == "ObjectDelete" then
        self:SaveObjectsDeleted(objects)
    elseif packetType == "ObjectMove" then
        self:SaveObjectsMoved(objects)
    elseif packetType == "ObjectRotate" then
        self:SaveObjectsRotated(objects)
    elseif packetType == "ObjectLock" then
        self:SaveObjectsLocked(objects)
    elseif packetType == "ObjectMiscellaneous" then
        self:SaveObjectsMiscellaneous(objects)
    elseif packetType == "ObjectTrap" then
        self:SaveObjectTrapsTriggered(objects)
    elseif packetType == "ObjectScale" then
        self:SaveObjectsScaled(objects)
    elseif packetType == "ObjectState" then
        self:SaveObjectStates(objects)
    elseif packetType == "DoorState" then
        self:SaveDoorStates(objects)
    elseif packetType == "ClientScriptLocal" then
        self:SaveClientScriptLocals(objects)
    end
end

function BaseCell:SaveActorList(actors)

    for uniqueIndex, actor in pairs(actors) do

        self:InitializeObjectData(uniqueIndex, actor.refId)
        tes3mp.LogAppend(enumerations.log.INFO, "- " .. uniqueIndex .. ", refId: " .. actor.refId)

        tableHelper.insertValueIfMissing(self.data.packets.actorList, uniqueIndex)
    end

    self:QuicksaveToDrive()

    -- Were we waiting on an actor list request from this pid?
    if self.isRequestingActorList == true then
        self.isRequestingActorList = false
        self.data.loadState.hasFullActorList = true

        tes3mp.LogAppend(enumerations.log.INFO, "- " .. self.description ..
            " is now recorded as having a full actor list")
    end
end

function BaseCell:SaveActorPositions()

    tes3mp.ReadCellActorList(self.description)
    local actorListSize = tes3mp.GetActorListSize()

    if actorListSize == 0 then
        return
    end

    for objectIndex = 0, actorListSize - 1 do

        local uniqueIndex = tes3mp.GetActorRefNum(objectIndex) .. "-" .. tes3mp.GetActorMpNum(objectIndex)

        if tes3mp.DoesActorHavePosition(objectIndex) == true and self:ContainsObject(uniqueIndex) then

            self.data.objectData[uniqueIndex].location = {
                posX = tes3mp.GetActorPosX(objectIndex),
                posY = tes3mp.GetActorPosY(objectIndex),
                posZ = tes3mp.GetActorPosZ(objectIndex),
                rotX = tes3mp.GetActorRotX(objectIndex),
                rotY = tes3mp.GetActorRotY(objectIndex),
                rotZ = tes3mp.GetActorRotZ(objectIndex)
            }

            tableHelper.insertValueIfMissing(self.data.packets.position, uniqueIndex)
        end
    end
end

function BaseCell:SaveActorStatsDynamic()

    tes3mp.ReadCellActorList(self.description)
    local actorListSize = tes3mp.GetActorListSize()

    if actorListSize == 0 then
        return
    end

    for objectIndex = 0, actorListSize - 1 do

        local uniqueIndex = tes3mp.GetActorRefNum(objectIndex) .. "-" .. tes3mp.GetActorMpNum(objectIndex)

        if tes3mp.DoesActorHaveStatsDynamic(objectIndex) == true and self:ContainsObject(uniqueIndex) then

            self.data.objectData[uniqueIndex].stats = {
                healthBase = tes3mp.GetActorHealthBase(objectIndex),
                healthCurrent = tes3mp.GetActorHealthCurrent(objectIndex),
                healthModified = tes3mp.GetActorHealthModified(objectIndex),
                magickaBase = tes3mp.GetActorMagickaBase(objectIndex),
                magickaCurrent = tes3mp.GetActorMagickaCurrent(objectIndex),
                magickaModified = tes3mp.GetActorMagickaModified(objectIndex),
                fatigueBase = tes3mp.GetActorFatigueBase(objectIndex),
                fatigueCurrent = tes3mp.GetActorFatigueCurrent(objectIndex),
                fatigueModified = tes3mp.GetActorFatigueModified(objectIndex)
            }

            tableHelper.insertValueIfMissing(self.data.packets.statsDynamic, uniqueIndex)
        end
    end
end

function BaseCell:SaveActorEquipment(actors)

    local repairedEquipment = {}
    local now = os.time()

    for uniqueIndex, actor in pairs(actors) do

        tes3mp.LogAppend(enumerations.log.INFO, "- " .. uniqueIndex)

        if self:ContainsObject(uniqueIndex) then
            local objectData = self.data.objectData[uniqueIndex]
            local incoming = {}
            for equipmentIndex, item in pairs(actor.equipment or {}) do
                incoming[equipmentIndex] = copyEquipmentItem(item)
            end

            -- Preserve worn items only while the authoritative inventory still
            -- contains them. Sales, scripts, broken armor and corpse looting can
            -- therefore remove apparel normally without resurrecting items.
            local guard = self.actorEquipmentTransferGuards and self.actorEquipmentTransferGuards[uniqueIndex]
            local activeGuard = type(guard) == "table" and guard.toCell == self.description
                and now <= (tonumber(guard.expiresAt) or 0)
            if actorApparelGuard.Repair(objectData, incoming, activeGuard and guard.apparel or nil) then
                repairedEquipment[uniqueIndex] = true
                tes3mp.LogAppend(enumerations.log.WARN, "-- Restored inventory-backed actor apparel for " .. uniqueIndex)
            end
            if guard and (not activeGuard or actorApparelGuard.IsDead(objectData)) then
                self.actorEquipmentTransferGuards[uniqueIndex] = nil
            end

            -- Y097: retain the last nonempty inventory-backed outfit in the
            -- same cell save. A later stripped packet never erases this snapshot.
            local content = package.loaded["contentApi"]
            if content and content.Actors and content.Actors.ObserveSnapshot then
                local ok, err = pcall(content.Actors.ObserveSnapshot, objectData, incoming)
                if not ok then tes3mp.LogAppend(enumerations.log.WARN, "-- Content outfit snapshot skipped: " .. tostring(err)) end
            end
            objectData.equipment = incoming
            tableHelper.insertValueIfMissing(self.data.packets.equipment, uniqueIndex)
        end
    end

    -- If a hand-off packet was repaired, immediately push the authoritative
    -- equipment back to every observer (including the current authority). The
    -- original stripped packet may already have been relayed by native TES3MP.
    for uniqueIndex in pairs(repairedEquipment) do
        for _, visitorPid in pairs(self.visitors or {}) do
            self:LoadActorEquipment(visitorPid, self.data.objectData, { uniqueIndex })
        end
    end

    -- Y019: one fight against a single NPC produced 91 equipment packets in this
    -- cell, and every one of them wrote the whole cell file to disk. Equipment is
    -- re-sent in full on every change, so losing the last few seconds of it to a
    -- crash costs nothing; throttle the save instead.
    local minInterval = tonumber(config.actorEquipmentSaveInterval) or 5

    if self.lastActorEquipmentSave == nil or now - self.lastActorEquipmentSave >= minInterval then
        self.lastActorEquipmentSave = now
        self:QuicksaveToDrive()
    end
end

function BaseCell:SaveActorSpellsActive(actors)

    for uniqueIndex, actor in pairs(actors) do

        tes3mp.LogAppend(enumerations.log.INFO, "- " .. uniqueIndex)

        if self:ContainsObject(uniqueIndex) then

            local action = actor.spellActiveChangesAction

            if action == enumerations.spellbook.SET or self.data.objectData[uniqueIndex].spellsActive == nil then
                self.data.objectData[uniqueIndex].spellsActive = {}
            end

            for spellId, spellInstances in pairs(actor.spellsActive) do

                if action == enumerations.spellbook.SET or action == enumerations.spellbook.ADD then
                    if self.data.objectData[uniqueIndex].spellsActive[spellId] == nil then
                        self.data.objectData[uniqueIndex].spellsActive[spellId] = {}
                    end

                    for _, spellInstanceValues in pairs(spellInstances) do

                        local spellInstanceIndex

                        -- Get an unused spellInstanceIndex if this is a spell with stacking effects
                        if spellInstanceValues.stackingState then
                            spellInstanceIndex = tableHelper.getUnusedNumericalIndex(
                                self.data.objectData[uniqueIndex].spellsActive[spellId])
                        -- Otherwise, replace what's under index 1
                        else
                            spellInstanceIndex = 1
                        end

                        self.data.objectData[uniqueIndex].spellsActive[spellId][spellInstanceIndex] = {
                            displayName = spellInstanceValues.displayName,
                            stackingState = spellInstanceValues.stackingState,
                            effects = tableHelper.deepCopy(spellInstanceValues.effects),
                            startTime = os.time()
                        }

                        if spellInstanceValues.caster ~= nil then
                            self.data.objectData[uniqueIndex].spellsActive[spellId][spellInstanceIndex].caster = {
                                playerName = spellInstanceValues.caster.playerName,
                                refId = spellInstanceValues.caster.refId,
                                uniqueIndex = spellInstanceValues.caster.uniqueIndex
                            }
                        end
                    end
                elseif action == enumerations.spellbook.REMOVE then
                    if self.data.objectData[uniqueIndex].spellsActive[spellId] ~= nil then
                        self.data.objectData[uniqueIndex].spellsActive[spellId][1] = nil

                        if tableHelper.getCount(self.data.objectData[uniqueIndex].spellsActive[spellId]) == 0 then
                            self.data.objectData[uniqueIndex].spellsActive[spellId] = nil
                        end
                    end
                end
            end

            if action == enumerations.spellbook.REMOVE then
                tableHelper.cleanNils(self.data.objectData[uniqueIndex].spellsActive)
            end

            if tableHelper.getCount(self.data.objectData[uniqueIndex].spellsActive) > 0 then
                tableHelper.insertValueIfMissing(self.data.packets.spellsActive, uniqueIndex)
            else
                tableHelper.removeValue(self.data.packets.spellsActive, uniqueIndex)
            end
        end
    end

    self:QuicksaveToDrive()
end

function BaseCell:SaveActorAI(actors)

    if type(self.data.packets.ai) ~= "table" then self.data.packets.ai = {} end
    local changed = false

    for uniqueIndex, actor in pairs(actors or {}) do
        local ai = actor.ai
        if ai ~= nil and self:ContainsObject(uniqueIndex) then
            if ai.action == enumerations.ai.FOLLOW or ai.action == enumerations.ai.ESCORT then
                -- Y092: Follow-no-home, clear before the next cell transfer.
                local object = self.data.objectData[uniqueIndex]
                if object.home ~= nil then object.home=nil;changed=true end
                -- Persist only authored follower packages. COMBAT/COMBAT_END are
                -- high-frequency authority heartbeats and must never become save data.
                local incoming = {
                    action = ai.action,
                    targetPlayer = ai.targetPlayer,
                    targetUniqueIndex = ai.targetUniqueIndex,
                    groupId = ai.groupId,
                    posX = ai.posX, posY = ai.posY, posZ = ai.posZ,
                    distance = ai.distance,
                    duration = ai.duration,
                    shouldRepeat = ai.shouldRepeat == true
                }
                local previous=type(object.ai)=="table" and object.ai or nil
                local equal=previous~=nil
                for key,value in pairs(incoming) do
                    if not previous or previous[key]~=value then equal=false;break end
                end
                if equal then
                    for key in pairs(previous) do if incoming[key]==nil then equal=false;break end end
                end
                if not equal then object.ai=incoming;changed=true end
                if not tableHelper.containsValue(self.data.packets.ai,uniqueIndex) then
                    table.insert(self.data.packets.ai,uniqueIndex);changed=true
                end
            elseif ai.action == enumerations.ai.CANCEL then
                -- An explicit cancel is the only transient client packet that should
                -- erase an authored follower package. Combat ending uses COMBAT_END.
                if self.data.objectData[uniqueIndex].ai ~= nil then
                    self.data.objectData[uniqueIndex].ai = nil
                    tableHelper.removeValue(self.data.packets.ai, uniqueIndex)
                    changed = true
                end
            end
        end
    end

    if changed then self:QuicksaveToDrive() end
end

function BaseCell:SaveActorDeath(actors)

    if self.data.packets.death == nil then
        self.data.packets.death = {}
    end

    for uniqueIndex, actor in pairs(actors) do

        if self:ContainsObject(uniqueIndex) then

            self.data.objectData[uniqueIndex].deathState = actor.deathState

            if actor.killer.pid ~= nil then
                self.data.objectData[uniqueIndex].killer = {
                    playerName = actor.killer.playerName
                }
            elseif actor.killerName ~= "" then
                self.data.objectData[uniqueIndex].killer = {
                    refId = actor.killer.refId,
                    uniqueIndex = actor.killer.uniqueIndex
                }
            end

            tableHelper.insertValueIfMissing(self.data.packets.death, uniqueIndex)

            -- Y019: the corpse is about to become lootable. If its container was
            -- already recorded before death, repair it now rather than waiting for
            -- the next SET, and push the corrected contents to everyone nearby.
            if actorLootRepair.Repair(self, uniqueIndex) > 0 then
                packetBuilder.UpdateContainerForRelevantPlayers(self, uniqueIndex, nil)
            end
        end
    end

    self:QuicksaveToDrive()
end

function BaseCell:SaveActorCellChanges(pid)

    local temporaryLoadedCells = {}

    tes3mp.ReadReceivedActorList()
    tes3mp.LogMessage(enumerations.log.INFO, "Saving ActorCellChange from " .. logicHandler.GetChatName(pid) ..
        " about " .. self.description)

    for actorIndex = 0, tes3mp.GetActorListSize() - 1 do

        local uniqueIndex = tes3mp.GetActorRefNum(actorIndex) .. "-" .. tes3mp.GetActorMpNum(actorIndex)
        local newCellDescription = tes3mp.GetActorCell(actorIndex)

        -- X022 defense in depth: older clients could accidentally serialize
        -- OpenMW's transitional exterior sentinel as a real destination. Never
        -- persist or create a JSON cell for INT_MIN,INT_MIN.
        if isTransitionalExteriorCell(newCellDescription) then
            tes3mp.LogAppend(enumerations.log.WARN, "- Rejected transitional ActorCellChange for " ..
                uniqueIndex .. " to " .. newCellDescription)
        elseif newCellDescription == self.description then
            tes3mp.LogAppend(enumerations.log.INFO, "- Ignored invalid cell change that was moving " .. uniqueIndex .. " to " ..
                self.description .. " despite that actor already being in that cell")
        else
            tes3mp.LogAppend(enumerations.log.INFO, "- " .. uniqueIndex .. " moved to " .. newCellDescription)

            -- If the new cell is not loaded, load it temporarily
            if LoadedCells[newCellDescription] == nil then
                logicHandler.LoadCell(newCellDescription)
                table.insert(temporaryLoadedCells, newCellDescription)
            end

            local newCell = LoadedCells[newCellDescription]

            -- Only proceed if this Actor is actually supposed to exist in this cell
            if self.data.objectData[uniqueIndex] ~= nil then

                -- The packet itself carries the destination transform. A missing
                -- historical source position must not discard an otherwise valid
                -- move after C++ has already accepted it and will relay it.
                local position = {
                    tes3mp.GetActorPosX(actorIndex), tes3mp.GetActorPosY(actorIndex),
                    tes3mp.GetActorPosZ(actorIndex), tes3mp.GetActorRotX(actorIndex),
                    tes3mp.GetActorRotY(actorIndex), tes3mp.GetActorRotZ(actorIndex)
                }
                local validPosition = #position == 6
                for _, value in pairs(position) do
                    if type(value) ~= "number" or value ~= value or math.abs(value) == math.huge then
                        validPosition = false
                    end
                end
                if not validPosition then
                    tes3mp.LogAppend(enumerations.log.WARN, "- Invalid destination transform for " .. uniqueIndex)
                else

                -- X034: persist the route independently of whichever client owns
                -- the actor. Interior transitions are door breadcrumbs; exterior
                -- grid hops are harmless because the client walks exterior homes
                -- directly. A reverse hop consumes the previous breadcrumb.
                local objectData = self.data.objectData[uniqueIndex]

                -- Y092-EQ: arm the apparel hand-off guard for every real cell
                -- transition, not only the first trip away from home. NPCs can
                -- chase/follow through several exterior/interior cells and each
                -- authority rebuild can emit a transient naked equipment packet.
                local transferSeconds = tonumber(config.actorEquipmentTransferGuardSeconds) or 8
                local apparel = makeApparelSnapshot(objectData.equipment)
                newCell.actorEquipmentTransferGuards = newCell.actorEquipmentTransferGuards or {}
                self.actorEquipmentTransferGuards = self.actorEquipmentTransferGuards or {}
                self.actorEquipmentTransferGuards[uniqueIndex] = nil
                if next(apparel) ~= nil then
                    newCell.actorEquipmentTransferGuards[uniqueIndex] = {
                        fromCell = self.description,
                        toCell = newCellDescription,
                        expiresAt = os.time() + math.max(1, transferSeconds),
                        apparel = apparel
                    }
                else
                    newCell.actorEquipmentTransferGuards[uniqueIndex] = nil
                end

                local oldLocation = objectData.location
                local ai = objectData.ai
                if ai and (ai.action == enumerations.ai.FOLLOW or ai.action == enumerations.ai.ESCORT) then
                    objectData.home = nil
                end
                local home = objectData.home
                if home ~= nil and home.cell ~= nil and oldLocation ~= nil and oldLocation.posX ~= nil then
                    home.route = home.route or {}
                    local lastHop = home.route[#home.route]
                    if lastHop ~= nil and lastHop.fromCell == newCellDescription
                        and lastHop.toCell == self.description then
                        table.remove(home.route, #home.route)
                    else
                        table.insert(home.route, {
                            fromCell = self.description,
                            fromX = oldLocation.posX, fromY = oldLocation.posY, fromZ = oldLocation.posZ,
                            toCell = newCellDescription,
                            toX = tes3mp.GetActorPosX(actorIndex),
                            toY = tes3mp.GetActorPosY(actorIndex),
                            toZ = tes3mp.GetActorPosZ(actorIndex)
                        })
                        while #home.route > 12 do table.remove(home.route, 1) end
                    end
                end

                -- Was this actor spawned in the old cell, instead of being a pre-existing actor?
                -- If so, delete it entirely from the old cell and make it get spawned in the new cell
                if tableHelper.containsValue(self.data.packets.spawn, uniqueIndex) == true then
                    tes3mp.LogAppend(enumerations.log.INFO, "-- As a server-only object, it was moved entirely")

                    -- If this object is based on a generated record, move its record link
                    -- to the new cell
                    local refId = self.data.objectData[uniqueIndex].refId

                    if logicHandler.IsGeneratedRecord(refId) then

                        local recordStore = logicHandler.GetRecordStoreByRecordId(refId)

                        if recordStore ~= nil then
                            newCell:AddLinkToRecord(recordStore.storeType, refId, uniqueIndex)
                            self:RemoveLinkToRecord(recordStore.storeType, refId, uniqueIndex)
                        end

                        -- Send this generated record to every visitor in the new cell
                        for _, visitorPid in pairs(newCell.visitors) do
                            if pid ~= visitorPid then
                                recordStore:LoadGeneratedRecords(visitorPid, recordStore.data.generatedRecords, { refId })
                            end
                        end
                    end

                    -- A server-only actor only needs to be introduced to players who
                    -- currently observe its destination. Do not broadcast its full
                    -- packet history to unrelated players elsewhere on the server.
                    for _, visitorPid in pairs(newCell.visitors) do
                        if pid ~= visitorPid and not tableHelper.containsValue(self.visitors, visitorPid) then
                            self:LoadActorPackets(visitorPid, self.data.objectData, { uniqueIndex })
                        end
                    end

                    self:MoveObjectData(uniqueIndex, newCell)

                -- Was this actor moved to the old cell from another cell?
                elseif tableHelper.containsValue(self.data.packets.cellChangeFrom, uniqueIndex) == true then

                    local originalCellDescription = self.data.objectData[uniqueIndex].cellChangeFrom

                    -- Is the new cell actually this actor's original cell?
                    -- If so, move its data back and remove all of its cell change data
                    if originalCellDescription == newCellDescription then
                        tes3mp.LogAppend(enumerations.log.INFO, "-- It is now back in its original cell " .. originalCellDescription)
                        self:MoveObjectData(uniqueIndex, newCell)

                        -- X024: the actor made it home, so drop both the anchor and
                        -- the hidden travel order the server may have issued for it.
                        if newCell.data.objectData[uniqueIndex] ~= nil then
                            newCell.data.objectData[uniqueIndex].home = nil

                            if newCell.data.objectData[uniqueIndex].ai ~= nil
                                and newCell.data.objectData[uniqueIndex].ai.action == enumerations.ai.TRAVEL then
                                newCell.data.objectData[uniqueIndex].ai = nil
                                tableHelper.removeValue(newCell.data.packets.ai, uniqueIndex)
                            end
                        end

                        tableHelper.removeValue(newCell.data.packets.cellChangeTo, uniqueIndex)
                        tableHelper.removeValue(newCell.data.packets.cellChangeFrom, uniqueIndex)

                        newCell.data.objectData[uniqueIndex].cellChangeTo = nil
                        newCell.data.objectData[uniqueIndex].cellChangeFrom = nil
                    -- Otherwise, move its data to the new cell, delete it from the old cell, and update its
                    -- information in its original cell
                    else
                        self:MoveObjectData(uniqueIndex, newCell)

                        -- If the original cell is not loaded, load it temporarily
                        if LoadedCells[originalCellDescription] == nil then
                            logicHandler.LoadCell(originalCellDescription)
                            table.insert(temporaryLoadedCells, originalCellDescription)
                        end

                        local originalCell = LoadedCells[originalCellDescription]

                        if originalCell.data.objectData[uniqueIndex] ~= nil then
                            tes3mp.LogAppend(enumerations.log.INFO, "-- This is now referenced in its original cell " ..
                                originalCellDescription)
                            originalCell.data.objectData[uniqueIndex].cellChangeTo = newCellDescription
                            originalCell:QuicksaveToDrive()
                        else
                            tes3mp.LogAppend(enumerations.log.ERROR, "-- It does not exist in its original cell " ..
                                originalCellDescription .. "! Please report this to a developer")
                        end
                    end

                -- Otherwise, simply move this actor's data to the new cell and mark it as being moved there
                -- in its old cell, as long as it's not supposed to already be in the new cell
                elseif self.data.objectData[uniqueIndex].cellChangeTo ~= newCellDescription then

                    tes3mp.LogAppend(enumerations.log.INFO, "-- This was its first move away from its original cell")

                    -- X024: remember where this actor belongs before it leaves.
                    -- The client-side return-home package is stored in AiState and
                    -- is lost whenever actor authority moves to another player, so
                    -- an NPC that started a fight under player A and finished it
                    -- under player B had nothing left telling it where home was.
                    -- The server keeps the anchor instead and can hand it to
                    -- whichever client currently owns the actor.
                    local authoredAi = self.data.objectData[uniqueIndex].ai
                    local following = authoredAi ~= nil and (authoredAi.action == enumerations.ai.FOLLOW
                        or authoredAi.action == enumerations.ai.ESCORT)
                    if following then self.data.objectData[uniqueIndex].home = nil end
                    if config.rememberActorHomes and not following and self.data.objectData[uniqueIndex].home == nil then

                        local currentLocation = self.data.objectData[uniqueIndex].location

                        if currentLocation ~= nil and currentLocation.posX ~= nil then
                            self.data.objectData[uniqueIndex].home = {
                                cell = self.description,
                                posX = currentLocation.posX,
                                posY = currentLocation.posY,
                                posZ = currentLocation.posZ,
                                rotZ = currentLocation.rotZ,
                                awayTime = os.time(),
                                route = {
                                    {
                                        fromCell = self.description,
                                        fromX = currentLocation.posX, fromY = currentLocation.posY, fromZ = currentLocation.posZ,
                                        toCell = newCellDescription,
                                        toX = tes3mp.GetActorPosX(actorIndex),
                                        toY = tes3mp.GetActorPosY(actorIndex),
                                        toZ = tes3mp.GetActorPosZ(actorIndex)
                                    }
                                }
                            }
                            tes3mp.LogAppend(enumerations.log.INFO, "-- Remembered its home and first route hop in " .. self.description)
                        end
                    end

                    self:MoveObjectData(uniqueIndex, newCell)

                    table.insert(self.data.packets.cellChangeTo, uniqueIndex)

                    if self.data.objectData[uniqueIndex] == nil then
                        self.data.objectData[uniqueIndex] = {}
                    end

                    self.data.objectData[uniqueIndex].cellChangeTo = newCellDescription

                    table.insert(newCell.data.packets.cellChangeFrom, uniqueIndex)

                    newCell.data.objectData[uniqueIndex].cellChangeFrom = self.description
                end

                if newCell.data.objectData[uniqueIndex] ~= nil then
                    -- Y020: only persist an explicit destination actor-list entry for
                    -- transitions involving an interior. Y013 did this for every
                    -- exterior grid hop, which could leave two adjacent cells treating
                    -- the same NPC as locally present during rapid authority hand-off.
                    -- The interior case is the one the reliable-return fix actually
                    -- needs; ordinary exterior movement stays on the normal authority
                    -- and ActorCellChange path.
                    local isInteriorTransition = newCell.isExterior ~= true or self.isExterior ~= true
                    if isInteriorTransition then
                        tableHelper.insertValueIfMissing(newCell.data.packets.actorList, uniqueIndex)
                    end
                    newCell.data.objectData[uniqueIndex].location = {
                        posX = tes3mp.GetActorPosX(actorIndex),
                        posY = tes3mp.GetActorPosY(actorIndex),
                        posZ = tes3mp.GetActorPosZ(actorIndex),
                        rotX = tes3mp.GetActorRotX(actorIndex),
                        rotY = tes3mp.GetActorRotY(actorIndex),
                        rotZ = tes3mp.GetActorRotZ(actorIndex)
                    }
                    -- Alpha 0.12: a destination-only observer may never have loaded
                    -- the intermediate cell. Introduce the reference from its original
                    -- content cell, then replay its current state in the destination.
                    -- Source observers already know this reference and receive the
                    -- normal C++ ActorCellChange broadcast after this callback.
                    local arrived = newCell.data.objectData[uniqueIndex]
                    local origin = arrived.cellChangeFrom or self.description
                    do
                        for _, visitorPid in pairs(newCell.visitors) do
                            if visitorPid ~= pid and not tableHelper.containsValue(self.visitors, visitorPid) then
                                tes3mp.ClearActorList()
                                tes3mp.SetActorListPid(visitorPid)
                                tes3mp.SetActorListCell(origin)
                                tes3mp.SetActorRefNum(tes3mp.GetActorRefNum(actorIndex))
                                tes3mp.SetActorMpNum(tes3mp.GetActorMpNum(actorIndex))
                                tes3mp.SetActorCell(newCellDescription)
                                local location = arrived.location
                                tes3mp.SetActorPosition(location.posX, location.posY, location.posZ)
                                tes3mp.SetActorRotation(location.rotX, location.rotY, location.rotZ)
                                tes3mp.AddActor()
                                tes3mp.SendActorCellChange(false, false)
                                newCell:LoadActorPackets(visitorPid, newCell.data.objectData, { uniqueIndex })
                            end
                        end
                    end
                    -- Y013-fix-01: the original code quicksaved the destination on
                    -- every actor cell change, including exterior->exterior. Wandering
                    -- NPCs and followers cross exterior borders constantly, so on a
                    -- populated server that turned into continuous disk I/O.
                    --
                    -- The breadcrumb only has to survive a restart, and the case the
                    -- NPC-return fix exists for is the interior transition. Persist
                    -- those immediately; let exterior hops ride the normal periodic
                    -- save. Interior transfers are never throttled, including rapid returns.
                    if isInteriorTransition then
                        -- A quick return can occur inside the old five-second
                        -- throttle. Persist the destination on every real door
                        -- transition so its latest stats/redirect survive reload.
                        newCell.lastActorCellChangeSave = os.time()
                        newCell:QuicksaveToDrive()
                    end
                end
                -- Close the destination-position guard before handling the stale
                -- objectData branch below.  Keeping this boundary explicit is
                -- important: the two branches are independent (missing source
                -- transform versus missing actor record).
                end
            else
                -- Y050: this is a stale authority hand-off, not a corrupt cell.
                -- The actor may already have been moved by a newer packet from
                -- another authority. Never fabricate it in the source cell; just
                -- ignore the obsolete move. This removes the repeated false ERROR
                -- seen during busy exterior transitions.
                tes3mp.LogAppend(enumerations.log.INFO, "- Ignored stale ActorCellChange for missing actor " ..
                    uniqueIndex .. " from " .. self.description .. " to " .. newCellDescription)
            end
        end
    end

    -- Go through every temporary loaded cell and unload it
    for arrayIndex, newCellDescription in pairs(temporaryLoadedCells) do
        logicHandler.UnloadCell(newCellDescription)
    end

    self:QuicksaveToDrive()
end

function BaseCell:LoadActorPackets(pid, objectData, uniqueIndexArray)

    local packets = self.data.packets

    self:LoadObjectsDeleted(pid, objectData, tableHelper.getValueOverlap(uniqueIndexArray, packets.delete))
    self:LoadObjectsSpawned(pid, objectData, tableHelper.getValueOverlap(uniqueIndexArray, packets.spawn))
    self:LoadObjectsScaled(pid, objectData, tableHelper.getValueOverlap(uniqueIndexArray, packets.scale))

    self:LoadContainers(pid, objectData, tableHelper.getValueOverlap(uniqueIndexArray, packets.container))

    self:LoadActorPositions(pid, objectData, tableHelper.getValueOverlap(uniqueIndexArray, packets.position))
    self:LoadActorDeath(pid, objectData, tableHelper.getValueOverlap(uniqueIndexArray, packets.statsDynamic))
    self:LoadActorStatsDynamic(pid, objectData, tableHelper.getValueOverlap(uniqueIndexArray, packets.statsDynamic))
    self:LoadActorEquipment(pid, objectData, tableHelper.getValueOverlap(uniqueIndexArray, packets.equipment))
    self:LoadActorAI(pid, objectData, tableHelper.getValueOverlap(uniqueIndexArray, packets.ai))
end

function BaseCell:LoadObjectsDeleted(pid, objectData, uniqueIndexArray, forEveryone)

    local objectCount = 0

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        packetBuilder.AddObjectDelete(uniqueIndex, objectData[uniqueIndex])
        objectCount = objectCount + 1
    end

    if objectCount > 0 then
        tes3mp.SendObjectDelete(forEveryone)
    end
end

function BaseCell:LoadObjectsPlaced(pid, objectData, uniqueIndexArray, forEveryone, isReplay)

    local objectCount = 0
    -- Keep reliable fragments small for interiors with hundreds of books/items.
    local batchLimit=128

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        if objectData[uniqueIndex] ~= nil then

            local currentObject = objectData[uniqueIndex]
            local location = currentObject.location
            -- Y089 safety net: FLGBedrollMiscScript uses a disabled misc object
            -- only as a one-shot conversion source for flg_bedroll01acti. If a
            -- legacy cell still contains such a source, never recreate it as an
            -- enabled ObjectPlace and give the local script a chance to clone
            -- another activator before ObjectState(false) arrives.
            local isSpentBedrollSource = currentObject.state == false and
                type(currentObject.refId) == "string" and
                string.lower(currentObject.refId) == "flg_bedroll01misc"

            -- Ensure data integrity before proceeeding
            if not isSpentBedrollSource and type(location) == "table" and
                tableHelper.getCount(location) == 6 and tableHelper.usesNumericalValues(location) and
                self:ContainsPosition(location.posX, location.posY) then

                packetBuilder.AddObjectPlace(uniqueIndex, currentObject, isReplay)
                objectCount = objectCount + 1
            elseif not isSpentBedrollSource then
                -- Loading must not silently delete saved objects with bad transforms.
                tes3mp.LogAppend(enumerations.log.WARN,
                    "[CellLoad] Skipped invalid placement " .. tostring(uniqueIndex) .. " in " .. self.description)
            end

            -- If we're about to exceed the maximum number of objects in a single packet,
            -- start a new packet
            if objectCount >= batchLimit then
                tes3mp.SendObjectPlace(forEveryone)
                tes3mp.ClearObjectList()
                tes3mp.SetObjectListPid(pid)
                tes3mp.SetObjectListCell(self.description)
                objectCount = 0
            end
        end
    end

    if objectCount > 0 then
        tes3mp.SendObjectPlace(forEveryone)
    end
end

function BaseCell:LoadObjectsSpawned(pid, objectData, uniqueIndexArray, forEveryone)

    local objectCount = 0

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        if objectData[uniqueIndex] ~= nil then

            local location = objectData[uniqueIndex].location

            -- Ensure data integrity before proceeeding
            if type(location) == "table" and tableHelper.getCount(location) == 6 and
                tableHelper.usesNumericalValues(location) and
                self:ContainsPosition(location.posX, location.posY) then

                local shouldSkip = false
                local summon = objectData[uniqueIndex].summon

                if summon ~= nil then
                    local currentTime = os.time()
                    local finishTime = summon.startTime + summon.duration

                    -- Don't spawn this summoned creature if its summoning duration is over..
                    if currentTime >= finishTime then
                        self:DeleteObjectData(uniqueIndex)
                        shouldSkip = true
                    -- ...or if its player is offline
                    elseif summon.summoner.playerName ~= nil then
                        if not logicHandler.IsPlayerNameLoggedIn(summon.summoner.playerName) then
                            shouldSkip = true
                        end
                    -- ...or if it doesn't have an actor stored as its summoner
                    elseif summon.summoner.uniqueIndex == nil then
                        shouldSkip = true
                    end
                end

                if not shouldSkip then
                    packetBuilder.AddObjectSpawn(uniqueIndex, objectData[uniqueIndex])
                    objectCount = objectCount + 1
                end
            else
                objectData[uniqueIndex] = nil
                tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
            end
        end
    end

    if objectCount > 0 then
        tes3mp.SendObjectSpawn(forEveryone)
    end
end

function BaseCell:LoadObjectsLocked(pid, objectData, uniqueIndexArray, forEveryone)

    local objectCount = 0

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        if objectData[uniqueIndex] ~= nil and objectData[uniqueIndex].refId ~= nil and
            objectData[uniqueIndex].lockLevel ~= nil then

            packetBuilder.AddObjectLock(uniqueIndex, objectData[uniqueIndex])
            objectCount = objectCount + 1
        else
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if objectCount > 0 then
        tes3mp.SendObjectLock(forEveryone)
    end
end

function BaseCell:LoadObjectsMiscellaneous(pid, objectData, uniqueIndexArray, forEveryone)

    local objectCount = 0

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        if objectData[uniqueIndex] ~= nil and objectData[uniqueIndex].refId ~= nil and
            objectData[uniqueIndex].goldPool ~= nil then

            local lastGoldRestockHour = objectData[uniqueIndex].lastGoldRestockHour
            local lastGoldRestockDay = objectData[uniqueIndex].lastGoldRestockDay

            if lastGoldRestockHour == nil or lastGoldRestockDay == nil then
                objectData[uniqueIndex].lastGoldRestockHour = 0
                objectData[uniqueIndex].lastGoldRestockDay = 0
            end

            packetBuilder.AddObjectMiscellaneous(uniqueIndex, objectData[uniqueIndex])
            objectCount = objectCount + 1
        else
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if objectCount > 0 then
        tes3mp.SendObjectMiscellaneous(forEveryone)
    end
end

function BaseCell:LoadObjectTrapsTriggered(pid, objectData, uniqueIndexArray, forEveryone)

    local objectCount = 0

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        if objectData[uniqueIndex] ~= nil then
            packetBuilder.AddObjectTrap(uniqueIndex, objectData[uniqueIndex])
            objectCount = objectCount + 1
        else
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if objectCount > 0 then
        tes3mp.SendObjectTrap(forEveryone)
    end
end

function BaseCell:LoadObjectsMoved(pid, objectData, uniqueIndexArray, forEveryone, skipPid)

    -- Y020: tolerate legacy cells or partially upgraded server configs.  Y013
    -- called this loader unconditionally, and pairs(nil) here aborted the rest
    -- of cell loading, including ActorList/AI/position synchronization.
    if type(objectData) ~= "table" or type(uniqueIndexArray) ~= "table" then return end

    local function sendTo(targetPid)
        local objectCount = 0
        tes3mp.ClearObjectList()
        tes3mp.SetObjectListPid(targetPid)
        tes3mp.SetObjectListCell(self.description)

        for _, uniqueIndex in pairs(uniqueIndexArray) do
            local data = objectData[uniqueIndex]
            if data ~= nil and data.location ~= nil and data.location.posX ~= nil then
                packetBuilder.AddObjectMove(uniqueIndex, data)
                objectCount = objectCount + 1
            end
        end
        if objectCount > 0 then tes3mp.SendObjectMove(false, false) end
    end

    if forEveryone then
        for _, targetPid in ipairs(packetBuilder.GetRelevantPlayersForCell(self)) do
            if targetPid ~= skipPid then sendTo(targetPid) end
        end
    else
        sendTo(pid)
    end
end

function BaseCell:LoadObjectsRotated(pid, objectData, uniqueIndexArray, forEveryone, skipPid)

    -- Same compatibility guard as LoadObjectsMoved.  Rotation state is optional
    -- for old cells and must never prevent actors from being loaded.
    if type(objectData) ~= "table" or type(uniqueIndexArray) ~= "table" then return end

    local function sendTo(targetPid)
        local objectCount = 0
        tes3mp.ClearObjectList()
        tes3mp.SetObjectListPid(targetPid)
        tes3mp.SetObjectListCell(self.description)

        for _, uniqueIndex in pairs(uniqueIndexArray) do
            local data = objectData[uniqueIndex]
            if data ~= nil and data.location ~= nil and data.location.rotX ~= nil then
                packetBuilder.AddObjectRotate(uniqueIndex, data)
                objectCount = objectCount + 1
            end
        end
        if objectCount > 0 then tes3mp.SendObjectRotate(false, false) end
    end

    if forEveryone then
        for _, targetPid in ipairs(packetBuilder.GetRelevantPlayersForCell(self)) do
            if targetPid ~= skipPid then sendTo(targetPid) end
        end
    else
        sendTo(pid)
    end
end

function BaseCell:LoadObjectsScaled(pid, objectData, uniqueIndexArray, forEveryone)

    local objectCount = 0

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        if objectData[uniqueIndex] ~= nil and objectData[uniqueIndex].refId ~= nil and
            objectData[uniqueIndex].scale ~= nil then

            packetBuilder.AddObjectScale(uniqueIndex, objectData[uniqueIndex])
            objectCount = objectCount + 1
        else
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if objectCount > 0 then
        tes3mp.SendObjectScale(forEveryone)
    end
end

function BaseCell:LoadObjectStates(pid, objectData, uniqueIndexArray, forEveryone)

    local objectCount = 0

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        if objectData[uniqueIndex] ~= nil and objectData[uniqueIndex].refId ~= nil and
            objectData[uniqueIndex].state ~= nil then

            packetBuilder.AddObjectState(uniqueIndex, objectData[uniqueIndex])
            objectCount = objectCount + 1
        else
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if objectCount > 0 then
        tes3mp.SendObjectState(forEveryone)
    end
end

function BaseCell:LoadDoorStates(pid, objectData, uniqueIndexArray, forEveryone)

    local objectCount = 0

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        if objectData[uniqueIndex] ~= nil then
            packetBuilder.AddDoorState(uniqueIndex, objectData[uniqueIndex])
            objectCount = objectCount + 1
        else
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if objectCount > 0 then
        tes3mp.SendDoorState(forEveryone)
    end
end

-- Start of AMP change
--
-- This was the only loader in the file without a guard on objectData[uniqueIndex], while
-- its neighbours all check for one. A uniqueIndex left in packets.clientScriptLocal whose
-- object data had since been removed therefore raised a Lua error partway through cell
-- loading, and everything that would have been sent after it - containers, actor data,
-- door states - never reached the player. That is the likeliest source of the intermittent
-- desyncs where a cell comes up half populated.
function BaseCell:LoadClientScriptLocals(pid, objectData, uniqueIndexArray, forEveryone)

    if type(uniqueIndexArray) ~= "table" then return end

    local objectCount = 0

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        if objectData[uniqueIndex] ~= nil and type(objectData[uniqueIndex].variables) == "table" and
            not require("personalScriptLocals").IsPersonalTarget(self.description, uniqueIndex, objectData[uniqueIndex]) then
            packetBuilder.AddClientScriptLocal(uniqueIndex, objectData[uniqueIndex])
            objectCount = objectCount + 1
        else
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if objectCount > 0 then
        tes3mp.SendClientScriptLocal(forEveryone)
    end
end
-- End of AMP change

function BaseCell:LoadContainers(pid, objectData, uniqueIndexArray)

    local objectCount = 0

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        local splitIndex = uniqueIndex:split("-")
        tes3mp.SetObjectRefNum(splitIndex[1])
        tes3mp.SetObjectMpNum(splitIndex[2])

        if self:ContainsObject(uniqueIndex) and objectData[uniqueIndex].inventory ~= nil then

            -- Y019: heal cell data written before the duplication fix, lazily, the
            -- first time each container is actually sent to somebody.
            if objectData == self.data.objectData then
                local repaired = actorLootRepair.Repair(self, uniqueIndex)
                if repaired > 0 then
                    -- Persist the one-time migration immediately, otherwise an
                    -- old duplicated corpse would be repaired again after every
                    -- server restart until some unrelated write touched the cell.
                    self:QuicksaveToDrive()
                end
            end

            tes3mp.SetObjectRefId(objectData[uniqueIndex].refId)

            for itemIndex, item in pairs(objectData[uniqueIndex].inventory) do

                local visibleCount = questItemPhasing.GetVisibleContainerCount(
                    pid, self.description, uniqueIndex, item)

                if visibleCount > 0 then
                    if item.enchantmentCharge == nil then
                        item.enchantmentCharge = -1
                    end

                    if item.soul == nil then
                        item.soul = ""
                    end

                    tes3mp.SetContainerItemRefId(item.refId)
                    tes3mp.SetContainerItemCount(visibleCount)
                    tes3mp.SetContainerItemCharge(item.charge)
                    tes3mp.SetContainerItemEnchantmentCharge(item.enchantmentCharge)
                    tes3mp.SetContainerItemSoul(item.soul)
                    tes3mp.SetContainerItemPoison(item.poisonId or "", item.poisonCharges or 0)

                    tes3mp.AddContainerItem()
                end
            end

            tes3mp.AddObject()

            objectCount = objectCount + 1
        else
            tes3mp.LogAppend(enumerations.log.ERROR, "- Had container packet recorded for " .. uniqueIndex ..
                ", but no matching object data! Please report this to a developer")
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if objectCount > 0 then

        -- Set the action to SET
        tes3mp.SetObjectListAction(0)

        tes3mp.SendContainer()
    end
end

function BaseCell:LoadObjectsByPacketType(packetType, pid, objectData, uniqueIndexArray, forEveryone, skipPid)

    if packetType == "ObjectPlace" then
        self:LoadObjectsPlaced(pid, objectData, uniqueIndexArray, forEveryone)
    elseif packetType == "ObjectSpawn" then
        self:LoadObjectsSpawned(pid, objectData, uniqueIndexArray, forEveryone)
    elseif packetType == "ObjectDelete" then
        self:LoadObjectsDeleted(pid, objectData, uniqueIndexArray, forEveryone)
    elseif packetType == "ObjectMove" then
        self:LoadObjectsMoved(pid, objectData, uniqueIndexArray, forEveryone, skipPid)
    elseif packetType == "ObjectRotate" then
        self:LoadObjectsRotated(pid, objectData, uniqueIndexArray, forEveryone, skipPid)
    elseif packetType == "ObjectLock" then
        self:LoadObjectsLocked(pid, objectData, uniqueIndexArray, forEveryone)
    elseif packetType == "ObjectMiscellaneous" then
        self:LoadObjectsMiscellaneous(pid, objectData, uniqueIndexArray, forEveryone)
    elseif packetType == "ObjectTrap" then
        self:LoadObjectTrapsTriggered(pid, objectData, uniqueIndexArray, forEveryone)
    elseif packetType == "ObjectScale" then
        self:LoadObjectsScaled(pid, objectData, uniqueIndexArray, forEveryone)
    elseif packetType == "ObjectState" then
        self:LoadObjectStates(pid, objectData, uniqueIndexArray, forEveryone)
    elseif packetType == "DoorState" then
        self:LoadDoorStates(pid, objectData, uniqueIndexArray, forEveryone)
    elseif packetType == "ClientScriptLocal" then
        self:LoadClientScriptLocals(pid, objectData, uniqueIndexArray, forEveryone)
    end
end

function BaseCell:LoadActorList(pid, objectData, uniqueIndexArray)

    local actorCount = 0

    tes3mp.ClearActorList()
    tes3mp.SetActorListPid(pid)
    tes3mp.SetActorListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        local splitIndex = uniqueIndex:split("-")
        tes3mp.SetActorRefNum(splitIndex[1])
        tes3mp.SetActorMpNum(splitIndex[2])

        if self:ContainsObject(uniqueIndex) then
            tes3mp.SetActorRefId(objectData[uniqueIndex].refId)

            actorCount = actorCount + 1
        else
            tes3mp.LogAppend(enumerations.log.ERROR, "- Had actorList packet recorded for " .. uniqueIndex ..
                ", but no matching object data! Please report this to a developer")
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if actorCount > 0 then

        -- Set the action to SET
        tes3mp.SetActorListAction(0)

        tes3mp.SendActorList()
    end
end

function BaseCell:LoadActorAuthority(pid)

    tes3mp.ClearActorList()
    tes3mp.SetActorListPid(pid)
    tes3mp.SetActorListCell(self.description)

    tes3mp.SendActorAuthority()
end

function BaseCell:LoadActorPositions(pid, objectData, uniqueIndexArray)

    local actorCount = 0

    tes3mp.ClearActorList()
    tes3mp.SetActorListPid(pid)
    tes3mp.SetActorListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        local splitIndex = uniqueIndex:split("-")
        tes3mp.SetActorRefNum(splitIndex[1])
        tes3mp.SetActorMpNum(splitIndex[2])

        if self:ContainsObject(uniqueIndex) then
            local location = objectData[uniqueIndex].location

            -- Ensure data integrity before proceeeding
            if tableHelper.getCount(location) == 6 and tableHelper.usesNumericalValues(location) and
                self:ContainsPosition(location.posX, location.posY) then

                tes3mp.SetActorPosition(location.posX, location.posY, location.posZ)
                tes3mp.SetActorRotation(location.rotX, location.rotY, location.rotZ)

                tes3mp.AddActor()

                actorCount = actorCount + 1
            end
        else
            tes3mp.LogAppend(enumerations.log.ERROR, "- Had position packet recorded for " .. uniqueIndex ..
                ", but no matching object data! Please report this to a developer")
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if actorCount > 0 then
        tes3mp.SendActorPosition()
    end
end

function BaseCell:LoadActorStatsDynamic(pid, objectData, uniqueIndexArray)

    local actorCount = 0

    tes3mp.ClearActorList()
    tes3mp.SetActorListPid(pid)
    tes3mp.SetActorListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        local splitIndex = uniqueIndex:split("-")
        tes3mp.SetActorRefNum(splitIndex[1])
        tes3mp.SetActorMpNum(splitIndex[2])

        if self:ContainsObject(uniqueIndex) and objectData[uniqueIndex].stats ~= nil then
            local stats = objectData[uniqueIndex].stats

            tes3mp.SetActorHealthBase(stats.healthBase)
            tes3mp.SetActorHealthCurrent(stats.healthCurrent)
            tes3mp.SetActorHealthModified(stats.healthModified)
            tes3mp.SetActorMagickaBase(stats.magickaBase)
            tes3mp.SetActorMagickaCurrent(stats.magickaCurrent)
            tes3mp.SetActorMagickaModified(stats.magickaModified)
            tes3mp.SetActorFatigueBase(stats.fatigueBase)
            tes3mp.SetActorFatigueCurrent(stats.fatigueCurrent)
            tes3mp.SetActorFatigueModified(stats.fatigueModified)

            tes3mp.AddActor()

            actorCount = actorCount + 1
        else
            tes3mp.LogAppend(enumerations.log.ERROR, "- Had statsDynamic packet recorded for " .. uniqueIndex ..
                ", but no matching object data! Please report this to a developer")
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if actorCount > 0 then
        tes3mp.SendActorStatsDynamic()
    end
end

function BaseCell:LoadActorEquipment(pid, objectData, uniqueIndexArray)

    local actorCount = 0

    tes3mp.ClearActorList()
    tes3mp.SetActorListPid(pid)
    tes3mp.SetActorListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        local splitIndex = uniqueIndex:split("-")
        tes3mp.SetActorRefNum(splitIndex[1])
        tes3mp.SetActorMpNum(splitIndex[2])

        if self:ContainsObject(uniqueIndex) and objectData[uniqueIndex].equipment ~= nil then
            local equipment = objectData[uniqueIndex].equipment

            for itemIndex = 0, tes3mp.GetEquipmentSize() - 1 do

                local currentItem = equipment[itemIndex]

                if currentItem ~= nil then
                    if currentItem.enchantmentCharge == nil then
                        currentItem.enchantmentCharge = -1
                    end

                    tes3mp.EquipActorItem(itemIndex, currentItem.refId, currentItem.count,
                        currentItem.charge, currentItem.enchantmentCharge)
                    tes3mp.SetActorEquipmentItemPoison(itemIndex, currentItem.poisonId or "",
                        currentItem.poisonCharges or 0)
                else
                    tes3mp.UnequipActorItem(itemIndex)
                end
            end

            tes3mp.AddActor()

            actorCount = actorCount + 1
        else
            tes3mp.LogAppend(enumerations.log.ERROR, "- Had equipment packet recorded for " .. uniqueIndex ..
                ", but no matching object data! Please report this to a developer")
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if actorCount > 0 then
        tes3mp.SendActorEquipment()
    end
end

function BaseCell:LoadActorSpellsActive(pid, objectData, uniqueIndexArray)

    local actorCount = 0

    tes3mp.ClearActorList()
    tes3mp.SetActorListPid(pid)
    tes3mp.SetActorListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        local splitIndex = uniqueIndex:split("-")
        tes3mp.SetActorRefNum(splitIndex[1])
        tes3mp.SetActorMpNum(splitIndex[2])

        if self:ContainsObject(uniqueIndex) and objectData[uniqueIndex].spellsActive ~= nil then

            packetBuilder.AddActorSpellsActive(uniqueIndex, objectData[uniqueIndex].spellsActive,
                enumerations.spellbook.SET)

            actorCount = actorCount + 1
        else
            tes3mp.LogAppend(enumerations.log.ERROR, "- Had spellsActive packet recorded for " .. uniqueIndex ..
                ", but no matching object data! Please report this to a developer")
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if actorCount > 0 then
        tes3mp.SendActorSpellsActiveChanges()
    end
end

function BaseCell:LoadActorDeath(pid, objectData, uniqueIndexArray)

    local actorCount = 0

    tes3mp.ClearActorList()
    tes3mp.SetActorListPid(pid)
    tes3mp.SetActorListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        local splitIndex = uniqueIndex:split("-")
        tes3mp.SetActorRefNum(splitIndex[1])
        tes3mp.SetActorMpNum(splitIndex[2])

        if self:ContainsObject(uniqueIndex) and objectData[uniqueIndex].deathState ~= nil then
            if objectData[uniqueIndex].stats == nil or objectData[uniqueIndex].stats.healthCurrent < 1 then
                tes3mp.SetActorDeathState(objectData[uniqueIndex].deathState)
                tes3mp.SetActorDeathInstant(true)
                tes3mp.AddActor()

                actorCount = actorCount + 1
            else
                tes3mp.LogAppend(enumerations.log.ERROR, "- Had death packet recorded for " .. uniqueIndex ..
                ", but its health is above 0! Please report this to a developer")
                tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
            end
        else
            tes3mp.LogAppend(enumerations.log.ERROR, "- Had death packet recorded for " .. uniqueIndex ..
                ", but no matching object data! Please report this to a developer")
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    if actorCount > 0 then
        tes3mp.SendActorDeath()
    end    
end

function BaseCell:LoadActorAI(pid, objectData, uniqueIndexArray)

    local actorCount = 0

    -- These packets only need to be sent to the new visitor, unless the
    -- new visitor is the target of some of them, in which case those
    -- need to be tracked and sent separately to all the cell's visitors
    local sharedPacketUniqueIndexes = {}

    tes3mp.ClearActorList()
    tes3mp.SetActorListPid(pid)
    tes3mp.SetActorListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(uniqueIndexArray) do

        local splitIndex = uniqueIndex:split("-")
        tes3mp.SetActorRefNum(splitIndex[1])
        tes3mp.SetActorMpNum(splitIndex[2])

        if self:ContainsObject(uniqueIndex) and objectData[uniqueIndex].ai ~= nil then
            local ai = objectData[uniqueIndex].ai
            local targetPid = self:ResolveSavedAITargetPid(ai, uniqueIndex)

            local isValid = true

            -- Don't allow untargeted packets that require targets
            if targetPid == nil and ai.targetUniqueIndex == nil then
                if ai.action == enumerations.ai.ACTIVATE or ai.action == enumerations.ai.COMBAT or
                    ai.action == enumerations.ai.ESCORT or ai.action == enumerations.ai.FOLLOW then

                    isValid = false
                    tes3mp.LogAppend(enumerations.log.WARN, "- Could not find valid AI target for actor " ..
                        uniqueIndex)
                end
            end

            if isValid then
                -- Is this new visitor the target of one of the actors? If so, we'll
                -- send a separate packet to every cell visitor with just that at
                -- the end
                if pid == targetPid then
                    table.insert(sharedPacketUniqueIndexes, uniqueIndex)
                else
                    packetBuilder.AddAIActor(uniqueIndex, targetPid, ai)

                    actorCount = actorCount + 1
                end
            end
        else
            tes3mp.LogAppend(enumerations.log.ERROR, "- Had AI packet recorded for " .. uniqueIndex ..
                ", but no matching object data! Please report this to a developer")
            tableHelper.removeValue(uniqueIndexArray, uniqueIndex)
        end
    end

    -- Send the packets meant for just this new visitor
    if actorCount > 0 then
        tes3mp.SendActorAI(false)
    end

    -- Send the packets targeting this visitor that all the visitors
    -- need to have
    if tableHelper.getCount(sharedPacketUniqueIndexes) > 0 then

        tes3mp.ClearActorList()
        tes3mp.SetActorListPid(pid)
        tes3mp.SetActorListCell(self.description)

        for arrayIndex, uniqueIndex in pairs(sharedPacketUniqueIndexes) do

            local splitIndex = uniqueIndex:split("-")
            tes3mp.SetActorRefNum(splitIndex[1])
            tes3mp.SetActorMpNum(splitIndex[2])
            local ai = objectData[uniqueIndex].ai
            packetBuilder.AddAIActor(uniqueIndex, pid, ai)
        end

        tes3mp.SendActorAI(true)
    end
end

function BaseCell:LoadActorCellChanges(pid, objectData)

    local temporaryLoadedCells = {}
    local actorCount = 0

    -- Move actors originally from this cell to other cells
    tes3mp.ClearActorList()
    tes3mp.SetActorListPid(pid)
    tes3mp.SetActorListCell(self.description)

    for arrayIndex, uniqueIndex in pairs(self.data.packets.cellChangeTo) do

        if objectData[uniqueIndex] ~= nil and objectData[uniqueIndex].cellChangeTo ~= nil then

            local newCellDescription = objectData[uniqueIndex].cellChangeTo

            if isTransitionalExteriorCell(newCellDescription) then
                -- X022 migration: X021 and older could already have persisted an
                -- actor into "-2147483648, -2147483648". Restore its full data
                -- to the original cell the next time that original cell loads.
                tes3mp.LogAppend(enumerations.log.WARN, "- Repairing stale transitional cell move for " ..
                    uniqueIndex .. " from " .. self.description)

                if LoadedCells[newCellDescription] == nil then
                    logicHandler.LoadCell(newCellDescription)
                    table.insert(temporaryLoadedCells, newCellDescription)
                end

                local transitionalCell = LoadedCells[newCellDescription]
                if transitionalCell ~= nil and transitionalCell.data.objectData[uniqueIndex] ~= nil then
                    transitionalCell:MoveObjectData(uniqueIndex, self)

                    tableHelper.removeValue(self.data.packets.cellChangeTo, uniqueIndex)
                    tableHelper.removeValue(self.data.packets.cellChangeFrom, uniqueIndex)

                    if self.data.objectData[uniqueIndex] ~= nil then
                        self.data.objectData[uniqueIndex].cellChangeTo = nil
                        self.data.objectData[uniqueIndex].cellChangeFrom = nil
                    end

                    tableHelper.removeValue(transitionalCell.data.packets.cellChangeTo, uniqueIndex)
                    tableHelper.removeValue(transitionalCell.data.packets.cellChangeFrom, uniqueIndex)
                    transitionalCell:QuicksaveToDrive()
                    self:QuicksaveToDrive()
                else
                    tes3mp.LogAppend(enumerations.log.ERROR, "- Could not recover " .. uniqueIndex ..
                        " because the transitional cell has no matching object data")
                    tableHelper.removeValue(self.data.packets.cellChangeTo, uniqueIndex)
                    if self.data.objectData[uniqueIndex] ~= nil then
                        self.data.objectData[uniqueIndex].cellChangeTo = nil
                    end
                end
            else
                tes3mp.SetActorCell(newCellDescription)

                local splitIndex = uniqueIndex:split("-")
                tes3mp.SetActorRefNum(splitIndex[1])
                tes3mp.SetActorMpNum(splitIndex[2])

                -- If the new cell is not loaded, load it temporarily
                if LoadedCells[newCellDescription] == nil then
                    logicHandler.LoadCell(newCellDescription)
                    table.insert(temporaryLoadedCells, newCellDescription)
                end

                if LoadedCells[newCellDescription].data.objectData[uniqueIndex] ~= nil then

                    local location = LoadedCells[newCellDescription].data.objectData[uniqueIndex].location

                    -- Ensure data integrity before proceeding
                    if tableHelper.getCount(location) == 6 and tableHelper.usesNumericalValues(location) and
                        LoadedCells[newCellDescription]:ContainsPosition(location.posX, location.posY) then

                        tes3mp.SetActorPosition(location.posX, location.posY, location.posZ)
                        tes3mp.SetActorRotation(location.rotX, location.rotY, location.rotZ)

                        tes3mp.AddActor()

                        actorCount = actorCount + 1
                    end
                else
                    tes3mp.LogAppend(enumerations.log.ERROR, "- Tried to move " .. uniqueIndex .. " from " ..
                        self.description .. " to  " .. newCellDescription .. " with no position data!")
                    objectData[uniqueIndex] = nil
                    tableHelper.removeValue(self.data.packets.cellChangeTo, uniqueIndex)
                end
            end
        else
            tes3mp.LogAppend(enumerations.log.ERROR, "- Had cellChangeTo packet recorded for " .. uniqueIndex ..
                ", but no matching cell description! Please report this to a developer")
            tableHelper.removeValue(self.data.packets.cellChangeTo, uniqueIndex)
        end
    end

    if actorCount > 0 then
        tes3mp.SendActorCellChange()
    end

    -- Go through every temporary loaded cell and unload it
    for arrayIndex, newCellDescription in pairs(temporaryLoadedCells) do
        logicHandler.UnloadCell(newCellDescription)
    end

    -- Make a table of every cell that has sent actors to this cell
    local cellChangesFrom = {}

    for arrayIndex, uniqueIndex in pairs(self.data.packets.cellChangeFrom) do

        if objectData[uniqueIndex] ~= nil and objectData[uniqueIndex].cellChangeFrom ~= nil then

            local originalCellDescription = objectData[uniqueIndex].cellChangeFrom

            if cellChangesFrom[originalCellDescription] == nil then
                cellChangesFrom[originalCellDescription] = {}
            end

            table.insert(cellChangesFrom[originalCellDescription], uniqueIndex)
        else
            tes3mp.LogAppend(enumerations.log.ERROR, "- Had cellChangeFrom packet recorded for " .. uniqueIndex ..
                ", but no matching cell description! Please report this to a developer")
            tableHelper.removeValue(self.data.packets.cellChangeFrom, uniqueIndex)
        end
    end

    local actorCount = 0

    -- Send a cell change packet for every cell that has sent actors to this cell
    for originalCellDescription, actorArray in pairs(cellChangesFrom) do

        tes3mp.ClearActorList()
        tes3mp.SetActorListPid(pid)
        tes3mp.SetActorListCell(originalCellDescription)

        for arrayIndex, uniqueIndex in pairs(actorArray) do

            local splitIndex = uniqueIndex:split("-")
            tes3mp.SetActorRefNum(splitIndex[1])
            tes3mp.SetActorMpNum(splitIndex[2])

            tes3mp.SetActorCell(self.description)

            local location = objectData[uniqueIndex].location

            -- Ensure data integrity before proceeeding
            if tableHelper.getCount(location) == 6 and tableHelper.usesNumericalValues(location) and
                self:ContainsPosition(location.posX, location.posY) then

                tes3mp.SetActorPosition(location.posX, location.posY, location.posZ)
                tes3mp.SetActorRotation(location.rotX, location.rotY, location.rotZ)

                tes3mp.AddActor()

                actorCount = actorCount + 1
            end
        end

        if actorCount > 0 then
            tes3mp.SendActorCellChange()
        end
    end
end

function BaseCell:RequestContainers(pid, requestUniqueIndexes)

    -- Partial requests (dead actor, spawned chest, etc.) must NEVER overwrite
    -- the outstanding full-cell request's pid or its timestamp.
    local isFullRequest = requestUniqueIndexes == nil
    if isFullRequest then
        self.isRequestingContainerData = true
        self.containerRequestPid = pid
        self.containerRequestTime = os.time()
    elseif not self:HasFullContainerData() then
        -- The network protocol has no full/partial reply correlation ID.
        -- Do not let a small actor/chest reply impersonate a full-cell reply.
        for _, uniqueIndex in pairs(requestUniqueIndexes) do
            self.deferredContainerRequests[uniqueIndex] = true
        end
        return
    end

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(self.description)

    -- Set the action to REQUEST
    tes3mp.SetObjectListAction(enumerations.container.REQUEST)
    tes3mp.SetObjectListContainerSubAction(enumerations.containerSub.NONE)

    -- If certain uniqueIndexes are specified, iterate through them and
    -- add them as world objects
    --
    -- Otherwise, the client will simply reply with the contents of all
    -- the containers in this cell
    if requestUniqueIndexes ~= nil and type(requestUniqueIndexes) == "table" then
        for arrayIndex, uniqueIndex in pairs(requestUniqueIndexes) do

            local splitIndex = uniqueIndex:split("-")
            tes3mp.SetObjectRefNum(splitIndex[1])
            tes3mp.SetObjectMpNum(splitIndex[2])

            if self.data.objectData[uniqueIndex] ~= nil and self.data.objectData[uniqueIndex].refId ~= nil then
                tes3mp.SetObjectRefId(self.data.objectData[uniqueIndex].refId)
            end
            tes3mp.AddObject()
        end
    end

    tes3mp.SendContainer()
end

function BaseCell:RequestActorList(pid)

    self.isRequestingActorList = true
    self.actorListRequestPid = pid

    tes3mp.ClearActorList()
    tes3mp.SetActorListPid(pid)
    tes3mp.SetActorListCell(self.description)

    -- Set the action to REQUEST
    tes3mp.SetActorListAction(3)

    tes3mp.SendActorList()
end

function BaseCell:LoadPersistentActorState(pid)

    self:EnsurePacketTables()
    self:EnsurePacketValidity()

    local objectData = self.data.objectData
    local packets = self.data.packets

    -- ObjectSpawn is actor-only in TES3MP and is idempotent on the client by
    -- mpNum, so replaying it restores dynamic NPCs without duplicating them.
    self:LoadObjectsSpawned(pid, objectData, packets.spawn)

    -- Scripted enable/disable and deletion are common ways quests change NPC
    -- existence. They are safe to reassert and prevent stale same-session views.
    self:LoadObjectsDeleted(pid, objectData, packets.delete)
    self:LoadObjectStates(pid, objectData, packets.state)

    self:LoadActorCellChanges(pid, objectData)
    self:LoadActorDeath(pid, objectData, packets.death)
    self:LoadActorEquipment(pid, objectData, packets.equipment)
    self:LoadActorSpellsActive(pid, objectData, packets.spellsActive)
    self:LoadActorAI(pid, objectData, packets.ai)
end

function BaseCell:LoadInitialCellData(pid)

    self:EnsurePacketTables()
    self:EnsurePacketValidity()

    if self.data.loadState == nil then
        self.data.loadState = {
            hasFullActorList = false,
            hasFullContainerData = false
        }
    end

    tes3mp.LogMessage(enumerations.log.INFO, "Loading data of cell " .. self.description .. " for " ..
        logicHandler.GetChatName(pid))

    local objectData = self.data.objectData
    local packets = self.data.packets
    -- Diagnose a slow large-cell transfer once, rather than logging each object.
    local profile = tableHelper.getCount(packets.place or {})>=128
    local clock = function()
        local value=type(tes3mp.GetMillisecondsSinceServerStart)=="function" and tes3mp.GetMillisecondsSinceServerStart()
        return tonumber(value) or math.floor(os.clock()*1000)
    end
    local started=profile and clock() or 0
    local placementMs,containerMs=0,0

    self:LoadObjectsDeleted(pid, objectData, packets.delete)
    local phaseStart=profile and clock() or 0
    -- Y105: saved placement provenance must not become a fresh client drop.
    self:LoadObjectsPlaced(pid, objectData, packets.place, nil, true)
    if profile then placementMs=(clock()-phaseStart)%4294967296 end
    self:LoadObjectsSpawned(pid, objectData, packets.spawn)
    self:LoadObjectsMoved(pid, objectData, packets.move)
    self:LoadObjectsRotated(pid, objectData, packets.rotate)
    self:LoadObjectsLocked(pid, objectData, packets.lock)
    self:LoadObjectTrapsTriggered(pid, objectData, packets.trap)
    self:LoadObjectsScaled(pid, objectData, packets.scale)
    self:LoadObjectsMiscellaneous(pid, objectData, packets.miscellaneous)
    self:LoadObjectStates(pid, objectData, packets.state)
    self:LoadDoorStates(pid, objectData, packets.doorState)
    self:LoadClientScriptLocals(pid, objectData, packets.clientScriptLocal)

    phaseStart=profile and clock() or 0
    self:LoadContainers(pid, objectData, packets.container)
    if profile then containerMs=(clock()-phaseStart)%4294967296 end

    self:LoadActorCellChanges(pid, objectData)
    self:LoadActorDeath(pid, objectData, packets.death)
    self:LoadActorEquipment(pid, objectData, packets.equipment)
    self:LoadActorSpellsActive(pid, objectData, packets.spellsActive)
    self:LoadActorAI(pid, objectData, packets.ai)
    if profile then
        local elapsed=(clock()-started)%4294967296
        if elapsed>=250 then
            tes3mp.LogMessage(enumerations.log.WARN,"[CellLoad] Slow transfer " .. self.description ..
                "; total=" .. elapsed .. "ms; placements=" .. placementMs .. "ms; containers=" .. containerMs .. "ms")
        end
    end
    self.initialDataSentTo = self.initialDataSentTo or {}
    self.initialDataSentTo[pid] = Players[pid]

end

function BaseCell:LoadMomentaryCellData(pid)

    local objectData = self.data.objectData
    local packets = self.data.packets

    self:LoadActorPositions(pid, objectData, packets.position)
    self:LoadActorStatsDynamic(pid, objectData, packets.statsDynamic)
end

function BaseCell:LoadGeneratedRecords(pid)

    if self.data.recordLinks == nil then self.data.recordLinks = {} end

    local recordLinks = self.data.recordLinks

    for storeType, recordList in pairs(recordLinks) do

        local recordStore = RecordStores[storeType]

        if recordStore ~= nil then
            recordStore:LoadGeneratedRecords(pid, recordStore.data.generatedRecords,
                tableHelper.getArrayFromIndexes(recordList))
        end
    end
end

return BaseCell
