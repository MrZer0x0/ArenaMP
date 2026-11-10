StateHelper = class("StateHelper")
local loginSyncHelper = require("loginSyncHelper")

function StateHelper:LoadJournal(pid, stateObject)

    if stateObject.data.journal == nil then
        stateObject.data.journal = {}
    end

    tes3mp.ClearJournalChanges(pid)

    for index, journalItem in pairs(stateObject.data.journal) do

        if journalItem.type == enumerations.journal.ENTRY then

            if journalItem.actorRefId == nil then
                journalItem.actorRefId = "player"
            end

            if journalItem.timestamp ~= nil then
                tes3mp.AddJournalEntryWithTimestamp(pid, journalItem.quest, journalItem.index, journalItem.actorRefId,
                    journalItem.timestamp.daysPassed, journalItem.timestamp.month, journalItem.timestamp.day)
            else
                tes3mp.AddJournalEntry(pid, journalItem.quest, journalItem.index, journalItem.actorRefId)
            end
        else
            tes3mp.AddJournalIndex(pid, journalItem.quest, journalItem.index)
        end
    end

    tes3mp.SendJournalChanges(pid)
end

function StateHelper:LoadFactionRanks(pid, stateObject)

    if stateObject.data.factionRanks == nil then
        stateObject.data.factionRanks = {}
    end

    tes3mp.ClearFactionChanges(pid)
    tes3mp.SetFactionChangesAction(pid, enumerations.faction.RANK)

    for factionId, rank in pairs(stateObject.data.factionRanks) do

        tes3mp.SetFactionId(factionId)
        tes3mp.SetFactionRank(rank)
        tes3mp.AddFaction(pid)
    end

    tes3mp.SendFactionChanges(pid)
end

function StateHelper:LoadFactionExpulsion(pid, stateObject)

    if stateObject.data.factionExpulsion == nil then
        stateObject.data.factionExpulsion = {}
    end

    tes3mp.ClearFactionChanges(pid)
    tes3mp.SetFactionChangesAction(pid, enumerations.faction.EXPULSION)

    for factionId, state in pairs(stateObject.data.factionExpulsion) do

        tes3mp.SetFactionId(factionId)
        tes3mp.SetFactionExpulsionState(state)
        tes3mp.AddFaction(pid)
    end

    tes3mp.SendFactionChanges(pid)
end

-- Y052: clear saved guild expulsion flags for one client on every fresh login.
-- This deliberately touches only expulsion state; faction rank and reputation
-- remain intact. When faction expulsion is globally shared we only clear the
-- client's runtime flags, never the shared world table.
function StateHelper:ClearFactionExpulsionForLogin(pid, stateObject, clearStoredState)

    if stateObject == nil or type(stateObject.data) ~= "table" then
        return
    end

    if stateObject.data.factionExpulsion == nil then
        stateObject.data.factionExpulsion = {}
    end

    local factionIds = {}
    for factionId, _ in pairs(stateObject.data.factionExpulsion) do
        table.insert(factionIds, factionId)
    end

    if #factionIds > 0 then
        tes3mp.ClearFactionChanges(pid)
        tes3mp.SetFactionChangesAction(pid, enumerations.faction.EXPULSION)

        for _, factionId in ipairs(factionIds) do
            tes3mp.SetFactionId(factionId)
            tes3mp.SetFactionExpulsionState(false)
            tes3mp.AddFaction(pid)
        end

        tes3mp.SendFactionChanges(pid)
    end

    if clearStoredState == true and #factionIds > 0 then
        stateObject.data.factionExpulsion = {}
        if type(stateObject.QuicksaveToDrive) == "function" then
            stateObject:QuicksaveToDrive()
        elseif type(stateObject.SaveToDrive) == "function" then
            stateObject:SaveToDrive()
        end
    end
end

