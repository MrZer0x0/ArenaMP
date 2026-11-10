local deathRecovery = {}
local nirnRestoreHealth = require("nirnRestoreHealth")

local CONTROL_PREFIX = "@@AMP_REVIVE@@"

local function clamp(value, low, high)
    value = tonumber(value) or low
    if value < low then return low end
    if value > high then return high end
    return value
end

local function cfgNumber(key, fallback)
    if type(config.deathRecovery) == "table" and config.deathRecovery[key] ~= nil then
        return tonumber(config.deathRecovery[key]) or fallback
    end
    return fallback
end

local function now()
    -- Y039: use the server monotonic clock so a rescue at e.g. 7.6 seconds
    -- settles the exact XP fraction instead of rounding to whole wall-clock seconds.
    if type(tes3mp.GetMillisecondsSinceServerStart) == "function" then
        return tes3mp.GetMillisecondsSinceServerStart() / 1000
    end
    return os.time()
end

local function stopTimer(player, field)
    if player ~= nil and player[field] ~= nil then
        tes3mp.StopTimer(player[field])
        player[field] = nil
    end
end

local function sendLevelSilently(player)
    player.data.stats.experience = math.max(0, tonumber(player.data.stats.experience) or 0)

    -- Y040: rebuild the complete PlayerLevel packet. Setting only the experience
    -- field left level/levelProgress/skillPoints/xpAttributeProgress at whatever
    -- the server had last written for this pid, and LocalPlayer::setLevel()
    -- applies every field of the packet it receives.
    if type(player.LoadLevel) == "function" then
        player:LoadLevel()
        return
    end

    tes3mp.SetExperience(player.pid, player.data.stats.experience)
    tes3mp.SendLevel(player.pid)
end

local function exactRemainingXp(player, atTime)
    local initial = math.max(0, tonumber(player.deathRecoveryInitialXp) or 0)
    local duration = math.max(0.1, tonumber(player.deathRecoveryDuration) or 30)
    local started = tonumber(player.deathRecoveryStartedAt) or atTime
    local elapsed = math.max(0, atTime - started)
    local fraction = math.max(0, 1 - elapsed / duration)
    return initial * fraction
end

local function applyXpAtTime(player, atTime, persist)
    if player == nil or player.deathRecoveryActive ~= true then return 0 end
    local nextXp = exactRemainingXp(player, atTime)
    if nextXp < 0.001 then nextXp = 0 end
    local oldXp = math.max(0, tonumber(player.data.stats.experience) or 0)
    if math.abs(oldXp - nextXp) >= 0.001 then
        player.data.stats.experience = nextXp
        sendLevelSilently(player)
        if persist and type(player.PersistXpProgress) == "function" then
            player:PersistXpProgress(true)
        end
    end
    return nextXp
end

local function scheduleTick(player)
    if player == nil or player.deathRecoveryActive ~= true then return end
    stopTimer(player, "deathRecoveryXpTimerId")
    local tickSeconds = clamp(cfgNumber("xp tick seconds", 1), 0.25, 2)
    player.deathRecoveryXpTimerId = tes3mp.CreateTimerEx(
        "OnDeathXpDecayTick", time.seconds(tickSeconds), "is", player.pid, player.accountName)
    tes3mp.StartTimer(player.deathRecoveryXpTimerId)
end

-- U035 --------------------------------------------------------------------
-- Getting up costs a lockout. After any successful recovery the player cannot
-- be brought back again for "revive lockout seconds"; dying inside that window
-- is a plain death with no incapacitated state at all.
--
-- The stamp is wall-clock (os.time) and lives in customVariables, not on the
-- session table: a monotonic server clock would restart with the server and a
-- session field would be cleared by a relog, which is the first thing anyone
-- tries.
----------------------------------------------------------------------------

local function reviveLockoutSeconds()
    return math.max(0, cfgNumber("revive lockout seconds", 60))
end

