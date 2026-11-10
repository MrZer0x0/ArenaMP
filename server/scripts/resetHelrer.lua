local fileHelper = require("fileHelper")
local jsonInterface = require("jsonInterface")

-- ArenaMP C21: houseHelper is an OPTIONAL dependency.
-- Never crash the server when a housing package is not installed.
local houseHelper = nil
do
    local ok, helper = pcall(require, "houseHelper")
    if ok and type(helper) == "table" then
        houseHelper = helper
    end
end

periodicCellResets = {}

periodicCellResets.hData = {
    cells = {}
}

local function callHouseHelper(methodNames, ...)
    if not houseHelper then
        return nil, false
    end

    for _, methodName in ipairs(methodNames) do
        local fn = houseHelper[methodName]
        if type(fn) == "function" then
            local ok, result = pcall(fn, ...)
            if ok then
                return result, true
            end

            -- Some helper modules expose methods with ':' instead of '.'
            ok, result = pcall(fn, houseHelper, ...)
            if ok then
                return result, true
            end
        end
    end

    return nil, false
end

local function getHouseCellData(cellDescription)
    local data, called = callHouseHelper({"GetCellData", "getCellData"}, cellDescription)
    if called then
        return data
    end

    if periodicCellResets.hData.cells then
        return periodicCellResets.hData.cells[cellDescription]
    end

    return nil
end

local function isHouseCell(cellDescription)
    local cellData = getHouseCellData(cellDescription)
    if type(cellData) == "table" and cellData.house then
        return true
    end

    local result, called = callHouseHelper({"IsHouseCell", "isHouseCell"}, cellDescription)
    return called and result == true
end

local function canLookupHouseOwner()
    if not houseHelper then
        return false
    end

    return type(houseHelper.GetHouseOwnerName) == "function"
        or type(houseHelper.getHouseOwnerName) == "function"
end

local function getHouseOwnerName(houseName)
    local ownerName, called = callHouseHelper({"GetHouseOwnerName", "getHouseOwnerName"}, houseName)
    if called then
        return ownerName
    end
    return nil
end

local function getLas()
    local value = rawget(_G, "las")
    if type(value) == "table" then
        return value
    end
    return nil
end

local function callLas(methodName, ...)
    local helper = getLas()
    local fn = helper and helper[methodName]
    if type(fn) ~= "function" then
        return nil, false
    end

    local ok, result = pcall(fn, ...)
    if ok then
        return result, true
    end

    ok, result = pcall(fn, helper, ...)
    if ok then
        return result, true
    end

    tes3mp.LogAppend(enumerations.log.WARN,
        "[ArenaMP Core] las." .. methodName .. " вызвал ошибку; вызов пропущен.")
    return nil, false
end

local function getLasTable(name)
    local helper = getLas()
    if helper and type(helper[name]) == "table" then
        return helper[name]
    end
    return nil
end

local function refreshCellThroughLas(cellDescription)
    callLas("loadCell", cellDescription)
end

local function refreshLasMenu(pid, cmd)
    callLas("LASResetControllerMenuG", pid, cmd)
end

local cellResetTimeCheck = 300       -- fallback polling (секунды), используется только если нет ячеек в очереди
local exteriorCellResetTime = 3600
local interiorCellResetTime = 3600
local merchantDayRestock = 1
local runStartupCommandsAutomatically = true
local requiredStaffRank = 1
local viewResetsStaffRank = 2
local viewResetSortTypeCellName = true
local resetExteriorCellsOnly = false
local resetNormalCellsOnRestart = false
local resetWorldKillCountsOnRestart = true
local unlinkCustomRecordsOnReset = true
local resetWorldGlobalsResetOnRestart = true

-- ============================================================================
-- Z001: ОГРАНИЧЕНИЕ ДЛЯ ПРЕСЕТА CO-OP
-- ----------------------------------------------------------------------------
-- MMO-сервер живёт постоянно и рассчитывает на регулярный автосброс мира.
-- CO-OP — это несколько друзей, которые играют сессиями: для них периодический
-- сброс ячеек не «обновляет мир», а стирает результат их же вечера. Поэтому в
-- CO-OP автоматика выключается, а ручные команды администратора остаются.
--
-- Пресет определяется так же, как его задаёт лаунчер: по config.gameMode
-- («ArenaMP CO-OP») и, как запасной признак, по включённым share-флагам
-- прогрессии. Любое поле config.coopResetPolicy имеет приоритет над
-- автоопределением.
-- ============================================================================
local function detectCoopPreset()
    if type(config) ~= "table" then return false end

    if type(config.coopResetPolicy) == "table" and config.coopResetPolicy.forceCoop ~= nil then
        return config.coopResetPolicy.forceCoop == true
    end

    local mode = string.lower(tostring(config.gameMode or ""))
    if string.find(mode, "co%-op", 1, false) or string.find(mode, "coop", 1, false) then
        return true
    end

    -- Запасной признак: лаунчер включает общий журнал и общие темы только в
    -- CO-OP. Одного флага мало, двух — достаточно, чтобы отличить пресет от
    -- точечной настройки MMO-сервера.
    local shared = 0
    if config.shareJournal == true then shared = shared + 1 end
    if config.shareTopics == true then shared = shared + 1 end
    if config.shareFactionRanks == true then shared = shared + 1 end
    if config.shareMapExploration == true then shared = shared + 1 end
    return shared >= 3
end

local coopPolicyDefaults = {
    -- Ставить ли новые таймеры сброса при входе игрока в ячейку.
    scheduleTimers = false,
    -- Выполнять ли сброс по таймеру.
    timerResets = false,
    -- Массовый сброс мира при старте сервера.
    startupResets = false,
    -- Команды массового сброса (/resetall, /pushresets).
    massResetCommands = false,
    -- Ручные команды по текущей ячейке (/clean, /clear, /reset) и очистка
    -- домов остаются доступными: администратор должен уметь починить ячейку.
    manualCommands = true,
    houseCleanup = true
}

local isCoopPreset = detectCoopPreset()

local function coopAllows(key)
    if not isCoopPreset then return true end
    local override = type(config.coopResetPolicy) == "table" and config.coopResetPolicy[key] or nil
    if override ~= nil then return override == true end
    local value = coopPolicyDefaults[key]
    if value == nil then return true end
    return value == true
end

periodicCellResets.isCoopPreset = function() return isCoopPreset end
periodicCellResets.coopAllows = coopAllows

-- Причина отказа, одинаковая для всех заблокированных путей.
local function coopBlockedMessage()
    return color.Yellow .. "[CO-OP] " .. color.White ..
        "Автоматический и массовый сброс ячеек отключён в пресете CO-OP. " ..
        "Доступны /clean, /clear и /cleanuphomes для текущей ячейки или домов.\n"
end

periodicCellResets.exemptCellNamesExact = {
    "Sixth House, Lair",
    "Ebonheart, Logus Manor",
    "ToddTest"
}

periodicCellResets.exemptCellNamesLike = {
    -- Add server-specific pattern exemptions here if needed. Generic instance
    -- cells now use the normal hourly schedule; only private Caius instances
    -- are permanently protected below.
}

local ViewResetsGuiId = 44332202
local SaveCellGuiId = 44332203

local cellResetTimersNeedsInitialSave = false

local function loadCellResetTimersFile()
    -- H011: ampCore.EnsureStartupFiles has already created this file. Never
    -- probe with the standard io.open here: it fails on Cyrillic Windows
    -- profile paths even when the timer file exists, causing a false reset.
    -- jsonInterface uses TES3MP's Unicode-safe io2 on Windows.
    local ok, loaded = pcall(jsonInterface.load, "custom/cellResetTimers.json")
    if ok and type(loaded) == "table" then
        return loaded
    end

    -- Do not let a missing/corrupt timer file take down the whole server.  The
    -- invalid contents are ignored and replaced by a valid empty JSON object on
    -- post-init.
    cellResetTimersNeedsInitialSave = true
    tes3mp.LogAppend(enumerations.log.WARN,
        "[ArenaMP Core] cellResetTimers.json отсутствует или поврежден; будет создан новый пустой файл.")
    return {}
end

local cellResetTimers = loadCellResetTimersFile()
local merchantCells = {}
local startupCommandsHaveRun = false

