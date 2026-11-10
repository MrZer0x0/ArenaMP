-- ===========================================================================
-- ArenaMP Z002 — mwScriptHelper: подмена игровых MWScript
-- ===========================================================================
--
-- Происхождение
-- -------------
-- Это встроенная и переработанная версия custom/Nirn_mwScript.lua из архивной
-- сборки. Исходная задача сохранена полностью: положить файл со скриптом в
-- server/data/custom/MWScripts/, и его текст заменит одноимённый скрипт
-- Morrowind у всех клиентов. Штатного способа чинить сломанные скрипты
-- плагинов без правки самого плагина попросту нет, так что механизм нужный.
--
-- Что пришлось изменить и почему
-- ------------------------------
-- 1. Оригинал читал каталог через io.popen('dir ... /b') — это команда cmd.exe.
--    На Linux и Android, которые собирает этот же проект, обработчик падал на
--    первой же строке и не загружал ничего. Теперь листинг берётся командой,
--    соответствующей системе, а если io.popen недоступен (его отключают в
--    некоторых сборках), работает манифест: список имён, который сервер сам
--    пишет рядом со скриптами при каждом удачном листинге. Один запуск на
--    Windows — и та же папка грузится на Android без всякого popen.
-- 2. Оригинал объявлял file_exists и read_file_content глобальными: любой
--    другой скрипт мог их незаметно переопределить. Теперь всё локальное.
-- 3. Чтение шло через io. На Windows это тот же узкий ANSI-API, из-за которого
--    не удалялись профили с кириллицей: файл MWScripts с русским именем просто
--    не открывался. Теперь используется та же io2-библиотека, через которую
--    CoreScripts читает и пишет JSON.
-- 4. Оригинал работал только на старте сервера. Подмена скрипта — занятие
--    итеративное, и перезапуск сервера ради каждой правки съедает больше
--    времени, чем сама правка. Добавлена горячая перезагрузка.
-- 5. print() заменён на серверный лог с меткой [ArenaMP MWScript].
--
-- Чего этот механизм не может
-- ---------------------------
-- Запись SCRIPT отправляется клиенту при входе. Перезагрузка рассылает её и
-- тем, кто уже в игре, но скрипт, который УЖЕ выполняется на объекте в
-- загруженной ячейке, продолжит работать по старому тексту до перезахода в
-- ячейку. То же и с отключением подмены: у клиента нельзя отобрать уже
-- полученную запись, отключение действует со следующего входа. Интерфейс
-- говорит об этом прямо, чтобы администратор не гадал.
-- ===========================================================================

require("config")

local jsonInterface = require("jsonInterface")

local mwScriptHelper = {}

local LOG_TAG = "[ArenaMP MWScript] "
local STATE_PATH = "custom/mwScriptOverrides.json"

local function log(level, message)
    tes3mp.LogAppend(level, LOG_TAG .. tostring(message))
end

-- ===========================================================================
-- КОНФИГУРАЦИЯ
-- ===========================================================================
local defaults = {
    enabled = true,

    -- Каталог относительно server/data.
    directory = "custom/MWScripts",

    -- Какие расширения считать скриптами. По умолчанию только .es3 — ровно как
    -- в оригинале, чтобы включение модуля не подхватило лишнего из старых
    -- папок. Добавьте ".mws", если держите скрипты в этом формате.
    extensions = { ".es3" },

    -- Явный список файлов. Если задан, листинг каталога не выполняется вовсе —
    -- полезно на хостингах, где io.popen и os.execute запрещены.
    files = nil,

    -- Ранг персонала для команд и вкладки администратора.
    adminRank = 2,

    -- Предупреждать в логе, если имя файла не совпадает с именем в строке
    -- "Begin <name>" внутри скрипта. Почти всегда это значит, что подмена
    -- нацелена не на тот скрипт и молча ничего не делает.
    warnOnBeginMismatch = true
}

local function cfg()
    local user = type(config.mwScriptOverrides) == "table" and config.mwScriptOverrides or {}
    local merged = {}
    for key, value in pairs(defaults) do
        if user[key] ~= nil then merged[key] = user[key] else merged[key] = value end
    end
    return merged
