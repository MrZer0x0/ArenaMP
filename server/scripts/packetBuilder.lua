packetBuilder = {}
local questItemPhasing = require("questItemPhasing")

-- ArenaMP AOI routing for container state.  The stock TES3MP scripts use
-- SendContainer(true, ...), which broadcasts through the server.  Instead, use
-- the client's authoritative loaded-cell list: exterior neighbours naturally
-- participate because OpenMW reports those cells as loaded.  This is stricter
-- than guessing distance and also excludes nearby coordinates in another
-- interior/instance.
packetBuilder.GetRelevantPlayersForCell = function(cell)
    local relevantPids = {}

    if cell == nil then return relevantPids end

    for pid, player in pairs(Players) do
        if player ~= nil and player:IsLoggedIn() then
            local isVisitor = tableHelper.containsValue(cell.visitors, pid)
            local hasCellLoaded = player.cellsLoaded ~= nil and
                tableHelper.containsValue(player.cellsLoaded, cell.description)

            if isVisitor or hasCellLoaded then
                table.insert(relevantPids, pid)
            end
        end
    end

    return relevantPids
end

-- Re-route the received Container packet to the AOI only.
packetBuilder.SendReceivedContainerToRelevantPlayers = function(cell, skipPid)
    local sentCount = 0

    for _, targetPid in ipairs(packetBuilder.GetRelevantPlayersForCell(cell)) do
        if skipPid == nil or targetPid ~= skipPid then
            tes3mp.CopyReceivedObjectListToStore()
            tes3mp.SetObjectListPid(targetPid)
            tes3mp.SendContainer(false, false)
            sentCount = sentCount + 1
        end
    end

    return sentCount
end

-- Send one authoritative full SET of a stored container to the AOI.  REMOVE
-- packets are not forwarded to observers because TES3MP may treat the attached
-- player as the item recipient; observers only need the resulting container
-- state.  Poison state is preserved as part of the item identity.
packetBuilder.UpdateContainerForRelevantPlayers = function(cell, uniqueIndex, skipPid)
    if cell == nil or cell.data == nil or cell.data.objectData == nil then return 0 end

    local containerData = cell.data.objectData[uniqueIndex]
    if containerData == nil or containerData.inventory == nil then return 0 end

    local splitIndex = uniqueIndex:split("-")
    if splitIndex[1] == nil or splitIndex[2] == nil then return 0 end

    local sentCount = 0

    for _, targetPid in ipairs(packetBuilder.GetRelevantPlayersForCell(cell)) do
        if skipPid == nil or targetPid ~= skipPid then
            tes3mp.ClearObjectList()
            tes3mp.SetObjectListPid(targetPid)
            tes3mp.SetObjectListCell(cell.description)
            tes3mp.SetObjectListAction(enumerations.container.SET)
            tes3mp.SetObjectListContainerSubAction(enumerations.containerSub.NONE)

            tes3mp.SetObjectRefNum(splitIndex[1])
            tes3mp.SetObjectMpNum(splitIndex[2])
            if containerData.refId ~= nil then tes3mp.SetObjectRefId(containerData.refId) end

            for _, item in pairs(containerData.inventory) do
                local visibleCount = questItemPhasing.GetVisibleContainerCount(
                    targetPid, cell.description, uniqueIndex, item)

                if visibleCount > 0 then
                    local charge = item.charge or -1
                    local enchantmentCharge = item.enchantmentCharge or -1
                    local soul = item.soul or ""

                    if charge < -1 then charge = -1 end
                    if enchantmentCharge < -1 then enchantmentCharge = -1 end

                    tes3mp.SetContainerItemRefId(item.refId)
                    tes3mp.SetContainerItemCount(visibleCount)
                    tes3mp.SetContainerItemCharge(charge)
                    tes3mp.SetContainerItemEnchantmentCharge(enchantmentCharge)
                    tes3mp.SetContainerItemSoul(soul)
                    tes3mp.SetContainerItemPoison(item.poisonId or "", item.poisonCharges or 0)
                    tes3mp.AddContainerItem()
                end
            end

            tes3mp.AddObject()
            tes3mp.SendContainer(false, false)
            sentCount = sentCount + 1
        end
    end

    return sentCount
end

packetBuilder.AddPlayerInventoryItemChange = function(pid, item)

    -- Use default values when necessary
    if item.charge == nil or item.charge < -1 then item.charge = -1 end
    if item.enchantmentCharge == nil or item.enchantmentCharge < -1 then item.enchantmentCharge = -1 end
    if item.soul == nil then item.soul = "" end

    tes3mp.AddItemChange(pid, item.refId, item.count, item.charge, item.enchantmentCharge, item.soul)
    local poisonId = item.poisonId or ""
    local poisonCharges = item.poisonCharges or 0
    tes3mp.SetInventoryItemPoison(pid, tes3mp.GetInventoryChangesSize(pid) - 1, poisonId, poisonCharges)
end

packetBuilder.AddPlayerSpellsActive = function(pid, spellsActive, action)

    tes3mp.ClearSpellsActiveChanges(pid)
    tes3mp.SetSpellsActiveChangesAction(pid, action)

    for spellId, spellInstances in pairs(spellsActive) do
        for spellInstanceIndex, spellInstanceValues in pairs(spellInstances) do

            if action == enumerations.spellbook.SET or action == enumerations.spellbook.ADD then
                for effectIndex, effectTable in pairs(spellInstanceValues.effects) do

                    tes3mp.AddSpellActiveEffect(pid, effectTable.id, effectTable.magnitude,
                        effectTable.duration, effectTable.timeLeft, effectTable.arg)
                end
            end

            tes3mp.AddSpellActive(pid, spellId, spellInstanceValues.displayName,
                spellInstanceValues.stackingState)
        end
    end
end

