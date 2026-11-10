-- ArenaMP Y067 persistent progression anti-cheat (CharGen-safe V2).
--
-- Design goals:
--   * the server profile is authoritative; client stat packets are only proposals;
--   * keep a level-1 baseline and a rolling trusted snapshot + checksum;
--   * use generous plausibility margins to avoid false positives;
--   * first anomaly restores the last trusted snapshot;
--   * only a repeated anomaly on a non-legacy profile may rebuild to level 1;
--   * a rebuild refunds only server-confirmed skill-point investment, never the
--     suspicious values that triggered the anti-cheat;
--   * Y092-SP: skillPoints are bounded by server-known progression on every
--     PlayerLevel packet. The client may spend points, but cannot create them.

local arenaAntiCheat = {}

local STATE_KEY = "ArenaMPAntiCheatV2"
local LEGACY_STATE_KEY = "ArenaMPAntiCheatV1"
local SCHEMA_VERSION = 2
local MAX_LEVEL = 150
local BASE_STAT_CAP = 105
local HARD_STAT_CAP = 110
local SKILL_POINTS_PER_LEVEL = 10
local SKILL_BUDGET_TOLERANCE = 10
local ATTRIBUTE_BUDGET_TOLERANCE = 10
local HEALTH_PER_LEVEL_ALLOWANCE = 15
local HEALTH_FLAT_TOLERANCE = 25
local MAX_SINGLE_STAT_DELTA = 25
local MAX_SINGLE_HEALTH_BASE_DELTA = 250
local HARD_RECOVERY_STRIKES = 2
local TRUSTED_PERSIST_INTERVAL = 30
local MIN_BASELINE_ATTRIBUTES = 8
local MIN_BASELINE_SKILLS = 20

-- Runtime strikes deliberately do not persist. A reconnect cannot turn one
-- transient desync into an automatic level-1 rebuild.
local runtime = {}
local hooksRegistered = false

local function now()
    if os ~= nil and type(os.time) == "function" then return os.time() end
    return 0
end

local function log(level, text)
    if tes3mp == nil then return end
    if type(tes3mp.LogAppend) == "function" and enumerations ~= nil and enumerations.log ~= nil then
        tes3mp.LogAppend(level or enumerations.log.WARN, "[ArenaMP Anti-Cheat] " .. text)
    elseif type(tes3mp.LogMessage) == "function" then
        tes3mp.LogMessage(level or 2, "[ArenaMP Anti-Cheat] " .. text)
    end
end

local function playerName(pid)
    if logicHandler ~= nil and type(logicHandler.GetChatName) == "function" then
        return logicHandler.GetChatName(pid)
    end
    return "pid=" .. tostring(pid)
end

local function detectCoopPreset()
    if type(config) ~= "table" then return false end
    if type(config.coopResetPolicy) == "table" and config.coopResetPolicy.forceCoop ~= nil then
        return config.coopResetPolicy.forceCoop == true
    end
    local mode = string.lower(tostring(config.gameMode or ""))
    if string.find(mode, "co%-op") or string.find(mode, "coop", 1, true) then return true end
    local shared = 0
    if config.shareJournal == true then shared = shared + 1 end
    if config.shareTopics == true then shared = shared + 1 end
    if config.shareFactionRanks == true then shared = shared + 1 end
    if config.shareMapExploration == true then shared = shared + 1 end
    return shared >= 3
end

local function isEnabled()
    local settings = type(config) == "table" and config.arenaAntiCheat or nil
    if type(settings) == "table" and settings.enabled == false then return false end
    local disableInCoop = type(settings) ~= "table" or settings.disableInCoop ~= false
    if disableInCoop and detectCoopPreset() then return false end
    return true
end

local function notify(pid, hard, refund)
    -- Y072: deliberately silent client-side. Rollback and strikes still happen;
    -- exact reasons are written only to the dedicated-server log for admins.
    return
end

local function copyMapOfBases(source)
    local out = {}
    if type(source) ~= "table" then return out end
    for key, value in pairs(source) do
        if type(value) == "table" then
            local base = tonumber(value.base)
            if base ~= nil then out[tostring(key)] = base end
        end
    end
    return out
end

local function readFirstNumber(data, names)
    local containers = {data, data and data.stats, data and data.customVariables}
    for _, container in ipairs(containers) do
        if type(container) == "table" then
            for _, name in ipairs(names) do
                local value = tonumber(container[name])
                if value ~= nil then return value, container, name end
            end
        end
    end
    return nil, nil, nil
end

local function setFirstNumber(data, names, value)
    local _, container, name = readFirstNumber(data, names)
    if container ~= nil and name ~= nil then
        container[name] = value
        return true
    end
    return false
end

local function getLevel(data)
    if type(data) ~= "table" then return 1 end
    if type(data.stats) == "table" and tonumber(data.stats.level) ~= nil then
        return tonumber(data.stats.level)
    end
    if tonumber(data.level) ~= nil then return tonumber(data.level) end
    return 1
end

local function setLevel(data, value)
    if type(data.stats) == "table" and data.stats.level ~= nil then data.stats.level = value
    elseif data.level ~= nil then data.level = value
    else
        data.stats = data.stats or {}
        data.stats.level = value
    end
end

