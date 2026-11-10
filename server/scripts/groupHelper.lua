-- ArenaMP server-authoritative group core, integrated with the player menu.
--
-- Responsibilities:
--   * persistent groups, leader/member management and invitations;
--   * independent journal/topic synchronization toggles;
--   * same-cell XP splitting for validated kill/quest rewards, including
--     kills landed by a member's summon;
--   * registering group members as native allies so the engine's own
--     "friendly fire mode = group" rule covers the party;
--   * friendly summon protection for the owner and the whole party;
--   * hidden state protocol consumed by GUIChat's Group tab.

local groupHelper = {}

local DATA_PATH = "custom/groupHelper.json"
local STATE_PREFIX = "@@AMP_GROUP@@"
local XP_PREFIX = "@@AMP_XP@@"
local XP_SIGNAL_PREFIX = "amp-xp|v1|"
local GROUP_INVITE_GUI = 20560001
local GROUP_INVITE_NOTICE_GUI = 20560002

local cfg = {
    inviteLifetime = 120,
    xpSignalLifetime = 8,
    xpEventLifetime = 8,
    maxKillXpSignal = 25000,
    maxQuestXpSignal = 25000,
    protectSummons = true,
    summonCheckInterval = 2000,
    summonStopCombatCooldown = 2,
    -- Sending a console StopCombat to a summon during a cell transition can
    -- race the client's UpdateSummonedCreatures cleanup.  Keep the legacy
    -- backstop available, but require an explicit opt-in from the server
    -- configuration instead of emitting this packet by default.
    summonStopCombat = false,
    summonLegacyStopCombatTick = false,
    debugMode = false,
    invitePopups = true,
    nativeAllies = true,
    summonKillXp = true,
    summonXpEventLifetime = 20
}

if type(config.groupSystem) == "table" then
    cfg.inviteLifetime = tonumber(config.groupSystem["invite lifetime seconds"]) or cfg.inviteLifetime
    cfg.xpSignalLifetime = tonumber(config.groupSystem["xp signal lifetime seconds"]) or cfg.xpSignalLifetime
    cfg.xpEventLifetime = cfg.xpSignalLifetime
    cfg.maxKillXpSignal = tonumber(config.groupSystem["max client kill xp signal"]) or cfg.maxKillXpSignal
    cfg.maxQuestXpSignal = tonumber(config.groupSystem["max client quest xp signal"]) or cfg.maxQuestXpSignal
    if config.groupSystem["summon protection"] ~= nil then cfg.protectSummons = config.groupSystem["summon protection"] end
    cfg.summonCheckInterval = tonumber(config.groupSystem["summon check interval ms"]) or cfg.summonCheckInterval
    if config.groupSystem["summon stopcombat"] ~= nil then
        cfg.summonStopCombat = config.groupSystem["summon stopcombat"]
    end
    if config.groupSystem["summon legacy stopcombat tick"] ~= nil then
        cfg.summonLegacyStopCombatTick = config.groupSystem["summon legacy stopcombat tick"]
    end
    if config.groupSystem["invite popups"] ~= nil then cfg.invitePopups = config.groupSystem["invite popups"] end
    if config.groupSystem["native allies"] ~= nil then cfg.nativeAllies = config.groupSystem["native allies"] end
    if config.groupSystem["summon kill xp"] ~= nil then cfg.summonKillXp = config.groupSystem["summon kill xp"] end
    cfg.summonXpEventLifetime = tonumber(config.groupSystem["summon xp event lifetime seconds"]) or cfg.summonXpEventLifetime
end

local data = { version = 1, nextId = 1, groups = {} }
local pendingInvites = {}
local pendingXpSignals = {}
local recentXpEvents = {}
local strippedSignalPids = {}
local summonCombatStopCache = {}

local function now()
    return os.time()
end

local function log(level, message)
    ampCore.Log(level, message)
end

local function lower(value)
    return tostring(value or ""):lower()
end

local function trim(value)
    value = tostring(value or "")
    return value:match("^%s*(.-)%s*$") or ""
end

local function safeToken(value, maxLength)
    value = trim(value)
    value = value:gsub("[%c\t]", " ")
    value = value:gsub("[;%^]", " ")
    value = value:gsub("%s+", " ")
    if maxLength ~= nil and #value > maxLength then value = value:sub(1, maxLength) end
    return value
end

local function escapeField(value)
    value = tostring(value or "")
    value = value:gsub("\\", "\\\\")
    value = value:gsub("\t", "\\t")
    value = value:gsub("\r", "")
    value = value:gsub("\n", "\\n")
    return value
end

local function isValidPid(pid)
    return Players[pid] ~= nil and Players[pid]:IsLoggedIn() and Players[pid].data ~= nil
end

local function accountName(pid)
    if not isValidPid(pid) then return nil end
    return tostring(Players[pid].accountName or Players[pid].name or "")
end

local function displayName(pid)
    if not isValidPid(pid) then return "?" end
    return tostring(Players[pid].name or Players[pid].accountName or ("PID " .. tostring(pid)))
end

local function persistPlayer(pid)
    if not isValidPid(pid) then return end
    if type(Players[pid].QuicksaveToDrive) == "function" then
        Players[pid]:QuicksaveToDrive()
    elseif type(Players[pid].SaveToDrive) == "function" then
        Players[pid]:SaveToDrive()
    elseif type(Players[pid].Save) == "function" then
        Players[pid]:Save()
    end
end

local function ensurePlayerPrefs(pid)
    if not isValidPid(pid) then return nil end
    Players[pid].data.customVariables = Players[pid].data.customVariables or {}
    local root = Players[pid].data.customVariables
    root.arenampGroup = root.arenampGroup or {}
    local prefs = root.arenampGroup
    if prefs.journalSync == nil then prefs.journalSync = true end
    if prefs.topicSync == nil then prefs.topicSync = true end
    return prefs
end

local function loadData()
    -- EnsureJson writes the default registry when the file is not there yet,
    -- which is what stops io2 from printing "Cannot open custom/groupHelper.json
    -- in mode r" on every fresh install.
    local loaded = ampCore.EnsureJson(DATA_PATH, { version = 1, nextId = 1, groups = {} }, DATA_PATH)
    if type(loaded) == "table" then
        data = loaded
    end
    data.version = 1
    data.nextId = tonumber(data.nextId) or 1
    data.groups = type(data.groups) == "table" and data.groups or {}
end

local function saveData()
    ampCore.SaveJson(DATA_PATH, data, DATA_PATH)
end

local function getGroupById(id)
    if id == nil then return nil end
    return data.groups[tostring(id)]
end

function groupHelper.GetPlayerGroupId(pid)
    if not isValidPid(pid) then return nil end
    local prefs = ensurePlayerPrefs(pid)
    local id = prefs and tostring(prefs.id or "") or ""
    if id == "" then return nil end
    local group = getGroupById(id)
    local account = accountName(pid)
    if group == nil or type(group.members) ~= "table" or group.members[account] == nil then
        prefs.id = nil
        persistPlayer(pid)
        return nil
    end
    return id