end

-- ===========================================================================
-- СОСТОЯНИЕ
-- ---------------------------------------------------------------------------
-- manifest — список имён файлов, увиденных при последнем удачном листинге.
--            Он же запасной источник, когда листинг недоступен.
-- disabled — подмены, выключенные администратором; переживают перезапуск.
-- ===========================================================================
local state = nil

local function loadState()
    if state ~= nil then return state end
    local ok, loaded = pcall(jsonInterface.load, STATE_PATH)
    if ok and type(loaded) == "table" then
        loaded.manifest = type(loaded.manifest) == "table" and loaded.manifest or {}
        loaded.disabled = type(loaded.disabled) == "table" and loaded.disabled or {}
        state = loaded
    else
        state = { manifest = {}, disabled = {} }
        pcall(jsonInterface.save, STATE_PATH, state)
    end
    return state
end

local function saveState()
    if state == nil then return end
    pcall(jsonInterface.save, STATE_PATH, state)
end

-- Последний результат загрузки: имя записи -> сведения о файле.
local loaded = {}

-- ===========================================================================
-- ФАЙЛОВЫЕ ОПЕРАЦИИ
-- ===========================================================================
local function ioLibrary()
    -- jsonInterface уже выбрал io2 на Windows и io в остальных случаях.
    if type(jsonInterface.ioLibrary) == "table" then
        return jsonInterface.ioLibrary
    end
    return io
end

local function directoryPath()
    local dir = tostring(cfg().directory or "custom/MWScripts")
    dir = dir:gsub("^[/\\]+", ""):gsub("[/\\]+$", "")
    return config.dataPath .. "/" .. dir
end

local function readFile(path)
    local lib = ioLibrary()
    if type(lib.open) ~= "function" then return nil end
    local ok, handle = pcall(lib.open, path, "rb")
    if not ok or handle == nil then return nil end
    local okRead, contents = pcall(function() return handle:read("*a") end)
    pcall(function() handle:close() end)
    if not okRead or type(contents) ~= "string" then return nil end
    return contents
end

local function fileExists(path)
    if type(tes3mp.DoesFileExist) == "function" then
        local ok, exists = pcall(tes3mp.DoesFileExist, path)
        if ok then return exists == true end
    end
    local lib = ioLibrary()
    local ok, handle = pcall(lib.open, path, "rb")
    if ok and handle ~= nil then
        pcall(function() handle:close() end)
        return true
    end
    return false
end

local function hasWantedExtension(fileName)
    for _, extension in ipairs(cfg().extensions or {".es3"}) do
        local lowerName = string.lower(fileName)
        local lowerExt = string.lower(tostring(extension))
        if #lowerName > #lowerExt and string.sub(lowerName, -#lowerExt) == lowerExt then
            return true
        end
    end
    return false
end

