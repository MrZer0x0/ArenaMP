-- ===========================================================================
-- ArenaMP Z001 — adminInterface: единое окно администратора
-- ===========================================================================
--
-- До сих пор администрирование жило в трёх десятках команд, каждая со своим
-- синтаксисом и своим списком рангов. Здесь они собраны в одно окно со
-- вкладками. Вкладка «Недвижимость» — та, ради которой всё и затевалось:
-- добавление домов, цена, владелец, тип, сброс инстанса и пакетное
-- сканирование ячеек.
--
-- Устройство
-- ----------
-- TES3MP умеет ровно два вида окон: CustomMessageBox (текст + кнопки) и
-- ListBox (длинный список). Вкладки поэтому собраны вручную: каждая вкладка —
-- отдельный CustomMessageBox, а «переключение» — это открытие следующего окна.
-- Состояние навигации на игрока хранится в `session`, чтобы список из ListBox
-- можно было сопоставить с конкретным объектом, когда придёт ответ.
--
-- Все идентификаторы окон берутся из config.customMenuIds — так их видно
-- рядом с идентификаторами остальных подсистем и легко проверить на
-- пересечение.
-- ===========================================================================

require("config")

local houseHelper = require("houseHelper")
-- Z002: вкладка подмены скриптов работает через этот модуль.
local mwScriptHelper = require("mwScriptHelper")

local adminInterface = {}

local LOG_TAG = "[ArenaMP Admin] "

local function log(level, message)
    tes3mp.LogAppend(level, LOG_TAG .. tostring(message))
end

local ids = config.customMenuIds or {}
local ID = {
    main = ids.adminMain or 9070,
    houses = ids.adminHouses or 9071,
    houseList = ids.adminHouseList or 9072,
    houseDetail = ids.adminHouseDetail or 9073,
    houseInput = ids.adminHouseInput or 9074,
    houseKind = ids.adminHouseKind or 9075,
    players = ids.adminPlayers or 9076,
    server = ids.adminServer or 9077,
    resets = ids.adminResets or 9078,
    confirm = ids.adminConfirm or 9079,
    scripts = ids.adminScripts or 9080,
    scriptList = ids.adminScriptList or 9081,
    scriptDetail = ids.adminScriptDetail or 9082,
    scriptConfirm = ids.adminScriptConfirm or 9083
}

adminInterface.ID = ID

-- Y111: the minimum rank is config.staffPermissions.adminPanel (default 1:
-- moderators and admins). It is read on every check, so hot reload applies.
local staffPermissions = require("staffPermissions")
local function requiredRank() return staffPermissions.Required("adminPanel") end

-- Состояние навигации на игрока. Ключи:
--   tab           — открытая вкладка
--   listCells     — соответствие индекса в ListBox и описания ячейки
--   selectedCell  — объект, с которым работает вкладка деталей
--   inputField    — какое поле редактируется в текстовом окне
--   filter        — фильтр списка объектов
--   confirmAction — что подтверждается в окне подтверждения
local session = {}

local function ensureSession(pid)
    session[pid] = type(session[pid]) == "table" and session[pid] or {}
    return session[pid]
end

-- ===========================================================================
-- ЛОКАЛИЗАЦИЯ
-- ===========================================================================
local function isRu(pid)
    return Players[pid] ~= nil and tostring(Players[pid].language or "EN"):upper() == "RU"
end

local function tr(pid, ru, en)
    return isRu(pid) and ru or en
end

local function say(pid, message)
    if Players[pid] == nil then return end
    tes3mp.SendMessage(pid, tostring(message) .. "\n", false)
end

-- ===========================================================================
-- ДОСТУП
-- ===========================================================================
local function staffRank(pid)
    if Players[pid] == nil or not Players[pid]:IsLoggedIn() then return -1 end
    return tonumber(Players[pid].data.settings and Players[pid].data.settings.staffRank) or 0
end

local function hasAccess(pid)
    if Players[pid] == nil or not Players[pid]:IsLoggedIn() then return false end
    if Players[pid]:IsServerOwner() then return true end
    return staffRank(pid) >= requiredRank()
end

adminInterface.HasAccess = hasAccess

-- ===========================================================================
-- ГЛАВНОЕ ОКНО
-- ===========================================================================
local TABS = { "houses", "scripts", "players", "resets", "server" }

local function tabTitle(pid, tab)
    local titles = {
        houses  = { "Недвижимость", "Property" },
        scripts = { "Скрипты", "Scripts" },
        players = { "Игроки", "Players" },
        resets  = { "Сбросы ячеек", "Cell resets" },
        server  = { "Сервер", "Server" }
    }
    local entry = titles[tab] or { tab, tab }
    return isRu(pid) and entry[1] or entry[2]
end