packetBuilder.AddObjectDelete = function(uniqueIndex, objectData)

    local splitIndex = uniqueIndex:split("-")
    tes3mp.SetObjectRefNum(splitIndex[1])
    tes3mp.SetObjectMpNum(splitIndex[2])
    if objectData.refId ~= nil then tes3mp.SetObjectRefId(objectData.refId) end
    tes3mp.AddObject()
end

packetBuilder.AddObjectPlace = function(uniqueIndex, objectData, isReplay)

    local splitIndex = uniqueIndex:split("-")
    tes3mp.SetObjectRefNum(splitIndex[1])
    tes3mp.SetObjectMpNum(splitIndex[2])
    tes3mp.SetObjectRefId(objectData.refId)

    local count = objectData.count
    local charge = objectData.charge
    local enchantmentCharge = objectData.enchantmentCharge
    local soul = objectData.soul
    local poisonId = objectData.poisonId
    local poisonCharges = objectData.poisonCharges
    local goldValue = objectData.goldValue
    local droppedByPlayer = objectData.droppedByPlayer
    -- Y105: provenance remains in saved data, but a cell snapshot is not a new
    -- drop event: do not replay hundreds of drop sounds and OnPCDrop scripts.
    if isReplay == true then droppedByPlayer = false end

    -- Use default values when necessary
    if count == nil then count = 1 end
    if charge == nil then charge = -1 end
    if enchantmentCharge == nil then enchantmentCharge = -1 end
    if soul == nil then soul = "" end
    if poisonId == nil then poisonId = "" end
    if poisonCharges == nil then poisonCharges = 0 end
    if goldValue == nil then goldValue = 1 end
    if droppedByPlayer == nil then droppedByPlayer = false end

    tes3mp.SetObjectCount(count)
    tes3mp.SetObjectCharge(charge)
    tes3mp.SetObjectEnchantmentCharge(enchantmentCharge)
    tes3mp.SetObjectSoul(soul)
    tes3mp.SetObjectPoison(poisonId, poisonCharges)
    tes3mp.SetObjectGoldValue(goldValue)
    tes3mp.SetObjectDroppedByPlayerState(droppedByPlayer)

    local location = objectData.location
    tes3mp.SetObjectPosition(location.posX, location.posY, location.posZ)
    tes3mp.SetObjectRotation(location.rotX, location.rotY, location.rotZ)

    tes3mp.AddObject()
end

packetBuilder.AddObjectSpawn = function(uniqueIndex, objectData)

    local splitIndex = uniqueIndex:split("-")
    tes3mp.SetObjectRefNum(splitIndex[1])
    tes3mp.SetObjectMpNum(splitIndex[2])
    tes3mp.SetObjectRefId(objectData.refId)

    if objectData.summon ~= nil then
        tes3mp.SetObjectSummonState(true)
        tes3mp.SetObjectSummonEffectId(objectData.summon.effectId)
        tes3mp.SetObjectSummonSpellId(objectData.summon.spellId)

        local currentTime = os.time()
        local finishTime = objectData.summon.startTime + objectData.summon.duration
        tes3mp.SetObjectSummonDuration(finishTime - currentTime)

        if objectData.summon.summoner.playerName then
            local player = logicHandler.GetPlayerByName(objectData.summon.summoner.playerName)
            tes3mp.SetObjectSummonerPid(player.pid)
        else
            local summonerSplitIndex = objectData.summon.summoner.uniqueIndex:split("-")
            tes3mp.SetObjectSummonerRefNum(summonerSplitIndex[1])
            tes3mp.SetObjectSummonerMpNum(summonerSplitIndex[2])
        end
    end

    local location = objectData.location
    tes3mp.SetObjectPosition(location.posX, location.posY, location.posZ)
    tes3mp.SetObjectRotation(location.rotX, location.rotY, location.rotZ)

    tes3mp.AddObject()
end

packetBuilder.AddObjectLock = function(uniqueIndex, objectData)

    local splitIndex = uniqueIndex:split("-")
    tes3mp.SetObjectRefNum(splitIndex[1])
    tes3mp.SetObjectMpNum(splitIndex[2])
    if objectData.refId ~= nil then tes3mp.SetObjectRefId(objectData.refId) end
    tes3mp.SetObjectLockLevel(objectData.lockLevel)
    tes3mp.AddObject()
end

packetBuilder.AddObjectMiscellaneous = function(uniqueIndex, objectData)

    local splitIndex = uniqueIndex:split("-")
    tes3mp.SetObjectRefNum(splitIndex[1])
    tes3mp.SetObjectMpNum(splitIndex[2])
    if objectData.refId ~= nil then tes3mp.SetObjectRefId(objectData.refId) end
    tes3mp.SetObjectGoldPool(objectData.goldPool)
    tes3mp.SetObjectLastGoldRestockHour(objectData.lastGoldRestockHour)
    tes3mp.SetObjectLastGoldRestockDay(objectData.lastGoldRestockDay)
    tes3mp.AddObject()
end

packetBuilder.AddObjectTrap = function(uniqueIndex, objectData)

    local splitIndex = uniqueIndex:split("-")
    tes3mp.SetObjectRefNum(splitIndex[1])
    tes3mp.SetObjectMpNum(splitIndex[2])
    if objectData.refId ~= nil then tes3mp.SetObjectRefId(objectData.refId) end
    tes3mp.SetObjectDisarmState(true)
    tes3mp.AddObject()
end

packetBuilder.AddObjectMove = function(uniqueIndex, objectData)

    local splitIndex = uniqueIndex:split("-")
    tes3mp.SetObjectRefNum(splitIndex[1])
    tes3mp.SetObjectMpNum(splitIndex[2])
    if objectData.refId ~= nil then tes3mp.SetObjectRefId(objectData.refId) end
    local location = objectData.location or {}
    tes3mp.SetObjectPosition(location.posX or 0, location.posY or 0, location.posZ or 0)
    tes3mp.AddObject()