end

function groupHelper.GetPlayerGroup(pid)
    return getGroupById(groupHelper.GetPlayerGroupId(pid))
end

function groupHelper.ArePlayersInSameGroup(pid1, pid2)
    local a = groupHelper.GetPlayerGroupId(pid1)
    local b = groupHelper.GetPlayerGroupId(pid2)
    return a ~= nil and a == b
end

function groupHelper.IsLeader(pid)
    local group = groupHelper.GetPlayerGroup(pid)
    return group ~= nil and lower(group.leader) == lower(accountName(pid))
end

local function findOnlinePidByAccount(account)
    account = lower(account)
    for pid, player in pairs(Players) do
        if player ~= nil and player:IsLoggedIn() and lower(player.accountName or player.name) == account then
            return pid
        end
    end
    return nil
end

local function findOnlinePidByName(name)
    name = lower(trim(name))
    if name == "" then return nil end
    for pid, player in pairs(Players) do
        if player ~= nil and player:IsLoggedIn() then
            if lower(player.name) == name or lower(player.accountName) == name then return pid end
        end
    end
    return nil
end

local function groupMemberCount(group)
    local count = 0
    for _ in pairs(group and group.members or {}) do count = count + 1 end
    return count
end

local function onlineGroupPids(group)
    local result = {}
    if group == nil then return result end
    for account in pairs(group.members or {}) do
        local pid = findOnlinePidByAccount(account)
        if pid ~= nil then table.insert(result, pid) end
    end
    return result
end

function groupHelper.GetGroupMembers(pid, sameCellOnly)
    local group = groupHelper.GetPlayerGroup(pid)
    if group == nil then return {} end
    local result = {}
    local cell = sameCellOnly and tes3mp.GetCell(pid) or nil
    for _, memberPid in ipairs(onlineGroupPids(group)) do
        if not sameCellOnly or tes3mp.GetCell(memberPid) == cell then table.insert(result, memberPid) end
    end
    table.sort(result)
    return result
end

local function isLeader(pid, group)
    return group ~= nil and lower(group.leader) == lower(accountName(pid))
end

local function updateMemberDisplayName(pid, group)
    if group == nil then return end
    local account = accountName(pid)
    if account == nil then return end
    group.members = group.members or {}
    group.members[account] = group.members[account] or {}
    group.members[account].name = displayName(pid)
end

-- Z005: групповой войс.
--
-- Раздачей голоса занимается C++: он сравнивает у говорящего и слушателя одно
-- число и, если оно совпадает и не равно нулю, отправляет кадр независимо от
-- расстояния и ячейки. Группы живут в Lua и опознаются строкой, поэтому строку
-- надо свернуть в число.
--
-- Считаем обычным полиномиальным хешем на умножении и остатке: сервер собран
-- с LuaJIT (это Lua 5.1), где побитовых операторов языка ещё нет, а тянуть
-- сюда bit-библиотеку ради одной свёртки незачем. Ноль зарезервирован под
-- «вне группы», поэтому нулевой результат поднимаем до единицы.
local VOICE_GROUP_MODULUS = 2147483629  -- наибольшее простое ниже 2^31

local function voiceGroupNumber(id)
    if type(id) ~= "string" or id == "" then return 0 end
    local hash = 2166136261 % VOICE_GROUP_MODULUS
    for i = 1, #id do
        hash = (hash * 131 + id:byte(i)) % VOICE_GROUP_MODULUS
    end
    if hash == 0 then return 1 end
    return hash
end

local function applyVoiceGroup(pid, id)
    if type(tes3mp.SetPlayerVoiceGroup) ~= "function" then return end
    if not isValidPid(pid) then return end
    pcall(tes3mp.SetPlayerVoiceGroup, pid, voiceGroupNumber(id))
end

local function setPlayerGroup(pid, id)
    local prefs = ensurePlayerPrefs(pid)
    if prefs == nil then return end
    prefs.id = id
    applyVoiceGroup(pid, id)
    persistPlayer(pid)
end

local function clearPlayerGroup(pid)
    local prefs = ensurePlayerPrefs(pid)
    if prefs == nil then return end
    prefs.id = nil
    applyVoiceGroup(pid, nil)
    persistPlayer(pid)
end

-- Native ally registration -------------------------------------------------
--
-- The engine already knows how to suppress player-versus-player damage: the
-- server runs with "friendly fire mode = group", and MechanicsFunctions
-- resolves that against Player::alliedPlayers. Nothing was ever writing group
-- membership into that list, though -- it was only ever fed by the legacy
-- /ally and /invite commands -- so party members happily damaged each other
-- while the mode claimed otherwise. Mirroring the roster into
-- data.alliedPlayers and calling LoadAllies closes that gap, and it is also
-- what lets the client resolve a summon's owner to a friendly player.
--
-- Entries added here are tracked separately in the player's group prefs so a
-- later refresh removes exactly what the group system added and leaves any
-- manually added ally alone.

local function trackedNativeAllies(pid)
    local prefs = ensurePlayerPrefs(pid)
    if prefs == nil then return nil end
    if type(prefs.nativeAllies) ~= "table" then prefs.nativeAllies = {} end
    return prefs
end

local function applyNativeAllies(pid, desiredAccounts)
    if not cfg.nativeAllies or not isValidPid(pid) then return false end
    local prefs = trackedNativeAllies(pid)
    if prefs == nil then return false end

    local player = Players[pid]
    if type(player.data.alliedPlayers) ~= "table" then player.data.alliedPlayers = {} end
    local allied = player.data.alliedPlayers
    local selfAccount = lower(accountName(pid) or "")

    local desired = {}
    for _, account in ipairs(desiredAccounts or {}) do
        local key = lower(account)
        if key ~= "" and key ~= selfAccount then desired[key] = account end
    end

    local changed = false

    for _, previous in ipairs(prefs.nativeAllies) do
        if desired[lower(previous)] == nil and tableHelper.containsValue(allied, previous) then
            tableHelper.removeValue(allied, previous)
            tableHelper.cleanNils(allied)
            changed = true
        end
    end

    local tracked = {}
    for _, account in pairs(desired) do
        table.insert(tracked, account)
        if not tableHelper.containsValue(allied, account) then
            table.insert(allied, account)
            changed = true
        end
    end
    table.sort(tracked)
    prefs.nativeAllies = tracked

    if changed then persistPlayer(pid) end

    -- LoadAllies rebuilds the native list from data.alliedPlayers and pushes a
    -- PlayerAlly packet to everyone, so it runs even when the stored list did
    -- not change: the set of *online* allies may still have.
    if type(player.LoadAllies) == "function" then
        local ok, err = pcall(function() player:LoadAllies() end)
        if not ok then
            log(enumerations.log.ERROR, "could not publish party allies for pid " .. tostring(pid) .. ": " .. tostring(err))
        end
    end
    return changed
