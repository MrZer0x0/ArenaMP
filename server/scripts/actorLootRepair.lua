require("config")

-- Arena Y019/Y020 — actor loot repair
--
-- DedicatedActor equipment snapshots can re-create worn items when authority
-- changes and the same refId arrives with a different durability/enchantment
-- charge.  If that inflated client inventory is later accepted as the corpse
-- container, one NPC can suddenly contain two or more copies of the same worn
-- weapon/armour piece.
--
-- This repair is deliberately conservative:
--   * only refIds that are actually present in the actor's equipment are touched;
--   * only inventory entries carrying mutable condition data are candidates;
--   * the number of legitimate equipped copies is preserved (important for two
--     identical rings or other same-refId equipment in multiple slots);
--   * exact equipment condition snapshots are preferred, then the newest/most
--     worn remaining copy is retained as fallback.
-- Ordinary loot, ammunition stacks and unrelated duplicate refIds are untouched.

local tableHelper = require("tableHelper")

local repair = {}

local function lower(value)
    return string.lower(tostring(value or ""))
end

local function toNumber(value, fallback)
    return tonumber(value) or fallback
end

local function isCharged(item)
    return toNumber(item.charge, -1) >= 0 or toNumber(item.enchantmentCharge, -1) >= 0
end

-- refId -> { count = number of equipped slots, items = equipment snapshots }
local function buildEquipmentIndex(objectData)
    local equipment = objectData.equipment
    if type(equipment) ~= "table" then return nil end

    local index = {}
    local any = false

    for _, item in pairs(equipment) do
        if type(item) == "table" and item.refId ~= nil and item.refId ~= "" then
            local key = lower(item.refId)
            if index[key] == nil then
                index[key] = { count = 0, items = {} }
            end
            index[key].count = index[key].count + 1
            table.insert(index[key].items, item)
            any = true
        end
    end

    if not any then return nil end
    return index
end

local function exactConditionMatch(entry, equipmentItem)
    return toNumber(entry.item.charge, -1) == toNumber(equipmentItem.charge, -1)
        and toNumber(entry.item.enchantmentCharge, -1) == toNumber(equipmentItem.enchantmentCharge, -1)
end

local function survivorScore(entry)
    local charge = toNumber(entry.item.charge, -1)
    local enchantment = toNumber(entry.item.enchantmentCharge, -1)

    -- Lower durability generally represents the newest state after a fight.  If
    -- the item has no durability, use lower remaining enchantment charge instead.
    if charge >= 0 then return charge end
    if enchantment >= 0 then return enchantment end
    return math.huge
end

local function selectSurvivors(entries, equipmentInfo)
    local keepCount = 1
    if equipmentInfo ~= nil then
        keepCount = math.max(1, tonumber(equipmentInfo.count) or 1)
    end

    if #entries <= keepCount then return nil, keepCount end

    local selected = {}
    local selectedCount = 0

    -- First preserve exact per-slot condition snapshots when they still exist.
    if equipmentInfo ~= nil and type(equipmentInfo.items) == "table" then
        for _, equipmentItem in ipairs(equipmentInfo.items) do
            for entryIndex, entry in ipairs(entries) do
                if selected[entryIndex] ~= true and exactConditionMatch(entry, equipmentItem) then
                    selected[entryIndex] = true
                    selectedCount = selectedCount + 1
                    break
                end
            end
            if selectedCount >= keepCount then break end
        end
    end

    -- Fill any remaining legitimate slots with the most worn/newest candidates.
    while selectedCount < keepCount do
        local bestIndex = nil
        local bestScore = math.huge

        for entryIndex, entry in ipairs(entries) do
            if selected[entryIndex] ~= true then
                local score = survivorScore(entry)
                if bestIndex == nil or score < bestScore then
                    bestIndex = entryIndex
                    bestScore = score
                end
            end
        end

        if bestIndex == nil then break end
        selected[bestIndex] = true
        selectedCount = selectedCount + 1
    end

    return selected, keepCount
end

-- Collapse equipment-sync duplicates in one actor container. Returns how many
-- item entries were removed.
function repair.Repair(cell, uniqueIndex)
    local cfg = config.actorLootRepair or {}
    if cfg.enabled == false then return 0 end

    if cell == nil or cell.data == nil or cell.data.objectData == nil then return 0 end

    local objectData = cell.data.objectData[uniqueIndex]
    if objectData == nil or type(objectData.inventory) ~= "table" then return 0 end

    local equipmentIndex = buildEquipmentIndex(objectData)
    if equipmentIndex == nil and cfg.requireEquipmentMatch ~= false then return 0 end

    local groups = {}
    for inventoryIndex, item in pairs(objectData.inventory) do
        if type(item) == "table" and item.refId ~= nil and isCharged(item) then
            local key = lower(item.refId)
            if equipmentIndex == nil or equipmentIndex[key] ~= nil then
                groups[key] = groups[key] or {}
                table.insert(groups[key], { index = inventoryIndex, item = item })
            end
        end
    end

    local removedEntries = 0

    for key, entries in pairs(groups) do
        local equipmentInfo = equipmentIndex and equipmentIndex[key] or nil
        local selected, keepCount = selectSurvivors(entries, equipmentInfo)

        if selected ~= nil then
            local keptRefId = nil
            local kept = 0

            for entryIndex, entry in ipairs(entries) do
                if selected[entryIndex] == true then
                    kept = kept + 1
                    keptRefId = keptRefId or entry.item.refId
                else
                    objectData.inventory[entry.index] = nil
                    removedEntries = removedEntries + 1
                end
            end

            tes3mp.LogMessage(enumerations.log.WARN,
                "Repaired duplicated actor loot in " .. tostring(cell.description) .. " " .. tostring(uniqueIndex) ..
                ": kept " .. tostring(kept) .. " legitimate equipped copy/copies of " .. tostring(keptRefId) ..
                " from " .. tostring(#entries) .. " charged entries (expected slots " .. tostring(keepCount) .. ")")
        end
    end

    if removedEntries > 0 then
        tableHelper.cleanNils(objectData.inventory)
    end

    return removedEntries
end

return repair