local function reviveLockoutRemaining(player)
    if player == nil or type(player.data) ~= "table" then return 0 end
    local limit = reviveLockoutSeconds()
    if limit <= 0 then return 0 end

    player.data.customVariables = player.data.customVariables or {}
    local last = tonumber(player.data.customVariables.ampLastRevive) or 0
    if last <= 0 then return 0 end

    local elapsed = os.time() - last
    -- A clock that jumped backwards (NTP, host migration) must not lock a
    -- player out forever.
    if elapsed < 0 then
        player.data.customVariables.ampLastRevive = os.time()
        return limit
    end
    return math.max(0, limit - elapsed)
end

local function stampReviveLockout(player)
    if player == nil or type(player.data) ~= "table" then return end
    player.data.customVariables = player.data.customVariables or {}
    player.data.customVariables.ampLastRevive = os.time()
end

-- Server -> client control message on the existing @@AMP_REVIVE@@ channel.
-- state: "OPEN" | "DENY" | "LOCK"
local function sendRecoveryState(player, state, seconds)
    if player == nil or not player:IsLoggedIn() then return end
    tes3mp.SendMessage(player.pid, CONTROL_PREFIX .. "STATE\t" .. tostring(state) ..
        "\t" .. string.format("%.1f", math.max(0, tonumber(seconds) or 0)) .. "\n", false)
end

function deathRecovery.LockoutRemaining(player)
    return reviveLockoutRemaining(player)
end

function deathRecovery.Begin(player)
    if player == nil then return 30 end

    deathRecovery.Cancel(player, false)

    -- U035: still under the lockout - no incapacitated window at all. The decay
    -- window IS the XP penalty, so with no window the penalty is settled in one
    -- go instead of being silently skipped.
    local lockoutLeft = reviveLockoutRemaining(player)
    if lockoutLeft > 0 then
        local xpAtDeath = math.max(0, tonumber(player.data.stats.experience) or 0)
        if xpAtDeath > 0 then
            player.data.stats.experience = 0
            sendLevelSilently(player)
            if type(player.PersistXpProgress) == "function" then
                player:PersistXpProgress(true)
            end
        end

        sendRecoveryState(player, "DENY", lockoutLeft)

        local minimumRespawn = 0
        if xpAtDeath <= 0 and type(config.xpLeveling) == "table" then
            local perLevel = tonumber(config.xpLeveling["zero xp death cooldown per level"]) or 0
            local currentLevel = math.max(1, tonumber(player.data.stats.level) or 1)
            minimumRespawn = math.max(0, perLevel) * currentLevel
        end
        return minimumRespawn
    end

    local duration = clamp(cfgNumber("xp decay seconds", 30), 3, 30)
    local currentXp = math.max(0, tonumber(player.data.stats.experience) or 0)

    player.deathRecoveryActive = true
    player.deathRecoveryStartedAt = now()
    player.deathRecoveryDuration = duration
    player.deathRecoveryInitialXp = currentXp

    -- The first tick is delayed; the client renders a smooth interpolation while
    -- these authoritative one-second checkpoints prevent reconnect/lag exploits.
    scheduleTick(player)

    sendRecoveryState(player, "OPEN", duration)

    -- Never auto-respawn before the XP decay window can finish.
    local minimumRespawn = duration
    if currentXp <= 0 and type(config.xpLeveling) == "table" then
        local perLevel = tonumber(config.xpLeveling["zero xp death cooldown per level"]) or 0
        local currentLevel = math.max(1, tonumber(player.data.stats.level) or 1)
        minimumRespawn = math.max(minimumRespawn, duration + math.max(0, perLevel) * currentLevel)
    end
    return minimumRespawn
end

function deathRecovery.Tick(pid, accountName)
    local player = Players and Players[pid] or nil
    if player == nil or not player:IsLoggedIn() or player.accountName ~= accountName
        or player.deathRecoveryActive ~= true then
        return
    end

    player.deathRecoveryXpTimerId = nil
    local remaining = applyXpAtTime(player, now(), true)
    if remaining > 0 then
        scheduleTick(player)
    else
        player.data.stats.experience = 0
        sendLevelSilently(player)
        if type(player.PersistXpProgress) == "function" then
            player:PersistXpProgress(true)
        end
        -- Keep deathRecoveryActive true until normal respawn or a recovery action;
        -- the recovery window is over, but the old zero-XP extra cooldown may remain.
    end
