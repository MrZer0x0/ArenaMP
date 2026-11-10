-- ===========================================================================
-- ArenaMP Z001 — houseHelper: дома, магазины, таверны и гильдейские залы
-- ===========================================================================
--
-- Что это решает
-- --------------
-- В архивной версии сервера «дом» был теневой ячейкой, в которую игрока
-- затаскивали обработчиком OnPlayerCellChange: он входил в дверь, попадал в
-- оригинальную ячейку, и только потом сервер телепортировал его в инстанс.
-- Отсюда и рывок «вошёл — и тебя вернуло назад»: клиент уже выполнил переход,
-- а сервер спорил с ним задним числом.
--
-- Здесь переход через дверь решается ДО перехода. Клиент откладывает телепорт
-- управляемой двери и спрашивает сервер, куда идти; сервер показывает выбор
-- (оригинальная ячейка, дом игрока, магазин, таверна) и телепортирует
-- авторитетно. Клиент никогда не оказывается в ячейке, из которой его нужно
-- вытаскивать.
--
-- Из чего состоит система
-- -----------------------
--   * Реестр объектов недвижимости в server/data/custom/arenaHouses.json.
--   * Автоопределение: имя ячейки, похожее на дом/лавку/таверну/гильдию,
--     регистрируется само при первом обращении к ней. Администратору не нужно
--     перечислять тысячу интерьеров вручную.
--   * Инстанс на владельца: динамическая CELL-запись с baseId, как у
--     privateCellInstances. Ячейку не надо переделывать под дом — инстанс
--     наследует её содержимое, а лишнее из него вычищается.
--   * Автоочистка инстанса: NPC, существа и мусорные предметы (оружие, misc,
--     книги...) удаляются, чтобы в купленном доме не жил бывший хозяин.
--   * Права: владелец, совладельцы, друзья, замок. Запертый дом не предлагает
--     себя чужим — они просто проходят в оригинальную ячейку.
--   * Автоценообразование по типу и названию.
--   * Очистка при освобождении — через resetHelper (periodicCellResets).
--
-- Интеграция с resetHelper
-- ------------------------
-- resetHelrer.lua уже умеет искать необязательный houseHelper и звать у него
-- GetCellData / IsHouseCell / GetHouseOwnerName. Эти три метода — публичный
-- контракт, менять их сигнатуры нельзя.
-- ===========================================================================

require("config")

local jsonInterface = require("jsonInterface")
local fileHelper = require("fileHelper")
local inventoryHelper = require("inventoryHelper")
local packetBuilder = require("packetBuilder")

local houseHelper = {}

local LOG_TAG = "[ArenaMP House] "
local STORE_PATH = "custom/arenaHouses.json"
local CONTROL_PREFIX = "@@AMP_HOUSE@@"
-- Session-only door interests; never write client claims into the registry.
local clientInterests = {}
local CURRENCY = "gold_001"

local function log(level, message)
    tes3mp.LogAppend(level, LOG_TAG .. tostring(message))
end

-- ===========================================================================
-- КОНФИГУРАЦИЯ
-- ===========================================================================
local defaults = {
    enabled = true,

    -- Суффикс инстанса. Отличается от privateCellInstances, чтобы две системы
    -- никогда не претендовали на одну и ту же сгенерированную ячейку.
    legacyInstanceSuffix = " - Home of ", -- old saved instances only; new names end in " - home"

    -- Автоматически регистрировать подходящие по названию ячейки.
    autoRegister = true,

    -- Базовые цены по типу объекта.
    basePrice = {
        house = 8000,
        shop = 25000,
        tavern = 35000,
        guild = 60000
    },

    -- Минимальный уровень покупателя.
    minimumLevel = {
        house = 10,
        shop = 20,
        tavern = 15,
        guild = 25
    },

    -- Сколько объектов одного типа может держать один аккаунт.
    ownershipLimit = {
        house = 1,
        shop = 1,
        tavern = 1,
        guild = 1
    },

    -- Доля цены, возвращаемая при продаже.
    refundRatio = 0.5,

    -- Что вычищать из инстанса. Классы соответствуют тем, что присылает
    -- клиент в пакете очистки; "actor" — NPC и существа.
    purgeClasses = { "actor", "misc", "weapon", "book", "ingredient", "apparatus",
        "clothing", "armor", "potion", "lockpick", "probe", "repair", "light" },

    -- Что в инстансе трогать нельзя ни при каких условиях.
    purgeKeepRefIds = { "door", "static", "container", "activator", "bed", "furniture" },

    -- Через сколько дней после регистрации аккаунта сундуки в собственном
    -- домашнем инстансе перестают учитывать ESM-лимит вместимости.
    bottomlessStorageAgeDays = 30,

    -- Ранг персонала, которому доступны админские операции.
    adminRank = 2
}

local function cfg()
    local user = type(config.houseSystem) == "table" and config.houseSystem or {}
    local merged = {}
    for key, value in pairs(defaults) do
        if user[key] ~= nil then
            merged[key] = user[key]
        else
            merged[key] = value
        end
    end
    -- Read the old option only for compatibility with saved owner-named cells.
    if user.legacyInstanceSuffix == nil and user.instanceSuffix ~= nil then
        merged.legacyInstanceSuffix = user.instanceSuffix
    end
    if config.houseEnabled ~= nil then merged.enabled = config.houseEnabled == true end
    merged.generationMode = config.houseAutoGeneration or (merged.autoRegister and "players" or "off")
    merged.generationLevel = config.houseGenerationLevel or "houses"
    return merged
end

-- ===========================================================================
-- КЛАССИФИКАЦИЯ ЯЧЕЙКИ ПО НАЗВАНИЮ
-- ---------------------------------------------------------------------------
-- Смысл всего этого блока — не угадать «правильный» тип, а отсеять очевидно
-- неподходящие интерьеры (гробницы, пещеры, храмы, крепости) и разложить
-- остальное по четырём корзинам. Ошибка классификации не ломает сервер:
-- администратор меняет тип одной строкой в интерфейсе.
--
-- Списки намеренно содержат и английские, и русские варианты: сервер видит
-- название ячейки ровно в том виде, в каком его дали плагины, а русские
-- сборки переводят интерьеры.
-- ===========================================================================
local EXCLUDE_PATTERNS = {
    "tomb", "burial", "ancestral", "cave", "grotto", "mine", "ruin", "shrine",
    "temple", "palace", "stronghold", "fort", "prison", "jail", "sewer", "dungeon",
    "lighthouse", "shipwreck", "daedric", "dwemer", "velothi", "egg mine",
    "гробниц", "склеп", "пещер", "шахт", "руин", "святилищ", "храм", "дворец",
    "крепост", "форт", "тюрьм", "канализац", "подземел", "маяк", "затонув",
    "даэдрич", "двемер", "велот", "яйцевая шахта"
}

local KIND_PATTERNS = {
    guild = {
        "guild", "hall of", "council house", "гильди", "зал ", "совет "
    },
    tavern = {
        "tavern", "inn", "corner club", "cornerclub", "club", "alehouse",
        -- "X's House of Y" is Morrowind's naming for a drinking establishment,
        -- not a residence. It has to be matched before the "house" bucket.
        "house of earthly", "house of delights", "house of",
        "таверн", "трактир", "постоял", "клуб", "корчм", "кабак", "пивн",
        "дом наслаждений", "дом удовольств"
    },
    shop = {
        "trader", "tradehouse", "trade house", "outfitter", "smith", "pawnbroker",
        "apothecary", "bookseller", "alchemist", "clothier", "enchanter",
        "shop", "store", "general goods", "provisioner", "merchant",
        "лавк", "торгов", "кузн", "аптек", "книготорг", "алхимик", "портн",
        "ростовщик", "магазин", "зачаров", "оружейн", "бронник", "товар"
    },
    house = {
        "house", "manor", "shack", "hut", "yurt", "farmhouse", "residence",
        "home", "quarters", "apartment", "villa", "cottage",
        "дом", "хижин", "лачуг", "юрт", "усадьб", "помест", "жилищ", "квартир",
        "особняк", "изб", "комнат"
    }
}

-- Модификаторы цены по ключевым словам названия. Множители применяются
-- последовательно, так что "Redoran Manor" дороже "Shack" без отдельной
-- таблицы для каждого сочетания.
local PRICE_MODIFIERS = {
    { patterns = { "manor", "villa", "особняк", "помест", "усадьб" }, factor = 2.5 },
    { patterns = { "shack", "hut", "yurt", "лачуг", "хижин", "юрт" }, factor = 0.5 },
    { patterns = { "quarters", "apartment", "комнат", "квартир" }, factor = 0.7 },
    { patterns = { "vivec", "вивек" }, factor = 1.4 },
    { patterns = { "balmora", "балмор" }, factor = 1.3 },
    { patterns = { "sadrith mora", "садрит мора" }, factor = 1.25 },
    { patterns = { "ald'ruhn", "альд'рун", "альд-рун" }, factor = 1.2 },
    { patterns = { "seyda neen", "сейда нин" }, factor = 0.8 },
    { patterns = { "pelagiad", "пелагиад" }, factor = 0.9 }
}

-- Lua's string.lower works one byte at a time, so it leaves every Cyrillic
-- letter untouched: "Дом Фарена" stayed "Дом Фарена" and never matched the
-- pattern "дом". Cell names come from the plugins as UTF-8, and Russian builds
-- translate every interior, so case folding has to cover the Cyrillic block or
-- half the world is invisible to the classifier.
--
-- Only the two-byte Cyrillic range is handled, which is all Morrowind cell
-- names ever contain:
--   А-П  D0 90..9F  ->  а-п  D0 B0..BF
--   Р-Я  D0 A0..AF  ->  р-я  D1 80..8F
--   Ё    D0 81      ->  ё    D1 91
local function lowerCyrillic(value)
    value = value:gsub("\208([\144-\159])", function(c) return "\208" .. string.char(c:byte() + 32) end)
    value = value:gsub("\208([\160-\175])", function(c) return "\209" .. string.char(c:byte() - 32) end)
    value = value:gsub("\208\129", "\209\145")
    return value
end

local function lower(value)
    return lowerCyrillic(string.lower(tostring(value or "")))
end

-- Общедоступно: тем же сравнением пользуется админский интерфейс, иначе фильтр
-- «в этой локации» промахивался бы мимо русских названий.
houseHelper.Lower = lower

local function containsAny(haystack, needles)
    for _, needle in ipairs(needles) do
        if string.find(haystack, needle, 1, true) ~= nil then
            return true, needle
        end
    end
    return false, nil
end

-- Возвращает тип объекта ("house"/"shop"/"tavern"/"guild") или nil.
-- Z006: экстерьер не может быть недвижимостью — ни автоматически, ни руками.
--
-- Раньше проверка жила внутри автоопределения, поэтому ручная регистрация
-- («добавить текущую» у администратора, стоящего на улице) проходила мимо неё,
-- и потом КАЖДЫЙ вход в эту клетку экстерьера встречал игрока сообщением
-- «этот дом продаётся», а дверь наружу подсказывала покупку. Теперь это одна
-- функция, и через неё проходят все входы в реестр.
--
-- Основной формат описания экстерьера в TES3MP — «-3, -2»; вариант
-- «Balmora (-3, -2)» встречается в описаниях некоторых сборок, поэтому
-- ловим оба.
function houseHelper.IsExteriorCell(cellDescription)
    if type(cellDescription) ~= "string" then return false end
    return cellDescription:match("^%s*%-?%d+%s*,%s*%-?%d+%s*$") ~= nil
        or cellDescription:match("%(%s*%-?%d+%s*,%s*%-?%d+%s*%)%s*$") ~= nil
end

function houseHelper.ClassifyCellName(cellDescription)
    if type(cellDescription) ~= "string" or cellDescription == "" then
        return nil
    end

    if houseHelper.IsExteriorCell(cellDescription) then
        return nil
    end

    local name = lower(cellDescription)

    -- Уже сгенерированный инстанс — не самостоятельный объект.
    if name:sub(-7) == " - home" or string.find(name, lower(cfg().legacyInstanceSuffix), 1, true) ~= nil then
        return nil
    end
    if string.find(name, " - instance for ", 1, true) ~= nil then
        return nil
    end

    if containsAny(name, EXCLUDE_PATTERNS) then
        return nil
    end

    -- Порядок важен: "Guild of Fighters Hall" должен стать гильдией, а не домом.
    for _, kind in ipairs({"guild", "tavern", "shop", "house"}) do
        if containsAny(name, KIND_PATTERNS[kind]) then
            return kind
        end
    end

    -- Классический морровиндский шаблон "Balmora, Ra'Virr" — имя NPC после
    -- запятой без всяких ключевых слов. Это почти всегда жилой интерьер, но
    -- уверенности мало, поэтому такие ячейки не регистрируются автоматически.
    return nil
end

function houseHelper.CalculatePrice(cellDescription, kind, skipInteriorProfile)
    local settings = cfg()
    local base = tonumber(settings.basePrice[kind]) or tonumber(settings.basePrice.house) or 8000
    local name = lower(cellDescription)

    local price = base
    for _, modifier in ipairs(PRICE_MODIFIERS) do
        if containsAny(name, modifier.patterns) then
            price = price * modifier.factor
        end
    end

    -- Price only server-known contents or an administrator-provided profile.
    -- Never accept a price or content count from an untrusted client message.
    if not skipInteriorProfile and config.houseDynamicPrices ~= false and houseHelper.GetInteriorProfile then
        local profile = houseHelper.GetInteriorProfile(cellDescription)
        local function count(value) return math.max(0, math.min(10000, tonumber(value) or 0)) end
        local factor = 1 + math.min(0.60, count(profile.objects) * 0.006)
            + math.min(0.45, count(profile.containers) * 0.03)
            + math.min(0.40, count(profile.beds) * 0.08)
            + math.min(0.60, math.max(0, count(profile.doors) - 1) * 0.08)
        price = price * factor
    end
    -- Округление до сотни, чтобы цены читались как цены, а не как результат
    -- умножения дробей.
    price = math.floor(price / 100 + 0.5) * 100
    if price < 100 then price = 100 end
    return price
end

-- ===========================================================================
-- ХРАНИЛИЩЕ
-- ===========================================================================
local store = nil
-- Derived topology only: prices, owners and permissions remain live.
local topologyIndex = nil

local function emptyStore()
    return {
        properties = {},
        -- Двери, назначение которых сервер уже видел. Ключ — описание ячейки
        -- назначения, значение — список ячеек, из которых туда ведёт дверь.
        -- Нужен, чтобы подсказка «здесь есть свободный дом» появлялась и в той
        -- ячейке, где игрок стоит перед входом.
        doors = {},
        -- H020: authoritative tombstones for authored (ESM/ESP) furniture that
        -- an owner removed from a private home. Cell JSON normally carries the
        -- same ObjectDelete, but generated instance reloads can rebuild that
        -- file from baseId. Keeping this tiny independent index means a removed
        -- fixture can never silently reappear after relog/re-entry.
        deletedFurniture = {},
        version = 2
    }
end

local function recoverPayment(data)
    local payment = data.pendingPayment
    if payment == nil then return true end
    if type(payment) ~= "table" or type(payment.file) ~= "string" or
        payment.file == "" or payment.file:find("[/\\\\]") or
        payment.file == "." or payment.file == ".." or type(payment.profile) ~= "table" then
        log(enumerations.log.ERROR, "Invalid housing payment journal; refusing to discard it")
        tes3mp.StopServer(2)
        return false
    end
    local writer = jsonInterface.saveAtomic or jsonInterface.save
    local ok, result = pcall(writer, "player/" .. payment.file, payment.profile, config.playerKeyOrder)
    if not ok or result == false then
        log(enumerations.log.ERROR, "Could not replay housing payment; journal preserved")
        tes3mp.StopServer(2)
        return false
    end
    data.pendingPayment = nil
    ok, result = pcall(writer, STORE_PATH, data)
    if not ok or result == false then
        data.pendingPayment = payment
        log(enumerations.log.ERROR, "Could not finalize housing payment; journal preserved")
        tes3mp.StopServer(2)
        return false
    end
    return true
end

local function loadStore()
    if store ~= nil then return store end

    local ok, loaded = pcall(jsonInterface.load, STORE_PATH)
    if ok and type(loaded) == "table" and type(loaded.properties) == "table" then
        loaded.doors = type(loaded.doors) == "table" and loaded.doors or {}
        loaded.deletedFurniture = type(loaded.deletedFurniture) == "table" and loaded.deletedFurniture or {}
        loaded.version = math.max(2, tonumber(loaded.version) or 1)
        if not recoverPayment(loaded) then error("Housing payment recovery failed") end
        store = loaded
        return store
    end

    store = emptyStore()
    pcall(jsonInterface.saveAtomic or jsonInterface.save, STORE_PATH, store)
    log(enumerations.log.INFO, "создан новый реестр недвижимости " .. STORE_PATH)
    return store
end

local saveScheduled = false

local function writeStore()
    local ok, result = pcall(jsonInterface.saveAtomic or jsonInterface.save, STORE_PATH, store)
    if not ok or result == false then
        log(enumerations.log.ERROR, "Could not save the housing registry: " .. tostring(result))
        return false
    end
    return true
end

local function saveStore(immediate)
    topologyIndex = nil
    if store == nil then return end
    if immediate then
        saveScheduled = not writeStore()
        return not saveScheduled
    end
    -- Реестр меняется редко, но покупка/продажа приходят пачками во время
    -- работы админского интерфейса. Одна запись за тик планировщика достаточна.
    saveScheduled = true
end

function houseHelper.FlushStore()
    if saveScheduled then
        saveScheduled = not writeStore()
    end
end

-- H020: persistent authored-furniture tombstones.  The normal cell packet list
-- remains the transport mechanism; this registry is an independent source of
-- truth used to reconstruct that packet when a generated home is rebuilt.
local function isAuthoredUniqueIndex(uniqueIndex)
    if type(uniqueIndex) ~= "string" then return false end
    local refNum, mpNum = uniqueIndex:match("^(%d+)%-(%d+)$")
    return tonumber(refNum) ~= nil and tonumber(refNum) > 0 and tonumber(mpNum) == 0
end

function houseHelper.RecordDeletedFurniture(instanceCell, uniqueIndex, refId)
    if type(instanceCell) ~= "string" or not houseHelper.IsInstanceCell(instanceCell) then return false end
    if not isAuthoredUniqueIndex(uniqueIndex) or type(refId) ~= "string" or refId == "" then return false end

    local data = loadStore()
    data.deletedFurniture = type(data.deletedFurniture) == "table" and data.deletedFurniture or {}
    local deleted = data.deletedFurniture[instanceCell]
    if type(deleted) ~= "table" then
        deleted = {}
        data.deletedFurniture[instanceCell] = deleted
    end

    if deleted[uniqueIndex] ~= refId then
        deleted[uniqueIndex] = refId
        saveStore(true)
        log(enumerations.log.INFO, "сохранено постоянное удаление мебели " ..
            uniqueIndex .. " (" .. refId .. ") в " .. instanceCell)
    end
    return true
end

