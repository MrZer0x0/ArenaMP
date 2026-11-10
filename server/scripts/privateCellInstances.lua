require("config")

local packetBuilder = require("packetBuilder")
local localization = require("localization")

local privateCellInstances = {}

local function getDefinitions()
    if type(config.privateCellInstances) ~= "table" then
        return {}
    end
    return config.privateCellInstances
end

local function isEnabled(definition)
    return type(definition) == "table" and definition.enabled ~= false and
        type(definition.baseCellDescription) == "string" and definition.baseCellDescription ~= ""
end

local function getOwnerName(playerOrName)
    if type(playerOrName) == "table" then
        return playerOrName.accountName or playerOrName.name
    end
    if playerOrName ~= nil then
        return tostring(playerOrName)
    end
    return nil
end

local function getSuffix(definition)
    if type(definition.instanceSuffix) == "string" and definition.instanceSuffix ~= "" then
        return definition.instanceSuffix
    end
    return " - Instance for "
end

local function startsWithCaseInsensitive(value, prefix)
    if type(value) ~= "string" or type(prefix) ~= "string" then
        return false
    end
    return string.sub(string.lower(value), 1, #prefix) == string.lower(prefix)
end

function privateCellInstances.GetInstanceCellDescription(definition, playerOrName)
    if not isEnabled(definition) then
        return nil
    end

    local ownerName = getOwnerName(playerOrName)
    if ownerName == nil or ownerName == "" then
        return nil
    end

    return definition.baseCellDescription .. getSuffix(definition) .. ownerName
end

function privateCellInstances.GetDefinitionForCell(cellDescription)
    if type(cellDescription) ~= "string" or cellDescription == "" then
        return nil, nil
    end

    local lowerCell = string.lower(cellDescription)
    for key, definition in pairs(getDefinitions()) do
        if isEnabled(definition) then
            local baseCell = definition.baseCellDescription
            local instancePrefix = baseCell .. getSuffix(definition)

            if lowerCell == string.lower(baseCell) or startsWithCaseInsensitive(cellDescription, instancePrefix) then
                return key, definition
            end
        end
    end

    return nil, nil
end

function privateCellInstances.GetOwnCellForTarget(playerOrName, cellDescription)
    local key, definition = privateCellInstances.GetDefinitionForCell(cellDescription)
    if definition == nil then
        return nil, nil, nil
    end

    return privateCellInstances.GetInstanceCellDescription(definition, playerOrName), key, definition
end

function privateCellInstances.IsOwnPrivateCell(playerOrName, cellDescription)
    local ownCell = privateCellInstances.GetOwnCellForTarget(playerOrName, cellDescription)
    return ownCell ~= nil and ownCell == cellDescription
end

-- Send the dynamic CELL record for every personal instance to exactly one client.
-- The record inherits all static content from the original interior through baseId,
-- while the server stores dynamic state under the unique instance description.
function privateCellInstances.SendCellRecords(pid, playerOrName)
    local sent = 0

    for _, definition in pairs(getDefinitions()) do
        if isEnabled(definition) then
            local instanceCell = privateCellInstances.GetInstanceCellDescription(definition, playerOrName)
            if instanceCell ~= nil then
                tes3mp.ClearRecords()
                tes3mp.SetRecordType(enumerations.recordType["CELL"])
                packetBuilder.AddCellRecord(instanceCell, {baseId = definition.baseCellDescription})
                tes3mp.SendRecordDynamic(pid, false, false)
                sent = sent + 1
            end
        end
    end

    return sent
end

-- Make the mapping persistent in the player profile as well as deterministic.
-- If an old profile points Caius' house somewhere else, the private instance wins.
function privateCellInstances.EnsurePlayerDestinationOverrides(player)
    if type(player) ~= "table" or type(player.data) ~= "table" then
        return false
    end

    if type(player.data.destinationOverrides) ~= "table" then
        player.data.destinationOverrides = {}
    end

    local changed = false
    for _, definition in pairs(getDefinitions()) do
        if isEnabled(definition) then
            local instanceCell = privateCellInstances.GetInstanceCellDescription(definition, player)
            if instanceCell ~= nil and player.data.destinationOverrides[definition.baseCellDescription] ~= instanceCell then
                player.data.destinationOverrides[definition.baseCellDescription] = instanceCell
                changed = true
            end
        end
    end

    return changed
end

-- StateHelper currently clears the native destination-override store on each load.
-- Call this after both player and world overrides have been loaded so personal
-- interiors are the final authoritative mapping for their base cell.
function privateCellInstances.ApplyDestinationOverrides(pid, playerOrName)
    local count = 0

    -- Re-append all player-specific overrides after the world set. In the stock
    -- script StateHelper clears the native store for each load, so loading the
    -- world immediately after the player would otherwise discard the player's
    -- mappings. This also preserves existing admin/user destination overrides.
    if type(playerOrName) == "table" and type(playerOrName.data) == "table" and
        type(playerOrName.data.destinationOverrides) == "table" then
        for oldCellDescription, newCellDescription in pairs(playerOrName.data.destinationOverrides) do
            tes3mp.AddDestinationOverride(oldCellDescription, newCellDescription)
            count = count + 1
        end
    end

    -- Personal-cell routes are appended last so they always win for their base
    -- cell even if a stale profile override existed before C18.
    for _, definition in pairs(getDefinitions()) do
        if isEnabled(definition) then
            local instanceCell = privateCellInstances.GetInstanceCellDescription(definition, playerOrName)
            if instanceCell ~= nil then
                tes3mp.AddDestinationOverride(definition.baseCellDescription, instanceCell)
                count = count + 1
            end
        end
    end

    if count > 0 then
        tes3mp.SendWorldDestinationOverride(pid)
    end

    return count
end

-- Existing profiles may have logged out in the shared base cell or, after an old
-- admin teleport, in another player's personal instance. Normalize them before
-- LoadCell() so they never become visitors of the wrong cell on login.
function privateCellInstances.NormalizeSavedLocation(player)
    if type(player) ~= "table" or type(player.data) ~= "table" or type(player.data.location) ~= "table" then
        return false
    end

    local currentCell = player.data.location.cell
    local ownCell = privateCellInstances.GetOwnCellForTarget(player, currentCell)
    if ownCell ~= nil and ownCell ~= currentCell then
        player.data.location.cell = ownCell
        return true
    end

    return false
end

-- Fallback for coc/script/admin teleports that bypass destination overrides.
-- Keep the position and rotation from the attempted target; only substitute the
-- cell record. A normal follow-up PlayerCellChange will then save/load the cell.
function privateCellInstances.RedirectCellChange(pid, player, playerPacket)
    if type(playerPacket) ~= "table" or type(playerPacket.location) ~= "table" then
        return false, nil, nil, nil
    end

    local currentCell = playerPacket.location.cell
    local ownCell, key, definition = privateCellInstances.GetOwnCellForTarget(player, currentCell)
    if ownCell == nil or ownCell == currentCell then
        return false, ownCell, key, definition
    end

    tes3mp.SetCell(pid, ownCell)
    tes3mp.SetPos(pid, playerPacket.location.posX, playerPacket.location.posY, playerPacket.location.posZ)
    tes3mp.SetRot(pid, playerPacket.location.rotX, playerPacket.location.rotZ)
    tes3mp.SendCell(pid)
    tes3mp.SendPos(pid)

    tes3mp.LogAppend(enumerations.log.INFO,
        "[ArenaMP Core] Redirected " .. tostring(player.accountName or player.name or pid) ..
        " from " .. tostring(currentCell) .. " to " .. tostring(ownCell))

    return true, ownCell, key, definition
end

function privateCellInstances.NotifyIfInside(player, cellDescription)
    if type(player) ~= "table" or player.pid == nil then
        return false
    end

    local currentCell = cellDescription
    if currentCell == nil then
        currentCell = tes3mp.GetCell(player.pid)
    end

    local ownCell, key, definition = privateCellInstances.GetOwnCellForTarget(player, currentCell)
    if ownCell == nil or ownCell ~= currentCell or definition == nil then
        return false
    end

    if type(player.privateCellNoticesShown) ~= "table" then
        player.privateCellNoticesShown = {}
    end

    if definition.noticeEveryEntry ~= true and player.privateCellNoticesShown[key] == true then
        return false
    end

    local noticeKey = definition.noticeKey or "private_cell_notice"
    local message = localization.Get(player.pid, "core", noticeKey, {cell = definition.baseCellDescription})
    if message ~= nil and message ~= "" then
        tes3mp.MessageBox(player.pid, -1, message)
        player.privateCellNoticesShown[key] = true
        return true
    end

    return false
end


-- X043: normalize a generated personal instance back to its vanilla/template
-- cell. Server quests use this so a giver authored for the base interior also
-- exists in every player's private copy without making progress shared.
function privateCellInstances.GetBaseCellDescription(cellDescription)
    local _, definition = privateCellInstances.GetDefinitionForCell(cellDescription)
    if definition ~= nil then
        return definition.baseCellDescription
    end
    return cellDescription
end

local function copyLocation(location)
    if type(location) ~= "table" then return nil end
    return {
        cell = location.cell,
        posX = location.posX,
        posY = location.posY,
        posZ = location.posZ,
        rotX = location.rotX,
        rotZ = location.rotZ,
        regionName = location.regionName
    }
end

local function sendAuthoritativeLocation(pid, location)
    if type(location) ~= "table" or type(location.cell) ~= "string" or location.cell == "" then
        return false
    end

    tes3mp.SetCell(pid, location.cell)
    if location.posX ~= nil and location.posY ~= nil and location.posZ ~= nil then
        tes3mp.SetPos(pid, location.posX, location.posY, location.posZ)
    end
    if location.rotX ~= nil and location.rotZ ~= nil then
        tes3mp.SetRot(pid, location.rotX, location.rotZ)
    end
    tes3mp.SendCell(pid)
    if location.posX ~= nil and location.posY ~= nil and location.posZ ~= nil then
        tes3mp.SendPos(pid)
    end
    return true
end

-- X043: dynamic CELL records sent on OnPlayerConnect can arrive too early to be
-- useful during a reconnect. Re-send them immediately before LoadCell and keep
-- the saved transform authoritative until the client confirms that exact
-- instance. This prevents the login/start-location packet from overwriting the
-- instance save and prevents spawning below an unresolved dynamic interior.
function privateCellInstances.PrepareLoginRestore(player)
    if type(player) ~= "table" or player.pid == nil or type(player.data) ~= "table"
        or type(player.data.location) ~= "table" then
        return false
    end

    local cellDescription = player.data.location.cell
    if not privateCellInstances.IsOwnPrivateCell(player, cellDescription) then
        player.privateCellLoginRestore = nil
        return false
    end

    privateCellInstances.SendCellRecords(player.pid, player)
    player.privateCellLoginRestore = copyLocation(player.data.location)
    player.privateCellLoginRestore.startedAt = os.time()
    player.privateCellLoginRestore.redirects = 0

    tes3mp.LogAppend(enumerations.log.INFO,
        "[ArenaMP Core] Prepared login restore for " ..
        tostring(player.accountName or player.name or player.pid) .. " in " .. tostring(cellDescription))
    return true
end

-- Returns true when the incoming PlayerCellChange was a stale login packet and
-- has been consumed. When the expected instance arrives, the packet's transform
-- is replaced with the saved server transform before normal SaveCell handling.
function privateCellInstances.HandleLoginCellChange(pid, player, playerPacket)
    if type(player) ~= "table" or type(player.privateCellLoginRestore) ~= "table"
        or type(playerPacket) ~= "table" or type(playerPacket.location) ~= "table" then
        return false
    end

    local restore = player.privateCellLoginRestore
    if os.time() - (restore.startedAt or os.time()) > 15 then
        tes3mp.LogAppend(enumerations.log.WARN,
            "[ArenaMP Core] Login restore timed out for " ..
            tostring(player.accountName or player.name or pid))
        player.privateCellLoginRestore = nil
        return false
    end

    local incomingCell = tostring(playerPacket.location.cell or "")
    if incomingCell ~= tostring(restore.cell or "") then
        restore.redirects = (restore.redirects or 0) + 1
        privateCellInstances.SendCellRecords(pid, player)
        sendAuthoritativeLocation(pid, restore)
        tes3mp.LogAppend(enumerations.log.INFO,
            "[ArenaMP Core] Ignored stale login cell " .. incomingCell ..
            "; restoring " .. tostring(restore.cell))
        return true
    end

    playerPacket.location.posX = restore.posX
    playerPacket.location.posY = restore.posY
    playerPacket.location.posZ = restore.posZ
    playerPacket.location.rotX = restore.rotX
    playerPacket.location.rotZ = restore.rotZ
    if restore.regionName ~= nil then playerPacket.location.regionName = restore.regionName end

    -- Apply position once more after the client has confirmed the dynamic cell;
    -- SendCell before the CELL record is resolved is exactly the race that used
    -- to leave reconnecting players falling in empty space.
    if restore.posX ~= nil and restore.posY ~= nil and restore.posZ ~= nil then
        tes3mp.SetPos(pid, restore.posX, restore.posY, restore.posZ)
        if restore.rotX ~= nil and restore.rotZ ~= nil then
            tes3mp.SetRot(pid, restore.rotX, restore.rotZ)
        end
        tes3mp.SendPos(pid)
    end

    tes3mp.LogAppend(enumerations.log.INFO,
        "[ArenaMP Core] Login restore confirmed in " .. tostring(restore.cell) ..
        " after " .. tostring(restore.redirects or 0) .. " stale packet(s)")
    player.privateCellLoginRestore = nil
    return false
end

function privateCellInstances.IsNeverResetCell(cellDescription)
    local _, definition = privateCellInstances.GetDefinitionForCell(cellDescription)
    if definition == nil or definition.neverReset ~= true then
        return false
    end

    -- The shared base cell is only a routing target. The permanent exemption is
    -- specifically for generated personal instances.
    local prefix = definition.baseCellDescription .. getSuffix(definition)
    return startsWithCaseInsensitive(cellDescription, prefix)
end

return privateCellInstances