end

packetBuilder.AddObjectRotate = function(uniqueIndex, objectData)

    local splitIndex = uniqueIndex:split("-")
    tes3mp.SetObjectRefNum(splitIndex[1])
    tes3mp.SetObjectMpNum(splitIndex[2])
    if objectData.refId ~= nil then tes3mp.SetObjectRefId(objectData.refId) end
    local location = objectData.location or {}
    tes3mp.SetObjectRotation(location.rotX or 0, location.rotY or 0, location.rotZ or 0)
    tes3mp.AddObject()
end

packetBuilder.AddObjectScale = function(uniqueIndex, objectData)

    local splitIndex = uniqueIndex:split("-")
    tes3mp.SetObjectRefNum(splitIndex[1])
    tes3mp.SetObjectMpNum(splitIndex[2])
    if objectData.refId ~= nil then tes3mp.SetObjectRefId(objectData.refId) end
    tes3mp.SetObjectScale(objectData.scale)
    tes3mp.AddObject()
end

packetBuilder.AddObjectState = function(uniqueIndex, objectData)

    local splitIndex = uniqueIndex:split("-")
    tes3mp.SetObjectRefNum(splitIndex[1])
    tes3mp.SetObjectMpNum(splitIndex[2])
    if objectData.refId ~= nil then tes3mp.SetObjectRefId(objectData.refId) end
    tes3mp.SetObjectState(objectData.state)
    tes3mp.AddObject()
end

packetBuilder.AddDoorState = function(uniqueIndex, objectData)

    local splitIndex = uniqueIndex:split("-")
    tes3mp.SetObjectRefNum(splitIndex[1])
    tes3mp.SetObjectMpNum(splitIndex[2])
    if objectData.refId ~= nil then tes3mp.SetObjectRefId(objectData.refId) end
    tes3mp.SetObjectDoorState(objectData.doorState)
    tes3mp.AddObject()
end

-- Start of AMP change
--
-- Rewritten to tolerate incomplete data instead of erroring out halfway through building a
-- packet, and to carry the ID of the script the variables belong to.
--
-- The old version indexed objectData.variables unconditionally, so a single object entry
-- that had lost its variables table raised a Lua error inside cell loading and left the
-- player with a partially loaded cell.
packetBuilder.AddClientScriptLocal = function(uniqueIndex, objectData)

    if objectData == nil or type(objectData.variables) ~= "table" then return end

    local splitIndex = uniqueIndex:split("-")

    if splitIndex[1] == nil or splitIndex[2] == nil then return end

    tes3mp.SetObjectRefNum(splitIndex[1])
    tes3mp.SetObjectMpNum(splitIndex[2])
    if objectData.refId ~= nil then tes3mp.SetObjectRefId(objectData.refId) end
    if objectData.clientScriptId ~= nil then tes3mp.SetObjectClientScriptId(objectData.clientScriptId) end

    local variableCount = 0

    for variableType, variableTable in pairs(objectData.variables) do

        if type(variableTable) == "table" then

            for internalIndex, value in pairs(variableTable) do

                local numericIndex = tonumber(internalIndex)

                if numericIndex ~= nil and type(value) == "number" then

                    if variableType == enumerations.variableType.SHORT then
                        tes3mp.AddClientLocalInteger(numericIndex, value, enumerations.variableType.SHORT)
                        variableCount = variableCount + 1
                    elseif variableType == enumerations.variableType.LONG then
                        tes3mp.AddClientLocalInteger(numericIndex, value, enumerations.variableType.LONG)
                        variableCount = variableCount + 1
                    elseif variableType == enumerations.variableType.FLOAT then
                        tes3mp.AddClientLocalFloat(numericIndex, value)
                        variableCount = variableCount + 1
                    end
                end
            end
        end
    end

    if variableCount > 0 then
        tes3mp.AddObject()
    end
end

-- Build the same kind of entry, but attached to a player instead of to a cell reference,
-- so that scripts running on a character travel with that character's profile
packetBuilder.AddPlayerClientScriptLocal = function(pid, scriptId, variables)

    if type(variables) ~= "table" then return false end

    tes3mp.SetPlayerAsObject(pid)
    tes3mp.SetObjectClientScriptId(scriptId)

    local variableCount = 0

    for variableType, variableTable in pairs(variables) do

        if type(variableTable) == "table" then

            for internalIndex, value in pairs(variableTable) do

                local numericIndex = tonumber(internalIndex)

                if numericIndex ~= nil and type(value) == "number" then

                    if variableType == enumerations.variableType.SHORT then
                        tes3mp.AddClientLocalInteger(numericIndex, value, enumerations.variableType.SHORT)
                        variableCount = variableCount + 1
                    elseif variableType == enumerations.variableType.LONG then
                        tes3mp.AddClientLocalInteger(numericIndex, value, enumerations.variableType.LONG)
                        variableCount = variableCount + 1
                    elseif variableType == enumerations.variableType.FLOAT then
                        tes3mp.AddClientLocalFloat(numericIndex, value)
                        variableCount = variableCount + 1
                    end
                end
            end
        end
    end

    if variableCount > 0 then
        tes3mp.AddObject()
        return true
    end

    return false
-- End of AMP change
end