end

function deathRecovery.Cancel(player, settleXp)
    if player == nil then return end
    if settleXp == true and player.deathRecoveryActive == true then
        applyXpAtTime(player, now(), true)
    end
    stopTimer(player, "deathRecoveryXpTimerId")
    player.deathRecoveryActive = false
    player.deathRecoveryStartedAt = nil
    player.deathRecoveryDuration = nil
    player.deathRecoveryInitialXp = nil
end

local function inventoryCount(player, refId)
    if player == nil or type(player.data.inventory) ~= "table" or type(refId) ~= "string" or refId == "" then
        return 0
    end
    local count = 0
    for _, item in pairs(player.data.inventory) do
        if type(item) == "table" and item.refId == refId then
            count = count + math.max(0, tonumber(item.count) or 0)
        end
    end
    return count
end

local RESTORE_HEALTH_EFFECT_ID = 75

local function cfgList(key)
    if type(config.deathRecovery) == "table" and type(config.deathRecovery[key]) == "table" then
        return config.deathRecovery[key]
    end
    return {}
end

local function cfgBoolean(key, fallback)
    if type(config.deathRecovery) == "table" and type(config.deathRecovery[key]) == "boolean" then
        return config.deathRecovery[key]
    end
    return fallback
end

local function recordRestoreHealthStatus(refId)
    -- U023: return a tri-state value so a base/mod potion that the client can
    -- inspect does not become indistinguishable from a server-known non-healing
    -- generated record. true = known Restore Health, false = known non-healing,
    -- nil = the server has no record/effect data for this refId.
    if RecordStores == nil or RecordStores["potion"] == nil then return nil end
    local data = RecordStores["potion"].data
    if type(data) ~= "table" then return nil end

    local record = nil
    if type(data.permanentRecords) == "table" then record = data.permanentRecords[refId] end
    if record == nil and type(data.generatedRecords) == "table" then record = data.generatedRecords[refId] end
    if record == nil then return nil end
    if type(record) ~= "table" or type(record.effects) ~= "table" then return false end

    for _, effect in pairs(record.effects) do
        if type(effect) == "table" and tonumber(effect.id) == RESTORE_HEALTH_EFFECT_ID then
            local magnitude = math.max(tonumber(effect.magnitudeMax) or 0, tonumber(effect.magnitudeMin) or 0)
            if magnitude > 0 then return true end
        end
    end
    return false
end

local function restoreHealthPotionStatus(refId)
    -- Y040/U023: the request itself is text, so keep all existing authoritative
    -- checks. nirnRestoreHealth and configured patterns remain trusted known
    -- healing IDs; RecordStores can also prove a generated potion either way.
    if type(refId) ~= "string" or refId == "" then return false end
    if not cfgBoolean("validate potion refIds", true) then return true end

    local lowered = string.lower(refId)
    if nirnRestoreHealth[lowered] then return true end
    for _, pattern in pairs(cfgList("restore health refId patterns")) do
        if type(pattern) == "string" and pattern ~= "" and string.match(lowered, pattern) ~= nil then
            return true
        end
    end

    return recordRestoreHealthStatus(refId)
end

local function isRestoreHealthPotion(refId)
    return restoreHealthPotionStatus(refId) == true
end

local function requiredPotionCount(target)
    if target == nil then return math.max(1, math.floor(cfgNumber("base potions required", 1))) end
    local base = math.max(1, math.floor(cfgNumber("base potions required", 1)))
    local levelsPerStep = math.max(1, math.floor(cfgNumber("levels per extra potion", 5)))
    local extraPerStep = math.max(0, math.floor(cfgNumber("extra potions per step", 1)))
    local level = math.max(1, math.floor(tonumber(target.data.stats.level) or 1))
    return base + math.floor(level / levelsPerStep) * extraPerStep