local function showMain(pid)
    local state = ensureSession(pid)
    state.tab = "main"

    local propertyCount, ownedCount = 0, 0
    for _, property in pairs(houseHelper.AllProperties()) do
        propertyCount = propertyCount + 1
        if type(property.owner) == "string" and property.owner ~= "" then
            ownedCount = ownedCount + 1
        end
    end

    local online = 0
    for _, player in pairs(Players) do
        if player and player:IsLoggedIn() then online = online + 1 end
    end

    local coop = false
    local resets = rawget(_G, "periodicCellResets")
    if type(resets) == "table" and type(resets.isCoopPreset) == "function" then
        coop = resets.isCoopPreset() == true
    end

    local lines = {
        tr(pid, "ПАНЕЛЬ АДМИНИСТРАТОРА ArenaMP", "ArenaMP ADMIN PANEL"),
        "",
        tr(pid, "Режим: ", "Mode: ") .. tostring(config.gameMode) .. (coop and " (CO-OP)" or ""),
        tr(pid, "Игроков онлайн: ", "Players online: ") .. tostring(online),
        tr(pid, "Объектов недвижимости: ", "Registered property: ") .. tostring(propertyCount) ..
            " (" .. tr(pid, "занято ", "owned ") .. tostring(ownedCount) .. ")",
        "",
        tr(pid, "Выберите вкладку.", "Choose a tab.")
    }

    local buttons = {}
    for _, tab in ipairs(TABS) do
        table.insert(buttons, tabTitle(pid, tab))
    end
    table.insert(buttons, tr(pid, "Закрыть", "Close"))

    tes3mp.CustomMessageBox(pid, ID.main, table.concat(lines, "\n"), table.concat(buttons, ";"))
end

adminInterface.ShowMain = showMain

-- ===========================================================================
-- ВКЛАДКА: НЕДВИЖИМОСТЬ
-- ===========================================================================
local FILTERS = { "all", "free", "owned", "here" }

local function filterTitle(pid, filter)
    local titles = {
        all   = { "все", "all" },
        free  = { "свободные", "vacant" },
        owned = { "занятые", "owned" },
        here  = { "в этой локации", "in this area" }
    }
    local entry = titles[filter] or { filter, filter }
    return isRu(pid) and entry[1] or entry[2]
end

local function kindTitle(pid, kind)
    local titles = {
        house  = { "дом", "house" },
        shop   = { "магазин", "shop" },
        tavern = { "таверна", "tavern" },
        guild  = { "гильдия", "guild" }
    }
    local entry = titles[kind] or { tostring(kind), tostring(kind) }
    return isRu(pid) and entry[1] or entry[2]
end

local function showHousesTab(pid)
    local state = ensureSession(pid)
    state.tab = "houses"
    state.filter = state.filter or "all"

    local currentCell = tes3mp.GetCell(pid)
    local property = houseHelper.GetProperty(currentCell)
    local suggestion = houseHelper.ClassifyCellName(currentCell)

    local lines = {
        tr(pid, "НЕДВИЖИМОСТЬ", "PROPERTY"),
        "",
        tr(pid, "Текущая ячейка: ", "Current cell: ") .. tostring(currentCell)
    }

    if property ~= nil then
        table.insert(lines, tr(pid, "  зарегистрирована как ", "  registered as ") ..
            kindTitle(pid, property.kind) .. ", " .. tostring(property.price) ..
            tr(pid, " золота", " gold"))
        table.insert(lines, tr(pid, "  владелец: ", "  owner: ") ..
            (property.owner or tr(pid, "нет", "none")))
    elseif suggestion ~= nil then
        table.insert(lines, tr(pid, "  не зарегистрирована; по названию похоже на: ",
            "  not registered; the name suggests: ") .. kindTitle(pid, suggestion))
    else
        table.insert(lines, tr(pid, "  не зарегистрирована и не похожа на недвижимость.",
            "  not registered and does not look like property."))
    end

    table.insert(lines, "")
    table.insert(lines, tr(pid, "Фильтр списка: ", "List filter: ") .. filterTitle(pid, state.filter))

    local buttons = {
        tr(pid, "Список объектов", "Property list"),
        tr(pid, "Сменить фильтр", "Change filter"),
        tr(pid, "Добавить текущую", "Register current"),
        tr(pid, "Добавить по названию", "Register by name"),
        tr(pid, "Сканировать посещённые", "Scan visited cells"),
        tr(pid, "Текущая без инстанса", "Current: no instance"),
        tr(pid, "Без инстанса по имени", "Original cell by name"),
        tr(pid, "Назад", "Back")
    }

    tes3mp.CustomMessageBox(pid, ID.houses, table.concat(lines, "\n"), table.concat(buttons, ";"))
end

local function matchesFilter(pid, property, filter)
    if filter == "free" then
        return type(property.owner) ~= "string" or property.owner == ""
    elseif filter == "owned" then
        return type(property.owner) == "string" and property.owner ~= ""
    elseif filter == "here" then
        -- «Здесь» — это ячейки, чьё описание начинается с того же префикса до
        -- запятой: "Balmora, ..." для игрока, стоящего в Балморе или в любом
        -- из её интерьеров.
        local current = tostring(tes3mp.GetCell(pid) or "")
        local area = string.match(current, "^([^,]+)") or current
        if area == "" then return false end
        -- houseHelper.Lower folds Cyrillic as well; plain string.lower would
        -- make the filter miss every Russian cell name.
        return string.find(houseHelper.Lower(property.cell), houseHelper.Lower(area), 1, true) == 1
    end
    return true
end