packetBuilder.AddAIActor = function(actorUniqueIndex, targetPid, aiData)

    local splitIndex = actorUniqueIndex:split("-")
    tes3mp.SetActorRefNum(splitIndex[1])
    tes3mp.SetActorMpNum(splitIndex[2])

    tes3mp.SetActorAIAction(aiData.action)

    if targetPid ~= nil then
        tes3mp.SetActorAITargetToPlayer(targetPid)
    elseif aiData.targetUniqueIndex ~= nil then
        local targetSplitIndex = aiData.targetUniqueIndex:split("-")

        if targetSplitIndex[2] ~= nil then
            tes3mp.SetActorAITargetToObject(targetSplitIndex[1], targetSplitIndex[2])
        end
    end

    -- Y029: these are independent fields in ActorAI.  The old elseif chain
    -- dropped ESCORT duration/coordinates whenever the package also had a target.
    if aiData.posX ~= nil and aiData.posY ~= nil and aiData.posZ ~= nil then
        tes3mp.SetActorAICoordinates(aiData.posX, aiData.posY, aiData.posZ)
    end
    if aiData.distance ~= nil then
        tes3mp.SetActorAIDistance(aiData.distance)
    end
    if aiData.duration ~= nil then
        tes3mp.SetActorAIDuration(aiData.duration)
    end

    tes3mp.SetActorAIRepetition(aiData.shouldRepeat == true)

    -- X034: persistent server-side home/door breadcrumbs are attached to AI
    -- orders so a new authority can resume an exact return route after restart.
    if aiData.home ~= nil and aiData.home.cell ~= nil then
        tes3mp.SetActorAIReturnHome(aiData.home.cell, aiData.home.posX or 0, aiData.home.posY or 0,
            aiData.home.posZ or 0, aiData.home.rotZ or 0)
        if aiData.home.route ~= nil then
            for _, hop in ipairs(aiData.home.route) do
                if hop.fromCell ~= nil and hop.toCell ~= nil then
                    tes3mp.AddActorAIDoorBreadcrumb(hop.fromCell, hop.fromX or 0, hop.fromY or 0, hop.fromZ or 0,
                        hop.toCell, hop.toX or 0, hop.toY or 0, hop.toZ or 0)
                end
            end
        end
    end

    tes3mp.AddActor()
end

packetBuilder.AddActorSpellsActive = function(actorUniqueIndex, spellsActive, action)

    local splitIndex = actorUniqueIndex:split("-")
    tes3mp.SetActorRefNum(splitIndex[1])
    tes3mp.SetActorMpNum(splitIndex[2])
    tes3mp.SetActorSpellsActiveAction(action)

    for spellId, spellInstances in pairs(spellsActive) do
        for spellInstanceIndex, spellInstanceValues in pairs(spellInstances) do

            if action == enumerations.spellbook.SET or action == enumerations.spellbook.ADD then
                for effectIndex, effectTable in pairs(spellInstanceValues.effects) do

                    tes3mp.AddActorSpellActiveEffect(effectTable.id, effectTable.magnitude,
                        effectTable.duration, effectTable.timeLeft, effectTable.arg)
                end
            end

            tes3mp.AddActorSpellActive(spellId, spellInstanceValues.displayName,
                spellInstanceValues.stackingState)
        end
    end

    tes3mp.AddActor() 
end

packetBuilder.AddEffectToRecord = function(effect)

    tes3mp.SetRecordEffectId(effect.id)
    if effect.attribute ~= nil then tes3mp.SetRecordEffectAttribute(effect.attribute) end
    if effect.skill ~= nil then tes3mp.SetRecordEffectSkill(effect.skill) end
    if effect.rangeType ~= nil then tes3mp.SetRecordEffectRangeType(effect.rangeType) end
    if effect.area ~= nil then tes3mp.SetRecordEffectArea(effect.area) end
    if effect.duration ~= nil then tes3mp.SetRecordEffectDuration(effect.duration) end
    if effect.magnitudeMin ~= nil then tes3mp.SetRecordEffectMagnitudeMin(effect.magnitudeMin) end
    if effect.magnitudeMax ~= nil then tes3mp.SetRecordEffectMagnitudeMax(effect.magnitudeMax) end

    tes3mp.AddRecordEffect()
end

packetBuilder.AddBodyPartToRecord = function(part)

    tes3mp.SetRecordBodyPartType(part.partType)
    if part.malePart ~= nil then tes3mp.SetRecordBodyPartIdForMale(part.malePart) end
    if part.femalePart ~= nil then tes3mp.SetRecordBodyPartIdForFemale(part.femalePart) end

    tes3mp.AddRecordBodyPart()
end

packetBuilder.AddInventoryItemToRecord = function(item)

    tes3mp.SetRecordInventoryItemId(item.id)
    if item.count ~= nil then tes3mp.SetRecordInventoryItemCount(item.count) end

    tes3mp.AddRecordInventoryItem()
end

packetBuilder.AddRecordByType = function(id, record, storeType)

    if storeType == "activator" then
        packetBuilder.AddActivatorRecord(id, record)
    elseif storeType == "apparatus" then
        packetBuilder.AddApparatusRecord(id, record)
    elseif storeType == "armor" then
        packetBuilder.AddArmorRecord(id, record)
    elseif storeType == "bodypart" then
        packetBuilder.AddBodyPartRecord(id, record)
    elseif storeType == "book" then
        packetBuilder.AddBookRecord(id, record)
    elseif storeType == "cell" then
        packetBuilder.AddCellRecord(id, record)
    elseif storeType == "clothing" then
        packetBuilder.AddClothingRecord(id, record)
    elseif storeType == "container" then
        packetBuilder.AddContainerRecord(id, record)
    elseif storeType == "creature" then
        packetBuilder.AddCreatureRecord(id, record)
    elseif storeType == "door" then
        packetBuilder.AddDoorRecord(id, record)
    elseif storeType == "enchantment" then
        packetBuilder.AddEnchantmentRecord(id, record)
    elseif storeType == "gamesetting" then
        packetBuilder.AddGameSettingRecord(id, record)
    elseif storeType == "ingredient" then
        packetBuilder.AddIngredientRecord(id, record)
    elseif storeType == "light" then
        packetBuilder.AddLightRecord(id, record)
    elseif storeType == "lockpick" then
        packetBuilder.AddLockpickRecord(id, record)
    elseif storeType == "miscellaneous" then
        packetBuilder.AddMiscellaneousRecord(id, record)
    elseif storeType == "npc" then
        packetBuilder.AddNpcRecord(id, record)
    elseif storeType == "potion" then
        packetBuilder.AddPotionRecord(id, record)
    elseif storeType == "probe" then
        packetBuilder.AddProbeRecord(id, record)
    elseif storeType == "repair" then
        packetBuilder.AddRepairRecord(id, record)
    elseif storeType == "script" then
        packetBuilder.AddScriptRecord(id, record)
    elseif storeType == "sound" then
        packetBuilder.AddSoundRecord(id, record)
    elseif storeType == "spell" then
        packetBuilder.AddSpellRecord(id, record)
    elseif storeType == "static" then
        packetBuilder.AddStaticRecord(id, record)
    elseif storeType == "weapon" then
        packetBuilder.AddWeaponRecord(id, record)
    end