-- Y052: reset the script globals that vanilla Morrowind and supported content
-- use for guild-expulsion forgiveness/timers. The IDs come from
-- clientVariableScopes, so Tamriel Rebuilt/Skyrim Home of the Nords/etc. are
-- covered without hardcoding another faction list here.
function StateHelper:ClearFactionExpulsionGlobalsForLogin(pid, playerState, worldState, clearStoredState)

    if type(clientVariableScopes) ~= "table"
        or type(clientVariableScopes.globals) ~= "table"
        or type(clientVariableScopes.globals.factionExpulsion) ~= "table" then
        return
    end

    local function findStored(stateObject, wantedId)
        if stateObject == nil or type(stateObject.data) ~= "table"
            or type(stateObject.data.clientVariables) ~= "table"
            or type(stateObject.data.clientVariables.globals) ~= "table" then
            return nil, nil
        end

        local wanted = string.lower(tostring(wantedId))
        for storedId, variable in pairs(stateObject.data.clientVariables.globals) do
            if string.lower(tostring(storedId)) == wanted then
                return storedId, variable
            end
        end
        return nil, nil
    end

    tes3mp.ClearClientGlobals()
    local count, removed = 0, false

    for _, variableId in ipairs(clientVariableScopes.globals.factionExpulsion) do
        local storedKey, variable = findStored(playerState, variableId)
        if variable == nil then
            local worldKey
            worldKey, variable = findStored(worldState, variableId)
        end

        local variableType = type(variable) == "table" and variable.variableType
            or enumerations.variableType.SHORT

        if variableType == enumerations.variableType.FLOAT then
            tes3mp.AddClientGlobalFloat(variableId, 0)
        else
            if variableType ~= enumerations.variableType.LONG then
                variableType = enumerations.variableType.SHORT
            end
            tes3mp.AddClientGlobalInteger(variableId, 0, variableType)
        end
        count = count + 1

        if clearStoredState == true and storedKey ~= nil
            and type(playerState) == "table" and type(playerState.data) == "table"
            and type(playerState.data.clientVariables) == "table"
            and type(playerState.data.clientVariables.globals) == "table" then
            playerState.data.clientVariables.globals[storedKey] = nil
            removed = true
        end
    end

    if count > 0 then
        tes3mp.SendClientScriptGlobal(pid)
    end

    if removed and playerState ~= nil then
        if type(playerState.QuicksaveToDrive) == "function" then
            playerState:QuicksaveToDrive()
        elseif type(playerState.SaveToDrive) == "function" then
            playerState:SaveToDrive()
        end
    end
end

function StateHelper:LoadFactionReputation(pid, stateObject)

    if stateObject.data.factionReputation == nil then
        stateObject.data.factionReputation = {}
    end

    tes3mp.ClearFactionChanges(pid)
    tes3mp.SetFactionChangesAction(pid, enumerations.faction.REPUTATION)

    for factionId, reputation in pairs(stateObject.data.factionReputation) do

        tes3mp.SetFactionId(factionId)
        tes3mp.SetFactionReputation(reputation)
        tes3mp.AddFaction(pid)
    end

    tes3mp.SendFactionChanges(pid)
end

function StateHelper:LoadTopics(pid, stateObject)

    if stateObject.data.topics == nil then
        stateObject.data.topics = {}
    end

    tes3mp.ClearTopicChanges(pid)

    for index, topicId in pairs(stateObject.data.topics) do

        tes3mp.AddTopic(pid, topicId)
    end

    tes3mp.SendTopicChanges(pid)
end

function StateHelper:LoadBounty(pid, stateObject)

    if stateObject.data.fame == nil then
        stateObject.data.fame = { bounty = 0, reputation = 0 }
    elseif stateObject.data.fame.bounty == nil then
        stateObject.data.fame.bounty = 0
    end

    -- Update old player files to the new format
    if stateObject.data.stats ~= nil and stateObject.data.stats.bounty ~= nil then
        stateObject.data.fame.bounty = stateObject.data.stats.bounty
        stateObject.data.stats.bounty = nil
    end

    tes3mp.SetBounty(pid, stateObject.data.fame.bounty)
    tes3mp.SendBounty(pid)
end

function StateHelper:LoadReputation(pid, stateObject)

    if stateObject.data.fame == nil then
        stateObject.data.fame = { bounty = 0, reputation = 0 }
    elseif stateObject.data.fame.reputation == nil then
        stateObject.data.fame.reputation = 0
    end

    tes3mp.SetReputation(pid, stateObject.data.fame.reputation)
    tes3mp.SendReputation(pid)
end