end

local function refreshNativeAllies(group, alsoRefreshPids)
    if not cfg.nativeAllies then return end
    local accounts = {}
    if group ~= nil then
        for account in pairs(group.members or {}) do table.insert(accounts, account) end
        for _, memberPid in ipairs(onlineGroupPids(group)) do
            applyNativeAllies(memberPid, accounts)
        end
    end
    -- Players who just left, were kicked or were disbanded still hold stale
    -- native allies until they are cleared explicitly.
    for _, pid in ipairs(alsoRefreshPids or {}) do
        if isValidPid(pid) then applyNativeAllies(pid, {}) end
    end
end

local function generateGroupId()
    local id = tostring(now()) .. "-" .. tostring(data.nextId)
    data.nextId = data.nextId + 1
    return id
end

local function groupChatNotificationsEnabled()
    return type(config.groupSystem) ~= "table" or config.groupSystem["chat notifications"] ~= false
end

local function groupXpChatMessagesEnabled()
    return type(config.groupSystem) == "table" and config.groupSystem["xp share chat messages"] == true
end

local function sendNotice(pid, text, errorState)
    if not groupChatNotificationsEnabled() or not isValidPid(pid) then return end
    local prefix = errorState and color.Red or color.Turquoise
    tes3mp.SendMessage(pid, prefix .. "[Group] " .. color.White .. tostring(text) .. "\n", false)
end

local function buildMembersField(group, viewerPid)
    local items = {}
    local viewerCell = isValidPid(viewerPid) and tes3mp.GetCell(viewerPid) or ""
    local accounts = {}
    for account in pairs(group.members or {}) do table.insert(accounts, account) end
    table.sort(accounts, function(a, b)
        local aLeader = lower(a) == lower(group.leader)
        local bLeader = lower(b) == lower(group.leader)
        if aLeader ~= bLeader then return aLeader end
        local an = lower((group.members[a] or {}).name or a)
        local bn = lower((group.members[b] or {}).name or b)
        return an < bn
    end)

    for _, account in ipairs(accounts) do
        local member = group.members[account] or {}
        local pid = findOnlinePidByAccount(account)
        local online = pid ~= nil
        local sameCell = online and viewerCell ~= "" and tes3mp.GetCell(pid) == viewerCell
        local leader = lower(account) == lower(group.leader)
        local name = safeToken(member.name or account, 48)
        table.insert(items, name .. "^" .. (online and "1" or "0") .. "^" .. (sameCell and "1" or "0") .. "^" .. (leader and "1" or "0"))
    end
    return table.concat(items, ";")
end

-- X052: everyone online except the viewer, so the Player Menu can offer an
-- invite list instead of asking the leader to type a name correctly.
--   name ^ hasAnyGroup ^ inViewersGroup ^ sameCell
local function buildRosterField(viewerPid)
    if not isValidPid(viewerPid) then return "" end
    local viewerGroup = groupHelper.GetPlayerGroup(viewerPid)
    local viewerCell = tes3mp.GetCell(viewerPid)
    local items = {}

    for pid, player in pairs(Players) do
        if player ~= nil and player:IsLoggedIn() and pid ~= viewerPid then
            local theirGroup = groupHelper.GetPlayerGroup(pid)
            local inViewersGroup = viewerGroup ~= nil and theirGroup ~= nil and
                tostring(theirGroup.id) == tostring(viewerGroup.id)
            local sameCell = viewerCell ~= nil and tes3mp.GetCell(pid) == viewerCell
            table.insert(items, safeToken(displayName(pid), 48) .. "^" ..
                (theirGroup ~= nil and "1" or "0") .. "^" ..
                (inViewersGroup and "1" or "0") .. "^" ..
                (sameCell and "1" or "0"))
        end
    end

    table.sort(items)
    return table.concat(items, ";")
end

function groupHelper.SendState(pid)
    if not isValidPid(pid) then return end
    local prefs = ensurePlayerPrefs(pid)
    local group = groupHelper.GetPlayerGroup(pid)
    local account = accountName(pid)
    -- Z005: после переподключения группа восстанавливается из сохранённых
    -- настроек, а число для войса на сервере обнулено — вернуть его здесь.
    applyVoiceGroup(pid, group ~= nil and prefs.id or nil)

    local invite = pendingInvites[lower(account)]
    if invite ~= nil and invite.expiresAt < now() then
        pendingInvites[lower(account)] = nil
        invite = nil
    end

    local fields = {
        "STATE",
        group ~= nil and "1" or "0",
        group ~= nil and isLeader(pid, group) and "1" or "0",
        prefs.journalSync and "1" or "0",
        prefs.topicSync and "1" or "0",
        invite ~= nil and "1" or "0",
        group ~= nil and escapeField(group.name) or "",
        group ~= nil and escapeField((group.members[group.leader] or {}).name or group.leader) or "",
        group ~= nil and escapeField(buildMembersField(group, pid)) or "",
        invite ~= nil and escapeField(invite.inviterName or invite.inviterAccount) or "",
        invite ~= nil and escapeField(invite.groupName or "") or "",
        escapeField(buildRosterField(pid))
    }
    tes3mp.SendMessage(pid, STATE_PREFIX .. table.concat(fields, "\t") .. "\n", false)
end

local function broadcastState(group)
    for _, pid in ipairs(onlineGroupPids(group)) do groupHelper.SendState(pid) end
    if groupMapHelper ~= nil and group ~= nil then groupMapHelper.BroadcastGroup(group.id) end
end

local function createGroup(pid, requestedName)
    if groupHelper.GetPlayerGroup(pid) ~= nil then return false, "You are already in a group" end
    local name = safeToken(requestedName, 32)
    if #name < 2 then return false, "Group name must contain at least 2 characters" end
    local id = generateGroupId()
    local account = accountName(pid)
    data.groups[id] = {
        id = id,
        name = name,
        leader = account,
        createdAt = now(),
        members = { [account] = { name = displayName(pid), joinedAt = now() } }
    }
    setPlayerGroup(pid, id)
    saveData()
    refreshNativeAllies(data.groups[id])
    return true, "Group created: " .. name
end

local function localizedGroupText(pid, ru, en)
    if type(localization) == "table" and type(localization.GetLanguage) == "function" then
        if tostring(localization.GetLanguage(pid) or "RU"):upper() == "EN" then return en end
    end
    return ru
end

local function notifyInviteResult(invite, targetPid, accepted)
    if invite == nil then return end
    local inviterPid = findOnlinePidByAccount(invite.inviterAccount)
    if inviterPid == nil then return end

    local targetName = isValidPid(targetPid) and displayName(targetPid) or tostring(invite.targetName or "Player")
    local message
    if accepted then
        message = localizedGroupText(inviterPid,
            targetName .. " принял приглашение в группу.",
            targetName .. " accepted the group invitation.")
    else
        message = localizedGroupText(inviterPid,
            targetName .. " отказался от приглашения в группу.",
            targetName .. " declined the group invitation.")
    end
    tes3mp.MessageBox(inviterPid, GROUP_INVITE_NOTICE_GUI, message)