end

local function takePotions(player, preferredRefId, required)
    required = math.max(1, math.floor(tonumber(required) or 1))
    if player == nil or type(player.data.inventory) ~= "table" then return false, false end

    -- Build an ordered list so the client-selected potion is consumed first.
    -- Only that selected refId may use the U023 unknown-record fallback: the
    -- client has already resolved it as a real ESM::Potion with Restore Health,
    -- while the server still verifies that the exact refId is in inventory.
    local candidates = {}
    local seen = {}
    local function addCandidate(refId, allowUnknown)
        if type(refId) ~= "string" or refId == "" or seen[refId] then return end
        local status = restoreHealthPotionStatus(refId)
        if status ~= true and not (allowUnknown == true and status == nil) then return end
        local count = inventoryCount(player, refId)
        if count <= 0 then return end
        seen[refId] = true
        table.insert(candidates, { refId = refId, count = count, unknown = status == nil })
    end

    if type(preferredRefId) == "string" and preferredRefId ~= "" then
        local preferredStatus = restoreHealthPotionStatus(preferredRefId)
        if preferredStatus == false then
            tes3mp.LogMessage(enumerations.log.WARN, "deathRecovery: refused known non-healing revive item " ..
                tostring(preferredRefId) .. " from " .. tostring(player.accountName))
            return false, false
        end

        addCandidate(preferredRefId, preferredStatus == nil)
        if preferredStatus == nil and inventoryCount(player, preferredRefId) > 0 then
            tes3mp.LogMessage(enumerations.log.WARN, "deathRecovery: using 5% fallback for uninspectable healing potion " ..
                tostring(preferredRefId) .. " from " .. tostring(player.accountName))
        end
    end
    for _, item in pairs(player.data.inventory) do
        if type(item) == "table" then addCandidate(item.refId, false) end
    end

    local available = 0
    for _, entry in ipairs(candidates) do available = available + entry.count end
    if available < required then return false, false end

    local remaining = required
    local removals = {}
    local usedUnknownFallback = false
    for _, entry in ipairs(candidates) do
        if remaining <= 0 then break end
        local amount = math.min(remaining, entry.count)
        inventoryHelper.removeClosestItem(player.data.inventory, entry.refId, amount)
        table.insert(removals, { refId = entry.refId, count = amount })
        if amount > 0 and entry.unknown == true then usedUnknownFallback = true end
        remaining = remaining - amount
    end

    tableHelper.cleanNils(player.data.inventory)
    player:LoadItemChanges(removals, enumerations.inventory.REMOVE)
    player:Save()
    return true, usedUnknownFallback
end

local function findPidByCharacterName(name)
    if type(name) ~= "string" or name == "" or Players == nil then return nil end
    for pid, player in pairs(Players) do
        if player ~= nil and player:IsLoggedIn() and tes3mp.GetName(pid) == name then
            return pid
        end
    end
    return nil
end

local function nearby(pidA, pidB)
    if pidA == nil or pidB == nil then return false end

    -- Y040: interior and exterior cells share the same local coordinate ranges, so
    -- a pure distance test let a player in one interior revive a party member lying
    -- in a completely different cell with similar coordinates.
    local cellA = tes3mp.GetCell(pidA)
    local cellB = tes3mp.GetCell(pidB)
    if type(cellA) ~= "string" or type(cellB) ~= "string" or cellA ~= cellB then
        return false
    end

    local maxDistance = clamp(cfgNumber("ally revive distance", 256), 96, 768)
    local dx = tes3mp.GetPosX(pidA) - tes3mp.GetPosX(pidB)
    local dy = tes3mp.GetPosY(pidA) - tes3mp.GetPosY(pidB)
    local dz = tes3mp.GetPosZ(pidA) - tes3mp.GetPosZ(pidB)
    return dx * dx + dy * dy + dz * dz <= maxDistance * maxDistance