local function getHealthBase(data)
    if type(data) ~= "table" or type(data.stats) ~= "table" then return 0 end
    return tonumber(data.stats.healthBase) or 0
end

local function getLevelProgress(data)
    if type(data) ~= "table" or type(data.stats) ~= "table" then return 0 end
    return tonumber(data.stats.levelProgress) or 0
end

local function snapshotPlayer(player)
    local data = player and player.data or {}
    local xp = readFirstNumber(data, {"experience", "characterXp", "characterXP", "xp"})
    local points = readFirstNumber(data, {"skillPoints", "skillpoints", "skill_points", "xpSkillPoints", "unspentSkillPoints", "skillPointPool"})
    return {
        attributes = copyMapOfBases(data.attributes),
        skills = copyMapOfBases(data.skills),
        level = math.floor(getLevel(data) + 0.5),
        levelProgress = getLevelProgress(data),
        healthBase = getHealthBase(data),
        healthCurrent = tonumber(data.stats and data.stats.healthCurrent) or 0,
        magickaBase = tonumber(data.stats and data.stats.magickaBase) or 0,
        magickaCurrent = tonumber(data.stats and data.stats.magickaCurrent) or 0,
        fatigueBase = tonumber(data.stats and data.stats.fatigueBase) or 0,
        fatigueCurrent = tonumber(data.stats and data.stats.fatigueCurrent) or 0,
        experience = xp,
        skillPoints = points,
    }
end

local function mapCount(values)
    local count = 0
    for _ in pairs(values or {}) do count = count + 1 end
    return count
end

local function snapshotLooksComplete(snapshot)
    return type(snapshot) == "table" and
        mapCount(snapshot.attributes) >= MIN_BASELINE_ATTRIBUTES and
        mapCount(snapshot.skills) >= MIN_BASELINE_SKILLS and
        (tonumber(snapshot.level) or 0) >= 1 and
        (tonumber(snapshot.healthBase) or 0) > 1
end

local function sortedKeys(t)
    local keys = {}
    for key in pairs(t or {}) do table.insert(keys, key) end
    table.sort(keys)
    return keys
end

local function canonical(snapshot)
    local parts = {
        "v=" .. tostring(SCHEMA_VERSION),
        "lvl=" .. string.format("%.0f", tonumber(snapshot.level) or 0),
        "lp=" .. string.format("%.4f", tonumber(snapshot.levelProgress) or 0),
        "hp=" .. string.format("%.4f", tonumber(snapshot.healthBase) or 0),
        "xp=" .. string.format("%.4f", tonumber(snapshot.experience) or -1),
        "sp=" .. string.format("%.0f", tonumber(snapshot.skillPoints) or -1),
    }
    -- Y088: old V2 snapshots did not contain dynamic current/base values.  Only
    -- append the new fields when they exist, so their historic checksums remain
    -- verifiable and can be migrated without trusting client state again.
    if snapshot.healthCurrent ~= nil or snapshot.magickaBase ~= nil or snapshot.magickaCurrent ~= nil
        or snapshot.fatigueBase ~= nil or snapshot.fatigueCurrent ~= nil then
        table.insert(parts, "hpc=" .. string.format("%.4f", tonumber(snapshot.healthCurrent) or 0))
        table.insert(parts, "mp=" .. string.format("%.4f", tonumber(snapshot.magickaBase) or 0))
        table.insert(parts, "mpc=" .. string.format("%.4f", tonumber(snapshot.magickaCurrent) or 0))
        table.insert(parts, "fp=" .. string.format("%.4f", tonumber(snapshot.fatigueBase) or 0))
        table.insert(parts, "fpc=" .. string.format("%.4f", tonumber(snapshot.fatigueCurrent) or 0))
    end
    for _, key in ipairs(sortedKeys(snapshot.attributes)) do
        table.insert(parts, "a:" .. key .. "=" .. string.format("%.4f", tonumber(snapshot.attributes[key]) or 0))
    end
    for _, key in ipairs(sortedKeys(snapshot.skills)) do
        table.insert(parts, "s:" .. key .. "=" .. string.format("%.4f", tonumber(snapshot.skills[key]) or 0))
    end
    return table.concat(parts, "|")
end

local function fallbackChecksum(text)
    -- Integer-only rolling checksum for servers built without GetSHA256Hash.
    -- SHA-256 is preferred and used by normal ArenaMP/TES3MP builds.
    local mod = 2147483647
    local hash = 216613626
    for i = 1, #text do
        hash = (hash * 16777619 + text:byte(i)) % mod
    end
    return string.format("fallback-%08x", hash)
end

local function checksum(snapshot)
    local text = "ArenaMP:Y067:AntiCheat|" .. canonical(snapshot)
    if tes3mp ~= nil and type(tes3mp.GetSHA256Hash) == "function" then
        return tes3mp.GetSHA256Hash(text)
    end
    return fallbackChecksum(text)
end

local function ensureCustomVariables(player)
    player.data = player.data or {}
    player.data.customVariables = player.data.customVariables or {}
    return player.data.customVariables
end

local function validChecksum(snapshot, expected)
    return type(snapshot) == "table" and type(expected) == "string" and checksum(snapshot) == expected
end

