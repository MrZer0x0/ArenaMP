-- ArenaMP HS1: personal home storage.
--
-- Selling a home, "Clear home", gifting it away or an admin reset used to wipe
-- the private copy of the interior together with everything the owner kept in
-- it. Now, right before such a reset, the owner's belongings are written to a
-- separate database (data/custom/arenaHouseStorage.json):
--   * every item lying in any container of the home;
--   * loose items the owner put down (only real inventory items: weapons,
--     armour, books, potions..., checked against the active plugins and the
--     server's generated records, never statics);
--   * furniture bought in the furniture shop and placed in the home: it goes
--     back to the owner's furniture inventory ("My furniture").
-- The next time the owner enters a home of theirs, the items are put into the
-- first chest or box there. A home without any container gets a free storage
-- chest placed in front of the owner. A home entered for the first time with
-- no container offers to place a chest (free once, then from the furniture
-- inventory or the furniture shop).
--
-- Safety: the collected bundle is staged on disk BEFORE the reset and becomes
-- the owner's only after the reset succeeded; a failed reset discards it, and a
-- crash in between is resolved at the next start (cell file still present =
-- reset did not happen). Items are therefore never duplicated nor lost.
local houseStorage = {}

local STORE_PATH = "custom/arenaHouseStorage.json"
local OFFER_MENU_ID = 9063
if type(config) == "table" and type(config.customMenuIds) == "table" then
    OFFER_MENU_ID = config.customMenuIds.houseStorageOffer or OFFER_MENU_ID
end
local TICK_MS = 1500
local MAX_TRIES = 40 -- about a minute for the home to finish loading/preparing

local db = nil
local pending = {}   -- pid -> {cell=, tries=}
local offers = {}    -- pid -> {cell=, options={...}}
local timerId = nil

local function lower(s) return type(s) == "string" and s:lower() or "" end

local function isRu(pid)
    local player = Players and Players[pid]
    local language = player and player.language
    if language == nil and type(localization) == "table" and type(localization.GetClientLanguage) == "function" then
        local ok, value = pcall(localization.GetClientLanguage, pid)
        if ok then language = value end
    end
    return tostring(language or ""):upper():sub(1, 2) == "RU"
end
local function tr(pid, ru, en) return isRu(pid) and ru or en end

local function logInfo(message)
    tes3mp.LogMessage(enumerations.log.INFO, "[HouseStorage] " .. message)
end
local function logError(message)
    tes3mp.LogMessage(enumerations.log.ERROR, "[HouseStorage] " .. message)
end

-- ------------------------------------------------------------------ database

local function load()
    if db ~= nil then return db end
    local ok, data = pcall(jsonInterface.load, STORE_PATH)
    db = (ok and type(data) == "table") and data or {}
    db.version = 1
    db.accounts = type(db.accounts) == "table" and db.accounts or {}
    db.staged = type(db.staged) == "table" and db.staged or {}
    return db
end

local function save()
    load()
    local saver = jsonInterface.saveAtomic or jsonInterface.save
    local ok, result = pcall(saver, STORE_PATH, db)
    if not ok or result == false then
        logError("could not write " .. STORE_PATH .. ": " .. tostring(result))
        return false
    end
    return true
end

local function account(name)
    load()
    local key = lower(name)
    local entry = db.accounts[key]
    if type(entry) ~= "table" then
        entry = {}
        db.accounts[key] = entry
    end
    entry.items = type(entry.items) == "table" and entry.items or {}
    entry.loose = type(entry.loose) == "table" and entry.loose or {}
    entry.furniture = type(entry.furniture) == "table" and entry.furniture or {}
    entry.nonCommercial = type(entry.nonCommercial) == "table" and entry.nonCommercial or {}
    entry.offered = type(entry.offered) == "table" and entry.offered or {}
    entry.generated = type(entry.generated) == "table" and entry.generated or {}
    return entry
end

local function stackKey(item)
    return lower(item.refId) .. "|" .. tostring(tonumber(item.charge) or -1) .. "|" ..
        tostring(tonumber(item.enchantmentCharge) or -1) .. "|" .. tostring(item.soul or "")
end

local function addStack(list, item, count)
    count = math.floor(tonumber(count) or 0)
    if count < 1 or type(item.refId) ~= "string" or item.refId == "" then return end
    local key = stackKey(item)
    for _, entry in ipairs(list) do
        if stackKey(entry) == key then
            entry.count = entry.count + count
            return
        end
    end
    list[#list + 1] = {
        refId = item.refId, count = count,
        charge = tonumber(item.charge) or -1,
        enchantmentCharge = tonumber(item.enchantmentCharge) or -1,
        soul = type(item.soul) == "string" and item.soul or ""
    }
end

local function totalCount(list)
    local total = 0
    for _, item in ipairs(list or {}) do total = total + (tonumber(item.count) or 0) end
    return total
end

local function asSet(list)
    local set = {}
    for _, value in pairs(type(list) == "table" and list or {}) do set[value] = true end
    return set
end

-- --------------------------------------------------------- item knowledge

-- true = a real inventory item, false = not an item (static, door...),
-- nil = unknown right now (plugins could not be read): keep it and retry.
local function isPortable(refId)
    if type(houseItemMarket) == "table" and type(houseItemMarket.IsPortable) == "function" then
        local ok, result = pcall(houseItemMarket.IsPortable, refId)
        if ok then return result end
    end
    return nil
end

local function generatedStore(refId)
    if type(logicHandler) ~= "table" or type(logicHandler.IsGeneratedRecord) ~= "function"
        or not logicHandler.IsGeneratedRecord(refId) then return nil end
    return logicHandler.GetRecordStoreByRecordId(refId)
end

local function saveRecordStore(store)
    if type(store.QuicksaveToDrive) == "function" then store:QuicksaveToDrive()
    elseif type(store.Save) == "function" then store:Save() end
end

-- Generated records (enchanted items, custom potions...) are deleted by the
-- server once nothing refers to them. While items sit in this database the
-- owner's account refers to them.
local function linkGeneratedToOwner(items, ownerName, generated)
    for _, item in ipairs(items) do
        local store = generatedStore(item.refId)
        if store ~= nil then
            store:AddLinkToPlayer(item.refId, { accountName = ownerName })
            saveRecordStore(store)
            generated[item.refId] = store.storeType
        end
    end
end

-- ------------------------------------------------------------ collecting

local function cellData(instanceCell)
    local room = LoadedCells and LoadedCells[instanceCell]
    if room ~= nil and type(room.data) == "table" then return room.data end
    if type(fileHelper) ~= "table" then return nil end
    local fixed = fileHelper.fixFilename(instanceCell)
    local ok, data = pcall(jsonInterface.load, "cell/" .. fixed .. ".json")
    if not ok or type(data) ~= "table" then return nil end
    if type(tableHelper.fixNumericalKeys) == "function" then pcall(tableHelper.fixNumericalKeys, data) end
    return data
end

-- Collect the owner's belongings from a home copy that is about to be reset.
-- Returns a staged bundle (already written to disk) or nil when there is
-- nothing to keep.
function houseStorage.Stage(instanceCell, ownerName)
    if type(instanceCell) ~= "string" or type(ownerName) ~= "string" or ownerName == "" then return nil end
    load()
    local data = cellData(instanceCell)
    if type(data) ~= "table" or type(data.objectData) ~= "table" then return nil end

    local packets = type(data.packets) == "table" and data.packets or {}
    local deleted = asSet(packets.delete)
    local actors = asSet(packets.actorList)
    for index in pairs(asSet(packets.spawn)) do actors[index] = true end
    local placed = asSet(packets.place)
    local ownerKey = lower(ownerName)
    -- A home whose purchase clean-up has not finished still holds the cell's
    -- ORIGINAL loot, which never belonged to the owner: keep only furniture.
    local helper = rawget(_G, "houseHelper")
    local originalLootPending = type(helper) == "table" and type(helper.IsPurgePending) == "function"
        and helper.IsPurgePending(instanceCell) == true

    local bundle = {
        cell = instanceCell, owner = ownerKey, ownerName = ownerName, stagedAt = os.time(),
        items = {}, loose = {}, furniture = {}, nonCommercial = {}, generated = {}
    }
    local looseCandidates = {}

    for index, object in pairs(data.objectData) do
        if type(object) == "table" and type(object.refId) == "string" and not deleted[index] and not actors[index] then
            if type(object.inventory) == "table" and not originalLootPending then
                for _, item in pairs(object.inventory) do
                    if type(item) == "table" then addStack(bundle.items, item, item.count) end
                end
            end
            if object.arenaFurnitureOwner == ownerKey and object.arenaFurnitureRefId == object.refId then
                bundle.furniture[object.refId] = (bundle.furniture[object.refId] or 0) + 1
                if object.arenaFurnitureNonCommercial then
                    bundle.nonCommercial[object.refId] = (bundle.nonCommercial[object.refId] or 0) + 1
                end
            elseif not originalLootPending and placed[index] and type(index) == "string" and index:match("^0%-%d+$") then
                addStack(looseCandidates, object, tonumber(object.count) or 1)
            end
        end
    end

    -- Only real inventory items may go into a chest; anything unknown waits.
    for _, item in ipairs(looseCandidates) do
        local portable = isPortable(item.refId)
        if portable == true then addStack(bundle.items, item, item.count)
        elseif portable == nil then addStack(bundle.loose, item, item.count) end
    end

    if #bundle.items == 0 and #bundle.loose == 0 and next(bundle.furniture) == nil then return nil end

    linkGeneratedToOwner(bundle.items, ownerName, bundle.generated)
    linkGeneratedToOwner(bundle.loose, ownerName, bundle.generated)
    db.staged[instanceCell] = bundle
    if not save() then
        db.staged[instanceCell] = nil
        return nil, "storage could not be written"
    end
    logInfo("staged " .. totalCount(bundle.items) .. " items, " .. totalCount(bundle.loose) ..
        " unverified items and furniture from " .. instanceCell .. " for " .. ownerName)
    return bundle
end

local function creditFurniture(pid, entry)
    if next(entry.furniture) == nil then return false end
    if type(arenaFurniture) ~= "table" or type(arenaFurniture.CreditStock) ~= "function" then return false end
    if not arenaFurniture.CreditStock(pid, entry.furniture, entry.nonCommercial) then return false end
    local count = 0
    for _, value in pairs(entry.furniture) do count = count + value end
    entry.furniture, entry.nonCommercial = {}, {}
    tes3mp.MessageBox(pid, -1, tr(pid,
        "Мебель из прошлого дома (" .. count .. " шт.) возвращена в “Моя мебель”.",
        "Furniture from your previous home (" .. count .. ") was returned to \"My furniture\"."))
    return true
end

local function onlinePid(ownerKey)
    for pid, player in pairs(Players or {}) do
        if player ~= nil and player:IsLoggedIn() and lower(player.accountName) == ownerKey then return pid end
    end
    return nil
end

-- The reset succeeded: the bundle now belongs to the owner.
function houseStorage.Commit(bundle)
    if type(bundle) ~= "table" then return false end
    load()
    local entry = account(bundle.owner)
    for _, item in ipairs(bundle.items or {}) do addStack(entry.items, item, item.count) end
    for _, item in ipairs(bundle.loose or {}) do addStack(entry.loose, item, item.count) end
    for refId, count in pairs(bundle.furniture or {}) do entry.furniture[refId] = (entry.furniture[refId] or 0) + count end
    for refId, count in pairs(bundle.nonCommercial or {}) do
        entry.nonCommercial[refId] = (entry.nonCommercial[refId] or 0) + count
    end
    for refId, storeType in pairs(bundle.generated or {}) do entry.generated[refId] = storeType end
    entry.ownerName = bundle.ownerName or entry.ownerName
    entry.savedAt = os.time()
    entry.fromCell = bundle.cell
    -- The reset copy will be a new home: offer a chest again when entered.
    entry.offered[bundle.cell] = nil
    db.staged[bundle.cell] = nil
    save()
    logInfo("committed storage from " .. tostring(bundle.cell) .. " for " .. tostring(bundle.ownerName))

    local pid = onlinePid(bundle.owner)
    if pid ~= nil then
        if creditFurniture(pid, entry) then save() end
        local stored = totalCount(entry.items) + totalCount(entry.loose)
        if stored > 0 then
            tes3mp.SendMessage(pid, color.Yellow .. tr(pid,
                "Ваши вещи из дома (" .. stored .. " шт.) сохранены. Они появятся в первом сундуке вашего следующего дома.",
                "Your belongings from the home (" .. stored .. ") were saved. They will appear in the first chest of your next home.") ..
                color.Default .. "\n", false)
        end
    end
    return true
end

-- The reset failed: nothing changed in the home, drop the copy.
function houseStorage.Discard(bundle)
    if type(bundle) ~= "table" then return end
    load()
    db.staged[bundle.cell] = nil
    save()
end

-- Resolve bundles left staged by a crash between Stage and Commit/Discard.
local function recoverStaged()
    load()
    local changed = false
    for cell, bundle in pairs(db.staged) do
        if cellData(cell) ~= nil then
            db.staged[cell] = nil  -- the home still exists: the reset never happened
        else
            houseStorage.Commit(bundle)
        end
        changed = true
    end
    if changed then save() end
end

-- -------------------------------------------------------------- delivering

local function containerRank(refId)
    local id = lower(refId)
    if id:find("flora", 1, true) or id:find("corpse", 1, true) or id:find("skeleton", 1, true)
        or id:find("bones", 1, true) or id:find("_dead", 1, true) then return nil end
    if id:find("chest", 1, true) or id:find("trunk", 1, true) or id:find("strongbox", 1, true)
        or id:find("сундук", 1, true) then return 1 end
    if id:find("crate", 1, true) or id:find("box", 1, true) or id:find("ящик", 1, true) then return 2 end
    return 3
end

local function indexOrder(index)
    local a, b = tostring(index):match("^(%d+)%-(%d+)$")
    return (tonumber(b) or 0) * 1e9 + (tonumber(a) or 0)
end

-- The first chest or box of the home: a storage chest placed by this module
-- first, then chests, then crates/boxes, then any other container.
function houseStorage.FindChest(room)
    if type(room) ~= "table" or type(room.data) ~= "table" then return nil end
    local objects = room.data.objectData or {}
    local packets = room.data.packets or {}
    local deleted = asSet(packets.delete)
    local actors = asSet(packets.actorList)
    for index in pairs(asSet(packets.spawn)) do actors[index] = true end

    local best, bestRank, bestOrder = nil, nil, nil
    local function consider(index, rank)
        local order = indexOrder(index)
        if best == nil or rank < bestRank or (rank == bestRank and order < bestOrder) then
            best, bestRank, bestOrder = index, rank, order
        end
    end
    for index, object in pairs(objects) do
        if type(object) == "table" and object.arenaHouseStorageChest and not deleted[index] then consider(index, 0) end
    end
    for _, index in pairs(packets.container or {}) do
        local object = objects[index]
        if type(object) == "table" and type(object.refId) == "string" and type(object.inventory) == "table"
            and not deleted[index] and not actors[index] then
            local rank = containerRank(object.refId)
            if rank ~= nil then consider(index, rank) end
        end
    end
    return best
end

local function fillChest(room, index, entry)
    local object = room.data.objectData[index]
    if type(object) ~= "table" then return 0 end
    object.inventory = type(object.inventory) == "table" and object.inventory or {}
    local moved = 0
    for _, item in ipairs(entry.items) do
        inventoryHelper.addItem(object.inventory, item.refId, item.count, item.charge,
            item.enchantmentCharge, item.soul)
        moved = moved + item.count
        local store = generatedStore(item.refId)
        if store ~= nil then
            store:AddLinkToCell(item.refId, room)
            store:RemoveLinkToPlayer(item.refId, { accountName = entry.ownerName or "" })
            saveRecordStore(store)
            entry.generated[item.refId] = nil
        end
    end
    room.data.packets.container = room.data.packets.container or {}
    if not tableHelper.containsValue(room.data.packets.container, index) then
        table.insert(room.data.packets.container, index)
    end
    if type(room.QuicksaveToDrive) == "function" then room:QuicksaveToDrive() else room:Save() end
    for _, visitor in pairs(room.visitors or {}) do
        if Players[visitor] ~= nil and Players[visitor]:IsLoggedIn() then
            room:LoadContainers(visitor, room.data.objectData, { index })
        end
    end
    entry.items = {}
    return moved
end

local function placeChest(pid, refId)
    if type(arenaFurniture) ~= "table" or type(arenaFurniture.PlaceServerChest) ~= "function" then return nil end
    refId = refId or (arenaFurniture.DefaultChest and arenaFurniture.DefaultChest())
    if type(refId) ~= "string" then return nil, "no chest in the furniture catalogue" end
    return arenaFurniture.PlaceServerChest(pid, refId)
end

local function homeOf(pid, cell)
    if type(houseItemMarket) ~= "table" or type(houseItemMarket.Property) ~= "function" then return nil end
    local property = houseItemMarket.Property(cell)
    if property == nil or Players[pid] == nil then return nil end
    if lower(property.owner) ~= lower(Players[pid].accountName) then return nil end
    return property
end

function houseStorage.ShowOffer(pid, cell)
    if Players[pid] == nil or not Players[pid]:IsLoggedIn() then return end
    local entry = account(Players[pid].accountName)
    local options, labels = {}, {}
    local function add(action, label) options[#options + 1] = action; labels[#labels + 1] = label end
    if not entry.freeChestGiven then add("free", tr(pid, "Поставить бесплатный сундук", "Place a free chest")) end
    local stockChest = type(arenaFurniture) == "table" and arenaFurniture.OwnedContainerInStock
        and arenaFurniture.OwnedContainerInStock(pid) or nil
    if stockChest then add("stock", tr(pid, "Поставить мой сундук", "Place my chest")) end
    add("shop", tr(pid, "Купить в магазине мебели", "Buy in the furniture shop"))
    add("none", tr(pid, "Не нужно", "Not now"))
    offers[pid] = { cell = cell, options = options, stockChest = stockChest }
    local text = tr(pid,
        "В вашем доме нет сундука для вещей.\n\nСундук поставится прямо перед вами. Потом его можно передвинуть или убрать, как любую мебель.\n\nПредложение можно открыть снова командой /house chest.",
        "Your home has no chest for your belongings.\n\nThe chest is placed right in front of you. You can move or pack it later like any furniture.\n\nYou can open this offer again with /house chest.")
    tes3mp.CustomMessageBox(pid, OFFER_MENU_ID, text, table.concat(labels, ";"))
end

local function deliver(pid, cell, room)
    local property = homeOf(pid, cell)
    if property == nil then return true end
    local entry = account(Players[pid].accountName)
    entry.ownerName = Players[pid].accountName
    local changed = creditFurniture(pid, entry)

    -- Items that could not be checked earlier: try again now.
    if #entry.loose > 0 then
        local still = {}
        for _, item in ipairs(entry.loose) do
            local portable = isPortable(item.refId)
            if portable == true then addStack(entry.items, item, item.count)
            elseif portable == nil then addStack(still, item, item.count) end
        end
        if #still ~= #entry.loose then changed = true end
        entry.loose = still
    end

    local chest = houseStorage.FindChest(room)
    if #entry.items > 0 then
        local placedNew = false
        if chest == nil then
            chest = placeChest(pid)
            placedNew = chest ~= nil
            if placedNew then entry.freeChestGiven = true end
        end
        if chest == nil then
            tes3mp.MessageBox(pid, -1, tr(pid,
                "Не удалось поставить сундук для ваших сохранённых вещей. Они в безопасности; попробуйте войти снова.",
                "Could not place a chest for your saved belongings. They are safe; enter the home again later."))
            if changed then save() end
            return true
        end
        local moved = fillChest(room, chest, entry)
        entry.offered[cell] = true
        save()
        tes3mp.MessageBox(pid, -1, tr(pid,
            "Ваши вещи из прошлого дома (" .. moved .. " шт.) лежат в " .. (placedNew and "сундуке перед вами." or "первом сундуке дома."),
            "Your belongings from your previous home (" .. moved .. ") are in " .. (placedNew and "the chest in front of you." or "the first chest of the home.")))
        logInfo("delivered " .. moved .. " items to " .. cell .. " " .. tostring(chest) .. " for " .. Players[pid].accountName)
        return true
    end

    if chest == nil and not entry.offered[cell] then
        entry.offered[cell] = true
        save()
        houseStorage.ShowOffer(pid, cell)
        return true
    end
    if changed then save() end
    return true
end

-- Called every TICK_MS while somebody waits for their home to be ready.
function ArenaMP_HouseStorageTick()
    local any = false
    for pid, job in pairs(pending) do
        local player = Players and Players[pid]
        if player == nil or not player:IsLoggedIn() or tes3mp.GetCell(pid) ~= job.cell then
            pending[pid] = nil
        else
            job.tries = job.tries + 1
            local room = LoadedCells and LoadedCells[job.cell]
            local ready = room ~= nil and type(room.data) == "table"
                and not (houseHelper and houseHelper.IsPurgePending and houseHelper.IsPurgePending(job.cell))
            if ready and type(room.HasFullContainerData) == "function" and not room:HasFullContainerData() then
                ready = false
                if not job.requested and type(room.EnsureFullContainerData) == "function" then
                    job.requested = true
                    pcall(room.EnsureFullContainerData, room, pid)
                end
            end
            if ready then
                pending[pid] = nil
                local ok, err = pcall(deliver, pid, job.cell, room)
                if not ok then logError("delivery failed: " .. tostring(err)) end
            elseif job.tries >= MAX_TRIES then
                pending[pid] = nil
            else
                any = true
            end
        end
    end
    if any and timerId ~= nil then tes3mp.RestartTimer(timerId, TICK_MS) end
end

local function schedule(pid, cell)
    pending[pid] = { cell = cell, tries = 0 }
    if timerId == nil then timerId = tes3mp.CreateTimer("ArenaMP_HouseStorageTick", TICK_MS) end
    tes3mp.RestartTimer(timerId, TICK_MS)
end

function houseStorage.OnEnter(pid, cell)
    if Players[pid] == nil or not Players[pid]:IsLoggedIn() or homeOf(pid, cell) == nil then return end
    local entry = account(Players[pid].accountName)
    local needsWork = #entry.items > 0 or #entry.loose > 0 or next(entry.furniture) ~= nil or not entry.offered[cell]
    if needsWork then schedule(pid, cell) end
end

-- "/house chest": open the chest offer again in your own home.
function houseStorage.CommandChest(pid)
    local cell = tes3mp.GetCell(pid)
    if homeOf(pid, cell) == nil then
        tes3mp.SendMessage(pid, tr(pid, "Встаньте внутри своего дома.", "Stand inside your home.") .. "\n", false)
        return
    end
    houseStorage.ShowOffer(pid, cell)
end

function houseStorage.HasStoredItems(name)
    local entry = account(name)
    return totalCount(entry.items) + totalCount(entry.loose)
end

customEventHooks.registerHandler("OnGUIAction", function(eventStatus, pid, idGui, data)
    if idGui ~= OFFER_MENU_ID then return end
    local offer = offers[pid]
    offers[pid] = nil
    if type(offer) ~= "table" or Players[pid] == nil or not Players[pid]:IsLoggedIn() then return end
    local action = offer.options[(tonumber(data) or -1) + 1]
    if action == nil or action == "none" then return end
    if tes3mp.GetCell(pid) ~= offer.cell or homeOf(pid, offer.cell) == nil then
        tes3mp.MessageBox(pid, -1, tr(pid, "Вы уже вышли из дома.", "You have left the home."))
        return
    end
    if action == "free" then
        local entry = account(Players[pid].accountName)
        if entry.freeChestGiven then return end
        local index, why = placeChest(pid)
        if index ~= nil then
            entry.freeChestGiven = true
            save()
            tes3mp.MessageBox(pid, -1, tr(pid, "Сундук поставлен перед вами.", "The chest is in front of you."))
        else
            tes3mp.MessageBox(pid, -1, tostring(why or tr(pid, "Не удалось поставить сундук.", "Could not place the chest.")))
        end
    elseif action == "stock" and offer.stockChest then
        local ok, message = arenaFurniture.Place(pid, offer.stockChest)
        tes3mp.MessageBox(pid, -1, tostring(message))
    elseif action == "shop" and type(arenaFurniture.OpenCategory) == "function" then
        arenaFurniture.OpenCategory(pid, "Контейнеры")
    end
end)

customEventHooks.registerHandler("OnPlayerCellChange", function(eventStatus, pid)
    local ok, err = pcall(houseStorage.OnEnter, pid, tes3mp.GetCell(pid))
    if not ok then logError("OnEnter failed: " .. tostring(err)) end
end)

customEventHooks.registerHandler("OnPlayerAuthentified", function(eventStatus, pid)
    -- Furniture saved while the owner was offline (e.g. an admin reset).
    local ok, err = pcall(function()
        local entry = account(Players[pid].accountName)
        if creditFurniture(pid, entry) then save() end
        if totalCount(entry.items) + totalCount(entry.loose) > 0 then
            tes3mp.SendMessage(pid, color.Yellow .. tr(pid,
                "У вас есть сохранённые вещи из прежнего дома. Они появятся в первом сундуке вашего дома.",
                "You have belongings saved from a previous home. They will appear in the first chest of your home.") ..
                color.Default .. "\n", false)
        end
    end)
    if not ok then logError("login credit failed: " .. tostring(err)) end
end)

customEventHooks.registerHandler("OnPlayerDisconnect", function(eventStatus, pid)
    pending[pid] = nil
    offers[pid] = nil
end)

customEventHooks.registerHandler("OnServerPostInit", function()
    local ok, err = pcall(recoverStaged)
    if not ok then logError("staged recovery failed: " .. tostring(err)) end
end)

return houseStorage
