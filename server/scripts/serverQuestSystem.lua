-- ArenaMP X043: server-authoritative quest extension core + instance-aware MyGUI Quest Studio.
--
-- Definitions, requirements, progression and rewards stay on the server. X036
-- exposes the published/player-visible subset through a hidden reliable GUI transport
-- so matching clients can render green topics inside the normal DialogueWindow without
-- changing the existing packet wire format.

local serverQuestSystem = {}
local privateCellInstances = require("privateCellInstances")
local unpackValues = table.unpack or unpack

local INDEX_PATH = "custom/quests/index.json"
local QUEST_DIR = "custom/quests/"
local SCHEMA_VERSION = 1
local MAX_AUDIT = 64
local MAX_TOPICS = 64
local MAX_STAGES = 256
local MAX_TARGETS = 8

-- X042 safe merge: destructive world/vanilla actions require an explicit
-- Administrator publish. A moderator may inspect/edit them in a draft, but the
-- definition cannot become runtime-active until an administrator approves it.
local ADMIN_ONLY_REWARD_TYPES = { setVanillaJournal = true, setServerVariable = true }

-- X036: hidden transport over the existing reliable GUI packet. Matching X036
-- clients consume this id before any modal GUI is created.
local QUEST_TRANSPORT_GUI_ID = -35036
local QUEST_TOPIC_PREFIX = "@ArenaQuest:"
local QUEST_CHOICE_PREFIX = "@ArenaQuestChoice:"

serverQuestSystem.quests = {}
serverQuestSystem.index = { schemaVersion = SCHEMA_VERSION, quests = {} }
serverQuestSystem.editor = {}
serverQuestSystem.validation = {}

local function log(level, message)
    tes3mp.LogMessage(level, "[ArenaMP Core] " .. tostring(message))
end

local function enabled()
    return config.serverQuests == nil or config.serverQuests.enabled ~= false
end

local function getAccountName(pid)
    if Players[pid] ~= nil and Players[pid].accountName ~= nil then
        return Players[pid].accountName
    end
    return "unknown"
end

local function isStaff(pid)
    return Players[pid] ~= nil and Players[pid]:IsServerStaff()
end

local function isModerator(pid)
    return Players[pid] ~= nil and (Players[pid]:IsModerator() or Players[pid]:IsAdmin() or Players[pid]:IsServerOwner())
end

local function isAdmin(pid)
    return Players[pid] ~= nil and (Players[pid]:IsAdmin() or Players[pid]:IsServerOwner())
end

local function send(pid, message)
    if Players[pid] ~= nil then
        Players[pid]:Message(color.SkyBlue .. "[Quest] " .. color.Default .. tostring(message) .. "\n")
    end
end

-- X050b: runtime quest notifications are separate from explicit /quest/editor
-- feedback. This keeps the authoritative quest system quiet in the normal chat
-- unless the administrator deliberately enables a category in config.lua.
local function runtimeChatEnabled(category)
    local questConfig = type(config.serverQuests) == "table" and config.serverQuests or {}
    if questConfig.chatNotificationsEnabled == false then return false end

    local key = nil
    if category == "progress" then key = "chatProgressMessages"
    elseif category == "reward" then key = "chatRewardMessages"
    elseif category == "error" then key = "chatRuntimeErrors" end

    return key ~= nil and questConfig[key] == true
end

local function sendRuntime(pid, message, category)
    if runtimeChatEnabled(category) then send(pid, message) end
end

local function percentEncode(value)
    value = tostring(value or "")
    return (value:gsub("([^%w%-%._~ ])", function(c)
        return string.format("%%%02X", string.byte(c))
    end))
end

local function percentDecode(value)
    value = tostring(value or "")
    return (value:gsub("%%(%x%x)", function(hex)
        return string.char(tonumber(hex, 16))
    end))
end

local function splitTabsDecoded(value)
    local result = {}
    value = tostring(value or "")
    for part in (value .. "\t"):gmatch("(.-)\t") do
        table.insert(result, percentDecode(part))
    end
    return result
end

local function transportLine(kind, ...)
    local fields = { kind }
    for index = 1, select('#', ...) do
        table.insert(fields, percentEncode(select(index, ...)))
    end
    return table.concat(fields, "\t")
end

local function sendTransport(pid, payload)
    if Players[pid] ~= nil and Players[pid]:IsLoggedIn() then
        tes3mp.CustomMessageBox(pid, QUEST_TRANSPORT_GUI_ID, payload, "")
    end
end

local function trim(value)
    if value == nil then return "" end
    return tostring(value):match("^%s*(.-)%s*$")
end

local function normalizeQuestCell(cellDescription)
    cellDescription = trim(cellDescription)
    if cellDescription == "" then return "" end
    if privateCellInstances ~= nil and type(privateCellInstances.GetBaseCellDescription) == "function" then
        cellDescription = privateCellInstances.GetBaseCellDescription(cellDescription) or cellDescription
    end
    return string.lower(trim(cellDescription))
end

local function questCellMatches(expectedCell, actualCell)
    expectedCell = trim(expectedCell)
    if expectedCell == "" then return true end
    return normalizeQuestCell(expectedCell) == normalizeQuestCell(actualCell)
end

local function splitPipe(value)
    local result = {}
    value = tostring(value or "")
    for part in (value .. "|"):gmatch("(.-)|") do
        table.insert(result, trim(part))
    end
    return result
end

local function normalizeId(value)
    value = trim(value):lower()
    value = value:gsub("%s+", "_")
    return value
end

local function validId(value)
    return type(value) == "string" and value ~= "" and value:match("^[a-z0-9_%.%-]+$") ~= nil
end

local function copy(value)
    if type(value) ~= "table" then return value end
    if tableHelper ~= nil and tableHelper.deepCopy ~= nil then
        return tableHelper.deepCopy(value)
    end
    local out = {}
    for k, v in pairs(value) do out[k] = copy(v) end
    return out
end

-- X054 ----------------------------------------------------------------------
-- Quest localization.
--
-- A quest JSON is authored in one language ("sourceLanguage", RU by default)
-- and may carry a "translations" block keyed by language code. The block
-- mirrors the quest structure by id, so an author never has to renumber
-- anything to add a language:
--
--   "sourceLanguage": "RU",
--   "translations": {
--     "EN": {
--       "name": "A Drink for Caius",
--       "topics":  { "<topicId>": "..." },
--       "offer":   { "dialogue": "...", "choices": { "<choiceId>": "..." } },
--       "stages":  { "10": { "journal": "...", "dialogue": "...",
--                            "choices": { "<choiceId>": "..." } } }
--     }
--   }
--
-- Nothing is required: a missing language, a missing key or an empty string all
-- fall through to the source text, so a partially translated quest still works.
-- Requirements, rewards, stage numbers and ids are never translated - only text
-- the player reads.
local questLocalizationCache = {}

local function questSourceLanguage(quest)
    local language = tostring(quest.sourceLanguage or "RU"):upper()
    if language ~= "EN" and language ~= "RU" then return "RU" end
    return language
end

local function questLocalizationKey(quest)
    return tostring(quest.version or 0) .. ":" .. tostring(quest.updatedAt or 0)
end

local function applyChoiceTranslations(choices, translated)
    if type(choices) ~= "table" or type(translated) ~= "table" then return end
    for _, choice in ipairs(choices) do
        local text = translated[choice.id]
        if type(text) == "string" and text ~= "" then choice.text = text end
    end
end

local function buildLocalizedQuest(quest, translation)
    local clone = copy(quest)

    if type(translation.name) == "string" and translation.name ~= "" then
        clone.name = translation.name
    end

    if clone.offer ~= nil and type(translation.offer) == "table" then
        if type(translation.offer.dialogue) == "string" and translation.offer.dialogue ~= "" then
            clone.offer.dialogue = translation.offer.dialogue
        end
        applyChoiceTranslations(clone.offer.choices, translation.offer.choices)
    end

    if type(translation.topics) == "table" then
        for _, topic in ipairs(clone.topics or {}) do
            local text = translation.topics[topic.id]
            if type(text) == "string" and text ~= "" then topic.text = text end
        end
    end

    if type(translation.stages) == "table" then
        for _, stage in ipairs(clone.stages or {}) do
            -- JSON object keys are strings; accept a numeric key too so a
            -- hand-written table is not silently ignored.
            local entry = translation.stages[tostring(stage.index)] or translation.stages[stage.index]
            if type(entry) == "table" then
                if type(entry.journal) == "string" and entry.journal ~= "" then stage.journal = entry.journal end
                if type(entry.dialogue) == "string" and entry.dialogue ~= "" then stage.dialogue = entry.dialogue end
                applyChoiceTranslations(stage.choices, entry.choices)
            end
        end
    end

    return clone
end

-- Returns the quest as the given player should read it. Falls back to the quest
-- itself whenever there is nothing to apply, so callers never have to branch.
function serverQuestSystem.GetLocalizedQuest(pid, quest)
    if type(quest) ~= "table" then return quest end

    local language = "RU"
    if type(localization) == "table" and type(localization.GetLanguage) == "function" then
        language = tostring(localization.GetLanguage(pid) or "RU"):upper()
    end
    if language == questSourceLanguage(quest) then return quest end

    local translations = quest.translations
    if type(translations) ~= "table" then return quest end
    local translation = translations[language]
    if type(translation) ~= "table" then return quest end

    local key = questLocalizationKey(quest)
    local perQuest = questLocalizationCache[quest.id]
    if perQuest ~= nil and perQuest.key == key and perQuest[language] ~= nil then
        return perQuest[language]
    end

    if perQuest == nil or perQuest.key ~= key then
        perQuest = { key = key }
        questLocalizationCache[quest.id] = perQuest
    end
    perQuest[language] = buildLocalizedQuest(quest, translation)
    return perQuest[language]
end

local function invalidateQuestLocalization(questId)
    if questId == nil then
        questLocalizationCache = {}
    else
        questLocalizationCache[questId] = nil
    end
end

local function questText(pid, key, variables)
    if type(localization) ~= "table" or type(localization.Get) ~= "function" then return "" end
    return localization.Get(pid, "serverQuests", key, variables)
end

local function saveJson(path, value)
    local ok, err = pcall(jsonInterface.save, path, value)
    if not ok then
        log(enumerations.log.ERROR, "Could not save " .. path .. ": " .. tostring(err))
        return false, tostring(err)
    end
    return true
end

local function loadJson(path)
    local ok, value = pcall(jsonInterface.load, path)
    if not ok or type(value) ~= "table" then
        return nil, ok and "not a table" or tostring(value)
    end
    return value
end

local function appendAudit(quest, actor, action)
    if quest.audit == nil then quest.audit = {} end
    table.insert(quest.audit, {
        time = os.time(),
        by = actor or "server",
        action = action or "changed"
    })
    while #quest.audit > MAX_AUDIT do table.remove(quest.audit, 1) end
end

local function sortQuestIds(ids)
    table.sort(ids, function(a, b) return tostring(a):lower() < tostring(b):lower() end)
    return ids
end

local function sortedQuestIds(filter)
    local ids = {}
    for id, quest in pairs(serverQuestSystem.quests) do
        if filter == nil or filter == "all" or quest.status == filter then
            table.insert(ids, id)
        end
    end
    return sortQuestIds(ids)
end

local function findStage(quest, stageIndex)
    if quest == nil or quest.stages == nil then return nil end
    stageIndex = tonumber(stageIndex)
    for _, stage in ipairs(quest.stages) do
        if tonumber(stage.index) == stageIndex then return stage end
    end
    return nil
end

local function findTopic(quest, topicId)
    if quest == nil or quest.topics == nil then return nil end
    topicId = normalizeId(topicId)
    for _, topic in ipairs(quest.topics) do
        if normalizeId(topic.id) == topicId then return topic end
    end
    return nil
end

local function normalizeChoice(choice)
    choice = type(choice) == "table" and choice or {}
    choice.id = normalizeId(choice.id)
    choice.text = trim(choice.text)
    choice.action = tostring(choice.action or "none"):lower()
    choice.targetStage = choice.targetStage ~= nil and tonumber(choice.targetStage) or nil
    choice.requirements = type(choice.requirements) == "table" and choice.requirements or {}
    return choice
end

local function normalizeChoiceList(list)
    list = type(list) == "table" and list or {}
    for index, choice in ipairs(list) do
        list[index] = normalizeChoice(choice)
    end
    return list
end

local function normalizeQuest(quest)
    quest.schemaVersion = tonumber(quest.schemaVersion) or SCHEMA_VERSION
    quest.id = normalizeId(quest.id)
    quest.name = trim(quest.name)
    quest.author = trim(quest.author)
    quest.status = quest.status or "draft"
    quest.progressMode = quest.progressMode or "personal"
    quest.version = tonumber(quest.version) or 1
    quest.createdAt = tonumber(quest.createdAt) or os.time()
    quest.updatedAt = tonumber(quest.updatedAt) or quest.createdAt
    quest.giver = type(quest.giver) == "table" and quest.giver or { refId = "", cell = "" }
    quest.giver.refId = trim(quest.giver.refId)
    quest.giver.cell = trim(quest.giver.cell)
    quest.topics = type(quest.topics) == "table" and quest.topics or {}
    quest.stages = type(quest.stages) == "table" and quest.stages or {}
    quest.offer = type(quest.offer) == "table" and quest.offer or nil
    if quest.offer ~= nil then
        quest.offer.dialogue = trim(quest.offer.dialogue)
        quest.offer.choices = normalizeChoiceList(quest.offer.choices)
    end
    quest.audit = type(quest.audit) == "table" and quest.audit or {}
    -- X054: translation metadata is preserved verbatim; only the language codes
    -- are normalized so "en"/"En"/"EN" all resolve to the same bucket.
    quest.sourceLanguage = questSourceLanguage(quest)
    if type(quest.translations) == "table" then
        local normalized = {}
        for language, block in pairs(quest.translations) do
            if type(block) == "table" then
                normalized[tostring(language):upper()] = block
            end
        end
        quest.translations = normalized
    end
    invalidateQuestLocalization(quest.id)

    for _, topic in ipairs(quest.topics) do
        topic.id = normalizeId(topic.id)
        topic.text = trim(topic.text)
        if topic.enabled == nil then topic.enabled = true end
        -- X036 will render this source as green rather than blue.
        topic.green = true
    end

    table.sort(quest.stages, function(a, b) return (tonumber(a.index) or 0) < (tonumber(b.index) or 0) end)
    for _, stage in ipairs(quest.stages) do
        stage.index = tonumber(stage.index) or 0
        stage.journal = trim(stage.journal)
        stage.dialogue = trim(stage.dialogue)
        stage.requirements = type(stage.requirements) == "table" and stage.requirements or {}
        stage.rewards = type(stage.rewards) == "table" and stage.rewards or {}
        stage.choices = normalizeChoiceList(stage.choices)
        stage.next = type(stage.next) == "table" and stage.next or (stage.next ~= nil and { tonumber(stage.next) } or {})
        if stage.complete == nil then stage.complete = false end
        if stage.fail == nil then stage.fail = false end
    end

    if quest.initialStage == nil and quest.stages[1] ~= nil then
        quest.initialStage = quest.stages[1].index
    end
    quest.initialStage = tonumber(quest.initialStage) or 0
    return quest
end

local function questSummary(quest)
    return {
        id = quest.id,
        name = quest.name,
        status = quest.status,
        version = quest.version,
        author = quest.author,
        updatedAt = quest.updatedAt
    }
end

function serverQuestSystem.SaveIndex()
    local entries = {}
    for _, id in ipairs(sortedQuestIds()) do
        table.insert(entries, questSummary(serverQuestSystem.quests[id]))
    end
    serverQuestSystem.index = { schemaVersion = SCHEMA_VERSION, quests = entries, updatedAt = os.time() }
    return saveJson(INDEX_PATH, serverQuestSystem.index)
end

function serverQuestSystem.SaveQuest(quest, actor, action)
    if type(quest) ~= "table" then return false, "quest is not a table" end
    normalizeQuest(quest)
    quest.updatedAt = os.time()
    if actor ~= nil and action ~= nil then appendAudit(quest, actor, action) end
    serverQuestSystem.quests[quest.id] = quest
    local errors, warnings = {}, {}
    if serverQuestSystem.ValidateQuest ~= nil then
        errors, warnings = serverQuestSystem.ValidateQuest(quest)
    end
    serverQuestSystem.validation[quest.id] = { errors = errors or {}, warnings = warnings or {} }
    local ok, err = saveJson(QUEST_DIR .. quest.id .. ".json", quest)
    if not ok then return false, err end
    serverQuestSystem.SaveIndex()
    if serverQuestSystem.SyncAll ~= nil then
        serverQuestSystem.SyncAll()
    end
    return true
end