-- ArenaMP Y088: one durable checkpoint per server-confirmed character level.
-- Rolling `trusted` remains the closest clean state in time; level snapshots are
-- the corruption-proof fallback chain when that rolling snapshot is missing or
-- its checksum no longer validates.
local function saveLevelCheckpoint(state, snapshot, replaceExisting)
    if type(state) ~= "table" or type(snapshot) ~= "table" then return false end
    local level = math.max(1, math.floor((tonumber(snapshot.level) or 1) + 0.5))
    state.levelSnapshots = state.levelSnapshots or {}
    local key = tostring(level)
    if state.levelSnapshots[key] == nil or replaceExisting == true then
        state.levelSnapshots[key] = {
            level = level,
            at = now(),
            snapshot = snapshot,
            checksum = checksum(snapshot),
        }
    end
    state.highestConfirmedLevel = math.max(tonumber(state.highestConfirmedLevel) or 0, level)
    return true
end

local function nearestLevelCheckpoint(state, targetLevel)
    if type(state) ~= "table" or type(state.levelSnapshots) ~= "table" then return nil end
    local ceiling = math.max(tonumber(targetLevel) or 1, tonumber(state.highestConfirmedLevel) or 1)
    local best, bestLevel = nil, -1
    for _, entry in pairs(state.levelSnapshots) do
        local level = tonumber(entry and entry.level) or tonumber(entry and entry.snapshot and entry.snapshot.level) or -1
        if level <= ceiling and level > bestLevel and entry ~= nil
            and validChecksum(entry.snapshot, entry.checksum) then
            best = entry.snapshot
            bestLevel = level
        end
    end
    return best
end

local function nearestRollbackSnapshot(state, current)
    if type(state) ~= "table" then return nil end
    if state.trusted ~= nil and validChecksum(state.trusted, state.trustedChecksum) then
        return state.trusted
    end
    local checkpoint = nearestLevelCheckpoint(state, current and current.level or nil)
    if checkpoint ~= nil then return checkpoint end
    if state.baseline ~= nil and validChecksum(state.baseline, state.baselineChecksum) then
        return state.baseline
    end
    return nil
end

local function skillRaiseCost(value)
    value = math.floor(tonumber(value) or 0)
    if value < 50 then return 1 end
    if value < 75 then return 2 end
    if value < 90 then return 3 end
    return 4
end

local function skillInvestment(baseline, current)
    local total = 0
    for name, startValue in pairs((baseline and baseline.skills) or {}) do
        local finishValue = tonumber(current and current.skills and current.skills[name]) or tonumber(startValue) or 0
        local first = math.max(0, math.floor((tonumber(startValue) or 0) + 0.0001))
        local last = math.max(first, math.floor(finishValue + 0.0001))
        for value = first, last - 1 do total = total + skillRaiseCost(value) end
    end
    return total
end

local function attributeGain(baseline, current)
    local total = 0
    for name, startValue in pairs((baseline and baseline.attributes) or {}) do
        local finishValue = tonumber(current and current.attributes and current.attributes[name]) or tonumber(startValue) or 0
        total = total + math.max(0, finishValue - (tonumber(startValue) or 0))
    end
    return total
end

local function maxMapValue(t)
    local maxValue = 0
    for _, value in pairs(t or {}) do maxValue = math.max(maxValue, tonumber(value) or 0) end
    return maxValue
end

local function maxMapDelta(a, b)
    local delta = 0
    for key, before in pairs(a or {}) do
        local after = tonumber(b and b[key]) or tonumber(before) or 0
        delta = math.max(delta, math.abs(after - (tonumber(before) or 0)))
    end
    return delta
end

local function plausibleAgainstBaseline(state, baseline, current)
    local reasons = {}
    local level = tonumber(current.level) or 1

    if level < 1 or level > MAX_LEVEL then table.insert(reasons, "level=" .. tostring(level)) end
    if maxMapValue(current.attributes) > HARD_STAT_CAP then table.insert(reasons, "attribute>" .. HARD_STAT_CAP) end
    if maxMapValue(current.skills) > HARD_STAT_CAP then table.insert(reasons, "skill>" .. HARD_STAT_CAP) end

    if baseline ~= nil then
        local invested = skillInvestment(baseline, current)
        -- Y092-SP: training legitimately raises skills without spending SP, while
        -- /addpoint legitimately creates SP on the server. Account for both using
        -- server-owned ledgers instead of weakening the whole budget check.
        local trainingCredit = math.max(0, tonumber(state and state.authorizedTrainingInvestment) or 0)
        local adminBonus = math.max(0, tonumber(state and state.authorizedSkillPointBonus) or 0)
        local budgetedInvestment = math.max(0, invested - trainingCredit)
        local unspent = math.max(0, tonumber(current.skillPoints) or 0)
        local earned = math.max(0, level - 1) * SKILL_POINTS_PER_LEVEL + adminBonus + SKILL_BUDGET_TOLERANCE
        if budgetedInvestment + unspent > earned then
            table.insert(reasons, "skill-budget=" .. tostring(budgetedInvestment + unspent) .. ">" .. tostring(earned))
        end

        -- Very loose vanilla-compatible attribute envelope: up to 15 total base
        -- attribute points per level plus migration/script tolerance.
        local attrBudget = math.max(0, level - 1) * 15 + ATTRIBUTE_BUDGET_TOLERANCE
        local gain = attributeGain(baseline, current)
        if gain > attrBudget then table.insert(reasons, "attribute-budget=" .. tostring(gain) .. ">" .. tostring(attrBudget)) end

        local baselineHp = tonumber(baseline.healthBase) or 0
        local hpCap = baselineHp + math.max(0, level - 1) * HEALTH_PER_LEVEL_ALLOWANCE + HEALTH_FLAT_TOLERANCE
        if baselineHp > 0 and tonumber(current.healthBase) and current.healthBase > hpCap then
            table.insert(reasons, "health-base=" .. tostring(current.healthBase) .. ">" .. tostring(hpCap))
        end
    end

    return #reasons == 0, reasons