end

local function recoverTarget(target, sourcePid, kind)
    if target == nil or target.deathRecoveryActive ~= true or (bountyRespawn and bountyRespawn.Pending(target)) then return false end

    -- Settle the exact XP at the instant of rescue before stopping decay.
    applyXpAtTime(target, now(), true)
    stopTimer(target, "deathRecoveryXpTimerId")

    local fractionKey = "potion revive health fraction"
    local defaultFraction = 0.25
    if kind == "touch" then
        fractionKey = "touch revive health fraction"
        defaultFraction = 0.10
    elseif kind == "potion_fallback" then
        fractionKey = "unknown potion revive health fraction"
        defaultFraction = 0.05
    end
    local fraction = clamp(cfgNumber(fractionKey, defaultFraction), 0.01, 1)
    local healthBase = math.max(1, tonumber(target.data.stats.healthBase) or 1)
    local restoredHealth = math.max(1, healthBase * fraction)

    target.deathRecoveryActive = false
    target.deathRecoveryStartedAt = nil
    target.deathRecoveryDuration = nil
    target.deathRecoveryInitialXp = nil

    -- U035: every way back on to your feet starts the lockout, including an
    -- ally rescue. If only self-revives were stamped, a duo would trade
    -- rescues and the cooldown would never apply to either of them.
    stampReviveLockout(target)
    sendRecoveryState(target, "LOCK", reviveLockoutSeconds())

    if target.resurrectTimerId ~= nil then
        tes3mp.StopTimer(target.resurrectTimerId)
        target.resurrectTimerId = nil
    end

    -- Recovery is intentionally not BasePlayer:Resurrect(): no shrine teleport and
    -- no death-jail penalty. The player gets back up exactly where they fell.
    if target.data.shapeshift ~= nil and target.data.shapeshift.isWerewolf == true then
        target:SetWerewolfState(false)
    end
    contentFixer.UnequipDeadlyItems(target.pid)
    tes3mp.Resurrect(target.pid, enumerations.resurrect.REGULAR)
    target.data.stats.healthCurrent = restoredHealth
    target:LoadStatsDynamic()
    target:Save()

    local helperName = sourcePid ~= nil and logicHandler.GetChatName(sourcePid) or ""
    local source = kind == "touch" and "healing touch" or "Restore Health potion"
    -- U035: say the lockout out loud, otherwise the next death looks like a bug.
    local lockoutNote = ""
    if reviveLockoutSeconds() > 0 then
        lockoutNote = string.format(" No second recovery for %d s.", math.ceil(reviveLockoutSeconds()))
    end
    if helperName ~= "" and sourcePid ~= target.pid then
    else
        -- Y040: the wording used to claim a potion even on the touch path.
    end
    return true
end