local function showHouseList(pid)
    local state = ensureSession(pid)
    state.tab = "houseList"

    local entries = {}
    for cellDescription, property in pairs(houseHelper.AllProperties()) do
        if matchesFilter(pid, property, state.filter or "all") then
            table.insert(entries, property)
        end
    end
    table.sort(entries, function(a, b) return a.cell < b.cell end)

    if #entries == 0 then
        say(pid, tr(pid, "По этому фильтру объектов нет.", "No property matches this filter."))
        showHousesTab(pid)
        return
    end

    -- ListBox не отдаёт обратно текст строки, только её индекс, поэтому
    -- соответствие индексов и ячеек запоминается на время показа списка.
    state.listCells = {}
    local lines = {}
    for index, property in ipairs(entries) do
        state.listCells[index] = property.cell
        local owner = property.owner
        local mark
        if type(owner) == "string" and owner ~= "" then
            mark = "[" .. owner .. "]"
        else
            mark = "[" .. tostring(property.price) .. "]"
        end
        table.insert(lines, property.cell .. "  " .. mark)
    end

    tes3mp.ListBox(pid, ID.houseList,
        tr(pid, "Объекты (", "Property (") .. filterTitle(pid, state.filter or "all") .. ")",
        table.concat(lines, "\n"))
end

local function showHouseDetail(pid, cellDescription)
    local state = ensureSession(pid)
    local property = houseHelper.GetProperty(cellDescription)
    if property == nil then
        say(pid, tr(pid, "Объект больше не существует.", "That property no longer exists."))
        showHousesTab(pid)
        return
    end

    state.tab = "houseDetail"
    state.selectedCell = cellDescription

    local lines = {
        property.cell,
        "",
        tr(pid, "Тип: ", "Kind: ") .. kindTitle(pid, property.kind),
        tr(pid,"Режим: ","Mode: ") .. (property.mode == "original" and "Исходная ячейка" or "Инстанс"),
        tr(pid, "Цена: ", "Price: ") .. tostring(property.price),
        tr(pid, "Владелец: ", "Owner: ") .. (property.owner or tr(pid, "нет", "none")),
        tr(pid, "Замок: ", "Locked: ") .. (property.locked and tr(pid, "да", "yes") or tr(pid, "нет", "no")),
        tr(pid, "Доступен к покупке: ", "Available: ") ..
            (property.enabled and tr(pid, "да", "yes") or tr(pid, "нет", "no")),
        tr(pid, "Добавлен автоматически: ", "Auto-detected: ") ..
            (property.autoDetected and tr(pid, "да", "yes") or tr(pid, "нет", "no"))
    }

    if #property.coOwners > 0 then
        table.insert(lines, tr(pid, "Совладельцы: ", "Co-owners: ") .. table.concat(property.coOwners, ", "))
    end
    if #property.friends > 0 then
        table.insert(lines, tr(pid, "Друзья: ", "Friends: ") .. table.concat(property.friends, ", "))
    end

    local buttons = {
        tr(pid, "Цена", "Price"),
        tr(pid, "Тип", "Kind"),
        tr(pid, "Владелец", "Owner"),
        tr(pid, "Освободить", "Release"),
        tr(pid, "Сбросить инстанс", "Reset instance"),
        property.enabled and tr(pid, "Снять с продажи", "Disable") or tr(pid, "Вернуть в продажу", "Enable"),
        tr(pid, "Телепорт", "Teleport"),
        tr(pid, "Удалить из реестра", "Remove"),
        tr(pid, "Переключить режим", "Toggle original/instance"),
        tr(pid, "Пересчитать цену по наполнению", "Recalculate content price"),
        tr(pid, "Назад", "Back")
    }

    tes3mp.CustomMessageBox(pid, ID.houseDetail, table.concat(lines, "\n"), table.concat(buttons, ";"))
end

adminInterface.ShowHouseDetail = showHouseDetail

-- ===========================================================================
-- ВКЛАДКА: СКРИПТЫ
-- ---------------------------------------------------------------------------
-- Подмена игровых MWScript. Штатного способа чинить сломанный скрипт плагина
-- без правки самого плагина нет, поэтому механизм используется часто — и
-- итеративно: положил файл, перезагрузил, проверил. Ради этого цикла вкладка и
-- существует; перезапускать сервер на каждую правку не нужно.
-- ===========================================================================
local function showScriptsTab(pid)
    local state = ensureSession(pid)
    state.tab = "scripts"

    local list = mwScriptHelper.List()
    local active, disabled = 0, 0
    for _, entry in ipairs(list) do
        if entry.disabled then disabled = disabled + 1 else active = active + 1 end
    end

    local lines = {
        tr(pid, "ПОДМЕНА СКРИПТОВ", "SCRIPT OVERRIDES"),
        "",
        tr(pid, "Каталог: ", "Directory: ") .. mwScriptHelper.DirectoryPath(),
        tr(pid, "Активных: ", "Active: ") .. tostring(active) ..
            tr(pid, ", отключённых: ", ", disabled: ") .. tostring(disabled)
    }

    if #list == 0 then
        table.insert(lines, "")
        table.insert(lines, tr(pid,
            "Файлов не найдено. Положите .es3 в каталог выше и нажмите «Перезагрузить».",
            "No files found. Put .es3 files in the directory above and press Reload."))
    end

    table.insert(lines, "")
    table.insert(lines, tr(pid,
        "Имя файла без расширения — это id подменяемого скрипта.",
        "A file name without its extension is the id of the script it replaces."))

    tes3mp.CustomMessageBox(pid, ID.scripts, table.concat(lines, "\n"),
        tr(pid, "Список;Перезагрузить;Разослать игрокам;Назад",
            "List;Reload;Push to players;Back"))
end