end

local function plausibleTransition(trusted, current)
    if type(trusted) ~= "table" then return true, {} end
    local reasons = {}
    if math.abs((tonumber(current.level) or 1) - (tonumber(trusted.level) or 1)) > 1 then
        table.insert(reasons, "level-jump")
    end
    if maxMapDelta(trusted.attributes, current.attributes) > MAX_SINGLE_STAT_DELTA then table.insert(reasons, "attribute-jump") end
    if maxMapDelta(trusted.skills, current.skills) > MAX_SINGLE_STAT_DELTA then table.insert(reasons, "skill-jump") end
    if math.abs((tonumber(current.healthBase) or 0) - (tonumber(trusted.healthBase) or 0)) > MAX_SINGLE_HEALTH_BASE_DELTA then
        table.insert(reasons, "health-jump")
    end
    return #reasons == 0, reasons
end

local function hasWritableSkillPoints(data)
    local _, container, name = readFirstNumber(data, {"skillPoints", "skillpoints", "skill_points", "xpSkillPoints", "unspentSkillPoints", "skillPointPool"})
    if container ~= nil and name ~= nil then return true end
    return tes3mp ~= nil and type(tes3mp.SetSkillPoints) == "function"
end

local function writeSkillPoints(pid, data, value)
    local wrote = setFirstNumber(data, {"skillPoints", "skillpoints", "skill_points", "xpSkillPoints", "unspentSkillPoints", "skillPointPool"}, value)
    if tes3mp ~= nil and type(tes3mp.SetSkillPoints) == "function" then
        tes3mp.SetSkillPoints(pid, value)
        wrote = true
    end
    return wrote
end

local function writeExperience(pid, data, value)
    local wrote = setFirstNumber(data, {"experience", "characterXp", "characterXP", "xp"}, value)
    if tes3mp ~= nil and type(tes3mp.SetExperience) == "function" then
        tes3mp.SetExperience(pid, value)
        wrote = true
    end
    return wrote
end

-- Y092-SP: raw skillPoints are not authoritative client state. A client may
-- lower the balance by spending points, but may only raise it by the amount
-- awarded by a same-packet level increase. Unauthorized gains are clamped while
-- the rest of the XP packet is still accepted.
local function sanitizeSkillPointsPacket(pid, playerPacket)
    if not isEnabled() then return false, nil end
    local player = Players and Players[pid] or nil
    if player == nil or type(player.data) ~= "table" or type(player.data.stats) ~= "table"
        or type(playerPacket) ~= "table" or type(playerPacket.stats) ~= "table" then
        return false, nil
    end

    local state = player.data.customVariables and player.data.customVariables[STATE_KEY] or nil
    local r = runtime[pid]
    if (type(state) == "table" and state.pendingBaseline == true)
        or (type(r) == "table" and (r.phase == "chargen" or r.phase == "settling")) then
        return false, nil
    end

    local storedLevel = math.max(1, math.floor((tonumber(player.data.stats.level) or 1) + 0.5))
    local incomingLevel = math.max(1, math.floor((tonumber(playerPacket.stats.level) or storedLevel) + 0.5))
    local storedPoints = math.max(0, math.floor((tonumber(player.data.stats.skillPoints) or 0) + 0.0001))
    local incomingRaw = tonumber(playerPacket.stats.skillPoints)
    local incomingPoints = 0
    if incomingRaw ~= nil and incomingRaw == incomingRaw and incomingRaw ~= math.huge and incomingRaw ~= -math.huge then
        incomingPoints = math.max(0, math.floor(incomingRaw + 0.0001))
    end

    local levelGain = math.max(0, incomingLevel - storedLevel)
    local maximumAllowed = storedPoints + levelGain * SKILL_POINTS_PER_LEVEL
    if incomingPoints <= maximumAllowed then return false, incomingPoints end

    playerPacket.stats.skillPoints = maximumAllowed
    if tes3mp ~= nil and type(tes3mp.SetSkillPoints) == "function" then
        tes3mp.SetSkillPoints(pid, maximumAllowed)
    end
    runtime[pid] = runtime[pid] or {phase = "active", strikes = 0}
    runtime[pid].skillPointTamperCount = (tonumber(runtime[pid].skillPointTamperCount) or 0) + 1
    runtime[pid].lastSkillPointTamperAt = now()
    log(enumerations and enumerations.log and enumerations.log.WARN or 2,
        "blocked unauthorized skillPoints gain for " .. playerName(pid) ..
        ": incoming=" .. tostring(incomingPoints) .. ", allowed=" .. tostring(maximumAllowed) ..
        ", stored=" .. tostring(storedPoints) .. ", levelDelta=" .. tostring(levelGain))
    return true, maximumAllowed