function StateHelper:LoadClientScriptVariables(pid, stateObject)

    if stateObject.data.clientVariables == nil then
        stateObject.data.clientVariables = {}
    end

    if stateObject.data.clientVariables.globals == nil then
        stateObject.data.clientVariables.globals = {}
    end

    local variableCount = 0

    tes3mp.ClearClientGlobals()

    for variableId, variableTable in pairs(stateObject.data.clientVariables.globals) do

        if type(variableTable) == "table" then

            if variableTable.variableType == enumerations.variableType.SHORT then
                tes3mp.AddClientGlobalInteger(variableId, variableTable.intValue, enumerations.variableType.SHORT)
            elseif variableTable.variableType == enumerations.variableType.LONG then
                tes3mp.AddClientGlobalInteger(variableId, variableTable.intValue, enumerations.variableType.LONG)
            elseif variableTable.variableType == enumerations.variableType.FLOAT then
                tes3mp.AddClientGlobalFloat(variableId, variableTable.floatValue)
            end

            variableCount = variableCount + 1
        end
    end

    if variableCount > 0 then
        tes3mp.SendClientScriptGlobal(pid)
    end
end

-- Start of AMP addition
--
-- Storage for the local variables of scripts attached to a player character.
--
-- Everything here is keyed on the lowercased script ID and remembers the original casing
-- alongside it, because Morrowind script IDs are case insensitive but the content files
-- are inconsistent about how they spell them.
function StateHelper:LoadClientScriptLocals(pid, stateObject)

    if stateObject.data.clientVariables == nil then
        stateObject.data.clientVariables = {}
    end

    if stateObject.data.clientVariables.locals == nil then
        stateObject.data.clientVariables.locals = {}
        return
    end

    local objectCount = 0

    -- At login the client has not reported a cell yet, so fall back to the one stored in
    -- the profile. The packet is sent straight to the attached player either way, but the
    -- cell still has to be something the client can parse.
    local cellDescription = tes3mp.GetCell(pid)

    if cellDescription == nil or cellDescription == "" then
        if stateObject.data.location ~= nil and stateObject.data.location.cell ~= nil then
            cellDescription = stateObject.data.location.cell
        else
            cellDescription = ""
        end
    end

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(cellDescription)

    for scriptKey, scriptTable in pairs(stateObject.data.clientVariables.locals) do

        if type(scriptTable) == "table" and type(scriptTable.variables) == "table" then

            local scriptId = scriptTable.scriptId or scriptKey

            if packetBuilder.AddPlayerClientScriptLocal(pid, scriptId, scriptTable.variables) then
                objectCount = objectCount + 1
            end
        end
    end

    if objectCount > 0 then
        tes3mp.SendClientScriptLocal(false, false)
    end
end

function StateHelper:SaveClientScriptLocal(stateObject, scriptId, variables)

    if scriptId == nil or scriptId == "" or type(variables) ~= "table" then return end

    if stateObject.data.clientVariables == nil then
        stateObject.data.clientVariables = {}
    end

    if stateObject.data.clientVariables.locals == nil then
        stateObject.data.clientVariables.locals = {}
    end

    local scriptKey = string.lower(scriptId)
    local storedScript = stateObject.data.clientVariables.locals[scriptKey]

    if storedScript == nil then
        storedScript = { scriptId = scriptId, variables = {} }
        stateObject.data.clientVariables.locals[scriptKey] = storedScript
    end

    if type(storedScript.variables) ~= "table" then
        storedScript.variables = {}
    end

    storedScript.scriptId = scriptId

    for variableType, variableTable in pairs(variables) do

        if type(variableTable) == "table" then

            if storedScript.variables[variableType] == nil then
                storedScript.variables[variableType] = {}
            end

            for internalIndex, value in pairs(variableTable) do
                storedScript.variables[variableType][internalIndex] = value
            end
        end
    end

    stateObject:QuicksaveToDrive()
end

function StateHelper:ClearClientScriptLocals(stateObject, scriptId)

    if stateObject.data.clientVariables == nil then return end
    if stateObject.data.clientVariables.locals == nil then return end

    if scriptId == nil then
        stateObject.data.clientVariables.locals = {}
    else
        stateObject.data.clientVariables.locals[string.lower(scriptId)] = nil
    end

    stateObject:QuicksaveToDrive()
end
-- End of AMP addition

function StateHelper:LoadDestinationOverrides(pid, stateObject)

    if stateObject.data.destinationOverrides == nil then
        stateObject.data.destinationOverrides = {}
    end

    local destinationCount = 0

    tes3mp.ClearDestinationOverrides()

    for oldCellDescription, newCellDescription in pairs(stateObject.data.destinationOverrides) do

        tes3mp.AddDestinationOverride(oldCellDescription, newCellDescription)
        destinationCount = destinationCount + 1
    end

    if destinationCount > 0 then
        tes3mp.SendWorldDestinationOverride(pid)
    end