end

local function showInvitePopup(targetPid, invite)
    if not cfg.invitePopups or not isValidPid(targetPid) or invite == nil then return end
    local seconds = math.max(0, math.floor((invite.expiresAt or now()) - now()))
    local message = localizedGroupText(targetPid,
        tostring(invite.inviterName or invite.inviterAccount) .. " приглашает вас в группу «" .. tostring(invite.groupName or "") .. "».\n" ..
        "Принять приглашение?\nСрок: " .. tostring(seconds) .. " сек.",
        tostring(invite.inviterName or invite.inviterAccount) .. " invites you to group '" .. tostring(invite.groupName or "") .. "'.\n" ..
        "Accept invitation?\nExpires in: " .. tostring(seconds) .. " sec.")
    local buttons = localizedGroupText(targetPid, "Простой вход;Синхронизация;Выход", "Join;Synchronize;Exit")
    tes3mp.CustomMessageBox(targetPid, GROUP_INVITE_GUI, message, buttons)
end

local function invitePlayer(pid, targetName)
    local group = groupHelper.GetPlayerGroup(pid)
    if group == nil then return false, "Create a group first" end
    if not isLeader(pid, group) then return false, "Only the group leader can invite players" end
    local targetPid = findOnlinePidByName(targetName)
    if targetPid == nil then return false, "Player not found or offline" end
    if targetPid == pid then return false, "You cannot invite yourself" end
    if groupHelper.GetPlayerGroup(targetPid) ~= nil then return false, "That player is already in a group" end

    local targetAccount = accountName(targetPid)
    local key = lower(targetAccount)
    local existing = pendingInvites[key]
    if existing ~= nil and existing.expiresAt >= now() then
        if tostring(existing.groupId) ~= tostring(group.id) then
            return false, localizedGroupText(pid,
                "У игрока уже есть активное приглашение от другой группы",
                "That player already has a pending invitation from another group")
        end
    end

    local invite = {
        groupId = group.id,
        groupName = group.name,
        inviterAccount = accountName(pid),
        inviterName = displayName(pid),
        targetName = displayName(targetPid),
        expiresAt = now() + cfg.inviteLifetime
    }
    pendingInvites[key] = invite
    groupHelper.SendState(targetPid)
    showInvitePopup(targetPid, invite)
    return true, "Invitation sent to " .. displayName(targetPid)
end

local function acceptInvite(pid)
    local key = lower(accountName(pid))
    local invite = pendingInvites[key]
    if groupHelper.GetPlayerGroup(pid) ~= nil then
        pendingInvites[key] = nil
        return false, "Leave your current group first"
    end
    if invite == nil or invite.expiresAt < now() then
        pendingInvites[key] = nil
        return false, "The invitation has expired"
    end
    local group = getGroupById(invite.groupId)
    if group == nil then
        pendingInvites[key] = nil
        return false, "The group no longer exists"
    end
    local account = accountName(pid)
    group.members[account] = { name = displayName(pid), joinedAt = now() }
    setPlayerGroup(pid, group.id)
    pendingInvites[key] = nil
    saveData()
    refreshNativeAllies(group)
    notifyInviteResult(invite, pid, true)
    broadcastState(group)
    return true, "Joined group: " .. group.name
end

local function declineInvite(pid)
    local key = lower(accountName(pid))
    local invite = pendingInvites[key]
    if invite == nil then return false, "There is no pending invitation" end
    if invite.expiresAt < now() then
        pendingInvites[key] = nil
        return false, "The invitation has expired"
    end
    pendingInvites[key] = nil
    notifyInviteResult(invite, pid, false)
    return true, "Invitation declined"
end

local function chooseNewLeader(group, excludingAccount)
    local accounts = {}
    for account in pairs(group.members or {}) do
        if lower(account) ~= lower(excludingAccount) then table.insert(accounts, account) end
    end
    table.sort(accounts, function(a, b)
        local aOnline = findOnlinePidByAccount(a) ~= nil
        local bOnline = findOnlinePidByAccount(b) ~= nil
        if aOnline ~= bOnline then return aOnline end
        return lower(a) < lower(b)
    end)
    return accounts[1]
end

local function leaveGroup(pid)
    local group = groupHelper.GetPlayerGroup(pid)
    if group == nil then return false, "You are not in a group" end
    local account = accountName(pid)
    local wasLeader = isLeader(pid, group)
    group.members[account] = nil
    clearPlayerGroup(pid)

    if groupMemberCount(group) == 0 then
        data.groups[group.id] = nil
    elseif wasLeader then
        group.leader = chooseNewLeader(group, account)
    end
    saveData()
    refreshNativeAllies(group, { pid })
    broadcastState(group)
    groupHelper.SendState(pid)
    if groupMapHelper ~= nil then groupMapHelper.SyncGroup(pid) end
    if groupMemberCount(group) == 0 and groupMapHelper ~= nil then groupMapHelper.DeleteGroup(group.id) end
    return true, "You left the group"
end

local function disbandGroup(pid)
    local group = groupHelper.GetPlayerGroup(pid)
    if group == nil then return false, "You are not in a group" end
    if not isLeader(pid, group) then return false, "Only the leader can disband the group" end
    local online = onlineGroupPids(group)
    for _, memberPid in ipairs(online) do clearPlayerGroup(memberPid) end
    data.groups[group.id] = nil
    saveData()
    refreshNativeAllies(nil, online)
    if groupMapHelper ~= nil then groupMapHelper.DeleteGroup(group.id) end
    for _, memberPid in ipairs(online) do
        groupHelper.SendState(memberPid)
        if groupMapHelper ~= nil then groupMapHelper.SyncGroup(memberPid) end
        sendNotice(memberPid, "The group was disbanded", false)
    end
    return true, nil
end

local function kickMember(pid, targetName)
    local group = groupHelper.GetPlayerGroup(pid)
    if group == nil then return false, "You are not in a group" end
    if not isLeader(pid, group) then return false, "Only the leader can remove members" end
    local targetPid = findOnlinePidByName(targetName)
    local targetAccount = targetPid and accountName(targetPid) or nil
    if targetAccount == nil then
        local needle = lower(trim(targetName))
        for account, member in pairs(group.members or {}) do
            if lower(account) == needle or lower(member.name) == needle then targetAccount = account break end
        end
    end
    if targetAccount == nil or group.members[targetAccount] == nil then return false, "Group member not found" end
    if lower(targetAccount) == lower(group.leader) then return false, "Transfer leadership before removing the leader" end
    local targetDisplay = (group.members[targetAccount] or {}).name or targetAccount
    group.members[targetAccount] = nil
    if targetPid ~= nil then
        clearPlayerGroup(targetPid)
        groupHelper.SendState(targetPid)
        if groupMapHelper ~= nil then groupMapHelper.SyncGroup(targetPid) end
        sendNotice(targetPid, "You were removed from " .. group.name, true)
    end
    saveData()
    refreshNativeAllies(group, targetPid ~= nil and { targetPid } or {})
    broadcastState(group)
    return true, targetDisplay .. " removed from the group"