end

local function applySnapshot(pid, snapshot, skillPointOverride)
    local player = Players[pid]
    if player == nil or type(snapshot) ~= "table" then return false end
    local data = player.data

    for name, value in pairs(snapshot.attributes or {}) do
        if type(data.attributes) == "table" and type(data.attributes[name]) == "table" then
            data.attributes[name].base = value
        end
    end
    for name, value in pairs(snapshot.skills or {}) do
        if type(data.skills) == "table" and type(data.skills[name]) == "table" then
            data.skills[name].base = value
            if data.skills[name].progress ~= nil then data.skills[name].progress = 0 end
        end
    end

    setLevel(data, tonumber(snapshot.level) or 1)
    data.stats = data.stats or {}
    if data.stats.levelProgress ~= nil then data.stats.levelProgress = tonumber(snapshot.levelProgress) or 0 end
    if tonumber(snapshot.healthBase) ~= nil and snapshot.healthBase > 0 then
        data.stats.healthBase = snapshot.healthBase
    end
    -- Y088 checkpoints restore the full dynamic state captured at the trusted
    -- point, not only max health.  Legacy V2 snapshots simply omit these fields.
    if snapshot.healthCurrent ~= nil then data.stats.healthCurrent = math.max(0, tonumber(snapshot.healthCurrent) or 0) end
    if snapshot.magickaBase ~= nil then data.stats.magickaBase = math.max(0, tonumber(snapshot.magickaBase) or 0) end
    if snapshot.magickaCurrent ~= nil then data.stats.magickaCurrent = math.max(0, tonumber(snapshot.magickaCurrent) or 0) end
    if snapshot.fatigueBase ~= nil then data.stats.fatigueBase = math.max(0, tonumber(snapshot.fatigueBase) or 0) end
    if snapshot.fatigueCurrent ~= nil then data.stats.fatigueCurrent = math.max(0, tonumber(snapshot.fatigueCurrent) or 0) end

    if snapshot.experience ~= nil then writeExperience(pid, data, snapshot.experience) end
    if skillPointOverride ~= nil then writeSkillPoints(pid, data, skillPointOverride)
    elseif snapshot.skillPoints ~= nil then writeSkillPoints(pid, data, snapshot.skillPoints) end

    if type(player.LoadAttributes) == "function" then player:LoadAttributes() end
    if type(player.LoadSkills) == "function" then player:LoadSkills() end
    if type(player.LoadLevel) == "function" then player:LoadLevel() end
    if type(player.LoadStatsDynamic) == "function" then player:LoadStatsDynamic() end
    if type(player.QuicksaveToDrive) == "function" then player:QuicksaveToDrive() end
    return true
end

local function persistTrusted(player, state, snapshot, force)
    state.trusted = snapshot
    state.trustedChecksum = checksum(snapshot)
    local timestamp = now()
    if force or timestamp - (tonumber(state.lastPersistAt) or 0) >= TRUSTED_PERSIST_INTERVAL then
        state.lastPersistAt = timestamp
        if type(player.QuicksaveToDrive) == "function" then player:QuicksaveToDrive() end
    end
end

local function beginCharGen(pid)
    -- Always mark CharGen, even when anti-cheat is disabled (for example CO-OP).
    -- If config is hot-reloaded to MMO mid-creation, partial CharGen packets still
    -- must not be mistaken for established progression.
    runtime[pid] = {phase = "chargen", strikes = 0, postCharGenCellSeen = false}
    if not isEnabled() then return nil end
    -- Set the runtime guard before touching Player.data. Even if Register() is
    -- still finishing, the following CharGen stat packets must never auto-enroll.
    local player = Players[pid]
    if player == nil or player.data == nil then return nil end
    local vars = ensureCustomVariables(player)
    -- Never inherit a possibly broken V1 baseline into a newly created character.
    vars[STATE_KEY] = {
        schema = SCHEMA_VERSION,
        legacy = false,
        pendingBaseline = true,
        charGenPhase = "creating",
        createdAt = now(),
    }
    return vars[STATE_KEY]
end

local function finalizeBaseline(pid, state, reason)
    local player = Players[pid]
    if player == nil or player.data == nil or type(state) ~= "table" then return false end
    local current = snapshotPlayer(player)
    if not snapshotLooksComplete(current) then return false end

    state.baseline = current
    state.baselineChecksum = checksum(current)
    state.legacy = false
    state.pendingBaseline = false
    state.charGenPhase = "active"
    state.baselineFinalizedAt = now()
    state.baselineFinalizedBy = reason or "post-chargen"
    saveLevelCheckpoint(state, current)
    persistTrusted(player, state, current, true)
    runtime[pid] = {phase = "active", strikes = 0, postCharGenCellSeen = true}
    log(enumerations and enumerations.log and enumerations.log.INFO or 1,
        "finalized post-CharGen level-1 baseline for " .. playerName(pid) ..
        "; skills=" .. tostring(mapCount(current.skills)) ..
        "; attributes=" .. tostring(mapCount(current.attributes)) ..
        "; healthBase=" .. tostring(current.healthBase))
    return true
end