local function showScriptList(pid)
    local state = ensureSession(pid)
    local list = mwScriptHelper.List()

    if #list == 0 then
        say(pid, tr(pid, "Подменённых скриптов нет.", "There are no script overrides."))
        showScriptsTab(pid)
        return
    end

    -- Как и список объектов, ListBox возвращает только индекс строки, поэтому
    -- соответствие индексов и имён запоминается на время показа.
    state.listScripts = {}
    local lines = {}
    for index, entry in ipairs(list) do
        state.listScripts[index] = entry.name
        table.insert(lines, string.format("%s %s  (%d)",
            entry.disabled and "[off]" or "[on] ", entry.name, entry.bytes))
    end

    tes3mp.ListBox(pid, ID.scriptList,
        tr(pid, "Подменённые скрипты", "Script overrides"), table.concat(lines, "\n"))
end

local function showScriptDetail(pid, scriptName)
    local state = ensureSession(pid)
    local info = mwScriptHelper.Get(scriptName)
    if info == nil then
        say(pid, tr(pid, "Скрипт больше не найден.", "That script is no longer present."))
        showScriptsTab(pid)
        return
    end

    state.selectedScript = scriptName

    local lines = {
        scriptName,
        "",
        tr(pid, "Файл: ", "File: ") .. tostring(info.file),
        tr(pid, "Размер: ", "Size: ") .. tostring(info.bytes) .. tr(pid, " байт", " bytes"),
        tr(pid, "Состояние: ", "State: ") ..
            (info.disabled and tr(pid, "отключён", "disabled") or tr(pid, "активен", "active"))
    }

    -- Несовпадение имени файла и имени в строке Begin — самая частая причина
    -- «подмена не работает», поэтому оно показывается прямо в карточке.
    if info.beginName ~= nil and string.lower(info.beginName) ~= string.lower(scriptName) then
        table.insert(lines, "")
        table.insert(lines, tr(pid, "ВНИМАНИЕ: внутри файла «Begin ", "WARNING: the file says \"Begin ") ..
            tostring(info.beginName) ..
            tr(pid, "». Подменяется скрипт «", "\". The replaced script is \"") .. scriptName ..
            tr(pid, "», а не «", "\", not \"") .. tostring(info.beginName) .. tr(pid, "».", "\"."))
    end

    local preview = mwScriptHelper.Preview(scriptName, 10)
    if preview ~= nil and preview ~= "" then
        table.insert(lines, "")
        table.insert(lines, preview)
    end

    tes3mp.CustomMessageBox(pid, ID.scriptDetail, table.concat(lines, "\n"),
        (info.disabled and tr(pid, "Включить", "Enable") or tr(pid, "Отключить", "Disable")) ..
        tr(pid, ";Перезагрузить этот;Назад", ";Reload this;Back"))
end

