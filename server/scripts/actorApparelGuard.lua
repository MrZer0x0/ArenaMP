-- Keep inventory-backed worn apparel across barter and client resynchronization.
-- This never adds inventory items or pins weapon/shield/ammunition slots.
local guard = {}
local SLOT_MAX = enumerations.equipment.BELT or 15
local CLOTHING_SLOT_MIN = enumerations.equipment.SHIRT or 8

local function usableInSlot(item, slot)
    -- Scripted AddItem may serialize clothing with charge=0. Clothing/jewelry
    -- have no durability; only armor slots can be broken by a zero charge.
    return slot >= CLOTHING_SLOT_MIN or tonumber(item.charge) ~= 0
end

local function valid(item)
    return type(item) == "table" and type(item.refId) == "string" and item.refId ~= ""
        and (tonumber(item.count) or 1) > 0
end

local function key(item)
    return string.lower(item.refId)
end

function guard.IsDead(object)
    return (tonumber(object.deathState) or 0) ~= 0
        or (type(object.stats) == "table" and tonumber(object.stats.healthCurrent) ~= nil
            and tonumber(object.stats.healthCurrent) <= 0)
end

function guard.Repair(object, incoming, transferApparel)
    if guard.IsDead(object) then return false end
    local available = {}
    local hasInventory = type(object.inventory) == "table"
    for _, item in pairs(hasInventory and object.inventory or {}) do
        if valid(item) then
            local id = key(item)
            available[id] = available[id] or {}
            table.insert(available[id], {item=item, count=tonumber(item.count) or 1})
        end
    end
    local function take(item)
        local entries = available[key(item)] or {}
        local fallback
        for _, entry in ipairs(entries) do
            if entry.count > 0 then
                fallback = fallback or entry
                if (tonumber(entry.item.charge) or -1) == (tonumber(item.charge) or -1)
                    and (tonumber(entry.item.enchantmentCharge) or -1) == (tonumber(item.enchantmentCharge) or -1)
                    and (entry.item.poisonId or "") == (item.poisonId or "")
                    and (tonumber(entry.item.poisonCharges) or 0) == (tonumber(item.poisonCharges) or 0) then
                    entry.count = entry.count - 1
                    return entry.item
                end
            end
        end
        if fallback then fallback.count = fallback.count - 1; return fallback.item end
    end
    -- Reserve items already worn, including two rings with the same refId.
    for slot = 0, SLOT_MAX do
        if valid(incoming[slot]) then take(incoming[slot]) end
    end
    local repaired = false
    for slot = 0, SLOT_MAX do
        if not valid(incoming[slot]) then
            local previous = object.equipment and object.equipment[slot]
            if not valid(previous) then previous = transferApparel and transferApparel[slot] end
            -- Broken armor may legitimately be unequipped by the engine.
            if valid(previous) and usableInSlot(previous, slot) then
                local source = hasInventory and take(previous) or nil
                if not hasInventory and transferApparel and valid(transferApparel[slot]) then
                    source = transferApparel[slot]
                end
                if source and usableInSlot(source, slot) then
                    -- Use CURRENT inventory condition, not old equipment charge:
                    -- native loading must not create a second durability variant.
                    local copy = {refId=source.refId, count=1, charge=source.charge,
                        enchantmentCharge=source.enchantmentCharge,
                        poisonId=source.poisonId, poisonCharges=source.poisonCharges}
                    incoming[slot] = copy
                    repaired = true
                end
            end
        end
    end
    return repaired
end

-- A confirmed inventory removal must invalidate cached worn items before the
-- next cell replay. Otherwise native equipment loading can recreate sold loot.
function guard.Prune(object)
    if type(object.inventory) ~= "table" or type(object.equipment) ~= "table" then return false end
    local available = {}
    for _, item in pairs(object.inventory) do
        if valid(item) then available[key(item)] = (available[key(item)] or 0) + (tonumber(item.count) or 1) end
    end
    local changed = false
    for slot = 0, SLOT_MAX do
        local item = object.equipment[slot]
        if valid(item) then
            local id = key(item)
            if (available[id] or 0) > 0 then
                available[id] = available[id] - 1
            else
                object.equipment[slot] = nil
                changed = true
            end
        end
    end
    return changed
end

return guard