function houseHelper.RestoreDeletedFurniture(cellOrDescription)
    local cell = cellOrDescription
    if type(cellOrDescription) == "string" then
        cell = LoadedCells and LoadedCells[cellOrDescription] or nil
    end
    if type(cell) ~= "table" or type(cell.description) ~= "string" or
        not houseHelper.IsInstanceCell(cell.description) or type(cell.data) ~= "table" then return false end

    local data = loadStore()
    local perCell = type(data.deletedFurniture) == "table" and data.deletedFurniture[cell.description] or nil
    if type(perCell) ~= "table" then return false end

    cell:EnsurePacketTables()
    cell.data.objectData = type(cell.data.objectData) == "table" and cell.data.objectData or {}
    local changed = false

    for uniqueIndex, refId in pairs(perCell) do
        if isAuthoredUniqueIndex(uniqueIndex) and type(refId) == "string" and refId ~= "" then
            local object = cell.data.objectData[uniqueIndex]
            local canonical = tableHelper.containsValue(cell.data.packets.delete, uniqueIndex) and
                type(object) == "table" and tostring(object.refId or ""):lower() == refId:lower()

            -- A tombstoned reference must belong to no packet other than delete.
            -- Stale container/move/state packets are exactly what can resurrect
            -- an inherited base object after an instance has been reconstructed.
            if canonical then
                for packetName, packet in pairs(cell.data.packets) do
                    if packetName ~= "delete" and type(packet) == "table" and
                        tableHelper.containsValue(packet, uniqueIndex) then
                        canonical = false
                        break
                    end
                end
            end

            if not canonical then
                cell:DeleteObjectData(uniqueIndex)
                -- DeleteObjectData returns early if the objectData stub was
                -- already lost, so remove stale packet memberships explicitly.
                for _, packet in pairs(cell.data.packets) do
                    if type(packet) == "table" then
                        tableHelper.removeValue(packet, uniqueIndex)
                    end
                end
                tableHelper.insertValueIfMissing(cell.data.packets.delete, uniqueIndex)
                cell:InitializeObjectData(uniqueIndex, refId)
                changed = true
            end
        end
    end

    if changed then
        if type(cell.QuicksaveToDrive) == "function" then cell:QuicksaveToDrive()
        elseif type(cell.Save) == "function" then cell:Save() end
        log(enumerations.log.INFO, "восстановлены постоянные удаления мебели в " .. cell.description)
    end
    return changed
end

-- ===========================================================================
-- ДОСТУП К ОБЪЕКТАМ
-- ===========================================================================
local function normalizeProperty(cellDescription, property)
    property.cell = property.cell or cellDescription
    property.kind = property.kind or "house"
    property.cells = type(property.cells) == "table" and property.cells or {property.cell}
    property.instanceNaming = property.instanceNaming or
        ((property.owner and property.owner ~= "") and "legacy" or "home")
    property.title = property.title or property.cell
    property.allowTake = property.allowTake == true
    property.mode = property.mode == "original" and "original" or "instance"
    property.enabled = property.enabled ~= false
    property.locked = property.locked == true
    property.coOwners = type(property.coOwners) == "table" and property.coOwners or {}
    property.friends = type(property.friends) == "table" and property.friends or {}
    property.price = tonumber(property.price) or houseHelper.CalculatePrice(cellDescription, property.kind)
    return property
end

