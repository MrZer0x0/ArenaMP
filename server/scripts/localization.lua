local localization = {}

local dictionaries = {}
local native = {}
local wrappersInstalled = false

local function normalizeLanguage(language)
    if type(language) ~= "string" then
        return "EN"
    end

    language = string.upper(language)
    if language == "RU" or language == "RU-RU" or language == "RUSSIAN" then
        return "RU"
    end
    return "EN"
end

local function getConfiguredServerLanguage()
    if config == nil or type(config.serverLanguage) ~= "string" then
        return "AUTO"
    end

    local language = string.upper(config.serverLanguage)
    if language == "RU" or language == "EN" then
        return language
    end
    return "AUTO"
end

local function getClientLanguage(pid)
    if type(pid) == "number" and Players ~= nil and Players[pid] ~= nil and Players[pid].language ~= nil then
        return normalizeLanguage(Players[pid].language)
    end

    if type(pid) == "number" and tes3mp.GetLanguage ~= nil then
        local ok, language = pcall(tes3mp.GetLanguage, pid)
        if ok then
            return normalizeLanguage(language)
        end
    end

    return normalizeLanguage(config ~= nil and config.defaultLanguage or "EN")
end

local function getEffectiveLanguage(pid)
    local serverLanguage = getConfiguredServerLanguage()
    if serverLanguage ~= "AUTO" then
        return serverLanguage
    end
    return getClientLanguage(pid)
end

local function resolveLanguage(pidOrLanguage)
    if type(pidOrLanguage) == "number" then
        return getEffectiveLanguage(pidOrLanguage)
    end
    return normalizeLanguage(pidOrLanguage)
end

local function applyVariables(text, variables)
    if type(text) ~= "string" or type(variables) ~= "table" then
        return text
    end

    return (text:gsub("{([%w_]+)}", function(name)
        local value = variables[name]
        if value == nil then
            return "{" .. name .. "}"
        end
        return tostring(value)
    end))
end

local function mergeTable(destination, source)
    if type(source) ~= "table" then
        return
    end
    for key, value in pairs(source) do
        destination[key] = value
    end
end

local function getAutomaticTable(language, tableName)
    local result = {}
    for _, dictionary in pairs(dictionaries) do
        local automatic = dictionary.automatic
        if type(automatic) == "table" and type(automatic[language]) == "table" then
            mergeTable(result, automatic[language][tableName])
        end
    end
    return result
end

local function getAutomaticList(language, tableName)
    local result = {}
    for _, dictionary in pairs(dictionaries) do
        local automatic = dictionary.automatic
        if type(automatic) == "table" and type(automatic[language]) == "table" then
            local source = automatic[language][tableName]
            if type(source) == "table" then
                for _, value in ipairs(source) do
                    table.insert(result, value)
                end
            end
        end
    end
    return result
end

local function translateAutomatic(language, text)
    if language == "EN" or type(text) ~= "string" or text == "" then
        return text
    end

    local exact = getAutomaticTable(language, "exact")
    if exact[text] ~= nil then
        return exact[text]
    end

    local translated = text

    for _, rule in ipairs(getAutomaticList(language, "patterns")) do
        if type(rule) == "table" and type(rule.pattern) == "string" and rule.replacement ~= nil then
            local updated, count = translated:gsub(rule.pattern, rule.replacement)
            if count > 0 then
                translated = updated
            end
        end
    end

    local phrases = getAutomaticTable(language, "phrases")
    local ordered = {}
    for source, replacement in pairs(phrases) do
        table.insert(ordered, {source = source, replacement = replacement})
    end
    table.sort(ordered, function(left, right)
        return #left.source > #right.source
    end)

    for _, entry in ipairs(ordered) do
        translated = translated:gsub(entry.source:gsub("([^%w])", "%%%1"), function()
            return entry.replacement
        end)
    end

    return translated
end

--- Register a dictionary that can be reused by any server script.
--- Expected format:
--- {
---   EN = { key = "English text {name}" },
---   RU = { key = "Русский текст {name}" },
---   automatic = { RU = { exact = {}, patterns = {}, phrases = {} } }
--- }
function localization.RegisterDictionary(namespace, dictionary)
    if type(namespace) ~= "string" or namespace == "" or type(dictionary) ~= "table" then
        return false
    end

    dictionaries[namespace] = dictionary
    return true
end

function localization.LoadDictionary(namespace, moduleName)
    if type(moduleName) ~= "string" or moduleName == "" then
        return false
    end

    local ok, dictionary = pcall(require, moduleName)
    if not ok then
        tes3mp.LogMessage(enumerations.log.ERROR,
            "Unable to load localization dictionary " .. tostring(moduleName) .. ": " .. tostring(dictionary))
        return false
    end

    return localization.RegisterDictionary(namespace, dictionary)
end

function localization.GetLanguage(pid)
    return getEffectiveLanguage(pid)
end

function localization.GetClientLanguage(pid)
    return getClientLanguage(pid)
end

function localization.GetServerLanguage()
    return getConfiguredServerLanguage()
end

function localization.SetServerLanguage(language)
    if config == nil or type(language) ~= "string" then
        return false
    end

    language = string.upper(language)
    if language ~= "AUTO" and language ~= "RU" and language ~= "EN" then
        return false
    end

    config.serverLanguage = language
    return true
end

function localization.SetPlayerLanguage(pid, language)
    if Players == nil or Players[pid] == nil then
        return false
    end

    Players[pid].language = normalizeLanguage(language)
    return true