-- ArenaMP Y043: protect every configured persistent private instance, not only
-- Caius. MFR adds several quest-exclusive interiors using the same dynamic CELL
-- mechanism, so resetall/hourly/soft/full reset must never delete their state.
local function getProtectedPrivateInstanceDefinition(cellDescription)
    if type(cellDescription) ~= "string" or type(config) ~= "table" or
        type(config.privateCellInstances) ~= "table" then
        return nil, nil
    end

    local current = string.lower(cellDescription)
    for key, definition in pairs(config.privateCellInstances) do
        if type(definition) == "table" and definition.enabled ~= false and definition.neverReset == true and
            type(definition.baseCellDescription) == "string" and definition.baseCellDescription ~= "" then
            local suffix = definition.instanceSuffix
            if type(suffix) ~= "string" or suffix == "" then suffix = " - Instance for " end
            local prefix = string.lower(definition.baseCellDescription .. suffix)
            if string.sub(current, 1, #prefix) == prefix then
                return key, definition
            end
        end
    end
    return nil, nil
end

local function isProtectedPrivateInstance(cellDescription)
    return getProtectedPrivateInstanceDefinition(cellDescription) ~= nil
end

-- Backward-compatible exported name for older addons; now true only for the
-- configured Caius definition and kept separately from the generalized guard.
local function isPrivateCaiusInstance(cellDescription)
    local key = getProtectedPrivateInstanceDefinition(cellDescription)
    return key == "caiusHouse"
end

periodicCellResets.isProtectedPrivateInstance = isProtectedPrivateInstance
periodicCellResets.isPrivateCaiusInstance = isPrivateCaiusInstance

-- ============================================================================
-- ЗАНЯТОСТЬ ЯЧЕЙКИ — ИСПРАВЛЕНО (Y-fix: cell reset timer)
-- Раньше занятость определялась как LoadedCells[cell] ~= nil. Но ядро сервера
-- НЕ удаляет ячейку из LoadedCells, когда её покидает последний игрок
-- (logicHandler.UnloadCellForPlayer только снимает visitor'а). В результате
-- любая посещённая хотя бы раз ячейка навсегда считалась "занятой игроками" и
-- по таймеру никогда не сбрасывалась.
--
-- Теперь занятость = есть ли онлайн-игрок, у которого эта ячейка реально
-- загружена (cellsLoaded) или в которой он сейчас находится.
-- ============================================================================
local function getCellOccupantCount(cellDescription)
    local count = 0

    for pid, player in pairs(Players) do
        if player and player:IsLoggedIn() then
            local occupies = false

            if player.cellsLoaded and tableHelper.containsValue(player.cellsLoaded, cellDescription) then
                occupies = true
            else
                local ok, currentCell = pcall(tes3mp.GetCell, pid)
                if ok and currentCell == cellDescription then
                    occupies = true
                end
            end

            if occupies then
                count = count + 1
            end
        end
    end

    return count
end

local function isCellOccupied(cellDescription)
    return getCellOccupantCount(cellDescription) > 0
end

-- Z001: список ячеек, о которых сервер уже знает. Админский интерфейс
-- использует его для пакетного сканирования недвижимости: имя ячейки из
-- таймера означает, что в ней кто-то был и её файл существует.
periodicCellResets.GetKnownCells = function()
    local known = {}
    for cellDescription in pairs(cellResetTimers or {}) do
        table.insert(known, cellDescription)
    end
    for cellDescription in pairs(LoadedCells or {}) do
        if not tableHelper.containsValue(known, cellDescription) then
            table.insert(known, cellDescription)
        end
    end
    return known
end

periodicCellResets.getCellOccupantCount = getCellOccupantCount
periodicCellResets.isCellOccupied = isCellOccupied

-- ============================================================================
-- БЕЗОПАСНАЯ ФУНКЦИЯ ОТПРАВКИ СБРОСА ЯЧЕЙКИ
-- Отправляет пакеты ТОЛЬКО игрокам, у которых ячейка загружена
-- ============================================================================
local function safeSendCellReset(cellDescription, allPlayers)
    tes3mp.ClearCellsToReset()
    tes3mp.AddCellToReset(cellDescription)

    for pid, player in pairs(Players) do
        if player and player:IsLoggedIn() then
            local hasCellLoaded = false
            if player.cellsLoaded then
                hasCellLoaded = tableHelper.containsValue(player.cellsLoaded, cellDescription)
            end

            if hasCellLoaded or allPlayers then
                tes3mp.SendCellReset(pid, false)
                tes3mp.LogAppend(enumerations.log.VERBOSE,
                    "[ArenaMP Core] Sent cell reset for " .. cellDescription .. " to pid " .. pid)
            end
        end
    end
end

-- ============================================================================
-- БЕЗОПАСНАЯ ФУНКЦИЯ УДАЛЕНИЯ ОБЪЕКТОВ ИЗ ЯЧЕЙКИ
-- Правильное использование API: ClearObjectList() без аргументов,
-- SetObjectListPid(pid), SendObjectDelete(false)
-- ============================================================================
local function safeSendObjectDelete(cellDescription)
    for pid, player in pairs(Players) do
        if player and player:IsLoggedIn() then
            local hasCellLoaded = false
            if player.cellsLoaded then
                hasCellLoaded = tableHelper.containsValue(player.cellsLoaded, cellDescription)
            end

            if hasCellLoaded then
                tes3mp.ClearObjectList()
                tes3mp.SetObjectListPid(pid)
                tes3mp.SetObjectListCell(cellDescription)
                tes3mp.SendObjectDelete(false)
            end
        end
    end
end

-- ============================================================================
-- БЕЗОПАСНЫЙ СБРОС ДАННЫХ ЯЧЕЙКИ (общий код)
-- ============================================================================
local function safeClearCellData(cellDescription)
    if not LoadedCells[cellDescription] then
        return false
    end

    if LoadedCells[cellDescription].isResetting then
        tes3mp.LogAppend(enumerations.log.WARN,
            "Ячейка " .. cellDescription .. " уже в процессе сброса, пропускаем.")
        return false
    end

    LoadedCells[cellDescription].isResetting = true
    LoadedCells[cellDescription].data.objectData = {}
    LoadedCells[cellDescription].data.packets = {}
    LoadedCells[cellDescription]:EnsurePacketTables()
    LoadedCells[cellDescription].data.loadState.hasFullActorList = false
    LoadedCells[cellDescription].data.loadState.hasFullContainerData = false
    LoadedCells[cellDescription].unusableContainerUniqueIndexes = {}
    LoadedCells[cellDescription].data.lastVisit = {}
    LoadedCells[cellDescription].isRequestingContainerData = false
    LoadedCells[cellDescription].containerRequestTime = nil
    LoadedCells[cellDescription].containerRequestPid = nil
    LoadedCells[cellDescription]:ClearRecordLinks()
    LoadedCells[cellDescription].isResetting = false

    return true
end

-- ============================================================================

local runStartupCommands = function(pid)
    for _, scriptName in pairs(config.worldStartupScripts) do
        logicHandler.RunConsoleCommandOnPlayer(pid, "startscript " .. scriptName, false)
    end
    WorldInstance.coreVariables.hasRunStartupScripts = true
    startupCommandsHaveRun = true
end

-- ============================================================================
-- УМНЫЙ ПЛАНИРОВЩИК СБРОСОВ (Priority Queue / Min-Heap)
-- Вместо одного таймера на 300 сек, который каждый раз перебирает всю таблицу,
-- используем min-heap: таймер спит ровно до следующего запланированного сброса.
-- Это устраняет лишние итерации и снижает нагрузку на сервер.
-- ============================================================================
local resetHeap = {}    -- min-heap: { time = unixtime, cell = cellDescription }

-- Вставка в heap (sift-up)
local function heapPush(t, cell)
    local entry = {time = t, cell = cell}
    table.insert(resetHeap, entry)
    local i = #resetHeap
    while i > 1 do
        local parent = math.floor(i / 2)
        if resetHeap[parent].time > resetHeap[i].time then
            resetHeap[parent], resetHeap[i] = resetHeap[i], resetHeap[parent]
            i = parent
        else
            break
        end
    end
end

-- Извлечение минимума из heap (sift-down)
local function heapPop()
    local n = #resetHeap
    if n == 0 then return nil end
    local top = resetHeap[1]
    resetHeap[1] = resetHeap[n]
    resetHeap[n] = nil
    n = n - 1
    local i = 1
    while true do
        local smallest = i
        local l, r = 2 * i, 2 * i + 1
        if l <= n and resetHeap[l].time < resetHeap[smallest].time then smallest = l end
        if r <= n and resetHeap[r].time < resetHeap[smallest].time then smallest = r end
        if smallest == i then break end
        resetHeap[i], resetHeap[smallest] = resetHeap[smallest], resetHeap[i]
        i = smallest
    end
    return top
end

-- Добавить ячейку в heap-расписание
local function scheduleCell(cellDescription, resetTime)
    heapPush(resetTime, cellDescription)
end

-- Полная перестройка heap из cellResetTimers (после загрузки с диска)
local function rebuildHeap()
    resetHeap = {}
    for cellDescription, resetTime in pairs(cellResetTimers) do
        heapPush(resetTime, cellDescription)
    end
end

-- Вычислить задержку до следующего тика (пропускаем устаревшие/удалённые записи)
local function nextTimerDelay()
    while #resetHeap > 0 do
        local top = resetHeap[1]
        local current = cellResetTimers[top.cell]
        -- Запись актуальна: время совпадает и ячейка всё ещё ожидает сброса
        if current and current == top.time then
            local delay = top.time - os.time()
            if delay < 1 then delay = 1 end
            -- Не более cellResetTimeCheck сек (защита от слишком далёких таймеров)
            if delay > cellResetTimeCheck then delay = cellResetTimeCheck end
            return delay
        else
            heapPop() -- устаревшая/удалённая запись — выбрасываем
        end
    end
    return cellResetTimeCheck -- нет активных таймеров — стандартный интервал
end

-- ============================================================================
-- ДЕБАУНС ЗАПИСИ JSON
-- Запись cellResetTimers — дорогая I/O операция.
-- При частых добавлениях (много игроков меняют ячейки) группируем записи:
-- реальный flush происходит не чаще раз в saveDebounceTime секунд.
-- ============================================================================
local saveDebounceTime = 10   -- секунд между реальными записями на диск
local saveScheduled = false
local lastSaveTime = 0

local function flushSave()
    tableHelper.cleanNils(cellResetTimers)
    jsonInterface.save("custom/cellResetTimers.json", cellResetTimers)
    lastSaveTime = os.time()
    saveScheduled = false
end

local SaveCellResetTimers = function(forceImmediate)
    if forceImmediate then
        flushSave()
        return
    end
    if os.time() - lastSaveTime >= saveDebounceTime then
        flushSave()
    else
        -- Отложить: сохранится при следующем тике планировщика
        saveScheduled = true
    end
end

local LoadCellResetTimers = function()
    if cellResetTimers == nil then
        cellResetTimers = {}
        SaveCellResetTimers(true)
    end
    -- Строим heap из загруженных данных сразу
    rebuildHeap()
end

local removeCustomRecordsFromResetCell = function(cellDescription)
    if unlinkCustomRecordsOnReset then
        for _, storeType in pairs(config.recordStoreLoadOrder) do
            if RecordStores[storeType] and RecordStores[storeType].data.recordLinks then
                local recordLinks = RecordStores[storeType].data.recordLinks
                for recordId, recordData in pairs(recordLinks) do
                    if recordData.cells and tableHelper.containsValue(recordData.cells, cellDescription) then
                        local linkIndex = tableHelper.getIndexByValue(recordData.cells, cellDescription)
                        if linkIndex then
                            recordLinks[recordId].cells[linkIndex] = nil
                        end
                        if not RecordStores[storeType]:HasLinks(recordId) then
                            table.insert(RecordStores[storeType].data.unlinkedRecordsToCheck, recordId)
                        end
                    end
                end
                tableHelper.cleanNils(recordLinks)
            end
        end
        for _, storeType in pairs(config.recordStoreLoadOrder) do
            if RecordStores[storeType] then
                RecordStores[storeType]:Save()
            end
        end
    end
end

local getCellsArray = function(directory)
    local i, t, popen = 0, {}, io.popen
    local pfile = nil
    if tes3mp.GetOperatingSystemType() == "Windows" then
        pfile = popen('dir "' .. directory .. '" /b')
    else
        pfile = popen('find "' .. directory .. '" -maxdepth 1 -type f -printf "%f\\n"')
    end
    for filename in pfile:lines() do
        i = i + 1
        t[i] = filename
    end
    pfile:close()
    return t
end

local hasCellBackup = function(cellDescription)
    local backupDir = tes3mp.GetDataPath() .. "/cell/backup/"
    local fixedCellDescription = fileHelper.fixFilename(cellDescription)
    local backupFile = backupDir .. fixedCellDescription .. ".json"
    local file = io.open(backupFile, "r")
    if file then
        file:close()
        return true
    end
    return false
end

local saveCellBackup = function(cellDescription)
    local backupDir = tes3mp.GetDataPath() .. "/cell/backup/"
    local fixedCellDescription = fileHelper.fixFilename(cellDescription)

    os.execute((tes3mp.GetOperatingSystemType() == "Windows" and 'mkdir "' or 'mkdir -p "') .. backupDir .. '"')

    if not LoadedCells[cellDescription] then
        logicHandler.LoadCell(cellDescription)
    end

    if LoadedCells[cellDescription] then
        local cellData = LoadedCells[cellDescription].data
        local success = jsonInterface.save("cell/backup/" .. fixedCellDescription .. ".json", cellData)
        if success then
            tes3mp.LogAppend(enumerations.log.INFO, "Бэкап для ячейки " .. cellDescription .. " успешно сохранён.")
            return true
        else
            tes3mp.LogAppend(enumerations.log.ERROR, "Не удалось сохранить бэкап для ячейки " .. cellDescription)
            return false
        end
    else
        tes3mp.LogAppend(enumerations.log.ERROR, "Ячейка " .. cellDescription .. " не загружена, бэкап не создан.")
        return false
    end
end

local clearCellBackup = function(cellDescription)
    local backupDir = tes3mp.GetDataPath() .. "/cell/backup/"
    local fixedCellDescription = fileHelper.fixFilename(cellDescription)
    local backupFile = backupDir .. fixedCellDescription .. ".json"
    if hasCellBackup(cellDescription) then
        os.remove(backupFile)
        return true
    end
    return false
end

local restoreCellFromBackup = function(cellDescription)
    local fixedCellDescription = fileHelper.fixFilename(cellDescription)
    local sourceFile = tes3mp.GetDataPath() .. "/cell/backup/" .. fixedCellDescription .. ".json"
    local backupDir = tes3mp.GetDataPath() .. "/cell/backup/"

    os.execute((tes3mp.GetOperatingSystemType() == "Windows" and 'mkdir "' or 'mkdir -p "') .. backupDir .. '"')

    local file = io.open(sourceFile, "r")
    if not file then
        tes3mp.LogAppend(enumerations.log.ERROR, "Не удалось открыть файл бэкапа: " .. sourceFile)
        return false
    end
    file:close()

    local cellData = jsonInterface.load("cell/backup/" .. fixedCellDescription .. ".json")
    if not cellData then
        tes3mp.LogAppend(enumerations.log.ERROR, "Не удалось загрузить данные из файла бэкапа: " .. sourceFile)
        return false
    end

    local unloadAtEnd = false
    if not LoadedCells[cellDescription] then
        logicHandler.LoadCell(cellDescription)
        unloadAtEnd = true
    end

    if not LoadedCells[cellDescription] then
        tes3mp.LogAppend(enumerations.log.ERROR, "Не удалось загрузить ячейку " .. cellDescription)
        return false
    end

    if LoadedCells[cellDescription].isResetting then
        tes3mp.LogAppend(enumerations.log.WARN, "Ячейка " .. cellDescription .. " уже в процессе сброса, пропускаем.")
        return false
    end

    LoadedCells[cellDescription].isResetting = true

    LoadedCells[cellDescription].data.objectData = {}
    LoadedCells[cellDescription].data.packets = {}
    LoadedCells[cellDescription]:EnsurePacketTables()
    LoadedCells[cellDescription].data.loadState.hasFullActorList = false
    LoadedCells[cellDescription].data.loadState.hasFullContainerData = false
    LoadedCells[cellDescription].unusableContainerUniqueIndexes = {}
    LoadedCells[cellDescription].data.lastVisit = {}
    LoadedCells[cellDescription].isRequestingContainerData = false
    LoadedCells[cellDescription].containerRequestTime = nil
    LoadedCells[cellDescription].containerRequestPid = nil
    LoadedCells[cellDescription]:ClearRecordLinks()

    local success = jsonInterface.save("cell/" .. fixedCellDescription .. ".json", cellData)
    if not success then
        tes3mp.LogAppend(enumerations.log.ERROR, "Не удалось сохранить восстановленные данные для ячейки " .. cellDescription)
        LoadedCells[cellDescription].isResetting = false
        return false
    end

    -- Отправляем сброс ДО выгрузки ячейки (пока visitors ещё доступны)
    safeSendCellReset(cellDescription)

    LoadedCells[cellDescription] = nil
    logicHandler.LoadCell(cellDescription)

    if not LoadedCells[cellDescription] then
        tes3mp.LogAppend(enumerations.log.ERROR, "Не удалось перезагрузить ячейку " .. cellDescription .. " после восстановления.")
        return false
    end

    LoadedCells[cellDescription]:Save()
    LoadedCells[cellDescription].isResetting = false

    if unloadAtEnd then
        logicHandler.UnloadCell(cellDescription)
    end

    return true
end

local resetCellsOnStartup = function()
    if resetNormalCellsOnRestart then
        local clearedCellCount = 0
        local directory = tes3mp.GetModDir() .. "/cell/"
        local cells = getCellsArray(directory)

        for _, cellFile in pairs(cells) do
            local splitFileExtension = cellFile:split(".")
            local cellName = splitFileExtension[1]
            local preventDeletion = false

            local cellData = getHouseCellData(cellName)
            if cellData and cellData.house then
                preventDeletion = true
            end

            if not preventDeletion then
                if string.match(string.lower(cellFile), ".json") then
                    os.remove(directory .. cellFile)
                    clearedCellCount = clearedCellCount + 1
                end
            end
        end

        if clearedCellCount > 0 then
            print("Всего удалено ячеек: " .. clearedCellCount)
        end
    end

    local worldData = WorldInstance and WorldInstance.data or nil
    local worldDataChanged = false

    if resetWorldKillCountsOnRestart then
        local kills = worldData and worldData.kills or nil
        if type(kills) == "table" then
            local clearedCellKills = 0
            for refId, killCount in pairs(kills) do
                clearedCellKills = clearedCellKills + (tonumber(killCount) or 0)
                kills[refId] = 0
            end
            print("Всего убийств сброшено: " .. clearedCellKills)
            worldDataChanged = true
        else
            tes3mp.LogAppend(enumerations.log.WARN,
                "[ArenaMP Core] WorldInstance.data.kills ещё не создан; сброс kill counters при старте пропущен.")
        end
    end

    if resetWorldGlobalsResetOnRestart then
        local clientVariables = worldData and worldData.clientVariables or nil
        local globals = clientVariables and clientVariables.globals or nil
        if type(globals) == "table" then
            local clearedGlobals = 0
            for key, _ in pairs(globals) do
                globals[key] = nil
                clearedGlobals = clearedGlobals + 1
            end
            tableHelper.cleanNils(globals)
            print("Всего глобальных переменных сброшено: " .. clearedGlobals)
            worldDataChanged = true
        else
            -- Fresh/legacy world profiles can legitimately have no
            -- clientVariables table yet.  Treat that as an empty globals set,
            -- not as a fatal startup error.
            tes3mp.LogAppend(enumerations.log.WARN,
                "[ArenaMP Core] WorldInstance.data.clientVariables.globals ещё не создан; сброс globals при старте пропущен.")
        end
    end

    if worldDataChanged and WorldInstance and type(WorldInstance.QuicksaveToDrive) == "function" then
        WorldInstance:QuicksaveToDrive()
    end
end


local specificCellFunctionsToAlwaysRun = function(pid, cellDescription)
end

local nameLikeCellExemptions = function(cellDescription)
    local currentCell = string.lower(cellDescription)
    for _, exemptCellName in pairs(periodicCellResets.exemptCellNamesLike) do
        local exemptCell = string.lower(exemptCellName)
        if string.match(currentCell, exemptCell) then
            return true
        end
    end
    return false
end

local interiorCellExemption = function(cellDescription)
    if resetExteriorCellsOnly then
        local currentCell = string.lower(cellDescription)
        if not string.match(currentCell, patterns.exteriorCell) then
            return true
        end
    end
    return false
end

-- ============================================================================
-- ЕДИНАЯ ПРОВЕРКА: НУЖНО ЛИ ДОБАВЛЯТЬ ЯЧЕЙКУ В СПИСОК СБРОСА
-- Все условия исключения собраны здесь, чтобы не дублировать логику.
-- Возвращает false если ячейку сбрасывать не нужно.
-- ============================================================================
local function isCellEligibleForReset(cellDescription)
    -- Персональный дом Кая никогда не сбрасывается. Остальные generated
    -- instances используют обычный часовой таймер, если не внесены в явные
    -- исключения сервера.
    if isProtectedPrivateInstance(cellDescription) then
        return false
    end

    -- Исключения по точному имени
    if tableHelper.containsValue(periodicCellResets.exemptCellNamesExact, cellDescription) then
        return false
    end
    -- Исключения по паттерну ("Instance for [RP]" и подобные)
    if nameLikeCellExemptions(cellDescription) then
        return false
    end
    -- Исключение внутренних ячеек если включён режим только-внешних
    if interiorCellExemption(cellDescription) then
        return false
    end
    -- Чёрный список las
    local rcBlacklist = getLasTable("rc_blacklist")
    if rcBlacklist and rcBlacklist[cellDescription] then
        return false
    end
    -- Домовые ячейки защищаем через опциональный houseHelper или локальный кэш.
    if isHouseCell(cellDescription) then
        return false
    end
    return true
end

local removeDeletedCellsFromResetTimers = function()
    local doSave = false
    tes3mp.LogAppend(enumerations.log.INFO, "-=-=-ПРОВЕРКА ТАЙМЕРОВ СБРОСА ЯЧЕЕК-=-=-")
    local toRemove = {}
    for cellDescription, _ in pairs(cellResetTimers) do
        -- Сначала: убираем всё что вообще не должно сбрасываться (дома, исключения)
        if not isCellEligibleForReset(cellDescription) then
            table.insert(toRemove, cellDescription)
            tes3mp.LogAppend(enumerations.log.INFO,
                "Удаление таймера для \"" .. cellDescription .. "\" — дом или исключение.")
            doSave = true
        else
            -- Затем: проверяем существование файла ячейки на диске
            local fixedCellDescription = fileHelper.fixFilename(cellDescription)
            if fixedCellDescription then
                local cellFile = tes3mp.GetDataPath() .. "/cell/" .. fixedCellDescription .. ".json"
                local file = io.open(cellFile, "r")
                if not file then
                    table.insert(toRemove, cellDescription)
                    tes3mp.LogAppend(enumerations.log.INFO,
                        "Удаление таймера для \"" .. fixedCellDescription .. "\" — файл ячейки не существует.")
                    removeCustomRecordsFromResetCell(cellDescription)
                    doSave = true
                else
                    file:close()
                end
            end
        end
    end
    for _, cellDescription in ipairs(toRemove) do
        cellResetTimers[cellDescription] = nil
    end
    if doSave then
        SaveCellResetTimers(true)
    end
end

-- ============================================================================
-- ОЧИСТКА ДОМОВ БЕЗ ХОЗЯИНА
-- Сбрасывает ячейки домов, у которых нет владельца.
-- Вызывается при старте сервера и по команде /cleanuphomes.
-- ============================================================================
local function cleanUnownedHouses()
    if not canLookupHouseOwner() then
        tes3mp.LogAppend(enumerations.log.WARN,
            "[ArenaMP Core] houseHelper отсутствует или не имеет API владельцев; очистка домов пропущена.")
        return 0, 0
    end
    local cleaned = 0
    local skipped = 0

    for cellDescription, cellData in pairs(periodicCellResets.hData.cells or {}) do
        if cellData and cellData.house then
            local houseName = cellData.house
            -- Проверяем наличие владельца через актуальный API
            local ownerName = getHouseOwnerName(houseName)
            if not ownerName then
                if isCellOccupied(cellDescription) then
                    -- В ячейке кто-то есть — пропустим до следующей проверки
                    skipped = skipped + 1
                    tes3mp.LogAppend(enumerations.log.WARN,
                        "[ArenaMP Core] Пропуск " .. cellDescription .. " — ячейка загружена (игроки внутри).")
                else
                    tes3mp.LogAppend(enumerations.log.INFO,
                        "[ArenaMP Core] Сброс пустующего дома: " .. houseName .. " (" .. cellDescription .. ")")
                    local ok, err = pcall(function()
                        periodicCellResets.ResetHome(cellDescription)
                    end)
                    if ok then
                        cleaned = cleaned + 1
                    else
                        tes3mp.LogAppend(enumerations.log.ERROR,
                            "[ArenaMP Core] Ошибка при сбросе " .. cellDescription .. ": " .. tostring(err))
                    end
                end
            end
        end
    end

    tes3mp.LogAppend(enumerations.log.INFO,
        "[ArenaMP Core] Завершено: очищено " .. cleaned .. " ячеек, пропущено " .. skipped .. " (заняты).")
    return cleaned, skipped
end

periodicCellResets.cleanUnownedHouses = cleanUnownedHouses

-- ============================================================================
-- БЕЗОПАСНЫЙ СБРОС ОДНОЙ НЕЗАГРУЖЕННОЙ ЯЧЕЙКИ
-- (для таймеров, pushResets и т.д.)
-- ============================================================================
local function safeResetUnloadedCell(cellDescription)
    -- Final safety gate for timer/admin paths and stale timer JSON from older
    -- versions: never allow a private Caius instance to reach reset code.
    if isProtectedPrivateInstance(cellDescription) then
        cellResetTimers[cellDescription] = nil
        return false
    end

    if hasCellBackup(cellDescription) then
        if restoreCellFromBackup(cellDescription) then
            cellResetTimers[cellDescription] = nil
            refreshCellThroughLas(cellDescription)
            return true
        end
    end

    -- Ячейка может уже висеть в LoadedCells без единого игрока внутри — это
    -- нормальное состояние ядра. Сбрасываем её так же, как незагруженную.
    if isCellOccupied(cellDescription) then
        return false
    end

    if not LoadedCells[cellDescription] then
        logicHandler.LoadCell(cellDescription)
    end

    if not LoadedCells[cellDescription] then
        return false
    end

    if not safeClearCellData(cellDescription) then
        return false
    end

    safeSendObjectDelete(cellDescription)
    safeSendCellReset(cellDescription)

    -- Записываем очищенные данные на диск и выгружаем ячейку: внутри никого
    -- нет, а следующий вход перечитает её уже пустой из JSON. Без этого
    -- очистка жила только в памяти до ближайшего Quicksave.
    LoadedCells[cellDescription]:Save()
    logicHandler.UnloadCell(cellDescription)

    cellResetTimers[cellDescription] = nil
    removeCustomRecordsFromResetCell(cellDescription)
    refreshCellThroughLas(cellDescription)
    return true
end

-- ============================================================================
-- doCellReset — ИСПРАВЛЕНО
-- ============================================================================
local doCellReset = function(pid, cellDescription)
    if isProtectedPrivateInstance(cellDescription) then
        tes3mp.SendMessage(pid, color.Yellow .. "[Сброс Ячеек]: " .. color.Error ..
            "Личный квестовый инстанс не сбрасывается.\n")
        return
    end

    local cellData = getHouseCellData(cellDescription)
    if cellData and cellData.house then
        tes3mp.SendMessage(pid, color.Yellow .. "[Сброс Ячеек]: " .. color.Error .. "Эта ячейка принадлежит дому и не может быть сброшена.\n")
        return
    end

    if hasCellBackup(cellDescription) then
        if restoreCellFromBackup(cellDescription) then
            tes3mp.SendMessage(pid, color.Yellow .. "[Сброс Ячеек]: " .. color.Turquoise .. cellDescription .. color.White .. " была восстановлена из бэкапа.\n")
            cellResetTimers[cellDescription] = nil
            SaveCellResetTimers(true)
            refreshCellThroughLas(cellDescription)
            return
        else
            tes3mp.SendMessage(pid, color.Yellow .. "[Сброс Ячеек]: " .. color.Error .. "Не удалось восстановить бэкап для " .. cellDescription .. ". Выполняется стандартный сброс.\n")
        end
    end

    local txt = color.Error .. "Эта ячейка не находится в таблице сброса."
    if cellResetTimers[cellDescription] then
        local unloadAtEnd = false
        if not LoadedCells[cellDescription] then
            logicHandler.LoadCell(cellDescription)
            unloadAtEnd = true
        end

        if LoadedCells[cellDescription] then
            if not safeClearCellData(cellDescription) then
                tes3mp.SendMessage(pid, color.Yellow .. "[Сброс Ячеек]: " .. color.Error .. "Ячейка уже сбрасывается.\n")
                return
            end
        end

        safeSendObjectDelete(cellDescription)
        safeSendCellReset(cellDescription)

        if unloadAtEnd then
            logicHandler.UnloadCell(cellDescription)
        end

        cellResetTimers[cellDescription] = nil
        SaveCellResetTimers(true)
        removeCustomRecordsFromResetCell(cellDescription)
        txt = color.Turquoise .. cellDescription .. color.White .. " была сброшена и больше не находится в списке ячеек для сброса."
    end
    tes3mp.SendMessage(pid, color.Yellow .. "[Сброс Ячеек]: " .. txt .. "\n")
end

local pushForCellReset = function(pid, cmd)
    if Players[pid].data.settings.staffRank >= requiredStaffRank then
        if not cmd[2] then
            tes3mp.SendMessage(pid, 'Неверные данные! Используйте /resetcell "Название Ячейки"\n')
            return
        end
        local inputConcatenation = tableHelper.concatFromIndex(cmd, 2)
        local cellDescription = string.gsub(inputConcatenation, '"', '')
        doCellReset(pid, cellDescription)
        refreshCellThroughLas(cellDescription)
    end
end
customCommandHooks.registerCommand("reset", pushForCellReset)
customCommandHooks.registerCommand("RESET", pushForCellReset)

periodicCellResets.ResetThisCell = function(pid, cmd)
    if Players[pid].data.settings.staffRank >= requiredStaffRank then
        local cellDescription = tes3mp.GetCell(pid)
        doCellReset(pid, cellDescription)
        refreshCellThroughLas(cellDescription)
        refreshLasMenu(pid, cmd)
    end
end
customCommandHooks.registerCommand("resetthis", periodicCellResets.ResetThisCell)
customCommandHooks.registerCommand("resett", periodicCellResets.ResetThisCell)

-- ============================================================================
-- ResetCell — ИСПРАВЛЕНО
-- ============================================================================
periodicCellResets.ResetCell = function(cellDescription)
    if isProtectedPrivateInstance(cellDescription) then
        return
    end

    local cellData = getHouseCellData(cellDescription)
    if cellData and cellData.house then
        return
    end
    if hasCellBackup(cellDescription) then
        if restoreCellFromBackup(cellDescription) then
            cellResetTimers[cellDescription] = nil
            SaveCellResetTimers(true)
            refreshCellThroughLas(cellDescription)
            return
        end
    end
    local unloadAtEnd = false
    if not LoadedCells[cellDescription] then
        logicHandler.LoadCell(cellDescription)
        unloadAtEnd = true
    end

    if LoadedCells[cellDescription] then
        safeClearCellData(cellDescription)
    end

    safeSendObjectDelete(cellDescription)
    safeSendCellReset(cellDescription)

    if unloadAtEnd then
        logicHandler.UnloadCell(cellDescription)
    end
end

-- ============================================================================
-- ResetHome — ИСПРАВЛЕНО
-- ============================================================================
periodicCellResets.ResetHome = function(cellDescription)
    if isProtectedPrivateInstance(cellDescription) then
        tes3mp.LogAppend(enumerations.log.INFO,
            "[ArenaMP Core] Пропуск защищённого личного инстанса: " .. cellDescription)
        return
    end

    tes3mp.LogAppend(enumerations.log.INFO, "[ArenaMP Core] Начало процесса сброса дома для ячейки: " .. cellDescription)

    if hasCellBackup(cellDescription) then
        tes3mp.LogAppend(enumerations.log.INFO, "[ArenaMP Core] Обнаружен бэкап для ячейки: " .. cellDescription)
        if restoreCellFromBackup(cellDescription) then
            tes3mp.LogAppend(enumerations.log.INFO, "[ArenaMP Core] Ячейка " .. cellDescription .. " успешно восстановлена из бэкапа.")
            cellResetTimers[cellDescription] = nil
            SaveCellResetTimers(true)
            return
        else
            tes3mp.LogAppend(enumerations.log.ERROR, "[ArenaMP Core] Не удалось восстановить бэкап для ячейки: " .. cellDescription .. ". Выполняется стандартный сброс.")
        end
    else
        tes3mp.LogAppend(enumerations.log.INFO, "[ArenaMP Core] Бэкап для ячейки " .. cellDescription .. " не найден. Выполняется стандартный сброс.")
    end

    local unloadAtEnd = false
    if not LoadedCells[cellDescription] then
        logicHandler.LoadCell(cellDescription)
        unloadAtEnd = true
    end

    if LoadedCells[cellDescription] then
        safeClearCellData(cellDescription)
    end

    safeSendObjectDelete(cellDescription)
    safeSendCellReset(cellDescription)

    if unloadAtEnd then
        logicHandler.UnloadCell(cellDescription)
    end

    removeCustomRecordsFromResetCell(cellDescription)
    tes3mp.LogAppend(enumerations.log.INFO, "[ArenaMP Core] Завершён сброс ячейки " .. cellDescription)
end

-- ============================================================================
-- pushCellResetsEarly — ИСПРАВЛЕНО
-- ============================================================================
local pushCellResetsEarly = function(pid, cmd)
    -- Z001: массовые сбросы запрещены в CO-OP.
    if not coopAllows("massResetCommands") then
        tes3mp.SendMessage(pid, coopBlockedMessage())
        return
    end
    if Players[pid] and Players[pid]:IsLoggedIn() and Players[pid].data.settings.staffRank >= requiredStaffRank then
        local markTime = os.time()
        local doSave = false
        local txt = color.Error .. "Нет ячеек, готовых к сбросу."
        local cellsReset = 0
        local cellLoaded = 0

        if not tableHelper.isEmpty(cellResetTimers) then
            -- Собираем список ячеек для сброса, чтобы не менять таблицу во время итерации
            local cellsToReset = {}
            for cellDescription, cellResetTime in pairs(cellResetTimers) do
                local skip = not isCellEligibleForReset(cellDescription)
                if skip then
                    cellResetTimers[cellDescription] = nil
                    doSave = true
                end
                if not skip and markTime >= cellResetTime then
                    if not isCellOccupied(cellDescription) then
                        table.insert(cellsToReset, cellDescription)
                    else
                        cellLoaded = cellLoaded + 1
                    end
                end
            end

            for _, cellDescription in ipairs(cellsToReset) do
                local success, err = pcall(function()
                    if safeResetUnloadedCell(cellDescription) then
                        cellsReset = cellsReset + 1
                        doSave = true
                    end
                end)
                if not success then
                    tes3mp.LogMessage(enumerations.log.ERROR,
                        "[ArenaMP Core] " .. cellDescription .. ": " .. tostring(err))
                end
            end
        end

        if cellsReset > 1 then
            txt = color.White .. cellsReset .. color.Yellow .. " ячеек было сброшено."
        elseif cellsReset == 1 then
            txt = color.White .. "1" .. color.Yellow .. " ячейка была сброшена."
        end
        if cellLoaded > 1 then
            txt = txt .. "\n" .. color.White .. cellLoaded .. color.Error .. " ячеек не удалось сбросить, так как в них находятся игроки."
        elseif cellLoaded == 1 then
            txt = txt .. "\n" .. color.White .. cellLoaded .. color.Error .. " ячейка не могла быть сброшена, так как в ней находятся игроки."
        end
        tes3mp.SendMessage(pid, txt .. "\n")
        if doSave then
            SaveCellResetTimers(true)
        end
    end
end
customCommandHooks.registerCommand("pushresets", pushCellResetsEarly)
customCommandHooks.registerCommand("PUSHRESETS", pushCellResetsEarly)

periodicCellResets.pushCellResetsEarly = function(pid, cmd)
    pushCellResetsEarly(pid, cmd)
    refreshLasMenu(pid, cmd)
end

-- ============================================================================
-- pushResetAllCells — ИСПРАВЛЕНО
-- ============================================================================
local pushResetAllCells = function(pid, cmd)
    -- Z001: массовые сбросы запрещены в CO-OP.
    if not coopAllows("massResetCommands") then
        tes3mp.SendMessage(pid, coopBlockedMessage())
        return
    end
    if Players[pid] and Players[pid]:IsLoggedIn() and Players[pid].data.settings.staffRank >= requiredStaffRank then
        local doSave = false
        local txt = color.Error .. "Нет ячеек, готовых к сбросу."
        local cellsReset = 0
        local cellLoaded = 0

        if not tableHelper.isEmpty(cellResetTimers) then
            local cellsToReset = {}
            local cellsToReschedule = {}

            for cellDescription, _ in pairs(cellResetTimers) do
                local skip = not isCellEligibleForReset(cellDescription)
                if skip then
                    cellResetTimers[cellDescription] = nil
                    doSave = true
                end
                if not skip then
                    if not isCellOccupied(cellDescription) then
                        table.insert(cellsToReset, cellDescription)
                    else
                        -- Ячейка занята игроками — запланируем повторную попытку
                        table.insert(cellsToReschedule, cellDescription)
                        cellLoaded = cellLoaded + 1
                    end
                end
            end

            -- Переносим занятые ячейки: перепланируем сброс через cellResetTimeCheck сек
            for _, cellDescription in ipairs(cellsToReschedule) do
                local retryTime = os.time() + cellResetTimeCheck
                cellResetTimers[cellDescription] = retryTime
                heapPush(retryTime, cellDescription)
                doSave = true
                tes3mp.LogAppend(enumerations.log.INFO,
                    "[ArenaMP Core] Ячейка " .. cellDescription .. " занята — перепланирован сброс через " .. cellResetTimeCheck .. " сек.")
            end

            -- Сбрасываем свободные ячейки
            for _, cellDescription in ipairs(cellsToReset) do
                local success, err = pcall(function()
                    if safeResetUnloadedCell(cellDescription) then
                        cellsReset = cellsReset + 1
                        doSave = true
                    end
                end)
                if not success then
                    tes3mp.LogMessage(enumerations.log.ERROR,
                        "[ArenaMP Core] " .. cellDescription .. ": " .. tostring(err))
                end
            end
        end

        if cellsReset > 1 then
            txt = color.White .. cellsReset .. color.Yellow .. " ячеек было сброшено."
        elseif cellsReset == 1 then
            txt = color.White .. "1" .. color.Yellow .. " ячейка была сброшена."
        elseif cellLoaded == 0 then
            txt = color.Error .. "Нет ячеек, готовых к сбросу."
        end
        if cellLoaded > 1 then
            txt = txt .. "\n" .. color.White .. cellLoaded .. color.Yellow
                .. " ячеек с игроками — сброс запланирован автоматически."
        elseif cellLoaded == 1 then
            txt = txt .. "\n" .. color.White .. "1" .. color.Yellow
                .. " ячейка с игроками — сброс запланирован автоматически."
        end
        tes3mp.SendMessage(pid, txt .. "\n")
        if doSave then
            SaveCellResetTimers(true)
            -- Перезапускаем таймер с оптимальной задержкой после перепланирования
            tes3mp.RestartTimer(GlobalCellResetTimer, time.seconds(nextTimerDelay()))
        end
    end
end
customCommandHooks.registerCommand("resetall", pushResetAllCells)
customCommandHooks.registerCommand("resetAll", pushResetAllCells)
customCommandHooks.registerCommand("ResetAll", pushResetAllCells)
customCommandHooks.registerCommand("RESETALL", pushResetAllCells)

periodicCellResets.pushResetAllCells = function(pid, cmd)
    pushResetAllCells(pid, cmd)
    refreshLasMenu(pid, cmd)
end

customEventHooks.registerHandler("OnObjectDialogueChoice", function(eventStatus, pid, cellDescription, objects)
    if Players[pid] and Players[pid]:IsLoggedIn() then
        if not tableHelper.containsValue(periodicCellResets.exemptCellNamesExact, cellDescription) and not nameLikeCellExemptions(cellDescription) and not interiorCellExemption(cellDescription) then
            for _, object in pairs(objects) do
                if object.dialogueChoiceType == 3 then
                    if not merchantCells[cellDescription] then
                        merchantCells[cellDescription] = WorldInstance.data.time.daysPassed + merchantDayRestock
                    end
                end
            end
        end
    end
end)

local checkMerchantCell = function()
    local doSave = false
    local currentDay = WorldInstance.data.time.daysPassed
    -- Собираем ключи заранее чтобы не изменять таблицу в цикле итерации
    local toRemove = {}
    for cellDescription, daySaved in pairs(merchantCells) do
        if not isCellOccupied(cellDescription) and daySaved and currentDay >= daySaved then
            cellResetTimers[cellDescription] = 0
            -- Без heapPush запись с временем 0 не попадала в планировщик и
            -- ячейка торговца ждала сброса до перезапуска сервера.
            heapPush(0, cellDescription)
            table.insert(toRemove, cellDescription)
            doSave = true
        end
    end
    for _, cellDescription in ipairs(toRemove) do
        merchantCells[cellDescription] = nil
    end
    if doSave then
        SaveCellResetTimers(true)
    end
end

customEventHooks.registerHandler("OnPlayerCellChange", function(eventStatus, pid, playerPacket, previousCellDescription)
    if Players[pid] and Players[pid]:IsLoggedIn() then
        local cellDescription = playerPacket.location.cell
        local cell = LoadedCells[cellDescription]
        if cell then
            -- Единая проверка через isCellEligibleForReset — без дублирования логики.
            -- Z001: в CO-OP новые таймеры не заводятся вовсе, иначе после
            -- переключения пресета мир продолжал бы копить отложенные сбросы.
            if coopAllows("scheduleTimers") and not cellResetTimers[cellDescription]
                and isCellEligibleForReset(cellDescription) then
                local exteriorCell = cell.isExterior
                local getResetTime = exteriorCell and exteriorCellResetTime or interiorCellResetTime
                local rcList = getLasTable("rc_list")
                if rcList and rcList[cellDescription] then
                    getResetTime = tonumber(rcList[cellDescription]) or getResetTime
                end
                local resetAt = os.time() + getResetTime
                cellResetTimers[cellDescription] = resetAt
                -- Добавляем в heap сразу — таймер не нужно ждать
                scheduleCell(cellDescription, resetAt)
                -- Дебаунс: не пишем JSON при каждой смене ячейки каждым игроком
                SaveCellResetTimers()
                -- Перезапускаем таймер с оптимальной задержкой
                -- StartTimer принимает только id таймера: период из второго
                -- аргумента молча игнорировался. Нужен RestartTimer.
                tes3mp.RestartTimer(GlobalCellResetTimer, time.seconds(nextTimerDelay()))
            end
            specificCellFunctionsToAlwaysRun(pid, cellDescription)
        end
    end
end)

customEventHooks.registerHandler("OnPlayerAuthentified", function(eventStatus, pid)
    if runStartupCommandsAutomatically and not startupCommandsHaveRun and Players[pid] and Players[pid]:IsLoggedIn() then
        runStartupCommands(pid)
        startupCommandsHaveRun = true
    end
end)

-- ============================================================================
-- UpdateResetTimers — УМНЫЙ ПЛАНИРОВЩИК (heap-based)
-- Больше не перебирает ВСЕ ячейки каждые 300 сек.
-- Извлекает только те, чьё время наступило (O(log n) per reset).
-- Таймер перезапускается ровно на время до следующего сброса.
-- ============================================================================
periodicCellResets.UpdateResetTimers = function()
    checkMerchantCell()

    -- Сохраняем отложенные записи если пришло время
    if saveScheduled then
        flushSave()
    end

    -- Z001: в CO-OP планировщик остаётся живым только ради восстановления
    -- запасов торговцев. Ни одна ячейка по таймеру не сбрасывается, а уже
    -- накопленные таймеры (например, от прошлого запуска в режиме MMO)
    -- очищаются, чтобы не сработать при обратном переключении пресета.
    if not coopAllows("timerResets") then
        if not tableHelper.isEmpty(cellResetTimers) then
            cellResetTimers = {}
            resetHeap = {}
            SaveCellResetTimers(true)
            tes3mp.LogAppend(enumerations.log.INFO,
                "[ArenaMP Core] CO-OP: очередь автосброса ячеек очищена.")
        end
        tes3mp.RestartTimer(GlobalCellResetTimer, time.seconds(cellResetTimeCheck))
        return
    end

    -- Проверяем наличие онлайн-игроков
    local hasOnlinePlayer = false
    for pid, player in pairs(Players) do
        if player and player:IsLoggedIn() then
            hasOnlinePlayer = true
            break
        end
    end

    if hasOnlinePlayer then
        local markTime = os.time()
        local doSave = false
        local cellsToReset = {}

        -- Извлекаем из heap все ячейки, время которых наступило
        while #resetHeap > 0 do
            local top = resetHeap[1]
            -- Пропускаем устаревшие записи (ячейка уже сброшена или время изменилось)
            local current = cellResetTimers[top.cell]
            if not current or current ~= top.time then
                heapPop()
            elseif top.time <= markTime then
                heapPop()
                if not isCellEligibleForReset(top.cell) then
                    -- Remove stale timers for houses/private instances/exemptions.
                    cellResetTimers[top.cell] = nil
                    doSave = true
                -- Ячейку нельзя сбросить пока в ней игроки. Раньше запись просто
                -- исчезала из heap и могла больше никогда не сброситься. Теперь
                -- занятая ячейка получает новую попытку через cellResetTimeCheck.
                elseif not isCellOccupied(top.cell) then
                    table.insert(cellsToReset, top.cell)
                else
                    local retryTime = markTime + cellResetTimeCheck
                    cellResetTimers[top.cell] = retryTime
                    heapPush(retryTime, top.cell)
                    doSave = true
                end
            else
                break -- следующий сброс ещё в будущем
            end
        end

        for _, cellDescription in ipairs(cellsToReset) do
            local success, err = pcall(function()
                if safeResetUnloadedCell(cellDescription) then
                    doSave = true
                end
            end)
            if not success then
                tes3mp.LogMessage(enumerations.log.ERROR,
                    "[ArenaMP Core] " .. cellDescription .. ": " .. tostring(err))
            end
        end

        if doSave then
            flushSave()
        end
    end

    -- Перезапускаем таймер ровно до следующего события, а не через фиксированные 300 сек
    local delay = nextTimerDelay()
    tes3mp.RestartTimer(GlobalCellResetTimer, time.seconds(delay))
end

GlobalCellResetTimerUpdate = periodicCellResets.UpdateResetTimers
GlobalCellResetTimer = tes3mp.CreateTimer("GlobalCellResetTimerUpdate", time.seconds(cellResetTimeCheck))
tes3mp.StartTimer(GlobalCellResetTimer)

-- ============================================================================
-- ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ВРЕМЕНИ
-- ============================================================================
local determineTime = function(timeInput, returnList)
    local timeString = ""
    local mod
    local timeArray = {0, 0, 0}
    local timeSection = {86400, 3600, 60}
    local timeName, div1, div2, plural, dot
    if not returnList then
        timeName = {" день", " ч.", " мин.", " сек."}
        div1 = ", "
        div2 = " и "
        plural = "ы"
        dot = "."
    else
        timeName = {"д", "ч", "м", "с"}
        div1 = " "
        div2 = " "
        plural = ""
        dot = ""
    end
    for i = 1, 3 do
        mod = timeInput % timeSection[i]
        timeArray[i] = (timeInput - mod) / timeSection[i]
        if timeArray[i] > 0 then
            if i > 1 then
                if timeArray[i-1] > 0 then
                    if mod ~= 0 then
                        timeString = timeString .. div1 .. timeArray[i]
                    else
                        timeString = timeString .. div2 .. timeArray[i]
                    end
                else
                    timeString = timeString .. timeArray[i]
                end
            else
                timeString = timeString .. timeArray[i]
            end
            timeString = timeString .. timeName[i] .. (timeArray[i] > 1 and plural or "")
        end
        timeInput = mod
    end
    if mod ~= 0 then
        if timeString ~= "" then
            timeString = timeString .. div2 .. mod .. timeName[4] .. (mod > 1 and plural or "") .. dot
        else
            timeString = mod .. timeName[4] .. (mod > 1 and plural or "") .. dot
        end
    end
    return timeString
end

periodicCellResets.convertTime = function(timeInput)
    return determineTime(timeInput, true)
end

local getListOfUpcomingResetCells = function()
    local txt = ""
    local list = {}
    if tableHelper.isEmpty(cellResetTimers) then
        return "Нет ячеек с предстоящими сбросами."
    end
    for cellDescription, resetTime in pairs(cellResetTimers) do
        local timeRemainder = resetTime - os.time()
        local timeRemainderText = color.Lime .. "Покиньте ячейку для сброса!"
        if timeRemainder > 0 then
            local timerColor = color.Grey
            if timeRemainder <= 900 then
                timerColor = color.Red
            elseif timeRemainder <= 1700 then
                timerColor = color.Orange
            elseif timeRemainder <= 5400 then
                timerColor = color.Yellow
            elseif timeRemainder <= 9000 then
                timerColor = color.Khaki
            end
            timeRemainderText = timerColor .. periodicCellResets.convertTime(timeRemainder)
        end
        table.insert(list, {name = cellDescription, timer = timeRemainder, timeText = timeRemainderText})
    end
    if viewResetSortTypeCellName then
        table.sort(list, function(a, b) return a.name < b.name end)
    else
        table.sort(list, function(a, b) return a.timer < b.timer end)
    end
    for _, item in ipairs(list) do
        txt = txt .. "\"" .. item.name .. "\"\n" .. color.White .. "  - Сброс через: " .. item.timeText .. "\n"
    end
    return txt:sub(1, -2)
end

local viewResetMenu = function(pid)
    local header = color.DarkOrange .. "Просмотр предстоящих сбросов ячеек\n" .. color.Yellow .. "Следующий список содержит ячейки и время до их сброса."
    tes3mp.ListBox(pid, ViewResetsGuiId, header, getListOfUpcomingResetCells())
end

local showSaveCellMenu = function(pid)
    local cellDescription = tes3mp.GetCell(pid)
    local cellData = getHouseCellData(cellDescription)
    local isHouse = cellData and cellData.house or false
    local hasBackup = hasCellBackup(cellDescription)
    local resetTime = cellResetTimers[cellDescription]
    local timeRemainderText = "Нет таймера сброса"
    if resetTime then
        local timeRemainder = resetTime - os.time()
        timeRemainderText = timeRemainder > 0 and periodicCellResets.convertTime(timeRemainder) or "Готова к сбросу"
    end
    local message = color.DarkOrange .. "Управление бэкапом ячейки: " .. color.White .. cellDescription .. "\n\n" ..
                    color.Yellow .. "Состояние:\n" ..
                    color.White .. "  - Дом: " .. (isHouse and color.Turquoise .. "Да" or color.Red .. "Нет") .. "\n" ..
                    color.White .. "  - Бэкап: " .. (hasBackup and color.Turquoise .. "Есть" or color.Red .. "Нет") .. "\n" ..
                    color.White .. "  - Таймер сброса: " .. color.Yellow .. timeRemainderText .. "\n\n" ..
                    color.Yellow .. "Выберите действие:"
    local buttons = "Сохранить;Очистить;Выход"
    tes3mp.CustomMessageBox(pid, SaveCellGuiId, message, buttons)
end

customEventHooks.registerHandler("OnGUIAction", function(eventStatus, pid, idGui, data)
    local isValid = eventStatus.validDefaultHandler
    if isValid ~= false then
        if idGui == ViewResetsGuiId then
            isValid = false
        elseif idGui == SaveCellGuiId then
            local cellDescription = tes3mp.GetCell(pid)
            local action = tonumber(data)
            if action == 0 then
                if saveCellBackup(cellDescription) then
                    tes3mp.SendMessage(pid, color.Yellow .. "[Бэкап Ячейки]: " .. color.Turquoise .. "Бэкап для " .. cellDescription .. " успешно сохранён.\n")
                else
                    tes3mp.SendMessage(pid, color.Yellow .. "[Бэкап Ячейки]: " .. color.Error .. "Не удалось сохранить бэкап для " .. cellDescription .. ".\n")
                end
            elseif action == 1 then
                if clearCellBackup(cellDescription) then
                    tes3mp.SendMessage(pid, color.Yellow .. "[Бэкап Ячейки]: " .. color.Turquoise .. "Бэкап для " .. cellDescription .. " успешно удалён.\n")
                else
                    tes3mp.SendMessage(pid, color.Yellow .. "[Бэкап Ячейки]: " .. color.Error .. "Бэкап для " .. cellDescription .. " не найден.\n")
                end
            end
            isValid = false
        end
    end
    eventStatus.validDefaultHandler = isValid
    return eventStatus
end)

-- ===========================================================================================
-- ФУНКЦИЯ ЭВАКУАЦИИ ИГРОКОВ (БЕЗОПАСНАЯ)
-- ===========================================================================================
local function evacuatePlayersFromCell(cellDescription)
    local evacuatedPlayers = {}

    if not LoadedCells[cellDescription] then
        return evacuatedPlayers
    end

    local visitorsCopy = {}
    if LoadedCells[cellDescription].visitors then
        for _, pid in ipairs(LoadedCells[cellDescription].visitors) do
            table.insert(visitorsCopy, pid)
        end
    end

    for _, pid in ipairs(visitorsCopy) do
        if Players[pid] and Players[pid]:IsLoggedIn() then
            local playerName = Players[pid].name
            local playerCell = tes3mp.GetCell(pid)

            if playerCell == cellDescription then
                tes3mp.LogAppend(enumerations.log.INFO, "Эвакуация игрока " .. playerName .. " (" .. pid .. ") из ячейки " .. cellDescription)

                table.insert(evacuatedPlayers, {
                    pid = pid,
                    name = playerName,
                    originalCell = cellDescription
                })

                -- Сообщаем игроку
                Players[pid]:Message(color.Yellow .. "Ячейка " .. cellDescription .. " обновляется. Пожалуйста, подождите...\n")

                -- Телепортируем БЕЗОПАСНО — выставляем ячейку и позицию
                tes3mp.SetCell(pid, "-2, -9")
                tes3mp.SendCell(pid)
            end
        end
    end

    tes3mp.LogAppend(enumerations.log.INFO, "Эвакуировано игроков: " .. #evacuatedPlayers)
    return evacuatedPlayers
end

-- ===========================================================================================
-- ФУНКЦИЯ ВОЗВРАТА ИГРОКОВ
-- ===========================================================================================
local function returnPlayersToCell(evacuatedPlayers, cellDescription)
    if #evacuatedPlayers == 0 then
        return
    end

    for _, playerData in ipairs(evacuatedPlayers) do
        local pid = playerData.pid
        if Players[pid] and Players[pid]:IsLoggedIn() then
            tes3mp.LogAppend(enumerations.log.INFO, "Возврат игрока " .. playerData.name .. " (" .. pid .. ") обратно в " .. cellDescription)
            tes3mp.SetCell(pid, cellDescription)
            tes3mp.SendCell(pid)
        end
    end
end

-- ===========================================================================================
-- МЯГКИЙ СБРОС (ОБНОВЛЕНИЕ МОБОВ) — ИСПРАВЛЕНО
-- ===========================================================================================
periodicCellResets.softResetCell = function(cellDescription)
    if isProtectedPrivateInstance(cellDescription) then
        return false, "Личный квестовый инстанс не сбрасывается."
    end

    local cellData = getHouseCellData(cellDescription)
    if cellData and cellData.house then
        return false, "Эта ячейка принадлежит дому и не может быть сброшена."
    end

    local evacuatedPlayers = evacuatePlayersFromCell(cellDescription)

    local unloadAtEnd = false
    if not LoadedCells[cellDescription] then
        logicHandler.LoadCell(cellDescription)
        unloadAtEnd = true
    end

    if not LoadedCells[cellDescription] then
        return false, "Не удалось загрузить ячейку."
    end

    if LoadedCells[cellDescription].isResetting then
        returnPlayersToCell(evacuatedPlayers, cellDescription)
        return false, "Ячейка уже в процессе сброса."
    end

    LoadedCells[cellDescription].isResetting = true

    -- Удаляем мобов из данных ячейки
    local actorUniqueIndexes = {}
    for uniqueIndex, objectData in pairs(LoadedCells[cellDescription].data.objectData) do
        if objectData.refId then
            if tableHelper.containsValue(LoadedCells[cellDescription].data.packets.actorList or {}, uniqueIndex) then
                table.insert(actorUniqueIndexes, uniqueIndex)
            end
        end
    end

    tes3mp.LogAppend(enumerations.log.INFO, "Удаление " .. #actorUniqueIndexes .. " мобов из ячейки " .. cellDescription)

    if #actorUniqueIndexes > 0 then
        -- Удаляем из данных ячейки
        for _, uniqueIndex in ipairs(actorUniqueIndexes) do
            LoadedCells[cellDescription].data.objectData[uniqueIndex] = nil
        end

        -- Отправляем удаление только игрокам с загруженной ячейкой
        safeSendObjectDelete(cellDescription)
    end

    -- Очищаем пакеты связанные с actors
    if LoadedCells[cellDescription].data.packets then
        LoadedCells[cellDescription].data.packets.actorList = {}
        LoadedCells[cellDescription].data.packets.death = {}
        LoadedCells[cellDescription].data.packets.position = {}
        LoadedCells[cellDescription].data.packets.statsDynamic = {}
        LoadedCells[cellDescription].data.packets.equipment = {}
        LoadedCells[cellDescription].data.packets.ai = {}
        LoadedCells[cellDescription].data.packets.spellsActive = {}
    end

    LoadedCells[cellDescription].unusableContainerUniqueIndexes = {}
    LoadedCells[cellDescription].data.lastVisit = {}
    LoadedCells[cellDescription].isRequestingContainerData = false
    LoadedCells[cellDescription].containerRequestTime = nil
    LoadedCells[cellDescription].containerRequestPid = nil
    LoadedCells[cellDescription].data.loadState.hasFullActorList = false

    LoadedCells[cellDescription]:Save()
    LoadedCells[cellDescription].isResetting = false

    safeSendCellReset(cellDescription)

    returnPlayersToCell(evacuatedPlayers, cellDescription)

    if unloadAtEnd then
        logicHandler.UnloadCell(cellDescription)
    end

    tes3mp.LogAppend(enumerations.log.INFO, "Мягкий сброс ячейки " .. cellDescription .. " завершён успешно.")
    return true, "Ячейка успешно обновлена."
end

-- ===========================================================================================
-- ПОЛНЫЙ СБРОС ЯЧЕЙКИ — ИСПРАВЛЕНО
-- ===========================================================================================
-- Z005: forceHouse снимает защиту «это чей-то дом» — но только для вызовов
-- изнутри системы недвижимости.
--
-- Защита нужна, чтобы администратор случайной командой /clear не стёр чужой
-- дом. Но она же запрещала и единственный законный сброс жилой ячейки:
-- houseHelper вызывает fullResetCell для ЛИЧНОЙ КОПИИ при продаже, изъятии и
-- команде «очистить дом», а копия по определению принадлежит дому. В итоге
-- очистка молча возвращала false, дом продавался вместе со всем содержимым, и
-- следующий владелец получал чужие вещи.
--
-- Ручные команды по-прежнему вызывают функцию без этого аргумента и остаются
-- под защитой.
periodicCellResets.fullResetCell = function(cellDescription, forceHouse)
    if isProtectedPrivateInstance(cellDescription) then
        return false, "Личный квестовый инстанс не сбрасывается."
    end

    local cellData = getHouseCellData(cellDescription)
    if cellData and cellData.house and forceHouse ~= true then
        return false, "Эта ячейка принадлежит дому и не может быть сброшена."
    end

    local evacuatedPlayers = evacuatePlayersFromCell(cellDescription)

    if forceHouse ~= true and hasCellBackup(cellDescription) then
        if restoreCellFromBackup(cellDescription) then
            returnPlayersToCell(evacuatedPlayers, cellDescription)
            cellResetTimers[cellDescription] = nil
            SaveCellResetTimers(true)
            return true, "Ячейка восстановлена из бэкапа."
        end
    end

    local unloadAtEnd = false
    if not LoadedCells[cellDescription] then
        logicHandler.LoadCell(cellDescription)
        unloadAtEnd = true
    end

    if not LoadedCells[cellDescription] then
        return false, "Не удалось загрузить ячейку."
    end

    if LoadedCells[cellDescription].isResetting then
        returnPlayersToCell(evacuatedPlayers, cellDescription)
        return false, "Ячейка уже в процессе сброса."
    end

    if not safeClearCellData(cellDescription) then
        returnPlayersToCell(evacuatedPlayers, cellDescription)
        return false, "Не удалось очистить данные ячейки."
    end

    LoadedCells[cellDescription]:Save()

    safeSendObjectDelete(cellDescription)
    safeSendCellReset(cellDescription, forceHouse == true)

    removeCustomRecordsFromResetCell(cellDescription)

    if forceHouse ~= true then returnPlayersToCell(evacuatedPlayers, cellDescription) end

    if unloadAtEnd or forceHouse == true then
        logicHandler.UnloadCell(cellDescription)
    end

    if cellResetTimers[cellDescription] then
        cellResetTimers[cellDescription] = nil
        SaveCellResetTimers(true)
    end

    tes3mp.LogAppend(enumerations.log.INFO, "Полный сброс ячейки " .. cellDescription .. " завершён успешно.")
    return true, "Ячейка полностью очищена и сброшена."
end

-- ===========================================================================================
-- КОМАНДЫ
-- ===========================================================================================
periodicCellResets.cleanCurrentCell = function(pid, cmd)
    if Players[pid].data.settings.staffRank < requiredStaffRank then
        tes3mp.SendMessage(pid, color.Error .. "У вас нет доступа к этой команде.\n")
        return
    end

    local cellDescription = tes3mp.GetCell(pid)
    tes3mp.SendMessage(pid, color.Yellow .. "[Обновление Ячейки]: " .. color.White .. "Начинается мягкое обновление " .. cellDescription .. "...\n")

    local success, message = periodicCellResets.softResetCell(cellDescription)

    if success then
        tes3mp.SendMessage(pid, color.Yellow .. "[Обновление Ячейки]: " .. color.Green .. message .. "\n")
        tes3mp.LogAppend(enumerations.log.INFO, "[ArenaMP Core] Игрок " .. Players[pid].name .. " выполнил мягкий сброс ячейки: " .. cellDescription)
    else
        tes3mp.SendMessage(pid, color.Yellow .. "[Обновление Ячейки]: " .. color.Error .. message .. "\n")
    end
end

periodicCellResets.clearCurrentCell = function(pid, cmd)
    if Players[pid].data.settings.staffRank < requiredStaffRank then
        tes3mp.SendMessage(pid, color.Error .. "У вас нет доступа к этой команде.\n")
        return
    end

    local cellDescription = tes3mp.GetCell(pid)
    tes3mp.SendMessage(pid, color.Yellow .. "[Очистка Ячейки]: " .. color.White .. "Начинается полная очистка " .. cellDescription .. "...\n")

    local success, message = periodicCellResets.fullResetCell(cellDescription)

    if success then
        tes3mp.SendMessage(pid, color.Yellow .. "[Очистка Ячейки]: " .. color.Green .. message .. "\n")
        tes3mp.LogAppend(enumerations.log.INFO, "[ArenaMP Core] Игрок " .. Players[pid].name .. " выполнил полный сброс ячейки: " .. cellDescription)
    else
        tes3mp.SendMessage(pid, color.Yellow .. "[Очистка Ячейки]: " .. color.Error .. message .. "\n")
    end
end

periodicCellResets.resetCurrentCell = function(pid, cmd)
    if Players[pid].data.settings.staffRank < requiredStaffRank then
        tes3mp.SendMessage(pid, color.Error .. "У вас нет доступа к этой команде.\n")
        return
    end
    periodicCellResets.clearCurrentCell(pid, cmd)
end

periodicCellResets.viewResets = function(pid, cmd)
    if Players[pid] and Players[pid]:IsLoggedIn() and Players[pid].data.settings.staffRank >= viewResetsStaffRank then
        viewResetMenu(pid)
    else
        tes3mp.SendMessage(pid, color.Error .. "У вас нет доступа к этой команде.\n")
    end
end
customCommandHooks.registerCommand("resets", periodicCellResets.viewResets)

periodicCellResets.saveCell = function(pid, cmd)
    if Players[pid] and Players[pid]:IsLoggedIn() and Players[pid].data.settings.staffRank >= requiredStaffRank then
        showSaveCellMenu(pid)
    else
        tes3mp.SendMessage(pid, color.Error .. "У вас нет доступа к этой команде.\n")
    end
end

customCommandHooks.registerCommand("savecell", periodicCellResets.saveCell)
customCommandHooks.registerCommand("clean", periodicCellResets.cleanCurrentCell)
customCommandHooks.registerCommand("clear", periodicCellResets.clearCurrentCell)

-- ============================================================================
-- ГЛОБАЛЬНЫЙ КОЛБЭК ТАЙМЕРА ДЛЯ ОЧИСТКИ ПУСТЫХ ДОМОВ
-- ============================================================================
function PeriodicUnownedHouseCleanup()
    periodicCellResets.cleanUnownedHouses()
end

-- Команда администратора для ручной очистки пустых домов
local function cmdCleanUnowned(pid, cmd)
    if not Players[pid] or Players[pid].data.settings.staffRank < requiredStaffRank then
        tes3mp.SendMessage(pid, color.Error .. "У вас нет доступа к этой команде.\n")
        return
    end
    tes3mp.SendMessage(pid, color.Yellow .. "[Очистка Домов]: " .. color.White .. "Запуск очистки пустующих домов...\n")
    local cleaned, skipped = periodicCellResets.cleanUnownedHouses()
    local msg = color.Yellow .. "[Очистка Домов]: "
    if cleaned > 0 then
        msg = msg .. color.Green .. "Очищено " .. cleaned .. " пустых ячеек."
    else
        msg = msg .. color.White .. "Нет пустых домов для очистки."
    end
    if skipped > 0 then
        msg = msg .. color.Error .. " Пропущено " .. skipped .. " (игроки внутри)."
    end
    tes3mp.SendMessage(pid, msg .. "\n")
end
customCommandHooks.registerCommand("cleanuphomes", cmdCleanUnowned)
customCommandHooks.registerCommand("cleanunowned", cmdCleanUnowned)

-- Регистрируем OnServerPostInit в конце файла, после объявления всех local функций
customEventHooks.registerHandler("OnServerPostInit", function(eventStatus)
    if not houseHelper then
        tes3mp.LogAppend(enumerations.log.WARN,
            "[ArenaMP Core] houseHelper.lua не найден. Сервер продолжает работу; дополнительные housing-проверки отключены.")
    end

    if not getLas() then
        tes3mp.LogAppend(enumerations.log.WARN,
            "[ArenaMP Core] las не найден. Reset Controller интеграция отключена; базовые hourly reset работают.")
    else
        callLas("LoadRCbl")
        callLas("LoadRC")
    end

    LoadCellResetTimers()
    if cellResetTimersNeedsInitialSave or tableHelper.isEmpty(cellResetTimers) then
        SaveCellResetTimers(true)
        if cellResetTimersNeedsInitialSave then
            tes3mp.LogAppend(enumerations.log.INFO,
                "[ArenaMP Core] Создан server/data/custom/cellResetTimers.json.")
            cellResetTimersNeedsInitialSave = false
        end
    end
    if coopAllows("startupResets") then
        resetCellsOnStartup()
    else
        tes3mp.LogAppend(enumerations.log.INFO,
            "[ArenaMP Core] CO-OP: массовый сброс ячеек при старте пропущен.")
    end
    removeDeletedCellsFromResetTimers()
    -- Очистка домов без хозяина при старте отключена.
    -- Для ручного запуска используйте /cleanuphomes или /cleanunowned
end)

return periodicCellResets