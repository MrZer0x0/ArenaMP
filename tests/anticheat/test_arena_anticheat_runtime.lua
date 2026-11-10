-- Runtime smoke test for ArenaMP Y067 anti-cheat using a minimal CoreScripts mock.
local root = arg[1]
assert(root ~= nil)

local messages = {}
local handlers = {}
enumerations = {log = {INFO = 1, WARN = 2}}
config = {
    gameMode = "ArenaMP MMO",
    arenaAntiCheat = {enabled = true, disableInCoop = true},
    coopResetPolicy = {},
    shareJournal = false, shareTopics = false, shareFactionRanks = false, shareMapExploration = false,
}
tes3mp = {
    GetSHA256Hash = function(text)
        local h = 17
        for i = 1, #text do h = (h * 131 + text:byte(i)) % 2147483647 end
        return tostring(h)
    end,
    LogAppend = function() end,
    MessageBox = function(pid, id, text) messages[#messages + 1] = text end,
    SendMessage = function(pid, text) messages[#messages + 1] = text end,
}
logicHandler = {GetChatName = function(pid) return "P" .. tostring(pid) end}
customEventHooks = {registerHandler = function(name, fn) handlers[name] = fn end}
Players = {}

local function player(level, skill, points, hp)
    local attrs = {}
    for _, name in ipairs({"strength","intelligence","willpower","agility","speed","endurance","personality","luck"}) do
        attrs[name] = {base = 40}
    end
    local skills = {}
    for i = 1, 27 do skills["skill" .. tostring(i)] = {base = (i == 1 and skill or 10), progress = 0} end
    local p = {
        data = {
            attributes = attrs,
            skills = skills,
            stats = {level = level, levelProgress = 0, healthBase = hp or 50, healthCurrent = hp or 50,
                     magickaBase = 80, magickaCurrent = 70, fatigueBase = 160, fatigueCurrent = 150,
                     experience = 0, skillPoints = points},
            customVariables = {},
        }
    }
    function p:LoadAttributes() end
    function p:LoadSkills() end
    function p:LoadLevel() end
    function p:LoadStatsDynamic() end
    function p:QuicksaveToDrive() self.saved = (self.saved or 0) + 1 end
    return p
end

local ac = dofile(root .. "/server/scripts/arenaAntiCheat.lua")

-- New character: pre-EndCharGen race/class skill packets must never be judged.
Players[1] = player(1, 35, 0, 1)
ac.BeginCharGen(1)
ac.Check(1, "attribute")
ac.Check(1, "skill")
local state = Players[1].data.customVariables.ArenaMPAntiCheatV2
assert(state ~= nil and state.pendingBaseline == true and state.baseline == nil)
assert(#messages == 0)

-- EndCharGen only starts settlement; it still does not freeze HP/skills.
ac.Enroll(1, true)
Players[1].data.stats.healthBase = 65
Players[1].data.stats.healthCurrent = 65
Players[1].data.skills.skill1.base = 40
ac.Check(1, "stats-dynamic")
state = Players[1].data.customVariables.ArenaMPAntiCheatV2
assert(state.pendingBaseline == true and state.baseline == nil)

-- First stable post-CharGen cell transition becomes the real level-1 baseline.
ac.Check(1, "cell-change")
state = Players[1].data.customVariables.ArenaMPAntiCheatV2
assert(state.pendingBaseline == false and state.legacy == false)
assert(state.baseline.skills.skill1 == 40)
assert(state.baseline.healthBase == 65)
assert(#messages == 0)

-- Legitimate progression to level 2.
Players[1].data.stats.level = 2
Players[1].data.skills.skill1.base = 41
Players[1].data.stats.skillPoints = 9
ac.Check(1, "legit")
state = Players[1].data.customVariables.ArenaMPAntiCheatV2
assert(state.trusted.level == 2 and state.trusted.skills.skill1 == 41)

-- First impossible change restores trusted state.
Players[1].data.skills.skill1.base = 110
ac.Check(1, "cheat-one")
assert(Players[1].data.stats.level == 2)
assert(Players[1].data.skills.skill1.base == 41)

-- Repeated violation restores the nearest trusted/checkpoint state, never a
-- destructive level-1 rebuild. Dynamic HP/magicka/fatigue are checkpointed too.
Players[1].data.skills.skill1.base = 110
Players[1].data.stats.healthCurrent = 1
Players[1].data.stats.magickaCurrent = 1
Players[1].data.stats.fatigueCurrent = 1
ac.Check(1, "cheat-two")
state = Players[1].data.customVariables.ArenaMPAntiCheatV2
assert(Players[1].data.stats.level == 2)
assert(Players[1].data.skills.skill1.base == 41)
assert(Players[1].data.stats.healthCurrent == 65)
assert(Players[1].data.stats.magickaCurrent == 70)
assert(Players[1].data.stats.fatigueCurrent == 150)
assert(state.lastRecoveryBackup ~= nil)
assert(state.hardRecoveryCount == 1)

-- Old V1 baseline is never trusted for hard recovery after migration.
Players[2] = player(20, 60, 0, 150)
Players[2].data.customVariables.ArenaMPAntiCheatV1 = {schema = 1, baseline = {skills = {skill1 = 1}}}
local legacy = ac.Enroll(2, false)
assert(legacy.legacy == true and legacy.baseline == nil and legacy.migratedFrom == "ArenaMPAntiCheatV1")
Players[2].data.skills.skill1.base = 110
ac.Check(2, "legacy-one")
Players[2].data.skills.skill1.base = 110
ac.Check(2, "legacy-two")
assert(Players[2].data.stats.level == 20)
assert(Players[2].data.skills.skill1.base == 60)
assert(legacy.hardRecoveryCount == nil)

-- CO-OP disables the anti-cheat automatically.
config.gameMode = "ArenaMP CO-OP"
Players[3] = player(1, 35, 0, 60)
assert(ac.IsCoopPreset() == true)
assert(ac.IsEnabled() == false)
assert(ac.BeginCharGen(3) == nil)
Players[3].data.skills.skill1.base = 110
ac.Check(3, "coop")
assert(Players[3].data.skills.skill1.base == 110)
assert(Players[3].data.customVariables.ArenaMPAntiCheatV2 == nil)

print("Y067 ArenaMP anti-cheat runtime mock: PASS")