end

local function transferLeader(pid, targetName)
    local group = groupHelper.GetPlayerGroup(pid)
    if group == nil then return false, "You are not in a group" end
    if not isLeader(pid, group) then return false, "Only the leader can transfer leadership" end
    local targetPid = findOnlinePidByName(targetName)
    local targetAccount = targetPid and accountName(targetPid) or nil
    if targetAccount == nil or group.members[targetAccount] == nil then return false, "Choose an online group member" end
    if targetPid == pid then return false, "You are already the leader" end
    group.leader = targetAccount
    updateMemberDisplayName(targetPid, group)
    saveData()
    broadcastState(group)
    return true, displayName(targetPid) .. " is now the group leader"
end

local reconcileJournalForGroup
local reconcileTopicsForGroup

local function toggleSync(pid, key)
    if groupHelper.GetPlayerGroup(pid) == nil then return false, "You are not in a group" end
    local prefs = ensurePlayerPrefs(pid)
    prefs[key] = not prefs[key]
    persistPlayer(pid)
    local group = groupHelper.GetPlayerGroup(pid)
    if prefs[key] and group ~= nil then
        if key == "journalSync" then reconcileJournalForGroup(group) else reconcileTopicsForGroup(group) end
    end
    groupHelper.SendState(pid)
    return true, (key == "journalSync" and "Journal" or "Topics") .. " synchronization " .. (prefs[key] and "enabled" or "disabled")
end

local function journalEntryKey(entry)
    return tostring(entry.type or "") .. "|" .. lower(entry.quest) .. "|" .. tostring(entry.index or "")
end

local function mergeJournal(targetPid, changes)
    if not isValidPid(targetPid) or type(changes) ~= "table" then return false end
    local targetJournal = Players[targetPid].data.journal
    if type(targetJournal) ~= "table" then
        targetJournal = {}
        Players[targetPid].data.journal = targetJournal
    end
    local known = {}
    for _, entry in pairs(targetJournal) do
        if type(entry) == "table" then known[journalEntryKey(entry)] = true end
    end
    local changed = false
    for _, entry in pairs(changes) do
        if type(entry) == "table" then
            local key = journalEntryKey(entry)
            if not known[key] then
                table.insert(targetJournal, tableHelper.deepCopy(entry))
                known[key] = true
                changed = true
            end
        end
    end
    if changed then
        persistPlayer(targetPid)
        Players[targetPid]:LoadJournal()
    end
    return changed
end

local function mergeTopics(targetPid, sourceTopics)
    if not isValidPid(targetPid) or type(sourceTopics) ~= "table" then return false end
    local target = Players[targetPid].data.topics
    if type(target) ~= "table" then target = {}; Players[targetPid].data.topics = target end
    local changed = false

    if #sourceTopics > 0 then
        for _, topic in ipairs(sourceTopics) do
            if not tableHelper.containsValue(target, topic) then
                table.insert(target, tableHelper.deepCopy(topic))
                changed = true
            end
        end
    else
        for key, value in pairs(sourceTopics) do
            if target[key] == nil then
                target[key] = tableHelper.deepCopy(value)
                changed = true
            end
        end
    end

    if changed then
        persistPlayer(targetPid)
        Players[targetPid]:LoadTopics()
    end
    return changed
end

function groupHelper.SyncJournalChanges(pid, playerPacket)
    local group = groupHelper.GetPlayerGroup(pid)
    local prefs = ensurePlayerPrefs(pid)
    if group == nil or prefs == nil or not prefs.journalSync then return end
    local changes = playerPacket and playerPacket.journal or {}
    if type(changes) ~= "table" or next(changes) == nil then return end
    for _, targetPid in ipairs(onlineGroupPids(group)) do
        if targetPid ~= pid then
            local targetPrefs = ensurePlayerPrefs(targetPid)
            if targetPrefs ~= nil and targetPrefs.journalSync then mergeJournal(targetPid, changes) end
        end
    end
end

function groupHelper.SyncTopics(pid)
    local group = groupHelper.GetPlayerGroup(pid)
    local prefs = ensurePlayerPrefs(pid)
    if group == nil or prefs == nil or not prefs.topicSync then return end
    local topics = Players[pid].data.topics or {}
    for _, targetPid in ipairs(onlineGroupPids(group)) do
        if targetPid ~= pid then
            local targetPrefs = ensurePlayerPrefs(targetPid)
            if targetPrefs ~= nil and targetPrefs.topicSync then mergeTopics(targetPid, topics) end
        end
    end
end

reconcileJournalForGroup = function(group)
    if group == nil then return end
    local union, known = {}, {}
    local eligible = {}
    for _, memberPid in ipairs(onlineGroupPids(group)) do
        local prefs = ensurePlayerPrefs(memberPid)
        if prefs ~= nil and prefs.journalSync then
            table.insert(eligible, memberPid)
            for _, entry in pairs(Players[memberPid].data.journal or {}) do
                if type(entry) == "table" then
                    local key = journalEntryKey(entry)
                    if not known[key] then
                        known[key] = true
                        table.insert(union, tableHelper.deepCopy(entry))
                    end
                end
            end
        end
    end
    for _, memberPid in ipairs(eligible) do mergeJournal(memberPid, union) end
end

reconcileTopicsForGroup = function(group)
    if group == nil then return end
    local union, seen = {}, {}
    local eligible = {}
    for _, memberPid in ipairs(onlineGroupPids(group)) do
        local prefs = ensurePlayerPrefs(memberPid)
        if prefs ~= nil and prefs.topicSync then
            table.insert(eligible, memberPid)
            local topics = Players[memberPid].data.topics or {}
            if #topics > 0 then
                for _, topic in ipairs(topics) do
                    local key = lower(topic)
                    if not seen[key] then seen[key] = true; table.insert(union, tableHelper.deepCopy(topic)) end
                end
            else
                for key, value in pairs(topics) do
                    local normalized = lower(key)
                    if not seen[normalized] then seen[normalized] = true; union[key] = tableHelper.deepCopy(value) end
                end
            end
        end
    end
    for _, memberPid in ipairs(eligible) do mergeTopics(memberPid, union) end
end

