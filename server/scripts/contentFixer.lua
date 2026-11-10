tableHelper = require("tableHelper")
require("utils")

local contentFixer = {}

local deadlyItems = { "keening", "sunder" }
local fixesByCell = {}

-- Delete the chargen boat and associated guards and objects
fixesByCell["-1, -9"] = { disable =  { 268178, 297457, 297459, 297460, 299125 }}
fixesByCell["-2, -9"] = { disable = { 172848, 172850, 172852, 289104, 297461, 397559 }}
fixesByCell["-2, -10"] = { disable = { 297463, 297464, 297465, 297466 }}

-- Delete the census papers and unlock the doors
fixesByCell["Seyda Neen, Census and Excise Office"] = { disable = { 172859 }, unlock = { 119513, 172860 }}

function contentFixer.FixCell(pid, cellDescription)

    if fixesByCell[cellDescription] ~= nil then

        for action, refNumArray in pairs(fixesByCell[cellDescription]) do

            tes3mp.ClearObjectList()
            tes3mp.SetObjectListPid(pid)
            tes3mp.SetObjectListCell(cellDescription)

            for arrayIndex, refNum in ipairs(refNumArray) do
                tes3mp.SetObjectRefNum(refNum)
                tes3mp.SetObjectMpNum(0)
                tes3mp.SetObjectRefId("")
                if action == "disable" then tes3mp.SetObjectState(false) end
                if action == "unlock" then tes3mp.SetObjectLockLevel(0) end
                tes3mp.AddObject()
            end

            if action == "delete" then
                tes3mp.SendObjectDelete()
            elseif action == "disable" then
                tes3mp.SendObjectState()
            elseif action == "unlock" then
                tes3mp.SendObjectLock()
            end
        end
    end
end

-- Unequip items that damage the player when worn
--
-- Note: Items with constant damage effects like Whitewalker and the Mantle of Woe
--       are already unequipped by default in the TES3MP client, so this only needs
--       to account for scripted items that are missed there
--
function contentFixer.UnequipDeadlyItems(pid)

    local itemsFound = 0

    for arrayIndex, itemRefId in pairs(deadlyItems) do
        if tableHelper.containsKeyValue(Players[pid].data.equipment, "refId", itemRefId, true) then
            local itemSlot = tableHelper.getIndexByNestedKeyValue(Players[pid].data.equipment, "refId", itemRefId, true)
            Players[pid].data.equipment[itemSlot] = nil
            itemsFound = itemsFound + 1
        end
    end

    if itemsFound > 0 then
        Players[pid]:QuicksaveToDrive()
        Players[pid]:LoadEquipment()
    end
end

function contentFixer.AdjustSharedCorprusState(pid)

    local corprusId = "corprus"

    if WorldInstance.data.customVariables.corprusCured == true then
        if tableHelper.containsValue(Players[pid].data.spellbook, corprusId) == true then
        
            tableHelper.removeValue(Players[pid].data.spellbook, corprusId)
            tableHelper.cleanNils(Players[pid].data.spellbook)

            tes3mp.ClearSpellbookChanges(pid)
            tes3mp.SetSpellbookChangesAction(pid, enumerations.spellbook.REMOVE)
            tes3mp.AddSpell(pid, corprusId)
            tes3mp.SendSpellbookChanges(pid)

            tes3mp.ClearSpellbookChanges(pid)
            tes3mp.SetSpellbookChangesAction(pid, enumerations.spellbook.ADD)
            for _, spellId in ipairs({"common disease immunity", "blight disease immunity","corprus immunity"}) do
                table.insert(Players[pid].data.spellbook, spellId)
                tes3mp.AddSpell(pid, spellId)
            end
            tes3mp.SendSpellbookChanges(pid)
            tes3mp.MessageBox(pid, -1, "You have been cured of corprus.")
        end
    elseif WorldInstance.data.customVariables.corprusGained == true then
        if tableHelper.containsValue(Players[pid].data.spellbook, corprusId) == false then

            table.insert(Players[pid].data.spellbook, corprusId)

            tes3mp.ClearSpellbookChanges(pid)
            tes3mp.SetSpellbookChangesAction(pid, enumerations.spellbook.ADD)
            tes3mp.AddSpell(pid, corprusId)
            tes3mp.SendSpellbookChanges(pid)
            tes3mp.MessageBox(pid, -1, "You have been afflicted with corprus.")
        end
    end
end

function contentFixer.AdjustWorldCorprusVariables(journal)

    local madeAdjustment = false

    for _, journalItem in ipairs(journal) do

        if journalItem.quest == "a2_3_corpruscure" and journalItem.index >= 50 then
            WorldInstance.data.customVariables.corprusCured = true
            madeAdjustment = true
        elseif journalItem.quest == "a2_2_6thhouse" and journalItem.index >= 50 then
            WorldInstance.data.customVariables.corprusGained = true
            madeAdjustment = true
        end
    end

    return madeAdjustment
end



-- ArenaMP Y043: MFR.esm compatibility ---------------------------------------
-- MFR ships a single-player startup selector. In multiplayer its one-shot
-- choice is not authoritative and can leave an old world in Start1/Start2,
-- hiding entire quest branches. Mirror Start3 on the server and route the
-- quest-exclusive interiors through the existing per-player instance layer.

local function appendCaseInsensitiveUnique(target, value)
    if type(target) ~= "table" or type(value) ~= "string" or value == "" then
        return false
    end
    for _, current in ipairs(target) do
        if type(current) == "string" and string.lower(current) == string.lower(value) then
            return false
        end
    end
    table.insert(target, value)
    return true
end