end

packetBuilder.AddActivatorRecord = function(id, record)

    tes3mp.SetRecordId(id)
    if record.baseId ~= nil then tes3mp.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then tes3mp.SetRecordName(record.name) end
    if record.model ~= nil then tes3mp.SetRecordModel(record.model) end
    if record.script ~= nil then tes3mp.SetRecordScript(record.script) end

    tes3mp.AddRecord()
end

packetBuilder.AddApparatusRecord = function(id, record)

    tes3mp.SetRecordId(id)
    if record.baseId ~= nil then tes3mp.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then tes3mp.SetRecordName(record.name) end
    if record.model ~= nil then tes3mp.SetRecordModel(record.model) end
    if record.icon ~= nil then tes3mp.SetRecordIcon(record.icon) end
    if record.subtype ~= nil then tes3mp.SetRecordSubtype(record.subtype) end
    if record.weight ~= nil then tes3mp.SetRecordWeight(record.weight) end
    if record.value ~= nil then tes3mp.SetRecordValue(record.value) end
    if record.quality ~= nil then tes3mp.SetRecordQuality(record.quality) end
    if record.script ~= nil then tes3mp.SetRecordScript(record.script) end

    tes3mp.AddRecord()
end

packetBuilder.AddArmorRecord = function(id, record)

    tes3mp.SetRecordId(id)
    if record.baseId ~= nil then tes3mp.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then tes3mp.SetRecordName(record.name) end
    if record.model ~= nil then tes3mp.SetRecordModel(record.model) end
    if record.icon ~= nil then tes3mp.SetRecordIcon(record.icon) end
    if record.subtype ~= nil then tes3mp.SetRecordSubtype(record.subtype) end
    if record.weight ~= nil then tes3mp.SetRecordWeight(record.weight) end
    if record.value ~= nil then tes3mp.SetRecordValue(record.value) end
    if record.health ~= nil then tes3mp.SetRecordHealth(record.health) end
    if record.armorRating ~= nil then tes3mp.SetRecordArmorRating(record.armorRating) end
    if record.enchantmentId ~= nil then tes3mp.SetRecordEnchantmentId(record.enchantmentId) end
    if record.enchantmentCharge ~= nil then tes3mp.SetRecordEnchantmentCharge(record.enchantmentCharge) end
    if record.script ~= nil then tes3mp.SetRecordScript(record.script) end

    if type(record.parts) == "table" then
        for _, part in pairs(record.parts) do
            packetBuilder.AddBodyPartToRecord(part)
        end
    end

    tes3mp.AddRecord()
end

packetBuilder.AddBodyPartRecord = function(id, record)
    
    tes3mp.SetRecordId(id)
    if record.baseId ~= nil then tes3mp.SetRecordBaseId(record.baseId) end
    if record.subtype ~= nil then tes3mp.SetRecordSubtype(record.subtype) end
    if record.part ~= nil then tes3mp.SetRecordBodyPartType(record.part) end
    if record.model ~= nil then tes3mp.SetRecordModel(record.model) end
    if record.race ~= nil then tes3mp.SetRecordRace(record.race) end
    if record.vampireState ~= nil then tes3mp.SetRecordVampireState(record.vampireState) end
    if record.flags ~= nil then tes3mp.SetRecordFlags(record.flags) end

    tes3mp.AddRecord()
end

packetBuilder.AddBookRecord = function(id, record)

    tes3mp.SetRecordId(id)
    if record.baseId ~= nil then tes3mp.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then tes3mp.SetRecordName(record.name) end
    if record.model ~= nil then tes3mp.SetRecordModel(record.model) end
    if record.icon ~= nil then tes3mp.SetRecordIcon(record.icon) end
    if record.text ~= nil then tes3mp.SetRecordText(record.text) end
    if record.weight ~= nil then tes3mp.SetRecordWeight(record.weight) end
    if record.value ~= nil then tes3mp.SetRecordValue(record.value) end
    if record.scrollState ~= nil then tes3mp.SetRecordScrollState(record.scrollState) end
    if record.skillId ~= nil then tes3mp.SetRecordSkillId(record.skillId) end
    if record.enchantmentId ~= nil then tes3mp.SetRecordEnchantmentId(record.enchantmentId) end
    if record.enchantmentCharge ~= nil then tes3mp.SetRecordEnchantmentCharge(record.enchantmentCharge) end
    if record.script ~= nil then tes3mp.SetRecordScript(record.script) end

    tes3mp.AddRecord()
end

packetBuilder.AddCellRecord = function(id, record)

    tes3mp.SetRecordName(id)
    if record.baseId ~= nil then tes3mp.SetRecordBaseId(record.baseId) end

    tes3mp.AddRecord()
end