-- ===========================================================================
-- ВКЛАДКА: ИГРОКИ
-- ===========================================================================
local function showPlayersTab(pid)
    local state = ensureSession(pid)
    state.tab = "players"

    local lines = { tr(pid, "ИГРОКИ ОНЛАЙН", "PLAYERS ONLINE"), "" }
    local count = 0
    for otherPid, player in pairs(Players) do
        if player and player:IsLoggedIn() then
            count = count + 1
            local owned = houseHelper.ListOwnedBy(player.accountName)
            lines[#lines + 1] = string.format("%d. %s  (%s: %d, %s: %d)",
                count, tostring(player.accountName),
                tr(pid, "ранг", "rank"),
                tonumber(player.data.settings and player.data.settings.staffRank) or 0,
                tr(pid, "объектов", "property"), #owned)
        end
    end
    if count == 0 then
        lines[#lines + 1] = tr(pid, "Никого нет.", "Nobody online.")
    end

    tes3mp.CustomMessageBox(pid, ID.players, table.concat(lines, "\n"),
        tr(pid, "Обновить;Назад", "Refresh;Back"))
end

-- ===========================================================================
-- ВКЛАДКА: СБРОСЫ
-- ===========================================================================
local function showResetsTab(pid)
    local state = ensureSession(pid)
    state.tab = "resets"

    local resets = rawget(_G, "periodicCellResets")
    local coop = type(resets) == "table" and type(resets.isCoopPreset) == "function"
        and resets.isCoopPreset() == true

    local lines = {
        tr(pid, "СБРОСЫ ЯЧЕЕК", "CELL RESETS"),
        "",
        tr(pid, "Текущая ячейка: ", "Current cell: ") .. tostring(tes3mp.GetCell(pid))
    }

    if coop then
        table.insert(lines, "")
        table.insert(lines, tr(pid,
            "Пресет CO-OP: автоматический и массовый сброс отключены. Ручные операции доступны.",
            "CO-OP preset: automatic and mass resets are disabled. Manual operations remain available."))
    end

    tes3mp.CustomMessageBox(pid, ID.resets, table.concat(lines, "\n"),
        tr(pid, "Мягкий сброс;Полная очистка;Дома без хозяина;Назад",
            "Soft reset;Full clear;Unowned houses;Back"))
end

-- ===========================================================================
-- ВКЛАДКА: СЕРВЕР
-- ===========================================================================
local function showServerTab(pid)
    local state = ensureSession(pid)
    state.tab = "server"

    local lines = {
        tr(pid, "СЕРВЕР", "SERVER"),
        "",
        tr(pid, "Режим: ", "Mode: ") .. tostring(config.gameMode),
        tr(pid, "База данных: ", "Database: ") .. tostring(config.databaseType),
        tr(pid, "Язык сервера: ", "Server language: ") .. tostring(config.serverLanguage),
        tr(pid, "Система домов: ", "Housing system: ") ..
            ((type(config.houseSystem) == "table" and config.houseSystem.enabled ~= false)
                and tr(pid, "включена", "enabled") or tr(pid, "выключена", "disabled"))
    }

    tes3mp.CustomMessageBox(pid, ID.server, table.concat(lines, "\n"),
        tr(pid, "Сохранить мир;Назад", "Save world;Back"))
end

-- ===========================================================================
-- ДЕЙСТВИЯ ВКЛАДКИ НЕДВИЖИМОСТИ
-- ===========================================================================
local function registerCurrentCell(pid, explicitCell)
    -- Z005: та же процедура обслуживает и «добавить текущую», и «добавить по
    -- названию»: администратор чаще всего заводит ячейку, в которой он не
    -- стоит, — интерьер из плагина, который он видел в логе.
    local cellDescription = explicitCell
    if type(cellDescription) ~= "string" or cellDescription == "" then
        cellDescription = tes3mp.GetCell(pid)
    end
    if houseHelper.GetProperty(cellDescription) ~= nil then
        say(pid, tr(pid, "Эта ячейка уже в реестре.", "This cell is already registered."))
        showHouseDetail(pid, cellDescription)
        return
    end

    -- Z006: улицу зарегистрировать нельзя. Раньше это ловилось только
    -- автоопределением, поэтому администратор, нажавший «добавить текущую»
    -- под открытым небом, заводил дом из клетки экстерьера — и все, кто потом
    -- туда заходил, получали «этот дом продаётся».
    if type(houseHelper.IsExteriorCell) == "function" and houseHelper.IsExteriorCell(cellDescription) then
        say(pid, tr(pid, "Это клетка экстерьера — недвижимостью может быть только интерьер.",
            "This is an exterior cell - only interiors can be property."))
        showHousesTab(pid)
        return
    end

    -- Z005: тип выбирает администратор, а не угадывает автоопределение.
    --
    -- Раньше ячейка, которую правило «похоже на дом» не узнало, молча
    -- становилась домом. Но администратор чаще всего заводит вручную именно
    -- НЕОБЫЧНУЮ ячейку — зал из стороннего плагина под таверну, склад под
    -- магазин, — и тип там как раз не дом. Поэтому спрашиваем прямо; подсказка
    -- по названию идёт в заголовок окна как рекомендация.
    local state = ensureSession(pid)
    state.pendingRegisterCell = cellDescription

    local suggestion = houseHelper.ClassifyCellName(cellDescription)
    local lines = {
        tr(pid, "ДОБАВЛЕНИЕ ЯЧЕЙКИ", "REGISTER CELL"),
        "",
        cellDescription,
        ""
    }
    if suggestion ~= nil then
        table.insert(lines, tr(pid, "По названию похоже на: ", "The name suggests: ") ..
            kindTitle(pid, suggestion))
    else
        table.insert(lines, tr(pid, "По названию тип не определяется — выберите сами.",
            "The name gives no hint - choose the kind yourself."))
    end
    table.insert(lines, "")
    table.insert(lines, tr(pid, "Ячейка будет защищена от автосброса.",
        "The cell will be protected from automatic resets."))

    tes3mp.CustomMessageBox(pid, ID.houseKind, table.concat(lines, "\n"),
        tr(pid, "Дом;Магазин;Таверна;Гильдия;Отмена", "House;Shop;Tavern;Guild;Cancel"))
end

-- Пакетное сканирование: пройти по всем ячейкам, о которых сервер уже знает,
-- и зарегистрировать подходящие. Источники — загруженные ячейки и файлы в
-- server/data/cell, поэтому сканирование находит ровно то, что кто-то уже
-- посещал. Ячейки, куда не ступала нога игрока, появятся в реестре сами при
-- первом входе.
local function scanVisitedCells(pid)
    local scanned, added = 0, 0

    for cellDescription in pairs(LoadedCells or {}) do
        scanned = scanned + 1
        if houseHelper.GetProperty(cellDescription) == nil
            and houseHelper.ClassifyCellName(cellDescription) ~= nil then
            if houseHelper.Register(cellDescription, nil, nil, true, pid) ~= nil then
                added = added + 1
            end
        end
    end

    -- Кроме загруженных ячеек, сервер помнит те, для которых уже заведён файл
    -- состояния. Их имена хранит resetHelper в своих таймерах.
    local resets = rawget(_G, "periodicCellResets")
    if type(resets) == "table" and type(resets.GetKnownCells) == "function" then
        local ok, known = pcall(resets.GetKnownCells)
        if ok and type(known) == "table" then
            for _, cellDescription in ipairs(known) do
                scanned = scanned + 1
                if houseHelper.GetProperty(cellDescription) == nil
                    and houseHelper.ClassifyCellName(cellDescription) ~= nil then
                    if houseHelper.Register(cellDescription, nil, nil, true, pid) ~= nil then
                        added = added + 1
                    end
                end
            end
        end
    end

    houseHelper.FlushStore()
    houseHelper.RefreshAll()
    say(pid, tr(pid, "Просмотрено ячеек: ", "Cells scanned: ") .. tostring(scanned) ..
        tr(pid, ", добавлено: ", ", registered: ") .. tostring(added))
    log(enumerations.log.INFO, "сканирование: просмотрено " .. scanned .. ", добавлено " .. added)
    showHousesTab(pid)
end

local function askInput(pid, field, label)
    local state = ensureSession(pid)
    state.inputField = field
    tes3mp.InputDialog(pid, ID.houseInput, label, "")
end

local function applyInput(pid, value)
    local state = ensureSession(pid)
    local field = state.inputField
    state.inputField = nil

    value = tostring(value or "")

    -- Z005: регистрация по названию не имеет выбранного объекта — она его и
    -- создаёт, поэтому обрабатывается до проверки на существующий.
    if field == "registerName" then
        local wanted = value:gsub("^%s+", ""):gsub("%s+$", "")
        if wanted == "" then
            showHousesTab(pid)
            return
        end
        registerCurrentCell(pid, wanted)
        return
    end

    if field == "originalCell" then
        local cell = tostring(value or ""):match("^%s*(.-)%s*$")
        local property = houseHelper.Register(cell,"house",nil,false)
        if property and houseHelper.SetMode(property.cell,"original") then showHouseDetail(pid,property.cell)
        else say(pid,"Укажите свободный интерьер. Режим купленного дома менять нельзя.");showHousesTab(pid) end
        return
    end
    local cellDescription = state.selectedCell
    local property = houseHelper.GetProperty(cellDescription)
    if property == nil then
        showHousesTab(pid)
        return
    end

    if field == "price" then
        local price = tonumber(value)
        if price == nil or price < 0 then
            say(pid, tr(pid, "Нужно число.", "A number is required."))
        else
            houseHelper.SetPrice(cellDescription, math.floor(price))
            say(pid, tr(pid, "Цена изменена: ", "Price updated: ") .. tostring(math.floor(price)))
        end
    elseif field == "owner" then
        if value == "" then
            houseHelper.Release(cellDescription, "cleared by admin")
            say(pid, tr(pid, "Владелец снят.", "Owner cleared."))
        else
            if houseHelper.SetOwner(cellDescription, value) then
                say(pid, tr(pid, "Владелец назначен: ", "Owner set: ") .. value)
            else
                say(pid, tr(pid, "Назначение отменено: у игрока уже есть дом.", "Assignment refused: the player already owns a home."))
            end
        end
    end

    houseHelper.FlushStore()
    houseHelper.RefreshAll()
    showHouseDetail(pid, cellDescription)
end

local function showKindChooser(pid)
    tes3mp.CustomMessageBox(pid, ID.houseKind,
        tr(pid, "Выберите тип объекта.", "Choose the property kind."),
        tr(pid, "Дом;Магазин;Таверна;Гильдия;Отмена", "House;Shop;Tavern;Guild;Cancel"))
end

local function askConfirm(pid, action, text)
    local state = ensureSession(pid)
    state.confirmAction = action
    tes3mp.CustomMessageBox(pid, ID.confirm, text, tr(pid, "Подтвердить;Отмена", "Confirm;Cancel"))
end

local function runConfirmed(pid)
    local state = ensureSession(pid)
    local action = state.confirmAction
    state.confirmAction = nil

    local cellDescription = state.selectedCell
    if action == "release" then
        houseHelper.Release(cellDescription, "released by admin")
        houseHelper.FlushStore()
    houseHelper.RefreshAll()
        say(pid, tr(pid, "Объект освобождён.", "Property released."))
        showHouseDetail(pid, cellDescription)

    elseif action == "resetInstance" then
        local property = houseHelper.GetProperty(cellDescription)
        local owner = property and property.owner
        if type(owner) ~= "string" or owner == "" then
            say(pid, tr(pid, "У объекта нет владельца — инстанса не существует.",
                "The property has no owner, so there is no instance."))
        else
            local success = true
            for _, member in ipairs(property.cells or {cellDescription}) do
                success = houseHelper.ResetInstance(houseHelper.GetInstanceDescription(member, owner)) and success
            end
            if success then
                say(pid, tr(pid, "Инстанс сброшен.", "Instance reset."))
            else
                say(pid, tr(pid, "Не удалось сбросить инстанс.", "The instance could not be reset."))
            end
        end
        showHouseDetail(pid, cellDescription)

    elseif action == "remove" then
        houseHelper.Unregister(cellDescription)
        say(pid, tr(pid, "Объект удалён из реестра.", "Property removed from the registry."))
        state.selectedCell = nil
        showHousesTab(pid)

    else
        showMain(pid)
    end
end

-- ===========================================================================
-- ОБРАБОТКА ОТВЕТОВ
-- ===========================================================================
customEventHooks.registerHandler("OnGUIAction", function(eventStatus, pid, idGui, data)
    if not hasAccess(pid) then return end

    local state = ensureSession(pid)
    local choice = tonumber(data) or -1

    -- ---------------------------------------------------------------- главное
    if idGui == ID.main then
        local tab = TABS[choice + 1]
        if tab == "houses" then showHousesTab(pid)
        elseif tab == "scripts" then showScriptsTab(pid)
        elseif tab == "players" then showPlayersTab(pid)
        elseif tab == "resets" then showResetsTab(pid)
        elseif tab == "server" then showServerTab(pid) end
        return
    end

    -- ------------------------------------------------------- недвижимость
    if idGui == ID.houses then
        if choice == 0 then
            showHouseList(pid)
        elseif choice == 1 then
            -- Фильтры перебираются по кругу: отдельное окно ради четырёх
            -- значений было бы лишним щелчком.
            local current = state.filter or "all"
            local index = 1
            for i, value in ipairs(FILTERS) do
                if value == current then index = i end
            end
            state.filter = FILTERS[(index % #FILTERS) + 1]
            showHousesTab(pid)
        elseif choice == 2 then
            registerCurrentCell(pid)
        elseif choice == 3 then
            askInput(pid, "registerName",
                tr(pid, "Точное название ячейки", "Exact cell description"))
        elseif choice == 5 then
            local cell = tes3mp.GetCell(pid)
            local property = houseHelper.Register(cell,"house",nil,false)
            if property and houseHelper.SetMode(property.cell,"original") then showHouseDetail(pid,property.cell)
            else say(pid,"Нужен свободный интерьер; купленные дома не меняются.");showHousesTab(pid) end
        elseif choice == 6 then
            askInput(pid,"originalCell","Точное название пустого интерьера для дома без инстанса:")
        elseif choice == 4 then
            scanVisitedCells(pid)
        else
            showMain(pid)
        end
        return
    end

    if idGui == ID.houseList then
        local cellDescription = type(state.listCells) == "table" and state.listCells[choice + 1] or nil
        if cellDescription == nil then
            showHousesTab(pid)
        else
            showHouseDetail(pid, cellDescription)
        end
        return
    end

    if idGui == ID.houseDetail then
        local cellDescription = state.selectedCell
        local property = houseHelper.GetProperty(cellDescription)
        if property == nil then showHousesTab(pid) return end

        if choice == 0 then
            askInput(pid, "price", tr(pid, "Новая цена в золоте:", "New price in gold:"))
        elseif choice == 1 then
            showKindChooser(pid)
        elseif choice == 2 then
            askInput(pid, "owner",
                tr(pid, "Имя владельца (пусто — снять владельца):",
                    "Owner account name (empty clears the owner):"))
        elseif choice == 3 then
            askConfirm(pid, "release", tr(pid,
                "Освободить объект? Личная копия владельца будет удалена.",
                "Release this property? The owner's private copy will be deleted."))
        elseif choice == 4 then
            askConfirm(pid, "resetInstance", tr(pid,
                "Сбросить личную копию владельца? Всё, что там лежит, пропадёт.",
                "Reset the owner's private copy? Everything inside is lost."))
        elseif choice == 5 then
            houseHelper.SetEnabled(cellDescription, not property.enabled)
            houseHelper.FlushStore()
    houseHelper.RefreshAll()
            showHouseDetail(pid, cellDescription)
        elseif choice == 6 then
            houseHelper.Warp(pid, cellDescription)
            say(pid, tr(pid, "Телепорт: ", "Teleported to: ") .. cellDescription)
        elseif choice == 8 then
            if not houseHelper.SetMode(cellDescription,property.mode == "original" and "instance" or "original") then
                say(pid,"Режим можно менять только до покупки дома.")
            end
            showHouseDetail(pid,cellDescription)
        elseif choice == 9 then
            if not houseHelper.RecalculatePrice(cellDescription) then
                say(pid, tr(pid,"Цена купленного дома не пересчитывается.","Owned property cannot be repriced."))
            end
            showHouseDetail(pid,cellDescription)
        elseif choice == 7 then
            askConfirm(pid, "remove", tr(pid,
                "Удалить объект из реестра? Владелец, если он есть, потеряет дом.",
                "Remove this property from the registry? Its owner, if any, loses the house."))
        else
            showHousesTab(pid)
        end
        return
    end

    if idGui == ID.houseKind then
        local kinds = { "house", "shop", "tavern", "guild" }
        local kind = kinds[choice + 1]

        -- Z005: то же окно обслуживает и добавление новой ячейки, и смену типа
        -- у существующей. Ожидающая регистрация имеет приоритет.
        if state.pendingRegisterCell ~= nil then
            local cellDescription = state.pendingRegisterCell
            state.pendingRegisterCell = nil
            if kind == nil then
                say(pid, tr(pid, "Добавление отменено.", "Registration cancelled."))
                showHousesTab(pid)
                return
            end

            local property = houseHelper.Register(cellDescription, kind, nil, false)
            if property == nil then
                say(pid, tr(pid, "Не удалось зарегистрировать эту ячейку.",
                    "This cell could not be registered."))
                showHousesTab(pid)
                return
            end

            houseHelper.FlushStore()
            if type(houseHelper.RefreshAll) == "function" then houseHelper.RefreshAll() end
            log(enumerations.log.INFO, tostring(Players[pid].accountName) ..
                " зарегистрировал " .. cellDescription .. " как " .. kind)
            say(pid, tr(pid, "Зарегистрировано: ", "Registered: ") .. cellDescription ..
                " (" .. kindTitle(pid, kind) .. ", " .. tostring(property.price) .. ")")
            showHouseDetail(pid, cellDescription)
            return
        end

        if kind ~= nil and state.selectedCell ~= nil then
            houseHelper.SetKind(state.selectedCell, kind)
            houseHelper.FlushStore()
    houseHelper.RefreshAll()
            say(pid, tr(pid, "Тип изменён: ", "Kind updated: ") .. kindTitle(pid, kind))
        end
        if state.selectedCell ~= nil then
            showHouseDetail(pid, state.selectedCell)
        else
            showHousesTab(pid)
        end
        return
    end

    if idGui == ID.houseInput then
        applyInput(pid, data)
        return
    end

    if idGui == ID.confirm then
        if choice == 0 then
            runConfirmed(pid)
        else
            state.confirmAction = nil
            if state.selectedCell ~= nil then
                showHouseDetail(pid, state.selectedCell)
            else
                showMain(pid)
            end
        end
        return
    end

    -- ------------------------------------------------------------ скрипты
    if idGui == ID.scripts then
        if choice == 0 then
            showScriptList(pid)
        elseif choice == 1 then
            local added, skipped, disabledCount = mwScriptHelper.Reload()
            say(pid, tr(pid, "Загружено: ", "Loaded: ") .. tostring(added) ..
                tr(pid, ", отключено: ", ", disabled: ") .. tostring(disabledCount) ..
                tr(pid, ", пропущено: ", ", skipped: ") .. tostring(skipped))
            showScriptsTab(pid)
        elseif choice == 2 then
            local recipients = mwScriptHelper.SendToOnlinePlayers()
            say(pid, tr(pid, "Записи разосланы игрокам: ", "Records pushed to players: ") ..
                tostring(recipients))
            if recipients > 0 then
                -- Сказать это прямо важнее, чем выглядеть всемогущим: иначе
                -- администратор решит, что подмена не сработала.
                say(pid, tr(pid,
                    "Уже работающие экземпляры скриптов подхватят новый текст после перезахода в ячейку.",
                    "Script instances already running pick up the new text after a cell reload."))
            end
            showScriptsTab(pid)
        else
            showMain(pid)
        end
        return
    end

    if idGui == ID.scriptList then
        local scriptName = type(state.listScripts) == "table" and state.listScripts[choice + 1] or nil
        if scriptName == nil then
            showScriptsTab(pid)
        else
            showScriptDetail(pid, scriptName)
        end
        return
    end

    if idGui == ID.scriptDetail then
        local scriptName = state.selectedScript
        if scriptName == nil then showScriptsTab(pid) return end

        if choice == 0 then
            local wasDisabled = mwScriptHelper.IsDisabled(scriptName)
            mwScriptHelper.SetDisabled(scriptName, not wasDisabled)
            if wasDisabled then
                mwScriptHelper.SendToOnlinePlayers()
                say(pid, tr(pid, "Подмена включена: ", "Override enabled: ") .. scriptName)
            else
                say(pid, tr(pid, "Подмена отключена: ", "Override disabled: ") .. scriptName ..
                    tr(pid, ". У игроков, уже получивших запись, она останется до перезахода на сервер.",
                        ". Players who already received the record keep it until they reconnect."))
            end
            showScriptDetail(pid, scriptName)
        elseif choice == 1 then
            mwScriptHelper.Reload()
            mwScriptHelper.SendToOnlinePlayers()
            say(pid, tr(pid, "Перезагружено: ", "Reloaded: ") .. scriptName)
            showScriptDetail(pid, scriptName)
        else
            showScriptList(pid)
        end
        return
    end

    -- ------------------------------------------------------------- игроки
    if idGui == ID.players then
        if choice == 0 then showPlayersTab(pid) else showMain(pid) end
        return
    end

    -- ------------------------------------------------------------- сбросы
    if idGui == ID.resets then
        local resets = rawget(_G, "periodicCellResets")
        if type(resets) ~= "table" then
            say(pid, tr(pid, "Модуль сбросов недоступен.", "The reset module is unavailable."))
            showMain(pid)
            return
        end

        if choice == 0 and type(resets.cleanCurrentCell) == "function" then
            pcall(resets.cleanCurrentCell, pid, {})
            showResetsTab(pid)
        elseif choice == 1 and type(resets.clearCurrentCell) == "function" then
            pcall(resets.clearCurrentCell, pid, {})
            showResetsTab(pid)
        elseif choice == 2 and type(resets.cleanUnownedHouses) == "function" then
            local ok, cleaned, skipped = pcall(resets.cleanUnownedHouses)
            if ok then
                say(pid, tr(pid, "Очищено: ", "Cleaned: ") .. tostring(cleaned or 0) ..
                    tr(pid, ", пропущено: ", ", skipped: ") .. tostring(skipped or 0))
            end
            showResetsTab(pid)
        else
            showMain(pid)
        end
        return
    end

    -- ------------------------------------------------------------- сервер
    if idGui == ID.server then
        if choice == 0 then
            if WorldInstance ~= nil and type(WorldInstance.SaveToDrive) == "function" then
                WorldInstance:SaveToDrive()
            end
            houseHelper.FlushStore()
    houseHelper.RefreshAll()
            say(pid, tr(pid, "Состояние мира сохранено.", "World state saved."))
            showServerTab(pid)
        else
            showMain(pid)
        end
        return
    end
end)

customEventHooks.registerHandler("OnPlayerDisconnect", function(eventStatus, pid)
    session[pid] = nil
end)

-- ===========================================================================
-- КОМАНДА
-- ===========================================================================
local function commandAdmin(pid, cmd)
    if not hasAccess(pid) then
        say(pid, tr(pid, "Недостаточно прав.", "Insufficient permissions."))
        return
    end

    local sub = string.lower(tostring(cmd[2] or ""))
    if sub == "houses" or sub == "house" then
        showHousesTab(pid)
    elseif sub == "scripts" or sub == "script" then
        showScriptsTab(pid)
    elseif sub == "players" then
        showPlayersTab(pid)
    elseif sub == "resets" then
        showResetsTab(pid)
    elseif sub == "server" then
        showServerTab(pid)
    else
        showMain(pid)
    end
end

customCommandHooks.registerCommand("admin", commandAdmin)
customCommandHooks.registerCommand("panel", commandAdmin)
-- The command hook only keeps ordinary players out (rank 1 = lowest staff);
-- commandAdmin/hasAccess applies the configured adminPanel rank dynamically.
customCommandHooks.setRankRequirement("admin", 1)
customCommandHooks.setRankRequirement("panel", 1)

return adminInterface