local function sameCellRecipients(originPid, cellDescription)
    local result = {}
    local group = groupHelper.GetPlayerGroup(originPid)
    if group == nil then
        if isValidPid(originPid) then table.insert(result, originPid) end
        return result
    end
    for _, pid in ipairs(onlineGroupPids(group)) do
        if isValidPid(pid) and tes3mp.GetCell(pid) == cellDescription then table.insert(result, pid) end
    end
    if #result == 0 and isValidPid(originPid) then table.insert(result, originPid) end
    table.sort(result)
    return result
end

local function sendXpControl(pid, amount, scaled, kind, reason)
    amount = tonumber(amount) or 0
    if not isValidPid(pid) or amount == 0 then return end
    local payload = XP_PREFIX .. string.format("%.6f", amount) .. "\t" .. (scaled and "1" or "0") .. "\t" ..
        escapeField(kind or "system") .. "\t" .. escapeField(reason or "") .. "\n"
    tes3mp.SendMessage(pid, payload, false)
end

-- Arena Y012: server-authoritative negative XP changes use the same hidden
-- control channel as group rewards, so the client renders them in the XP HUD
-- lane without duplicating the actual stat mutation locally.
function groupHelper.SendXpAdjustment(pid, amount, kind)
    if not isValidPid(pid) then return false end
    sendXpControl(pid, tonumber(amount) or 0, false, kind or "system", "")
    return true
end