packetBuilder.AddClothingRecord = function(id, record)

    tes3mp.SetRecordId(id)
    if record.baseId ~= nil then tes3mp.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then tes3mp.SetRecordName(record.name) end
    if record.model ~= nil then tes3mp.SetRecordModel(record.model) end
    if record.icon ~= nil then tes3mp.SetRecordIcon(record.icon) end
    if record.subtype ~= nil then tes3mp.SetRecordSubtype(record.subtype) end
    if record.weight ~= nil then tes3mp.SetRecordWeight(record.weight) end
    if record.value ~= nil then tes3mp.SetRecordValue(record.value) end
    if record.enchantmentId ~= nil then tes3mp.SetRecordEnchantmentId(record.enchantmentId) end
    if record.enchantmentCharge ~= nil then tes3mp.SetRecordEnchantmentCharge(record.enchantmentCharge) end
    if record.script ~= nil then tes3mp.SetRecordScript(record.script) end

    if type(record.parts) == "table" then
        for _, part in pairs(record.parts) do
            packetBuilder.AddBodyPartToRecord(part)
        end
    end

    tes3mp.AddRecord()
end

packetBuilder.AddContainerRecord = function(id, record)

    tes3mp.SetRecordId(id)
    if record.baseId ~= nil then tes3mp.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then tes3mp.SetRecordName(record.name) end
    if record.model ~= nil then tes3mp.SetRecordModel(record.model) end
    if record.weight ~= nil then tes3mp.SetRecordWeight(record.weight) end
    if record.flags ~= nil then tes3mp.SetRecordFlags(record.flags) end
    if record.script ~= nil then tes3mp.SetRecordScript(record.script) end

    if type(record.items) == "table" then
        for _, item in pairs(record.items) do
            packetBuilder.AddInventoryItemToRecord(item)
        end
    end

    tes3mp.AddRecord()
end

packetBuilder.AddCreatureRecord = function(id, record)

    tes3mp.SetRecordId(id)
    if record.baseId ~= nil then tes3mp.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then tes3mp.SetRecordName(record.name) end
    if record.model ~= nil then tes3mp.SetRecordModel(record.model) end
    if record.subtype ~= nil then tes3mp.SetRecordSubtype(record.subtype) end
    if record.scale ~= nil then tes3mp.SetRecordScale(record.scale) end
    if record.bloodType ~= nil then tes3mp.SetRecordBloodType(record.bloodType) end
    if record.level ~= nil then tes3mp.SetRecordLevel(record.level) end
    if record.health ~= nil then tes3mp.SetRecordHealth(record.health) end
    if record.magicka ~= nil then tes3mp.SetRecordMagicka(record.magicka) end
    if record.fatigue ~= nil then tes3mp.SetRecordFatigue(record.fatigue) end
    if record.soulValue ~= nil then tes3mp.SetRecordSoulValue(record.soulValue) end
    if record.damageChop ~= nil then tes3mp.SetRecordDamageChop(record.damageChop.min, record.damageChop.max) end
    if record.damageSlash ~= nil then tes3mp.SetRecordDamageSlash(record.damageSlash.min, record.damageSlash.max) end
    if record.damageThrust ~= nil then tes3mp.SetRecordDamageThrust(record.damageThrust.min, record.damageThrust.max) end
    if record.aiFight ~= nil then tes3mp.SetRecordAIFight(record.aiFight) end
    if record.aiServices ~= nil then tes3mp.SetRecordAIServices(record.aiServices) end
    if record.aiFlee ~= nil then tes3mp.SetRecordAIFlee(record.aiFlee) end
    if record.aiAlarm ~= nil then tes3mp.SetRecordAIAlarm(record.aiAlarm) end
    if record.flags ~= nil then tes3mp.SetRecordFlags(record.flags) end
    if record.script ~= nil then tes3mp.SetRecordScript(record.script) end

    if type(record.items) == "table" then
        for _, item in pairs(record.items) do
            packetBuilder.AddInventoryItemToRecord(item)
        end
    end

    tes3mp.AddRecord()
end

packetBuilder.AddDoorRecord = function(id, record)

    tes3mp.SetRecordId(id)
    if record.baseId ~= nil then tes3mp.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then tes3mp.SetRecordName(record.name) end
    if record.model ~= nil then tes3mp.SetRecordModel(record.model) end
    if record.openSound ~= nil then tes3mp.SetRecordOpenSound(record.openSound) end
    if record.closeSound ~= nil then tes3mp.SetRecordCloseSound(record.closeSound) end
    if record.script ~= nil then tes3mp.SetRecordScript(record.script) end

    tes3mp.AddRecord()
end

packetBuilder.AddEnchantmentRecord = function(id, record)

    tes3mp.SetRecordId(id)
    if record.baseId ~= nil then tes3mp.SetRecordBaseId(record.baseId) end
    if record.subtype ~= nil then tes3mp.SetRecordSubtype(record.subtype) end
    if record.cost ~= nil then tes3mp.SetRecordCost(record.cost) end
    if record.charge ~= nil then tes3mp.SetRecordCharge(record.charge) end

    if record.flags ~= nil then tes3mp.SetRecordFlags(record.flags)
    -- Keep this for compatibility with older data which used autoCalc
    elseif record.autoCalc ~= nil then tes3mp.SetRecordFlags(record.autoCalc) end

    if type(record.effects) == "table" then
        for _, effect in pairs(record.effects) do
            packetBuilder.AddEffectToRecord(effect)
        end
    end

    tes3mp.AddRecord()
end

packetBuilder.AddGameSettingRecord = function(id, record)

    tes3mp.SetRecordId(id)
    if record.baseId ~= nil then tes3mp.SetRecordBaseId(record.baseId) end

    if record.intVar ~= nil then tes3mp.SetRecordIntegerVariable(record.intVar)
    elseif record.floatVar ~= nil then tes3mp.SetRecordFloatVariable(record.floatVar)
    elseif record.stringVar ~= nil then tes3mp.SetRecordStringVariable(tostring(record.stringVar)) end

    tes3mp.AddRecord()
end