local function enroll(pid, charGen)
    if not isEnabled() then
        runtime[pid] = {phase = "disabled"}
        return nil
    end
    local player = Players[pid]
    if player == nil or player.data == nil then return nil end
    local vars = ensureCustomVariables(player)
    local current = snapshotPlayer(player)
    local state = vars[STATE_KEY]

    if charGen then
        -- EndCharGen is not a safe instant for the final baseline: derived HP and
        -- some race/class state can settle on the following cell transition.
        if type(state) ~= "table" or tonumber(state.schema) ~= SCHEMA_VERSION then
            state = beginCharGen(pid)
            if state == nil then return nil end
        end
        state.pendingBaseline = true
        state.legacy = false
        state.charGenPhase = "settling"
        state.charGenEndedAt = now()
        local r = runtime[pid] or {}
        r.phase = "settling"
        r.strikes = 0
        r.postCharGenCellSeen = false
        runtime[pid] = r
        return state
    end

    if type(state) ~= "table" or tonumber(state.schema) ~= SCHEMA_VERSION then
        -- V1 is intentionally NOT migrated as a baseline. Y061 could enroll in
        -- the middle of CharGen, so its level-1 snapshot may be incomplete.
        local hadV1 = type(vars[LEGACY_STATE_KEY]) == "table"
        state = {
            schema = SCHEMA_VERSION,
            legacy = true,
            pendingBaseline = false,
            migratedFrom = hadV1 and LEGACY_STATE_KEY or nil,
            migratedAt = now(),
        }
        vars[STATE_KEY] = state
        saveLevelCheckpoint(state, current)
        persistTrusted(player, state, current, true)
        runtime[pid] = {phase = "active", strikes = 0}
        log(enumerations and enumerations.log and enumerations.log.INFO or 1,
            "enrolled " .. playerName(pid) .. " as V2 legacy-safe profile" ..
            (hadV1 and "; ignored unsafe V1 baseline" or ""))
        return state
    end

    if state.pendingBaseline == true then
        -- A server restart/reconnect interrupted the settlement window. Never
        -- guess a level-1 baseline from that half-finished state. Keep current
        -- persisted data as trusted and disable hard recovery for this profile.
        state.baseline = nil
        state.baselineChecksum = nil
        state.pendingBaseline = false
        state.charGenPhase = "active"
        state.legacy = true
        state.interruptedSettlementAt = now()
        saveLevelCheckpoint(state, current)
        persistTrusted(player, state, current, true)
        runtime[pid] = {phase = "active", strikes = 0}
        log(enumerations and enumerations.log and enumerations.log.WARN or 2,
            "interrupted CharGen settlement for " .. playerName(pid) ..
            "; switched to legacy-safe trusted state")
        return state
    end

    if state.baseline ~= nil and not validChecksum(state.baseline, state.baselineChecksum) then
        state.baseline = nil
        state.baselineChecksum = nil
        state.legacy = true
        log(enumerations and enumerations.log and enumerations.log.WARN or 2,
            "baseline checksum mismatch for " .. playerName(pid) .. "; hard recovery disabled")
    end

    -- Y088 migration: seed a durable checkpoint for profiles created before
    -- levelSnapshots existed. Never promote a profile with a broken trusted
    -- checksum into a new checkpoint: prefer valid trusted, then valid baseline;
    -- only a profile with no previous trusted state may seed from current data.
    if type(state.levelSnapshots) ~= "table" then
        state.levelSnapshots = {}
        local seed = nil
        if state.trusted ~= nil and validChecksum(state.trusted, state.trustedChecksum) then
            seed = state.trusted
        elseif state.trusted == nil and state.baseline ~= nil and validChecksum(state.baseline, state.baselineChecksum) then
            seed = state.baseline
        elseif state.trusted == nil then
            seed = current
        elseif state.baseline ~= nil and validChecksum(state.baseline, state.baselineChecksum) then
            seed = state.baseline
        end
        if seed ~= nil then
            saveLevelCheckpoint(state, seed)
            if type(player.QuicksaveToDrive) == "function" then player:QuicksaveToDrive() end
        end
    end

    if state.trusted ~= nil and not validChecksum(state.trusted, state.trustedChecksum) then
        state.lastChecksumMismatch = {at = now(), previous = state.trustedChecksum}
        local fallback = nearestLevelCheckpoint(state, state.highestConfirmedLevel)
        if fallback == nil and state.baseline ~= nil and validChecksum(state.baseline, state.baselineChecksum) then
            fallback = state.baseline
        end
        if fallback ~= nil then
            applySnapshot(pid, fallback)
            current = snapshotPlayer(player)
            persistTrusted(player, state, current, true)
            log(enumerations and enumerations.log and enumerations.log.WARN or 2,
                "trusted checksum mismatch for " .. playerName(pid) ..
                "; restored nearest confirmed checkpoint")
        else
            state.legacy = true
            persistTrusted(player, state, current, true)
            log(enumerations and enumerations.log and enumerations.log.WARN or 2,
                "trusted checksum mismatch for " .. playerName(pid) ..
                "; no checkpoint existed, retained server profile in legacy-safe mode")
        end
    end
    runtime[pid] = runtime[pid] or {phase = "active", strikes = 0}
    return state
end