-- Листинг каталога. Возвращает массив имён и признак того, удался ли настоящий
-- листинг (в отличие от использования манифеста).
local function listDirectory()
    local settings = cfg()

    -- 1. Явный список из конфига — самый предсказуемый источник.
    if type(settings.files) == "table" and #settings.files > 0 then
        return tableHelper.shallowCopy(settings.files), false
    end

    local path = directoryPath()
    local names = {}

    -- 2. Настоящий листинг. io.popen отсутствует или запрещён в части сборок,
    -- поэтому вызов целиком под pcall, а не только чтение.
    if type(io.popen) == "function" then
        local command
        if tes3mp.GetOperatingSystemType() == "Windows" then
            command = 'dir "' .. path .. '" /b /a-d'
        else
            command = 'ls -1p "' .. path .. '" 2>/dev/null'
        end

        local ok = pcall(function()
            local pipe = io.popen(command)
            if pipe == nil then return end
            for line in pipe:lines() do
                -- ls -1p помечает каталоги завершающим слэшем; отбрасываем их,
                -- чтобы подпапка вроде old/ не попала в список файлов.
                if line ~= "" and string.sub(line, -1) ~= "/" then
                    table.insert(names, (line:gsub("[\r\n]", "")))
                end
            end
            pipe:close()
        end)

        if ok and #names > 0 then
            return names, true
        end
        names = {}
    end

    -- 3. Манифест прошлого удачного листинга.
    local stored = loadState().manifest
    if type(stored) == "table" and #stored > 0 then
        log(enumerations.log.INFO,
            "листинг каталога недоступен; используется манифест (" .. #stored .. " файл(ов))")
        return tableHelper.shallowCopy(stored), false
    end

    return {}, false
end

-- ===========================================================================
-- РАЗБОР СКРИПТА
-- ===========================================================================

-- Имя из строки "Begin <name>". Именно его компилирует движок, тогда как id
-- записи — это имя подменяемого скрипта. Несовпадение почти всегда означает
-- опечатку в имени файла и подмену, которая ничего не делает.
local function parseBeginName(text)
    if type(text) ~= "string" then return nil end
    local name = text:match("^%s*[Bb]egin%s+([%w_%-%.]+)")
    if name ~= nil then return name end
    return text:match("[\r\n]%s*[Bb]egin%s+([%w_%-%.]+)")
end

local function stripExtension(fileName)
    return (fileName:gsub("%.[^%.]+$", ""))
end

-- ===========================================================================
-- ЗАГРУЗКА
-- ===========================================================================

-- Возвращает: added, skipped, disabledCount, listedFromDirectory
function mwScriptHelper.Reload()
    local settings = cfg()
    if not settings.enabled then
        log(enumerations.log.INFO, "модуль отключён в config.mwScriptOverrides")
        return 0, 0, 0, false
    end

    local scriptStore = RecordStores and RecordStores["script"]
    if scriptStore == nil then
        log(enumerations.log.ERROR, "RecordStores[\"script\"] недоступен; подмена скриптов пропущена")
        return 0, 0, 0, false
    end

    local path = directoryPath()
    local names, fromDirectory = listDirectory()
    local data = loadState()

    local added, skipped, disabledCount = 0, 0, 0
    local seen = {}
    loaded = {}

    for _, fileName in ipairs(names) do
        if hasWantedExtension(fileName) then
            local fullPath = path .. "/" .. fileName
            local scriptName = stripExtension(fileName)

            if not fileExists(fullPath) then
                -- Попадает сюда только манифест, отставший от каталога.
                log(enumerations.log.WARN, "файл из манифеста не найден: " .. fileName)
                skipped = skipped + 1
            else
                local contents = readFile(fullPath)
                if contents == nil or contents == "" then
                    log(enumerations.log.WARN, "не удалось прочитать " .. fileName)
                    skipped = skipped + 1
                else
                    table.insert(seen, fileName)

                    local beginName = parseBeginName(contents)
                    if settings.warnOnBeginMismatch and beginName ~= nil
                        and string.lower(beginName) ~= string.lower(scriptName) then
                        log(enumerations.log.WARN, "имя файла и Begin не совпадают: " ..
                            fileName .. " содержит \"Begin " .. beginName ..
                            "\". Подменён будет скрипт \"" .. scriptName ..
                            "\", а не \"" .. beginName .. "\".")
                    end

                    if data.disabled[scriptName] == true then
                        -- Выключенная подмена не попадает в записи вовсе, но
                        -- остаётся видимой в списке.
                        scriptStore.data.permanentRecords[scriptName] = nil
                        disabledCount = disabledCount + 1
                        loaded[scriptName] = { file = fileName, bytes = #contents,
                            beginName = beginName, disabled = true }
                    else
                        scriptStore.data.permanentRecords[scriptName] = { scriptText = contents }
                        added = added + 1
                        loaded[scriptName] = { file = fileName, bytes = #contents,
                            beginName = beginName, disabled = false }
                    end
                end
            end
        end
    end

    if added > 0 or disabledCount > 0 then
        scriptStore:Save()
    end

    -- Манифест обновляется только по итогам настоящего листинга: иначе
    -- неудачная попытка на Android затёрла бы список, собранный на Windows.
    --
    -- И даже тогда он не заменяется, а дополняется. Листинг может оказаться
    -- неполным (права на часть файлов, экзотические имена, обрезанный вывод
    -- команды), и простая замена молча потеряла бы подмену, которая до этого
    -- работала. Имя выбывает из манифеста только если файла действительно нет
    -- на диске — так список сам вычищается от удалённых скриптов и при этом
    -- переживает частичный листинг.
    if fromDirectory then
        local merged = {}
        for _, fileName in ipairs(seen) do
            if not tableHelper.containsValue(merged, fileName) then
                table.insert(merged, fileName)
            end
        end
        for _, fileName in ipairs(data.manifest or {}) do
            if not tableHelper.containsValue(merged, fileName)
                and fileExists(path .. "/" .. fileName) then
                table.insert(merged, fileName)
                log(enumerations.log.WARN, "листинг не показал существующий файл " ..
                    fileName .. "; он сохранён в манифесте")
            end
        end
        table.sort(merged)
        data.manifest = merged
        saveState()
    end

    log(enumerations.log.INFO, "подмена скриптов: загружено " .. added ..
        ", отключено " .. disabledCount .. ", пропущено " .. skipped ..
        (fromDirectory and " (листинг каталога)" or " (список из конфига/манифеста)"))

    return added, skipped, disabledCount, fromDirectory
end

-- Разослать записи подменённых скриптов уже подключённым игрокам.
-- Новые игроки получают их сами при входе, вместе с остальными постоянными
-- записями.
function mwScriptHelper.SendToOnlinePlayers()
    local scriptStore = RecordStores and RecordStores["script"]
    if scriptStore == nil then return 0 end

    local idArray = {}
    for scriptName, info in pairs(loaded) do
        if not info.disabled and scriptStore.data.permanentRecords[scriptName] ~= nil then
            table.insert(idArray, scriptName)
        end
    end
    if #idArray == 0 then return 0 end

    local recipients = 0
    for pid, player in pairs(Players) do
        if player ~= nil and player:IsLoggedIn() then
            scriptStore:LoadRecords(pid, scriptStore.data.permanentRecords, idArray)
            recipients = recipients + 1
        end
    end
    return recipients
end

-- ===========================================================================
-- ЗАПРОСЫ
-- ===========================================================================
function mwScriptHelper.List()
    local list = {}
    for scriptName, info in pairs(loaded) do
        table.insert(list, {
            name = scriptName,
            file = info.file,
            bytes = info.bytes,
            beginName = info.beginName,
            disabled = info.disabled == true
        })
    end
    table.sort(list, function(a, b) return a.name < b.name end)
    return list
end

function mwScriptHelper.Get(scriptName)
    return loaded[scriptName]
end

function mwScriptHelper.Preview(scriptName, maxLines)
    local info = loaded[scriptName]
    if info == nil then return nil end
    local contents = readFile(directoryPath() .. "/" .. info.file)
    if contents == nil then return nil end

    maxLines = tonumber(maxLines) or 12

    -- gmatch("[^\r\n]*") also yields the empty string between every pair of
    -- separators, which doubled every line break in the preview. Walk the
    -- newlines explicitly instead.
    contents = contents:gsub("\r\n", "\n"):gsub("\r", "\n")
    local lines, count = {}, 0
    for line in (contents .. "\n"):gmatch("(.-)\n") do
        if count >= maxLines then
            table.insert(lines, "...")
            break
        end
        table.insert(lines, line)
        count = count + 1
    end
    return table.concat(lines, "\n")
end

function mwScriptHelper.SetDisabled(scriptName, disabled)
    if loaded[scriptName] == nil then return false end
    local data = loadState()
    if disabled then
        data.disabled[scriptName] = true
    else
        data.disabled[scriptName] = nil
    end
    saveState()

    -- Перезагрузка приводит записи в соответствие новому состоянию.
    mwScriptHelper.Reload()
    return true
end

function mwScriptHelper.IsDisabled(scriptName)
    return loadState().disabled[scriptName] == true
end

function mwScriptHelper.DirectoryPath()
    return directoryPath()
end

-- ===========================================================================
-- КОМАНДЫ
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

local function hasAccess(pid)
    if Players[pid] == nil or not Players[pid]:IsLoggedIn() then return false end
    if Players[pid]:IsServerOwner() then return true end
    local rank = tonumber(Players[pid].data.settings and Players[pid].data.settings.staffRank) or 0
    return rank >= (tonumber(cfg().adminRank) or 2)
end

mwScriptHelper.HasAccess = hasAccess

local function commandMwScript(pid, cmd)
    if not hasAccess(pid) then
        say(pid, tr(pid, "Недостаточно прав.", "Insufficient permissions."))
        return
    end

    local sub = string.lower(tostring(cmd[2] or ""))

    if sub == "" or sub == "list" then
        local list = mwScriptHelper.List()
        if #list == 0 then
            say(pid, tr(pid, "Подменённых скриптов нет. Каталог: ", "No script overrides. Directory: ") ..
                directoryPath())
            return
        end
        local lines = { tr(pid, "ПОДМЕНА СКРИПТОВ", "SCRIPT OVERRIDES") }
        for _, entry in ipairs(list) do
            table.insert(lines, string.format(" %s %s (%d %s)",
                entry.disabled and "[off]" or "[on] ", entry.name, entry.bytes,
                tr(pid, "б", "b")))
        end
        say(pid, table.concat(lines, "\n"))
        return
    end

    if sub == "reload" then
        local added, skipped, disabledCount = mwScriptHelper.Reload()
        local recipients = mwScriptHelper.SendToOnlinePlayers()
        say(pid, tr(pid, "Перезагружено: ", "Reloaded: ") .. added ..
            tr(pid, ", отключено: ", ", disabled: ") .. disabledCount ..
            tr(pid, ", пропущено: ", ", skipped: ") .. skipped ..
            tr(pid, ". Разослано игрокам: ", ". Sent to players: ") .. recipients .. ".")
        if recipients > 0 then
            say(pid, tr(pid,
                "Уже работающие экземпляры скриптов подхватят новый текст после перезахода в ячейку.",
                "Script instances already running pick up the new text after a cell reload."))
        end
        return
    end

    if sub == "show" then
        local name = tableHelper.concatenateFromIndex(cmd, 3)
        local preview = mwScriptHelper.Preview(name, 14)
        if preview == nil then
            say(pid, tr(pid, "Скрипт не найден: ", "Script not found: ") .. tostring(name))
            return
        end
        tes3mp.CustomMessageBox(pid, config.customMenuIds.adminScriptDetail or 9080,
            name .. "\n\n" .. preview, "Ok")
        return
    end

    if sub == "off" or sub == "on" then
        local name = tableHelper.concatenateFromIndex(cmd, 3)
        if not mwScriptHelper.SetDisabled(name, sub == "off") then
            say(pid, tr(pid, "Скрипт не найден: ", "Script not found: ") .. tostring(name))
            return
        end
        if sub == "off" then
            say(pid, tr(pid, "Подмена отключена: ", "Override disabled: ") .. name ..
                tr(pid, ". У игроков, уже получивших запись, она останется до перезахода на сервер.",
                    ". Players who already received the record keep it until they reconnect."))
        else
            say(pid, tr(pid, "Подмена включена: ", "Override enabled: ") .. name)
            mwScriptHelper.SendToOnlinePlayers()
        end
        return
    end

    say(pid, tr(pid,
        "Использование: /mwscript [list|reload|show <имя>|off <имя>|on <имя>]",
        "Usage: /mwscript [list|reload|show <name>|off <name>|on <name>]"))
end

customCommandHooks.registerCommand("mwscript", commandMwScript)
customCommandHooks.registerCommand("mws", commandMwScript)
customCommandHooks.setRankRequirement("mwscript", defaults.adminRank)
customCommandHooks.setRankRequirement("mws", defaults.adminRank)

-- ===========================================================================
-- СТАРТ
-- ===========================================================================
--
-- Загрузка идёт на OnServerPostInit — к этому моменту RecordStores уже создан
-- serverCore, как и в оригинальном скрипте.
customEventHooks.registerHandler("OnServerPostInit", function(eventStatus)
    local ok, err = pcall(mwScriptHelper.Reload)
    if not ok then
        log(enumerations.log.ERROR, "загрузка подмен не удалась: " .. tostring(err))
    end
end)

return mwScriptHelper