packetBuilder.AddIngredientRecord = function(id, record)

    tes3mp.SetRecordId(id)
    if record.baseId ~= nil then tes3mp.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then tes3mp.SetRecordName(record.name) end
    if record.model ~= nil then tes3mp.SetRecordModel(record.model) end
    if record.icon ~= nil then tes3mp.SetRecordIcon(record.icon) end
    if record.weight ~= nil then tes3mp.SetRecordWeight(record.weight) end
    if record.value ~= nil then tes3mp.SetRecordValue(record.value) end
    if record.script ~= nil then tes3mp.SetRecordScript(record.script) end

    if type(record.effects) == "table" then
        for effectIndex = 1, 4 do
            local effect = record.effects[effectIndex]

            if effect == nil then
                effect = { id = -1 }
            end
            
            packetBuilder.AddEffectToRecord(effect)
        end
    end

    tes3mp.AddRecord()
end

packetBuilder.AddLightRecord = function(id, record)

    tes3mp.SetRecordId(id)
    if record.baseId ~= nil then tes3mp.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then tes3mp.SetRecordName(record.name) end
    if record.model ~= nil then tes3mp.SetRecordModel(record.model) end
    if record.icon ~= nil then tes3mp.SetRecordIcon(record.icon) end
    if record.sound ~= nil then tes3mp.SetRecordSound(record.sound) end
    if record.weight ~= nil then tes3mp.SetRecordWeight(record.weight) end
    if record.value ~= nil then tes3mp.SetRecordValue(record.value) end
    if record.time ~= nil then tes3mp.SetRecordTime(record.time) end
    if record.radius ~= nil then tes3mp.SetRecordRadius(record.radius) end
    if record.color ~= nil then tes3mp.SetRecordColor(record.color.red, record.color.green, record.color.blue) end
    if record.flags ~= nil then tes3mp.SetRecordFlags(record.flags) end
    if record.script ~= nil then tes3mp.SetRecordScript(record.script) end

    tes3mp.AddRecord()
end

packetBuilder.AddLockpickRecord = function(id, record)

    tes3mp.SetRecordId(id)
    if record.baseId ~= nil then tes3mp.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then tes3mp.SetRecordName(record.name) end
    if record.model ~= nil then tes3mp.SetRecordModel(record.model) end
    if record.icon ~= nil then tes3mp.SetRecordIcon(record.icon) end
    if record.weight ~= nil then tes3mp.SetRecordWeight(record.weight) end
    if record.value ~= nil then tes3mp.SetRecordValue(record.value) end
    if record.quality ~= nil then tes3mp.SetRecordQuality(record.quality) end
    if record.uses ~= nil then tes3mp.SetRecordUses(record.uses) end
    if record.script ~= nil then tes3mp.SetRecordScript(record.script) end

    tes3mp.AddRecord()
end

packetBuilder.AddMiscellaneousRecord = function(id, record)

    tes3mp.SetRecordId(id)
    if record.baseId ~= nil then tes3mp.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then tes3mp.SetRecordName(record.name) end
    if record.model ~= nil then tes3mp.SetRecordModel(record.model) end
    if record.icon ~= nil then tes3mp.SetRecordIcon(record.icon) end
    if record.weight ~= nil then tes3mp.SetRecordWeight(record.weight) end
    if record.value ~= nil then tes3mp.SetRecordValue(record.value) end
    if record.keyState ~= nil then tes3mp.SetRecordKeyState(record.keyState) end
    if record.script ~= nil then tes3mp.SetRecordScript(record.script) end

    tes3mp.AddRecord()
end

packetBuilder.AddNpcRecord = function(id, record)

    tes3mp.SetRecordId(id)
    if record.baseId ~= nil then tes3mp.SetRecordBaseId(record.baseId) end
    if record.inventoryBaseId ~= nil then tes3mp.SetRecordInventoryBaseId(record.inventoryBaseId) end
    if record.name ~= nil then tes3mp.SetRecordName(record.name) end
    if record.gender ~= nil then tes3mp.SetRecordGender(record.gender) end
    if record.race ~= nil then tes3mp.SetRecordRace(record.race) end
    if record.hair ~= nil then tes3mp.SetRecordHair(record.hair) end
    if record.head ~= nil then tes3mp.SetRecordHead(record.head) end
    if record.class ~= nil then tes3mp.SetRecordClass(record.class) end
    if record.level ~= nil then tes3mp.SetRecordLevel(record.level) end
    if record.health ~= nil then tes3mp.SetRecordHealth(record.health) end
    if record.magicka ~= nil then tes3mp.SetRecordMagicka(record.magicka) end
    if record.fatigue ~= nil then tes3mp.SetRecordFatigue(record.fatigue) end
    if record.aiFight ~= nil then tes3mp.SetRecordAIFight(record.aiFight) end
    if record.aiFlee ~= nil then tes3mp.SetRecordAIFlee(record.aiFlee) end
    if record.aiAlarm ~= nil then tes3mp.SetRecordAIAlarm(record.aiAlarm) end
    if record.aiServices ~= nil then tes3mp.SetRecordAIServices(record.aiServices) end
    if record.autoCalc ~= nil then tes3mp.SetRecordAutoCalc(record.autoCalc) end
    if record.faction ~= nil then tes3mp.SetRecordFaction(record.faction) end
    if record.script ~= nil then tes3mp.SetRecordScript(record.script) end

    if type(record.items) == "table" then
        for _, item in pairs(record.items) do
            packetBuilder.AddInventoryItemToRecord(item)
        end
    end

    tes3mp.AddRecord()
end