local function restoreTrusted(pid, state, reasons)
    local player = Players[pid]
    local current = player ~= nil and snapshotPlayer(player) or nil
    local rollback = nearestRollbackSnapshot(state, current)
    if rollback ~= nil then
        applySnapshot(pid, rollback)
        if player ~= nil then persistTrusted(player, state, snapshotPlayer(player), true) end
    end
    log(enumerations and enumerations.log and enumerations.log.WARN or 2,
        "restored nearest trusted/checkpoint state for " .. playerName(pid) .. "; reasons=" .. table.concat(reasons or {}, ","))
    notify(pid, false, 0)
end

local function hardRecover(pid, state, reasons)
    -- Y088: repeated anomalies no longer destroy legitimate progression by
    -- rebuilding the character to level 1.  Preserve a forensic backup, then
    -- restore the nearest checksum-valid trusted/level-up checkpoint.
    local player = Players[pid]
    if player == nil then return false end
    local suspicious = snapshotPlayer(player)
    local rollback = nearestRollbackSnapshot(state, suspicious)
    if rollback == nil then return false end

    state.lastRecoveryBackup = {
        at = now(),
        reasons = reasons,
        suspicious = suspicious,
        suspiciousChecksum = checksum(suspicious),
        previousTrusted = state.trusted,
        previousTrustedChecksum = state.trustedChecksum,
        rollbackLevel = rollback.level,
        rollbackChecksum = checksum(rollback),
    }

    applySnapshot(pid, rollback)
    persistTrusted(player, state, snapshotPlayer(player), true)
    state.lastHardRecoveryAt = now()
    state.hardRecoveryCount = (tonumber(state.hardRecoveryCount) or 0) + 1
    if type(player.QuicksaveToDrive) == "function" then player:QuicksaveToDrive() end

    log(enumerations and enumerations.log and enumerations.log.WARN or 2,
        "CHECKPOINT RECOVERY for " .. playerName(pid) .. "; level=" .. tostring(rollback.level) ..
        "; reasons=" .. table.concat(reasons or {}, ","))
    notify(pid, true, tonumber(rollback.skillPoints) or 0)
    return true
end

local function check(pid, reason)
    if not isEnabled() then return end
    local player = Players[pid]
    if player == nil or player.data == nil then return end

    local vars = ensureCustomVariables(player)
    local state = vars[STATE_KEY]
    local r = runtime[pid]

    -- No state during initial connection is normal. A registered new character
    -- enters BeginCharGen before its first mutable stat packets. Existing
    -- profiles can be enrolled here when anti-cheat is enabled by hot reload.
    if type(state) ~= "table" or tonumber(state.schema) ~= SCHEMA_VERSION then
        if r ~= nil and (r.phase == "chargen" or r.phase == "settling") then return end
        state = enroll(pid, false)
        if state == nil then return end
    end

    if state.pendingBaseline == true then
        r = r or {phase = state.charGenPhase or "settling", strikes = 0}
        if state.charGenPhase == "creating" or r.phase == "chargen" then
            runtime[pid] = r
            return
        end
        if reason == "cell-change" then r.postCharGenCellSeen = true end
        runtime[pid] = r
        if r.postCharGenCellSeen == true then
            finalizeBaseline(pid, state, reason)
        end
        return
    end

    local current = snapshotPlayer(player)
    local baseline = nil
    if state.baseline ~= nil and validChecksum(state.baseline, state.baselineChecksum) then baseline = state.baseline end

    local okBase, baseReasons = plausibleAgainstBaseline(state, baseline, current)
    local okTransition, transitionReasons = plausibleTransition(state.trusted, current)
    local reasons = {}
    if not okBase then for _, v in ipairs(baseReasons) do table.insert(reasons, v) end end
    if not okTransition then for _, v in ipairs(transitionReasons) do table.insert(reasons, v) end end

    if baseline ~= nil and (maxMapValue(current.attributes) > BASE_STAT_CAP or maxMapValue(current.skills) > BASE_STAT_CAP) then
        table.insert(reasons, "base-stat-soft-cap")
    end

    if #reasons == 0 then
        runtime[pid] = {phase = "active", strikes = 0}
        local previousTrustedLevel = tonumber(state.trusted and state.trusted.level) or tonumber(current.level) or 1
        local trueLevelUp = reason == "level" and (tonumber(current.level) or 1) > previousTrustedLevel
        if trueLevelUp then
            saveLevelCheckpoint(state, current)
        end
        -- A confirmed level-up is a durable rollback point and must hit disk
        -- immediately; ordinary clean packets keep the existing debounce.
        persistTrusted(player, state, current, trueLevelUp)
        return
    end

    r = runtime[pid] or {phase = "active", strikes = 0}
    r.strikes = (tonumber(r.strikes) or 0) + 1
    r.lastReason = reason
    r.lastAt = now()
    runtime[pid] = r

    if r.strikes >= HARD_RECOVERY_STRIKES and baseline ~= nil and state.legacy ~= true then
        hardRecover(pid, state, reasons)
        r.strikes = 0
    else
        restoreTrusted(pid, state, reasons)
    end
end

function arenaAntiCheat.BeginCharGen(pid)
    return beginCharGen(pid)
end

function arenaAntiCheat.Enroll(pid, charGen)
    return enroll(pid, charGen == true)
end

function arenaAntiCheat.Check(pid, reason)
    check(pid, reason or "manual")
end