-- Index authored room membership and reverse door connections once per registry
-- revision. Never normalize every property here: missing prices can trigger I/O.
local function getTopologyIndex()
    if topologyIndex then return topologyIndex end
    local data = loadStore()
    local index = {members = {}, doorsFrom = {}}
    for key, raw in pairs(data.properties) do
        local cells = type(raw.cells) == "table" and raw.cells or {raw.cell or key}
        for _, member in ipairs(cells) do
            local name = lower(member)
            local bucket = index.members[name]
            if not bucket then bucket = {}; index.members[name] = bucket end
            bucket[#bucket + 1] = key
        end
    end
    for destination, origins in pairs(data.doors) do
        if type(origins) == "table" then
            for _, origin in pairs(origins) do
                local name = lower(tostring(origin))
                local bucket = index.doorsFrom[name]
                if not bucket then bucket = {}; index.doorsFrom[name] = bucket end
                bucket[lower(destination)] = true
            end
        end
    end
    topologyIndex = index
    return index
end

function houseHelper.GetProperty(cellDescription)
    if type(cellDescription) ~= "string" or cellDescription == "" then return nil end
    local data = loadStore()
    -- Preserve exact-key precedence and the old pairs-order for overlapping rooms.
    local key = cellDescription
    if data.properties[key] == nil then
        local matches = getTopologyIndex().members[lower(cellDescription)]
        key = matches and matches[1]
    end
    if key then
        local property = data.properties[key]
        if property then return normalizeProperty(key, property) end
    end
    return nil
end

-- Optional exact profiles can be supplied by the operator in config or JSON.
-- Without one, this is a conservative estimate from persisted cell state:
-- vanilla references the server has never received cannot be counted.
local interiorProfiles
function houseHelper.GetInteriorProfile(cellDescription)
    local explicit = type(config.houseInteriorProfiles) == "table" and config.houseInteriorProfiles[cellDescription]
    if type(explicit) == "table" then return explicit end
    if interiorProfiles == nil then
        local ok, data = pcall(jsonInterface.load, "custom/houseInteriorProfiles.json")
        interiorProfiles = ok and type(data) == "table" and data.profiles or {}
        if type(interiorProfiles) ~= "table" then interiorProfiles = {} end
    end
    if type(interiorProfiles[cellDescription]) == "table" then return interiorProfiles[cellDescription] end
    local cell = LoadedCells and LoadedCells[cellDescription]
    local data = cell and cell.data
    if not data then
        local ok, saved = pcall(jsonInterface.load, "cell/" .. fileHelper.fixFilename(cellDescription) .. ".json")
        if ok and type(saved) == "table" then data = saved end
    end
    local profile = {objects=0, containers=0, beds=0, doors=0, source="server_observed"}
    for index, object in pairs(data and data.objectData or {}) do
        if type(object) == "table" and not object.cellChangeTo and
            not tableHelper.containsValue(data.packets and data.packets.delete or {}, index) and
            not tableHelper.containsValue(data.packets and data.packets.place or {}, index) and
            not tableHelper.containsValue(data.packets and data.packets.spawn or {}, index) then
            local ref = lower(object.refId or "")
            if ref:find("door",1,true) or object.doorState ~= nil or object.teleport ~= nil then
                profile.doors = profile.doors + 1
            elseif not object.statsDynamic and not object.ai and not object.deathState then
                profile.objects = profile.objects + 1
                if object.inventory then profile.containers = profile.containers + 1 end
                if ref:find("bed",1,true) then profile.beds = profile.beds + 1 end
            end
        end
    end
    -- A known connection proves at least one door, not the exact door count.
    local neighbours = {}
    local doors = loadStore().doors
    for destination, origins in pairs(doors) do
        for _, origin in pairs(origins) do
            if lower(destination) == lower(cellDescription) then neighbours[origin]=true end
            if lower(origin) == lower(cellDescription) then neighbours[destination]=true end
        end
    end
    local connections=0; for _ in pairs(neighbours) do connections=connections+1 end
    profile.doors=math.max(profile.doors,connections)
    return profile
end

-- Зарегистрировать ячейку. kind = nil означает «определить по названию».
-- Возвращает property или nil, если ячейка не подходит.
function houseHelper.Register(cellDescription, kind, price, auto, pid, skipInteriorProfile)
    if type(cellDescription) ~= "string" or cellDescription == "" then return nil end
    -- Z006: явно указанный тип НЕ отменяет эту проверку.
    if houseHelper.IsExteriorCell(cellDescription) then return nil end

    local data = loadStore()
    local existing = houseHelper.GetProperty(cellDescription)
    if existing ~= nil then
        return normalizeProperty(cellDescription, existing)
    end

    kind = kind or houseHelper.ClassifyCellName(cellDescription)
    if kind == nil then return nil end
    if auto == true and not houseHelper.CanAutoGenerate(pid, kind) then return nil end

    local property = normalizeProperty(cellDescription, {
        cell = cellDescription,
        kind = kind,
        price = tonumber(price) or houseHelper.CalculatePrice(cellDescription, kind, skipInteriorProfile),
        owner = nil,
        locked = false,
        coOwners = {},
        friends = {},
        enabled = true,
        autoDetected = auto == true,
        priceMode = price ~= nil and "manual" or "auto",
        registeredAt = os.time()
    })

    data.properties[cellDescription] = property
    saveStore()
    log(enumerations.log.INFO, "зарегистрирован объект: " .. cellDescription ..
        " (" .. kind .. ", " .. tostring(property.price) .. ")")
    return property
end

-- Полный реестр. Возвращает нормализованные копии, пригодные для чтения,
-- но изменения через них НЕ сохраняются — для правок есть Set*-функции ниже.
function houseHelper.AllProperties()
    local result = {}
    for cellDescription, raw in pairs(loadStore().properties) do
        result[cellDescription] = normalizeProperty(cellDescription, raw)
    end
    return result
end

-- ---------------------------------------------------------------------------
-- Точечные изменения из админского интерфейса. Каждая функция трогает ровно
-- одно поле: так изменение цены не может случайно сбросить список друзей.
-- ---------------------------------------------------------------------------
function houseHelper.SetPrice(cellDescription, price)
    local property = houseHelper.GetProperty(cellDescription)
    if property == nil then return false end
    property.price = math.max(0, math.floor(tonumber(price) or 0))
    property.priceMode = "manual"
    saveStore()
    return true
end

function houseHelper.RecalculatePrice(cellDescription)
    local property = houseHelper.GetProperty(cellDescription)
    if not property or (property.owner and property.owner ~= "") then return false end
    local total = 0
    for _, member in ipairs(property.cells) do
        total = total + houseHelper.CalculatePrice(member, property.kind)
    end
    property.price, property.priceMode = total, "auto"
    saveStore(true)
    houseHelper.RefreshAll()
    return true
end

function houseHelper.SetKind(cellDescription, kind)
    local property = houseHelper.GetProperty(cellDescription)
    if property == nil then return false end
    if kind ~= "house" and kind ~= "shop" and kind ~= "tavern" and kind ~= "guild" then
        return false
    end
    local wasAutomaticPrice = property.priceMode == "auto" or
        (property.priceMode == nil and property.price == houseHelper.CalculatePrice(cellDescription, property.kind))
    property.kind = kind
    -- Цену, выставленную вручную, смена типа не трогает. Автоматическую —
    -- пересчитывает, иначе «дом за 8000» остался бы таверной за 8000.
    if wasAutomaticPrice then
        property.price = houseHelper.CalculatePrice(cellDescription, kind)
    end
    saveStore()
    return true
end

-- Mode changes are only safe before purchase: no implicit migration or loss
-- of furniture from an existing private copy.
function houseHelper.SetMode(cellDescription, mode)
    local property = houseHelper.GetProperty(cellDescription)
    if not property or (mode ~= "original" and mode ~= "instance") then return false end
    if property.mode == mode then return true end
    if property.owner and property.owner ~= "" then return false end
    property.mode = mode
    saveStore(true)
    houseHelper.RefreshAll()
    return true
end

function houseHelper.SetEnabled(cellDescription, enabled)
    local property = houseHelper.GetProperty(cellDescription)
    if property == nil then return false end
    property.enabled = enabled == true
    saveStore()
    return true
end

-- Назначение владельца администратором. Прежний владелец теряет объект вместе
-- со своей личной копией — иначе на диске остался бы инстанс, до которого уже
-- никто не может дойти.
function houseHelper.SetOwner(cellDescription, ownerName)
    local property = houseHelper.GetProperty(cellDescription)
    if property == nil then return false end
    if type(ownerName) ~= "string" or ownerName == "" then
        return houseHelper.Release(cellDescription, "owner cleared by admin")
    end

    if lower(property.owner or "") == lower(ownerName) then return true end
    for _, owned in ipairs(houseHelper.ListOwnedBy(ownerName)) do
        if owned ~= property then return false end
    end
    if type(property.owner) == "string" and property.owner ~= ""
        and lower(property.owner) ~= lower(ownerName) then
        for _, member in ipairs(property.cells) do
            if property.mode ~= "original" then
                houseHelper.ResetInstance(houseHelper.GetInstanceDescription(member, property.owner))
            end
        end
    end

    property.owner = ownerName
    property.instanceOwner = ownerName
    property.instanceNaming = "home"
    property.coOwners = {}
    property.friends = {}
    property.boughtAt = property.boughtAt or os.time()
    saveStore()

    local instanceCell = houseHelper.GetInstanceDescription(cellDescription, ownerName)
    houseHelper.EnsureInstanceRecord(nil, instanceCell, cellDescription)
    houseHelper.MarkForPurge(instanceCell)
    return true
end

function houseHelper.Unregister(cellDescription)
    local data = loadStore()
    if data.properties[cellDescription] == nil then return false end
    data.properties[cellDescription] = nil
    saveStore(true)
    return true
end

-- Ленивая автоматическая регистрация: вызывается каждый раз, когда сервер
-- вообще узнаёт об интерьере. Ничего не делает, если ячейка уже в реестре или
-- не похожа на недвижимость.
function houseHelper.CanAutoGenerate(pid, kind)
    local settings = cfg()
    if not settings.enabled then return false end
    if settings.generationMode == "admins" then
        if not houseHelper.IsStaff(pid) then return false end
    elseif settings.generationMode ~= "players" then return false end
    local keys = {house="houseGenerateHouses", tavern="houseGenerateTaverns",
        guild="houseGenerateGuilds", shop="houseGenerateShops"}
    local key = keys[kind]
    if not key then return false end
    if config[key] ~= nil then return config[key] == true end
    -- Compatibility for Z014 previews with the old cumulative selector.
    local level = settings.generationLevel
    return kind == "house" or
        (kind == "tavern" and (level == "houses_taverns" or level == "houses_taverns_guilds")) or
        (kind == "guild" and level == "houses_taverns_guilds")
end

function houseHelper.Observe(cellDescription, pid)
    if not cfg().enabled then return nil end
    local existing = houseHelper.GetProperty(cellDescription)
    if existing ~= nil then return existing end
    return houseHelper.Register(cellDescription, nil, nil, true, pid)
end

-- ===========================================================================
-- КОНТРАКТ ДЛЯ resetHelrer.lua
-- ===========================================================================
--
-- resetHelper вызывает эти три метода, чтобы не сбрасывать жилые ячейки по
-- общему таймеру. Сигнатуры зафиксированы, менять их нельзя.
function houseHelper.GetCellData(cellDescription)
    local property = houseHelper.GetProperty(cellDescription)
    if property == nil then
        -- Инстанс дома тоже должен считаться домом: его сбрасывает только
        -- владелец или админ, но не часовой таймер.
        local base = houseHelper.GetBaseCellForInstance(cellDescription)
        if base ~= nil then
            local home = houseHelper.GetProperty(base)
            return { house = home and home.cell or base, instance = true, owner = home and home.owner }
        end
        return nil
    end
    -- Z016: the registered base of an instanced property is a public cell,
    -- not the player's home. Only mode=original protects the base itself
    -- (including all linked rooms). Instance copies are handled above.
    if property.mode ~= "original" then return nil end
    return { house = property.cell, kind = property.kind, owner = property.owner }
end

function houseHelper.IsHouseCell(cellDescription)
    return houseHelper.GetCellData(cellDescription) ~= nil
end

function houseHelper.GetHouseOwnerName(houseName)
    local property = houseHelper.GetProperty(houseName)
    if property == nil then return nil end
    local owner = property.owner
    if type(owner) ~= "string" or owner == "" then return nil end
    return owner
end

-- ===========================================================================
-- ИНСТАНСЫ
-- ===========================================================================
function houseHelper.GetInstanceDescription(cellDescription, ownerName)
    if type(cellDescription) ~= "string" or cellDescription == "" then return nil end
    if type(ownerName) ~= "string" or ownerName == "" then return nil end
    local property = houseHelper.GetProperty(cellDescription)
    if property and property.mode == "original" then return cellDescription end
    if property and property.instanceNaming == "legacy" then
        return cellDescription .. cfg().legacyInstanceSuffix .. (property.instanceOwner or ownerName)
    end
    return cellDescription .. " - home"
end

-- Разобрать сгенерированное описание обратно на базовую ячейку и владельца.
function houseHelper.GetBaseCellForInstance(cellDescription)
    if type(cellDescription) ~= "string" then return nil end
    if cellDescription:sub(-7):lower() == " - home" then
        return cellDescription:sub(1, -8)
    end
    local suffix = cfg().legacyInstanceSuffix
    local start = string.find(cellDescription, suffix, 1, true)
    if start == nil then return nil end
    return string.sub(cellDescription, 1, start - 1)
end

function houseHelper.GetInstanceOwner(cellDescription)
    local base = houseHelper.GetBaseCellForInstance(cellDescription)
    local property = base and houseHelper.GetProperty(base)
    return property and property.owner or nil
end

function houseHelper.IsInstanceCell(cellDescription)
    return houseHelper.GetBaseCellForInstance(cellDescription) ~= nil
end

-- Z012: кому из игроков запись инстанса уже уехала в этой сессии.
--
-- Запись CELL с baseId — это не «описание» ячейки, а инструкция клиенту
-- СОБРАТЬ её заново из оригинального интерьера. Если слать её на каждый вход,
-- клиент каждый раз выбрасывает всё, что игрок успел сделать внутри: вещи с
-- пола, разграбленные контейнеры, удалённых NPC. Одного раза за сессию
-- достаточно: клиент помнит запись до выхода из игры, а таблица чистится в
-- OnPlayerDisconnect.
local instanceRecordSent = {}

local function shouldSendInstanceRecord(pid, instanceCell)
    if pid == nil then return false end
    local sent = instanceRecordSent[pid]
    if sent == nil then
        sent = {}
        instanceRecordSent[pid] = sent
    end
    if sent[instanceCell] == true then return false end
    sent[instanceCell] = true
    return true
end

function houseHelper.ForgetInstanceRecords(pid)
    instanceRecordSent[pid] = nil
end

-- Создать/подтвердить динамическую CELL-запись инстанса и отдать её клиенту.
--
-- Запись постоянная (permanentRecords), поэтому переживает рестарт сервера и
-- не зависит от того, кто первым зашёл в ячейку. baseId заставляет клиент
-- собрать инстанс из содержимого оригинального интерьера — именно поэтому
-- ячейки «не надо переделывать под дома».
function houseHelper.EnsureInstanceRecord(pid, instanceCell, baseCell)
    local home = houseHelper.GetProperty(baseCell)
    if home and home.mode == "original" then return true end
    if type(instanceCell) ~= "string" or instanceCell == "" then return false end
    if type(baseCell) ~= "string" or baseCell == "" then return false end

    local recordStore = RecordStores and RecordStores["cell"]
    if recordStore == nil then
        log(enumerations.log.ERROR, "RecordStores[\"cell\"] недоступен; инстанс не создан")
        return false
    end

    local recordTable = { baseId = baseCell }
    if recordStore.data.permanentRecords[instanceCell] == nil then
        recordStore.data.permanentRecords[instanceCell] = recordTable
        recordStore:Save()
        log(enumerations.log.INFO, "создана запись инстанса " .. instanceCell)
    end

    -- Z012: запись уходит получателю один раз за сессию, а не на каждый вход.
    -- Переподключившийся клиент про неё не знает — поэтому счётчик привязан к
    -- сессии, а не к факту создания записи.
    if shouldSendInstanceRecord(pid, instanceCell) then
        tes3mp.ClearRecords()
        tes3mp.SetRecordType(enumerations.recordType["CELL"])
        packetBuilder.AddCellRecord(instanceCell, recordTable)
        tes3mp.SendRecordDynamic(pid, false, false)
    end

    return true
end

-- ===========================================================================
-- ОЧИСТКА ИНСТАНСА
-- ---------------------------------------------------------------------------
-- Инстанс наследует содержимое оригинальной ячейки — вместе с её хозяином,
-- слугами и разложенным по полкам хламом. В личном доме их быть не должно.
--
-- Сервер не может перечислить статическое содержимое ячейки: он знает только
-- то, о чём ему рассказал клиент. Поэтому уборка двусторонняя:
--
--   * NPC и существ сервер удаляет сам, как только клиент прислал список
--     актёров ячейки (OnActorList).
--   * Предметы вычищает клиент: сервер помечает ячейку как «инстанс под
--     уборку» управляющим сообщением, клиент проходит по содержимому ячейки и
--     присылает обычный ObjectDelete. Удаление после этого хранится в файле
--     ячейки инстанса и действует для совладельцев тоже.
--
-- Отметка одноразовая: после первой уборки ячейка помнит, что она уже чистая.
-- ===========================================================================
local function instanceProperty(cell)
    local base = houseHelper.GetBaseCellForInstance(cell)
    local property = base and houseHelper.GetProperty(base) or houseHelper.GetProperty(cell)
    if property and property.mode == "original" then
        if not base and property.owner and property.owner ~= "" then return property end
        return nil
    end
    if property and property.owner and property.owner ~= "" and
        houseHelper.GetInstanceDescription(base, property.owner) == cell then return property end
end


local pendingPurge = {}
-- A purge is complete only after BOTH the local loose-object sweep and the
-- authoritative full-container snapshot have been received. This prevents
-- acknowledging an empty room while its original ESM chests still have loot.
local pendingPurgeAcknowledged = {}
local preparedContainerCells = {}
local purgeDeletes = {}
local repairingPurge = {}
local PURGE_VERSION = 2

function houseHelper.MarkForPurge(instanceCell)
    if not houseHelper.IsInstanceCell(instanceCell) then return false end
    local data = loadStore()
    data.purged = type(data.purged) == "table" and data.purged or {}
    if data.purged[instanceCell] == PURGE_VERSION then return false end
    if not pendingPurge[instanceCell] then
        purgeDeletes[instanceCell] = {}
        repairingPurge[instanceCell] = data.purged[instanceCell] == true
    end
    pendingPurge[instanceCell] = true
    return true
end

-- Called by BaseCell:SaveContainers immediately after reading the first full
-- snapshot but BEFORE it sends that snapshot back to any client. An instanced
-- room's original/levelled container contents are never merchandise belonging
-- to the new homeowner. The original, public cell is never changed.
function houseHelper.PreparePurchasedContainers(cell, action, subAction)
    if type(cell) ~= "table" or not pendingPurge[cell.description] or
        preparedContainerCells[cell.description] then return false end
    if action ~= enumerations.container.SET or
        subAction ~= enumerations.containerSub.REPLY_TO_REQUEST then return false end
    local base = houseHelper.GetBaseCellForInstance(cell.description)
    local property = base and houseHelper.GetProperty(base)
    if not property or not property.owner or property.mode == "original" then return false end
    for _, uniqueIndex in pairs(cell.data.packets.container or {}) do
        local object = cell.data.objectData[uniqueIndex]
        if uniqueIndex:match("^%d+%-0$") and type(object) == "table" and type(object.inventory) == "table" then
            if repairingPurge[cell.description] and next(object.inventory) ~= nil then
                -- Old clients could mark a cluttered home clean. Preserve its
                -- original-container contents for operator recovery during repair.
                cell.data.housePurgeBackup = cell.data.housePurgeBackup or {}
                if cell.data.housePurgeBackup[uniqueIndex] == nil then
                    cell.data.housePurgeBackup[uniqueIndex] = tableHelper.deepCopy(object.inventory)
                end
            end
            object.inventory = {}
        end
    end
    preparedContainerCells[cell.description] = true
    log(enumerations.log.INFO, "initial containers emptied in purchased home " .. cell.description)
    -- Caller's existing AOI SET loop now publishes only the emptied snapshots.
    return true
end

function houseHelper.IsPurgeDelete(pid, cell, index, object)
    if not pendingPurge[cell] then return false end
    local property = instanceProperty(cell)
    return pendingPurge[cell] == true and property ~= nil and
        houseHelper.GetAccessLevel(pid, property) ~= "none" and tes3mp.GetCell(pid) == cell and
        type(index) == "string" and index:match("^[1-9]%d*%-0$") ~= nil
end

function houseHelper.AcknowledgePurge(instanceCell, pid, count)
    if not pendingPurge[instanceCell] then return false end
    local property = instanceProperty(instanceCell)
    if not property or houseHelper.GetAccessLevel(pid, property) == "none" then return false end
    -- The acknowledgement can arrive before ObjectDelete on another channel.
    -- Finish only once the server has accepted the reported number of deletions.
    count = tonumber(count)
    if not count or count < 0 or count ~= math.floor(count) then return false end
    pendingPurgeAcknowledged[instanceCell] = {pid=pid, count=count}

    -- The decoration sweep and the container handshake are two independent
    -- streams.  A client can finish removing loose authored objects before the
    -- server has ever requested the authored containers of a freshly generated
    -- instance.  Explicitly request that snapshot here instead of leaving the
    -- house permanently stuck in "preparing".
    local cell = LoadedCells and LoadedCells[instanceCell]
    if cell and pid ~= nil then
        if cell:HasFullContainerData() then
            if houseHelper.PreparePurchasedContainers(cell, enumerations.container.SET,
                enumerations.containerSub.REPLY_TO_REQUEST) then
                cell:Save()
                for _, visitor in pairs(cell.visitors or {}) do
                    cell:LoadContainers(visitor, cell.data.objectData, cell.data.packets.container or {})
                end
            end
        else
            cell:EnsureFullContainerData(pid)
        end
    end

    return houseHelper.TryFinalizePurge(instanceCell)
end

function houseHelper.TryFinalizePurge(instanceCell)
    if not pendingPurge[instanceCell] or not pendingPurgeAcknowledged[instanceCell] or
        not preparedContainerCells[instanceCell] then return false end
    local ack = pendingPurgeAcknowledged[instanceCell]
    local accepted = purgeDeletes[instanceCell] and purgeDeletes[instanceCell][ack.pid] or {}
    local count = 0
    for _ in pairs(accepted) do count = count + 1 end
    if count < ack.count then return false end
    local cell = LoadedCells and LoadedCells[instanceCell]
    if not cell or not cell:HasFullContainerData() then return false end
    cell:Save()
    houseHelper.MarkPurgeDone(instanceCell)
    return true
end

function houseHelper.IsPurgePending(instanceCell)
    return pendingPurge[instanceCell] == true
end

-- Furniture editing may start as soon as the client has positively completed
-- the loose-object purge.  Container interaction remains protected by the
-- OnContainer validator until the authoritative container snapshot arrives.
-- Keeping those two gates separate prevents a missed container reply from
-- disabling placement/deletion for the entire session.
function houseHelper.IsPurgeBlockingFurniture(instanceCell)
    return pendingPurge[instanceCell] == true and not pendingPurgeAcknowledged[instanceCell]
end

function houseHelper.MarkPurgeDone(instanceCell)
    local data = loadStore()
    data.purged = type(data.purged) == "table" and data.purged or {}
    data.purged[instanceCell] = PURGE_VERSION
    pendingPurge[instanceCell] = nil
    pendingPurgeAcknowledged[instanceCell] = nil
    preparedContainerCells[instanceCell] = nil
    purgeDeletes[instanceCell] = nil
    repairingPurge[instanceCell] = nil
    saveStore(true)
end

-- Удалить всех актёров инстанса. Вызывается из обработчика OnActorList.
function houseHelper.PurgeActors(pid, cellDescription)
    if not houseHelper.IsInstanceCell(cellDescription) then return 0 end
    local cell = LoadedCells[cellDescription]
    if cell == nil then return 0 end

    local removed = 0
    local objectData = cell.data.objectData or {}
    local actorList = cell.data.packets and cell.data.packets.actorList or {}

    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(cellDescription)

    for _, uniqueIndex in ipairs(actorList) do
        local object = objectData[uniqueIndex]
        if type(object) == "table" and type(object.refId) == "string" then
            local splitIndex = uniqueIndex:split("-")
            tes3mp.SetObjectRefNum(tonumber(splitIndex[1]) or 0)
            tes3mp.SetObjectMpNum(tonumber(splitIndex[2]) or 0)
            tes3mp.SetObjectRefId(object.refId)
            tes3mp.AddObject()
            removed = removed + 1
        end
    end

    if removed > 0 then
        tes3mp.SendObjectDelete(true, false)
        for _, uniqueIndex in ipairs(actorList) do
            objectData[uniqueIndex] = nil
            if cell.data.packets ~= nil then
                for _, packetName in ipairs({"actorList", "position", "statsDynamic", "equipment", "ai", "death", "spellsActive"}) do
                    local list = cell.data.packets[packetName]
                    if type(list) == "table" then
                        tableHelper.removeValue(list, uniqueIndex)
                    end
                end
            end
        end
        cell.data.loadState.hasFullActorList = false
        cell:Save()
        log(enumerations.log.INFO, "из инстанса " .. cellDescription .. " удалено актёров: " .. removed)
    end

    return removed
end

-- ===========================================================================
-- Y089: BEDROLL DUPLICATION REPAIR
-- ---------------------------------------------------------------------------
-- Some content packs implement a portable bedroll with a two-object handoff:
-- a dropped flg_bedroll01misc runs FLGBedrollMiscScript, disables itself and
-- places flg_bedroll01acti at the same transform.  The disabled misc object is
-- therefore only a transient script source and must never be replayed later.
--
-- Home instances intentionally replay persistent placed objects on re-entry.
-- Replaying one of those already-disabled misc sources creates it enabled for
-- a moment before ObjectState arrives, reruns FLGBedrollMiscScript and places a
-- fresh activator.  Repeating that cycle produces geometric piles of bedrolls.
--
-- Repair strategy:
--   * permanently discard disabled dynamic flg_bedroll01misc sources;
--   * collapse only dynamic flg_bedroll01acti objects whose full transform is
--     effectively identical, keeping the oldest mpNum;
--   * broadcast deletes so clients that already cached the cell are repaired
--     immediately as well as the persistent cell file.
-- Different bedrolls at different transforms are intentionally preserved.
-- ===========================================================================
local BEDROLL_MISC_REF = "flg_bedroll01misc"
local BEDROLL_ACTI_REF = "flg_bedroll01acti"

local function dynamicMpNum(uniqueIndex)
    local refNum, mpNum = tostring(uniqueIndex or ""):match("^(%d+)%-(%d+)$")
    if tonumber(refNum) ~= 0 then return nil end
    return tonumber(mpNum)
end

local function quantize(value, scale)
    value = tonumber(value)
    if value == nil then return nil end
    return math.floor(value * scale + 0.5)
end

local function bedrollTransformKey(object)
    local location = type(object) == "table" and object.location or nil
    if type(location) ~= "table" then return nil end

    -- Position: 0.5-unit buckets. Rotation: 0.01-radian buckets. This absorbs
    -- harmless float roundoff while remaining far tighter than any deliberate
    -- placement difference a player can perceive/use.
    local values = {
        quantize(location.posX, 2), quantize(location.posY, 2), quantize(location.posZ, 2),
        quantize(location.rotX, 100), quantize(location.rotY, 100), quantize(location.rotZ, 100)
    }
    for _, value in ipairs(values) do
        if value == nil then return nil end
    end
    return table.concat(values, ":")
end

function houseHelper.RepairBedrollArtifacts(cell, pid)
    if type(cell) ~= "table" or type(cell.description) ~= "string" or
        not houseHelper.IsInstanceCell(cell.description) or type(cell.data) ~= "table" or
        type(cell.data.objectData) ~= "table" then
        return 0, 0
    end

    if type(cell.EnsurePacketTables) == "function" then cell:EnsurePacketTables() end
    if type(cell.EnsurePacketValidity) == "function" then cell:EnsurePacketValidity() end

    local hiddenSources = {}
    local groups = {}

    for uniqueIndex, object in pairs(cell.data.objectData) do
        local refId = type(object) == "table" and lower(object.refId) or ""
        if dynamicMpNum(uniqueIndex) ~= nil then
            if refId == BEDROLL_MISC_REF and object.state == false then
                table.insert(hiddenSources, uniqueIndex)
            elseif refId == BEDROLL_ACTI_REF then
                local key = bedrollTransformKey(object)
                if key ~= nil then
                    if groups[key] == nil then groups[key] = {} end
                    table.insert(groups[key], uniqueIndex)
                end
            end
        end
    end

    local duplicates = {}
    for _, indexes in pairs(groups) do
        if #indexes > 1 then
            table.sort(indexes, function(a, b)
                return (dynamicMpNum(a) or math.huge) < (dynamicMpNum(b) or math.huge)
            end)
            -- Keep the oldest activator; every later object at the exact same
            -- transform is a replay-generated copy.
            for index = 2, #indexes do
                table.insert(duplicates, indexes[index])
            end
        end
    end

    if #hiddenSources == 0 and #duplicates == 0 then return 0, 0 end

    local removals = {}
    for _, uniqueIndex in ipairs(hiddenSources) do table.insert(removals, uniqueIndex) end
    for _, uniqueIndex in ipairs(duplicates) do table.insert(removals, uniqueIndex) end

    -- Remove first from persistent packet membership so LoadInitialCellData can
    -- never replay them even if a delete packet below is ignored by a client.
    for _, uniqueIndex in ipairs(removals) do
        if cell.data.objectData[uniqueIndex] ~= nil then
            cell:DeleteObjectData(uniqueIndex)
        end
    end

    if type(cell.QuicksaveToDrive) == "function" then cell:QuicksaveToDrive() end

    -- Heal clients that still have the instance cached from an earlier visit.
    -- Sending a delete for an unknown dynamic ref is harmless; sending it for
    -- cached duplicates makes the migration visible immediately.
    if pid ~= nil and Players[pid] ~= nil and #removals > 0 then
        tes3mp.ClearObjectList()
        tes3mp.SetObjectListPid(pid)
        tes3mp.SetObjectListCell(cell.description)
        local batchCount = 0
        for _, uniqueIndex in ipairs(removals) do
            packetBuilder.AddObjectDelete(uniqueIndex, {})
            batchCount = batchCount + 1
            if batchCount >= 3000 then
                tes3mp.SendObjectDelete(true, false)
                tes3mp.ClearObjectList()
                tes3mp.SetObjectListPid(pid)
                tes3mp.SetObjectListCell(cell.description)
                batchCount = 0
            end
        end
        if batchCount > 0 then tes3mp.SendObjectDelete(true, false) end
    end

    log(enumerations.log.WARN, "bedroll repair in " .. cell.description ..
        ": removed hidden sources=" .. #hiddenSources ..
        ", duplicate activators=" .. #duplicates)
    return #hiddenSources, #duplicates
end

-- ===========================================================================
-- Z012: ПОВТОРНАЯ ВЫДАЧА СОДЕРЖИМОГО ИНСТАНСА
-- ---------------------------------------------------------------------------
-- Ядро отдаёт полные данные ячейки только при ПЕРВОМ визите игрока за сессию.
-- BaseCell:AddVisitor сравнивает lastVisit[аккаунт] с initTimestamp игрока, а
-- BaseCell:RemoveVisitor при выходе записывает в lastVisit текущее время —
-- значит, на второй вход в ту же ячейку не отправляется ничего, кроме
-- позиций актёров (LoadMomentaryCellData).
--
-- Для обычного интерьера этого хватает: состояние ячейки держит сам клиент.
-- Инстанс же клиент собирает из baseId заново при каждой загрузке — назад
-- возвращаются NPC, заново катается plx_stat_randomiser, снова наполняются
-- rnd-контейнеры, а положенные игроком вещи исчезают. Сервер при этом всё
-- помнит (в файле ячейки предметы на месте) — он просто молчит.
--
-- Поэтому на каждый вход в инстанс данные отдаются заново. Первый визит за
-- сессию пропускается: там уже отработал AddVisitor.
-- ===========================================================================
local INSTANCE_REFRESH_RETRY_MS = 500
local INSTANCE_REFRESH_MAX_ATTEMPTS = 6

-- Возвращает true, если делать больше нечего (отправили или отправлять некому),
-- и false, если ячейка ещё не загружена и стоит попробовать позже.
local function sendInstanceCellData(pid, cellDescription)
    if Players[pid] == nil or not Players[pid]:IsLoggedIn() then return true end
    -- Игрок мог уйти, пока мы ждали загрузки ячейки.
    if tes3mp.GetCell(pid) ~= cellDescription then return true end

    local cell = LoadedCells ~= nil and LoadedCells[cellDescription] or nil
    if cell == nil or type(cell.data) ~= "table" then return false end
    if type(cell.LoadInitialCellData) ~= "function" then return true end

    -- Y100: OnCellLoad may already have delivered this visit. Comparing two
    -- unrelated login timestamps replayed the entire library a second time.
    -- Wait for AddVisitor/records before sending, and reset on RemoveVisitor.
    if not tableHelper.containsValue(cell.visitors or {},pid) then return false end
    if cell.initialDataSentTo and cell.initialDataSentTo[pid]==Players[pid] then return true end

    cell:LoadInitialCellData(pid)
    log(enumerations.log.INFO, "повторная выдача данных инстанса " .. cellDescription ..
        " для " .. tostring(Players[pid].accountName))
    return true
end

local function refreshInstanceCell(pid, cellDescription, attempt)
    if sendInstanceCellData(pid, cellDescription) then return end

    attempt = tonumber(attempt) or 1
    if attempt >= INSTANCE_REFRESH_MAX_ATTEMPTS then
        log(enumerations.log.WARN, "ячейка инстанса " .. cellDescription ..
            " не загрузилась; содержимое не отправлено")
        return
    end

    tes3mp.StartTimer(tes3mp.CreateTimerEx("HouseHelper_InstanceRefreshTimer",
        INSTANCE_REFRESH_RETRY_MS, "isi", pid, cellDescription, attempt + 1))
end

-- Колбэк таймера обязан быть глобальным.
function HouseHelper_InstanceRefreshTimer(pid, cellDescription, attempt)
    local ok, err = pcall(refreshInstanceCell, pid, cellDescription, attempt)
    if not ok then
        log(enumerations.log.ERROR, "HouseHelper_InstanceRefreshTimer: " .. tostring(err))
    end
end

houseHelper.RefreshInstanceCell = refreshInstanceCell

-- ===========================================================================
-- ПРАВА ДОСТУПА
-- ===========================================================================
local function accountName(pid)
    if Players[pid] == nil then return nil end
    return tostring(Players[pid].accountName or "")
end

local function nameInList(list, name)
    if type(list) ~= "table" or type(name) ~= "string" then return false end
    local target = lower(name)
    for _, entry in ipairs(list) do
        if lower(entry) == target then return true end
    end
    return false
end

-- Уровень доступа игрока к объекту:
--   "owner"    — владелец
--   "coowner"  — совладелец, полные права внутри
--   "friend"   — может войти, но не управлять
--   "public"   — объект открыт для всех
--   "none"     — доступа нет
function houseHelper.GetAccessLevel(pid, property)
    if property == nil then return "none" end
    local name = accountName(pid)
    if name == nil or name == "" then return "none" end

    local owner = property.owner
    if type(owner) ~= "string" or owner == "" then
        -- Свободный объект: внутрь никто не ходит, его покупают.
        return "none"
    end

    if lower(owner) == lower(name) then return "owner" end
    if nameInList(property.coOwners, name) then return "coowner" end
    if nameInList(property.friends, name) then return "friend" end
    if not property.locked or property.kind == "shop" or property.kind == "guild" then return "public" end
    return "none"
end

function houseHelper.CanManage(pid, property)
    local level = houseHelper.GetAccessLevel(pid, property)
    return level == "owner" or level == "coowner"
end

local function isStaff(pid)
    if Players[pid] == nil or not Players[pid]:IsLoggedIn() then return false end
    local rank = tonumber(Players[pid].data.settings and Players[pid].data.settings.staffRank) or 0
    return rank >= (tonumber(cfg().adminRank) or 2)
end

houseHelper.IsStaff = isStaff

-- ===========================================================================
-- ЛОКАЛИЗАЦИЯ СООБЩЕНИЙ
-- ===========================================================================
local function isRu(pid)
    local locale = package.loaded.localization
    if locale and type(locale.GetClientLanguage) == "function" then
        return locale.GetClientLanguage(pid) == "RU"
    end
    return Players[pid] ~= nil and tostring(Players[pid].language or "EN"):upper() == "RU"
end

local function tr(pid, ru, en)
    return isRu(pid) and ru or en
end

local function kindName(pid, kind)
    local names = {
        house = { "дом", "house" },
        shop = { "магазин", "shop" },
        tavern = { "таверна", "tavern" },
        guild = { "гильдейский зал", "guild hall" }
    }
    local entry = names[kind] or names.house
    return isRu(pid) and entry[1] or entry[2]
end

-- Заголовок окна. Отдельная таблица, а не kindName():upper(): string.upper
-- не трогает кириллицу, и русский заголовок выглядел как "дом: Балмора...".
local function kindHeading(pid, kind)
    local names = {
        house = { "ДОМ", "HOUSE" },
        shop = { "МАГАЗИН", "SHOP" },
        tavern = { "ТАВЕРНА", "TAVERN" },
        guild = { "ГИЛЬДЕЙСКИЙ ЗАЛ", "GUILD HALL" }
    }
    local entry = names[kind] or names.house
    return isRu(pid) and entry[1] or entry[2]
end

local function say(pid, message)
    if Players[pid] == nil then return end
    tes3mp.SendMessage(pid, tostring(message) .. "\n", false)
end

-- ===========================================================================
-- КОШЕЛЁК
-- ===========================================================================
local function goldCount(pid)
    if Players[pid] == nil or type(Players[pid].data.inventory) ~= "table" then return 0 end
    local total = 0
    for _, item in pairs(Players[pid].data.inventory) do
        if type(item) == "table" and lower(item.refId) == CURRENCY then
            total = total + (tonumber(item.count) or 0)
        end
    end
    return total
end

local function takeGold(pid, amount)
    amount = math.floor(tonumber(amount) or 0)
    if amount <= 0 then return true end
    if goldCount(pid) < amount then return false end
    inventoryHelper.removeClosestItem(Players[pid].data.inventory, CURRENCY, amount)
    tableHelper.cleanNils(Players[pid].data.inventory)
    Players[pid]:LoadItemChanges({ { refId = CURRENCY, count = amount } }, enumerations.inventory.REMOVE)
    return true
end

local function saveEconomy(pid)
    local player = Players[pid]
    -- Production JSON profiles have an accountFile. Alternate storage backends
    -- retain their existing Save hook without inventing a JSON account path.
    if type(player.accountFile) ~= "string" or player.accountFile == "" then
        if not saveStore(true) then return false end
        local save = player.QuicksaveToDrive or player.Save
        if type(save) ~= "function" then return true end
        local ok, result = pcall(save, player)
        return ok and result ~= false
    end
    store.pendingPayment = {file=player.accountFile, profile=player.data}
    if not writeStore() then
        -- Do not let shutdown autosaves persist an uncommitted debit/refund.
        store.pendingPayment = nil
        return false
    end
    saveScheduled = false
    if not recoverPayment(store) then error("Housing payment commit failed") end
    return true
end

local function giveGold(pid, amount)
    amount = math.floor(tonumber(amount) or 0)
    if amount <= 0 then return true end
    local item = { refId = CURRENCY, count = amount, charge = -1, enchantmentCharge = -1,
        soul = "", poisonId = "", poisonCharges = 0 }
    inventoryHelper.addItem(Players[pid].data.inventory, item.refId, item.count, item.charge,
        item.enchantmentCharge, item.soul, item.poisonId, item.poisonCharges)
    Players[pid]:LoadItemChanges({ item }, enumerations.inventory.ADD)
    return true
end

houseHelper.GetGold = goldCount

-- ===========================================================================
-- ВЛАДЕНИЕ
-- ===========================================================================
function houseHelper.ListOwnedBy(ownerName)
    local owned = {}
    if type(ownerName) ~= "string" or ownerName == "" then return owned end
    local target = lower(ownerName)
    for cellDescription, property in pairs(loadStore().properties) do
        if type(property) == "table" and type(property.owner) == "string" and
            property.owner ~= "" and lower(property.owner) == target then
            table.insert(owned, normalizeProperty(cellDescription, property))
        end
    end
    table.sort(owned, function(a, b) return a.cell < b.cell end)
    return owned
end

local function countOwnedOfKind(ownerName, kind)
    local count = 0
    for _, property in ipairs(houseHelper.ListOwnedBy(ownerName)) do
        if property.kind == kind then count = count + 1 end
    end
    return count
end

-- Освободить объект: снять владельца, сбросить инстанс через resetHelper,
-- забыть права доступа. Используется при продаже, изъятии администратором и
-- при удалении профиля.
function houseHelper.Release(cellDescription, reason)
    local property = houseHelper.GetProperty(cellDescription)
    if property == nil then return false end

    local previousOwner = property.owner
    if type(previousOwner) == "string" and previousOwner ~= "" then
        for _, member in ipairs(property.cells) do
            if property.mode ~= "original" then
                if not houseHelper.ResetInstance(houseHelper.GetInstanceDescription(member, previousOwner)) then
                    return false
                end
            end
        end
    end

    property.owner = nil
    property.instanceOwner = nil
    property.locked = false
    property.coOwners = {}
    property.friends = {}
    property.releasedAt = os.time()
    property.releaseReason = tostring(reason or "")
    saveStore()

    log(enumerations.log.INFO, "освобождён объект " .. cellDescription ..
        " (был у " .. tostring(previousOwner) .. ", причина: " .. tostring(reason) .. ")")
    return true
end

-- Вызывается profileHelper при удалении профиля.
function houseHelper.ReleaseAllForOwner(ownerName, reason)
    local released = 0
    for _, property in ipairs(houseHelper.ListOwnedBy(ownerName)) do
        if houseHelper.Release(property.cell, reason or "owner removed") then
            released = released + 1
        end
    end
    if released > 0 then saveStore(true) end
    return released
end

-- Очистка инстанса делегируется resetHelper: там уже есть эвакуация игроков,
-- безопасная отправка пакетов и работа с бэкапами. Дублировать это здесь
-- значило бы иметь две разные и расходящиеся реализации сброса ячейки.
function houseHelper.ResetInstance(instanceCell)
    if not houseHelper.IsInstanceCell(instanceCell) then return false end
    if type(instanceCell) ~= "string" or instanceCell == "" then return false end

    local resets = rawget(_G, "periodicCellResets")
    if type(resets) ~= "table" then
        log(enumerations.log.WARN, "periodicCellResets недоступен; инстанс " ..
            instanceCell .. " не сброшен")
        return false
    end

    local ok, success, message
    if type(resets.fullResetCell) == "function" then
        -- Z005: второй аргумент снимает защиту «это чей-то дом». Личная копия
        -- всегда принадлежит дому, поэтому без него сброс инстанса просто
        -- возвращал false и дом продавался вместе со всем содержимым.
        ok, success, message = pcall(resets.fullResetCell, instanceCell, true)
    elseif type(resets.ResetHome) == "function" then
        ok, success, message = pcall(resets.ResetHome, instanceCell)
    end

    if not ok or success ~= true then
        log(enumerations.log.ERROR, "сброс инстанса " .. instanceCell .. " не удался: " .. tostring(message or success))
        return false
    end
    if playerVitrines and not playerVitrines.ClearHouseCell(instanceCell) then return false end

    -- Запись инстанса и файл ячейки убираются вместе с владельцем: иначе на
    -- диске копится по одной ячейке на каждого бывшего хозяина.
    local recordStore = RecordStores and RecordStores["cell"]
    if recordStore ~= nil and recordStore.data.permanentRecords[instanceCell] ~= nil then
        recordStore.data.permanentRecords[instanceCell] = nil
        recordStore:Save()
    end

    local fixed = fileHelper.fixFilename(instanceCell)
    pcall(os.remove, config.dataPath .. "/cell/" .. fixed .. ".json")

    local data = loadStore()
    if type(data.purged) == "table" then data.purged[instanceCell] = nil end
    if type(data.deletedFurniture) == "table" then data.deletedFurniture[instanceCell] = nil end
    saveStore(true)
    pendingPurge[instanceCell] = nil
    pendingPurgeAcknowledged[instanceCell] = nil
    preparedContainerCells[instanceCell] = nil
    purgeDeletes[instanceCell] = nil
    repairingPurge[instanceCell] = nil

    log(enumerations.log.INFO, "инстанс сброшен и удалён: " .. instanceCell ..
        (message ~= nil and (" (" .. tostring(message) .. ")") or ""))
    return true
end

-- ===========================================================================
-- ПОКУПКА И ПРОДАЖА
-- ===========================================================================
function houseHelper.Buy(pid, cellDescription)
    local property = houseHelper.GetProperty(cellDescription)
    if property == nil then
        return false, tr(pid, "Этот интерьер не продаётся.", "This interior is not for sale.")
    end
    if not property.enabled then
        return false, tr(pid, "Этот объект снят с продажи.", "This property is not available.")
    end
    if type(property.owner) == "string" and property.owner ~= "" then
        return false, tr(pid, "У этого объекта уже есть владелец.", "This property already has an owner.")
    end

    local name = accountName(pid)
    if name == nil or name == "" then return false, "no account" end

    local settings = cfg()
    -- Z006: тот же предел, что и в BuyBlockReason, из одного места конфигурации.
    local totalLimit = tonumber(settings.totalLimit) or 1
    if #houseHelper.ListOwnedBy(name) >= totalLimit then
        return false, tr(pid, "У вас уже есть недвижимость. Продайте её, чтобы купить другую.",
            "You already own property. Sell it before buying another.")
    end
    local level = tonumber(Players[pid].data.stats and Players[pid].data.stats.level) or 1
    local requiredLevel = tonumber(settings.minimumLevel[property.kind]) or 1
    if level < requiredLevel then
        return false, tr(pid, "Нужен уровень ", "Requires level ") .. tostring(requiredLevel) .. "."
    end

    local limit = tonumber(settings.ownershipLimit[property.kind]) or 1
    if countOwnedOfKind(name, property.kind) >= limit then
        return false, tr(pid, "Достигнут предел: ", "Ownership limit reached: ") ..
            tostring(limit) .. " " .. kindName(pid, property.kind) .. "."
    end

    local price = tonumber(property.price) or houseHelper.CalculatePrice(cellDescription, property.kind)
    if goldCount(pid) < price then
        return false, tr(pid, "Не хватает золота: нужно ", "Not enough gold: ") .. tostring(price) ..
            tr(pid, ", у вас ", ", you have ") .. tostring(goldCount(pid)) .. "."
    end

    local previousInventory = tableHelper.deepCopy(Players[pid].data.inventory)
    local previousProperty = tableHelper.deepCopy(property)
    if not takeGold(pid, price) then
        return false, tr(pid, "Не удалось списать золото.", "Could not take the gold.")
    end

    property.owner = name
    property.instanceOwner = name
    property.instanceNaming = "home"
    property.locked = true
    property.coOwners = {}
    property.friends = {}
    property.boughtAt = os.time()
    property.paidPrice = price
    if not saveEconomy(pid) then
        for key in pairs(property) do property[key] = nil end
        for key, value in pairs(previousProperty) do property[key] = value end
        Players[pid].data.inventory = previousInventory
        Players[pid]:LoadInventory()
        return false, tr(pid, "Не удалось сохранить покупку.", "Could not save the purchase.")
    end

    -- Инстанс создаётся сразу, чтобы первая же попытка войти не ждала записи.
    local instanceCell = houseHelper.GetInstanceDescription(cellDescription, name)
    houseHelper.EnsureInstanceRecord(pid, instanceCell, cellDescription)
    houseHelper.MarkForPurge(instanceCell)
    houseHelper.SendClientState(pid)

    log(enumerations.log.INFO, name .. " купил " .. cellDescription .. " за " .. price)
    return true, tr(pid, "Куплено: ", "Purchased: ") .. cellDescription ..
        " (" .. tostring(price) .. tr(pid, " золота).", " gold).")
end

function houseHelper.Sell(pid, cellDescription)
    local property = houseHelper.GetProperty(cellDescription)
    if property == nil then
        return false, tr(pid, "Объект не найден.", "Property not found.")
    end
    if houseHelper.GetAccessLevel(pid, property) ~= "owner" then
        return false, tr(pid, "Продать может только владелец.", "Only the owner can sell.")
    end

    local refund = math.floor((tonumber(property.paidPrice) or property.price) *
        (tonumber(cfg().refundRatio) or 0.5))

    local previousInventory = tableHelper.deepCopy(Players[pid].data.inventory)
    local previousProperty = tableHelper.deepCopy(property)
    if not houseHelper.Release(cellDescription, "sold by owner") then
        return false, tr(pid, "Не удалось очистить дом. Продажа отменена.", "Could not reset the home. Sale cancelled.")
    end
    giveGold(pid, refund)
    if not saveEconomy(pid) then
        for key in pairs(property) do property[key] = nil end
        for key, value in pairs(previousProperty) do property[key] = value end
        Players[pid].data.inventory = previousInventory
        Players[pid]:LoadInventory()
        return false, tr(pid, "Не удалось сохранить возврат золота.", "Could not save the refund.")
    end
    houseHelper.SendClientState(pid)

    return true, tr(pid, "Продано. Возврат: ", "Sold. Refund: ") .. tostring(refund) ..
        tr(pid, " золота.", " gold.")
end

-- ===========================================================================
-- ВХОД ЧЕРЕЗ ДВЕРЬ
-- ---------------------------------------------------------------------------
-- Клиент присылает запрос ДО перехода. Дальше возможны три исхода:
--
--   1. Игроку нечего выбирать (объект не зарегистрирован, заперт для него, или
--      он его владелец и других вариантов нет) — сервер молча разрешает
--      обычный переход, и клиент выполняет его сам.
--   2. Есть выбор — сервер показывает меню и ждёт ответа.
--   3. Долгое нажатие на свободном объекте — сервер показывает предложение
--      покупки.
--
-- Телепорт всегда выполняет сервер: SetCell + SendCell до того, как клиент
-- что-либо сделал. Возврата «назад» больше не бывает, потому что клиент туда и
-- не попадал.
-- ===========================================================================
local DOOR_MENU_ID = 9060
local BUY_MENU_ID = 9061

config.customMenuIds = type(config.customMenuIds) == "table" and config.customMenuIds or {}
config.customMenuIds.houseDoorChoice = config.customMenuIds.houseDoorChoice or DOOR_MENU_ID
config.customMenuIds.houseBuyConfirm = config.customMenuIds.houseBuyConfirm or BUY_MENU_ID
DOOR_MENU_ID = config.customMenuIds.houseDoorChoice
BUY_MENU_ID = config.customMenuIds.houseBuyConfirm

-- Что именно предложено конкретному игроку прямо сейчас.
local pendingChoice = {}

local function rememberDoor(destinationCell, fromCell)
    if type(destinationCell) ~= "string" or destinationCell == "" then return end
    -- Never persist a generated " - home" cell as a topology endpoint.
    -- Linked-room discovery works on the authored base interiors only.
    fromCell = houseHelper.GetBaseCellForInstance(fromCell) or fromCell
    destinationCell = houseHelper.GetBaseCellForInstance(destinationCell) or destinationCell
    local data = loadStore()
    data.doors[destinationCell] = type(data.doors[destinationCell]) == "table"
        and data.doors[destinationCell] or {}
    local list = data.doors[destinationCell]
    if type(fromCell) == "string" and fromCell ~= "" and not tableHelper.containsValue(list, fromCell) then
        table.insert(list, fromCell)
        saveStore()
    end
end

-- Разрешить обычный переход: сервер отвечает клиенту «иди куда шёл».
local function allowPlainEntry(pid, destinationCell)
    tes3mp.SendMessage(pid, CONTROL_PREFIX .. "GO\t" .. tostring(destinationCell) .. "\n", false)
end

-- Авторитетный телепорт в выбранную ячейку.
local function warp(pid, cellDescription, position)
    if type(cellDescription) ~= "string" or cellDescription == "" then return false end

    tes3mp.SendMessage(pid, CONTROL_PREFIX .. "CANCEL\n", false)
    -- Commit the profile only when OnPlayerCellChange confirms the target.
    -- Otherwise its previous-cell argument becomes the destination and a stale
    -- packet can overwrite the last valid location before the move completes.
    tes3mp.SetCell(pid, cellDescription)
    if type(position) == "table" and position.posX ~= nil
        and position.posY ~= nil and position.posZ ~= nil then
        tes3mp.SetPos(pid, position.posX, position.posY, position.posZ)
        tes3mp.SetRot(pid, position.rotX or 0, position.rotZ or 0)
    end
    tes3mp.SendCell(pid)
    if type(position) == "table" and position.posX ~= nil
        and position.posY ~= nil and position.posZ ~= nil then
        tes3mp.SendPos(pid)
    end
    return true
end

houseHelper.Warp = warp

-- Some OpenMW/TES3MP clients briefly rebuild the local player while a dynamic
-- CELL record with baseId is being entered.  The server profile still contains
-- the correct equipment, but the local model can appear completely naked and
-- a broken client may even publish that transient empty equipment state back.
-- Keep a very short authoritative snapshot around house-instance transitions
-- and re-apply it once the destination cell is loaded.
local equipmentTransitionGuards = {}
local EQUIPMENT_GUARD_SECONDS = 10
local EQUIPMENT_REFRESH_DELAY_MS = 450
local equipmentGuardSerial = 0

local function equippedCount(equipment)
    local count = 0
    for _, item in pairs(type(equipment) == "table" and equipment or {}) do
        if type(item) == "table" and type(item.refId) == "string" and item.refId ~= "" and
            (tonumber(item.count) or 0) > 0 then count = count + 1 end
    end
    return count
end

local function armEquipmentTransitionGuard(pid, targetCell)
    local player = Players[pid]
    if player == nil or not player:IsLoggedIn() or type(targetCell) ~= "string" then return end
    equipmentGuardSerial = equipmentGuardSerial + 1
    equipmentTransitionGuards[pid] = {
        serial = equipmentGuardSerial,
        targetCell = targetCell,
        expires = os.time() + EQUIPMENT_GUARD_SECONDS,
        equipment = tableHelper.deepCopy(player.data.equipment or {})
    }
end

local function restoreGuardedEquipment(pid, guard)
    local player = Players[pid]
    if player == nil or not player:IsLoggedIn() or type(guard) ~= "table" then return false end
    player.data.equipment = tableHelper.deepCopy(guard.equipment or {})
    player.previousEquipment = tableHelper.deepCopy(player.data.equipment)
    player:LoadEquipment()
    return true
end

function HouseHelper_EquipmentRefreshTimer(pid, serial)
    local guard = equipmentTransitionGuards[pid]
    if not guard or guard.serial ~= tonumber(serial) then return end
    if os.time() > guard.expires then equipmentTransitionGuards[pid] = nil; return end
    if tes3mp.GetCell(pid) == guard.targetCell then
        -- ArenaMP Y088: refreshing the outfit once does not mean the client's
        -- actor rebuild has finished.  Keep the snapshot armed until its timeout
        -- so a delayed empty PlayerEquipment delta cannot undress the player.
        restoreGuardedEquipment(pid, guard)
    end
end

function houseHelper.EvictOriginalVisitor(pid, home)
    local exit = home and home.exitLocation
    if not exit then
        local spawn = config.noninstancedSpawn or config.defaultRespawn
        if spawn and spawn.cellDescription and spawn.position then
            exit = {cell=spawn.cellDescription,posX=spawn.position[1],posY=spawn.position[2],posZ=spawn.position[3],
                rotX=(spawn.rotation or {})[1] or 0, rotZ=(spawn.rotation or {})[2] or 0}
        end
    end
    if exit and exit.cell then return warp(pid,exit.cell,exit) end
    return false
end


-- Собрать список вариантов для этой двери.
local function buildOptions(pid, property, destinationCell)
    local options = {}

    if property and property.mode == "original" and property.owner and property.owner ~= "" then
        if houseHelper.GetAccessLevel(pid, property) ~= "none" then
            table.insert(options, {kind="instance", cell=destinationCell, baseCell=destinationCell,
                owner=property.owner, label=tr(pid, "Войти в объект", "Enter property")})
        end
        if houseHelper.GetAccessLevel(pid, property) == "owner" then
            table.insert(options, {kind="manage", cell=destinationCell, label=tr(pid,"Управление домом","Manage property")})
        end
        return options
    end

    -- Оригинальная ячейка доступна всегда: система домов ничего не отбирает.
    table.insert(options, {
        kind = "origin",
        cell = destinationCell,
        label = tr(pid, "Войти в обычную ячейку", "Enter the ordinary cell")
    })

    if property == nil then return options end

    local level = houseHelper.GetAccessLevel(pid, property)

    if level == "owner" or level == "coowner" or level == "friend" or level == "public" then
        local owner = property.owner
        local instanceCell = houseHelper.GetInstanceDescription(destinationCell, owner)
        local label
        if level == "owner" then
            label = tr(pid, "Войти: ваш ", "Enter: your ") .. kindName(pid, property.kind)
        else
            label = tr(pid, "Войти: ", "Enter: ") .. kindName(pid, property.kind) ..
                " — " .. tostring(owner)
        end
        table.insert(options, {
            kind = "instance",
            cell = instanceCell,
            baseCell = destinationCell,
            owner = owner,
            label = label
        })
    end

    -- Z006: управление открывается только владельцу: вкладка «Мой дом» строится
    -- по списку ЕГО объектов, и совладельцу показывать там нечего.
    if level == "owner" then
        table.insert(options, {
            kind = "manage",
            cell = destinationCell,
            label = tr(pid, "Управление домом", "Manage the property")
        })
    end

    -- Свободный объект: покупка прямо из меню двери. Пункт показывается
    -- ВСЕГДА, в том числе когда купить нельзя, — иначе отказ выглядит как
    -- поломка. Причина пишется прямо в строке.
    if (type(property.owner) ~= "string" or property.owner == "") and property.enabled then
        local reason = houseHelper.BuyBlockReason(pid, property)
        table.insert(options, {
            kind = "buy",
            cell = destinationCell,
            blocked = reason,
            -- Кнопка остаётся короткой: длинная причина уходит в текст окна,
            -- иначе ряд кнопок расползается на весь экран.
            label = reason == nil
                and (tr(pid, "Купить (", "Buy (") .. tostring(property.price) ..
                     tr(pid, " золота)", " gold)"))
                or tr(pid, "Купить нельзя", "Cannot buy")
        })
    end

    return options
end

-- Per-account preference; customVariables is serialized in the player profile.
local function doorPreference(pid)
    local data = Players[pid] and Players[pid].data
    local mode = data and data.customVariables and data.customVariables.housingEntryMode
    if mode == "instance" or mode == "original" or mode == "menu" then return mode end
end

local function preferenceLabel(pid, mode)
    if mode == "instance" then return tr(pid, "В экземпляр", "Instance") end
    if mode == "menu" then return tr(pid, "Показывать меню", "Show menu") end
    return tr(pid, "В обычную ячейку", "Original cell")
end

local function saveDoorPreference(pid, mode)
    if mode ~= "instance" and mode ~= "original" and mode ~= "menu" then return false end
    local player = Players[pid]
    player.data.customVariables = player.data.customVariables or {}
    local old = player.data.customVariables.housingEntryMode
    player.data.customVariables.housingEntryMode = mode
    local save = player.QuicksaveToDrive or player.Save
    local ok, result = false, nil
    if type(save) == "function" then ok, result = pcall(save, player) end
    if not ok or result == false then
        player.data.customVariables.housingEntryMode = old
        tes3mp.MessageBox(pid, -1, tr(pid, "Не удалось сохранить настройку. Попробуйте снова.",
            "Could not save the preference. Try again."))
        return false
    end
    return true
end

local function showDoorMenu(pid, property, destinationCell, options, position, originalLocked, settingsPage)
    local header
    if property ~= nil then
        header = kindHeading(pid, property.kind) .. ": " .. destinationCell
    else
        header = destinationCell
    end

    local lines = { header, "" }
    if property ~= nil then
        if type(property.owner) == "string" and property.owner ~= "" then
            table.insert(lines, tr(pid, "Владелец: ", "Owner: ") .. property.owner)
            table.insert(lines, tr(pid, "Доступ: ", "Access: ") ..
                ((property.locked and property.kind ~= "shop" and property.kind ~= "guild") and tr(pid, "только для друзей и совладельцев", "friends and co-owners only")
                    or tr(pid, "открыт", "open")))
        else
            table.insert(lines, tr(pid, "Свободен. Цена: ", "Vacant. Price: ") .. tostring(property.price) ..
                tr(pid, " золота.", " gold."))
        end
        table.insert(lines, "")
    end
    -- Z006: причина отказа в покупке — здесь, в тексте, а не на кнопке.
    for _, option in ipairs(options) do
        if option.kind == "buy" and option.blocked ~= nil then
            table.insert(lines, tr(pid, "Купить нельзя: ", "Cannot buy: ") .. option.blocked)
            table.insert(lines, "")
        end
    end

    table.insert(lines, tr(pid, "Одно нажатие для всех объектов: ", "Single press for all properties: ") ..
        preferenceLabel(pid, doorPreference(pid)))
    if settingsPage then
        table.insert(lines, tr(pid, "Выберите режим. Он сохраняется в вашем профиле.",
            "Choose a mode. It is saved in your player profile."))
    else
        table.insert(options, {kind="settings", label=tr(pid, "Настроить одно нажатие", "Single-press preference")})
        table.insert(lines, tr(pid, "Что сделать?", "What do you want to do?"))
    end

    local buttons = {}
    for _, option in ipairs(options) do
        table.insert(buttons, option.label)
    end
    table.insert(buttons, tr(pid, "Отмена", "Cancel"))

    pendingChoice[pid] = { options = options, destinationCell = destinationCell, position = position, originalLocked = originalLocked }
    tes3mp.CustomMessageBox(pid, DOOR_MENU_ID, table.concat(lines, "\n"), table.concat(buttons, ";"))
end

-- Главная точка входа: клиент просит решение по управляемой двери.
function houseHelper.HandleDoorRequest(pid, destinationCell, hold, position, originalLocked)
    if Players[pid] == nil or not Players[pid]:IsLoggedIn() then return end
    if not cfg().enabled then
        allowPlainEntry(pid, destinationCell)
        return
    end

    -- Z006: дверь наружу — просто дверь наружу.
    if houseHelper.IsExteriorCell(destinationCell) then
        allowPlainEntry(pid, destinationCell)
        return
    end

    tes3mp.SendMessage(pid, CONTROL_PREFIX .. "WAIT\n", false)
    local currentCell = tes3mp.GetCell(pid)
    local currentBase = houseHelper.GetBaseCellForInstance(currentCell)
    local currentHome = currentBase and houseHelper.GetProperty(currentBase) or houseHelper.GetProperty(currentCell)
    local targetHome = houseHelper.GetProperty(destinationCell)

    -- A house and its basement/library/etc. are often separate CELL records in
    -- Morrowind plugins.  If an actually connected interior clearly belongs to
    -- the same named building, fold it into the same property before deciding
    -- where the door leads.  This is deliberately topology + name based; nearby
    -- but unrelated apartments are never merged merely because names look alike.
    if currentHome and currentHome ~= targetHome and type(houseHelper.TryAutoLink) == "function" then
        local ok, linked = pcall(houseHelper.TryAutoLink, currentHome.cell, destinationCell)
        if ok and linked then
            currentHome = houseHelper.GetProperty(currentHome.cell) or currentHome
            targetHome = houseHelper.GetProperty(destinationCell)
        end
    end

    -- Keep linked rooms in the same world: public originals stay public.
    -- Only an existing private instance (or an owned original property) may
    -- select EnterInstance; a vacant building has no instance owner at all.
    if currentHome and currentHome == targetHome and currentHome.enabled
        and type(currentHome.owner) == "string" and currentHome.owner ~= ""
        and (currentBase ~= nil or currentHome.mode == "original") then
        houseHelper.EnterInstance(pid, {owner = currentHome.owner, baseCell = destinationCell,
            cell = houseHelper.GetInstanceDescription(destinationCell, currentHome.owner)}, position)
        return
    end
    rememberDoor(destinationCell, currentCell)

    -- Ленивая регистрация: именно здесь сервер впервые узнаёт о существовании
    -- интерьера, куда ведёт дверь.
    local property = houseHelper.Observe(destinationCell, pid)

    if property == nil or not property.enabled then
        allowPlainEntry(pid, destinationCell)
        return
    end

    local access = houseHelper.GetAccessLevel(pid, property)
    local trusted = access == "owner" or access == "coowner" or access == "friend"
    local mode = doorPreference(pid)
    if property.mode == "original" then
        if property.owner and property.owner ~= "" and access == "none" then
            tes3mp.SendMessage(pid, CONTROL_PREFIX .. "CANCEL\n", false)
            tes3mp.MessageBox(pid, -1, tr(pid, "Доступ к этому дому закрыт.", "This property is private."))
            return
        end
        -- Remember a real approach position for revocation/reconnect eviction.
        local previous = Players[pid].data.location
        if previous and previous.cell == currentCell and houseHelper.GetProperty(currentCell) ~= property and previous.posX then
            property.exitLocation = {cell=currentCell,posX=previous.posX,posY=previous.posY,posZ=previous.posZ,
                rotX=previous.rotX or 0,rotZ=previous.rotZ or 0}
            saveStore()
        end
        if property.owner and property.owner ~= "" and hold ~= true and mode ~= "menu" then
            houseHelper.EnterInstance(pid, {owner=property.owner,baseCell=destinationCell,cell=destinationCell},position)
            return
        end
    end

    if hold ~= true then
        -- Preserve Z010's ESP convenience only until the player explicitly
        -- chooses a mode. A chosen original/menu mode must not be ignored.
        if (mode == nil and originalLocked == true and trusted) or
            (mode == "instance" and access ~= "none") then
            houseHelper.EnterInstance(pid, {owner = property.owner, baseCell = destinationCell,
                cell = houseHelper.GetInstanceDescription(destinationCell, property.owner)}, position)
            return
        elseif mode ~= "menu" then
            allowPlainEntry(pid, destinationCell)
            return
        end
    end
    -- Hold always opens the menu so a direct-entry preference is reversible.

    local options = buildOptions(pid, property, destinationCell)

    if originalLocked == true and options[1] and options[1].kind == "origin" then
        options[1].blocked = tr(pid, "Обычная дверь заперта. Сначала откройте её замок.",
            "The original door is locked. Unlock it first.")
        options[1].label = tr(pid, "Обычная ячейка (заперто)", "Ordinary cell (locked)")
    end
    showDoorMenu(pid, property, destinationCell, options, position, originalLocked)
end

-- Z006: почему покупка невозможна — одной строкой, или nil, если возможна.
--
-- Раньше это был булев CanBuy, и дверь на отказ просто молчала: игрок держал
-- «Взять» на свободной лавке и попадал в обычную ячейку, ничего не поняв.
-- Теперь причина есть у всех, кто её спрашивает: и у меню двери, и у подсказки.
-- Деньги здесь не проверяются — их проверяет сама покупка, чтобы окно с ценой
-- всё-таки открылось и игрок увидел, сколько не хватает.
function houseHelper.BuyBlockReason(pid, property)
    if property == nil then
        return tr(pid, "Эта ячейка не продаётся.", "This cell is not for sale.")
    end
    if not property.enabled then
        return tr(pid, "Снято с продажи.", "Not available.")
    end
    if type(property.owner) == "string" and property.owner ~= "" then
        return tr(pid, "Уже есть владелец.", "Already owned.")
    end
    if Players[pid] == nil or not Players[pid]:IsLoggedIn() then
        return tr(pid, "Недоступно.", "Unavailable.")
    end

    local settings = cfg()
    local level = tonumber(Players[pid].data.stats and Players[pid].data.stats.level) or 1
    local requiredLevel = tonumber(settings.minimumLevel[property.kind]) or 1
    if level < requiredLevel then
        return tr(pid, "Нужен уровень ", "Requires level ") .. tostring(requiredLevel) .. "."
    end

    local name = accountName(pid)
    local owned = houseHelper.ListOwnedBy(name)

    -- Общий предел: один объект любого типа на аккаунт (Z002).
    local totalLimit = tonumber(settings.totalLimit) or 1
    if #owned >= totalLimit then
        return tr(pid, "У вас уже есть недвижимость. Продайте её, чтобы купить другую.",
            "You already own property. Sell it before buying another.")
    end

    local limit = tonumber(settings.ownershipLimit[property.kind]) or 1
    local ofKind = 0
    for _, entry in ipairs(owned) do
        if entry.kind == property.kind then ofKind = ofKind + 1 end
    end
    if ofKind >= limit then
        return tr(pid, "Достигнут предел: ", "Limit reached: ") .. tostring(limit) ..
            " " .. kindName(pid, property.kind) .. "."
    end

    return nil
end

function houseHelper.CanBuy(pid, property)
    return houseHelper.BuyBlockReason(pid, property) == nil
end

function houseHelper.ShowBuyConfirmation(pid, property)
    tes3mp.SendMessage(pid, CONTROL_PREFIX .. "CANCEL\n", false)
    if property == nil then return end
    local lines = {
        kindHeading(pid, property.kind) .. ": " .. property.cell,
        "",
        tr(pid, "Цена: ", "Price: ") .. tostring(property.price) .. tr(pid, " золота", " gold"),
        tr(pid, "У вас: ", "You have: ") .. tostring(goldCount(pid)) .. tr(pid, " золота", " gold"),
        "",
        property.mode == "original"
            and tr(pid, "Вы покупаете исходную ячейку. Копия не создаётся; содержимое не очищается.",
                "You buy the original cell. No copy is created and its contents are preserved.")
            or tr(pid, "После покупки вы получите личную копию этой ячейки. NPC и лишние предметы из неё будут убраны.",
                "After the purchase you get a private copy of this interior. Its NPCs and loose items are removed.")
    }
    pendingChoice[pid] = { buyCell = property.cell }
    tes3mp.CustomMessageBox(pid, BUY_MENU_ID, table.concat(lines, "\n"),
        tr(pid, "Купить;Отмена", "Buy;Cancel"))
end

local function enterInstance(pid, option, position)
    local property = houseHelper.GetProperty(option.baseCell)
    if not property or not property.enabled or property.owner ~= option.owner
        or houseHelper.GetAccessLevel(pid, property) == "none" then
        tes3mp.SendMessage(pid, CONTROL_PREFIX .. "CANCEL\n", false)
        say(pid, tr(pid, "Доступ к дому изменился. Откройте дверь заново.", "Access changed. Use the door again."))
        return
    end
    if property.mode == "original" then
        houseHelper.SendClientState(pid)
        warp(pid, option.baseCell, position)
        return
    end
    local ownerName = option.owner
    local baseCell = option.baseCell
    local instanceCell = option.cell

    armEquipmentTransitionGuard(pid, instanceCell)

    if not houseHelper.EnsureInstanceRecord(pid, instanceCell, baseCell) then
        equipmentTransitionGuards[pid] = nil
        tes3mp.SendMessage(pid, CONTROL_PREFIX .. "CANCEL\n", false)
        say(pid, tr(pid, "Не удалось открыть личную копию ячейки.",
            "The private copy of this interior could not be opened."))
        allowPlainEntry(pid, baseCell)
        return
    end

    houseHelper.MarkForPurge(instanceCell)

    -- Сообщаем клиенту, что эта ячейка — инстанс под уборку, ДО телепорта:
    -- клиент должен знать об этом к моменту загрузки содержимого.
    if houseHelper.IsPurgePending(instanceCell) then
        tes3mp.SendMessage(pid, CONTROL_PREFIX .. "PURGE\t" .. instanceCell .. "\n", false)
    end

    houseHelper.SendClientState(pid)
    warp(pid, instanceCell, position)
    log(enumerations.log.INFO, tostring(accountName(pid)) .. " вошёл в инстанс " .. instanceCell)
end

houseHelper.EnterInstance = enterInstance

-- ===========================================================================
-- СОСТОЯНИЕ ДЛЯ КЛИЕНТА
-- ---------------------------------------------------------------------------
-- Клиент должен знать две вещи: какие ячейки назначения он обязан отдавать
-- серверу вместо самостоятельного перехода, и что показать в подсказке, когда
-- игрок смотрит на такую дверь.
--
-- Формат: @@AMP_HOUSE@@STATE \t <запись> \t <запись> ...
--         запись = <ячейка> ^ <статус> ^ <подсказка>
--         статус = free | owned | mine | locked
-- ===========================================================================
local function escapeField(value)
    value = tostring(value or "")
    value = value:gsub("\\", "\\\\")
    value = value:gsub("\t", " ")
    value = value:gsub("%^", " ")
    value = value:gsub("[\r\n]", " ")
    return value
end

local function displayHomeName(pid, property)
    local labels = {house = {"Дом", "House"}, tavern = {"Таверна", "Tavern"},
        guild = {"Гильдия", "Guild"}, shop = {"Магазин", "Shop"}}
    local label = labels[property.kind] or labels.house
    local owner = tostring(property.owner or ""):gsub('[\r\n\t%^#{}]', '')
    return tr(pid, label[1], label[2]) .. " (" .. owner .. ")"
end

local function hasBottomlessHomeStorage(pid)
    local player = Players[pid]
    if player == nil or not player:IsLoggedIn() then return false end
    local days = tonumber(cfg().bottomlessStorageAgeDays) or 30
    if days <= 0 then return true end
    local timestamps = type(player.data) == "table" and player.data.timestamps or nil
    local created = type(timestamps) == "table" and tonumber(timestamps.creation) or nil
    if created == nil or created <= 0 then return false end
    return (os.time() - created) >= math.floor(days * 86400)
end

function houseHelper.SendClientState(pid)
    if Players[pid] == nil or not Players[pid]:IsLoggedIn() then return end
    -- H019: server-supplied moderator/admin placement permission.
    local settings = Players[pid].data.settings or {}
    local staff = (tonumber(settings.staffRank) or 0) >= 1
    tes3mp.SendMessage(pid, CONTROL_PREFIX .. "EDIT_ROLE\t" .. (staff and "1" or "0") .. "\n", false)
    if not cfg().enabled then return end

    local name = accountName(pid)
    local currentCell = tes3mp.GetCell(pid) or ""
    local currentKey = lower(currentCell)
    local interest = clientInterests[pid]
    if interest and interest.cell ~= currentKey then
        clientInterests[pid] = nil
    for cell, receipts in pairs(purgeDeletes) do
        receipts[pid] = nil
        local ack = pendingPurgeAcknowledged[cell]
        if ack and ack.pid == pid then pendingPurgeAcknowledged[cell] = nil end
    end
        interest = nil
    end
    local relevant = {[currentKey] = true}
    local topology = getTopologyIndex()
    for destination in pairs(topology.doorsFrom[currentKey] or {}) do
        relevant[destination] = true
    end
    if interest then
        for destination in pairs(interest.destinations) do relevant[destination] = true end
    end
    local fields = { "STATE" }

    local candidates = {}
    for member in pairs(relevant) do
        for _, key in ipairs(topology.members[member] or {}) do
            candidates[key] = true
        end
    end
    for cellDescription in pairs(candidates) do
        local property = normalizeProperty(cellDescription, loadStore().properties[cellDescription])
        if property.enabled then
            local status
            local owner = property.owner

            -- Z008: подсказка двери читается сверху вниз, как вывеска.
            --
            -- Раньше всё лепилось в одну строку — «Свободный дом: 8000 золота.
            -- Удерживайте Взять, чтобы купить», — и название объекта в ней
            -- терялось. Теперь первая строка это всегда ИМЯ (владелец может его
            -- поменять во вкладке «Мой дом»), а цена, владелец, что даст
            -- удержание и почему нельзя купить идут ниже, каждое своей строкой.
            local lines = { tostring(property.title or property.cell) }

            if type(owner) ~= "string" or owner == "" then
                status = "free"
                table.insert(lines, tr(pid, "Свободный ", "Vacant ") ..
                    kindName(pid, property.kind) ..
                    tr(pid, ". Цена: ", ". Price: ") .. tostring(property.price) ..
                    tr(pid, " золота", " gold"))
                -- Причина отказа берётся оттуда же, откуда её берёт меню двери,
                -- поэтому подсказка и меню не могут разойтись.
                local reason = houseHelper.BuyBlockReason(pid, property)
                table.insert(lines, reason == nil
                    and tr(pid, "Удерживайте Взять, чтобы купить.", "Hold Take to buy.")
                    or reason)
            elseif lower(owner) == lower(name or "") then
                status = "mine"
                table.insert(lines, displayHomeName(pid, property))
                table.insert(lines, tr(pid, "Удерживайте Взять: выбор входа.", "Hold Take: choose entrance."))
            else
                local level = houseHelper.GetAccessLevel(pid, property)
                table.insert(lines, displayHomeName(pid, property))
                if level == "none" then
                    -- Имя владельца показываем всем; запертая дверь сохраняет
                    -- обычный переход в оригинал без запроса входа в дом.
                    status = "locked"
                else
                    status = "owned"
                    table.insert(lines, level == "public"
                        and tr(pid, "Удерживайте Взять: меню входа.", "Hold Take: entrance menu.")
                        or tr(pid, "Удерживайте Взять: выбор входа.", "Hold Take: choose entrance."))
                end
            end

            table.insert(lines, tr(pid, "Одно нажатие: ", "Single press: ") .. preferenceLabel(pid, doorPreference(pid)))
            -- Экранируем КАЖДУЮ строку отдельно и только потом склеиваем: иначе
            -- escapeField удвоил бы обратный слэш в самом разделителе, и клиент
            -- показал бы "\n" текстом вместо переноса.
            for index, line in ipairs(lines) do
                lines[index] = escapeField(line)
            end
            local hint = table.concat(lines, "\\n")

            local access = houseHelper.GetAccessLevel(pid, property)
            local trusted = access == "owner" or access == "friend" or access == "coowner"
            if status ~= nil then
                for _, member in ipairs(property.cells) do
                    if relevant[lower(member)] then
                        table.insert(fields, escapeField(member) .. "^" .. status .. "^" .. hint ..
                            "^" .. ((property.title == nil or property.title == property.cell) and "celltitle" or "customtitle") .. "^" .. (trusted and doorPreference(pid) ~= "original" and doorPreference(pid) ~= "menu" and "1" or "0") .. "^" .. property.mode)
                    end
                end
            end
        end
    end

    -- H018: keep each ChatMessage below RakString's 65535-byte wire limit.
    -- Large snapshots are assembled atomically by the updated client. Never
    -- send multiple STATE packets: each one replaces the previous door list.
    local state = "CELL_STATE\t" .. escapeField(currentCell) .. "\t" .. table.concat(fields, "\t")
    if #state <= 12000 then
        tes3mp.SendMessage(pid, CONTROL_PREFIX .. state .. "\n", false)
    elseif #state <= 16 * 1024 * 1024 then
        tes3mp.SendMessage(pid, CONTROL_PREFIX .. "STATE_BEGIN\t" .. tostring(#state) .. "\n", false)
        for offset = 1, #state, 12000 do
            tes3mp.SendMessage(pid, CONTROL_PREFIX .. "STATE_PART\t" .. state:sub(offset, offset + 11999) .. "\n", false)
        end
        tes3mp.SendMessage(pid, CONTROL_PREFIX .. "STATE_END\n", false)
    else
        log(enumerations.log.ERROR, "Housing state exceeds 16 MiB; client state retained")
    end
    tes3mp.SendMessage(pid, CONTROL_PREFIX .. "ROOMS\n", false)
    for key, raw in pairs(loadStore().properties) do
        if raw.owner and raw.owner ~= "" then
            local property = normalizeProperty(key, raw)
            local level = houseHelper.GetAccessLevel(pid, property)
            local take = level == "owner" or level == "coowner" or (level ~= "none" and property.allowTake)
            -- Z006: «свой дом» — это владелец и совладелец. Клиенту это нужно
            -- отдельно от take: гость в таверне с разрешением брать предметы
            -- всё-таки в чужом доме, и сундуки там должны оставаться чужими.
            local mine = level == "owner" or level == "coowner"
            -- Y069: friends are trusted enough to take BOOK/notes even when the
            -- owner disabled general item pickup. Public visitors remain read-only.
            local bookTake = take or level == "friend"
            -- Y070: beds/containers are trusted utilities. Friends may use them
            -- even when ordinary visitors are not allowed to take loose items.
            local utility = bookTake
            local title = displayHomeName(pid, property)
            for _, member in ipairs(property.cells) do
                local instance = houseHelper.GetInstanceDescription(member, property.owner)
                tes3mp.SendMessage(pid, CONTROL_PREFIX .. "ROOM\t" .. escapeField(instance) .. "\t" ..
                    escapeField(title) .. "\t" .. (take and "1" or "0") .. "\t" ..
                    (mine and "1" or "0") .. "\t" .. (bookTake and "1" or "0") .. "\t" ..
                    (utility and "1" or "0") .. "\n", false)
            end
        end
    end
    tes3mp.SendMessage(pid, CONTROL_PREFIX .. "OWN\t" .. (#houseHelper.ListOwnedBy(name) > 0 and "1" or "0") .. "\n", false)
    tes3mp.SendMessage(pid, CONTROL_PREFIX .. "STORAGE\t" ..
        (hasBottomlessHomeStorage(pid) and "1" or "0") .. "\n", false)
    houseHelper.SendHomeState(pid)
end

local function currentProperty(pid)
    local cellDescription = tes3mp.GetCell(pid)
    local property = houseHelper.GetProperty(cellDescription)
    if property ~= nil then return property end

    -- Игрок может стоять внутри собственного инстанса.
    local base = houseHelper.GetBaseCellForInstance(cellDescription)
    if base ~= nil then return houseHelper.GetProperty(base) end
    return nil
end

local function describeProperty(pid, property)
    local lines = {
        kindHeading(pid, property.kind) .. ": " .. property.cell,
        tr(pid, "Цена: ", "Price: ") .. tostring(property.price)
    }
    if type(property.owner) == "string" and property.owner ~= "" then
        table.insert(lines, tr(pid, "Владелец: ", "Owner: ") .. property.owner)
        table.insert(lines, tr(pid, "Замок: ", "Locked: ") ..
            (property.locked and tr(pid, "да", "yes") or tr(pid, "нет", "no")))
        if #property.coOwners > 0 then
            table.insert(lines, tr(pid, "Совладельцы: ", "Co-owners: ") .. table.concat(property.coOwners, ", "))
        end
        if #property.friends > 0 then
            table.insert(lines, tr(pid, "Друзья: ", "Friends: ") .. table.concat(property.friends, ", "))
        end
    else
        table.insert(lines, tr(pid, "Свободен.", "Vacant."))
    end
    return table.concat(lines, "\n")
end

-- ===========================================================================
-- Z005: вкладка «Мой дом» в меню игрока.
--
-- Раньше управление домом было серверным ListBox: игрок нажимал вкладку, меню
-- закрывалось, поверх выпадал диалог. Теперь вкладка — обычная страница меню,
-- как «Группа» или «Игроки», а сервер шлёт ей готовое состояние. Диалог
-- остаётся ровно там, где без него нельзя, — для ввода текста (название,
-- имя игрока).
--
-- Формат: @@AMP_HOUSE@@HOME \t <есть 0|1> \t <название> \t <вид> \t <ячейка>
--                        \t <помещений> \t <закрыт 0|1> \t <гости берут 0|1>
--                        \t <совладельцы> \t <друзья> \t <описание>
-- ===========================================================================
function houseHelper.SendHomeState(pid)
    if Players[pid] == nil or not Players[pid]:IsLoggedIn() then return end

    local owned = houseHelper.ListOwnedBy(accountName(pid))
    local property = owned[1]
    if not property then
        tes3mp.SendMessage(pid, CONTROL_PREFIX .. "HOME\t0\n", false)
        return
    end

    local info = describeProperty(pid, property) .. "\n" ..
        tr(pid, "Название: ", "Title: ") .. tostring(property.title) .. "\n" ..
        tr(pid, "Режим: ", "Mode: ") .. (property.mode == "original" and tr(pid,"Исходная ячейка","Original cell") or tr(pid,"Инстанс","Instance")) .. "\n" ..
        tr(pid, "Помещений: ", "Rooms: ") .. tostring(#property.cells) .. "\n" ..
        tr(pid, "Предметы для гостей: ", "Guest items: ") ..
        (property.allowTake and tr(pid, "можно брать", "may be taken")
            or tr(pid, "брать нельзя; книги и записки можно читать",
                   "may not be taken; books and notes stay readable"))

    local infoLines = {}
    for line in (info .. "\n"):gmatch("(.-)\n") do
        table.insert(infoLines, escapeField(line))
    end

    local fields = {
        "HOME", "1",
        escapeField(property.title),
        escapeField(kindName(pid, property.kind)),
        escapeField(property.cell),
        tostring(#property.cells),
        property.locked and "1" or "0",
        property.allowTake and "1" or "0",
        escapeField(table.concat(property.coOwners, ", ")),
        escapeField(table.concat(property.friends, ", ")),
        table.concat(infoLines, "\\n")
    }
    tes3mp.SendMessage(pid, CONTROL_PREFIX .. table.concat(fields, "\t") .. "\n", false)
end

-- ===========================================================================
-- КОМАНДЫ ИГРОКА
-- ===========================================================================

-- Z005: кнопки вкладки «Мой дом» шлют обычные команды, и владельцу больше не
-- нужно стоять внутри своего дома, чтобы им управлять: если он не в своей
-- ячейке, берём его единственный объект (лимит — один на игрока).
local function manageProperty(pid)
    local property = currentProperty(pid)
    if property and houseHelper.GetAccessLevel(pid, property) == "owner" then return property end
    return houseHelper.ListOwnedBy(accountName(pid))[1]
end

-- Ввод текста остаётся диалогом — это единственное, чего страница меню не умеет.
local promptHome

local function commandHouse(pid, cmd)
    if Players[pid] == nil or not Players[pid]:IsLoggedIn() then return end

    local sub = lower(cmd[2] or "")

    if sub == "" or sub == "menu" then houseHelper.ShowHomeMenu(pid); return end

    -- Z005: страница «Мой дом» просит состояние при открытии вкладки.
    if sub == "state" then houseHelper.SendHomeState(pid); return end

    if sub == "list" then
        local owned = houseHelper.ListOwnedBy(accountName(pid))
        if #owned == 0 then
            say(pid, tr(pid, "У вас нет недвижимости. Подойдите к двери свободного дома и удерживайте «Взять».",
                "You own no property. Walk up to a vacant house door and hold Take."))
            return
        end
        local lines = { tr(pid, "ВАША НЕДВИЖИМОСТЬ", "YOUR PROPERTY") }
        for _, property in ipairs(owned) do
            table.insert(lines, " - " .. property.cell .. " (" .. kindName(pid, property.kind) .. ")")
        end
        say(pid, table.concat(lines, "\n"))
        return
    end

    if sub == "info" then
        local property = currentProperty(pid)
        if property == nil then
            say(pid, tr(pid, "Эта ячейка не числится недвижимостью.", "This cell is not registered property."))
            return
        end
        say(pid, describeProperty(pid, property))
        return
    end

    if sub == "lock" or sub == "unlock" or sub == "togglelock" then
        local property = manageProperty(pid)
        if sub == "togglelock" and property ~= nil then
            sub = property.locked and "unlock" or "lock"
        end
        if property == nil or houseHelper.GetAccessLevel(pid, property) ~= "owner" then
            say(pid, tr(pid, "Это может сделать только владелец, находясь внутри.",
                "Only the owner can do this, from inside."))
            return
        end
        property.locked = (sub == "lock")
        saveStore(true)
        houseHelper.RefreshAll()
        say(pid, property.locked
            and tr(pid, "Закрыто. Войти смогут только друзья и совладельцы.",
                "Locked. Only friends and co-owners can enter.")
            or tr(pid, "Открыто для всех.", "Open to everyone."))
        return
    end

    if sub == "invite" or sub == "friend" or sub == "coowner" or sub == "remove" then
        local property = currentProperty(pid)
        if property == nil or houseHelper.GetAccessLevel(pid, property) ~= "owner" then
            say(pid, tr(pid, "Это может сделать только владелец.", "Only the owner can do this."))
            return
        end
        local targetName = tableHelper.concatenateFromIndex(cmd, 3)
        if targetName == nil or targetName == "" then
            say(pid, tr(pid, "Использование: /house invite|coowner|remove <имя>",
                "Usage: /house invite|coowner|remove <name>"))
            return
        end
        targetName = tostring(targetName):gsub('"', '')

        if sub == "remove" then
            for _, list in ipairs({property.friends, property.coOwners}) do
                for i = #list, 1, -1 do
                    if lower(list[i]) == lower(targetName) then table.remove(list, i) end
                end
            end
            say(pid, tr(pid, "Доступ отозван: ", "Access revoked: ") .. targetName)
        elseif sub == "coowner" then
            if not nameInList(property.coOwners, targetName) then
                table.insert(property.coOwners, targetName)
            end
            say(pid, tr(pid, "Совладелец добавлен: ", "Co-owner added: ") .. targetName)
        else
            if not nameInList(property.friends, targetName) then
                table.insert(property.friends, targetName)
            end
            say(pid, tr(pid, "Друг добавлен: ", "Friend added: ") .. targetName)
        end
        saveStore(true)
        return
    end

    if sub == "sell" then
        local property = manageProperty(pid)
        if property == nil then
            say(pid, tr(pid, "Встаньте внутри объекта, который хотите продать.",
                "Stand inside the property you want to sell."))
            return
        end
        local ok, message = houseHelper.Sell(pid, property.cell)
        say(pid, message)
        return
    end

    if sub == "buy" then
        local cellDescription = tes3mp.GetCell(pid)
        local property = houseHelper.Observe(cellDescription, pid)
        if property == nil then
            say(pid, tr(pid, "Эта ячейка не продаётся.", "This cell is not for sale."))
            return
        end
        houseHelper.ShowBuyConfirmation(pid, property)
        return
    end

    if sub == "reset" then
        local property = manageProperty(pid)
        if property == nil or houseHelper.GetAccessLevel(pid, property) ~= "owner" then
            say(pid, tr(pid, "Это может сделать только владелец.", "Only the owner can do this."))
            return
        end
        local success = true
        for _, member in ipairs(property.cells) do
            success = houseHelper.ResetInstance(houseHelper.GetInstanceDescription(member, property.owner)) and success
        end
        if success then
            say(pid, tr(pid, "Ваш дом очищен. Войдите заново.", "Your home was cleared. Enter it again."))
        else
            say(pid, tr(pid, "Не удалось очистить дом.", "The home could not be cleared."))
        end
        return
    end

    -- Z005: команды вкладки «Мой дом».
    if sub == "rename" or sub == "gift" or sub == "addcoowner" or sub == "delcoowner"
        or sub == "addfriend" or sub == "delfriend" or sub == "revoke" then
        local property = manageProperty(pid)
        if property == nil or houseHelper.GetAccessLevel(pid, property) ~= "owner" then
            say(pid, tr(pid, "У вас нет дома.", "You own no property."))
            return
        end
        local actions = {rename = 0, addcoowner = 2, delcoowner = 3,
            addfriend = 4, delfriend = 5, revoke = 6, gift = 11}
        promptHome(pid, property, actions[sub])
        return
    end

    if sub == "take" then
        local property = manageProperty(pid)
        if property == nil or houseHelper.GetAccessLevel(pid, property) ~= "owner" then
            say(pid, tr(pid, "У вас нет дома.", "You own no property."))
            return
        end
        property.allowTake = not property.allowTake
        saveStore(true)
        houseHelper.RefreshAll()
        say(pid, property.allowTake
            and tr(pid, "Гости могут брать предметы.", "Guests may take items.")
            or tr(pid, "Гости не могут брать предметы.", "Guests may not take items."))
        return
    end

    if sub == "kind" then
        local property = manageProperty(pid)
        if property == nil or houseHelper.GetAccessLevel(pid, property) ~= "owner" then
            say(pid, tr(pid, "У вас нет дома.", "You own no property."))
            return
        end
        local wanted = lower(cmd[3] or "")
        local known = {house = true, tavern = true, guild = true, shop = true}
        if not known[wanted] then
            say(pid, tr(pid, "Использование: /house kind house|tavern|guild|shop",
                "Usage: /house kind house|tavern|guild|shop"))
            return
        end
        property.kind = wanted
        saveStore(true)
        houseHelper.RefreshAll()
        say(pid, tr(pid, "Статус изменён: ", "Kind changed: ") .. kindName(pid, wanted))
        return
    end

    say(pid, tr(pid,
        "Использование: /house [list|info|buy|sell|lock|unlock|invite <имя>|coowner <имя>|remove <имя>|reset]",
        "Usage: /house [list|info|buy|sell|lock|unlock|invite <name>|coowner <name>|remove <name>|reset]"))
end

-- ===========================================================================
-- СОБЫТИЯ
-- ===========================================================================
-- Z002: one property record owns all rooms; the instance identity survives a gift.
local HOME_MENU, HOME_INPUT, HOME_GIFT = 29310, 29311, 29312
local homeSessions = {}

-- Multi-room property matching.  We only merge cells that are known to be
-- connected by a door (runtime caller / saved door topology) AND whose names
-- describe the same building.  This avoids merging neighbouring independent
-- homes while still handling "Manor" + "Manor: Basement" and similar layouts.
local LINKED_ROOM_MARKERS = {
    "basement", "cellar", "wine cellar", "library", "study", "private study",
    "hall", "bedroom", "bedchamber", "kitchen", "storage", "storeroom",
    "attic", "loft", "lower level", "lower levels", "upper level", "upper levels",
    "ground floor", "first floor", "second floor",
    "подвал", "погреб", "винный погреб", "библиотек", "кабинет", "зал",
    "спальн", "кухн", "кладов", "чердак", "нижн", "верхн", "этаж"
}

local function trim(value)
    return tostring(value or ""):match("^%s*(.-)%s*$")
end

local function linkedRoomSuffix(value)
    value = lower(trim(value))
    for _, marker in ipairs(LINKED_ROOM_MARKERS) do
        if value == marker or value:find(marker, 1, true) ~= nil then return true end
    end
    return false
end

local function splitLinkedRoomName(cellDescription)
    local value = trim(houseHelper.GetBaseCellForInstance(cellDescription) or cellDescription)
    local low = lower(value)

    local colon = value:match("^(.-)%s*:%s*(.-)%s*$")
    if colon then
        local prefix, suffix = value:match("^(.-)%s*:%s*(.-)%s*$")
        if prefix and suffix and linkedRoomSuffix(suffix) then return trim(prefix), true end
    end

    -- Suffix after the final comma: "Vendicci Manor, Basement".
    local prefix, suffix = value:match("^(.*),%s*([^,]+)%s*$")
    if prefix and suffix and linkedRoomSuffix(suffix) then return trim(prefix), true end

    -- A few mods omit punctuation: "Yngling Manor Basement".
    for _, marker in ipairs({"basement", "cellar", "подвал", "погреб"}) do
        local markerLow = lower(marker)
        if #low > #markerLow + 1 and low:sub(-#markerLow) == markerLow then
            return trim(value:sub(1, #value - #marker)), true
        end
    end

    return value, false
end

function houseHelper.IsLikelyLinkedRoom(firstCell, secondCell)
    if type(firstCell) ~= "string" or type(secondCell) ~= "string" or
        firstCell == "" or secondCell == "" then return false end
    if houseHelper.IsExteriorCell(firstCell) or houseHelper.IsExteriorCell(secondCell) then return false end
    firstCell = houseHelper.GetBaseCellForInstance(firstCell) or firstCell
    secondCell = houseHelper.GetBaseCellForInstance(secondCell) or secondCell
    if lower(firstCell) == lower(secondCell) then return true end

    local firstStem, firstSub = splitLinkedRoomName(firstCell)
    local secondStem, secondSub = splitLinkedRoomName(secondCell)
    if lower(firstStem) == lower(secondStem) and (firstSub or secondSub) then return true end

    -- Strong parent-child form: "Sandil House" -> "Sandil House: Library".
    local a, b = lower(trim(firstCell)), lower(trim(secondCell))
    if b:sub(1, #a + 1) == a .. ":" and linkedRoomSuffix(b:sub(#a + 2)) then return true end
    if a:sub(1, #b + 1) == b .. ":" and linkedRoomSuffix(a:sub(#b + 2)) then return true end
    return false
end

local function preferredLinkedRoot(firstCell, secondCell)
    local firstStem, firstSub = splitLinkedRoomName(firstCell)
    local secondStem, secondSub = splitLinkedRoomName(secondCell)
    if firstSub and lower(firstStem) == lower(secondCell) then return secondCell end
    if secondSub and lower(secondStem) == lower(firstCell) then return firstCell end
    if firstSub ~= secondSub then return firstSub and secondCell or firstCell end
    return #tostring(firstCell) <= #tostring(secondCell) and firstCell or secondCell
end

local function hasCell(cells, wanted)
    for _, member in ipairs(type(cells) == "table" and cells or {}) do
        if lower(member) == lower(wanted) then return true end
    end
    return false
end

function houseHelper.LinkCell(mainCell, memberCell)
    local property = houseHelper.GetProperty(mainCell)
    if not property or type(memberCell) ~= "string" or memberCell == "" or
        houseHelper.IsInstanceCell(memberCell) or memberCell:match("^%-?%d+, %-?%d+$") then
        return false, "Укажите основной дом и интерьер."
    end
    local other = houseHelper.GetProperty(memberCell)
    if other == property then return true, "Ячейка уже привязана." end
    if other and other.owner and other.owner ~= "" and
        (not property.owner or lower(other.owner) ~= lower(property.owner)) then
        return false, "Ячейка принадлежит другому объекту с владельцем; объединение отменено."
    end
    if other and other.mode ~= property.mode then return false, "Сначала задайте одинаковый режим помещений." end

    local members = other and other.cells or {memberCell}
    for _, member in ipairs(members) do
        if not hasCell(property.cells, member) then table.insert(property.cells, member) end
    end
    if other then loadStore().properties[other.cell] = nil end

    -- A free automatically priced property should reflect all of its rooms.
    -- Purchased homes keep their historical paid price when a missed basement
    -- is discovered later; that room was always part of the same building.
    if (not property.owner or property.owner == "") and property.priceMode ~= "manual" then
        local total = 0
        for _, member in ipairs(property.cells) do
            total = total + houseHelper.CalculatePrice(member, property.kind)
        end
        property.price, property.priceMode = total, "auto"
    end

    saveStore(true)
    houseHelper.RefreshAll()
    return true, "Помещения объединены в один дом."
end

function houseHelper.TryAutoLink(firstCell, secondCell)
    if not houseHelper.IsLikelyLinkedRoom(firstCell, secondCell) then return false end
    local firstProperty = houseHelper.GetProperty(firstCell)
    local secondProperty = houseHelper.GetProperty(secondCell)
    if not firstProperty and not secondProperty then return false end
    if firstProperty and secondProperty and firstProperty == secondProperty then return true end

    local rootCell = preferredLinkedRoot(firstCell, secondCell)
    local rootProperty = houseHelper.GetProperty(rootCell)
    local memberCell

    -- Prefer the already-owned side as the canonical property if only one side
    -- has an owner, even if a mod used an unusual room naming convention.
    if firstProperty and secondProperty then
        local firstOwned = type(firstProperty.owner) == "string" and firstProperty.owner ~= ""
        local secondOwned = type(secondProperty.owner) == "string" and secondProperty.owner ~= ""
        if firstOwned ~= secondOwned then
            rootProperty = firstOwned and firstProperty or secondProperty
            memberCell = firstOwned and secondCell or firstCell
        end
    end

    if not rootProperty then rootProperty = firstProperty or secondProperty end
    if not rootProperty then return false end
    if not memberCell then
        memberCell = (houseHelper.GetProperty(firstCell) == rootProperty) and secondCell or firstCell
    end

    local ok, message = houseHelper.LinkCell(rootProperty.cell, memberCell)
    if ok then
        log(enumerations.log.INFO, "автосвязь помещений: " .. rootProperty.cell .. " + " .. memberCell)
        return true
    end
    return false, message
end

function houseHelper.ReconcileLinkedRooms()
    local data = loadStore()
    local pairsToCheck = {}
    for destination, origins in pairs(data.doors or {}) do
        if type(origins) == "table" then
            for _, origin in pairs(origins) do
                local baseOrigin = houseHelper.GetBaseCellForInstance(origin) or origin
                local baseDestination = houseHelper.GetBaseCellForInstance(destination) or destination
                if type(baseOrigin) == "string" and type(baseDestination) == "string" and
                    baseOrigin ~= baseDestination then
                    table.insert(pairsToCheck, {baseOrigin, baseDestination})
                end
            end
        end
    end
    local merged = 0
    for _, pair in ipairs(pairsToCheck) do
        local a, b = pair[1], pair[2]
        local pa, pb = houseHelper.GetProperty(a), houseHelper.GetProperty(b)
        if (pa or pb) and pa ~= pb and houseHelper.IsLikelyLinkedRoom(a, b) then
            local ok = houseHelper.TryAutoLink(a, b)
            if ok then merged = merged + 1 end
        end
    end
    return merged
end

function houseHelper.RefreshAll()
    for pid, player in pairs(Players) do
        if player:IsLoggedIn() then
            houseHelper.SendClientState(pid)
            local cell = tes3mp.GetCell(pid)
            local normal = houseHelper.GetProperty(cell)
            if normal and normal.mode == "original" and normal.owner and normal.owner ~= "" and
                houseHelper.GetAccessLevel(pid, normal) == "none" then houseHelper.EvictOriginalVisitor(pid, normal) end
            local base = houseHelper.GetBaseCellForInstance(cell)
            local home = base and houseHelper.GetProperty(base)
            if base and (not home or houseHelper.GetAccessLevel(pid, home) == "none") then
                warp(pid, base, player.data.location)
            end
        end
    end
end


function houseHelper.CanTake(pid, cell)
    local property = instanceProperty(cell)
    if not property and not houseHelper.IsInstanceCell(cell) then return true end
    if not property then return false end
    local level = houseHelper.GetAccessLevel(pid, property)
    return level == "owner" or level == "coowner" or (level ~= "none" and property.allowTake)
end

function houseHelper.CanUseUtility(pid, cell)
    local property = instanceProperty(cell)
    if not property and not houseHelper.IsInstanceCell(cell) then return true end
    if not property then return false end
    local level = houseHelper.GetAccessLevel(pid, property)
    -- Y070: owner/co-owner/friend are trusted. Other admitted visitors follow
    -- the house-wide allowTake switch for beds and containers as well.
    return level == "owner" or level == "coowner" or level == "friend"
        or (level ~= "none" and property.allowTake)
end

local function cleanName(value)
    return tostring(value or ""):gsub('[\r\n\t%^;#{}]', ''):match('^%s*(.-)%s*$'):sub(1, 120)
end

function houseHelper.Gift(pid, property, targetPid)
    local target = Players[targetPid]
    if not property or houseHelper.GetAccessLevel(pid, property) ~= "owner" or
        not target or not target:IsLoggedIn() then return false, "Получатель должен быть в игре." end
    local name = accountName(targetPid)
    if lower(name) == lower(property.owner) or #houseHelper.ListOwnedBy(name) > 0 then
        return false, "У получателя уже есть дом."
    end
    property.instanceOwner = property.instanceOwner or property.owner
    property.owner = name
    property.coOwners, property.friends = {}, {}
    property.locked = true
    saveStore(true)
    houseHelper.RefreshAll()
    say(targetPid, "Вам подарен дом «" .. property.title .. "».")
    return true, "Дом подарен игроку " .. name .. ". Содержимое помещений сохранено."
end

-- Z005: страница меню просит сервер открыть только поле ввода. Сессия помечена
-- как "из меню", поэтому после ввода мы не выкидываем игрока в старый ListBox,
-- а просто обновляем страницу.
promptHome = function(pid, property, action)
    if action == nil then return end
    homeSessions[pid] = {cell = property.cell, action = action, fromPane = true}
    local prompt
    if action == 0 then prompt = tr(pid, "Название дома", "Property title")
    elseif action == 11 then prompt = tr(pid, "Точное имя игрока в сети — получателя подарка",
        "Exact online name of the player receiving the gift")
    elseif action == 3 or action == 5 or action == 6 then
        prompt = tr(pid, "Имя аккаунта: убрать доступ", "Account name: revoke access")
    else prompt = tr(pid, "Имя аккаунта игрока", "Player account name") end
    tes3mp.InputDialog(pid, HOME_INPUT, prompt, "")
end

function houseHelper.ShowHomeMenu(pid)
    local owned = houseHelper.ListOwnedBy(accountName(pid))
    local property = owned[1]
    if not property then say(pid, "У вас нет дома."); return end
    homeSessions[pid] = {cell = property.cell}
    local info = describeProperty(pid, property) .. "\nНазвание: " .. property.title ..
        "\nПомещений: " .. #property.cells .. "\nПредметы для гостей: " ..
        (property.allowTake and "можно брать" or "брать нельзя; контейнеры и кровати гостям недоступны; книги можно читать")
    tes3mp.ListBox(pid, HOME_MENU, info,
        "Изменить название\n" .. (property.locked and "Открыть всем" or "Только друзья и совладельцы") ..
        "\nДобавить совладельца\nУбрать совладельца\nДобавить друга\nУбрать друга\n" ..
        "Статус: дом\nСтатус: таверна\nСтатус: гильдия\nСтатус: магазин\n" ..
        (property.allowTake and "Запретить гостям брать предметы" or "Разрешить гостям брать предметы") ..
        "\nПодарить дом\nЗакрыть")
end

customEventHooks.registerHandler("OnGUIAction", function(eventStatus, pid, id, data)
    if id ~= HOME_MENU and id ~= HOME_INPUT and id ~= HOME_GIFT then return end
    local session = homeSessions[pid]
    local property = session and houseHelper.GetProperty(session.cell)
    if not property or houseHelper.GetAccessLevel(pid, property) ~= "owner" then
        homeSessions[pid] = nil; return
    end
    if id == HOME_MENU then
        local index = tonumber(data)
        if index == 0 or (index and index >= 2 and index <= 5) or index == 11 then
            session.action = index
            tes3mp.InputDialog(pid, HOME_INPUT, index == 0 and "Название дома" or
                (index == 11 and "Точное имя игрока в сети — получателя подарка" or "Имя аккаунта игрока"), "")
            return
        elseif index == 1 then property.locked = not property.locked
        elseif index and index >= 6 and index <= 9 then
            property.kind = ({"house", "tavern", "guild", "shop"})[index - 5]
        elseif index == 10 then property.allowTake = not property.allowTake
        else homeSessions[pid] = nil; return end
    elseif id == HOME_INPUT then
        local name = cleanName(data)
        if name == "" then
            if session.fromPane then homeSessions[pid] = nil; return end
            houseHelper.ShowHomeMenu(pid)
            return
        end
        local action = session.action
        session.action = nil
        if action == 0 then property.title = name
        elseif action == 11 then
            for targetPid, player in pairs(Players) do
                if player:IsLoggedIn() and lower(accountName(targetPid)) == lower(name) then
                    session.targetPid, session.targetName = targetPid, accountName(targetPid)
                    tes3mp.CustomMessageBox(pid, HOME_GIFT,
                        "Подарить «" .. property.title .. "» игроку " .. session.targetName ..
                        "? Вы потеряете права владельца. Друзья и совладельцы будут удалены.", "Подарить;Отмена")
                    return
                end
            end
            say(pid, "Игрок с таким именем не найден в сети.")
        elseif action == 6 then
            -- Z005: «Убрать доступ» на странице меню не спрашивает, кем был
            -- игрок: имя вычищается и из совладельцев, и из друзей.
            for _, list in ipairs({property.coOwners, property.friends}) do
                for i = #list, 1, -1 do
                    if lower(list[i]) == lower(name) then table.remove(list, i) end
                end
            end
        elseif action and action >= 2 and action <= 5 then
            local list = action <= 3 and property.coOwners or property.friends
            if action == 3 or action == 5 then
                for i = #list, 1, -1 do if lower(list[i]) == lower(name) then table.remove(list, i) end end
            elseif lower(name) ~= lower(property.owner) and not nameInList(list, name) then
                table.insert(list, name)
            end
        else return end
    else
        if tonumber(data) == 0 and session.targetPid and
            accountName(session.targetPid) == session.targetName then
            local ok, message = houseHelper.Gift(pid, property, session.targetPid)
            say(pid, message)
            homeSessions[pid] = nil
            if ok then return end
        end
    end
    saveStore(true)
    houseHelper.RefreshAll()
    if session.fromPane then
        homeSessions[pid] = nil
        return
    end
    houseHelper.ShowHomeMenu(pid)
end)

-- Packet guards complement client UX. Books/notes remain readable, but when
-- taking is forbidden a container itself must not open. The updated client
-- blocks every container before sending ObjectActivate; this validator is a
-- server-side fallback for older/modified clients once the container is known.
customEventHooks.registerValidator("OnObjectActivate", function(eventStatus, pid, cell, objects)
    if houseHelper.CanUseUtility(pid, cell) then return end
    local loaded = LoadedCells and LoadedCells[cell] or nil
    local data = loaded and type(loaded.data) == "table" and loaded.data or nil
    local packets = data and data.packets or {}
    local containerPackets = type(packets.container) == "table" and packets.container or {}

    for uniqueIndex, object in pairs(objects or {}) do
        local objectData = data and data.objectData and data.objectData[uniqueIndex] or nil
        local refId = lower((type(object) == "table" and object.refId)
            or (type(objectData) == "table" and objectData.refId) or "")
        local script = lower((type(objectData) == "table" and objectData.script) or "")
        local isContainer = tableHelper.containsValue(containerPackets, uniqueIndex)
            or (type(objectData) == "table" and type(objectData.inventory) == "table")
        -- Y070: beds are usually scripted activators and do not generate an
        -- OnContainer packet. Reject them at activation too. The refId fallback
        -- covers base CELL references whose objectData script was never persisted.
        local isBed = script:sub(1, 3) == "bed" or refId:find("bed", 1, true) ~= nil
        if isContainer or isBed then
            say(pid, isBed
                and tr(pid, "Владелец запретил пользоваться кроватями в этом доме.",
                    "The owner has disabled bed use in this home.")
                or tr(pid, "Владелец запретил открывать контейнеры в этом доме.",
                    "The owner has disabled container access in this home."))
            return customEventHooks.makeEventStatus(false, false)
        end
    end
end)

-- Packet guards complement client UX. Books are activated normally, only removal is denied.
customEventHooks.registerHandler("OnObjectDelete", function(eventStatus, pid, cell, objects)
    if not eventStatus.validDefaultHandler or not pendingPurge[cell] then return end
    purgeDeletes[cell] = purgeDeletes[cell] or {}
    local accepted = purgeDeletes[cell][pid] or {}
    purgeDeletes[cell][pid] = accepted
    for index, object in pairs(objects or {}) do
        if houseHelper.IsPurgeDelete(pid, cell, index, object) then accepted[index] = true end
    end
    houseHelper.TryFinalizePurge(cell)
end)
customEventHooks.registerValidator("OnObjectDelete", function(eventStatus, pid, cell, objects)
    if houseHelper.IsPurgePending(cell) then
        for index, object in pairs(objects or {}) do
            if not houseHelper.IsPurgeDelete(pid, cell, index, object) then
                return customEventHooks.makeEventStatus(false, false)
            end
        end
        return
    end
    if not houseHelper.CanTake(pid, cell) then
        -- Y089: FLGBedrollMiscScript performs a legitimate cleanup delete after
        -- it has disabled a dropped flg_bedroll01misc and created the matching
        -- activator. The generic no-take house rule used to reject that delete,
        -- leaving the spent dynamic source in packets.place. Replaying the home
        -- then ran the conversion script again and duplicated the bedroll.
        -- Allow only this already-disabled, dynamic implementation object; all
        -- ordinary item deletion remains forbidden.
        local loaded = LoadedCells and LoadedCells[cell] or nil
        local objectData = loaded and loaded.data and loaded.data.objectData or nil
        for index, object in pairs(objects or {}) do
            local stored = type(objectData) == "table" and objectData[index] or nil
            local refId = lower((type(object) == "table" and object.refId)
                or (type(stored) == "table" and stored.refId) or "")
            local isSpentBedroll = dynamicMpNum(index) ~= nil and refId == BEDROLL_MISC_REF and
                type(stored) == "table" and stored.state == false
            if not isSpentBedroll then
                return customEventHooks.makeEventStatus(false, false)
            end
        end
        return
    end
end)
customEventHooks.registerValidator("OnContainer", function(eventStatus, pid, cell)
    if houseHelper.IsPurgePending(cell) and
        tes3mp.GetObjectListContainerSubAction() ~= enumerations.containerSub.REPLY_TO_REQUEST then
        return customEventHooks.makeEventStatus(false, false)
    end
    if tes3mp.GetObjectListContainerSubAction() == enumerations.containerSub.REPLY_TO_REQUEST then return end
    if not houseHelper.CanUseUtility(pid, cell) then
        say(pid, tr(pid, "Владелец запретил открывать контейнеры в этом доме.",
            "The owner has disabled container access in this home."))
        return customEventHooks.makeEventStatus(false, false)
    end
end)
customEventHooks.registerValidator("OnPlayerCellChange", function(eventStatus, pid, packet)
    local cell = packet and packet.location and packet.location.cell
    if not cell then return end
    local property = instanceProperty(cell)
    if property and property.mode == "original" and property.owner and property.owner ~= "" then
        if houseHelper.GetAccessLevel(pid, property) == "none" then
            houseHelper.EvictOriginalVisitor(pid, property)
            return customEventHooks.makeEventStatus(false, false)
        end
        return
    end
    if not houseHelper.IsInstanceCell(cell) then return end
    if not property or houseHelper.GetAccessLevel(pid, property) == "none" then
        local base = houseHelper.GetBaseCellForInstance(cell)
        warp(pid, base, packet.location)
        return customEventHooks.makeEventStatus(false, false)
    end
end)
customEventHooks.registerHandler("OnPlayerDisconnect", function(eventStatus, pid)
    homeSessions[pid], pendingChoice[pid] = nil, nil
    clientInterests[pid] = nil
    equipmentTransitionGuards[pid] = nil
    for cell, receipts in pairs(purgeDeletes) do
        receipts[pid] = nil
        local ack = pendingPurgeAcknowledged[cell]
        if ack and ack.pid == pid then pendingPurgeAcknowledged[cell] = nil end
    end
    -- Z012: новая сессия — новый клиент, запись инстанса придётся отдать снова.
    houseHelper.ForgetInstanceRecords(pid)
end)
-- Admin-only bulk registration of suitable HOME interiors from the active
-- masters. Never trust a client-provided cell name as proof that it exists.
-- /addhome                 add all new residential CELL records
-- /addhome preview         show up to 15 candidates, do not change the registry
-- /addhome <name fragment>  add only matching residential CELL records
-- /addhome preview <text>  preview only matching names
local function commandAddHome(pid, cmd)
    if not houseHelper.IsStaff(pid) then
        say(pid, tr(pid,"Команда доступна только администратору.",
            "This command requires administrator access."))
        return
    end
    if not cfg().enabled then
        say(pid, tr(pid,"Система домов отключена в config.lua.",
            "Housing is disabled in config.lua."))
        return
    end
    local preview=(cmd[2] or ""):lower()=="preview" or (cmd[2] or ""):lower()=="list"
    local first=preview and 3 or 2
    local filter=lower(table.concat(cmd," ",first)):gsub("^%s+",""):gsub("%s+$","")
    local ok, candidates, scanErr, masters=pcall(function()
        return require("arenaFurnitureAutoCatalog").findActiveEsmInteriors(config,jsonInterface)
    end)
    if not ok or not candidates then
        local reason=ok and scanErr or candidates
        say(pid,tr(pid,"Поиск домов в ESM не выполнен: ",
            "ESM home scan failed: ")..tostring(reason).."\n"..
            tr(pid,"Проверьте requiredDataFiles.json и furnitureDataDirs / arenaFurnitureSources.json.",
               "Check requiredDataFiles.json and furnitureDataDirs / arenaFurnitureSources.json."))
        return
    end
    local found,added,already,shown=0,0,0,0
    local names={}
    for _, candidate in ipairs(candidates) do
        local name=candidate.name
        -- Only residential houses: no taverns, shops, guilds, exterior cells,
        -- instance names, caves or other rejected cell-name classifications.
        if houseHelper.ClassifyCellName(name)=="house" and
            (filter=="" or lower(name):find(filter,1,true)) then
            found=found+1
            local existing=houseHelper.GetProperty(name)
            if existing then
                already=already+1
            elseif preview then
                if shown<15 then
                    shown=shown+1
                    names[#names+1]=name.." ["..candidate.plugin.."]"
                end
            else
                -- ESM cells need not have a persisted server cell JSON yet.
                -- Use the name-based price without probing missing cell files:
                -- io2 would otherwise print an error for every unvisited home.
                local registered=houseHelper.Register(name,"house",nil,false,pid,true)
                if registered then
                    registered.esmSource=candidate.plugin
                    added=added+1
                end
            end
        end
    end
    if added>0 then
        houseHelper.FlushStore()
        houseHelper.RefreshAll()
    end
    local summary=tr(pid,"/addhome: ESM ","/addhome: ESM ")..
        tostring(#(masters or {}))..tr(pid,", найдено домов: ",", matching homes: ")..
        found..tr(pid,", уже в базе: ",", already registered: ")..already
    if preview then
        say(pid,summary..tr(pid,". Предпросмотр без изменений.",". Preview only.")..
            (#names>0 and "\n"..table.concat(names,"\n") or "")..
            (found-already>shown and tr(pid,"\n... ещё ","\n... and ")..
                tostring(found-already-shown)..tr(pid," объектов."," more.") or ""))
    else
        say(pid,summary..tr(pid,", добавлено: ",", added: ")..tostring(added)..".")
    end
end
customCommandHooks.registerCommand("addhome", commandAddHome)

customCommandHooks.registerCommand("houselink", function(pid, cmd)
    if not isStaff(pid) then return end
    local text = tableHelper.concatenateFromIndex(cmd, 2) or ""
    local main, member = text:match("^%s*(.-)%s*|%s*(.-)%s*$")
    if not main or not member then say(pid, " /houselink Основная ячейка | Дополнительная ячейка"); return end
    local ok, message = houseHelper.LinkCell(main, member)
    say(pid, message)
end)


-- H019a: the server stores exterior cells as "x, y", while older clients
-- may send their localized OpenMW UI description "Region (x, y)". Accept such
-- a request only when its grid coordinates match the authoritative current cell.
-- Interior names continue to require an exact match.
local function matchesInterestCell(clientCell, serverCell)
    if type(clientCell) ~= "string" or type(serverCell) ~= "string" then return false end
    if lower(clientCell) == lower(serverCell) then return true end
    local sx, sy = serverCell:match("^%s*(-?%d+)%s*,%s*(-?%d+)%s*$")
    if not sx then return false end
    local cx, cy = clientCell:match("%(%s*(-?%d+)%s*,%s*(-?%d+)%s*%)%s*$")
    return cx ~= nil and tonumber(cx) == tonumber(sx) and tonumber(cy) == tonumber(sy)
end

-- Клиент просит решение по двери. Формат управляющего сообщения:
--   @@AMP_HOUSE@@DOOR \t <ячейка> \t <hold 0|1> \t x \t y \t z \t rotX \t rotZ
customEventHooks.registerValidator("OnPlayerSendMessage", function(eventStatus, pid, message)
    if type(message) ~= "string" then return end
    if string.sub(message, 1, #CONTROL_PREFIX) ~= CONTROL_PREFIX then return end

    local payload = string.sub(message, #CONTROL_PREFIX + 1)
    local fields = payload:split("\t")
    local verb = fields[1]

    if verb == "INTEREST" then
        local player = Players[pid]
        local current = tes3mp.GetCell(pid)
        if player and player:IsLoggedIn() and type(current) == "string"
            and matchesInterestCell(fields[2], current) and #message <= 13000 then
            local key = lower(current)
            local state = clientInterests[pid]
            if not state or state.cell ~= key then
                state = {cell=key, destinations={}, count=0, second=0, requests=0, sent=false}
                clientInterests[pid] = state
            end
            local now = os.time()
            if state.second ~= now then state.second=now; state.requests=0 end
            if state.requests < 8 then
                state.requests = state.requests + 1
                local changed=not state.sent
                for i=3,math.min(#fields,259) do
                    local destination = fields[i]
                    if #destination <= 512 and not destination:find("[%c]") then
                        local destinationKey = lower(destination)
                        if not state.destinations[destinationKey] and state.count < 512 then
                            state.destinations[destinationKey] = true
                            state.count = state.count + 1
                            changed=true
                        end
                    end
                end
                -- Acknowledge retries even when no snapshot work is necessary.
                tes3mp.SendMessage(pid, CONTROL_PREFIX .. "CELL_ACK\t" .. escapeField(fields[2]) .. "\n", false)
                if changed then
                    houseHelper.SendClientState(pid)
                    state.sent=true
                end
            end
        end
    elseif verb == "DOOR" then
        local destinationCell = fields[2]
        local hold = fields[3] == "1"
        local position = nil
        if fields[4] ~= nil and fields[5] ~= nil and fields[6] ~= nil then
            position = {
                posX = tonumber(fields[4]), posY = tonumber(fields[5]), posZ = tonumber(fields[6]),
                rotX = tonumber(fields[7]) or 0, rotZ = tonumber(fields[8]) or 0
            }
        end
        local ok, err = pcall(houseHelper.HandleDoorRequest, pid, destinationCell, hold, position, tonumber(fields[9]) == 1)
        if not ok then
            log(enumerations.log.ERROR, "HandleDoorRequest: " .. tostring(err))
            allowPlainEntry(pid, destinationCell)
        end
    elseif verb == "FURNITURE_UI" then
        local shop = rawget(_G, "arenaFurniture")
        if shop and Players[pid] and Players[pid]:IsLoggedIn() then shop.OpenNative(pid) end
    elseif verb == "FURNITURE_CMD" then
        local shop = rawget(_G, "arenaFurniture")
        if shop and Players[pid] and Players[pid]:IsLoggedIn() then
            local ok, err = pcall(shop.NativeCommand, pid, fields[2], fields[3], fields[4], fields[5])
            if not ok then log(enumerations.log.ERROR, "Furniture command failed: " .. tostring(err)) end
        end
    elseif verb == "FURNITURE" then
        local shop = rawget(_G, "arenaFurniture")
        if shop and Players[pid] and Players[pid]:IsLoggedIn() then shop.Open(pid) end
    elseif verb == "PURGED" then
        -- Клиент отчитался, что вычистил инстанс.
        if fields[2] == tes3mp.GetCell(pid) and houseHelper.IsPurgePending(fields[2]) then
            houseHelper.AcknowledgePurge(fields[2], pid, fields[3])
        end
    elseif verb == "SYNC" then
        houseHelper.SendClientState(pid)
    end

    -- Управляющее сообщение никогда не должно попасть в чат или в разбор команд.
    return customEventHooks.makeEventStatus(false, false)
end)

-- Reject only the characteristic transition failure: a previously equipped
-- player suddenly reporting almost no equipment during the few seconds in which
-- a generated home CELL is being loaded.  Normal one-item equip/unequip changes
-- continue to pass through untouched.
customEventHooks.registerValidator("OnPlayerEquipment", function(eventStatus, pid, playerPacket)
    local guard = equipmentTransitionGuards[pid]
    if not guard then return end
    if os.time() > guard.expires then equipmentTransitionGuards[pid] = nil; return end
    local expected = equippedCount(guard.equipment)
    local incoming = equippedCount(playerPacket and playerPacket.equipment)
    local suspicious = expected > 0 and (incoming == 0 or (expected >= 4 and incoming <= math.floor(expected / 3)))
    if suspicious then
        restoreGuardedEquipment(pid, guard)
        log(enumerations.log.WARN, "отклонён пустой/обрезанный PlayerEquipment при входе в дом для " ..
            tostring(accountName(pid)) .. " (" .. tostring(incoming) .. "/" .. tostring(expected) .. ")")
        return customEventHooks.makeEventStatus(false, false)
    end
end)

customEventHooks.registerHandler("OnPlayerCellChange", function(eventStatus, pid)
    local guard = equipmentTransitionGuards[pid]
    if eventStatus.validDefaultHandler == false then return end
    if not guard or os.time() > guard.expires then return end
    if tes3mp.GetCell(pid) ~= guard.targetCell then return end
    restoreGuardedEquipment(pid, guard)
    tes3mp.StartTimer(tes3mp.CreateTimerEx("HouseHelper_EquipmentRefreshTimer",
        EQUIPMENT_REFRESH_DELAY_MS, "ii", pid, guard.serial))
end)

customEventHooks.registerHandler("OnGUIAction", function(eventStatus, pid, idGui, data)
    if Players[pid] == nil or not Players[pid]:IsLoggedIn() then return end

    if idGui == DOOR_MENU_ID then
        local choice = pendingChoice[pid]
        pendingChoice[pid] = nil
        if type(choice) ~= "table" or type(choice.options) ~= "table" then return end

        local index = (tonumber(data) or -1) + 1
        local option = choice.options[index]
        if option == nil then
            tes3mp.SendMessage(pid, CONTROL_PREFIX .. "CANCEL\n", false)
            -- «Отмена» или закрытие окна: игрок остаётся на месте.
            return
        end

        if option.kind == "settings" then
            local property = houseHelper.GetProperty(choice.destinationCell)
            local options = {}
            for _, mode in ipairs({"instance", "original", "menu"}) do
                table.insert(options, {kind="preference", mode=mode, label=preferenceLabel(pid, mode)})
            end
            table.insert(options, {kind="back", label=tr(pid, "Назад", "Back")})
            showDoorMenu(pid, property, choice.destinationCell, options, choice.position, choice.originalLocked, true)
        elseif option.kind == "preference" or option.kind == "back" then
            if option.kind == "preference" and saveDoorPreference(pid, option.mode) then
                houseHelper.SendClientState(pid)
            end
            -- Changing the setting does not teleport. The player chooses an
            -- entrance now, or closes the menu and uses the new mode next time.
            houseHelper.HandleDoorRequest(pid, choice.destinationCell, true, choice.position, choice.originalLocked)
        elseif option.kind == "origin" then
            if option.blocked then
                tes3mp.SendMessage(pid, CONTROL_PREFIX .. "CANCEL\n", false)
                tes3mp.MessageBox(pid, -1, option.blocked)
            else allowPlainEntry(pid, option.cell) end
        elseif option.kind == "instance" then
            enterInstance(pid, option, choice.position)
        elseif option.kind == "manage" then
            -- Z006: управление живёт во вкладке «Мой дом» меню игрока — дверь
            -- только открывает её, чтобы не было двух разных интерфейсов.
            tes3mp.SendMessage(pid, CONTROL_PREFIX .. "CANCEL\n", false)
            houseHelper.SendHomeState(pid)
            tes3mp.SendMessage(pid, CONTROL_PREFIX .. "MENU\n", false)
        elseif option.kind == "buy" then
            if option.blocked ~= nil then
                tes3mp.SendMessage(pid, CONTROL_PREFIX .. "CANCEL\n", false)
                say(pid, option.blocked)
                return
            end
            local property = houseHelper.GetProperty(option.cell)
            houseHelper.ShowBuyConfirmation(pid, property)
        end

    elseif idGui == BUY_MENU_ID then
        local choice = pendingChoice[pid]
        pendingChoice[pid] = nil
        if type(choice) ~= "table" or choice.buyCell == nil then return end
        if (tonumber(data) or -1) ~= 0 then return end

        local ok, message = houseHelper.Buy(pid, choice.buyCell)
        say(pid, message)
        if ok then
            houseHelper.RefreshAll()
            tes3mp.MessageBox(pid, -1, message)
        end
    end
end)

-- Инстанс наследует актёров оригинальной ячейки. Как только клиент прислал их
-- список, они удаляются — в личном доме NPC не живут.
customEventHooks.registerHandler("OnActorList", function(eventStatus, pid, cellDescription, actors)
    if eventStatus.validDefaultHandler == false then return end
    if not houseHelper.IsInstanceCell(cellDescription) then return end
    pcall(houseHelper.PurgeActors, pid, cellDescription)
end)

-- Сервер узнаёт о новых интерьерах, когда в них кто-то заходит.
customEventHooks.registerHandler("OnPlayerCellChange", function(eventStatus, pid, playerPacket, previousCellDescription)
    if eventStatus.validDefaultHandler == false then return end
    if Players[pid] == nil or not Players[pid]:IsLoggedIn() then return end
    if type(playerPacket) ~= "table" or type(playerPacket.location) ~= "table" then return end

    local cellDescription = playerPacket.location.cell
    if type(cellDescription) ~= "string" then return end

    if houseHelper.IsInstanceCell(cellDescription) then
        -- Z012: клиент пересобрал инстанс из baseId, а ядро второй раз за
        -- сессию данные ячейки не шлёт. Отдаём их сами — иначе всё, что игрок
        -- положил в доме, пропадает при следующем входе.
        houseHelper.MarkForPurge(cellDescription)
        pcall(refreshInstanceCell, pid, cellDescription, 1)
        -- Generated instance cells do not always reach BaseCell:AddVisitor on
        -- the same path as ordinary authored cells.  Make the full-container
        -- request explicit on entry so the purge can finish deterministically.
        local loadedInstance = LoadedCells and LoadedCells[cellDescription]
        if loadedInstance and not loadedInstance:HasFullContainerData() then
            loadedInstance:EnsureFullContainerData(pid)
        end
        if houseHelper.IsPurgePending(cellDescription) then
            tes3mp.SendMessage(pid, CONTROL_PREFIX .. "PURGE\t" .. cellDescription .. "\n", false)
        else
            -- Re-entering a previously purchased instance must unlock its
            -- inherited base containers without repeating the destructive purge.
            tes3mp.SendMessage(pid, CONTROL_PREFIX .. "UNLOCK_HOME\t" .. cellDescription .. "\n", false)
        end
        return
    end

    if houseHelper.IsExteriorCell(cellDescription) then
        houseHelper.SendClientState(pid)
        return
    end

    -- Ленивая регистрация: сервер узнаёт об интерьере, когда в него заходят.
    -- Z008: и только. Всплывающее «этот дом продаётся» на каждом входе было
    -- шумом: то же самое написано в подсказке двери, причём ДО того, как в неё
    -- вошли, а значит в нужный момент.
    pcall(houseHelper.Observe, cellDescription, pid)
    houseHelper.SendClientState(pid)
end)

customEventHooks.registerHandler("OnPlayerFinishLogin", function(eventStatus, pid)
    pcall(houseHelper.SendClientState, pid)
    local home = houseHelper.GetProperty(tes3mp.GetCell(pid))
    if home and home.mode == "original" and home.owner and home.owner ~= "" and
        houseHelper.GetAccessLevel(pid, home) == "none" then houseHelper.EvictOriginalVisitor(pid, home) end
end)

customEventHooks.registerHandler("OnServerPostInit", function(eventStatus)
    loadStore()

    -- Z006: подчистить клетки экстерьера, попавшие в реестр до этой проверки.
    -- Занятые не трогаем: это уже чья-то собственность, и решать её судьбу
    -- должен администратор, а не молчаливая миграция.
    local removed = 0
    for cellDescription, raw in pairs(store.properties) do
        if houseHelper.IsExteriorCell(cellDescription) then
            if type(raw.owner) == "string" and raw.owner ~= "" then
                log(enumerations.log.WARN, "клетка экстерьера числится купленной: " ..
                    cellDescription .. " (владелец " .. raw.owner .. "); уберите её вручную")
            else
                store.properties[cellDescription] = nil
                removed = removed + 1
            end
        end
    end
    if removed > 0 then
        saveStore(true)
        log(enumerations.log.INFO, "из реестра убрано клеток экстерьера: " .. removed)
    end

    local mergedRooms = houseHelper.ReconcileLinkedRooms()
    if mergedRooms > 0 then
        log(enumerations.log.INFO, "объединено связанных помещений в существующем реестре: " .. mergedRooms)
    end

    getTopologyIndex() -- Warm once after startup migrations, before players join.
    local count = 0
    for _ in pairs(store.properties) do count = count + 1 end
    log(enumerations.log.INFO, "реестр загружен, объектов: " .. count ..
        (cfg().enabled and "" or " (система отключена в config.houseSystem)"))
end)

customEventHooks.registerHandler("OnServerExit", function(eventStatus)
    houseHelper.FlushStore()
end)

local function refreshedCommand(pid, cmd)
    commandHouse(pid, cmd)
    houseHelper.FlushStore()
    houseHelper.RefreshAll()
end
customCommandHooks.registerCommand("house", refreshedCommand)
customCommandHooks.registerCommand("home", refreshedCommand)
customCommandHooks.registerCommand("дом", refreshedCommand)

return houseHelper