local function dispatchSharedXp(originPid, amount, scaled, kind, reason, recipients)
    amount = tonumber(amount) or 0
    if amount <= 0 or not isValidPid(originPid) then return false end
    recipients = recipients or sameCellRecipients(originPid, tes3mp.GetCell(originPid))
    if #recipients == 0 then recipients = { originPid } end
    local share = amount / #recipients
    for _, pid in ipairs(recipients) do
        sendXpControl(pid, share, scaled, kind, reason)
    end
    if groupXpChatMessagesEnabled() and #recipients > 1 then
        for _, pid in ipairs(recipients) do
            sendNotice(pid, string.format("Shared %s XP: %.1f each (%d members in cell)", kind or "group", share, #recipients), false)
        end
    end
    return true
end

function groupHelper.AwardServerQuestXp(pid, amount, reason)
    if not isValidPid(pid) then return false end
    return dispatchSharedXp(pid, tonumber(amount) or 0, false, "quest", reason or "Quest reward",
        sameCellRecipients(pid, tes3mp.GetCell(pid)))
end

local function parseXpSignal(key)
    if type(key) ~= "string" or key:sub(1, #XP_SIGNAL_PREFIX) ~= XP_SIGNAL_PREFIX then return nil end
    local parts = {}
    for part in key:gmatch("[^|]+") do table.insert(parts, part) end
    -- amp-xp|v1|kind|amount|quest|index|nonce
    if #parts < 5 or parts[1] ~= "amp-xp" or parts[2] ~= "v1" then return nil end
    local signal = {
        kind = parts[3],
        amount = tonumber(parts[4]),
        quest = tostring(parts[5] or ""),
        index = tonumber(parts[6] or "0") or 0,
        nonce = tostring(parts[7] or "")
    }
    if signal.amount == nil or signal.amount <= 0 then return nil end
    if signal.kind == "kill" then
        if signal.amount > cfg.maxKillXpSignal then return nil end
    elseif signal.kind == "quest" then
        if signal.amount > cfg.maxQuestXpSignal then return nil end
    else
        return nil
    end
    return signal
end

local function pruneList(list, lifetime, summonLifetime)
    local current = now()
    local cutoff = current - lifetime
    local summonCutoff = current - (summonLifetime or lifetime)
    local kept = {}
    for _, item in ipairs(list or {}) do
        local limit = item.viaSummon and summonCutoff or cutoff
        if (item.time or 0) >= limit then table.insert(kept, item) end
    end
    return kept
end

local function signalMatchesEvent(signal, event)
    if signal.kind ~= event.kind then return false end
    if signal.kind == "quest" then
        return lower(signal.quest) == lower(event.quest) and tonumber(signal.index) == tonumber(event.index)
    end
    return true
end

local function tryMatchXp(pid)
    local dispatched = false
    pendingXpSignals[pid] = pruneList(pendingXpSignals[pid], cfg.xpSignalLifetime)
    -- A kill credited through a summon is reported by whichever client holds
    -- authority over the dying actor, while the matching reward signal only
    -- arrives with the owner's next level packet. Those two can land a few
    -- seconds apart, so summon events get a longer window than direct ones.
    recentXpEvents[pid] = pruneList(recentXpEvents[pid], cfg.xpEventLifetime, cfg.summonXpEventLifetime)
    local signals = pendingXpSignals[pid] or {}
    local events = recentXpEvents[pid] or {}
    if #signals == 0 or #events == 0 then return false end

    local remainingSignals = {}
    for _, signal in ipairs(signals) do
        local matchedIndex = nil
        for index, event in ipairs(events) do
            if not event.used and signalMatchesEvent(signal, event) then matchedIndex = index break end
        end
        if matchedIndex ~= nil then
            local event = events[matchedIndex]
            event.used = true
            -- The client localizes kill/quest reasons from the kind. Never put
            -- raw server-authored English reason text into the HUD payload.
            if dispatchSharedXp(pid, signal.amount, true, signal.kind, "", event.recipients) then
                dispatched = true
            end
        else
            table.insert(remainingSignals, signal)
        end
    end
    pendingXpSignals[pid] = remainingSignals
    recentXpEvents[pid] = events
    return dispatched
end

function groupHelper.PreprocessPlayerLevel(pid, playerPacket)
    if not isValidPid(pid) or playerPacket == nil or playerPacket.stats == nil then return false end
    local keys = playerPacket.stats.xpRewardKeys
    if type(keys) ~= "table" then return false end
    local filtered = {}
    local consumed = false
    for _, key in ipairs(keys) do
        local signal = parseXpSignal(key)
        if signal ~= nil then
            consumed = true
            signal.time = now()
            pendingXpSignals[pid] = pendingXpSignals[pid] or {}
            table.insert(pendingXpSignals[pid], signal)
        else
            table.insert(filtered, key)
        end
    end
    if consumed then
        playerPacket.stats.xpRewardKeys = filtered
        strippedSignalPids[pid] = true
    end
    return consumed
end

function groupHelper.ProcessPendingXp(pid)
    if not isValidPid(pid) then return false end
    return tryMatchXp(pid)
end

function groupHelper.RebuildOutgoingXpKeysIfNeeded(pid)
    if not strippedSignalPids[pid] or not isValidPid(pid) then return false end
    tes3mp.ClearXpRewardKeys(pid)
    for _, key in ipairs(Players[pid].data.stats.xpRewardKeys or {}) do tes3mp.AddXpRewardKey(pid, key) end
    strippedSignalPids[pid] = nil
    return true
end

local getSummonOwnerPid

function groupHelper.RecordKillEvents(pid, cellDescription, actors)
    for _, actor in pairs(actors or {}) do
        local killerPid = actor.killer and actor.killer.pid or nil
        local viaSummon = false

        -- A kill landed by a summon is credited to the player who summoned it,
        -- and from there to the party through sameCellRecipients, exactly as if
        -- the owner had swung the weapon themselves.
        if killerPid == nil and actor.killer ~= nil and actor.killer.uniqueIndex ~= nil and getSummonOwnerPid ~= nil then
            killerPid = getSummonOwnerPid(cellDescription, actor.killer.uniqueIndex)
            viaSummon = killerPid ~= nil
        end

        if viaSummon and not cfg.summonKillXp then killerPid = nil end

        if killerPid ~= nil and isValidPid(killerPid) then
            recentXpEvents[killerPid] = recentXpEvents[killerPid] or {}
            table.insert(recentXpEvents[killerPid], {
                kind = "kill", time = now(), cell = cellDescription,
                viaSummon = viaSummon,
                recipients = sameCellRecipients(killerPid, cellDescription)
            })
            if viaSummon and cfg.debugMode then
                log(enumerations.log.INFO, "credited a summon kill in " .. tostring(cellDescription) ..
                    " to " .. displayName(killerPid))
            end
            tryMatchXp(killerPid)
        end
    end
end

function groupHelper.RecordJournalEvents(pid, playerPacket)
    if not isValidPid(pid) then return end
    local cell = tes3mp.GetCell(pid)
    for _, entry in pairs(playerPacket and playerPacket.journal or {}) do
        local quest = tostring(entry.quest or "")
        local index = tonumber(entry.index)
        if quest ~= "" and index ~= nil and index > 0 then
            recentXpEvents[pid] = recentXpEvents[pid] or {}
            table.insert(recentXpEvents[pid], {
                kind = "quest", time = now(), quest = quest, index = index, cell = cell,
                recipients = sameCellRecipients(pid, cell)
            })
        end
    end
    tryMatchXp(pid)
end

local function makeUID(refNum, mpNum)
    return tostring(refNum) .. "-" .. tostring(mpNum)
end

getSummonOwnerPid = function(cellDescription, uniqueIndex)
    if uniqueIndex == nil then return nil end

    -- Fast path: cell/base.lua already keeps a reverse index on the player
    -- (Players[pid].summons[uniqueIndex] = refId) when a summon is spawned,
    -- so a scan of the cell's object data is usually unnecessary and, more
    -- importantly, still works while the cell is being swapped between
    -- authorities.
    for pid, player in pairs(Players) do
        if player ~= nil and player:IsLoggedIn() and type(player.summons) == "table"
            and player.summons[uniqueIndex] ~= nil then
            return pid
        end
    end

    local cell = LoadedCells[cellDescription]
    if cell == nil or cell.data == nil or cell.data.objectData == nil then return nil end
    local obj = cell.data.objectData[uniqueIndex]
    local ownerName = obj and obj.summon and obj.summon.summoner and obj.summon.summoner.playerName or nil
    if ownerName == nil then return nil end
    for pid, player in pairs(Players) do
        if player ~= nil and player:IsLoggedIn() and lower(player.accountName) == lower(ownerName) then return pid end
    end
    return nil
end

--- Resolve any actor to the player it fights for, if there is one.
-- Returns nil for ordinary NPCs and wildlife, which must keep their normal
-- combat rules.
function groupHelper.GetActorOwnerPid(cellDescription, uniqueIndex)
    return getSummonOwnerPid(cellDescription, uniqueIndex)
end

--- True when two actors/players should never damage one another.
function groupHelper.AreFriendly(pidA, pidB)
    if pidA == nil or pidB == nil then return false end
    if pidA == pidB then return true end
    return groupHelper.ArePlayersInSameGroup(pidA, pidB)
end

function groupHelper.IsFriendlySummon(pid, cellDescription, uniqueIndex)
    if not cfg.protectSummons or not isValidPid(pid) then return false end
    local ownerPid = getSummonOwnerPid(cellDescription, uniqueIndex)
    if ownerPid == nil then return false end
    return ownerPid == pid or groupHelper.ArePlayersInSameGroup(pid, ownerPid)
end

local function stopSummonCombat(cellDescription, uid, targetPid)
    if not cfg.summonStopCombat then return false end
    if not isValidPid(targetPid) or LoadedCells[cellDescription] == nil then return false end
    local cacheKey = cellDescription .. "|" .. uid .. "|" .. tostring(targetPid)
    local current = now()
    if summonCombatStopCache[cacheKey] ~= nil and current - summonCombatStopCache[cacheKey] < cfg.summonStopCombatCooldown then
        return false
    end
    local split = uid:split("-")
    if split[1] == nil or split[2] == nil then return false end
    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(targetPid)
    tes3mp.SetObjectListCell(cellDescription)
    tes3mp.SetObjectListConsoleCommand("StopCombat")
    tes3mp.SetObjectRefNum(tonumber(split[1]))
    tes3mp.SetObjectMpNum(tonumber(split[2]))
    tes3mp.AddObject()
    tes3mp.SendConsoleCommand(false, false)
    summonCombatStopCache[cacheKey] = current
    return true
end

function GroupHelper_SummonTick()
    -- X057: this sweep is legacy compatibility only. The client-side
    -- MechanicsHelper protection rejects friendly summon combat before aggro is
    -- created, while OnObjectHit below remains a reactive server backstop for
    -- mismatched/older clients. Running this unconditionally caused an endless
    -- StopCombat command every scan interval even when the summon was idle.
    if not cfg.protectSummons or not cfg.summonLegacyStopCombatTick then
        return
    end

    if cfg.protectSummons then
        for cellDescription, cell in pairs(LoadedCells) do
            if cell ~= nil and cell.data ~= nil and type(cell.data.objectData) == "table" then
                for uid, obj in pairs(cell.data.objectData) do
                    if obj.summon and obj.summon.summoner and obj.summon.summoner.playerName then
                        local ownerPid = getSummonOwnerPid(cellDescription, uid)
                        if ownerPid ~= nil then
                            for pid, player in pairs(Players) do
                                if player ~= nil and player:IsLoggedIn() and tes3mp.GetCell(pid) == cellDescription and
                                    (pid == ownerPid or groupHelper.ArePlayersInSameGroup(pid, ownerPid)) then
                                    stopSummonCombat(cellDescription, uid, pid)
                                end
                            end
                        end
                    end
                end
            end
        end
    end
    local cutoff = now() - 10
    for key, stamp in pairs(summonCombatStopCache) do if stamp < cutoff then summonCombatStopCache[key] = nil end end
    tes3mp.StartTimer(tes3mp.CreateTimerEx("GroupHelper_SummonTick", cfg.summonCheckInterval, "i", 0))
end

local function reconcilePlayer(pid)
    local prefs = ensurePlayerPrefs(pid)
    local account = accountName(pid)
    local group = prefs.id and getGroupById(prefs.id) or nil
    if group ~= nil and group.members ~= nil and group.members[account] ~= nil then
        updateMemberDisplayName(pid, group)
        saveData()
        return
    end
    -- Recover membership from the durable registry if the player-side copy was lost.
    for id, candidate in pairs(data.groups) do
        if candidate.members ~= nil and candidate.members[account] ~= nil then
            prefs.id = id
            updateMemberDisplayName(pid, candidate)
            persistPlayer(pid)
            saveData()
            return
        end
    end
    prefs.id = nil
    persistPlayer(pid)
end

local function processCommand(pid, cmd)
    if not isValidPid(pid) then return end
    local sub = lower(cmd[2] or "state")
    local ok, message = true, nil
    if sub == "state" or sub == "refresh" then
        groupHelper.SendState(pid)
        return
    elseif sub == "create" then
        ok, message = createGroup(pid, tableHelper.concatenateFromIndex(cmd, 3))
    elseif sub == "invite" then
        ok, message = invitePlayer(pid, tableHelper.concatenateFromIndex(cmd, 3))
    elseif sub == "accept" then
        ok, message = acceptInvite(pid)
    elseif sub == "decline" then
        ok, message = declineInvite(pid)
    elseif sub == "leave" then
        ok, message = leaveGroup(pid)
    elseif sub == "disband" then
        ok, message = disbandGroup(pid)
    elseif sub == "kick" then
        ok, message = kickMember(pid, tableHelper.concatenateFromIndex(cmd, 3))
    elseif sub == "leader" or sub == "promote" then
        ok, message = transferLeader(pid, tableHelper.concatenateFromIndex(cmd, 3))
    elseif sub == "journal" then
        ok, message = toggleSync(pid, "journalSync")
    elseif sub == "topics" then
        ok, message = toggleSync(pid, "topicSync")
    elseif sub == "roster" then
        groupHelper.SendState(pid)
        return
    else
        ok, message = false, "Unknown group action"
    end
    if message ~= nil then sendNotice(pid, message, not ok) end
    groupHelper.SendState(pid)
    local group = groupHelper.GetPlayerGroup(pid)
    if group ~= nil then broadcastState(group) end
end

customCommandHooks.registerCommand("groupui", processCommand)
customCommandHooks.registerCommand("group", processCommand)

customEventHooks.registerHandler("OnServerPostInit", function(eventStatus)
    loadData()
    if cfg.protectSummons and cfg.summonLegacyStopCombatTick then
        tes3mp.StartTimer(tes3mp.CreateTimerEx("GroupHelper_SummonTick", cfg.summonCheckInterval, "i", 0))
    end
    log(enumerations.log.INFO, "loaded: groups, journal/topics sync, shared XP, summon protection")
end)

customEventHooks.registerHandler("OnPlayerAuthentified", function(eventStatus, pid)
    if isValidPid(pid) then
        reconcilePlayer(pid)
        local group = groupHelper.GetPlayerGroup(pid)
        local prefs = ensurePlayerPrefs(pid)
        if group ~= nil and prefs ~= nil then
            if prefs.journalSync then reconcileJournalForGroup(group) end
            if prefs.topicSync then reconcileTopicsForGroup(group) end
        end
        refreshNativeAllies(group)
        groupHelper.SendState(pid)
        local invite = pendingInvites[lower(accountName(pid))]
        if invite ~= nil and invite.expiresAt >= now() then showInvitePopup(pid, invite) end
    end
end)

customEventHooks.registerHandler("OnGUIAction", function(eventStatus, pid, idGui, dataValue)
    if idGui ~= GROUP_INVITE_GUI or not isValidPid(pid) then return end

    local choice = tonumber(dataValue) or -1
    local ok, message

    -- 0: обычный вход в группу.
    -- 1: вход + явное включение синхронизации (только по отдельной кнопке).
    -- 2: отказ.
    -- Никакой синхронизации не включается автоматически при принятии приглашения.
    if choice == 0 then
        ok, message = acceptInvite(pid)
    elseif choice == 1 then
        ok, message = acceptInvite(pid)
        if ok then
            local prefs = ensurePlayerPrefs(pid)
            if prefs ~= nil then
                prefs.journalSync = true
                prefs.topicSync = true
                saveData()
            end
            message = localizedGroupText(pid, "Вы вошли в группу. Синхронизация включена.", "You joined the group. Synchronization enabled.")
        end
    else
        ok, message = declineInvite(pid)
    end

    if message ~= nil then sendNotice(pid, message, not ok) end
    groupHelper.SendState(pid)
    local group = groupHelper.GetPlayerGroup(pid)
    if group ~= nil then broadcastState(group) end
end)

customEventHooks.registerHandler("OnPlayerDisconnect", function(eventStatus, pid)
    local group = groupHelper.GetPlayerGroup(pid)
    pendingXpSignals[pid] = nil
    recentXpEvents[pid] = nil
    strippedSignalPids[pid] = nil
    -- Refresh the remaining members so the departing player stops being
    -- published as a native ally; LoadAllies only republishes online players,
    -- so the stored roster itself is left intact for the next login.
    if group ~= nil then refreshNativeAllies(group) end
end)

customEventHooks.registerHandler("OnPlayerJournal", function(eventStatus, pid, playerPacket)
    if eventStatus.validDefaultHandler and isValidPid(pid) then
        groupHelper.RecordJournalEvents(pid, playerPacket)
        groupHelper.SyncJournalChanges(pid, playerPacket)
    end
end)

customEventHooks.registerHandler("OnPlayerTopic", function(eventStatus, pid)
    if eventStatus.validDefaultHandler and isValidPid(pid) then groupHelper.SyncTopics(pid) end
end)

customEventHooks.registerHandler("OnActorDeath", function(eventStatus, pid, cellDescription, actors)
    if eventStatus.validDefaultHandler then groupHelper.RecordKillEvents(pid, cellDescription, actors) end
end)

-- Server-side backstop for friendly damage on summons.
--
-- The authoritative fix lives on the client: MechanicsHelper resolves a summon
-- to its owning player and refuses the hit before any damage, aggro or hit
-- attempt is recorded. This validator stays because the server cannot assume
-- every connected client is running the matching build, and because a hit
-- packet that did get through must not be relayed to everyone else.
customEventHooks.registerValidator("OnObjectHit", function(eventStatus, pid, cellDescription)
    if not cfg.protectSummons or not isValidPid(pid) then return end
    tes3mp.ReadReceivedObjectList()
    for index = 0, tes3mp.GetObjectListSize() - 1 do
        if not tes3mp.IsObjectPlayer(index) then
            local uid = makeUID(tes3mp.GetObjectRefNum(index), tes3mp.GetObjectMpNum(index))
            if groupHelper.IsFriendlySummon(pid, cellDescription, uid) then
                stopSummonCombat(cellDescription, uid, pid)
                eventStatus.validDefaultHandler = false
                return eventStatus
            end
        end
    end
end)

return groupHelper
