-- ArenaMP Y052 server-authoritative empty-bottle requirement for alchemy.
--
-- Y049 added the requirement in apps/openmw/mwgui/alchemywindow.cpp only, so a
-- modified client could brew without ever owning a bottle. The client check
-- stays as the UX layer (it greys out the batch count and explains why), while
-- this validator is the authority.
--
-- Matching is done on refId, not on the displayed name: the server only stores
-- refIds in Players[pid].data.inventory and has no access to localized item
-- names. Vanilla Morrowind empty bottles are Misc records whose refIds contain
-- "bottle" (misc_com_bottle_01 .. _15, misc_de_bottle_*, ...), which is what the
-- default token list below targets.
--
-- Ordering note: OpenMW sends ID_RECORD_DYNAMIC for the new potion *before* the
-- inventory packet that removes the bottles, so at validation time the bottles
-- are still present in the server-side inventory copy. This validator therefore
-- only checks; it never removes anything, and there is no double-consumption.
local alchemyBottleHelper = {}

local cfg = {
    enabled = true,
    refIdTokens = { "bottle", "flask", "vial" },
    refIds = {},
    logRejections = true
}

if type(config.alchemySystem) == "table" then
    local source = config.alchemySystem
    if source["require bottle"] ~= nil then cfg.enabled = source["require bottle"] == true end
    if type(source["bottle refid tokens"]) == "table" then cfg.refIdTokens = source["bottle refid tokens"] end
    if type(source["bottle refids"]) == "table" then cfg.refIds = source["bottle refids"] end
    if source["log rejections"] ~= nil then cfg.logRejections = source["log rejections"] == true end
end

local exactRefIds = {}
for _, refId in ipairs(cfg.refIds) do
    exactRefIds[string.lower(tostring(refId))] = true
end

local lowerTokens = {}
for _, token in ipairs(cfg.refIdTokens) do
    token = string.lower(tostring(token))
    if token ~= "" then table.insert(lowerTokens, token) end
end

local function isBottleRefId(refId)
    refId = string.lower(tostring(refId or ""))
    if refId == "" then return false end
    if exactRefIds[refId] then return true end
    for _, token in ipairs(lowerTokens) do
        if string.find(refId, token, 1, true) ~= nil then return true end
    end
    return false
end

-- Counts empty bottles in the server-side copy of the player inventory.
function alchemyBottleHelper.CountBottles(pid)
    if Players[pid] == nil or type(Players[pid].data) ~= "table" then return 0 end
    local inventory = Players[pid].data.inventory
    if type(inventory) ~= "table" then return 0 end

    local total = 0
    for _, item in pairs(inventory) do
        if type(item) == "table" and isBottleRefId(item.refId) then
            total = total + math.max(0, tonumber(item.count) or 0)
        end
    end
    return total
end

local function isRu(pid)
    return Players[pid] ~= nil and tostring(Players[pid].language or "EN"):upper() == "RU"
end

customEventHooks.registerValidator("OnRecordDynamic", function(eventStatus, pid, recordArray, storeType)
    if not cfg.enabled or storeType ~= "potion" then return end
    if Players[pid] == nil or not Players[pid]:IsLoggedIn() then return end
    if type(recordArray) ~= "table" then return end

    local needed = 0
    for _, record in pairs(recordArray) do
        needed = needed + math.max(1, tonumber(record.quantity) or 1)
    end
    if needed <= 0 then return end

    local available = alchemyBottleHelper.CountBottles(pid)
    if available >= needed then return end

    if cfg.logRejections then
        tes3mp.LogMessage(enumerations.log.WARN,
            "[ArenaMP] rejected potion record from " .. logicHandler.GetChatName(pid) ..
            ": needs " .. tostring(needed) .. " empty bottle(s), has " .. tostring(available))
    end

    tes3mp.MessageBox(pid, -1, isRu(pid)
        and "У вас не хватает пустых бутылок для алхимии."
        or "You don't have enough empty bottles for alchemy.")

    -- Resynchronise the client with the authoritative inventory so the potion it
    -- optimistically added locally disappears again. SET replaces the whole
    -- client inventory with the server copy.
    if enumerations.inventory ~= nil and enumerations.inventory.SET ~= nil then
        Players[pid]:LoadItemChanges(Players[pid].data.inventory, enumerations.inventory.SET)
    end

    return customEventHooks.makeEventStatus(false, false)
end)

return alchemyBottleHelper