end

function StateHelper:LoadMap(pid, stateObject)

    if stateObject.data.mapExplored == nil then
        stateObject.data.mapExplored = {}
    end

    -- Y110: reading hundreds/thousands of PNGs here blocked the shared server
    -- thread during every login. The timer sends only this player's map, in
    -- globally bounded batches, while ordinary gameplay packets keep flowing.
    loginSyncHelper.QueueMap(pid, stateObject.data.mapExplored)
end

function StateHelper:SaveJournal(stateObject, playerPacket)

    if stateObject.data.journal == nil then
        stateObject.data.journal = {}
    end

    if stateObject.data.customVariables == nil then
        stateObject.data.customVariables = {}
    end

    for _, journalItem in ipairs(playerPacket.journal) do

        table.insert(stateObject.data.journal, journalItem)

        if journalItem.quest == "a1_1_findspymaster" and journalItem.index >= 14 then
            stateObject.data.customVariables.deliveredCaiusPackage = true
        end
    end

    stateObject:QuicksaveToDrive()
end

function StateHelper:SaveFactionRanks(pid, stateObject)

    if stateObject.data.factionRanks == nil then
        stateObject.data.factionRanks = {}
    end

    for i = 0, tes3mp.GetFactionChangesSize(pid) - 1 do

        local factionId = tes3mp.GetFactionId(pid, i)
        stateObject.data.factionRanks[factionId] = tes3mp.GetFactionRank(pid, i)
    end

    stateObject:QuicksaveToDrive()
end

function StateHelper:SaveFactionExpulsion(pid, stateObject)

    if stateObject.data.factionExpulsion == nil then
        stateObject.data.factionExpulsion = {}
    end

    for i = 0, tes3mp.GetFactionChangesSize(pid) - 1 do

        local factionId = tes3mp.GetFactionId(pid, i)
        stateObject.data.factionExpulsion[factionId] = tes3mp.GetFactionExpulsionState(pid, i)
    end

    stateObject:QuicksaveToDrive()
end

function StateHelper:SaveFactionReputation(pid, stateObject)

    if stateObject.data.factionReputation == nil then
        stateObject.data.factionReputation = {}
    end

    for i = 0, tes3mp.GetFactionChangesSize(pid) - 1 do

        local factionId = tes3mp.GetFactionId(pid, i)
        stateObject.data.factionReputation[factionId] = tes3mp.GetFactionReputation(pid, i)
    end

    stateObject:QuicksaveToDrive()
end

function StateHelper:SaveTopics(pid, stateObject)

    if stateObject.data.topics == nil then
        stateObject.data.topics = {}
    end

    for i = 0, tes3mp.GetTopicChangesSize(pid) - 1 do

        local topicId = tes3mp.GetTopicId(pid, i)

        if not tableHelper.containsValue(stateObject.data.topics, topicId) then
            table.insert(stateObject.data.topics, topicId)
        end
    end

    stateObject:QuicksaveToDrive()
end

function StateHelper:SaveBounty(pid, stateObject)

    if stateObject.data.fame == nil then
        stateObject.data.fame = {}
    end

    stateObject.data.fame.bounty = tes3mp.GetBounty(pid)

    stateObject:QuicksaveToDrive()
end

function StateHelper:SaveReputation(pid, stateObject)

    if stateObject.data.fame == nil then
        stateObject.data.fame = {}
    end

    stateObject.data.fame.reputation = tes3mp.GetReputation(pid)

    stateObject:QuicksaveToDrive()
end

function StateHelper:SaveClientScriptGlobal(stateObject, variables)

    if stateObject.data.clientVariables == nil then
        stateObject.data.clientVariables = {}
    end

    if stateObject.data.clientVariables.globals == nil then
        stateObject.data.clientVariables.globals = {}
    end

    for id, variable in pairs (variables) do
        stateObject.data.clientVariables.globals[id] = variable
    end

    stateObject:QuicksaveToDrive()
end

function StateHelper:SaveMapExploration(pid, stateObject)

    local cell = tes3mp.GetCell(pid)

    if tes3mp.IsInExterior(pid) == true then
        if not tableHelper.containsValue(stateObject.data.mapExplored, cell) then
            table.insert(stateObject.data.mapExplored, cell)
        end
    end
end

return StateHelper
