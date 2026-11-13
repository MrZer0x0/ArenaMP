-- ArenaMP QW1: Quest Studio wizard.
--
-- The wizard lets a moderator who has never seen the quest JSON build a working
-- quest from four templates:
--
--   fetch  - bring items to the quest giver
--   kill   - kill N creatures/NPCs of one kind, then report back
--   visit  - reach a place (cell, optionally a radius around a point), then report back
--   talk   - the giver asks a question; each answer leads to its own ending
--
-- This module is pure Lua (no tes3mp calls), so it can be tested on its own.
-- serverQuestSystem.lua owns transport, saving, publishing and the runtime
-- triggers for the kill/visit objectives that Build() emits.
--
-- The wizard parameters are stored verbatim in quest.wizard, so a quest made in
-- the wizard can be reopened in the wizard. The generated stages use fixed ids
-- (topic, accept, decline, turnin, answerN), so existing translations and
-- player progress survive a rebuild.

local M = {}

M.TYPES = { fetch = true, kill = true, visit = true, talk = true }
M.STAGE_START = 10
M.STAGE_MID = 20
M.STAGE_DONE = 100
M.MAX_ITEMS = 8
M.MAX_ANSWERS = 3
M.MAX_TEXT = 2000

-- Scalar keys accepted from the client. Anything else is ignored.
local SCALAR_KEYS = {
    type = true, name = true, topic = true,
    giverRefId = true, giverCell = true, giverUnique = true,
    takeItems = true, killRefId = true, killCount = true,
    visitCell = true, visitX = true, visitY = true, visitZ = true, visitRadius = true, visitName = true,
    talkQuestion = true,
    offer = true, accept = true, decline = true, waitText = true, returnText = true,
    turnIn = true, done = true,
    journalStart = true, journalMid = true, journalDone = true, journalFail = true,
    gold = true, xp = true
}

-- Serialization order (also used for EDITOR_WIZARD so the client sees a stable list).
local SCALAR_ORDER = {
    "type", "name", "topic", "giverRefId", "giverCell", "giverUnique",
    "takeItems", "killRefId", "killCount",
    "visitCell", "visitX", "visitY", "visitZ", "visitRadius", "visitName",
    "talkQuestion", "offer", "accept", "decline", "waitText", "returnText", "turnIn", "done",
    "journalStart", "journalMid", "journalDone", "journalFail", "gold", "xp"
}

local function trim(value)
    if value == nil then return "" end
    return (tostring(value):gsub("^%s+", ""):gsub("%s+$", ""))
end

local function text(value)
    value = trim(value)
    if #value > M.MAX_TEXT then value = value:sub(1, M.MAX_TEXT) end
    return value
end

local function integer(value, default, low, high)
    local n = tonumber(value)
    if n == nil or n ~= n then n = default end
    n = math.floor(n)
    if n < low then n = low end
    if n > high then n = high end
    return n
end

local function float(value, default)
    local n = tonumber(value)
    if n == nil or n ~= n or n == math.huge or n == -math.huge then return default end
    return n
end

local function isRefId(value)
    return type(value) == "string" and value ~= "" and #value <= 120 and not value:find("[%c]")
end

