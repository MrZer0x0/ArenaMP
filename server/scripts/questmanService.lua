-- ArenaMP QM1: Quest Manager services paid with Skill Points.
--
-- The client's Quest Manager (Questman) window asks; this module decides,
-- charges and records. Everything travels over the hidden GUI transport that
-- Quest Studio already uses (GUI id -35036), so no packet id is added:
--   client -> server  "QUESTMAN_CMD\t<action>\t<field>..."   (percent-encoded fields)
--   server -> client  "QUESTMAN\t<kind>\t<field>..."
--
-- Services (prices in config.questmanServices):
--   reset   forget a quest so it can be taken again        resetSmall / resetLarge
--   skip    complete a quest: XP (client, as for a normal   skip / skipFaction
--           completion) + faction reputation (server)
--   clear   wipe the journal text, keep every quest's
--           progress and every known dialogue topic         clearJournal
--   leave   leave a faction (rank, expulsion, reputation)   leaveFaction
--
-- Safety rules:
--   * server quests (Quest Studio) are never touched;
--   * a quest can be skipped once per character, and a skipped quest cannot be
--     skipped again after a reset (no reputation farming);
--   * shared journal / shared faction modes refuse the service (it would
--     change other players' progress);
--   * a short per-player cooldown, and every action is logged.
local questmanService = {}

local TRANSPORT_GUI_ID = -35036
local CMD_PREFIX = "QUESTMAN_CMD"
local REPLY = "QUESTMAN"
local STATE_KEY = "arenaQuestman"
local HIDE_CHUNK = 40

local defaults = {
    enabled = true,
    resetSmall = 5,          -- quest with few journal entries
    resetLarge = 10,         -- quest with resetLargeFromEntries entries or more
    resetLargeFromEntries = 3,
    skip = 10,
    skipFaction = 15,        -- guild / great house quests
    skipReputation = 2,      -- reputation granted with the faction of a skipped quest
    clearJournal = 10,
    leaveFaction = 10,
    cooldownSeconds = 2
}

local function cfg()
    local result = {}
    for key, value in pairs(defaults) do result[key] = value end
    if type(config) == "table" and type(config.questmanServices) == "table" then
        for key, value in pairs(config.questmanServices) do
            if defaults[key] ~= nil then result[key] = value end
        end
    end
    return result
end

local function logInfo(text)
    tes3mp.LogMessage(enumerations.log.INFO, "[Questman] " .. text)
end

-- ------------------------------------------------------------------ transport

local function percentEncode(value)
    value = tostring(value or "")
    return (value:gsub("[%%\t\r\n;:,]", function(c) return string.format("%%%02X", string.byte(c)) end))
end

local function percentDecode(value)
    value = tostring(value or "")
    return (value:gsub("%%(%x%x)", function(hex) return string.char(tonumber(hex, 16)) end))
end

local function splitTabs(value)
    local result = {}
    for part in (tostring(value or "") .. "\t"):gmatch("(.-)\t") do
        table.insert(result, percentDecode(part))
    end
    return result
end

local function send(pid, kind, ...)
    if Players[pid] == nil or not Players[pid]:IsLoggedIn() then return end
    local fields = { REPLY, kind }
    for index = 1, select('#', ...) do
        table.insert(fields, percentEncode(select(index, ...)))
    end
    tes3mp.CustomMessageBox(pid, TRANSPORT_GUI_ID, table.concat(fields, "\t"), "")
end

local function fail(pid, key, ...)
    send(pid, "error", key, ...)
    return false
end

-- ------------------------------------------------------------------ state

local function valid(pid)
    return Players[pid] ~= nil and Players[pid]:IsLoggedIn() and type(Players[pid].data) == "table"
end

local function state(pid)
    local data = Players[pid].data
    if type(data.customVariables) ~= "table" then data.customVariables = {} end
    local s = data.customVariables[STATE_KEY]
    if type(s) ~= "table" then
        s = {}
        data.customVariables[STATE_KEY] = s
    end
    if type(s.hidden) ~= "table" then s.hidden = {} end     -- "quest:index" entries kept only for state
    if type(s.skipped) ~= "table" then s.skipped = {} end   -- quest id -> true
    if type(s.history) ~= "table" then s.history = {} end
    return s
end

local function remember(pid, entry)
    local s = state(pid)
    entry.at = os.time()
    table.insert(s.history, entry)
    while #s.history > 30 do table.remove(s.history, 1) end
end

local function points(pid)
    local stats = Players[pid].data.stats or {}
    return math.max(0, math.floor((tonumber(stats.skillPoints) or 0) + 0.0001))
end

local function spend(pid, amount, reason)
    local ok, antiCheat = pcall(require, "arenaAntiCheat")
    if ok and type(antiCheat) == "table" and type(antiCheat.SpendSkillPoints) == "function" then
        local spent, balance = antiCheat.SpendSkillPoints(pid, amount, reason)
        return spent, balance
    end
    local player = Players[pid]
    local before = points(pid)
    if before < amount then return false, before end
    player.data.stats.skillPoints = before - amount
    tes3mp.SetSkillPoints(pid, player.data.stats.skillPoints)
    player:LoadLevel(true)
    player:QuicksaveToDrive()
    return true, player.data.stats.skillPoints
end

local function lower(value)
    return string.lower(tostring(value or ""))
end

-- ------------------------------------------------------------------ quests

-- Same buckets as the client's Questman classification: guild and great-house
-- quests cost more to skip. Only vanilla prefixes map to a real faction id for
-- the reputation reward; add-on factions are priced but get no reputation.
local VANILLA_FACTIONS = {
    { "fg_", "Fighters Guild" }, { "mg_", "Mages Guild" }, { "tg_", "Thieves Guild" },
    { "mt_", "Morag Tong" }, { "il_", "Imperial Legion" }, { "ic", "Imperial Cult" },
    { "tt_", "Temple" }, { "co_", "East Empire Company" },
    { "hh_", "Hlaalu" }, { "hr_", "Redoran" }, { "ht_", "Telvanni" }
}
local ADDON_FACTION_TOKENS = { "_hh", "_hr", "_ht", "_eec", "_fg", "_ic", "_il", "_jns", "_mg", "_mt", "_tg", "_tt", "_k1", "_ip" }

local function hasToken(id, token)
    local start = 1
    while true do
        local first, last = string.find(id, token, start, true)
        if first == nil then return false end
        local nextChar = id:sub(last + 1, last + 1)
        if nextChar == "_" or nextChar:match("%d") then return true end
        start = first + 1
    end
end

local function questFaction(questId)
    local id = lower(questId)
    local addon = (id:match("^tr_m%d") ~= nil) or id:find("^tr_necmq") ~= nil or id:match("^pc_m%d") ~= nil
    if addon then
        for _, token in ipairs(ADDON_FACTION_TOKENS) do
            if hasToken(id, token) then return true, nil end
        end
        return false, nil
    end
    if id:match("^sky_") then
        for _, tag in ipairs({ "tg", "mg", "fg" }) do
            if id:find(tag .. "%d") then return true, nil end
        end
        return false, nil
    end
    for _, pair in ipairs(VANILLA_FACTIONS) do
        if id:sub(1, #pair[1]) == pair[1] then return true, pair[2] end
    end
    return false, nil
end

local function journalOf(pid)
    if config.shareJournal == true then return nil end
    local data = Players[pid].data
    if type(data.journal) ~= "table" then data.journal = {} end
    return data.journal
end

local function isEntry(item)
    return item.type == enumerations.journal.ENTRY
end

local function questStats(journal, idSet)
    local stats = { records = 0, entries = 0, maxIndex = -1 }
    for _, item in pairs(journal) do
        if type(item) == "table" and idSet[lower(item.quest)] then
            stats.records = stats.records + 1
            if isEntry(item) then stats.entries = stats.entries + 1 end
            stats.maxIndex = math.max(stats.maxIndex, tonumber(item.index) or 0)
        end
    end
    return stats
end

local function validQuestId(id)
    return type(id) == "string" and id ~= "" and #id <= 64 and id:sub(1, 7) ~= "server:"
        and id:find("[%c]") == nil
end

-- ------------------------------------------------------------------ guards

local function guard(pid)
    local c = cfg()
    if c.enabled == false then return fail(pid, "disabled") end
    local s = state(pid)
    local now = os.time()
    if s.lastActionAt ~= nil and now - s.lastActionAt < (tonumber(c.cooldownSeconds) or 2) then
        return fail(pid, "busy")
    end
    s.lastActionAt = now
    return true
end

local function charge(pid, cost, reason)
    local have = points(pid)
    if have < cost then return fail(pid, "points", cost, have) end
    local ok, balance = spend(pid, cost, reason)
    if not ok then return fail(pid, "points", cost, have) end
    return true, balance
end

-- ------------------------------------------------------------------ actions

local function actionSkip(pid, fields)
    local questId = fields[3]
    local target = tonumber(fields[4])
    if not validQuestId(questId) then return fail(pid, "server_quest") end
    local journal = journalOf(pid)
    if journal == nil then return fail(pid, "shared") end
    local key = lower(questId)
    local s = state(pid)
    if s.skipped[key] then return fail(pid, "already") end
    local stats = questStats(journal, { [key] = true })
    if stats.records == 0 then return fail(pid, "unknown_quest") end
    if target == nil or target ~= math.floor(target) or target <= stats.maxIndex or target > 100000 then
        return fail(pid, "index")
    end
    local c = cfg()
    local isFaction, factionId = questFaction(questId)
    local cost = math.max(0, math.floor(tonumber(isFaction and c.skipFaction or c.skip) or 0))
    local ok, balance = charge(pid, cost, "questman skip " .. key)
    if not ok then return false end

    s.skipped[key] = true
    local reputation = 0
    if factionId ~= nil and config.shareFactionReputation ~= true then
        reputation = math.max(0, math.floor(tonumber(c.skipReputation) or 0))
        if reputation > 0 then
            local data = Players[pid].data
            if type(data.factionReputation) ~= "table" then data.factionReputation = {} end
            local stored = nil
            for existing, _ in pairs(data.factionReputation) do
                if lower(existing) == lower(factionId) then stored = existing end
            end
            stored = stored or lower(factionId)
            local value = (tonumber(data.factionReputation[stored]) or 0) + reputation
            data.factionReputation[stored] = value
            tes3mp.ClearFactionChanges(pid)
            tes3mp.SetFactionChangesAction(pid, enumerations.faction.REPUTATION)
            tes3mp.SetFactionId(stored)
            tes3mp.SetFactionReputation(value)
            tes3mp.AddFaction(pid)
            tes3mp.SendFactionChanges(pid)
        end
    end
    remember(pid, { action = "skip", quest = key, index = target, cost = cost, reputation = reputation })
    Players[pid]:QuicksaveToDrive()
    logInfo(Players[pid].name .. " skipped " .. key .. " -> " .. tostring(target) .. " for " .. cost .. " SP"
        .. (reputation > 0 and (", +" .. reputation .. " rep " .. factionId) or ""))
    -- The client writes the final journal entry itself and sends it back as an
    -- ordinary PlayerJournal packet: XP and party-XP validation run exactly as
    -- for a quest finished in dialogue.
    send(pid, "skip_ok", questId, target, cost, balance, reputation, factionId or "")
    return true
end

local function actionReset(pid, fields)
    local ids = {}
    local idSet = {}
    for id in tostring(fields[3] or ""):gmatch("[^,]+") do
        id = percentDecode(id)
        if not validQuestId(id) then return fail(pid, "server_quest") end
        if not idSet[lower(id)] then
            idSet[lower(id)] = true
            table.insert(ids, id)
        end
    end
    if #ids == 0 or #ids > 8 then return fail(pid, "unknown_quest") end
    local journal = journalOf(pid)
    if journal == nil then return fail(pid, "shared") end
    local stats = questStats(journal, idSet)
    if stats.records == 0 then return fail(pid, "unknown_quest") end
    local c = cfg()
    local large = stats.entries >= (tonumber(c.resetLargeFromEntries) or 3)
    local cost = math.max(0, math.floor(tonumber(large and c.resetLarge or c.resetSmall) or 0))
    local ok, balance = charge(pid, cost, "questman reset " .. table.concat(ids, ","))
    if not ok then return false end

    local kept = {}
    for _, item in ipairs(journal) do
        if not (type(item) == "table" and idSet[lower(item.quest)]) then table.insert(kept, item) end
    end
    Players[pid].data.journal = kept
    local s = state(pid)
    local hidden = {}
    for _, entry in ipairs(s.hidden) do
        local quest = tostring(entry):match("^(.*):%-?%d+$")
        if quest == nil or not idSet[lower(quest)] then table.insert(hidden, entry) end
    end
    s.hidden = hidden
    remember(pid, { action = "reset", quest = table.concat(ids, ","), cost = cost })
    Players[pid]:QuicksaveToDrive()
    logInfo(Players[pid].name .. " reset " .. table.concat(ids, ",") .. " for " .. cost .. " SP")
    send(pid, "reset_ok", table.concat(ids, ","), cost, balance)
    return true
end

local function actionClear(pid, fields)
    local journal = journalOf(pid)
    if journal == nil then return fail(pid, "shared") end
    -- Finished quests named by the client keep their finishing entry (so the
    -- quest stays "completed" after a relog); its text is hidden on login.
    local finished = {}
    for pair in tostring(fields[3] or ""):gmatch("[^;]+") do
        local quest, index = pair:match("^(.-):(%-?%d+)$")
        if quest ~= nil then finished[lower(percentDecode(quest))] = tonumber(index) end
    end
    local c = cfg()
    local cost = math.max(0, math.floor(tonumber(c.clearJournal) or 0))
    local ok, balance = charge(pid, cost, "questman clear journal")
    if not ok then return false end

    local order, best = {}, {}
    for _, item in ipairs(journal) do
        if type(item) == "table" and item.quest ~= nil then
            local key = lower(item.quest)
            if best[key] == nil then
                table.insert(order, key)
                best[key] = { quest = item.quest, maxIndex = tonumber(item.index) or 0 }
            end
            local b = best[key]
            b.maxIndex = math.max(b.maxIndex, tonumber(item.index) or 0)
            if isEntry(item) and finished[key] ~= nil and tonumber(item.index) == finished[key] then
                b.finishing = item
            end
        end
    end
    local rebuilt, hidden = {}, {}
    for _, key in ipairs(order) do
        local b = best[key]
        if b.finishing ~= nil then
            table.insert(rebuilt, b.finishing)
            table.insert(hidden, b.quest .. ":" .. tostring(b.finishing.index))
            if b.maxIndex > (tonumber(b.finishing.index) or 0) then
                table.insert(rebuilt, { type = enumerations.journal.INDEX, quest = b.quest, index = b.maxIndex })
            end
        else
            table.insert(rebuilt, { type = enumerations.journal.INDEX, quest = b.quest, index = b.maxIndex })
        end
    end
    Players[pid].data.journal = rebuilt
    state(pid).hidden = hidden
    remember(pid, { action = "clear", cost = cost, quests = #order })
    Players[pid]:QuicksaveToDrive()
    logInfo(Players[pid].name .. " cleared the journal (" .. #order .. " quests) for " .. cost .. " SP")
    send(pid, "clear_ok", cost, balance)
    return true
end

local function actionLeave(pid, fields)
    local faction = fields[3]
    if type(faction) ~= "string" or faction == "" or #faction > 64 then return fail(pid, "faction") end
    if config.shareFactionRanks == true then return fail(pid, "shared") end
    local data = Players[pid].data
    local stored = nil
    for existing, _ in pairs(data.factionRanks or {}) do
        if lower(existing) == lower(faction) then stored = existing end
    end
    if stored == nil then return fail(pid, "faction") end
    local c = cfg()
    local cost = math.max(0, math.floor(tonumber(c.leaveFaction) or 0))
    local ok, balance = charge(pid, cost, "questman leave " .. stored)
    if not ok then return false end

    data.factionRanks[stored] = nil
    for _, tableName in ipairs({ "factionExpulsion", "factionReputation" }) do
        if type(data[tableName]) == "table" and config.shareFactionReputation ~= true then
            for existing, _ in pairs(data[tableName]) do
                if lower(existing) == lower(stored) then data[tableName][existing] = nil end
            end
        end
    end
    remember(pid, { action = "leave", faction = stored, cost = cost })
    Players[pid]:QuicksaveToDrive()
    logInfo(Players[pid].name .. " left " .. stored .. " for " .. cost .. " SP")
    -- The client removes the membership locally (rank, expulsion flag and
    -- reputation) because the faction packet cannot express "no rank".
    send(pid, "leave_ok", stored, cost, balance)
    return true
end

-- ------------------------------------------------------------------ session

local function sendConfig(pid)
    local c = cfg()
    send(pid, "config", c.enabled == false and 0 or 1, c.resetSmall, c.resetLarge, c.resetLargeFromEntries,
        c.skip, c.skipFaction, c.skipReputation, c.clearJournal, c.leaveFaction,
        config.shareJournal == true and 1 or 0, config.shareFactionRanks == true and 1 or 0)
end

local function sendHidden(pid)
    local hidden = state(pid).hidden
    for first = 1, #hidden, HIDE_CHUNK do
        local part = {}
        for i = first, math.min(#hidden, first + HIDE_CHUNK - 1) do
            local quest, index = tostring(hidden[i]):match("^(.*):(%-?%d+)$")
            if quest ~= nil then table.insert(part, percentEncode(quest) .. ":" .. index) end
        end
        if #part > 0 then
            tes3mp.CustomMessageBox(pid, TRANSPORT_GUI_ID, REPLY .. "\thide\t" .. table.concat(part, ";"), "")
        end
    end
end

local ACTIONS = { skip = actionSkip, reset = actionReset, clear = actionClear, leave = actionLeave }

local function onGuiAction(eventStatus, pid, idGui, data)
    if idGui ~= TRANSPORT_GUI_ID or type(data) ~= "string" or data:sub(1, #CMD_PREFIX) ~= CMD_PREFIX then return end
    if not valid(pid) then return end
    local fields = splitTabs(data)
    local action = fields[2]
    if action == "config" then
        sendConfig(pid)
        return
    end
    local handler = ACTIONS[action]
    if handler == nil then return fail(pid, "bad") end
    if not guard(pid) then return end
    local ok, err = pcall(handler, pid, fields)
    if not ok then
        tes3mp.LogMessage(enumerations.log.ERROR, "[Questman] " .. tostring(action) .. " failed: " .. tostring(err))
        fail(pid, "bad")
    end
end

local function onPlayerFinishLogin(eventStatus, pid)
    if not valid(pid) then return end
    sendConfig(pid)
    sendHidden(pid)
end

function questmanService.Initialize()
    customEventHooks.registerHandler("OnGUIAction", onGuiAction)
    customEventHooks.registerHandler("OnPlayerFinishLogin", onPlayerFinishLogin)
    logInfo("Quest Manager services initialized")
end

-- exposed for tests and admin tooling
questmanService._questFaction = questFaction
questmanService._actions = ACTIONS

questmanService.Initialize()
return questmanService