local function isMfrLoaded()
    local mfr = config.mfrCompatibility
    if type(mfr) ~= "table" or mfr.enabled == false then
        return false
    end
    local dataFile = mfr.dataFile or "MFR.esm"
    return type(clientDataFiles) == "table" and tableHelper.containsCaseInsensitiveString(clientDataFiles, dataFile)
end

local function ensureMfrPrivateInstances(mfr)
    if mfr.privateQuestInstances == false or type(mfr.privateQuestCells) ~= "table" then
        return 0
    end
    config.privateCellInstances = config.privateCellInstances or {}

    local knownCells = {}
    for _, definition in pairs(config.privateCellInstances) do
        if type(definition) == "table" and type(definition.baseCellDescription) == "string" then
            knownCells[string.lower(definition.baseCellDescription)] = true
        end
    end

    local added = 0
    for index, cellDescription in ipairs(mfr.privateQuestCells) do
        if type(cellDescription) == "string" and cellDescription ~= "" and not knownCells[string.lower(cellDescription)] then
            local key = string.format("mfrQuest%02d", index)
            while config.privateCellInstances[key] ~= nil do
                key = key .. "x"
            end
            config.privateCellInstances[key] = {
                enabled = true,
                baseCellDescription = cellDescription,
                instanceSuffix = " - Instance for ",
                neverReset = true,
                noticeEveryEntry = false
            }
            knownCells[string.lower(cellDescription)] = true
            added = added + 1
        end
    end
    return added
end

local function enforceMfrFullContentGlobals(mfr)
    if mfr.forceFullContent == false or type(mfr.fullContentGlobals) ~= "table" or WorldInstance == nil then
        return 0
    end

    WorldInstance.data.clientVariables = WorldInstance.data.clientVariables or {}
    WorldInstance.data.clientVariables.globals = WorldInstance.data.clientVariables.globals or {}

    local changed = 0
    for _, definition in ipairs(mfr.fullContentGlobals) do
        if type(definition) == "table" and type(definition.id) == "string" then
            local key = string.lower(definition.id)
            local variable
            if definition.type == "float" then
                variable = { variableType = enumerations.variableType.FLOAT, floatValue = tonumber(definition.value) or 0 }
            elseif definition.type == "long" then
                variable = { variableType = enumerations.variableType.LONG, intValue = math.floor(tonumber(definition.value) or 0) }
            else
                variable = { variableType = enumerations.variableType.SHORT, intValue = math.floor(tonumber(definition.value) or 0) }
            end

            local old = WorldInstance.data.clientVariables.globals[key]
            local differs = type(old) ~= "table" or old.variableType ~= variable.variableType
            if variable.floatValue ~= nil then
                differs = differs or old.floatValue ~= variable.floatValue
            else
                differs = differs or old.intValue ~= variable.intValue
            end
            if differs then
                WorldInstance.data.clientVariables.globals[key] = variable
                changed = changed + 1
            end
        end
    end

    if changed > 0 then
        WorldInstance:QuicksaveToDrive()
    end
    return changed
end

function contentFixer.ConfigureMfrCompatibility()
    if not isMfrLoaded() then
        return false
    end

    local mfr = config.mfrCompatibility
    local disabledAdded, synchronizedAdded, startupAdded = 0, 0, 0

    if type(mfr.disabledScripts) == "table" then
        for _, scriptId in ipairs(mfr.disabledScripts) do
            if (scriptId ~= "al_mistScript" or mfr.disableWaterMist ~= false) and
                (string.sub(string.lower(scriptId), 1, 9) ~= "al_option" or mfr.disableOptionMenu ~= false) then
                if appendCaseInsensitiveUnique(config.disabledClientScriptIds, scriptId) then disabledAdded = disabledAdded + 1 end
            end
        end
    end

    if type(mfr.synchronizedScripts) == "table" then
        for _, scriptId in ipairs(mfr.synchronizedScripts) do
            if appendCaseInsensitiveUnique(config.synchronizedClientScriptIds, scriptId) then synchronizedAdded = synchronizedAdded + 1 end
        end
    end

    if type(mfr.playerStartupScripts) == "table" then
        for _, scriptId in ipairs(mfr.playerStartupScripts) do
            if appendCaseInsensitiveUnique(config.playerStartupScripts, scriptId) then startupAdded = startupAdded + 1 end
        end
    end

    local instanceAdded = ensureMfrPrivateInstances(mfr)
    local globalsChanged = enforceMfrFullContentGlobals(mfr)

    tes3mp.LogMessage(enumerations.log.INFO, string.format(
        "[ArenaMP MFR] active: full-content globals=%d, private instances=%d, sync scripts=%d, disabled scripts=%d, startup scripts=%d",
        globalsChanged, instanceAdded, synchronizedAdded, disabledAdded, startupAdded))
    return true
end

customEventHooks.registerHandler("OnServerPostInit", function(eventStatus)
    contentFixer.ConfigureMfrCompatibility()
end)

customEventHooks.registerHandler("OnPlayerJournal", function(eventStatus, pid, playerPacket)
    if config.shareJournal == true then
        local madeAdjustment = contentFixer.AdjustWorldCorprusVariables(playerPacket.journal)

        if madeAdjustment == true then
            for otherPid, otherPlayer in pairs(Players) do
                if otherPid ~= pid then
                    contentFixer.AdjustSharedCorprusState(otherPid)
                end
            end
        end
    end
end)

customEventHooks.registerHandler("OnPlayerFinishLogin", function(eventStatus, pid)
    if config.shareJournal == true then
        contentFixer.AdjustSharedCorprusState(pid)
    end
end)

customEventHooks.registerHandler("OnWorldReload", function(eventStatus)
    if config.shareJournal == true then
        contentFixer.AdjustWorldCorprusVariables(WorldInstance.data.journal)
    end
end)

return contentFixer