-- parts: array of decoded transport fields; keys start at index `first`.
function M.Parse(parts, first)
    local w = { fetchItems = {}, rewardItems = {}, answers = {} }
    local index = first or 1
    while index <= #parts do
        local key, value = tostring(parts[index] or ""), parts[index + 1]
        if key == "fetchItem" then
            if #w.fetchItems < M.MAX_ITEMS then table.insert(w.fetchItems, { refId = trim(value), count = 1 }) end
        elseif key == "fetchCount" then
            local last = w.fetchItems[#w.fetchItems]
            if last then last.count = integer(value, 1, 1, 1000) end
        elseif key == "rewardItem" then
            if #w.rewardItems < M.MAX_ITEMS then table.insert(w.rewardItems, { refId = trim(value), count = 1 }) end
        elseif key == "rewardCount" then
            local last = w.rewardItems[#w.rewardItems]
            if last then last.count = integer(value, 1, 1, 1000) end
        elseif key == "answer" then
            if #w.answers < M.MAX_ANSWERS then
                table.insert(w.answers, { success = tostring(value) == "1", text = "", reply = "" })
            end
        elseif key == "answerText" then
            local last = w.answers[#w.answers]
            if last then last.text = text(value) end
        elseif key == "answerReply" then
            local last = w.answers[#w.answers]
            if last then last.reply = text(value) end
        elseif SCALAR_KEYS[key] then
            w[key] = value
        end
        index = index + 2
    end
    return M.Normalize(w)
end

function M.Normalize(w)
    w = type(w) == "table" and w or {}
    w.type = tostring(w.type or "fetch")
    if not M.TYPES[w.type] then w.type = "fetch" end
    for _, key in ipairs(SCALAR_ORDER) do
        if key ~= "type" then w[key] = text(w[key]) end
    end
    w.takeItems = w.takeItems ~= "0" and w.takeItems ~= false
    w.killCount = integer(w.killCount, 1, 1, 1000)
    w.visitX = float(w.visitX, 0)
    w.visitY = float(w.visitY, 0)
    w.visitZ = float(w.visitZ, 0)
    w.visitRadius = integer(w.visitRadius, 0, 0, 20000)
    w.gold = integer(w.gold, 0, 0, 10000000)
    w.xp = integer(w.xp, 0, 0, 10000000)
    w.fetchItems = type(w.fetchItems) == "table" and w.fetchItems or {}
    w.rewardItems = type(w.rewardItems) == "table" and w.rewardItems or {}
    w.answers = type(w.answers) == "table" and w.answers or {}
    for _, list in ipairs({ w.fetchItems, w.rewardItems }) do
        for _, item in ipairs(list) do
            item.refId = trim(item.refId)
            item.count = integer(item.count, 1, 1, 1000)
        end
    end
    for _, answer in ipairs(w.answers) do
        answer.text = text(answer.text)
        answer.reply = text(answer.reply)
        answer.success = answer.success == true
    end
    return w
end

-- Flat key/value list (same keys Parse accepts).
function M.Serialize(w)
    local out = {}
    local function put(key, value) out[#out + 1] = key; out[#out + 1] = tostring(value == nil and "" or value) end
    for _, key in ipairs(SCALAR_ORDER) do
        local value = w[key]
        if key == "takeItems" then value = w.takeItems and "1" or "0" end
        put(key, value)
    end
    for _, item in ipairs(w.fetchItems or {}) do put("fetchItem", item.refId); put("fetchCount", item.count) end
    for _, item in ipairs(w.rewardItems or {}) do put("rewardItem", item.refId); put("rewardCount", item.count) end
    for _, answer in ipairs(w.answers or {}) do
        put("answer", answer.success and "1" or "0")
        put("answerText", answer.text)
        put("answerReply", answer.reply)
    end
    return out
end

-- Plain-language problems. `ru` selects the language.
function M.Validate(w, ru)
    local errors = {}
    local function add(r, e) errors[#errors + 1] = ru and r or e end
    if w.name == "" then add("Не задано название квеста.", "The quest has no name.") end
    if not isRefId(w.giverRefId) then
        add("Не выбран квестодатель: нажмите «Выбрать NPC в мире».", "No quest giver: press \"Pick NPC in the world\".")
    end
    if w.offer == "" then add("NPC ничего не говорит, когда предлагает квест.", "The offer text is empty.") end
    if w.accept == "" then add("Пустая кнопка согласия.", "The accept button text is empty.") end
    if w.decline == "" then add("Пустая кнопка отказа.", "The decline button text is empty.") end

    if w.type == "fetch" then
        if #w.fetchItems == 0 then add("Не выбрано, что нужно принести.", "No items to bring are selected.") end
        for _, item in ipairs(w.fetchItems) do
            if not isRefId(item.refId) then add("В списке предметов есть пустая строка.", "The item list has an empty entry.") break end
        end
    elseif w.type == "kill" then
        if not isRefId(w.killRefId) then
            add("Не выбрано, кого убить: нажмите «Выбрать цель в мире».", "No target: press \"Pick target in the world\".")
        end
    elseif w.type == "visit" then
        if w.visitCell == "" then
            add("Не отмечено место: встаньте там и нажмите «Отметить место».", "No place: stand there and press \"Mark this place\".")
        end
    elseif w.type == "talk" then
        if w.talkQuestion == "" then add("NPC не задаёт вопрос.", "The question text is empty.") end
        if #w.answers == 0 then add("Нет ни одного варианта ответа.", "There are no answers.") end
        for index, answer in ipairs(w.answers) do
            if answer.text == "" then
                add("Ответ " .. index .. ": пустой текст ответа.", "Answer " .. index .. ": the answer text is empty.")
            end
            if answer.reply == "" then
                add("Ответ " .. index .. ": NPC ничего не отвечает.", "Answer " .. index .. ": the NPC reply is empty.")
            end
        end
    end

    if w.type ~= "talk" then
        if w.turnIn == "" then add("Пустая кнопка сдачи задания.", "The turn-in button text is empty.") end
        if w.returnText == "" then
            add("NPC ничего не говорит, когда игрок возвращается.", "The return text is empty.")
        end
    end
    for _, item in ipairs(w.rewardItems) do
        if not isRefId(item.refId) then add("В награде есть пустой предмет.", "The reward list has an empty item.") break end
    end
    return errors
end

local function copyList(list)
    local out = {}
    for index, value in ipairs(list) do
        if type(value) == "table" then
            local entry = {}
            for k, v in pairs(value) do entry[k] = v end
            out[index] = entry
        else
            out[index] = value
        end
    end
    return out
end

local function stage(index, journal, dialogue)
    return { index = index, journal = journal or "", dialogue = dialogue or "", requirements = {},
        rewards = {}, choices = {}, next = {}, complete = false, fail = false }
end

local function successRewards(w)
    local rewards = {}
    if w.type == "fetch" and w.takeItems then
        for _, item in ipairs(w.fetchItems) do
            rewards[#rewards + 1] = { type = "takeItem", refId = item.refId, count = item.count }
        end
    end
    if w.gold > 0 then rewards[#rewards + 1] = { type = "gold", amount = w.gold } end
    if w.xp > 0 then rewards[#rewards + 1] = { type = "xp", amount = w.xp } end
    for _, item in ipairs(w.rewardItems) do
        rewards[#rewards + 1] = { type = "item", refId = item.refId, count = item.count }
    end
    return rewards
end

-- Returns the generated quest parts. The caller copies them into a quest table
-- and keeps id/author/status/audit/translations.
function M.Build(w)
    w = M.Normalize(w)
    local quest = {
        name = w.name,
        progressMode = "personal",
        giver = { refId = w.giverRefId, cell = w.giverCell,
            uniqueIndex = w.giverUnique ~= "" and w.giverUnique or nil },
        topics = { { id = "topic", text = w.topic ~= "" and w.topic or w.name, enabled = true, green = true } },
        offer = { dialogue = w.offer, choices = {
            { id = "accept", text = w.accept, action = "start", requirements = {} },
            { id = "decline", text = w.decline, action = "none", requirements = {} },
        } },
        stages = {},
        initialStage = M.STAGE_START,
    }

    local done = stage(M.STAGE_DONE, w.journalDone, w.done)
    done.complete = true

    if w.type == "fetch" then
        local requirements = {}
        for _, item in ipairs(w.fetchItems) do
            requirements[#requirements + 1] = { type = "item", refId = item.refId, operator = ">=",
                count = item.count, value = item.count }
        end
        local start = stage(M.STAGE_START, w.journalStart, w.returnText)
        -- The turn-in answer is only offered while the player carries the items;
        -- the target stage repeats the check authoritatively (and triggers the
        -- inventory resync + retry if the server's copy is stale).
        start.choices = { { id = "turnin", text = w.turnIn, action = "advance", targetStage = M.STAGE_DONE,
            requirements = copyList(requirements) } }
        start.next = { M.STAGE_DONE }
        done.requirements = requirements
        done.rewards = successRewards(w)
        quest.stages = { start, done }

    elseif w.type == "kill" or w.type == "visit" then
        local start = stage(M.STAGE_START, w.journalStart, w.waitText)
        if w.type == "kill" then
            start.objective = { type = "kill", refId = w.killRefId, count = w.killCount, nextStage = M.STAGE_MID }
        else
            start.objective = { type = "visit", cell = w.visitCell, posX = w.visitX, posY = w.visitY,
                posZ = w.visitZ, radius = w.visitRadius, nextStage = M.STAGE_MID }
        end
        start.next = { M.STAGE_MID }
        local back = stage(M.STAGE_MID, w.journalMid, w.returnText)
        back.choices = { { id = "turnin", text = w.turnIn, action = "advance", targetStage = M.STAGE_DONE,
            requirements = {} } }
        back.next = { M.STAGE_DONE }
        done.rewards = successRewards(w)
        quest.stages = { start, back, done }

    else -- talk
        local start = stage(M.STAGE_START, w.journalStart, w.talkQuestion)
        quest.stages = { start }
        for index, answer in ipairs(w.answers) do
            local target = M.STAGE_DONE + (index - 1) * 10
            start.choices[#start.choices + 1] = { id = "answer" .. index, text = answer.text,
                action = "advance", targetStage = target, requirements = {} }
            start.next[#start.next + 1] = target
            local ending = stage(target, answer.success and w.journalDone or w.journalFail, answer.reply)
            ending.complete = answer.success
            ending.fail = not answer.success
            if answer.success then ending.rewards = successRewards(w) end
            quest.stages[#quest.stages + 1] = ending
        end
    end
    return quest
end

-- Plain-language one-paragraph description used by the summary page and logs.
function M.Describe(w, ru)
    local lines = {}
    local function add(r, e) lines[#lines + 1] = ru and r or e end
    if w.type == "fetch" then
        local items = {}
        for _, item in ipairs(w.fetchItems) do items[#items + 1] = item.count .. " x " .. item.refId end
        add("Принести: " .. table.concat(items, ", "), "Bring: " .. table.concat(items, ", "))
    elseif w.type == "kill" then
        add("Убить: " .. w.killCount .. " x " .. w.killRefId, "Kill: " .. w.killCount .. " x " .. w.killRefId)
    elseif w.type == "visit" then
        add("Дойти до: " .. w.visitCell, "Reach: " .. w.visitCell)
    else
        add("Разговор: вариантов ответа " .. #w.answers, "Conversation: " .. #w.answers .. " answer(s)")
    end
    return table.concat(lines, "\n")
end

return M
