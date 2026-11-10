-- Y092: server-owned short travel and payment, never persisted over authored AI.
local M = {}
local function message(pid, ru, en)
    tes3mp.SendMessage(pid, (localization.GetLanguage(pid) == "RU" and ru or en) .. "\n", false)
end
local function finite(value)
    return type(value) == "number" and value == value and math.abs(value) < 100000000
end
function M.RememberActivation(pid, cellDescription, objects)
    local player = Players[pid]
    if not player then return end
    player.arenaMoveAsideDialogue = nil
    for uniqueIndex, object in pairs(objects) do
        if object.activatingPid == pid then
            player.arenaMoveAsideDialogue = { cell = cellDescription, uniqueIndex = uniqueIndex,
                refId = object.refId, expires = os.time() + 600 }
        end
    end
end
function M.Handle(pid, cellDescription, objects)
    local selected, uniqueIndex, count
    count = 0
    for key, object in pairs(objects) do
        count = count + 1
        if object.dialogueChoiceType == enumerations.dialogueChoice.MOVE_ASIDE
            or object.dialogueChoiceType == enumerations.dialogueChoice.MOVE_ASIDE_PAID then
            selected, uniqueIndex = object, key
        end
    end
    if not selected then return false end
    if count ~= 1 then return true end
    local player, cell = Players[pid], LoadedCells[cellDescription]
    if not player or not player:IsLoggedIn() or not cell or cell.authority == nil
        or player.deathRecoveryActive or tes3mp.GetCell(pid) ~= cellDescription then return true end
    local dialogue = player.arenaMoveAsideDialogue
    if not dialogue or dialogue.cell ~= cellDescription or dialogue.uniqueIndex ~= uniqueIndex
        or dialogue.refId ~= selected.refId or dialogue.expires < os.time() then return true end
    local actor = cell.data.objectData[uniqueIndex]
    if not actor or actor.refId ~= selected.refId
        or not tableHelper.containsValue(cell.data.packets.actorList or {}, uniqueIndex)
        or (actor.stats and (actor.stats.healthCurrent or 0) <= 0) then return true end
    local ai = actor.ai
    if ai and (ai.action == enumerations.ai.FOLLOW or ai.action == enumerations.ai.ESCORT) then
        message(pid, "Сопровождающий сейчас не может отойти.", "A follower cannot move aside right now.")
        return true
    end
    local position = actor.location
    if not position or not finite(position.posX) or not finite(position.posY) or not finite(position.posZ) then
        message(pid, "Положение собеседника ещё не синхронизировано.", "The actor position has not synchronized yet.")
        return true
    end
    local px, py, pz = tes3mp.GetPosX(pid), tes3mp.GetPosY(pid), tes3mp.GetPosZ(pid)
    if not finite(px) or not finite(py) or not finite(pz) then return true end
    local dx, dy, dz = position.posX - px, position.posY - py, position.posZ - pz
    local distance = math.sqrt(dx * dx + dy * dy)
    if distance * distance + dz * dz > 512 * 512 then return true end
    -- Per-actor cooldown also serializes simultaneous requests from different players.
    cell.arenaMoveAsideCooldown = cell.arenaMoveAsideCooldown or {}
    local now = os.time()
    if now < (cell.arenaMoveAsideCooldown[uniqueIndex] or 0) then return true end
    local cost = selected.dialogueChoiceType == enumerations.dialogueChoice.MOVE_ASIDE_PAID and 10 or 0
    -- Disposition tier is client-reported (TES3MP exposes no derived-disposition API).
    -- Gold, distance, actor existence, cooldown and travel are checked server-side.
    local inventory = player.data.inventory or {}
    local gold = 0
    for _, item in pairs(inventory) do
        if string.lower(item.refId or "") == "gold_001" then gold = gold + math.max(0, item.count or 0) end
    end
    if gold < cost then
        message(pid, "Нужно 10 золотых, чтобы попросить отойти.", "You need 10 gold to ask the actor to move aside.")
        return true
    end
    if distance < 1 then dx, dy, distance = 0, 1, 1 end
    -- Lateral target, 128 units from the actor. Normal AiTravel pathfinding/collision
    -- chooses the route; this is never a teleport or a client-provided coordinate.
    local aiData = { action = enumerations.ai.TRAVEL,
        posX = position.posX - dy / distance * 128,
        posY = position.posY + dx / distance * 128, posZ = position.posZ, shouldRepeat = false }
    local remaining = cost
    local removed = {}
    for index, item in pairs(inventory) do
        if remaining > 0 and string.lower(item.refId or "") == "gold_001" then
            local amount = math.min(remaining, math.max(0, item.count or 0))
            if amount > 0 then
                local change = tableHelper.deepCopy(item)
                change.count = amount
                table.insert(removed, change)
                item.count = item.count - amount
                remaining = remaining - amount
                if item.count == 0 then inventory[index] = nil end
            end
        end
    end
    if cost > 0 then
        tableHelper.cleanNils(inventory)
        player:LoadItemChanges(removed, enumerations.inventory.REMOVE)
        player:QuicksaveToDrive()
    end
    player.arenaMoveAsideDialogue = nil
    cell.arenaMoveAsideCooldown[uniqueIndex] = now + 3
    tes3mp.ClearActorList()
    tes3mp.SetActorListPid(cell.authority)
    tes3mp.SetActorListCell(cellDescription)
    packetBuilder.AddAIActor(uniqueIndex, nil, aiData)
    tes3mp.SendActorAI(true, false)
    tes3mp.CopyReceivedObjectListToStore()
    tes3mp.SendObjectDialogueChoice(false, false)
    return true
end
return M