packetBuilder.AddPotionRecord = function(id, record)

    tes3mp.SetRecordId(id)
    if record.baseId ~= nil then tes3mp.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then tes3mp.SetRecordName(record.name) end
    if record.weight ~= nil then tes3mp.SetRecordWeight(record.weight) end
    if record.value ~= nil then tes3mp.SetRecordValue(record.value) end
    if record.autoCalc ~= nil then tes3mp.SetRecordAutoCalc(record.autoCalc) end
    if record.icon ~= nil then tes3mp.SetRecordIcon(record.icon) end
    if record.model ~= nil then tes3mp.SetRecordModel(record.model) end
    if record.script ~= nil then tes3mp.SetRecordScript(record.script) end

    if type(record.effects) == "table" then
        for _, effect in pairs(record.effects) do
            packetBuilder.AddEffectToRecord(effect)
        end
    end

    tes3mp.AddRecord()
end

packetBuilder.AddProbeRecord = function(id, record)

    tes3mp.SetRecordId(id)
    if record.baseId ~= nil then tes3mp.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then tes3mp.SetRecordName(record.name) end
    if record.model ~= nil then tes3mp.SetRecordModel(record.model) end
    if record.icon ~= nil then tes3mp.SetRecordIcon(record.icon) end
    if record.weight ~= nil then tes3mp.SetRecordWeight(record.weight) end
    if record.value ~= nil then tes3mp.SetRecordValue(record.value) end
    if record.quality ~= nil then tes3mp.SetRecordQuality(record.quality) end
    if record.uses ~= nil then tes3mp.SetRecordUses(record.uses) end
    if record.script ~= nil then tes3mp.SetRecordScript(record.script) end

    tes3mp.AddRecord()
end

packetBuilder.AddRepairRecord = function(id, record)

    tes3mp.SetRecordId(id)
    if record.baseId ~= nil then tes3mp.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then tes3mp.SetRecordName(record.name) end
    if record.model ~= nil then tes3mp.SetRecordModel(record.model) end
    if record.icon ~= nil then tes3mp.SetRecordIcon(record.icon) end
    if record.weight ~= nil then tes3mp.SetRecordWeight(record.weight) end
    if record.value ~= nil then tes3mp.SetRecordValue(record.value) end
    if record.quality ~= nil then tes3mp.SetRecordQuality(record.quality) end
    if record.uses ~= nil then tes3mp.SetRecordUses(record.uses) end
    if record.script ~= nil then tes3mp.SetRecordScript(record.script) end

    tes3mp.AddRecord()
end

packetBuilder.AddScriptRecord = function(id, record)

    tes3mp.SetRecordId(id)
    if record.baseId ~= nil then tes3mp.SetRecordBaseId(record.baseId) end
    if record.scriptText ~= nil then tes3mp.SetRecordScriptText(record.scriptText) end

    tes3mp.AddRecord()
end

packetBuilder.AddSoundRecord = function(id, record)

    tes3mp.SetRecordId(id)
    if record.baseId ~= nil then tes3mp.SetRecordBaseId(record.baseId) end
    if record.sound ~= nil then tes3mp.SetRecordSound(record.sound) end
    if record.volume ~= nil then tes3mp.SetRecordVolume(record.volume) end
    if record.minRange ~= nil then tes3mp.SetRecordMinRange(record.minRange) end
    if record.maxRange ~= nil then tes3mp.SetRecordMaxRange(record.maxRange) end

    tes3mp.AddRecord()
end

packetBuilder.AddSpellRecord = function(id, record)

    tes3mp.SetRecordId(id)
    if record.baseId ~= nil then tes3mp.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then tes3mp.SetRecordName(record.name) end
    if record.subtype ~= nil then tes3mp.SetRecordSubtype(record.subtype) end
    if record.cost ~= nil then tes3mp.SetRecordCost(record.cost) end
    if record.flags ~= nil then tes3mp.SetRecordFlags(record.flags) end

    if type(record.effects) == "table" then
        for _, effect in pairs(record.effects) do
            packetBuilder.AddEffectToRecord(effect)
        end
    end

    tes3mp.AddRecord()
end

packetBuilder.AddStaticRecord = function(id, record)

    tes3mp.SetRecordId(id)
    if record.baseId ~= nil then tes3mp.SetRecordBaseId(record.baseId) end
    if record.model ~= nil then tes3mp.SetRecordModel(record.model) end

    tes3mp.AddRecord()
end

packetBuilder.AddWeaponRecord = function(id, record)

    tes3mp.SetRecordId(id)
    if record.baseId ~= nil then tes3mp.SetRecordBaseId(record.baseId) end
    if record.name ~= nil then tes3mp.SetRecordName(record.name) end
    if record.model ~= nil then tes3mp.SetRecordModel(record.model) end
    if record.icon ~= nil then tes3mp.SetRecordIcon(record.icon) end
    if record.subtype ~= nil then tes3mp.SetRecordSubtype(record.subtype) end
    if record.weight ~= nil then tes3mp.SetRecordWeight(record.weight) end
    if record.value ~= nil then tes3mp.SetRecordValue(record.value) end
    if record.health ~= nil then tes3mp.SetRecordHealth(record.health) end
    if record.speed ~= nil then tes3mp.SetRecordSpeed(record.speed) end
    if record.reach ~= nil then tes3mp.SetRecordReach(record.reach) end
    if record.damageChop ~= nil then tes3mp.SetRecordDamageChop(record.damageChop.min, record.damageChop.max) end
    if record.damageSlash ~= nil then tes3mp.SetRecordDamageSlash(record.damageSlash.min, record.damageSlash.max) end
    if record.damageThrust ~= nil then tes3mp.SetRecordDamageThrust(record.damageThrust.min, record.damageThrust.max) end
    if record.flags ~= nil then tes3mp.SetRecordFlags(record.flags) end
    if record.enchantmentId ~= nil then tes3mp.SetRecordEnchantmentId(record.enchantmentId) end
    if record.enchantmentCharge ~= nil then tes3mp.SetRecordEnchantmentCharge(record.enchantmentCharge) end
    if record.script ~= nil then tes3mp.SetRecordScript(record.script) end

    tes3mp.AddRecord()
end

return packetBuilder
