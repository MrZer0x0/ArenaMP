-- Y082. Bound equipment is client-local temporary magic, never world inventory.
local rules = {}
local bound = {}
for _, suffix in ipairs({'battle_axe', 'battleaxe', 'boots', 'cuirass', 'dagger',
    'helm', 'helmet', 'left_gauntlet', 'right_gauntlet', 'gauntlet_left', 'gauntlet_right', 'longbow', 'longsword',
    'mace', 'shield', 'spear'}) do
    local id = 'bound_' .. suffix
    bound[id] = true
    bound[id:gsub('_', ' ')] = true
end
function rules.IsBound(refId)
    local id = string.lower(tostring(refId or ''))
    if bound[id] then return true end
    for _, customId in ipairs(config.arenaBoundItemRefIds or {}) do
        if id == string.lower(customId) then return true end
    end
    return false
end
local function base(values, key)
    local n = tonumber(values and values[key] and values[key].base) or 0
    if n ~= n then return 0 end
    return math.max(0, math.min(100, n))
end
function rules.Limit(player)
    local data = player.data or {}
    return 1 + math.floor(9 * math.min(base(data.attributes, 'Intelligence'),
        base(data.skills, 'Conjuration')) / 100)
end
function rules.AllowSpawn(objects)
    local counts = {}
    for _, object in pairs(objects) do
        if rules.IsBound(object.refId) then return false end
        local summon = object.summon
        if summon and summon.hasPlayerSummoner then
            local pid = summon.summoner and summon.summoner.pid
            local player = Players[pid]
            if not player or not player:IsLoggedIn() then return false end
            if counts[pid] == nil then
                counts[pid] = 0
                for _ in pairs(player.summons or {}) do counts[pid] = counts[pid] + 1 end
            end
            counts[pid] = counts[pid] + 1
            if counts[pid] > rules.Limit(player) then return false end
        end
    end
    return true
end
function rules.Register()
    local deny = function() return customEventHooks.makeEventStatus(false, false) end
    customEventHooks.registerValidator('OnObjectSpawn', function(status, pid, cell, objects)
        if not rules.AllowSpawn(objects) then return deny() end
    end)
    customEventHooks.registerValidator('OnObjectPlace', function(status, pid, cell, objects)
        for _, object in pairs(objects) do
            if rules.IsBound(object.refId) then return deny() end
        end
    end)
    customEventHooks.registerValidator('OnContainer', function(status, pid, cell, objects)
        -- Y109: a requested SET is cell synchronization, not a player transfer.
        -- One temporary bound weapon on an NPC must not reject the entire cell
        -- snapshot and leave every ordinary chest locked in the initial sync.
        -- eventHandler validates the full request's sender; SaveContainers drops
        -- bound entries before any snapshot is saved or sent to other players.
        if tes3mp.GetObjectListAction() == enumerations.container.SET and
            tes3mp.GetObjectListContainerSubAction() == enumerations.containerSub.REPLY_TO_REQUEST then
            return
        end
        -- Gameplay transfers remain rejected, including mixed TAKE_ALL/DRAG.
        for i = 0, tes3mp.GetObjectListSize() - 1 do
            for j = 0, tes3mp.GetContainerChangesSize(i) - 1 do
                if rules.IsBound(tes3mp.GetContainerItemRefId(i, j)) then return deny() end
            end
        end
    end)
    customEventHooks.registerValidator('OnPlayerInventory', function(status, pid, packet)
        -- Never persist bound equipment; preserve unrelated inventory changes.
        for i = #(packet.inventory or {}), 1, -1 do
            if rules.IsBound(packet.inventory[i].refId) then table.remove(packet.inventory, i) end
        end
    end)
end
return rules