end

function localization.Get(pidOrLanguage, namespace, key, variables)
    local language = resolveLanguage(pidOrLanguage)
    local dictionary = dictionaries[namespace]
    if type(dictionary) ~= "table" then
        return namespace .. "." .. tostring(key)
    end

    local languageTable = dictionary[language]
    local englishTable = dictionary.EN
    local text = nil

    if type(languageTable) == "table" then
        text = languageTable[key]
    end
    if text == nil and type(englishTable) == "table" then
        text = englishTable[key]
    end
    if text == nil then
        return namespace .. "." .. tostring(key)
    end

    return applyVariables(text, variables)
end

function localization.TranslateText(pidOrLanguage, text)
    return translateAutomatic(resolveLanguage(pidOrLanguage), text)
end

local function translateButtonList(pid, buttons)
    if type(buttons) ~= "string" or buttons == "" then
        return buttons
    end

    local translated = {}
    local startIndex = 1
    while true do
        local separator = string.find(buttons, ";", startIndex, true)
        if separator == nil then
            table.insert(translated, localization.TranslateText(pid, string.sub(buttons, startIndex)))
            break
        end
        table.insert(translated, localization.TranslateText(pid, string.sub(buttons, startIndex, separator - 1)))
        startIndex = separator + 1
    end
    return table.concat(translated, ";")
end

function localization.Message(pid, namespace, key, variables, sendToOtherPlayers, skipAttachedPlayer)
    tes3mp.SendMessage(pid, localization.Get(pid, namespace, key, variables),
        sendToOtherPlayers == true, skipAttachedPlayer == true)
end

function localization.MessageBox(pid, id, namespace, key, variables)
    tes3mp.MessageBox(pid, id, localization.Get(pid, namespace, key, variables))
end

function localization.GetDictionaries()
    return dictionaries
end

local function sendLocalizedBroadcast(attachedPid, message, skipAttachedPlayer)
    if Players == nil then
        native.SendMessage(attachedPid, message, true, skipAttachedPlayer == true)
        return
    end

    local recipients = {}
    for recipientPid, player in pairs(Players) do
        if player ~= nil and not (skipAttachedPlayer == true and recipientPid == attachedPid) then
            recipients[recipientPid] = localization.TranslateText(recipientPid, message)
        end
    end

    -- During early connection validation, the attached pid may not have been
    -- inserted into Players yet, but it must still receive its own message.
    if not skipAttachedPlayer and recipients[attachedPid] == nil then
        recipients[attachedPid] = localization.TranslateText(attachedPid, message)
    end

    local firstText = nil
    local allTextsMatch = true
    local recipientCount = 0
    for _, translated in pairs(recipients) do
        recipientCount = recipientCount + 1
        if firstText == nil then
            firstText = translated
        elseif translated ~= firstText then
            allTextsMatch = false
            break
        end
    end

    -- Keep the original single broadcast packet whenever every recipient uses
    -- the same text. Mixed-language servers fall back to targeted packets.
    if recipientCount == 0 or allTextsMatch then
        native.SendMessage(attachedPid, firstText or message, true, skipAttachedPlayer == true)
        return
    end

    for recipientPid, translated in pairs(recipients) do
        native.SendMessageTo(attachedPid, recipientPid, translated)
    end
end

--- Install transparent wrappers around all CoreScripts chat and GUI output.
--- Existing scripts are localized without having to replace every SendMessage call.
function localization.InstallWrappers()
    if wrappersInstalled then
        return
    end

    native.SendMessage = tes3mp.SendMessage
    native.SendMessageTo = tes3mp.SendMessageTo
    native.MessageBox = tes3mp.MessageBox
    native.CustomMessageBox = tes3mp.CustomMessageBox
    native.InputDialog = tes3mp.InputDialog
    native.PasswordDialog = tes3mp.PasswordDialog
    native.ListBox = tes3mp.ListBox

    tes3mp.SendMessage = function(pid, message, sendToOtherPlayers, skipAttachedPlayer)
        sendToOtherPlayers = sendToOtherPlayers == true
        skipAttachedPlayer = skipAttachedPlayer == true

        if sendToOtherPlayers then
            sendLocalizedBroadcast(pid, message, skipAttachedPlayer)
        elseif not skipAttachedPlayer then
            native.SendMessage(pid, localization.TranslateText(pid, message), false, false)
        end
    end

    tes3mp.MessageBox = function(pid, id, label)
        native.MessageBox(pid, id, localization.TranslateText(pid, label))
    end

    tes3mp.CustomMessageBox = function(pid, id, label, buttons)
        native.CustomMessageBox(pid, id, localization.TranslateText(pid, label),
            translateButtonList(pid, buttons))
    end

    tes3mp.InputDialog = function(pid, id, label, note)
        native.InputDialog(pid, id, localization.TranslateText(pid, label),
            localization.TranslateText(pid, note))
    end

    tes3mp.PasswordDialog = function(pid, id, label, note)
        native.PasswordDialog(pid, id, localization.TranslateText(pid, label),
            localization.TranslateText(pid, note))
    end

    tes3mp.ListBox = function(pid, id, label, items)
        native.ListBox(pid, id, localization.TranslateText(pid, label),
            localization.TranslateText(pid, items))
    end

    wrappersInstalled = true
end

localization.NormalizeLanguage = normalizeLanguage

return localization