function serverQuestSystem.LoadAll()
    serverQuestSystem.quests = {}
    serverQuestSystem.validation = {}
    local index = loadJson(INDEX_PATH)
    if index == nil or type(index.quests) ~= "table" then
        serverQuestSystem.SaveIndex()
        log(enumerations.log.WARN, "Quest index missing or invalid; created an empty index")
        return
    end

    local loaded = 0
    for _, entry in ipairs(index.quests) do
        local id = type(entry) == "table" and entry.id or entry
        id = normalizeId(id)
        if validId(id) then
            local quest, err = loadJson(QUEST_DIR .. id .. ".json")
            if quest ~= nil then
                normalizeQuest(quest)
                serverQuestSystem.quests[id] = quest
                local errors, warnings = serverQuestSystem.ValidateQuest(quest)
                serverQuestSystem.validation[id] = { errors = errors, warnings = warnings }
                if quest.status == "published" and #errors > 0 then
                    log(enumerations.log.ERROR, "Published quest " .. id .. " is runtime-disabled by " .. #errors .. " validation error(s)")
                end
                loaded = loaded + 1
            else
                log(enumerations.log.ERROR, "Failed to load quest " .. id .. ": " .. tostring(err))
            end
        end
    end
    serverQuestSystem.SaveIndex()
    if serverQuestSystem.SyncAll ~= nil then
        serverQuestSystem.SyncAll()
    end
    log(enumerations.log.INFO, "Loaded " .. loaded .. " server quest definition(s)")
end

function serverQuestSystem.ValidateQuest(quest)
    local errors, warnings = {}, {}
    if type(quest) ~= "table" then return { "Quest is not a table" }, warnings end
    normalizeQuest(quest)

    if not validId(quest.id) then table.insert(errors, "Invalid quest id") end
    if quest.name == "" then table.insert(errors, "Quest name is empty") end
    if quest.author == "" then table.insert(errors, "Quest author is empty") end
    if quest.progressMode ~= "personal" and quest.progressMode ~= "party" and quest.progressMode ~= "server" then
        table.insert(errors, "progressMode must be personal, party or server")
    end
    if quest.status ~= "draft" and quest.status ~= "published" and quest.status ~= "disabled" then
        table.insert(errors, "status must be draft, published or disabled")
    end
    if quest.giver.refId == "" then table.insert(errors, "Quest giver refId is missing") end
    if #quest.topics == 0 then table.insert(errors, "Quest has no dialogue topic") end
    if #quest.topics > MAX_TOPICS then table.insert(errors, "Too many topics") end
    if #quest.stages == 0 then table.insert(errors, "Quest has no stages") end
    if #quest.stages > MAX_STAGES then table.insert(errors, "Too many stages") end

    local topicIds = {}
    for _, topic in ipairs(quest.topics) do
        if not validId(topic.id) then table.insert(errors, "Invalid topic id: " .. tostring(topic.id)) end
        if topic.text == "" then table.insert(errors, "Topic " .. tostring(topic.id) .. " has empty text") end
        if topicIds[topic.id] then table.insert(errors, "Duplicate topic id: " .. topic.id) end
        topicIds[topic.id] = true
    end

    local stageIds = {}
    -- X042: the vocabulary grew, so validation grew with it. Anything not
    -- listed here is rejected at Publish rather than failing silently at
    -- runtime.
    local validRequirementTypes = { level = true, item = true, gold = true, questStage = true,
        questState = true, questCompleted = true, questNotStarted = true,
        playerVariable = true, serverVariable = true, staffRank = true,
        skill = true, attribute = true, faction = true, factionRank = true,
        reputation = true, bounty = true, race = true, class = true, cell = true,
        global = true, vanillaJournal = true, realTime = true, cooldown = true }
    local validRewardTypes = { gold = true, xp = true, item = true, giveItem = true, takeItem = true,
        setPlayerVariable = true, message = true,
        setServerVariable = true, addSpell = true, removeSpell = true,
        setReputation = true, setBounty = true, teleport = true,
        playSound = true, messageBox = true, setVanillaJournal = true }
    local validChoiceActions = { none = true, start = true, advance = true }

    local validateRequirements

    local function validateRequirementNode(requirement, context, depth)
        if type(requirement) ~= "table" then
            table.insert(errors, context .. " has a non-table requirement")
            return
        end
        if depth > 8 then
            table.insert(errors, context .. " nests requirements more than 8 levels deep")
            return
        end

        if requirement.all ~= nil then
            validateRequirements(requirement.all, context .. " all", depth + 1)
            return
        end
        if requirement.any ~= nil then
            if type(requirement.any) ~= "table" or #requirement.any == 0 then
                table.insert(errors, context .. " has an empty any group")
            else
                validateRequirements(requirement.any, context .. " any", depth + 1)
            end
            return
        end
        if requirement["not"] ~= nil then
            validateRequirementNode(requirement["not"], context .. " not", depth + 1)
            return
        end

        local kind = tostring(requirement.type or "")
        if not validRequirementTypes[kind] then
            table.insert(errors, context .. " has unknown requirement type " .. kind)
        elseif kind == "item" and trim(requirement.refId) == "" then
            table.insert(errors, context .. " item requirement has no refId")
        elseif (kind == "questStage" or kind == "questState" or kind == "questCompleted"
                or kind == "questNotStarted" or kind == "cooldown") and trim(requirement.questId) == "" then
            table.insert(errors, context .. " quest requirement has no questId")
        elseif (kind == "playerVariable" or kind == "serverVariable" or kind == "global")
                and trim(requirement.key) == "" then
            table.insert(errors, context .. " variable requirement has no key")
        elseif (kind == "skill" or kind == "attribute" or kind == "faction" or kind == "factionRank")
                and trim(requirement.key) == "" and trim(requirement.refId) == "" then
            table.insert(errors, context .. " " .. kind .. " requirement has no key")
        elseif kind == "vanillaJournal" and trim(requirement.questId) == "" and trim(requirement.key) == "" then
            table.insert(errors, context .. " vanillaJournal requirement has no questId")
        end
    end

    validateRequirements = function(list, context, depth)
        depth = depth or 0
        for _, requirement in ipairs(list or {}) do
            validateRequirementNode(requirement, context, depth)
        end
    end

    local function validateChoices(list, context, sourceStage)
        local ids = {}
        for _, choice in ipairs(list or {}) do
            if not validId(choice.id) then table.insert(errors, context .. " has invalid choice id") end
            if choice.text == "" then table.insert(errors, context .. " choice " .. tostring(choice.id) .. " has empty text") end
            if ids[choice.id] then table.insert(errors, context .. " has duplicate choice id " .. choice.id) end
            ids[choice.id] = true
            if not validChoiceActions[choice.action] then
                table.insert(errors, context .. " choice " .. tostring(choice.id) .. " has invalid action " .. tostring(choice.action))
            end
            if choice.action == "advance" then
                if choice.targetStage == nil then
                    table.insert(errors, context .. " choice " .. tostring(choice.id) .. " has no targetStage")
                elseif findStage(quest, choice.targetStage) == nil then
                    table.insert(errors, context .. " choice " .. tostring(choice.id) .. " points to missing stage " .. tostring(choice.targetStage))
                elseif sourceStage ~= nil and #(sourceStage.next or {}) > 0 then
                    local allowed = false
                    for _, target in ipairs(sourceStage.next) do
                        if tonumber(target) == tonumber(choice.targetStage) then allowed = true break end
                    end
                    if not allowed then
                        table.insert(errors, context .. " choice " .. tostring(choice.id) .. " target is not listed in stage.next")
                    end
                end
            end
            validateRequirements(choice.requirements, context .. " choice " .. tostring(choice.id))
        end
    end

    if quest.offer ~= nil then
        if quest.offer.dialogue == "" then table.insert(warnings, "Quest offer has no dialogue text") end
        validateChoices(quest.offer.choices, "Quest offer", nil)
    end

    for _, stage in ipairs(quest.stages) do
        local index = tonumber(stage.index)
        if index == nil or index < 0 or index > 100000 then
            table.insert(errors, "Invalid stage index: " .. tostring(stage.index))
        elseif stageIds[index] then
            table.insert(errors, "Duplicate stage index: " .. tostring(index))
        end
        stageIds[index] = true
        if stage.journal == "" then table.insert(warnings, "Stage " .. tostring(index) .. " has no journal text") end
        if stage.dialogue == "" then table.insert(warnings, "Stage " .. tostring(index) .. " has no dialogue text") end

        validateRequirements(stage.requirements, "Stage " .. tostring(index))
        validateChoices(stage.choices, "Stage " .. tostring(index), stage)

        for _, reward in ipairs(stage.rewards or {}) do
            local kind = tostring(reward.type or "")
            if not validRewardTypes[kind] then
                table.insert(errors, "Stage " .. tostring(index) .. " has unknown reward type " .. kind)
            elseif (kind == "item" or kind == "giveItem" or kind == "takeItem") and trim(reward.refId) == "" then
                table.insert(errors, "Stage " .. tostring(index) .. " item reward/action has no refId")
            elseif (kind == "gold" or kind == "xp") and tonumber(reward.amount or reward.value) == nil then
                table.insert(errors, "Stage " .. tostring(index) .. " " .. kind .. " reward has no numeric amount")
            elseif kind == "setPlayerVariable" and trim(reward.key) == "" then
                table.insert(errors, "Stage " .. tostring(index) .. " setPlayerVariable has no key")
            elseif (kind == "addSpell" or kind == "removeSpell") and trim(reward.refId) == "" then
                table.insert(errors, "Stage " .. tostring(index) .. " " .. kind .. " has no refId")
            elseif kind == "setServerVariable" and trim(reward.key) == "" then
                table.insert(errors, "Stage " .. tostring(index) .. " setServerVariable has no key")
            elseif kind == "teleport" and trim(reward.cell) == "" and trim(reward.value) == "" then
                table.insert(errors, "Stage " .. tostring(index) .. " teleport has no cell")
            elseif kind == "setVanillaJournal" then
                local targetQuest = trim(reward.questId) ~= "" and trim(reward.questId) or trim(reward.key)
                local whitelist = config.serverQuestVanillaJournalWhitelist
                if targetQuest == "" or tonumber(reward.index or reward.value) == nil then
                    table.insert(errors, "Stage " .. tostring(index) .. " setVanillaJournal needs questId and index")
                elseif type(whitelist) ~= "table" or not tableHelper.containsCaseInsensitiveString(whitelist, targetQuest) then
                    table.insert(errors, "Stage " .. tostring(index) ..
                        " setVanillaJournal target " .. targetQuest ..
                        " is not in config.serverQuestVanillaJournalWhitelist")
                end
            end

            if ADMIN_ONLY_REWARD_TYPES[kind] then
                if quest.adminApprovedDangerousRewards == true then
                    table.insert(warnings, "Stage " .. tostring(index) .. " uses Administrator-approved reward " .. kind)
                else
                    table.insert(errors, "Stage " .. tostring(index) .. " uses Administrator-only reward " .. kind
                        .. "; an administrator must publish this draft")
                end
            end
        end
    end

    if findStage(quest, quest.initialStage) == nil then
        table.insert(errors, "initialStage does not exist")
    end

    for _, stage in ipairs(quest.stages) do
        for _, target in ipairs(stage.next or {}) do
            if findStage(quest, target) == nil then
                table.insert(errors, "Stage " .. stage.index .. " points to missing stage " .. tostring(target))
            end
        end
    end

    return errors, warnings
end

local function ensurePlayerData(pid)
    if Players[pid] == nil then return nil end
    local cv = Players[pid].data.customVariables
    if cv == nil then
        Players[pid].data.customVariables = {}
        cv = Players[pid].data.customVariables
    end
    if cv.serverQuests == nil then cv.serverQuests = {} end
    if cv.serverQuestVariables == nil then cv.serverQuestVariables = {} end
    return cv
end

function serverQuestSystem.GetPlayerState(pid, questId)
    local cv = ensurePlayerData(pid)
    if cv == nil then return nil end
    return cv.serverQuests[normalizeId(questId)]
end

local function compareValues(left, op, right)
    if tonumber(left) ~= nil and tonumber(right) ~= nil then
        left, right = tonumber(left), tonumber(right)
    else
        left, right = tostring(left), tostring(right)
    end
    if op == "==" or op == "=" then return left == right end
    if op == "!=" or op == "~=" then return left ~= right end
    if op == ">" then return left > right end
    if op == ">=" then return left >= right end
    if op == "<" then return left < right end
    if op == "<=" then return left <= right end
    return false
end

local function inventoryCount(pid, refId)
    local total = 0
    local inventory = Players[pid].data.inventory or {}
    for _, item in pairs(inventory) do
        if item.refId ~= nil and tostring(item.refId):lower() == tostring(refId):lower() then
            total = total + (tonumber(item.count) or 0)
        end
    end
    return total
end

-- X042: helpers for the vanilla-facing requirement types. Every accessor is
-- defensive: a missing table must fail the check, never raise.
local function playerData(pid)
    if Players[pid] == nil then return nil end
    return Players[pid].data
end

local function lookupCaseInsensitive(input, key)
    if type(input) ~= "table" or key == nil then return nil end
    if input[key] ~= nil then return input[key] end
    local wanted = tostring(key):lower()
    for candidate, value in pairs(input) do
        if tostring(candidate):lower() == wanted then return value end
    end
    return nil
end

local function skillValue(pid, name)
    local data = playerData(pid)
    if data == nil or data.skills == nil or name == nil then return nil end
    local entry = lookupCaseInsensitive(data.skills, name)
    if entry == nil then return nil end
    return tonumber(entry.base) or tonumber(entry)
end

local function attributeValue(pid, name)
    local data = playerData(pid)
    if data == nil or data.attributes == nil or name == nil then return nil end
    local entry = lookupCaseInsensitive(data.attributes, name)
    if entry == nil then return nil end
    return tonumber(entry.base) or tonumber(entry)
end

local function factionRank(pid, factionId)
    local data = playerData(pid)
    if data == nil or data.factionRanks == nil or factionId == nil then return nil end
    return tonumber(lookupCaseInsensitive(data.factionRanks, factionId))
end

local function worldVariables()
    if WorldInstance == nil or WorldInstance.data == nil then return {} end
    return WorldInstance.data.customVariables or {}
end

local function vanillaGlobal(key)
    if WorldInstance == nil or WorldInstance.data == nil then return nil end
    local globals = WorldInstance.data.clientVariables
    if globals == nil or globals.globals == nil or key == nil then return nil end
    local entry = lookupCaseInsensitive(globals.globals, key)
    if type(entry) == "table" then return entry.intValue or entry.floatValue end
    return entry
end

local function vanillaJournalIndex(pid, questId)
    local data = playerData(pid)
    if data == nil or data.journal == nil or questId == nil then return nil end
    local best = nil
    local wanted = tostring(questId):lower()
    for _, entry in pairs(data.journal) do
        if entry.quest ~= nil and tostring(entry.quest):lower() == wanted then
            local index = tonumber(entry.index)
            if index ~= nil and (best == nil or index > best) then best = index end
        end
    end
    return best
end

function serverQuestSystem.CheckRequirement(pid, state, requirement)
    if type(requirement) ~= "table" then return false, "invalid requirement" end
    local kind = tostring(requirement.type or "")
    local op = tostring(requirement.operator or ">=")
    local value = requirement.value

    if kind == "level" then
        return compareValues(Players[pid].data.stats.level or 1, op, value), "level"
    elseif kind == "item" then
        return compareValues(inventoryCount(pid, requirement.refId), op, tonumber(requirement.count or value or 1)), "item"
    elseif kind == "gold" then
        return compareValues(inventoryCount(pid, "gold_001"), op, tonumber(value or requirement.count or 0)), "gold"
    elseif kind == "questStage" then
        local other = serverQuestSystem.GetPlayerState(pid, requirement.questId)
        local stage = other ~= nil and other.stage or -1
        return compareValues(stage, op, value), "questStage"
    elseif kind == "questState" then
        local other = serverQuestSystem.GetPlayerState(pid, requirement.questId)
        return compareValues(other ~= nil and other.state or "not_started", op, value), "questState"
    elseif kind == "questCompleted" then
        local other = serverQuestSystem.GetPlayerState(pid, requirement.questId)
        return other ~= nil and other.state == "completed", "questCompleted"
    elseif kind == "questNotStarted" then
        return serverQuestSystem.GetPlayerState(pid, requirement.questId) == nil, "questNotStarted"
    elseif kind == "playerVariable" then
        local cv = ensurePlayerData(pid)
        return compareValues(cv.serverQuestVariables[requirement.key], op, value), "playerVariable"
    elseif kind == "serverVariable" then
        return compareValues(worldVariables()[requirement.key], op, value), "serverVariable"
    elseif kind == "staffRank" then
        return compareValues(Players[pid].data.settings.staffRank or 0, op, value), "staffRank"

    -- X042: vanilla-facing conditions.
    elseif kind == "skill" then
        local current = skillValue(pid, requirement.key or requirement.refId)
        if current == nil then return false, "skill (unknown)" end
        return compareValues(current, op, value), "skill"
    elseif kind == "attribute" then
        local current = attributeValue(pid, requirement.key or requirement.refId)
        if current == nil then return false, "attribute (unknown)" end
        return compareValues(current, op, value), "attribute"
    elseif kind == "faction" then
        return factionRank(pid, requirement.key or requirement.refId) ~= nil, "faction"
    elseif kind == "factionRank" then
        local rank = factionRank(pid, requirement.key or requirement.refId)
        if rank == nil then return false, "factionRank (not a member)" end
        return compareValues(rank, op, value), "factionRank"
    elseif kind == "reputation" then
        local fame = Players[pid].data.fame or {}
        return compareValues(fame.reputation or 0, op, value), "reputation"
    elseif kind == "bounty" then
        local fame = Players[pid].data.fame or {}
        return compareValues(fame.bounty or 0, op, value), "bounty"
    elseif kind == "race" then
        local character = Players[pid].data.character or {}
        return compareValues(tostring(character.race or ""):lower(), op == ">=" and "==" or op,
            tostring(value or ""):lower()), "race"
    elseif kind == "class" then
        local character = Players[pid].data.character or {}
        return compareValues(tostring(character.class or ""):lower(), op == ">=" and "==" or op,
            tostring(value or ""):lower()), "class"
    elseif kind == "cell" then
        local location = Players[pid].data.location or {}
        return compareValues(tostring(location.cell or ""):lower(), op == ">=" and "==" or op,
            tostring(value or ""):lower()), "cell"
    elseif kind == "global" then
        local current = vanillaGlobal(requirement.key)
        if current == nil then return false, "global (unset)" end
        return compareValues(current, op, value), "global"
    elseif kind == "vanillaJournal" then
        local index = vanillaJournalIndex(pid, requirement.questId or requirement.key)
        if index == nil then return compareValues(-1, op, value), "vanillaJournal" end
        return compareValues(index, op, value), "vanillaJournal"
    elseif kind == "realTime" then
        return compareValues(os.time(), op, tonumber(value) or 0), "realTime"
    elseif kind == "cooldown" then
        -- True once the quest may be taken again.
        local other = serverQuestSystem.GetPlayerState(pid, requirement.questId)
        if other == nil or other.completedAt == nil then return true, "cooldown" end
        local seconds = tonumber(requirement.value or requirement.count) or 0
        return os.time() >= (tonumber(other.completedAt) or 0) + seconds, "cooldown"
    end
    return false, "unknown requirement type " .. kind
end

-- X042: requirement lists are now a boolean tree. A plain array stays an AND,
-- so every existing quest keeps working untouched; an entry carrying "all",
-- "any" or "not" nests instead.
local checkRequirementList

-- X048: every failing check now also reports *which* requirement table failed, so
-- the dialogue layer can tell the player what is missing instead of silently
-- replaying the same menu.
local function checkRequirementNode(pid, state, node, depth)
    if type(node) ~= "table" then return false, "invalid requirement", nil end
    if depth > 8 then return false, "requirement nesting too deep", nil end

    if node.all ~= nil then
        return checkRequirementList(pid, state, node.all, depth + 1)
    end
    if node.any ~= nil then
        local list = node.any
        if type(list) ~= "table" or #list == 0 then return false, "empty any", nil end
        local lastWhy, lastNode = "any", nil
        for _, child in ipairs(list) do
            local ok, why, failing = checkRequirementNode(pid, state, child, depth + 1)
            if ok then return true, why, nil end
            lastWhy, lastNode = why, failing
        end
        return false, lastWhy, lastNode
    end
    if node["not"] ~= nil then
        local ok, why = checkRequirementNode(pid, state, node["not"], depth + 1)
        if ok then return false, "not " .. tostring(why), node end
        return true, "not " .. tostring(why), nil
    end

    local ok, why = serverQuestSystem.CheckRequirement(pid, state, node)
    if ok then return true, why, nil end
    return false, why, node
end

checkRequirementList = function(pid, state, requirements, depth)
    depth = depth or 0
    if depth > 8 then return false, "requirement nesting too deep", nil end
    for _, requirement in ipairs(requirements or {}) do
        local ok, why, failing = checkRequirementNode(pid, state, requirement, depth)
        if not ok then return false, why, failing end
    end
    return true
end

function serverQuestSystem.CheckStageRequirements(pid, state, stage)
    return checkRequirementList(pid, state, stage.requirements)
end

local function giveInventory(pid, refId, count)
    count = math.floor(tonumber(count) or 0)
    if count <= 0 then return true end
    local item = { refId = refId, count = count, charge = -1, enchantmentCharge = -1, soul = "", poisonId = "", poisonCharges = 0 }
    inventoryHelper.addItem(Players[pid].data.inventory, item.refId, item.count, item.charge,
        item.enchantmentCharge, item.soul, item.poisonId, item.poisonCharges)
    Players[pid]:LoadItemChanges({ item }, enumerations.inventory.ADD)
    return true
end

local function takeInventory(pid, refId, count)
    count = math.floor(tonumber(count) or 0)
    if count <= 0 then return true end
    if inventoryCount(pid, refId) < count then return false, "not enough " .. refId end
    inventoryHelper.removeClosestItem(Players[pid].data.inventory, refId, count)
    tableHelper.cleanNils(Players[pid].data.inventory)
    Players[pid]:LoadItemChanges({ { refId = refId, count = count } }, enumerations.inventory.REMOVE)
    return true
end

local function callIfPresent(name, ...)
    if type(tes3mp) == "table" and type(tes3mp[name]) == "function" then
        tes3mp[name](...)
        return true
    end
    return false
end

-- X048 ---------------------------------------------------------------------
-- The server's inventory mirror is only refreshed when the client volunteers a
-- PlayerInventory packet. Console additem and a few scripted paths do not send
-- one, so an item requirement could fail while the player really is holding the
-- item. RequestInventory asks the client for a full inventory dump; it is a new
-- ScriptFunction, so the call is guarded and simply does nothing on an older
-- binary.
local function requestInventoryResync(pid)
    return callIfPresent("RequestInventory", pid)
end

-- Player-facing description of a single unmet requirement.
local function requirementText(pid, requirement)
    -- X054: these lines used to be hardcoded Russian, so an EN player was told
    -- in Russian what they were missing. They now come from the serverQuests
    -- dictionary like every other engine string.
    if type(requirement) ~= "table" then return questText(pid, "requirement_unknown") end

    local kind = tostring(requirement.type or "?")
    local op = tostring(requirement.operator or ">=")
    local value = requirement.value

    if kind == "item" then
        local refId = tostring(requirement.refId or "?")
        local need = math.floor(tonumber(requirement.count or value or 1) or 1)
        return questText(pid, "requirement_item",
            { refId = refId, need = need, have = inventoryCount(pid, refId) })
    elseif kind == "gold" then
        local need = math.floor(tonumber(value or requirement.count or 0) or 0)
        return questText(pid, "requirement_gold",
            { op = op, need = need, have = inventoryCount(pid, "gold_001") })
    elseif kind == "level" then
        return questText(pid, "requirement_level", { op = op, value = tostring(value) })
    elseif kind == "skill" or kind == "attribute" then
        return questText(pid, "requirement_stat", { kind = kind,
            key = tostring(requirement.key or requirement.refId or "?"), op = op, value = tostring(value) })
    elseif kind == "faction" or kind == "factionRank" then
        return questText(pid, "requirement_faction",
            { key = tostring(requirement.key or requirement.refId or "?"), op = op, value = tostring(value or "") })
    elseif kind == "questCompleted" then
        return questText(pid, "requirement_quest_completed", { questId = tostring(requirement.questId or "?") })
    elseif kind == "questStage" or kind == "questState" then
        return questText(pid, "requirement_quest_stage",
            { questId = tostring(requirement.questId or "?"), op = op, value = tostring(value) })
    end

    return questText(pid, "requirement_generic", { kind = kind, op = op, value = tostring(value) })
end

-- Requirements the player can satisfy by simply carrying something are worth a
-- resync + automatic retry; the rest are not going to change within a second.
local function requirementIsInventoryBased(requirement)
    if type(requirement) ~= "table" then return false end
    local kind = tostring(requirement.type or "")
    return kind == "item" or kind == "gold"
end
-- End of X048 --------------------------------------------------------------

local function applyReward(pid, state, reward)
    local kind = tostring(reward.type or "")
    if kind == "gold" then
        return giveInventory(pid, "gold_001", reward.amount or reward.value)
    elseif kind == "item" or kind == "giveItem" then
        return giveInventory(pid, reward.refId, reward.count or 1)
    elseif kind == "takeItem" then
        return takeInventory(pid, reward.refId, reward.count or 1)
    elseif kind == "xp" then
        local amount = tonumber(reward.amount or reward.value) or 0
        if reward.scaled ~= false then
            local multiplier = 1
            if config.xpLeveling ~= nil and tonumber(config.xpLeveling["xp gain multiplier"]) ~= nil then
                multiplier = tonumber(config.xpLeveling["xp gain multiplier"])
            elseif tonumber(config.arenaXpRateMultiplier) ~= nil then
                multiplier = tonumber(config.arenaXpRateMultiplier)
            end
            amount = amount * math.max(0, multiplier)
        end

        -- X050: server quest XP uses the same same-cell party splitter as
        -- native kill/quest XP. The amount is already server-scaled here, so
        -- the client receives it as an exact award and only performs level-up
        -- processing locally.
        if groupHelper ~= nil and type(groupHelper.AwardServerQuestXp) == "function" then
            return groupHelper.AwardServerQuestXp(pid, amount, "Server quest reward")
        end

        -- Compatibility fallback for servers that deliberately omit groupHelper.
        Players[pid].data.stats.experience = math.max(0, (Players[pid].data.stats.experience or 0) + amount)
        Players[pid]:LoadLevel()
        return true
    elseif kind == "setPlayerVariable" then
        local cv = ensurePlayerData(pid)
        cv.serverQuestVariables[tostring(reward.key)] = reward.value
        return true
    elseif kind == "message" then
        sendRuntime(pid, tostring(reward.text or reward.value or ""), "reward")
        return true

    -- X042: additional server-authoritative effects.
    elseif kind == "setServerVariable" then
        if WorldInstance == nil or WorldInstance.data == nil then return false, "no world instance" end
        WorldInstance.data.customVariables = WorldInstance.data.customVariables or {}
        WorldInstance.data.customVariables[tostring(reward.key)] = reward.value
        if type(WorldInstance.QuicksaveToDrive) == "function" then WorldInstance:QuicksaveToDrive() end
        return true
    elseif kind == "addSpell" then
        local refId = tostring(reward.refId or "")
        if refId == "" then return false, "addSpell has no refId" end
        local spellbook = Players[pid].data.spellbook
        if spellbook ~= nil and not tableHelper.containsValue(spellbook, refId) then
            table.insert(spellbook, refId)
        end
        callIfPresent("ClearSpellbookChanges", pid)
        callIfPresent("SetSpellbookChangesAction", pid, enumerations.spellbook.ADD)
        callIfPresent("AddSpell", pid, refId)
        callIfPresent("SendSpellbookChanges", pid)
        return true
    elseif kind == "removeSpell" then
        local refId = tostring(reward.refId or "")
        if refId == "" then return false, "removeSpell has no refId" end
        local spellbook = Players[pid].data.spellbook
        if spellbook ~= nil then tableHelper.removeValue(spellbook, refId) end
        callIfPresent("ClearSpellbookChanges", pid)
        callIfPresent("SetSpellbookChangesAction", pid, enumerations.spellbook.REMOVE)
        callIfPresent("AddSpell", pid, refId)
        callIfPresent("SendSpellbookChanges", pid)
        return true
    elseif kind == "setReputation" then
        Players[pid].data.fame = Players[pid].data.fame or { bounty = 0, reputation = 0 }
        Players[pid].data.fame.reputation = tonumber(reward.value or reward.amount) or 0
        callIfPresent("SetReputation", pid, Players[pid].data.fame.reputation)
        callIfPresent("SendReputation", pid)
        return true
    elseif kind == "setBounty" then
        Players[pid].data.fame = Players[pid].data.fame or { bounty = 0, reputation = 0 }
        Players[pid].data.fame.bounty = math.max(0, tonumber(reward.value or reward.amount) or 0)
        callIfPresent("SetBounty", pid, Players[pid].data.fame.bounty)
        callIfPresent("SendBounty", pid)
        return true
    elseif kind == "teleport" then
        local cell = tostring(reward.cell or reward.value or "")
        if cell == "" then return false, "teleport has no cell" end
        local x = tonumber(reward.posX) or 0
        local y = tonumber(reward.posY) or 0
        local z = tonumber(reward.posZ) or 0
        if logicHandler ~= nil and type(logicHandler.TeleportToCell) == "function" then
            logicHandler.TeleportToCell(pid, cell, x, y, z, tonumber(reward.rotX) or 0, tonumber(reward.rotZ) or 0)
            return true
        end
        return false, "teleport unsupported on this server build"
    elseif kind == "playSound" then
        return callIfPresent("PlaySpeech", pid, tostring(reward.refId or reward.value or "")), "playSound"
    elseif kind == "messageBox" then
        callIfPresent("CustomMessageBox", pid, -1, tostring(reward.text or reward.value or ""), "Ok")
        return true
    elseif kind == "setVanillaJournal" then
        -- Deliberately the most restricted action in the vocabulary: rewriting
        -- the original journal can break the main quest for everyone. Gated by
        -- an explicit whitelist in config.lua, checked at apply time as well as
        -- at validation time.
        local questId = tostring(reward.questId or reward.key or "")
        local index = tonumber(reward.index or reward.value)
        if questId == "" or index == nil then return false, "setVanillaJournal needs questId and index" end
        local whitelist = config.serverQuestVanillaJournalWhitelist
        if type(whitelist) ~= "table" or not tableHelper.containsCaseInsensitiveString(whitelist, questId) then
            return false, "setVanillaJournal target " .. questId .. " is not whitelisted"
        end
        callIfPresent("ClearJournalChanges", pid)
        callIfPresent("AddJournalEntry", pid, questId, index, "")
        callIfPresent("SendJournalChanges", pid)
        return true
    end
    return false, "unknown reward type " .. kind
end

local function vanillaJournalEntryCount(pid)
    local journal = nil
    if config.shareJournal == true and WorldInstance ~= nil and WorldInstance.data ~= nil then
        journal = WorldInstance.data.journal
    elseif Players[pid] ~= nil and Players[pid].data ~= nil then
        journal = Players[pid].data.journal
    end

    local count = 0
    for _, item in pairs(journal or {}) do
        if item.type == enumerations.journal.ENTRY then count = count + 1 end
    end
    return count
end

local function appendJournal(pid, state, stage)
    state.journal = state.journal or {}
    if stage.journal ~= "" then
        table.insert(state.journal, {
            stage = stage.index,
            text = stage.journal,
            time = os.time(),
            vanillaAnchor = vanillaJournalEntryCount(pid)
        })
    end
end

local function applyStageRewardsAtMostOnce(pid, quest, state, stage)
    state.rewardedStages = state.rewardedStages or {}
    local key = tostring(stage.index)
    if state.rewardedStages[key] then return true end
    if tonumber(state.pendingRewardStage) == tonumber(stage.index) then
        return false, "reward transaction for stage " .. key .. " is pending manual review after an interrupted save"
    end

    -- At-most-once transaction marker. If the process dies after this quicksave,
    -- an administrator can explicitly retry the reward rather than the server
    -- duplicating gold/items on every reconnect.
    state.pendingRewardStage = stage.index
    Players[pid]:QuicksaveToDrive()

    for _, reward in ipairs(stage.rewards or {}) do
        local ok, err = applyReward(pid, state, reward)
        if not ok then
            state.rewardError = tostring(err)
            Players[pid]:QuicksaveToDrive()
            return false, err
        end
    end

    state.rewardedStages[key] = true
    state.pendingRewardStage = nil
    state.rewardError = nil
    return true
end

function serverQuestSystem.StartQuest(pid, questId, force, silent)
    questId = normalizeId(questId)
    local quest = serverQuestSystem.quests[questId]
    if quest == nil then return false, "Unknown quest " .. questId end
    if not force and quest.status ~= "published" then return false, "Quest is not published" end
    if not force then
        local validation = serverQuestSystem.validation[questId]
        if validation ~= nil and #validation.errors > 0 then
            return false, "Quest is runtime-disabled by validation errors"
        end
    end

    local cv = ensurePlayerData(pid)
    local existing = cv.serverQuests[questId]
    if existing ~= nil and existing.state ~= "failed" then return false, "Quest already started" end
    local stage = findStage(quest, quest.initialStage)
    if stage == nil then return false, "Initial stage is missing" end

    local state = {
        state = "active",
        stage = stage.index,
        definitionVersion = quest.version,
        startedAt = os.time(),
        updatedAt = os.time(),
        variables = {},
        journal = {},
        rewardedStages = {}
    }
    local requirementsOk, why, failing = serverQuestSystem.CheckStageRequirements(pid, state, stage)
    if not requirementsOk and not force then return false, "Requirements failed: " .. tostring(why), failing end

    cv.serverQuests[questId] = state
    appendJournal(pid, state, stage)
    local rewardsOk, rewardError = applyStageRewardsAtMostOnce(pid, quest, state, stage)
    if not rewardsOk then return false, "Reward error: " .. tostring(rewardError) end
    if stage.complete then state.state = "completed" elseif stage.fail then state.state = "failed" end
    -- X042: stamp the completion time so the "cooldown" requirement and
    -- repeatable quests have something authoritative to compare against.
    if state.state == "completed" or state.state == "failed" then state.completedAt = os.time() end
    Players[pid]:QuicksaveToDrive()
    if serverQuestSystem.SyncPlayer ~= nil then serverQuestSystem.SyncPlayer(pid) end
    if not silent then
        local view = serverQuestSystem.GetLocalizedQuest(pid, quest)
        sendRuntime(pid, questText(pid, "quest_started", { name = view.name, stage = stage.index }), "progress")
    end
    return true, state
end

function serverQuestSystem.AdvanceQuest(pid, questId, targetStage, force, silent)
    questId = normalizeId(questId)
    local quest = serverQuestSystem.quests[questId]
    local state = serverQuestSystem.GetPlayerState(pid, questId)
    if quest == nil then return false, "Unknown quest" end
    if state == nil then return false, "Quest is not started" end
    if state.state ~= "active" and not force then return false, "Quest is not active" end

    local current = findStage(quest, state.stage)
    local target = findStage(quest, targetStage)
    if target == nil then return false, "Target stage does not exist" end

    if not force and current ~= nil and current.next ~= nil and #current.next > 0 then
        local allowed = false
        for _, nextStage in ipairs(current.next) do
            if tonumber(nextStage) == tonumber(target.index) then allowed = true break end
        end
        if not allowed then return false, "Stage transition is not allowed" end
    end

    local requirementsOk, why, failing = serverQuestSystem.CheckStageRequirements(pid, state, target)
    if not requirementsOk and not force then return false, "Requirements failed: " .. tostring(why), failing end

    state.stage = target.index
    state.updatedAt = os.time()
    state.definitionVersion = quest.version
    appendJournal(pid, state, target)
    local rewardsOk, rewardError = applyStageRewardsAtMostOnce(pid, quest, state, target)
    if not rewardsOk then return false, "Reward error: " .. tostring(rewardError) end
    if target.complete then state.state = "completed" elseif target.fail then state.state = "failed" else state.state = "active" end
    if state.state == "completed" or state.state == "failed" then state.completedAt = os.time() end
    Players[pid]:QuicksaveToDrive()
    if serverQuestSystem.SyncPlayer ~= nil then serverQuestSystem.SyncPlayer(pid) end
    if not silent then
        local view = serverQuestSystem.GetLocalizedQuest(pid, quest)
        if state.state ~= "active" then
            sendRuntime(pid, questText(pid, "quest_advanced_state",
                { name = view.name, stage = target.index, state = state.state }), "progress")
        else
            sendRuntime(pid, questText(pid, "quest_advanced",
                { name = view.name, stage = target.index }), "progress")
        end
    end
    return true, state
end

function serverQuestSystem.GetAvailableTopics(pid, actorRefId, cellDescription)
    local result = {}
    actorRefId = tostring(actorRefId or ""):lower()
    cellDescription = tostring(cellDescription or "")
    for _, id in ipairs(sortedQuestIds("published")) do
        local quest = serverQuestSystem.quests[id]
        local giverMatches = tostring(quest.giver.refId or ""):lower() == actorRefId
        local cellMatches = questCellMatches(quest.giver.cell, cellDescription)
        local validation = serverQuestSystem.validation[id]
        if giverMatches and cellMatches and (validation == nil or #validation.errors == 0) then
            local state = serverQuestSystem.GetPlayerState(pid, id)
            if state == nil or state.state == "active" then
                -- X054: the green topic text is what the player clicks on, so it
                -- comes from their language view of the quest.
                quest = serverQuestSystem.GetLocalizedQuest(pid, quest)
                for _, topic in ipairs(quest.topics) do
                    if topic.enabled ~= false then
                        table.insert(result, {
                            questId = id,
                            topicId = topic.id,
                            text = topic.text,
                            green = true,
                            stage = state ~= nil and state.stage or quest.initialStage
                        })
                    end
                end
            end
        end
    end
    return result
end

function serverQuestSystem.GetCurrentDialogue(pid, questId)
    local quest = serverQuestSystem.quests[normalizeId(questId)]
    if quest == nil then return nil end
    local state = serverQuestSystem.GetPlayerState(pid, quest.id)

    -- X054: everything below this line is text the player reads - the quest
    -- name, the NPC line and the answer buttons - so switch to their language
    -- view. Ids, stage numbers and requirements are identical in every language,
    -- so validation elsewhere is unaffected.
    quest = serverQuestSystem.GetLocalizedQuest(pid, quest)

    local source = nil
    local stageIndex = 0
    local stateName = "not_started"
    if state == nil and quest.offer ~= nil then
        source = quest.offer
    else
        stageIndex = state ~= nil and state.stage or quest.initialStage
        source = findStage(quest, stageIndex)
        if state ~= nil then stateName = state.state end
    end
    if source == nil then return nil end

    local visibleChoices = {}
    for _, choice in ipairs(source.choices or {}) do
        local ok = checkRequirementList(pid, state, choice.requirements)
        if ok then
            table.insert(visibleChoices, {
                id = choice.id,
                text = choice.text,
                action = choice.action,
                targetStage = choice.targetStage
            })
        end
    end

    return {
        questId = quest.id,
        questName = quest.name,
        stage = stageIndex,
        text = source.dialogue,
        state = stateName,
        next = source.next ~= nil and copy(source.next) or {},
        choices = visibleChoices
    }
end

-- Y015: Older saves predate mixed vanilla/custom journal chronology. Give those
-- entries a one-time conservative anchor immediately before the newest vanilla
-- entry instead of leaving them permanently forced to the end of the journal.
local function migrateLegacyJournalAnchors(pid)
    local cv = ensurePlayerData(pid)
    if cv == nil then return false end
    local fallback = math.max(0, vanillaJournalEntryCount(pid) - 1)
    local changed = false
    for _, state in pairs(cv.serverQuests or {}) do
        for _, entry in ipairs(state.journal or {}) do
            if entry.vanillaAnchor == nil then
                entry.vanillaAnchor = fallback
                changed = true
            end
        end
    end
    if changed and Players[pid] ~= nil then Players[pid]:QuicksaveToDrive() end
    return changed
end

-- X036: publish the player-specific visible subset to the matching client.  The
-- definition stays authoritative on the server; the client receives only enough
-- data to render the green topic and the current response.
function serverQuestSystem.SyncPlayer(pid)
    if not enabled() or Players[pid] == nil or not Players[pid]:IsLoggedIn() then return end

    migrateLegacyJournalAnchors(pid)
    sendTransport(pid, "CLEAR")
    for _, id in ipairs(sortedQuestIds("published")) do
        local quest = serverQuestSystem.GetLocalizedQuest(pid, serverQuestSystem.quests[id])
        local validation = serverQuestSystem.validation[id]
        local state = serverQuestSystem.GetPlayerState(pid, id)
        if validation == nil or #validation.errors == 0 then
            -- X037: started/completed server quests are synced independently from
            -- visible dialogue topics so Quest Manager can show their journal history.
            if state ~= nil then
                sendTransport(pid, transportLine("STATE", quest.id, quest.name, quest.giver.refId,
                    quest.giver.cell, state.state, state.stage))
                for _, entry in ipairs(state.journal or {}) do
                    local date = os.date("%Y-%m-%d %H:%M", tonumber(entry.time) or os.time())
                    -- X054: journal history is persisted in the authoring
                    -- language. Rather than migrating saves, the stage is looked
                    -- up again and its localized text is preferred; the stored
                    -- text remains the fallback for a stage that has since been
                    -- deleted or renumbered.
                    local stage = findStage(quest, entry.stage)
                    local text = entry.text
                    if stage ~= nil and trim(tostring(stage.journal or "")) ~= "" then
                        text = stage.journal
                    end
                    sendTransport(pid, transportLine("JOURNAL", quest.id, entry.stage, date, text,
                        tonumber(entry.vanillaAnchor) or -1, tonumber(entry.time) or 0))
                end
            end

            if state == nil or state.state == "active" then
                local dialogue = serverQuestSystem.GetCurrentDialogue(pid, id)
                if dialogue ~= nil then
                    for _, topic in ipairs(quest.topics or {}) do
                        if topic.enabled ~= false then
                            sendTransport(pid, transportLine("QUEST", quest.id, quest.giver.refId, quest.giver.cell,
                                topic.id, topic.text, dialogue.state, dialogue.stage, dialogue.text, quest.name))
                        end
                    end
                end
            end
        end
    end
    sendTransport(pid, "END")
end

function serverQuestSystem.SyncAll()
    if Players == nil then return end
    for pid, player in pairs(Players) do
        if player ~= nil and player:IsLoggedIn() then
            serverQuestSystem.SyncPlayer(pid)
        end
    end
end

-- X039: full staff-only editor projection for the real MyGUI Quest Studio.
-- Only the selected quest sends deep details; the left-hand quest list receives
-- lightweight summaries for every definition, which keeps the editor scalable.
function serverQuestSystem.SyncEditor(pid, selectedId, notice)
    if not enabled() or not isModerator(pid) or Players[pid] == nil or not Players[pid]:IsLoggedIn() then return end

    local ids = sortedQuestIds()
    selectedId = normalizeId(selectedId or (serverQuestSystem.editor[pid] and serverQuestSystem.editor[pid].questId) or "")
    if serverQuestSystem.quests[selectedId] == nil then selectedId = ids[1] or "" end
    if serverQuestSystem.editor[pid] == nil then serverQuestSystem.editor[pid] = {} end
    local state = serverQuestSystem.editor[pid]
    state.questId = selectedId

    local canPublish = isAdmin(pid) or config.serverQuests == nil or config.serverQuests.moderatorsCanPublish ~= false
    sendTransport(pid, "EDITOR_CLEAR")
    sendTransport(pid, transportLine("EDITOR_META", canPublish and 1 or 0, isAdmin(pid) and 1 or 0, selectedId, notice or ""))

    for _, id in ipairs(ids) do
        local q = serverQuestSystem.quests[id]
        sendTransport(pid, transportLine("EDITOR_QUEST", q.id, q.name, q.status, q.author, q.progressMode,
            q.version, q.giver.refId, q.giver.cell, q.giver.uniqueIndex or "", q.initialStage or 0,
            id == selectedId and (q.offer and q.offer.dialogue or "") or ""))
    end

    local q = serverQuestSystem.quests[selectedId]
    if q ~= nil then
        for _, topic in ipairs(q.topics or {}) do
            sendTransport(pid, transportLine("EDITOR_TOPIC", q.id, topic.id, topic.text, topic.enabled ~= false and 1 or 0))
        end

        local function syncRequirement(scope, stageIndex, choiceId, index, requirement)
            sendTransport(pid, transportLine("EDITOR_REQ", q.id, scope, stageIndex or 0, choiceId or "", index,
                requirement.type or "", requirement.operator or ">=", requirement.count or requirement.value or "",
                requirement.refId or "", requirement.questId or "", requirement.key or ""))
        end

        local function syncChoice(scope, stageIndex, choice)
            sendTransport(pid, transportLine("EDITOR_CHOICE", q.id, scope, stageIndex or 0, choice.id, choice.text,
                choice.action or "none", choice.targetStage ~= nil and 1 or 0, choice.targetStage or 0))
            for index, requirement in ipairs(choice.requirements or {}) do
                syncRequirement(scope == "offer" and "offerChoice" or "stageChoice", stageIndex, choice.id, index, requirement)
            end
        end

        if q.offer ~= nil then
            for _, choice in ipairs(q.offer.choices or {}) do syncChoice("offer", 0, choice) end
        end

        for _, stage in ipairs(q.stages or {}) do
            sendTransport(pid, transportLine("EDITOR_STAGE", q.id, stage.index, stage.journal, stage.dialogue,
                stage.complete and 1 or 0, stage.fail and 1 or 0))
            for _, nextStage in ipairs(stage.next or {}) do
                sendTransport(pid, transportLine("EDITOR_NEXT", q.id, stage.index, nextStage))
            end
            for index, requirement in ipairs(stage.requirements or {}) do
                syncRequirement("stage", stage.index, "", index, requirement)
            end
            for _, choice in ipairs(stage.choices or {}) do syncChoice("stage", stage.index, choice) end
            for index, reward in ipairs(stage.rewards or {}) do
                sendTransport(pid, transportLine("EDITOR_REWARD", q.id, stage.index, index, reward.type or "",
                    reward.amount or "", reward.refId or "", reward.count or "", reward.key or "",
                    reward.value or "", reward.text or ""))
            end
        end

        local validation = serverQuestSystem.validation[q.id]
        if validation == nil then
            local errors, warnings = serverQuestSystem.ValidateQuest(q)
            validation = { errors = errors, warnings = warnings }
            serverQuestSystem.validation[q.id] = validation
        end
        for _, message in ipairs(validation.errors or {}) do sendTransport(pid, transportLine("EDITOR_VALID", q.id, "E", message)) end
        for _, message in ipairs(validation.warnings or {}) do sendTransport(pid, transportLine("EDITOR_VALID", q.id, "W", message)) end
    end
    sendTransport(pid, "EDITOR_END")
end

local function normalizeQuestWireToken(value)
    if type(value) ~= "string" then return value end

    -- X043 clients running with translated Morrowind data could accidentally
    -- serialize opaque quest tokens through the vanilla "label|topicId" path,
    -- yielding token|token. Accept the first server-token component so a server
    -- update remains compatible while clients are being upgraded to X043a.
    local separator = value:find("|", 1, true)
    if separator ~= nil then
        local first = value:sub(1, separator - 1)
        local second = value:sub(separator + 1)
        local firstIsQuest = first:sub(1, #QUEST_TOPIC_PREFIX) == QUEST_TOPIC_PREFIX
            or first:sub(1, #QUEST_CHOICE_PREFIX) == QUEST_CHOICE_PREFIX
        local secondIsQuest = second:sub(1, #QUEST_TOPIC_PREFIX) == QUEST_TOPIC_PREFIX
            or second:sub(1, #QUEST_CHOICE_PREFIX) == QUEST_CHOICE_PREFIX
        if firstIsQuest and secondIsQuest then return first end
    end
    return value
end

local function parseQuestTopicToken(value)
    value = normalizeQuestWireToken(value)
    if type(value) ~= "string" or value:sub(1, #QUEST_TOPIC_PREFIX) ~= QUEST_TOPIC_PREFIX then
        return nil, nil
    end
    local body = value:sub(#QUEST_TOPIC_PREFIX + 1)
    local separator = body:find(":", 1, true)
    if separator == nil then return nil, nil end
    return normalizeId(body:sub(1, separator - 1)), normalizeId(body:sub(separator + 1))
end

local function parseQuestChoiceToken(value)
    value = normalizeQuestWireToken(value)
    if type(value) ~= "string" or value:sub(1, #QUEST_CHOICE_PREFIX) ~= QUEST_CHOICE_PREFIX then
        return nil, nil, nil
    end
    local body = value:sub(#QUEST_CHOICE_PREFIX + 1)
    local a = body:find(":", 1, true)
    if a == nil then return nil, nil, nil end
    local b = body:find(":", a + 1, true)
    if b == nil then return nil, nil, nil end
    return normalizeId(body:sub(1, a - 1)), normalizeId(body:sub(a + 1, b - 1)), normalizeId(body:sub(b + 1))
end

local function findChoice(list, choiceId)
    choiceId = normalizeId(choiceId)
    for _, choice in ipairs(list or {}) do
        if normalizeId(choice.id) == choiceId then return choice end
    end
    return nil
end

local function findEligibleNextStage(pid, quest, state)
    local current = findStage(quest, state.stage)
    if current == nil then return nil end
    for _, nextIndex in ipairs(current.next or {}) do
        local candidate = findStage(quest, nextIndex)
        if candidate ~= nil then
            local ok = serverQuestSystem.CheckStageRequirements(pid, state, candidate)
            if ok then return candidate end
        end
    end
    return nil
end

-- Sends the NPC's reply. With closeChoices set, the reply carries no answers at
-- all, which is how the player is handed back to the topic list: the dialogue
-- window shows the topics again as soon as a RESPONSE arrives with zero
-- choices (DialogueWindow::updateChoicePane hides the answer pane and unhides
-- mTopicsList when the choice list is empty).
local function sendDialogueResponse(pid, quest, topicId, closeChoices)
    local dialogue = serverQuestSystem.GetCurrentDialogue(pid, quest.id)
    if dialogue == nil then return end
    local choices = closeChoices and {} or (dialogue.choices or {})
    local fields = { quest.id, topicId, quest.giver.refId, quest.giver.cell,
        dialogue.text, dialogue.state, dialogue.stage, #choices }
    for _, choice in ipairs(choices) do
        table.insert(fields, choice.id)
        table.insert(fields, choice.text)
    end
    sendTransport(pid, transportLine("RESPONSE", unpackValues(fields)))
end

-- X048 ---------------------------------------------------------------------
-- Pending choices waiting for a fresh inventory from the client. Keyed by pid,
-- dropped after PENDING_CHOICE_TIMEOUT seconds so a stale entry can never fire
-- against an unrelated later dialogue.
serverQuestSystem.pendingChoice = serverQuestSystem.pendingChoice or {}
local PENDING_CHOICE_TIMEOUT = 20

local function notifyJournalUpdate(pid, quest, stage)
    if stage == nil then return end

    -- X054: the popup and the runtime line are read by the player, so both the
    -- quest name and the journal text come from that player's language view.
    local view = serverQuestSystem.GetLocalizedQuest(pid, quest)
    local localizedStage = findStage(view, stage.index) or stage
    local text = trim(tostring(localizedStage.journal or ""))
    if text == "" then return end

    -- The client mirrors these entries into the vanilla journal window through the
    -- ServerQuestRegistry. X050b avoids the old double notification: by default
    -- the player gets only this compact popup and no duplicate [Quest] chat line.
    local questConfig = type(config.serverQuests) == "table" and config.serverQuests or {}
    if questConfig.journalUpdatePopup ~= false then
        callIfPresent("MessageBox", pid, -1, questText(pid, "journal_updated", { name = view.name }))
    end
    sendRuntime(pid, questText(pid, "journal_line", { name = view.name, text = text }), "progress")
end

-- Applies a server-validated dialogue choice. Returns ok, why.
-- When announce is true the player is told what happened and the dialogue is
-- refreshed; the deferred retry path uses that too.
function serverQuestSystem.ApplyChoice(pid, quest, topicId, choiceId, announce)
    local questId = quest.id

    -- Resolve the current authoritative dialogue again. A client cannot submit a
    -- choice that is hidden by requirements or that belongs to an old stage.
    local dialogue = serverQuestSystem.GetCurrentDialogue(pid, questId)
    local authoritativeChoice = nil
    for _, candidate in ipairs(dialogue ~= nil and dialogue.choices or {}) do
        if normalizeId(candidate.id) == choiceId then authoritativeChoice = candidate break end
    end

    if authoritativeChoice == nil then
        log(enumerations.log.WARN, "Rejected unavailable quest choice " .. tostring(choiceId) ..
            " for " .. questId .. " from pid " .. tostring(pid))
        if announce then
            -- Re-offering the list the client just picked from would leave the
            -- player clicking the same dead answer forever.
            sendDialogueResponse(pid, quest, topicId, true)
            serverQuestSystem.SyncPlayer(pid)
        end
        return false, "unavailable choice"
    end

    local ok, why, failing = true, nil, nil
    local stageBefore = nil
    local existing = serverQuestSystem.GetPlayerState(pid, questId)
    if existing ~= nil then stageBefore = existing.stage end

    if authoritativeChoice.action == "start" then
        if existing ~= nil then
            ok, why = false, "Quest already started"
        else
            ok, why, failing = serverQuestSystem.StartQuest(pid, questId, false, true)
        end
    elseif authoritativeChoice.action == "advance" then
        ok, why, failing = serverQuestSystem.AdvanceQuest(pid, questId, authoritativeChoice.targetStage, false, true)
    elseif authoritativeChoice.action ~= "none" then
        ok, why = false, "Unsupported choice action"
    end

    -- Does the answer list stay open?
    --
    -- It used to, unconditionally, and that is what trapped the player: a
    -- declined answer changes nothing, so the very same answers were sent back
    -- and the only way out was to close the whole dialogue. A positive answer
    -- whose requirements were not met behaved the same way. An answer that did
    -- not move the quest forward must close the list and return to the topics.
    local keepChoicesOpen = false

    if ok then
        serverQuestSystem.pendingChoice[pid] = nil

        local state = serverQuestSystem.GetPlayerState(pid, questId)
        if state ~= nil and state.stage ~= stageBefore then
            notifyJournalUpdate(pid, quest, findStage(quest, state.stage))
        end

        -- Only a start/advance that genuinely moved the quest may open the next
        -- stage's own answers, which is a legitimate chained conversation. An
        -- action of "none" never moves anything, so it always closes.
        keepChoicesOpen = authoritativeChoice.action ~= "none"
            and state ~= nil and state.stage ~= stageBefore
    else
        log(enumerations.log.WARN, "Quest choice failed " .. questId .. ": " .. tostring(why)
            .. (failing ~= nil and (" [" .. requirementText(pid, failing) .. "]") or ""))

        if announce then
            if failing ~= nil then
                sendRuntime(pid, questText(pid, "requirement_failed",
                    { reason = requirementText(pid, failing) }), "error")
            elseif why ~= nil then
                sendRuntime(pid, tostring(why), "error")
            end

            -- An item/gold requirement may have failed only because our copy of the
            -- inventory is stale. Ask the client for a fresh one and replay the choice
            -- as soon as it lands, so the player does not have to click twice.
            if requirementIsInventoryBased(failing) and requestInventoryResync(pid) then
                serverQuestSystem.pendingChoice[pid] = {
                    questId = questId,
                    topicId = topicId,
                    choiceId = choiceId,
                    time = os.time()
                }
                -- This exact answer is about to be replayed against a fresh
                -- inventory, so the list stays up for the moment that takes.
                -- Every other failure closes it.
                keepChoicesOpen = true
            end
        end
    end

    if announce then
        -- Final loop guard. If the dialogue we are about to send still offers
        -- the answer that was just applied, sending it would put the player
        -- right back where they started no matter how the state moved.
        --
        -- Skipped while an inventory replay is pending: there the answers are
        -- deliberately identical, because the very same answer is about to be
        -- retried against fresh inventory data a moment from now.
        if keepChoicesOpen and ok then
            local nextDialogue = serverQuestSystem.GetCurrentDialogue(pid, questId)
            if nextDialogue == nil or findChoice(nextDialogue.choices, choiceId) ~= nil then
                keepChoicesOpen = false
            end
        end

        sendDialogueResponse(pid, quest, topicId, not keepChoicesOpen)
        serverQuestSystem.SyncPlayer(pid)
    end

    return ok, why
end

-- Replays a pending choice once the client has answered our inventory request.
local function onPlayerInventoryResync(eventStatus, pid)
    if not enabled() then return end
    if Players[pid] == nil or not Players[pid]:IsLoggedIn() then return end

    local pending = serverQuestSystem.pendingChoice[pid]
    if pending == nil then return end

    if os.time() - (tonumber(pending.time) or 0) > PENDING_CHOICE_TIMEOUT then
        serverQuestSystem.pendingChoice[pid] = nil
        return
    end

    local quest = serverQuestSystem.quests[pending.questId]
    if quest == nil then
        serverQuestSystem.pendingChoice[pid] = nil
        return
    end

    serverQuestSystem.pendingChoice[pid] = nil

    local ok = serverQuestSystem.ApplyChoice(pid, quest, pending.topicId, pending.choiceId, false)
    if ok then
        log(enumerations.log.INFO, "replayed choice " .. tostring(pending.choiceId)
            .. " for " .. tostring(pending.questId) .. " after inventory resync (pid " .. tostring(pid) .. ")")
        sendDialogueResponse(pid, quest, pending.topicId)
    else
        -- The requirement really is unmet, not just stale. Without this the
        -- client kept showing the answer list from the first attempt with no
        -- reply and no way back to the topics.
        log(enumerations.log.INFO, "replayed choice " .. tostring(pending.choiceId)
            .. " for " .. tostring(pending.questId) .. " still fails after inventory resync (pid "
            .. tostring(pid) .. ")")
        sendDialogueResponse(pid, quest, pending.topicId, true)
    end
    serverQuestSystem.SyncPlayer(pid)
end
-- End of X048 --------------------------------------------------------------

local function validateServerQuestDialogue(eventStatus, pid, cellDescription, objects)
    for _, object in pairs(objects or {}) do
        if object.dialogueChoiceType == enumerations.dialogueChoice.TOPIC then
            local questId = parseQuestTopicToken(object.dialogueTopic)
            local choiceQuestId = parseQuestChoiceToken(object.dialogueTopic)
            if questId ~= nil or choiceQuestId ~= nil then
                -- Suppress the vanilla ObjectDialogueChoice echo. Server quest topics
                -- and choices are never allowed to reach local DIAL result execution.
                return customEventHooks.makeEventStatus(false, true)
            end
        end
    end
end

local function handleServerQuestDialogue(eventStatus, pid, cellDescription, objects)
    if not enabled() or Players[pid] == nil or not Players[pid]:IsLoggedIn() then return end

    for _, object in pairs(objects or {}) do
        if object.dialogueChoiceType == enumerations.dialogueChoice.TOPIC then
            local questId, topicId = parseQuestTopicToken(object.dialogueTopic)
            local choiceQuestId, choiceTopicId, choiceId = parseQuestChoiceToken(object.dialogueTopic)
            local isChoice = choiceQuestId ~= nil
            if isChoice then
                questId, topicId = choiceQuestId, choiceTopicId
            end

            if questId ~= nil then
                local quest = serverQuestSystem.quests[questId]
                local topic = quest ~= nil and findTopic(quest, topicId) or nil
                local validation = quest ~= nil and serverQuestSystem.validation[questId] or nil
                local giverMatches = quest ~= nil and tostring(quest.giver.refId or ""):lower() == tostring(object.refId or ""):lower()
                local cellMatches = quest ~= nil and questCellMatches(quest.giver.cell, cellDescription)

                if quest == nil or quest.status ~= "published" or topic == nil or topic.enabled == false or
                    not giverMatches or not cellMatches or (validation ~= nil and #validation.errors > 0) then
                    log(enumerations.log.WARN, "Rejected server quest token " .. tostring(object.dialogueTopic) ..
                        " from pid " .. tostring(pid))
                    serverQuestSystem.SyncPlayer(pid)
                    return
                end

                if isChoice then
                    -- X048: the whole apply-a-choice path lives in one function now so
                    -- that the deferred inventory retry can replay it verbatim.
                    serverQuestSystem.ApplyChoice(pid, quest, topicId, choiceId, true)
                    return
                end

                local state = serverQuestSystem.GetPlayerState(pid, questId)
                local dialogue = serverQuestSystem.GetCurrentDialogue(pid, questId)
                if dialogue ~= nil and #(dialogue.choices or {}) > 0 then
                    -- Choice-driven definitions do not mutate state merely because a
                    -- green topic was opened. Acceptance/turn-in happens only after
                    -- an explicit server-validated answer click.
                    sendDialogueResponse(pid, quest, topicId)
                    return
                end

                -- Backward compatibility with X035/X036 linear definitions: if no
                -- choices are authored, keep the old click-to-start/auto-advance path.
                if state == nil then
                    local ok, why, failing = serverQuestSystem.StartQuest(pid, questId, false, true)
                    if not ok then
                        sendRuntime(pid, questText(pid, "quest_cannot_start", {
                            name = serverQuestSystem.GetLocalizedQuest(pid, quest).name,
                            reason = failing ~= nil and requirementText(pid, failing) or tostring(why)
                        }), "error")
                        serverQuestSystem.SyncPlayer(pid)
                        return
                    end
                    notifyJournalUpdate(pid, quest, findStage(quest, quest.initialStage))
                elseif state.state == "active" then
                    local target = findEligibleNextStage(pid, quest, state)
                    if target ~= nil then
                        local ok, why = serverQuestSystem.AdvanceQuest(pid, questId, target.index, false, true)
                        if not ok then
                            log(enumerations.log.WARN, "Failed server quest transition " .. questId .. ": " .. tostring(why))
                        else
                            notifyJournalUpdate(pid, quest, target)
                        end
                    end
                end

                sendDialogueResponse(pid, quest, topicId)
                serverQuestSystem.SyncPlayer(pid)
                return
            end
        end
    end
end

local function newQuest(pid, raw)
    local parts = splitPipe(raw)
    local id = normalizeId(parts[1])
    local name = trim(parts[2])
    if not validId(id) then return false, "Use id like arena_missing_ring" end
    if name == "" then return false, "Quest name is required" end
    if serverQuestSystem.quests[id] ~= nil then return false, "Quest already exists" end
    local quest = normalizeQuest({
        id = id,
        name = name,
        author = getAccountName(pid),
        status = "draft",
        progressMode = "personal",
        version = 1,
        giver = { refId = "", cell = "" },
        topics = {},
        stages = {},
        audit = {}
    })
    appendAudit(quest, getAccountName(pid), "created")
    local ok, err = serverQuestSystem.SaveQuest(quest)
    if ok then serverQuestSystem.editor[pid] = { questId = id } end
    return ok, err or quest
end

local function getCurrentQuest(pid)
    local state = serverQuestSystem.editor[pid]
    if state == nil then return nil end
    return serverQuestSystem.quests[state.questId]
end

local function editorState(pid)
    serverQuestSystem.editor[pid] = serverQuestSystem.editor[pid] or {}
    return serverQuestSystem.editor[pid]
end

local function getSortedStages(quest)
    local stages = {}
    for _, stage in ipairs(quest.stages or {}) do table.insert(stages, stage) end
    table.sort(stages, function(a, b) return (tonumber(a.index) or 0) < (tonumber(b.index) or 0) end)
    return stages
end

local function getEditorStage(pid, quest)
    local state = editorState(pid)
    return findStage(quest, state.stageIndex)
end

local function getChoiceListForEditor(pid, quest)
    local state = editorState(pid)
    if state.choiceSource == "offer" then
        quest.offer = quest.offer or { dialogue = "", choices = {} }
        quest.offer.choices = quest.offer.choices or {}
        return quest.offer.choices, "offer"
    end
    local stage = findStage(quest, state.choiceSource)
    if stage == nil then return nil, nil end
    stage.choices = stage.choices or {}
    return stage.choices, tostring(stage.index)
end

local function getEditorChoice(pid, quest)
    local choices = getChoiceListForEditor(pid, quest)
    local state = editorState(pid)
    if choices == nil or state.choiceIndex == nil then return nil end
    return choices[tonumber(state.choiceIndex)]
end

local function formatRequirement(requirement)
    if type(requirement) ~= "table" then return "<invalid requirement>" end
    if requirement.type == "item" then
        return string.format("item %s %s %s", tostring(requirement.refId), tostring(requirement.operator), tostring(requirement.count or requirement.value))
    elseif requirement.type == "questStage" or requirement.type == "questState" then
        return string.format("%s %s %s %s", tostring(requirement.type), tostring(requirement.questId), tostring(requirement.operator), tostring(requirement.value))
    elseif requirement.type == "playerVariable" or requirement.type == "serverVariable" then
        return string.format("%s %s %s %s", tostring(requirement.type), tostring(requirement.key), tostring(requirement.operator), tostring(requirement.value))
    end
    return string.format("%s %s %s", tostring(requirement.type), tostring(requirement.operator), tostring(requirement.value))
end

local function formatReward(reward)
    if type(reward) ~= "table" then return "<invalid reward>" end
    if reward.type == "item" or reward.type == "takeItem" then
        return string.format("%s %s x%s", tostring(reward.type), tostring(reward.refId), tostring(reward.count or 1))
    elseif reward.type == "message" then
        return "message: " .. tostring(reward.text or "")
    end
    return string.format("%s %s", tostring(reward.type), tostring(reward.amount or ""))
end

local function formatChoice(choice)
    local target = choice.targetStage ~= nil and (" -> " .. tostring(choice.targetStage)) or ""
    return string.format("[%s%s] %s", tostring(choice.action or "none"), target, tostring(choice.text or choice.id or ""))
end

local function showGiverEditor(pid, quest)
    local text = "Quest giver\n\nRefId: " .. (quest.giver.refId ~= "" and quest.giver.refId or "<not set>") ..
        "\nCell: " .. (quest.giver.cell ~= "" and quest.giver.cell or "<any>") ..
        "\nUnique index: " .. tostring(quest.giver.uniqueIndex or "<not pinned>") ..
        "\n\nPick in game captures the next object/NPC you activate."
    tes3mp.CustomMessageBox(pid, config.customMenuIds.questEditorGiver, text, "Manual;Pick in game;Clear uniqueIndex;Back")
end

local function showStageFlags(pid, quest)
    local stage = getEditorStage(pid, quest)
    if stage == nil then send(pid, "No stage selected") return end
    local text = "Stage flags #" .. tostring(stage.index) ..
        "\nInitial stage: " .. tostring(tonumber(quest.initialStage) == tonumber(stage.index)) ..
        "\nComplete: " .. tostring(stage.complete == true) ..
        "\nFail: " .. tostring(stage.fail == true)
    tes3mp.CustomMessageBox(pid, config.customMenuIds.questEditorStageFlags, text, "Set initial;Toggle complete;Toggle fail;Back")
end

local function showQuestDetails(pid, quest)
    if quest == nil then send(pid, "No quest selected") return end
    local errors, warnings = serverQuestSystem.ValidateQuest(quest)
    local label = quest.name .. "\n" .. quest.id .. "\n\nStatus: " .. quest.status .. "  v" .. quest.version ..
        "\nGiver: " .. (quest.giver.refId ~= "" and quest.giver.refId or "<not set>") ..
        (quest.giver.cell ~= "" and (" @ " .. quest.giver.cell) or "") ..
        (quest.giver.uniqueIndex ~= nil and (" [" .. tostring(quest.giver.uniqueIndex) .. "]") or "") ..
        "\nTopics: " .. #quest.topics .. "   Stages: " .. #quest.stages ..
        "\nValidation: " .. #errors .. " error(s), " .. #warnings .. " warning(s)"
    local buttons = "Name;Giver;Topics;Offer;Stages;Validate;" ..
        (quest.status == "published" and "Disable" or "Publish") .. ";Test me;Back"
    tes3mp.CustomMessageBox(pid, config.customMenuIds.questEditorDetail, label, buttons)
end

local function showTopicsMenu(pid, quest)
    local lines = { "Green dialogue topics", "Count: " .. tostring(#(quest.topics or {})) }
    for index, topic in ipairs(quest.topics or {}) do
        table.insert(lines, string.format("%d. %s%s", index, topic.enabled == false and "[off] " or "", topic.text or topic.id))
        if index >= 8 and #quest.topics > 8 then table.insert(lines, "..."); break end
    end
    tes3mp.CustomMessageBox(pid, config.customMenuIds.questEditorTopics, table.concat(lines, "\n"), "Add;Select/Edit;Back")
end

local function showTopicList(pid, quest)
    local state = editorState(pid)
    state.topicList = {}
    local lines = {}
    for index, topic in ipairs(quest.topics or {}) do
        state.topicList[#state.topicList + 1] = index
        lines[#lines + 1] = string.format("%s%s (%s)", topic.enabled == false and "[off] " or "", topic.text or "", topic.id or "")
    end
    if #lines == 0 then send(pid, "Quest has no topics") showTopicsMenu(pid, quest) return end
    tes3mp.ListBox(pid, config.customMenuIds.questEditorTopicList, "Select green topic", table.concat(lines, "\n"))
end

local function showTopicDetail(pid, quest)
    local state = editorState(pid)
    local topic = quest.topics and quest.topics[tonumber(state.topicIndex or -1)] or nil
    if topic == nil then showTopicsMenu(pid, quest) return end
    local text = string.format("%s\nID: %s\nEnabled: %s\nColor: green", tostring(topic.text), tostring(topic.id), tostring(topic.enabled ~= false))
    tes3mp.CustomMessageBox(pid, config.customMenuIds.questEditorTopicDetail, text, "Edit text;Toggle enabled;Delete;Back")
end

local function showOfferEditor(pid, quest)
    quest.offer = quest.offer or { dialogue = "", choices = {} }
    local text = "Quest offer\n\nNPC text:\n" .. (quest.offer.dialogue ~= "" and quest.offer.dialogue or "<not set>") ..
        "\n\nChoices: " .. tostring(#(quest.offer.choices or {}))
    tes3mp.CustomMessageBox(pid, config.customMenuIds.questEditorOffer, text, "Edit dialogue;Choices;Back")
end

local function showChoicesMenu(pid, quest)
    local choices, source = getChoiceListForEditor(pid, quest)
    if choices == nil then showQuestDetails(pid, quest) return end
    local text = "Choices for " .. tostring(source) .. "\nCount: " .. tostring(#choices) ..
        "\n\nOnly choices whose requirements pass are sent to the client."
    tes3mp.CustomMessageBox(pid, config.customMenuIds.questEditorChoices, text, "Add;Select/Edit;Back")
end

local function showChoiceList(pid, quest)
    local choices = getChoiceListForEditor(pid, quest)
    if choices == nil or #choices == 0 then send(pid, "No choices yet") showChoicesMenu(pid, quest) return end
    local lines = {}
    for index, choice in ipairs(choices) do lines[index] = formatChoice(choice) end
    tes3mp.ListBox(pid, config.customMenuIds.questEditorChoiceList, "Select choice", table.concat(lines, "\n"))
end

local function showChoiceDetail(pid, quest)
    local choice = getEditorChoice(pid, quest)
    if choice == nil then showChoicesMenu(pid, quest) return end
    local text = string.format("%s\n\nID: %s\nAction: %s\nTarget stage: %s\nRequirements: %d",
        tostring(choice.text), tostring(choice.id), tostring(choice.action), tostring(choice.targetStage or "-"), #(choice.requirements or {}))
    tes3mp.CustomMessageBox(pid, config.customMenuIds.questEditorChoiceDetail, text, "Edit;Requirements;Delete;Back")
end

local function showStagesMenu(pid, quest)
    local stages = getSortedStages(quest)
    local text = "Quest stages\nCount: " .. tostring(#stages) .. "\nInitial stage: " .. tostring(quest.initialStage or "<not set>")
    tes3mp.CustomMessageBox(pid, config.customMenuIds.questEditorStages, text, "Add;Select/Edit;Back")
end

local function showStageList(pid, quest)
    local state = editorState(pid)
    state.stageList = {}
    local lines = {}
    for _, stage in ipairs(getSortedStages(quest)) do
        state.stageList[#state.stageList + 1] = tonumber(stage.index)
        local flags = stage.complete and " [complete]" or (stage.fail and " [failed]" or "")
        lines[#lines + 1] = "#" .. tostring(stage.index) .. flags .. " - " .. tostring(stage.journal or "")
    end
    if #lines == 0 then send(pid, "Quest has no stages") showStagesMenu(pid, quest) return end
    tes3mp.ListBox(pid, config.customMenuIds.questEditorStageList, "Select quest stage", table.concat(lines, "\n"))
end

local function showStageDetail(pid, quest)
    local stage = getEditorStage(pid, quest)
    if stage == nil then showStagesMenu(pid, quest) return end
    local text = "Stage #" .. tostring(stage.index) ..
        "\n\nJournal:\n" .. tostring(stage.journal or "") ..
        "\n\nNPC dialogue:\n" .. tostring(stage.dialogue or "") ..
        "\n\nRequirements: " .. tostring(#(stage.requirements or {})) ..
        "  Choices: " .. tostring(#(stage.choices or {})) ..
        "\nRewards: " .. tostring(#(stage.rewards or {})) .. "  Next: " .. tostring(#(stage.next or {})) ..
        "\nComplete: " .. tostring(stage.complete == true) .. "  Fail: " .. tostring(stage.fail == true)
    tes3mp.CustomMessageBox(pid, config.customMenuIds.questEditorStageDetail, text,
        "Journal;Dialogue;Requirements;Choices;Rewards;Transitions;Flags;Delete;Back")
end

local function getRequirementListForEditor(pid, quest)
    local state = editorState(pid)
    if state.requirementOwner == "choice" then
        local choice = getEditorChoice(pid, quest)
        if choice == nil then return nil end
        choice.requirements = choice.requirements or {}
        return choice.requirements
    end
    local stage = getEditorStage(pid, quest)
    if stage == nil then return nil end
    stage.requirements = stage.requirements or {}
    return stage.requirements
end

local function showRequirementsMenu(pid, quest)
    local requirements = getRequirementListForEditor(pid, quest)
    if requirements == nil then showQuestDetails(pid, quest) return end
    local text = "Requirements\nCount: " .. tostring(#requirements)
    for i, requirement in ipairs(requirements) do
        if i <= 6 then text = text .. "\n" .. tostring(i) .. ". " .. formatRequirement(requirement) end
    end
    tes3mp.CustomMessageBox(pid, config.customMenuIds.questEditorRequirements, text, "Add;Delete selected;Back")
end

local function showRequirementList(pid, quest)
    local requirements = getRequirementListForEditor(pid, quest)
    if requirements == nil or #requirements == 0 then send(pid, "No requirements") showRequirementsMenu(pid, quest) return end
    local lines = {}
    for i, requirement in ipairs(requirements) do lines[i] = formatRequirement(requirement) end
    tes3mp.ListBox(pid, config.customMenuIds.questEditorRequirementList, "Select requirement to delete", table.concat(lines, "\n"))
end

local function showRewardsMenu(pid, quest)
    local stage = getEditorStage(pid, quest)
    if stage == nil then showStagesMenu(pid, quest) return end
    local text = "Rewards / actions for stage #" .. tostring(stage.index) .. "\nCount: " .. tostring(#(stage.rewards or {}))
    for i, reward in ipairs(stage.rewards or {}) do if i <= 6 then text = text .. "\n" .. tostring(i) .. ". " .. formatReward(reward) end end
    tes3mp.CustomMessageBox(pid, config.customMenuIds.questEditorRewards, text, "Add;Delete selected;Back")
end

local function showRewardList(pid, quest)
    local stage = getEditorStage(pid, quest)
    if stage == nil or #(stage.rewards or {}) == 0 then send(pid, "No rewards") showRewardsMenu(pid, quest) return end
    local lines = {}
    for i, reward in ipairs(stage.rewards) do lines[i] = formatReward(reward) end
    tes3mp.ListBox(pid, config.customMenuIds.questEditorRewardList, "Select reward to delete", table.concat(lines, "\n"))
end

local function showTransitionsMenu(pid, quest)
    local stage = getEditorStage(pid, quest)
    if stage == nil then showStagesMenu(pid, quest) return end
    local nextList = stage.next or {}
    local text = "Transitions from stage #" .. tostring(stage.index) .. "\n"
    if #nextList == 0 then
        text = text .. "<none>"
    else
        local labels = {}
        for _, target in ipairs(nextList) do labels[#labels + 1] = tostring(target) end
        text = text .. table.concat(labels, ", ")
    end
    tes3mp.CustomMessageBox(pid, config.customMenuIds.questEditorTransitions, text, "Add;Delete selected;Back")
end

local function showTransitionList(pid, quest)
    local stage = getEditorStage(pid, quest)
    if stage == nil or #(stage.next or {}) == 0 then send(pid, "No transitions") showTransitionsMenu(pid, quest) return end
    local lines = {}
    for i, target in ipairs(stage.next) do lines[i] = "Stage #" .. tostring(target) end
    tes3mp.ListBox(pid, config.customMenuIds.questEditorTransitionList, "Select transition to delete", table.concat(lines, "\n"))
end


local function showQuestList(pid)
    local ids = sortedQuestIds()
    if #ids == 0 then send(pid, "No server quests yet") return end
    local lines = {}
    for _, id in ipairs(ids) do
        local q = serverQuestSystem.quests[id]
        table.insert(lines, string.format("[%s] %s (%s)", q.status, q.name, id))
    end
    serverQuestSystem.editor[pid] = serverQuestSystem.editor[pid] or {}
    serverQuestSystem.editor[pid].list = ids
    tes3mp.ListBox(pid, config.customMenuIds.questEditorList, "ArenaMP Server Quests", table.concat(lines, "\n"))
end

local function showPlayerQuests(pid)
    local cv = ensurePlayerData(pid)
    local lines = {}
    for id, state in pairs(cv.serverQuests) do
        local q = serverQuestSystem.quests[id]
        local name = q ~= nil and q.name or id
        table.insert(lines, string.format("[%s] %s - stage %s", state.state or "active", name, tostring(state.stage)))
    end
    table.sort(lines)
    if #lines == 0 then send(pid, "You have no server quests") return end
    tes3mp.ListBox(pid, config.customMenuIds.questPlayerList, "Server quests", table.concat(lines, "\n"))
end

local function showJournal(pid, questId)
    local cv = ensurePlayerData(pid)
    local chunks = {}
    local function appendState(id, state)
        local q = serverQuestSystem.quests[id]
        table.insert(chunks, (q ~= nil and q.name or id) .. " [" .. tostring(state.state) .. "]")
        for _, entry in ipairs(state.journal or {}) do
            table.insert(chunks, "  #" .. tostring(entry.stage) .. "  " .. tostring(entry.text))
        end
    end
    if questId ~= nil and questId ~= "" then
        local state = cv.serverQuests[normalizeId(questId)]
        if state ~= nil then appendState(normalizeId(questId), state) end
    else
        for id, state in pairs(cv.serverQuests) do appendState(id, state) end
    end
    if #chunks == 0 then table.insert(chunks, "No server quest journal entries yet.") end
    tes3mp.CustomMessageBox(pid, config.customMenuIds.questPlayerJournal, table.concat(chunks, "\n"), "Ok")
end

local function showMain(pid)
    if isModerator(pid) then
        tes3mp.CustomMessageBox(pid, config.customMenuIds.questEditorMain,
            "ArenaMP Server Quest Editor\nDraft -> Validate -> Publish\nDefinitions are server-authoritative.",
            "Quests;New quest;My quests;Journal;Help;Close")
    else
        tes3mp.CustomMessageBox(pid, config.customMenuIds.questEditorMain,
            "ArenaMP Server Quests", "My quests;Journal;Close")
    end
end

local HELP_TEXT = [[ArenaMP X039 MyGUI Server Quest Studio

Moderator/Admin editor:
/quest | /quest studio    - open the real MyGUI Quest Studio (staffRank >= 1)
/quests resync             - force a client topic/journal resync
/quests caius              - show built-in Caius test quest state and resync
/quest legacy             - open the old message-box editor (fallback)
/quest list               - list definitions
/quest new ID Name        - create draft
/quest giver ID REFID [CELL]
/quest topic ID TOPIC_ID TEXT
/quest stage ID INDEX JOURNAL_TEXT
/quest offer ID DIALOGUE_TEXT
/quest choice ID offer|STAGE CHOICE_ID start|advance|none TARGET|- TEXT
/quest dialogue ID INDEX TEXT
/quest next ID FROM TO
/quest require ID STAGE level >= 5
/quest require ID STAGE item ring_ref >= 1
/quest require ID STAGE questStage other_quest >= 20
/quest reward ID STAGE gold AMOUNT
/quest reward ID STAGE xp AMOUNT
/quest reward ID STAGE item REFID COUNT
/quest reward ID STAGE takeItem REFID COUNT
/quest complete ID STAGE true|false
/quest validate ID
/quest publish ID
/quest disable ID
/quest start ID [PID]
/quest advance ID STAGE [PID]
/quest reset ID [PID]
/quest rewardresolve ID [PID] skip|retry  (admin)
/quest reload

Player:
/quest my
/quest journal [ID]

Only moderator/admin accounts can create or edit definitions.]]

local function questUsesAdminOnlyRewards(quest)
    for _, stage in ipairs((quest and quest.stages) or {}) do
        for _, reward in ipairs(stage.rewards or {}) do
            if ADMIN_ONLY_REWARD_TYPES[tostring(reward.type or "")] then return true end
        end
    end
    return false
end

local function prepareQuestForPublish(pid, quest)
    if questUsesAdminOnlyRewards(quest) then
        if not isAdmin(pid) then
            quest.adminApprovedDangerousRewards = nil
            return false, "Administrator approval is required for setServerVariable/setVanillaJournal rewards"
        end
        quest.adminApprovedDangerousRewards = true
    else
        quest.adminApprovedDangerousRewards = nil
    end
    return true
end

local function parseValue(value)
    if value == nil then return nil end
    if tostring(value) == "true" then return true end
    if tostring(value) == "false" then return false end
    if tonumber(value) ~= nil then return tonumber(value) end
    return value
end

local function getEditable(pid, id)
    if not isModerator(pid) then return nil, "Moderator or administrator rank required" end
    local q = serverQuestSystem.quests[normalizeId(id)]
    if q == nil then return nil, "Unknown quest" end
    return q
end

local function markEdited(pid, q, action)
    q.version = (tonumber(q.version) or 1) + 1
    if q.status == "published" then q.status = "draft" end
    -- Any edit invalidates a previous administrator approval of dangerous
    -- rewards. The modified definition must be reviewed again.
    q.adminApprovedDangerousRewards = nil
    return serverQuestSystem.SaveQuest(q, getAccountName(pid), action)
end

local function processCommand(pid, cmd)
    local sub = tostring(cmd[2] or ""):lower()
    if sub == "" or sub == "studio" or sub == "editor" then
        if isModerator(pid) then
            local rank = tonumber(Players[pid].data.settings.staffRank) or 0
            log(enumerations.log.INFO, "Opening MyGUI Quest Studio for pid " .. tostring(pid) ..
                " (staffRank=" .. tostring(rank) .. ")")
            send(pid, "Opening Quest Studio...")
            serverQuestSystem.SyncEditor(pid)
        else
            local rank = Players[pid] ~= nil and Players[pid].data ~= nil and Players[pid].data.settings ~= nil
                and tonumber(Players[pid].data.settings.staffRank) or 0
            send(pid, "Quest Studio requires Moderator/Admin (staffRank >= 1). Your staffRank=" ..
                tostring(rank) .. ". Server owner can use /addmoderator " .. tostring(pid) ..
                ". Use /quests to view your player quests.")
        end
        return
    end
    if sub == "legacy" and isModerator(pid) then showMain(pid) return end
    if sub == "help" then tes3mp.CustomMessageBox(pid, config.customMenuIds.questPlayerJournal, HELP_TEXT, "Ok") return end
    if sub == "my" then showPlayerQuests(pid) return end
    if sub == "journal" then showJournal(pid, cmd[3]) return end

    if not isModerator(pid) then send(pid, "Only moderators and administrators can edit quests") return end

    if sub == "list" then showQuestList(pid) return end
    if sub == "reload" then
        serverQuestSystem.LoadAll()
        send(pid, "Quest definitions reloaded from disk")
        return
    end
    if sub == "new" then
        local id = normalizeId(cmd[3])
        local name = tableHelper.concatenateFromIndex(cmd, 4)
        local ok, err = newQuest(pid, id .. " | " .. name)
        send(pid, ok and ("Created draft " .. id) or tostring(err))
        return
    end

    local q, err = getEditable(pid, cmd[3])
    if q == nil then send(pid, err) return end

    if sub == "giver" then
        q.giver.refId = trim(cmd[4])
        q.giver.cell = cmd[5] ~= nil and tableHelper.concatenateFromIndex(cmd, 5) or ""
        markEdited(pid, q, "giver changed")
        send(pid, "Giver updated")
    elseif sub == "topic" then
        local topicId = normalizeId(cmd[4])
        local text = tableHelper.concatenateFromIndex(cmd, 5)
        if not validId(topicId) or trim(text) == "" then send(pid, "Usage: /quest topic ID TOPIC_ID TEXT") return end
        local topic = findTopic(q, topicId)
        if topic == nil then
            if #q.topics >= MAX_TOPICS then send(pid, "Topic limit reached") return end
            topic = { id = topicId, text = text, enabled = true, green = true }
            table.insert(q.topics, topic)
        else topic.text = text end
        markEdited(pid, q, "topic " .. topicId .. " changed")
        send(pid, "Green topic saved: " .. text)
    elseif sub == "offer" then
        q.offer = q.offer or { dialogue = "", choices = {} }
        q.offer.dialogue = tableHelper.concatenateFromIndex(cmd, 4)
        markEdited(pid, q, "offer dialogue changed")
        send(pid, "Quest offer saved")
    elseif sub == "choice" then
        local sourceKey = tostring(cmd[4] or "")
        local choiceId = normalizeId(cmd[5])
        local action = tostring(cmd[6] or "none"):lower()
        local targetRaw = tostring(cmd[7] or "-")
        local text = tableHelper.concatenateFromIndex(cmd, 8)
        local list = nil
        if sourceKey:lower() == "offer" then
            q.offer = q.offer or { dialogue = "", choices = {} }
            list = q.offer.choices
        else
            local stage = findStage(q, tonumber(sourceKey))
            if stage ~= nil then stage.choices = stage.choices or {}; list = stage.choices end
        end
        if list == nil or not validId(choiceId) or text == "" then
            send(pid, "Usage: /quest choice ID offer|STAGE CHOICE_ID start|advance|none TARGET|- TEXT")
            return
        end
        local target = targetRaw ~= "-" and tonumber(targetRaw) or nil
        table.insert(list, normalizeChoice({ id = choiceId, text = text, action = action, targetStage = target }))
        markEdited(pid, q, "choice " .. choiceId .. " added")
        send(pid, "Choice " .. choiceId .. " saved")
    elseif sub == "stage" then
        local stageIndex = tonumber(cmd[4])
        local journalText = tableHelper.concatenateFromIndex(cmd, 5)
        if stageIndex == nil or trim(journalText) == "" then send(pid, "Usage: /quest stage ID INDEX JOURNAL_TEXT") return end
        local stage = findStage(q, stageIndex)
        if stage == nil then
            if #q.stages >= MAX_STAGES then send(pid, "Stage limit reached") return end
            stage = { index = stageIndex, journal = journalText, dialogue = "", requirements = {}, rewards = {}, next = {}, complete = false, fail = false }
            table.insert(q.stages, stage)
            if #q.stages == 1 then q.initialStage = stageIndex end
        else stage.journal = journalText end
        normalizeQuest(q)
        markEdited(pid, q, "stage " .. stageIndex .. " changed")
        send(pid, "Stage " .. stageIndex .. " saved")
    elseif sub == "dialogue" then
        local stage = findStage(q, cmd[4])
        if stage == nil then send(pid, "Stage not found") return end
        stage.dialogue = tableHelper.concatenateFromIndex(cmd, 5)
        markEdited(pid, q, "dialogue changed at stage " .. stage.index)
        send(pid, "Dialogue saved")
    elseif sub == "next" then
        local stage = findStage(q, cmd[4])
        local target = tonumber(cmd[5])
        if stage == nil or findStage(q, target) == nil then send(pid, "FROM or TO stage not found") return end
        stage.next = stage.next or {}
        if not tableHelper.containsValue(stage.next, target) then table.insert(stage.next, target) end
        markEdited(pid, q, "transition " .. stage.index .. " -> " .. target)
        send(pid, "Transition added")
    elseif sub == "require" then
        local stage = findStage(q, cmd[4])
        if stage == nil then send(pid, "Stage not found") return end
        local kind = tostring(cmd[5] or "")
        local requirement
        if kind == "level" or kind == "gold" or kind == "staffRank"
                or kind == "reputation" or kind == "bounty" or kind == "realTime" then
            requirement = { type = kind, operator = cmd[6], value = parseValue(cmd[7]) }
        elseif kind == "item" then
            requirement = { type = kind, refId = cmd[6], operator = cmd[7], count = tonumber(cmd[8]), value = tonumber(cmd[8]) }
        elseif kind == "questStage" or kind == "questState" or kind == "cooldown" then
            requirement = { type = kind, questId = normalizeId(cmd[6]), operator = cmd[7], value = parseValue(cmd[8]) }
        elseif kind == "questCompleted" or kind == "questNotStarted" then
            requirement = { type = kind, questId = normalizeId(cmd[6]) }
        elseif kind == "playerVariable" or kind == "serverVariable" or kind == "global"
                or kind == "skill" or kind == "attribute" or kind == "faction" or kind == "factionRank" then
            requirement = { type = kind, key = cmd[6], operator = cmd[7], value = parseValue(cmd[8]) }
        elseif kind == "vanillaJournal" then
            requirement = { type = kind, questId = cmd[6], operator = cmd[7], value = parseValue(cmd[8]) }
        elseif kind == "race" or kind == "class" or kind == "cell" then
            requirement = { type = kind, operator = "==", value = tableHelper.concatenateFromIndex(cmd, 6) }
        else
            send(pid, "Unknown requirement type; use Quest Studio or /quest help") return
        end
        if requirement.type ~= "questCompleted" and requirement.type ~= "questNotStarted"
                and (requirement.operator == nil or requirement.value == nil) then
            send(pid, "Invalid requirement. Use /quest help for examples") return
        end
        table.insert(stage.requirements, requirement)
        markEdited(pid, q, "requirement added at stage " .. stage.index)
        send(pid, "Requirement added")
    elseif sub == "reward" then
        local stage = findStage(q, cmd[4])
        if stage == nil then send(pid, "Stage not found") return end
        local kind = tostring(cmd[5] or "")
        local reward
        if kind == "gold" or kind == "xp" then
            reward = { type = kind, amount = tonumber(cmd[6]) }
        elseif kind == "item" or kind == "giveItem" or kind == "takeItem" then
            reward = { type = kind, refId = cmd[6], count = tonumber(cmd[7]) or 1 }
        elseif kind == "message" or kind == "messageBox" then
            reward = { type = kind, text = tableHelper.concatenateFromIndex(cmd, 6) }
        elseif kind == "setPlayerVariable" or kind == "setServerVariable" then
            reward = { type = kind, key = cmd[6], value = parseValue(cmd[7]) }
        elseif kind == "addSpell" or kind == "removeSpell" or kind == "playSound" then
            reward = { type = kind, refId = cmd[6] }
        elseif kind == "setReputation" or kind == "setBounty" then
            reward = { type = kind, value = tonumber(cmd[6]) }
        elseif kind == "teleport" then
            reward = { type = kind, cell = tableHelper.concatenateFromIndex(cmd, 6) }
        elseif kind == "setVanillaJournal" then
            reward = { type = kind, questId = cmd[6], index = tonumber(cmd[7]) }
        else
            send(pid, "Unknown reward type; use Quest Studio or /quest help") return
        end
        table.insert(stage.rewards, reward)
        markEdited(pid, q, "reward added at stage " .. stage.index)
        send(pid, "Reward added")
    elseif sub == "complete" then
        local stage = findStage(q, cmd[4])
        if stage == nil then send(pid, "Stage not found") return end
        stage.complete = tostring(cmd[5]):lower() == "true" or tostring(cmd[5]) == "1"
        markEdited(pid, q, "complete flag changed at stage " .. stage.index)
        send(pid, "Complete flag = " .. tostring(stage.complete))
    elseif sub == "validate" then
        local errors, warnings = serverQuestSystem.ValidateQuest(q)
        local lines = { "Validation for " .. q.id, "Errors: " .. #errors, "Warnings: " .. #warnings }
        for _, e in ipairs(errors) do table.insert(lines, "ERROR: " .. e) end
        for _, w in ipairs(warnings) do table.insert(lines, "WARN: " .. w) end
        tes3mp.CustomMessageBox(pid, config.customMenuIds.questPlayerJournal, table.concat(lines, "\n"), "Ok")
    elseif sub == "publish" then
        local approvalOk, approvalWhy = prepareQuestForPublish(pid, q)
        if not approvalOk then send(pid, approvalWhy) return end
        local errors, warnings = serverQuestSystem.ValidateQuest(q)
        if #errors > 0 then send(pid, "Cannot publish: " .. #errors .. " validation error(s)") return end
        if not isAdmin(pid) and config.serverQuests ~= nil and config.serverQuests.moderatorsCanPublish == false then
            send(pid, "Administrator approval is required to publish") return
        end
        q.status = "published"
        q.version = q.version + 1
        serverQuestSystem.SaveQuest(q, getAccountName(pid), "published")
        send(pid, "Published " .. q.name .. (#warnings > 0 and (" with " .. #warnings .. " warning(s)") or ""))
    elseif sub == "disable" then
        q.status = "disabled"
        q.version = q.version + 1
        serverQuestSystem.SaveQuest(q, getAccountName(pid), "disabled")
        send(pid, "Disabled " .. q.name)
    elseif sub == "delete" then
        if not isAdmin(pid) then send(pid, "Only an administrator can delete quest definitions") return end
        q.status = "disabled"
        q.deleted = true
        q.version = q.version + 1
        serverQuestSystem.SaveQuest(q, getAccountName(pid), "soft-deleted")
        send(pid, "Quest soft-deleted (file retained for audit)")
    elseif sub == "start" then
        local targetPid = tonumber(cmd[4]) or pid
        if Players[targetPid] == nil then send(pid, "Target player is not online") return end
        local ok, why = serverQuestSystem.StartQuest(targetPid, q.id, true)
        send(pid, ok and "Test quest started" or tostring(why))
    elseif sub == "advance" then
        local targetStage = tonumber(cmd[4])
        local targetPid = tonumber(cmd[5]) or pid
        if targetStage == nil or Players[targetPid] == nil then send(pid, "Usage: /quest advance ID STAGE [PID]") return end
        local ok, why = serverQuestSystem.AdvanceQuest(targetPid, q.id, targetStage, true)
        send(pid, ok and "Quest advanced" or tostring(why))
    elseif sub == "reset" then
        local targetPid = tonumber(cmd[4]) or pid
        if Players[targetPid] == nil then send(pid, "Target player is not online") return end
        if targetPid ~= pid and not isAdmin(pid) then send(pid, "Only an administrator can reset another player") return end
        local cv = ensurePlayerData(targetPid)
        cv.serverQuests[q.id] = nil
        Players[targetPid]:QuicksaveToDrive()
        serverQuestSystem.SyncPlayer(targetPid)
        send(pid, "Quest state reset for " .. logicHandler.GetChatName(targetPid))
    elseif sub == "rewardresolve" then
        if not isAdmin(pid) then send(pid, "Only an administrator can resolve interrupted rewards") return end
        local targetPid = tonumber(cmd[4]) or pid
        local mode = tostring(cmd[5] or "skip"):lower()
        if Players[targetPid] == nil then send(pid, "Target player is not online") return end
        local state = serverQuestSystem.GetPlayerState(targetPid, q.id)
        if state == nil or state.pendingRewardStage == nil then send(pid, "No pending reward transaction") return end
        local pending = tonumber(state.pendingRewardStage)
        if mode == "retry" then
            -- Explicit admin action: retry can duplicate a partially-applied reward,
            -- so it is never performed automatically.
            state.pendingRewardStage = nil
            Players[targetPid]:QuicksaveToDrive()
            local stage = findStage(q, pending)
            local ok, why = applyStageRewardsAtMostOnce(targetPid, q, state, stage)
            if ok then Players[targetPid]:QuicksaveToDrive() end
            send(pid, ok and "Pending reward retried" or tostring(why))
        else
            state.rewardedStages = state.rewardedStages or {}
            state.rewardedStages[tostring(pending)] = true
            state.pendingRewardStage = nil
            state.rewardError = "resolved by admin as skipped"
            Players[targetPid]:QuicksaveToDrive()
            send(pid, "Pending reward marked resolved without retry")
        end
    else
        send(pid, "Unknown subcommand. Use /quest help")
    end
end

local function parseRequirementParts(parts)
    local kind = tostring(parts[1] or "")
    if kind == "level" or kind == "gold" or kind == "staffRank" then
        return { type = kind, operator = parts[2], value = parseValue(parts[3]) }
    elseif kind == "item" then
        return { type = kind, refId = parts[2], operator = parts[3], count = tonumber(parts[4]), value = tonumber(parts[4]) }
    elseif kind == "questStage" or kind == "questState" then
        return { type = kind, questId = normalizeId(parts[2]), operator = parts[3], value = parseValue(parts[4]) }
    elseif kind == "playerVariable" or kind == "serverVariable" then
        return { type = kind, key = parts[2], operator = parts[3], value = parseValue(parts[4]) }
    end
    return nil
end

local function parseRewardParts(parts)
    local kind = tostring(parts[1] or "")
    if kind == "gold" or kind == "xp" then return { type = kind, amount = tonumber(parts[2]) }
    elseif kind == "item" or kind == "takeItem" then return { type = kind, refId = parts[2], count = tonumber(parts[3]) or 1 }
    elseif kind == "message" then return { type = kind, text = parts[2] or "" } end
    return nil
end

local function returnFromRequirements(pid, quest)
    local state = editorState(pid)
    if state.requirementOwner == "choice" then showChoiceDetail(pid, quest) else showStageDetail(pid, quest) end
end

local function returnFromChoices(pid, quest)
    local state = editorState(pid)
    if state.choiceSource == "offer" then showOfferEditor(pid, quest) else showStageDetail(pid, quest) end
end

-- X039: helpers for the real MyGUI Quest Studio. Commands come back through the
-- same hidden reliable GUI packet used by QuestSync, but no modal message box is
-- created. Every command is revalidated server-side.
local function editorChoiceList(quest, scope, stageIndex)
    if scope == "offer" or scope == "offerChoice" then
        quest.offer = quest.offer or { dialogue = "", choices = {} }
        quest.offer.choices = quest.offer.choices or {}
        return quest.offer.choices
    end
    local stage = findStage(quest, stageIndex)
    if stage == nil then return nil end
    stage.choices = stage.choices or {}
    return stage.choices
end

local function editorFindChoice(quest, scope, stageIndex, choiceId)
    local list = editorChoiceList(quest, scope, stageIndex)
    if list == nil then return nil, nil end
    choiceId = normalizeId(choiceId)
    for index, choice in ipairs(list) do
        if normalizeId(choice.id) == choiceId then return choice, index end
    end
    return nil, nil
end

local function editorRequirementList(quest, scope, stageIndex, choiceId)
    if scope == "stage" then
        local stage = findStage(quest, stageIndex)
        if stage == nil then return nil end
        stage.requirements = stage.requirements or {}
        return stage.requirements
    end
    local choice = editorFindChoice(quest, scope, stageIndex, choiceId)
    if choice == nil then return nil end
    choice.requirements = choice.requirements or {}
    return choice.requirements
end

local function editorRequirement(kind, op, value, reference)
    kind = tostring(kind or "")
    op = tostring(op or ">=")
    if kind == "level" or kind == "gold" or kind == "staffRank"
            or kind == "reputation" or kind == "bounty" or kind == "realTime" then
        return { type = kind, operator = op, value = parseValue(value) }
    elseif kind == "item" then
        return { type = kind, refId = trim(reference), operator = op, count = tonumber(value), value = tonumber(value) }
    elseif kind == "questStage" or kind == "questState" or kind == "cooldown" then
        return { type = kind, questId = normalizeId(reference), operator = op, value = parseValue(value) }
    elseif kind == "questCompleted" or kind == "questNotStarted" then
        return { type = kind, questId = normalizeId(reference) }
    elseif kind == "playerVariable" or kind == "serverVariable" or kind == "global"
            or kind == "skill" or kind == "attribute" or kind == "faction" or kind == "factionRank" then
        return { type = kind, key = trim(reference), operator = op, value = parseValue(value) }
    elseif kind == "vanillaJournal" then
        return { type = kind, questId = trim(reference), operator = op, value = parseValue(value) }
    elseif kind == "race" or kind == "class" or kind == "cell" then
        local expected = trim(value) ~= "" and trim(value) or trim(reference)
        return { type = kind, operator = "==", value = expected }
    end
    return nil
end

local function editorReward(kind, valueA, valueB)
    kind = tostring(kind or "")
    if kind == "gold" or kind == "xp" then
        return { type = kind, amount = tonumber(valueA) }
    elseif kind == "item" or kind == "giveItem" or kind == "takeItem" then
        return { type = kind, refId = trim(valueA), count = tonumber(valueB) or 1 }
    elseif kind == "setPlayerVariable" or kind == "setServerVariable" then
        return { type = kind, key = trim(valueA), value = parseValue(valueB) }
    elseif kind == "message" or kind == "messageBox" then
        return { type = kind, text = tostring(valueA or "") }
    elseif kind == "addSpell" or kind == "removeSpell" or kind == "playSound" then
        return { type = kind, refId = trim(valueA) }
    elseif kind == "setReputation" or kind == "setBounty" then
        return { type = kind, value = tonumber(valueA) }
    elseif kind == "teleport" then
        -- MyGUI v1 exposes two generic value fields; for teleport the first is
        -- the destination cell and optional coordinates remain JSON-advanced.
        return { type = kind, value = trim(valueA) }
    elseif kind == "setVanillaJournal" then
        return { type = kind, key = trim(valueA), value = tonumber(valueB) }
    end
    return nil
end

local function renameStageReferences(quest, oldIndex, newIndex)
    if oldIndex == newIndex then return end
    if tonumber(quest.initialStage) == oldIndex then quest.initialStage = newIndex end
    for _, stage in ipairs(quest.stages or {}) do
        for i, target in ipairs(stage.next or {}) do
            if tonumber(target) == oldIndex then stage.next[i] = newIndex end
        end
        for _, choice in ipairs(stage.choices or {}) do
            if tonumber(choice.targetStage) == oldIndex then choice.targetStage = newIndex end
        end
    end
    if quest.offer ~= nil then
        for _, choice in ipairs(quest.offer.choices or {}) do
            if tonumber(choice.targetStage) == oldIndex then choice.targetStage = newIndex end
        end
    end
end

local function removeStageReferences(quest, removedIndex)
    for _, stage in ipairs(quest.stages or {}) do
        local nextStages = {}
        for _, target in ipairs(stage.next or {}) do
            if tonumber(target) ~= removedIndex then table.insert(nextStages, target) end
        end
        stage.next = nextStages
        for _, choice in ipairs(stage.choices or {}) do
            if tonumber(choice.targetStage) == removedIndex then choice.targetStage = nil end
        end
    end
    if quest.offer ~= nil then
        for _, choice in ipairs(quest.offer.choices or {}) do
            if tonumber(choice.targetStage) == removedIndex then choice.targetStage = nil end
        end
    end
end

local function handleMyGuiEditorCommand(pid, data)
    if not isModerator(pid) then return true end
    local parts = splitTabsDecoded(data)
    if parts[1] ~= "EDITOR_CMD" then return false end
    local action = tostring(parts[2] or "")
    local function sync(selected, notice) serverQuestSystem.SyncEditor(pid, selected, notice) end

    if action == "select" or action == "refresh" then
        sync(parts[3], "")
        return true
    elseif action == "new" then
        local id, name = normalizeId(parts[3]), trim(parts[4])
        local ok, err = newQuest(pid, id .. " | " .. name)
        sync(ok and id or nil, ok and ("Created draft " .. id) or tostring(err))
        return true
    elseif action == "clone" then
        local source = serverQuestSystem.quests[normalizeId(parts[3])]
        local newId, newName = normalizeId(parts[4]), trim(parts[5])
        if source == nil or not validId(newId) or newName == "" or serverQuestSystem.quests[newId] ~= nil then
            sync(source and source.id or nil, "Clone failed: invalid or duplicate quest id")
        else
            local clone = copy(source)
            clone.id = newId; clone.name = newName; clone.status = "draft"; clone.version = 1
            clone.author = getAccountName(pid); clone.createdAt = os.time(); clone.updatedAt = clone.createdAt; clone.audit = {}
            serverQuestSystem.SaveQuest(clone, getAccountName(pid), "cloned from " .. source.id)
            sync(newId, "Cloned " .. source.id .. " -> " .. newId)
        end
        return true
    end

    local q = serverQuestSystem.quests[normalizeId(parts[3])]
    if q == nil then sync(nil, "Unknown quest") return true end
    local function edited(message)
        markEdited(pid, q, message)
        sync(q.id, message)
    end

    if action == "delete" then
        if not isAdmin(pid) then sync(q.id, "Administrator rank required")
        else q.status = "disabled"; q.version = (tonumber(q.version) or 1) + 1
            serverQuestSystem.SaveQuest(q, getAccountName(pid), "soft-deleted from MyGUI editor")
            sync(q.id, "Quest disabled (soft delete); file retained for recovery") end

    elseif action == "overview" then
        local newName = trim(parts[4]); local mode = tostring(parts[5] or "personal")
        local initial = tonumber(parts[9])
        if newName == "" then sync(q.id, "Quest name cannot be empty") return true end
        if mode ~= "personal" and mode ~= "party" and mode ~= "server" then mode = "personal" end
        q.name = newName; q.progressMode = mode
        q.giver.refId = trim(parts[6]); q.giver.cell = trim(parts[7]); q.giver.uniqueIndex = trim(parts[8])
        if q.giver.uniqueIndex == "" then q.giver.uniqueIndex = nil end
        if initial ~= nil and findStage(q, initial) ~= nil then q.initialStage = initial end
        edited("overview updated from MyGUI editor")

    elseif action == "pick_giver" then
        local state = editorState(pid); state.questId = q.id; state.pickGiver = true
        send(pid, "Giver picker armed. Activate the desired NPC once.")

    elseif action == "topic_upsert" then
        local oldId, newId, text = normalizeId(parts[4]), normalizeId(parts[5]), trim(parts[6])
        local enabledValue = tostring(parts[7] or "true") ~= "false"
        if not validId(newId) or text == "" then sync(q.id, "Topic id/text is invalid") return true end
        local topic = oldId ~= "" and findTopic(q, oldId) or nil
        if topic == nil then topic = { id = newId, text = text, enabled = enabledValue, green = true }; table.insert(q.topics, topic)
        else topic.id = newId; topic.text = text; topic.enabled = enabledValue; topic.green = true end
        edited("topic updated from MyGUI editor")

    elseif action == "topic_delete" then
        local id = normalizeId(parts[4])
        for index = #q.topics, 1, -1 do if normalizeId(q.topics[index].id) == id then table.remove(q.topics, index) end end
        edited("topic deleted from MyGUI editor")

    elseif action == "offer" then
        q.offer = q.offer or { dialogue = "", choices = {} }; q.offer.dialogue = tostring(parts[4] or "")
        edited("offer dialogue updated from MyGUI editor")

    elseif action == "choice_upsert" then
        local scope, stageIndex, oldId = tostring(parts[4]), tonumber(parts[5]) or 0, normalizeId(parts[6])
        local newId, choiceAction, targetText, text = normalizeId(parts[7]), tostring(parts[8] or "none"), trim(parts[9]), tostring(parts[10] or "")
        if not validId(newId) or text == "" or (choiceAction ~= "none" and choiceAction ~= "start" and choiceAction ~= "advance") then
            sync(q.id, "Choice id/text/action is invalid") return true
        end
        local list = editorChoiceList(q, scope, stageIndex); if list == nil then sync(q.id, "Choice stage not found") return true end
        local choice = oldId ~= "" and editorFindChoice(q, scope, stageIndex, oldId) or nil
        if choice == nil then choice = { requirements = {} }; table.insert(list, choice) end
        choice.id = newId; choice.text = text; choice.action = choiceAction
        choice.targetStage = targetText ~= "" and tonumber(targetText) or nil
        edited("choice updated from MyGUI editor")

    elseif action == "choice_delete" then
        local scope, stageIndex, choiceId = tostring(parts[4]), tonumber(parts[5]) or 0, normalizeId(parts[6])
        local list = editorChoiceList(q, scope, stageIndex)
        if list ~= nil then for index = #list, 1, -1 do if normalizeId(list[index].id) == choiceId then table.remove(list, index) end end end
        edited("choice deleted from MyGUI editor")

    elseif action == "stage_upsert" then
        local oldIndex, newIndex = tonumber(parts[4]) or 0, tonumber(parts[5])
        if newIndex == nil then sync(q.id, "Stage index must be numeric") return true end
        local existingAtNew = findStage(q, newIndex)
        local stage = oldIndex ~= 0 and findStage(q, oldIndex) or nil
        if stage == nil and existingAtNew ~= nil then stage = existingAtNew end
        if stage == nil then
            stage = { index = newIndex, journal = "", dialogue = "", requirements = {}, rewards = {}, choices = {}, next = {}, complete = false, fail = false }
            table.insert(q.stages, stage)
        elseif oldIndex ~= newIndex and existingAtNew ~= nil and existingAtNew ~= stage then
            sync(q.id, "Another stage already uses index " .. newIndex) return true
        else
            renameStageReferences(q, stage.index, newIndex); stage.index = newIndex
        end
        stage.journal = tostring(parts[6] or ""); stage.dialogue = tostring(parts[7] or "")
        local makeInitial = tostring(parts[8]) == "1"; stage.complete = tostring(parts[9]) == "1"; stage.fail = tostring(parts[10]) == "1"
        if stage.complete then stage.fail = false elseif stage.fail then stage.complete = false end
        if makeInitial then q.initialStage = newIndex end
        table.sort(q.stages, function(a,b) return (tonumber(a.index) or 0) < (tonumber(b.index) or 0) end)
        edited("stage updated from MyGUI editor")

    elseif action == "stage_delete" then
        local stageIndex = tonumber(parts[4]); if stageIndex == nil then sync(q.id, "Invalid stage") return true end
        for index = #q.stages, 1, -1 do if tonumber(q.stages[index].index) == stageIndex then table.remove(q.stages, index) end end
        removeStageReferences(q, stageIndex)
        if tonumber(q.initialStage) == stageIndex then q.initialStage = q.stages[1] and q.stages[1].index or 0 end
        edited("stage deleted from MyGUI editor")

    elseif action == "next_add" or action == "next_delete" then
        local stage = findStage(q, tonumber(parts[4])); local target = tonumber(parts[5])
        if stage == nil or target == nil or findStage(q, target) == nil then sync(q.id, "Transition target does not exist") return true end
        stage.next = stage.next or {}
        if action == "next_add" then
            if not tableHelper.containsValue(stage.next, target) then table.insert(stage.next, target) end
        else
            for index = #stage.next, 1, -1 do if tonumber(stage.next[index]) == target then table.remove(stage.next, index) end end
        end
        edited("transition updated from MyGUI editor")

    elseif action == "require_add" then
        local scope, stageIndex, choiceId = tostring(parts[4]), tonumber(parts[5]) or 0, normalizeId(parts[6])
        local list = editorRequirementList(q, scope, stageIndex, choiceId)
        local requirement = editorRequirement(parts[7], parts[8], parts[9], parts[10])
        if list == nil or requirement == nil then sync(q.id, "Requirement target/type is invalid") return true end
        table.insert(list, requirement); edited("requirement added from MyGUI editor")

    elseif action == "require_delete" then
        local list = editorRequirementList(q, tostring(parts[4]), tonumber(parts[5]) or 0, normalizeId(parts[6]))
        local index = tonumber(parts[7]); if list == nil or index == nil or list[index] == nil then sync(q.id, "Requirement not found") return true end
        table.remove(list, index); edited("requirement deleted from MyGUI editor")

    elseif action == "reward_add" then
        local stage = findStage(q, tonumber(parts[4])); local reward = editorReward(parts[5], parts[6], parts[7])
        if stage == nil or reward == nil then sync(q.id, "Reward target/type is invalid") return true end
        stage.rewards = stage.rewards or {}; table.insert(stage.rewards, reward); edited("reward added from MyGUI editor")

    elseif action == "reward_delete" then
        local stage = findStage(q, tonumber(parts[4])); local index = tonumber(parts[5])
        if stage == nil or index == nil or stage.rewards == nil or stage.rewards[index] == nil then sync(q.id, "Reward not found") return true end
        table.remove(stage.rewards, index); edited("reward deleted from MyGUI editor")

    elseif action == "validate" then
        local errors, warnings = serverQuestSystem.ValidateQuest(q)
        serverQuestSystem.validation[q.id] = { errors = errors, warnings = warnings }
        sync(q.id, string.format("Validation: %d error(s), %d warning(s)", #errors, #warnings))

    elseif action == "publish" then
        local approvalOk, approvalWhy = prepareQuestForPublish(pid, q)
        if not approvalOk then sync(q.id, approvalWhy) return true end
        local errors, warnings = serverQuestSystem.ValidateQuest(q)
        if #errors > 0 then serverQuestSystem.validation[q.id] = { errors = errors, warnings = warnings }; sync(q.id, "Fix validation errors before publishing")
        elseif not isAdmin(pid) and config.serverQuests ~= nil and config.serverQuests.moderatorsCanPublish == false then sync(q.id, "Admin approval required")
        else q.status = "published"; q.version = (tonumber(q.version) or 1) + 1; serverQuestSystem.SaveQuest(q, getAccountName(pid), "published from MyGUI editor"); sync(q.id, "Published " .. q.name) end

    elseif action == "disable" then
        q.status = "disabled"; q.version = (tonumber(q.version) or 1) + 1
        serverQuestSystem.SaveQuest(q, getAccountName(pid), "disabled from MyGUI editor")
        sync(q.id, "Quest disabled")
    else
        sync(q.id, "Unknown editor command: " .. action)
    end
    return true
end

local function onGuiAction(eventStatus, pid, idGui, data)
    if not enabled() or Players[pid] == nil or not Players[pid]:IsLoggedIn() then return end
    if idGui == QUEST_TRANSPORT_GUI_ID and type(data) == "string" and data:sub(1, 10) == "EDITOR_CMD" then
        handleMyGuiEditorCommand(pid, data)
        return
    end
    local menu = config.customMenuIds
    local index = tonumber(data)

    if idGui == menu.questEditorMain then
        if isModerator(pid) then
            if index == 0 then showQuestList(pid)
            elseif index == 1 then
                serverQuestSystem.editor[pid] = { input = "new" }
                tes3mp.InputDialog(pid, menu.questEditorInput, "New server quest", "Enter: quest_id | Display name")
            elseif index == 2 then showPlayerQuests(pid)
            elseif index == 3 then showJournal(pid)
            elseif index == 4 then tes3mp.CustomMessageBox(pid, menu.questPlayerJournal, HELP_TEXT, "Ok") end
        else
            if index == 0 then showPlayerQuests(pid)
            elseif index == 1 then showJournal(pid) end
        end

    elseif idGui == menu.questEditorList then
        local state = editorState(pid)
        local id = state.list ~= nil and state.list[(index or -1) + 1] or nil
        if id ~= nil then state.questId = id; showQuestDetails(pid, serverQuestSystem.quests[id]) end

    elseif idGui == menu.questEditorDetail and isModerator(pid) then
        local q = getCurrentQuest(pid)
        if q == nil then showMain(pid) return end
        local state = editorState(pid)
        if index == 0 then
            state.input = "quest_name"
            tes3mp.InputDialog(pid, menu.questEditorInput, "Quest display name", "Enter new display name")
        elseif index == 1 then showGiverEditor(pid, q)
        elseif index == 2 then showTopicsMenu(pid, q)
        elseif index == 3 then showOfferEditor(pid, q)
        elseif index == 4 then showStagesMenu(pid, q)
        elseif index == 5 then
            local errors, warnings = serverQuestSystem.ValidateQuest(q)
            local lines = { "Validation for " .. q.id, "Errors: " .. #errors, "Warnings: " .. #warnings }
            for _, e in ipairs(errors) do table.insert(lines, "ERROR: " .. e) end
            for _, w in ipairs(warnings) do table.insert(lines, "WARN: " .. w) end
            tes3mp.CustomMessageBox(pid, menu.questPlayerJournal, table.concat(lines, "\n"), "Ok")
        elseif index == 6 then
            if q.status == "published" then
                q.status = "disabled"; q.version = q.version + 1
                serverQuestSystem.SaveQuest(q, getAccountName(pid), "disabled from editor v2")
            else
                local approvalOk, approvalWhy = prepareQuestForPublish(pid, q)
                if not approvalOk then send(pid, approvalWhy)
                else
                    local errors, warnings = serverQuestSystem.ValidateQuest(q)
                    if #errors > 0 then send(pid, "Fix validation errors before publishing")
                    elseif not isAdmin(pid) and config.serverQuests ~= nil and config.serverQuests.moderatorsCanPublish == false then send(pid, "Admin approval required")
                    else q.status = "published"; q.version = q.version + 1; serverQuestSystem.SaveQuest(q, getAccountName(pid), "published from editor v2")
                        send(pid, "Published " .. q.name .. (#warnings > 0 and (" with " .. #warnings .. " warning(s)") or "")) end
                end
            end
            serverQuestSystem.SyncAll(); showQuestDetails(pid, q)
        elseif index == 7 then
            local cv = ensurePlayerData(pid); cv.serverQuests[q.id] = nil; Players[pid]:QuicksaveToDrive()
            local ok, why = serverQuestSystem.StartQuest(pid, q.id, true)
            if not ok then send(pid, why) else send(pid, "Test state reset and quest started") end
            serverQuestSystem.SyncPlayer(pid); showQuestDetails(pid, q)
        elseif index == 8 then showMain(pid) end

    elseif idGui == menu.questEditorGiver and isModerator(pid) then
        local q = getCurrentQuest(pid); if q == nil then return end
        local state = editorState(pid)
        if index == 0 then
            state.input = "giver"
            tes3mp.InputDialog(pid, menu.questEditorInput, "Quest giver", "Enter: refId | Cell (cell may be empty)")
        elseif index == 1 then
            state.pickGiver = true
            send(pid, "Giver picker armed. Close the editor and activate the NPC you want to use as quest giver.")
            tes3mp.CustomMessageBox(pid, menu.questPlayerJournal,
                "Pick giver in game\n\nActivate the desired NPC once. ArenaMP will capture its refId, current cell and uniqueIndex for this quest.\n\nIf you activate the wrong object, use Pick giver again.", "Ok")
        elseif index == 2 then
            q.giver.uniqueIndex = nil
            markEdited(pid, q, "giver uniqueIndex cleared")
            showGiverEditor(pid, q)
        elseif index == 3 then showQuestDetails(pid, q) end

    elseif idGui == menu.questEditorTopics and isModerator(pid) then
        local q = getCurrentQuest(pid); if q == nil then showMain(pid) return end
        local state = editorState(pid)
        if index == 0 then state.input = "topic_add"; tes3mp.InputDialog(pid, menu.questEditorInput, "Add green topic", "Enter: topic_id | Display text")
        elseif index == 1 then showTopicList(pid, q)
        elseif index == 2 then showQuestDetails(pid, q) end

    elseif idGui == menu.questEditorTopicList and isModerator(pid) then
        local q = getCurrentQuest(pid); if q == nil then return end
        local state = editorState(pid); local topicIndex = state.topicList ~= nil and state.topicList[(index or -1)+1] or nil
        if topicIndex ~= nil then state.topicIndex = topicIndex; showTopicDetail(pid, q) end

    elseif idGui == menu.questEditorTopicDetail and isModerator(pid) then
        local q = getCurrentQuest(pid); if q == nil then return end
        local state = editorState(pid); local topic = q.topics[tonumber(state.topicIndex or -1)]
        if topic == nil then showTopicsMenu(pid, q) return end
        if index == 0 then state.input = "topic_edit"; tes3mp.InputDialog(pid, menu.questEditorInput, "Edit green topic", "Enter new display text")
        elseif index == 1 then topic.enabled = not (topic.enabled ~= false); markEdited(pid, q, "topic enabled toggled"); serverQuestSystem.SyncAll(); showTopicDetail(pid, q)
        elseif index == 2 then table.remove(q.topics, state.topicIndex); state.topicIndex = nil; markEdited(pid, q, "topic deleted"); serverQuestSystem.SyncAll(); showTopicsMenu(pid, q)
        elseif index == 3 then showTopicsMenu(pid, q) end

    elseif idGui == menu.questEditorOffer and isModerator(pid) then
        local q = getCurrentQuest(pid); if q == nil then return end
        local state = editorState(pid)
        if index == 0 then state.input = "offer_dialogue"; tes3mp.InputDialog(pid, menu.questEditorInput, "Quest offer dialogue", "Enter NPC offer text")
        elseif index == 1 then state.choiceSource = "offer"; state.choiceIndex = nil; showChoicesMenu(pid, q)
        elseif index == 2 then showQuestDetails(pid, q) end

    elseif idGui == menu.questEditorChoices and isModerator(pid) then
        local q = getCurrentQuest(pid); if q == nil then return end
        local state = editorState(pid)
        if index == 0 then state.input = "choice_add"; tes3mp.InputDialog(pid, menu.questEditorInput, "Add dialogue choice", "Enter: choice_id | start/advance/none | targetStage/- | Text")
        elseif index == 1 then showChoiceList(pid, q)
        elseif index == 2 then returnFromChoices(pid, q) end

    elseif idGui == menu.questEditorChoiceList and isModerator(pid) then
        local q = getCurrentQuest(pid); if q == nil then return end
        local choices = getChoiceListForEditor(pid, q)
        if choices ~= nil and choices[(index or -1)+1] ~= nil then editorState(pid).choiceIndex = (index or -1)+1; showChoiceDetail(pid, q) end

    elseif idGui == menu.questEditorChoiceDetail and isModerator(pid) then
        local q = getCurrentQuest(pid); if q == nil then return end
        local state = editorState(pid); local choice = getEditorChoice(pid, q)
        if choice == nil then showChoicesMenu(pid, q) return end
        if index == 0 then state.input = "choice_edit"; tes3mp.InputDialog(pid, menu.questEditorInput, "Edit dialogue choice", "Enter: choice_id | start/advance/none | targetStage/- | Text")
        elseif index == 1 then state.requirementOwner = "choice"; showRequirementsMenu(pid, q)
        elseif index == 2 then local choices = getChoiceListForEditor(pid, q); table.remove(choices, state.choiceIndex); state.choiceIndex=nil; markEdited(pid,q,"choice deleted"); serverQuestSystem.SyncAll(); showChoicesMenu(pid,q)
        elseif index == 3 then showChoicesMenu(pid, q) end

    elseif idGui == menu.questEditorStages and isModerator(pid) then
        local q = getCurrentQuest(pid); if q == nil then return end
        local state = editorState(pid)
        if index == 0 then state.input = "stage_add"; tes3mp.InputDialog(pid, menu.questEditorInput, "Add quest stage", "Enter: index | Journal text | NPC dialogue")
        elseif index == 1 then showStageList(pid, q)
        elseif index == 2 then showQuestDetails(pid, q) end

    elseif idGui == menu.questEditorStageList and isModerator(pid) then
        local q = getCurrentQuest(pid); if q == nil then return end
        local state = editorState(pid); local stageIndex = state.stageList ~= nil and state.stageList[(index or -1)+1] or nil
        if stageIndex ~= nil then state.stageIndex = stageIndex; showStageDetail(pid, q) end

    elseif idGui == menu.questEditorStageDetail and isModerator(pid) then
        local q = getCurrentQuest(pid); if q == nil then return end
        local state = editorState(pid); local stage = getEditorStage(pid, q)
        if stage == nil then showStagesMenu(pid,q) return end
        if index == 0 then state.input = "stage_journal"; tes3mp.InputDialog(pid, menu.questEditorInput, "Stage journal entry", "Enter journal text")
        elseif index == 1 then state.input = "stage_dialogue"; tes3mp.InputDialog(pid, menu.questEditorInput, "Stage NPC dialogue", "Enter NPC response text")
        elseif index == 2 then state.requirementOwner = "stage"; showRequirementsMenu(pid, q)
        elseif index == 3 then state.choiceSource = tonumber(stage.index); state.choiceIndex=nil; showChoicesMenu(pid, q)
        elseif index == 4 then showRewardsMenu(pid, q)
        elseif index == 5 then showTransitionsMenu(pid, q)
        elseif index == 6 then showStageFlags(pid, q)
        elseif index == 7 then
            local removedIndex = tonumber(stage.index)
            for i, candidate in ipairs(q.stages) do if tonumber(candidate.index)==removedIndex then table.remove(q.stages,i); break end end
            for _, candidate in ipairs(q.stages) do
                local kept = {}; for _, target in ipairs(candidate.next or {}) do if tonumber(target) ~= removedIndex then kept[#kept+1]=target end end; candidate.next=kept
            end
            normalizeQuest(q)
            if tonumber(q.initialStage)==removedIndex then local sorted=getSortedStages(q); q.initialStage=#sorted>0 and sorted[1].index or nil end
            state.stageIndex=nil; markEdited(pid,q,"stage deleted"); serverQuestSystem.SyncAll(); showStagesMenu(pid,q)
        elseif index == 8 then showStagesMenu(pid, q) end

    elseif idGui == menu.questEditorStageFlags and isModerator(pid) then
        local q=getCurrentQuest(pid); if q==nil then return end
        local stage=getEditorStage(pid,q); if stage==nil then showStagesMenu(pid,q) return end
        if index==0 then q.initialStage=tonumber(stage.index); markEdited(pid,q,"initial stage changed"); showStageFlags(pid,q)
        elseif index==1 then stage.complete=not(stage.complete==true); if stage.complete then stage.fail=false end; markEdited(pid,q,"stage complete toggled"); showStageFlags(pid,q)
        elseif index==2 then stage.fail=not(stage.fail==true); if stage.fail then stage.complete=false end; markEdited(pid,q,"stage fail toggled"); showStageFlags(pid,q)
        elseif index==3 then showStageDetail(pid,q) end

    elseif idGui == menu.questEditorRequirements and isModerator(pid) then
        local q=getCurrentQuest(pid); if q==nil then return end
        local state=editorState(pid)
        if index==0 then state.input="requirement_add"; tes3mp.InputDialog(pid,menu.questEditorInput,"Add requirement","Examples: level | >= | 5   OR   item | ring_ref | >= | 1")
        elseif index==1 then showRequirementList(pid,q)
        elseif index==2 then returnFromRequirements(pid,q) end

    elseif idGui == menu.questEditorRequirementList and isModerator(pid) then
        local q=getCurrentQuest(pid); if q==nil then return end
        local requirements=getRequirementListForEditor(pid,q); local removeIndex=(index or -1)+1
        if requirements~=nil and requirements[removeIndex]~=nil then table.remove(requirements,removeIndex); markEdited(pid,q,"requirement deleted"); serverQuestSystem.SyncAll() end
        showRequirementsMenu(pid,q)

    elseif idGui == menu.questEditorRewards and isModerator(pid) then
        local q=getCurrentQuest(pid); if q==nil then return end
        local state=editorState(pid)
        if index==0 then state.input="reward_add"; tes3mp.InputDialog(pid,menu.questEditorInput,"Add reward/action","Examples: gold | 250   OR   xp | 125   OR   item | potion_ref | 1")
        elseif index==1 then showRewardList(pid,q)
        elseif index==2 then showStageDetail(pid,q) end

    elseif idGui == menu.questEditorRewardList and isModerator(pid) then
        local q=getCurrentQuest(pid); if q==nil then return end
        local stage=getEditorStage(pid,q); local removeIndex=(index or -1)+1
        if stage~=nil and stage.rewards[removeIndex]~=nil then table.remove(stage.rewards,removeIndex); markEdited(pid,q,"reward deleted"); serverQuestSystem.SyncAll() end
        showRewardsMenu(pid,q)

    elseif idGui == menu.questEditorTransitions and isModerator(pid) then
        local q=getCurrentQuest(pid); if q==nil then return end
        local state=editorState(pid)
        if index==0 then state.input="transition_add"; tes3mp.InputDialog(pid,menu.questEditorInput,"Add stage transition","Enter target stage index")
        elseif index==1 then showTransitionList(pid,q)
        elseif index==2 then showStageDetail(pid,q) end

    elseif idGui == menu.questEditorTransitionList and isModerator(pid) then
        local q=getCurrentQuest(pid); if q==nil then return end
        local stage=getEditorStage(pid,q); local removeIndex=(index or -1)+1
        if stage~=nil and stage.next[removeIndex]~=nil then table.remove(stage.next,removeIndex); markEdited(pid,q,"transition deleted") end
        showTransitionsMenu(pid,q)

    elseif idGui == menu.questEditorInput and isModerator(pid) then
        local state = editorState(pid); local action = state.input; state.input = nil
        if action == "new" then
            local ok, why = newQuest(pid, data); if not ok then send(pid, why) else showQuestDetails(pid, getCurrentQuest(pid)) end; return
        end
        local q = getCurrentQuest(pid); if q == nil then showMain(pid) return end
        local parts = splitPipe(data)
        if action == "quest_name" then
            if trim(data) == "" then send(pid, "Quest name cannot be empty") else q.name=trim(data); markEdited(pid,q,"quest name changed") end
            showQuestDetails(pid,q)
        elseif action == "giver" then
            q.giver.refId, q.giver.cell, q.giver.uniqueIndex = parts[1] or "", parts[2] or "", nil
            markEdited(pid,q,"giver manually changed")
            showQuestDetails(pid,q)
        elseif action == "topic_add" then
            local id,text=normalizeId(parts[1]),parts[2] or ""
            if not validId(id) or text=="" then send(pid,"Invalid topic input") else
                local topic=findTopic(q,id); if topic==nil then table.insert(q.topics,{id=id,text=text,enabled=true,green=true}) else topic.text=text end
                markEdited(pid,q,"topic added from editor v2"); serverQuestSystem.SyncAll()
            end
            showTopicsMenu(pid,q)
        elseif action == "topic_edit" then
            local topic=q.topics[tonumber(state.topicIndex or -1)]
            if topic~=nil and trim(data)~="" then topic.text=trim(data); markEdited(pid,q,"topic text edited"); serverQuestSystem.SyncAll() end
            showTopicDetail(pid,q)
        elseif action == "offer_dialogue" then
            q.offer=q.offer or {dialogue="",choices={}}; q.offer.dialogue=trim(data); markEdited(pid,q,"offer dialogue edited"); serverQuestSystem.SyncAll(); showOfferEditor(pid,q)
        elseif action == "stage_add" then
            local stageIndex=tonumber(parts[1])
            if stageIndex==nil then send(pid,"Invalid stage index") else
                local stage=findStage(q,stageIndex)
                if stage==nil then stage={index=stageIndex,journal=parts[2] or "",dialogue=parts[3] or "",requirements={},choices={},rewards={},next={},complete=false,fail=false}; table.insert(q.stages,stage)
                    if #q.stages==1 or q.initialStage==nil then q.initialStage=stageIndex end
                else stage.journal=parts[2] or stage.journal; stage.dialogue=parts[3] or stage.dialogue end
                normalizeQuest(q); state.stageIndex=stageIndex; markEdited(pid,q,"stage added/edited from editor v2")
            end
            showStageDetail(pid,q)
        elseif action == "stage_journal" then
            local stage=getEditorStage(pid,q); if stage~=nil then stage.journal=trim(data); markEdited(pid,q,"stage journal edited") end; showStageDetail(pid,q)
        elseif action == "stage_dialogue" then
            local stage=getEditorStage(pid,q); if stage~=nil then stage.dialogue=trim(data); markEdited(pid,q,"stage dialogue edited"); serverQuestSystem.SyncAll() end; showStageDetail(pid,q)
        elseif action == "choice_add" or action == "choice_edit" then
            local choices=getChoiceListForEditor(pid,q); local choiceId=normalizeId(parts[1]); local actionName=tostring(parts[2] or "none"):lower(); local targetRaw=tostring(parts[3] or "-"); local text=parts[4] or ""
            if choices==nil or not validId(choiceId) or text=="" or not (actionName=="start" or actionName=="advance" or actionName=="none") then send(pid,"Invalid choice format")
            else
                local target=targetRaw~="-" and tonumber(targetRaw) or nil
                local replacement=normalizeChoice({id=choiceId,text=text,action=actionName,targetStage=target,requirements={}})
                if action=="choice_edit" then
                    local old=choices[tonumber(state.choiceIndex or -1)]; if old~=nil then replacement.requirements=old.requirements or {}; choices[state.choiceIndex]=replacement end
                else table.insert(choices,replacement); state.choiceIndex=#choices end
                markEdited(pid,q,"choice added/edited from editor v2"); serverQuestSystem.SyncAll()
            end
            showChoiceDetail(pid,q)
        elseif action == "requirement_add" then
            local requirements=getRequirementListForEditor(pid,q); local requirement=parseRequirementParts(parts)
            if requirements==nil or requirement==nil or requirement.operator==nil or requirement.value==nil then send(pid,"Invalid requirement format")
            else table.insert(requirements,requirement); markEdited(pid,q,"requirement added from editor v2") end
            showRequirementsMenu(pid,q)
        elseif action == "reward_add" then
            local stage=getEditorStage(pid,q); local reward=parseRewardParts(parts)
            if stage==nil or reward==nil then send(pid,"Invalid reward format") else table.insert(stage.rewards,reward); markEdited(pid,q,"reward added from editor v2") end
            showRewardsMenu(pid,q)
        elseif action == "transition_add" then
            local stage=getEditorStage(pid,q); local target=tonumber(parts[1])
            if stage==nil or target==nil or findStage(q,target)==nil then send(pid,"Target stage does not exist")
            else stage.next=stage.next or {}; if not tableHelper.containsValue(stage.next,target) then table.insert(stage.next,target); markEdited(pid,q,"transition added from editor v2") end end
            showTransitionsMenu(pid,q)
        else
            showQuestDetails(pid,q)
        end
    end
end


local function onObjectActivateQuestPicker(eventStatus, pid, cellDescription, objects, targetPlayers)
    if not enabled() or not isModerator(pid) then return end
    local state = serverQuestSystem.editor[pid]
    if state == nil or state.pickGiver ~= true then return end
    local q = getCurrentQuest(pid)
    if q == nil then state.pickGiver = false; return end

    for uniqueIndex, object in pairs(objects or {}) do
        if type(object) == "table" and trim(object.refId) ~= "" then
            q.giver.refId = trim(object.refId)
            q.giver.cell = trim(cellDescription)
            q.giver.uniqueIndex = tostring(uniqueIndex)
            state.pickGiver = false
            markEdited(pid, q, "giver picked in game")
            send(pid, "Quest giver selected: " .. q.giver.refId .. " @ " .. q.giver.cell .. " [" .. q.giver.uniqueIndex .. "]")
            serverQuestSystem.SyncEditor(pid, q.id, "Giver selected in game: " .. q.giver.refId)
            return
        end
    end
end

-- X047: dialogue topic sync is intentionally repeated just-in-time when a
-- player activates an NPC that is the giver of a published server quest.  The
-- login sync can race with private-instance CELL changes / CharGen teardown;
-- resending here is cheap and makes the dialogue registry self-healing.
local function onObjectActivateQuestResync(eventStatus, pid, cellDescription, objects, targetPlayers)
    if not enabled() or Players[pid] == nil or not Players[pid]:IsLoggedIn() then return end

    local shouldSync = false
    for _, object in pairs(objects or {}) do
        if type(object) == "table" then
            local refId = tostring(object.refId or ""):lower()
            if refId ~= "" then
                for _, questId in ipairs(sortedQuestIds("published")) do
                    local quest = serverQuestSystem.quests[questId]
                    local validation = serverQuestSystem.validation[questId]
                    if quest ~= nil and (validation == nil or #validation.errors == 0)
                        and tostring(quest.giver.refId or ""):lower() == refId
                        and questCellMatches(quest.giver.cell, cellDescription) then
                        shouldSync = true
                        break
                    end
                end
            end
        end
        if shouldSync then break end
    end

    if shouldSync then
        log(enumerations.log.INFO, "JIT topic sync for pid " .. tostring(pid)
            .. " in " .. tostring(cellDescription))
        serverQuestSystem.SyncPlayer(pid)

        -- X048: refresh our inventory mirror while the player is still walking up to
        -- the quest giver, so a hand-in evaluated a second later sees the truth.
        requestInventoryResync(pid)
    end
end

local function onPlayerAuthentified(eventStatus, pid)
    if enabled() then ensurePlayerData(pid) end
end

local function onPlayerFinishLogin(eventStatus, pid)
    if enabled() then
        serverQuestSystem.SyncPlayer(pid)
        -- Y089: do not request a full client inventory dump during the login
        -- handshake. Generated records can still be applying at this point, and
        -- an incomplete PlayerInventory SET used to overwrite the authoritative
        -- server save and remove self-enchanted/custom-named equipment. JIT
        -- resyncs near quest givers remain available after login.
    end
end

local function onPlayerDisconnectQuest(eventStatus, pid)
    serverQuestSystem.pendingChoice[pid] = nil
end

function serverQuestSystem.Initialize()
    if not enabled() then
        log(enumerations.log.INFO, "Server quest system disabled by config")
        return
    end
    serverQuestSystem.LoadAll()
    customCommandHooks.registerCommand("quest", processCommand)
    customCommandHooks.registerCommand("quests", function(pid, cmd)
        local sub = tostring(cmd[2] or ""):lower()
        if sub == "sync" or sub == "resync" then
            serverQuestSystem.SyncPlayer(pid)
            send(pid, "Server quest topics resynced. Reopen the NPC dialogue if it was already closed.")
            return
        elseif sub == "studio" or sub == "editor" then
            processCommand(pid, { "quest", "studio" })
            return
        elseif sub == "caius" or sub == "caiusdebug" then
            local questId = "arena_caius_drink"
            local quest = serverQuestSystem.quests[questId]
            if quest == nil then
                send(pid, "Caius test quest definition is not loaded")
                return
            end
            local state = serverQuestSystem.GetPlayerState(pid, questId)
            local stateText = state == nil and "not_started"
                or (tostring(state.state) .. " stage=" .. tostring(state.stage))
            local validation = serverQuestSystem.validation[questId]
            local validationText = validation ~= nil and #validation.errors > 0
                and ("invalid: " .. table.concat(validation.errors, "; ")) or "valid"
            send(pid, "Caius test quest: status=" .. tostring(quest.status)
                .. ", state=" .. stateText .. ", definition=" .. validationText
                .. ". If completed/failed, staff can use /quest reset arena_caius_drink " .. tostring(pid))
            serverQuestSystem.SyncPlayer(pid)
            return
        end
        showPlayerQuests(pid)
    end)
    customCommandHooks.registerCommand("queststudio", function(pid, cmd)
        processCommand(pid, { "quest", "studio" })
    end)
    customEventHooks.registerHandler("OnGUIAction", onGuiAction)
    customEventHooks.registerHandler("OnObjectActivate", onObjectActivateQuestPicker)
    customEventHooks.registerHandler("OnObjectActivate", onObjectActivateQuestResync)
    customEventHooks.registerHandler("OnPlayerAuthentified", onPlayerAuthentified)
    customEventHooks.registerHandler("OnPlayerFinishLogin", onPlayerFinishLogin)
    customEventHooks.registerHandler("OnPlayerInventory", onPlayerInventoryResync)
    customEventHooks.registerHandler("OnPlayerDisconnect", onPlayerDisconnectQuest)
    customEventHooks.registerValidator("OnObjectDialogueChoice", validateServerQuestDialogue)
    customEventHooks.registerHandler("OnObjectDialogueChoice", handleServerQuestDialogue)
    log(enumerations.log.INFO, "quest hand-in resync + journal injection + Quest Studio autofit initialized")
end

serverQuestSystem.Initialize()
return serverQuestSystem