function arenaAntiCheat.SanitizeSkillPointsPacket(pid, playerPacket)
    return sanitizeSkillPointsPacket(pid, playerPacket)
end

-- Called only after the server accepted a trainer USE control message. This
-- exempts that one legitimate skill raise from the old SP-investment budget.
function arenaAntiCheat.AuthorizeTraining(pid, skillId)
    if not isEnabled() then return false end
    local player = Players and Players[pid] or nil
    if player == nil or type(player.data) ~= "table" or type(player.data.skills) ~= "table" then return false end
    skillId = tonumber(skillId)
    if skillId == nil or skillId < 0 or math.floor(skillId) ~= skillId then return false end
    if tes3mp == nil or type(tes3mp.GetSkillName) ~= "function" then return false end

    local skillName = tes3mp.GetSkillName(skillId)
    local skill = skillName and player.data.skills[skillName] or nil
    if type(skill) ~= "table" then return false end
    local vars = ensureCustomVariables(player)
    local state = vars[STATE_KEY]
    if type(state) ~= "table" or tonumber(state.schema) ~= SCHEMA_VERSION then return false end

    state.authorizedTrainingInvestment = math.max(0, tonumber(state.authorizedTrainingInvestment) or 0)
        + skillRaiseCost(tonumber(skill.base) or 0)
    return true
end

-- Server-side administrator grant. Never edit skillPoints directly from a
-- command: the anti-cheat grant ledger, trusted snapshot and level checkpoint
-- must move together or a later rollback would erase the gift.
function arenaAntiCheat.GrantSkillPoints(targetPid, amount, source)
    local player = Players and Players[targetPid] or nil
    amount = tonumber(amount)
    if player == nil or type(player.data) ~= "table" or type(player.data.stats) ~= "table"
        or amount == nil or amount ~= amount or amount == math.huge or amount == -math.huge then
        return false, nil, "invalid"
    end
    amount = math.floor(amount)
    if amount <= 0 or amount > 100000 then return false, nil, "range" end

    local oldPoints = math.max(0, math.floor((tonumber(player.data.stats.skillPoints) or 0) + 0.0001))
    local newPoints = oldPoints + amount
    player.data.stats.skillPoints = newPoints

    local state = player.data.customVariables and player.data.customVariables[STATE_KEY] or nil
    if isEnabled() and type(state) == "table" and tonumber(state.schema) == SCHEMA_VERSION then
        state.authorizedSkillPointBonus = math.max(0, tonumber(state.authorizedSkillPointBonus) or 0) + amount
        state.lastAdminSkillPointGrant = {at = now(), amount = amount, source = tostring(source or "admin")}
        local current = snapshotPlayer(player)
        saveLevelCheckpoint(state, current, true)
        persistTrusted(player, state, current, true)
    elseif type(player.QuicksaveToDrive) == "function" then
        player:QuicksaveToDrive()
    end

    if tes3mp ~= nil and type(tes3mp.SetSkillPoints) == "function" then
        tes3mp.SetSkillPoints(targetPid, newPoints)
    end
    if type(player.LoadLevel) == "function" then
        player:LoadLevel(true)
    elseif tes3mp ~= nil and type(tes3mp.SendLevel) == "function" then
        tes3mp.SendLevel(targetPid)
    end

    log(enumerations and enumerations.log and enumerations.log.INFO or 1,
        "authorized /addpoint grant for " .. playerName(targetPid) .. ": +" .. tostring(amount) ..
        " SP by " .. tostring(source or "admin") .. "; balance=" .. tostring(newPoints))
    return true, newPoints, nil
end

function arenaAntiCheat.IsEnabled()
    return isEnabled()
end

function arenaAntiCheat.IsCoopPreset()
    return detectCoopPreset()
end

function arenaAntiCheat.RegisterHooks()
    if hooksRegistered then return end
    hooksRegistered = true

    customEventHooks.registerValidator("OnPlayerLevel", function(eventStatus, pid, playerPacket)
        if eventStatus.validDefaultHandler then sanitizeSkillPointsPacket(pid, playerPacket) end
    end)

    customEventHooks.registerHandler("OnPlayerAttribute", function(eventStatus, pid)
        if eventStatus.validCustomHandlers then check(pid, "attribute") end
    end)
    customEventHooks.registerHandler("OnPlayerSkill", function(eventStatus, pid)
        if eventStatus.validCustomHandlers then check(pid, "skill") end
    end)
    customEventHooks.registerHandler("OnPlayerLevel", function(eventStatus, pid)
        if eventStatus.validCustomHandlers then check(pid, "level") end
    end)
    customEventHooks.registerHandler("OnPlayerStatsDynamic", function(eventStatus, pid)
        if eventStatus.validCustomHandlers then check(pid, "stats-dynamic") end
    end)
    customEventHooks.registerHandler("OnPlayerCellChange", function(eventStatus, pid)
        if eventStatus.validCustomHandlers then check(pid, "cell-change") end
    end)
    customEventHooks.registerHandler("OnPlayerEndCharGen", function(eventStatus, pid)
        if eventStatus.validCustomHandlers then enroll(pid, true) end
    end)
    customEventHooks.registerHandler("OnPlayerDisconnect", function(eventStatus, pid)
        runtime[pid] = nil
    end)
end

return arenaAntiCheat