local function parseControl(message)
    if type(message) ~= "string" or message:sub(1, #CONTROL_PREFIX) ~= CONTROL_PREFIX then
        return nil
    end
    local payload = message:sub(#CONTROL_PREFIX + 1)
    local fields = {}
    for part in string.gmatch(payload .. "\t", "([^\t]*)\t") do
        table.insert(fields, part)
    end
    return fields
end

local function controlValidator(eventStatus, pid, message)
    local fields = parseControl(message)
    if fields == nil then return nil end

    local action = fields[1] or ""
    local player = Players and Players[pid] or nil
    if player == nil or not player:IsLoggedIn() then
        return customEventHooks.makeEventStatus(false, false)
    end

    if action == "STATE" then
        -- Server -> client direction only; a client must never inject it.
        return customEventHooks.makeEventStatus(false, false)

    elseif action == "SELF_POTION" then
        local refId = fields[2] or ""
        if player.deathRecoveryActive == true and not (bountyRespawn and bountyRespawn.Pending(player)) and refId ~= ""
            and reviveLockoutRemaining(player) <= 0 then
            local consumed, fallback = takePotions(player, refId, requiredPotionCount(player))
            if consumed then
                recoverTarget(player, pid, fallback and "potion_fallback" or "potion")
            end
        end

    elseif action == "ALLY_POTION" or action == "TOUCH" then
        local targetName = fields[2] or ""
        local targetPid = findPidByCharacterName(targetName)
        local target = targetPid ~= nil and Players[targetPid] or nil
        if target ~= nil and target.deathRecoveryActive == true and not (bountyRespawn and bountyRespawn.Pending(target))
            and reviveLockoutRemaining(target) <= 0
            and targetPid ~= pid and player.deathRecoveryActive ~= true
            and not (bountyRespawn and bountyRespawn.Pending(player))
            and (tonumber(player.data.stats.healthCurrent) or 0) > 0
            and nearby(pid, targetPid) then
            if action == "ALLY_POTION" then
                local refId = fields[3] or ""
                if refId ~= "" then
                    local consumed, fallback = takePotions(player, refId, requiredPotionCount(target))
                    if consumed then
                        recoverTarget(target, pid, fallback and "potion_fallback" or "potion")
                    end
                end
            else
                -- The client only emits TOUCH after a successful Restore Health
                -- effect actually resolves against the incapacitated player.
                recoverTarget(target, pid, "touch")
            end
        end
    end

    -- Recovery controls are transport, never visible player chat.
    return customEventHooks.makeEventStatus(false, false)
end

local function levelValidator(eventStatus, pid, playerPacket)
    local player = Players and Players[pid] or nil
    if player == nil or player.deathRecoveryActive ~= true
        or type(playerPacket) ~= "table" or type(playerPacket.stats) ~= "table" then
        return nil
    end

    -- Y057 anti-abuse: while incapacitated the server owns the complete
    -- progression snapshot. The client may only report a lower XP value caused
    -- by the death-recovery decay. Level, level progress, Skill Points,
    -- attribute XP progress and reward keys are restored from persisted data.
    -- This prevents repeatedly spending/recreating Skill Points while lying
    -- unconscious, including from modified clients.
    local stored = player.data and player.data.stats or {}
    local ceiling = exactRemainingXp(player, now())

    playerPacket.stats.level = stored.level or playerPacket.stats.level
    playerPacket.stats.levelProgress = stored.levelProgress or 0
    playerPacket.stats.skillPoints = stored.skillPoints or 0
    playerPacket.stats.xpAttributeProgress = stored.xpAttributeProgress or {0, 0, 0, 0, 0, 0, 0, 0}
    playerPacket.stats.xpRewardKeys = stored.xpRewardKeys or {}
    playerPacket.stats.experience = math.min(
        math.max(0, tonumber(playerPacket.stats.experience) or 0), ceiling)

    return nil
end

local function skillValidator(eventStatus, pid, playerPacket)
    local player = Players and Players[pid] or nil
    if player == nil or player.deathRecoveryActive ~= true then
        return nil
    end

    -- Y057 anti-abuse: no base skill change is legal while unconscious. Reject
    -- the packet before SaveSkills() and immediately restore the authoritative
    -- skills + Skill Points on the client. This also closes the exploit for
    -- clients that manage to keep/open the inventory or progression UI.
    tes3mp.LogAppend(enumerations.log.WARN,
        "[AntiAbuse] Blocked PlayerSkill while incapacitated for " ..
        logicHandler.GetChatName(pid))

    if type(player.LoadSkills) == "function" then
        player:LoadSkills()
    end
    if type(player.LoadLevel) == "function" then
        player:LoadLevel()
    end

    return customEventHooks.makeEventStatus(false, false)
end

customEventHooks.registerValidator("OnPlayerLevel", levelValidator)
customEventHooks.registerValidator("OnPlayerSkill", skillValidator)
customEventHooks.registerValidator("OnPlayerSendMessage", controlValidator)
customEventHooks.registerHandler("OnPlayerDisconnect", function(_, pid)
    local player = Players and Players[pid] or nil
    if player ~= nil then deathRecovery.Cancel(player, true) end
end)

return deathRecovery
