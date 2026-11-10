config = {}

-- Server message language. Keep this setting at the top so it is easy to find.
-- "AUTO": use each client's detected RU/EN language.
-- "RU": force Russian server messages for every client.
-- "EN": force English server messages for every client.
config.serverLanguage = "AUTO"

-- Fallback used in AUTO mode before a client language is available.
-- Supported fallback flags: EN and RU.
config.defaultLanguage = "EN"

-- The path used by the server for its data folder
config.dataPath = tes3mp.GetDataPath()

-- H003: host-side directories containing the *active* ESM/ESP files.
-- Android writes its verified Mods directory automatically to
-- server/data/custom/arenaFurnitureSources.json before server startup.
-- For dedicated desktop servers whose content is elsewhere, add e.g.
-- config.furnitureDataDirs = {"D:/Games/Morrowind/Data Files"}
-- An empty table enables relative Data Files autodiscovery and the
-- ARENAMP_DATA_FILES environment variable; missing mods disable the shop.
config.furnitureDataDirs = {}
-- Legacy Russian plugin text (CP1251) is converted to UTF-8 for MyGUI.
-- Use "id" instead if records have an unsupported legacy encoding.
config.furnitureNameEncoding = "cp1251"
-- /addhome reads CELL names from the currently active ESM masters.
-- "auto" accepts UTF-8 and falls back to CP1251 for legacy Russian plugins.
-- Use "cp1251" only when your ESM is known to use that encoding.
config.houseCellNameEncoding = "auto"

-- The game mode displayed for this server in the server browser
config.gameMode = "ArenaMP MMO"

-- Z001: политика сброса ячеек для пресета CO-OP.
--
-- Пресет определяется автоматически: лаунчер пишет сюда "ArenaMP CO-OP" и
-- включает общие журнал/темы/фракции. В CO-OP регулярный автосброс мира только
-- мешает — несколько друзей играют сессиями и возвращаются к тому же миру,
-- поэтому вся автоматика выключается, а ручные команды остаются.
--
-- Оставьте таблицу пустой, чтобы полностью довериться автоопределению.
-- forceCoop = true/false жёстко задаёт режим независимо от gameMode.
config.coopResetPolicy = {
    -- forceCoop = true,
    scheduleTimers = false,     -- заводить таймеры сброса при входе в ячейку
    timerResets = false,        -- сбрасывать ячейки по таймеру
    startupResets = false,      -- массовый сброс при старте сервера
    massResetCommands = false,  -- /resetall и /pushresets
    manualCommands = true,      -- /clean, /clear, /reset по текущей ячейке
    houseCleanup = true         -- очистка домов без владельца
}

-- Y067: server-side progression anti-cheat.
-- enabled = false disables it entirely.
-- CO-OP is intentionally trust-based: when disableInCoop is true the anti-cheat
-- automatically turns off for the launcher "ArenaMP CO-OP" preset (and for
-- forceCoop=true), while MMO keeps it enabled.
config.arenaAntiCheat = {
    enabled = true,
    disableInCoop = true
}

-- Y110: map restoration is cooperative; these limits apply globally across logins.
-- Existing installations need no config change: loginSyncHelper uses these defaults.
config.loginPerformance = {
    mapTilesPerTick = 16,
    mapTickMs = 25,
    mapBudgetMs = 2,
    slowPhaseMs = 25
}

-- Time to login, in seconds
config.loginTime = 60

-- X051: restart-safe player position persistence. The stock TES3MP 0.8.1
-- server does not expose ID_PLAYER_POSITION to Lua, so X051 adds that callback
-- and keeps the last in-cell transform authoritative. Disk writes are debounced;
-- disconnect always saves the cached value instead of querying a closing peer.
config.positionSafety = {
    enabled = true,
    autosaveSeconds = 10,
    loginGuardEnabled = true,
    loginGuardDelaysMs = {400, 1200, 3000},
    loginGuardFallThreshold = 24,
    loginGuardMoveCancelDistance = 96,
    maxAbsCoordinate = 100000000
}

-- How many clients are allowed to connect from the same IP address
config.maxClientsPerIP = 3

-- The difficulty level used by default
-- Note: This overhaul changes the difficulty logic significantly, see the readme on github for new setting values for EncoreMP
-- 0 = Apprentice
-- 1 to 50 = Journeyman
-- 51 to 100 = Master
-- 101 to 150 = Grandmaster 
-- 151 to 200 = Agent
-- 201+ = Nerevarine (absurdly difficult!)
config.difficulty = 75

-- ArenaMP engine mechanics enforced for every connected client
--
-- These values are converted to [Game] settings below and sent by CoreScripts
-- through tes3mp.SendSettings(). They therefore stay identical for all players,
-- regardless of the local settings.cfg used by an individual client.
--
-- Tactical combat keeps the regular AiCombat package active while NPCs strafe,
-- circle, retreat, dodge and recover from blocked positions.
config.arenaTacticalCombat = true

-- Delay in seconds before an NPC sheathes a weapon after combat ends.
config.arenaCombatWeaponSheatheDelay = 2.0

-- Y084: final NPC/creature -> player damage curve by attacker level.
-- Native offense reaches x1.50 by level 50, while this mild final curve keeps
-- early enemies fair without making veteran enemies explode in damage.
config.arenaNpcDamageLowLevelMultiplier = 0.75
config.arenaNpcDamageLevel100Multiplier = 1.00
config.arenaNpcDamageOver100Multiplier = 1.05

-- Allow hostile NPCs that are actively fighting a player to pursue that player
-- through nearby teleport doors without replacing AiCombat with AiFollow.
config.arenaCombatPursuitThroughDoors = true

-- Door pursuit is guaranteed up to this distance in world units.
config.arenaCombatPursuitGuaranteedDistance = 200

-- NPCs farther than this distance are never selected for door pursuit.
config.arenaCombatPursuitDoorMaxDistance = 800

-- Minimum pursuit chance at arenaCombatPursuitDoorMaxDistance.
-- Valid range: 0.0 to 1.0.
config.arenaCombatPursuitMinimumChance = 0.10

-- Maximum number of hostile actors allowed to pass through one door transition.
-- Set to 0 to disable door pursuers while keeping the mechanic available.
config.arenaCombatPursuitMaxActors = 3

-- Same-cell pursuit leash in world units. Set to 0 for unlimited pursuit.
config.arenaCombatPursuitMaxDistance = 4000

-- Additional ArenaMP/EncoreMP actor and combat rules.
config.arenaFollowersAttackOnSight = true
config.arenaNpcAvoidCollisions = true
config.arenaNpcGiveWay = true
config.arenaAllowActorsFollowOverWater = true
config.arenaActorsProcessingRange = 4000
config.arenaCanLootDuringDeathAnimation = true
config.arenaWeaponSheathing = true
config.arenaShieldSheathing = false
config.arenaGraphicHerbalism = true
config.arenaLongBladesUseAgility = true
config.arenaTwoHandedAccuracyPenalty = true
config.arenaStavesAccuracyBonus = true
config.arenaSkillBooksLevelLimit = true
config.arenaNewConstantEffectDifficulty = true
-- X030: skill-use-only multiplier (organic skill actions).
config.arenaGlobalXpMultiplier = 1.00
-- X030: overall server-authoritative XP rate. 0.50 very slow, 0.75 slow,
-- 1.00 normal, 1.50 fast, 2.00 very fast. This mirrors the launcher's
-- convenience profiles; remote servers still choose their own value.
config.arenaXpRateMultiplier = 1.0

-- Persistent group/party rules. Journal and topic synchronization default ON
-- for new members and can still be toggled from the Player Menu; XP sharing itself is automatic and only
-- includes online group members standing in the same cell as the source.
config.groupSystem = {
    ["invite lifetime seconds"] = 120,
    ["invite popups"] = true, -- immediate Yes/No dialog for the invited player
    ["xp signal lifetime seconds"] = 8,
    ["summon protection"] = true,
    ["summon check interval ms"] = 2000,
    ["max client kill xp signal"] = 25000,
    ["max client quest xp signal"] = 25000,

    -- Mirror group membership into each member's native allied-player list.
    -- This is what makes config.friendlyFireMode = "group" actually cover the
    -- party: the engine resolves friendly fire against that list, and until
    -- now only the legacy /ally command ever wrote to it. It is also how the
    -- client recognises a party member's summon as friendly. Turning this off
    -- restores the old behaviour where party members can damage each other.
    ["native allies"] = true,

    -- Credit a kill landed by a player's summon to that player, and split it
    -- across the party in the usual same-cell way.
    ["summon kill xp"] = true,

    -- A summon kill is reported by whichever client owns the dying actor,
    -- while the reward signal comes from the summoner's own client, so the two
    -- can arrive several seconds apart. Direct kills keep the shorter window.
    ["summon xp event lifetime seconds"] = 20,

    -- Ordinary group management notices may be silenced independently.
    -- Shared-XP messages are off by default because they are frequent and the
    -- XP bar already gives the player feedback.
    ["chat notifications"] = true,
    ["xp share chat messages"] = false
}

-- X031: RAW config.lua is watched while the server is running. A successful
-- reload is transactional and is pushed to connected players; malformed edits
-- leave the previous working table active. Interval is clamped to 1..60 seconds.
config.rawConfigReloadInterval = 2

-- X031: re-verify/regenerate custom/questIndex.json once for every server process
-- start. The stored file remains on disk as a diagnostic/fallback copy, but quest
-- phasing stays fail-closed until the first fresh verified client upload replaces it.
config.questIndexRefreshOnServerStart = false -- X046: deprecated; valid questIndex.json is always reused

-- Server-authoritative ArenaMP XP progression. These values overwrite the
-- client [XP Leveling] category when a player connects.
config.xpLeveling = {
    ["enabled"] = true,
    ["base xp to level"] = 1000,
    ["progressive xp curve"] = true,
    ["xp level increment start"] = 10,
    ["xp level increment step"] = 5,
    ["xp level increment threshold"] = 50,
    ["xp level increment high step"] = 10,
    ["xp per level"] = 250, -- legacy fallback only
    ["skill points per level"] = 10,
    ["xp per skill level equivalent"] = 50,
    ["xp gain multiplier"] = config.arenaXpRateMultiplier,
    ["difficulty xp scaling"] = true,
    ["difficulty xp tier 1 multiplier"] = 1.00,
    ["difficulty xp tier 2 multiplier"] = 1.15,
    ["difficulty xp tier 3 multiplier"] = 1.30,
    ["difficulty xp tier 4 multiplier"] = 1.45,
    ["difficulty xp tier 5 multiplier"] = 1.60,
    ["difficulty xp tier 6 multiplier"] = 1.75,
    ["kill base xp"] = 20,
    ["kill xp per victim level"] = 20,
    ["quest base xp"] = 125,
    ["quest xp per stage"] = 30,
    -- X050: native kill/quest rewards are validated and split by groupHelper.
    ["server party xp"] = true,
    ["travel xp"] = 25,
    ["travel xp chance"] = 0.20,
    ["travel xp cooldown hours"] = 2.0,
    ["trade bonus xp"] = 20,
    ["trade bonus xp chance"] = 0.20,
    ["critical bonus xp"] = 15,
    ["critical bonus xp chance"] = 0.35,
    ["theft bonus xp"] = 20,
    ["theft bonus xp chance"] = 1.00, -- Y092 compatibility: reward is once per theft source, not RNG-gated
    ["skill book xp"] = 25,
    ["lore book xp"] = 5,
    ["death xp loss fraction"] = 1.0, -- compatibility; Y012 always wipes current-level XP
    ["zero xp death cooldown per level"] = 5,
    ["attribute progress major"] = 0.30,
    ["attribute progress minor"] = 0.15,
    ["attribute progress misc"] = 0.05,
    ["attribute specialization multiplier"] = 1.25,
    ["attribute progress multiplier"] = 1.0
}

-- ArenaMP FIX26 combat AI. Keep these [Game] values identical on every client
-- so an authority handoff cannot change how the same NPC chooses attacks/spells.
config.arenaNpcsUseBestAttack = true
config.arenaCombatMagicBias = 1.40
config.arenaCombatHealThreshold = 0.65

-- Server-authoritative Armor/Weapon Requirements.
-- These are sent to [Equipment Requirements] on every client. Editing a
-- local settings.cfg therefore cannot lower requirements on this server.
-- weapon/armor requirement multiplier is the simple global difficulty knob:
-- 1.0 = normal, 1.2 = 20% harder, 0.8 = 20% easier.
config.equipmentRequirements = {
    ["enabled"] = true,
    ["armor enabled"] = true,
    ["weapon enabled"] = true,
    ["tooltip enabled"] = true,
    ["use modified stats"] = true,
    ["heavy armor enabled"] = true,
    ["medium armor enabled"] = true,
    ["light armor enabled"] = true,
    ["bound armor requirements"] = true,
    ["bound weapon requirements"] = true,
    ["automatic calculation"] = true,
    ["weapon requirement multiplier"] = 1.0,
    ["armor requirement multiplier"] = 1.0,
    ["automatic minimum skill"] = 10,
    ["automatic maximum skill"] = 90,
    ["automatic minimum attribute"] = 10,
    ["automatic maximum attribute"] = 80,
    ["automatic requirement step"] = 5,
    ["automatic skill curve"] = 1.35,
    ["automatic attribute curve"] = 1.50,
    ["automatic weapon damage scale"] = 28.0,
    ["automatic weapon dps scale"] = 35.0,
    ["automatic weapon reach range"] = 1.25,
    ["automatic weapon durability reference"] = 6000.0,
    ["automatic weapon value reference"] = 100000.0,
    ["automatic weapon weight scale"] = 1.0,
    ["automatic weapon damage influence"] = 0.42,
    ["automatic weapon dps influence"] = 0.18,
    ["automatic weapon reach influence"] = 0.10,
    ["automatic weapon durability influence"] = 0.12,
    ["automatic weapon value influence"] = 0.10,
    ["automatic weapon weight influence"] = 0.08,
    ["automatic weapon attribute power influence"] = 0.45,
    ["automatic weapon attribute weight influence"] = 0.45,
    ["automatic weapon attribute reach influence"] = 0.10,
    ["automatic armor protection scale"] = 55.0,
    ["automatic armor durability reference"] = 6000.0,
    ["automatic armor value reference"] = 100000.0,
    ["automatic light armor weight reference"] = 8.0,
    ["automatic medium armor weight reference"] = 18.0,
    ["automatic heavy armor weight reference"] = 38.0,
    ["automatic armor protection influence"] = 0.58,
    ["automatic armor durability influence"] = 0.17,
    ["automatic armor value influence"] = 0.12,
    ["automatic armor weight influence"] = 0.13,
    ["automatic armor attribute power influence"] = 0.28,
    ["automatic armor attribute weight influence"] = 0.60,
    ["automatic armor attribute slot influence"] = 0.12,
    ["heavy tier 2 armor"] = 16,
    ["heavy tier 3 armor"] = 59,
    ["heavy tier 4 armor"] = 65,
    ["medium tier 2 armor"] = 15,
    ["medium tier 3 armor"] = 39,
    ["medium tier 4 armor"] = 44,
    ["light tier 2 armor"] = 8,
    ["light tier 3 armor"] = 19,
    ["light tier 4 armor"] = 44,
    ["heavy tier 2 skill"] = 30,
    ["heavy tier 3 skill"] = 60,
    ["heavy tier 4 skill"] = 80,
    ["heavy tier 2 attribute"] = 30,
    ["heavy tier 3 attribute"] = 60,
    ["heavy tier 4 attribute"] = 80,
    ["medium tier 2 skill"] = 30,
    ["medium tier 3 skill"] = 60,
    ["medium tier 4 skill"] = 80,
    ["medium tier 2 attribute"] = 30,
    ["medium tier 3 attribute"] = 60,
    ["medium tier 4 attribute"] = 80,
    ["light tier 2 skill"] = 30,
    ["light tier 3 skill"] = 60,
    ["light tier 4 skill"] = 80,
    ["light tier 2 attribute"] = 30,
    ["light tier 3 attribute"] = 60,
    ["light tier 4 attribute"] = 80,
    ["weapon tier 1 skill"] = 0,
    ["weapon tier 2 skill"] = 30,
    ["weapon tier 3 skill"] = 60,
    ["weapon tier 4 skill"] = 80,
    ["weapon tier 1 attribute"] = 0,
    ["weapon tier 2 attribute"] = 30,
    ["weapon tier 3 attribute"] = 60,
    ["weapon tier 4 attribute"] = 80,
    ["axe 1h tier 2 damage"] = 10,
    ["axe 1h tier 3 damage"] = 17,
    ["axe 1h tier 4 damage"] = 19,
    ["axe 2h tier 2 damage"] = 17,
    ["axe 2h tier 3 damage"] = 22,
    ["axe 2h tier 4 damage"] = 38,
    ["mace tier 2 damage"] = 5,
    ["mace tier 3 damage"] = 10,
    ["mace tier 4 damage"] = 999,
    ["hammer tier 2 damage"] = 15,
    ["hammer tier 3 damage"] = 22,
    ["hammer tier 4 damage"] = 999,
    ["staff tier 2 damage"] = 6,
    ["staff tier 3 damage"] = 8,
    ["staff tier 4 damage"] = 999,
    ["long blade 1h tier 2 damage"] = 10,
    ["long blade 1h tier 3 damage"] = 16,
    ["long blade 1h tier 4 damage"] = 999,
    ["long blade 2h tier 2 damage"] = 13,
    ["long blade 2h tier 3 damage"] = 20,
    ["long blade 2h tier 4 damage"] = 30,
    ["short blade tier 2 damage"] = 6,
    ["short blade tier 3 damage"] = 12,
    ["short blade tier 4 damage"] = 999,
    ["spear tier 2 damage"] = 13,
    ["spear tier 3 damage"] = 18,
    ["spear tier 4 damage"] = 999,
    ["bow tier 2 damage"] = 10,
    ["bow tier 3 damage"] = 17,
    ["bow tier 4 damage"] = 24,
    ["crossbow tier 2 damage"] = 15,
    ["crossbow tier 3 damage"] = 28,
    ["crossbow tier 4 damage"] = 37,
    ["thrown tier 2 damage"] = 3,
    ["thrown tier 3 damage"] = 5,
    ["thrown tier 4 damage"] = 999,
}

-- Arrow Stick changes world item creation, so it is also server-controlled.
config.arrowStick = {
    ["enabled"] = true,
    ["stick chance"] = 1.0, -- -1 uses vanilla fProjectileThrownStoreChance
    ["stick aoe enchantments"] = false,
    ["stick underwater"] = false,
}


-- Refined Alchemy gameplay is server-authoritative. Local settings.cfg values
-- are fallbacks only and are overwritten on connection.
config.refinedAlchemy = {
    ["learn from eating"] = true,
    ["learn from brewing"] = true,
    ["learn from skill"] = true,
    ["learning confirmations"] = 3,
    ["synergy"] = true,
    ["ingredient bonus"] = true,
    ["failure mode"] = 1,
    ["max poison charges"] = 5,
    ["skill reveal effect 1"] = 15,
    ["skill reveal effect 2"] = 30,
    ["skill reveal effect 3"] = 45,
    ["skill reveal effect 4"] = 60,
    ["fatigue affects success"] = true,
}

config.alchemyGameplay = {
    ["ingredient value power cap"] = 500,
}

-- Player-versus-player damage policy.
-- "disabled": players cannot harm other players.
-- "enabled":  all player-versus-player damage is allowed.
-- "group":    damage is blocked only between allied players (/invite + /join).
config.friendlyFireMode = "group"

-- Keep values loaded from a manually edited config.lua inside the ranges that
-- the ArenaMP engine and launcher support. Invalid values fall back to the
-- defaults above instead of sending malformed settings to every client.
local function clampNumber(value, minimum, maximum, fallback)
    if type(value) ~= "number" or value ~= value then
        return fallback
    end

    if value < minimum then
        return minimum
    elseif value > maximum then
        return maximum
    end

    return value
end

local function clampInteger(value, minimum, maximum, fallback)
    return math.floor(clampNumber(value, minimum, maximum, fallback) + 0.5)
end

config.arenaCombatWeaponSheatheDelay = clampNumber(config.arenaCombatWeaponSheatheDelay, 0, 60, 2.0)
config.arenaNpcDamageLowLevelMultiplier = clampNumber(config.arenaNpcDamageLowLevelMultiplier, 0, 5, 0.75)
config.arenaNpcDamageLevel100Multiplier = clampNumber(config.arenaNpcDamageLevel100Multiplier, 0, 5, 1.00)
config.arenaNpcDamageOver100Multiplier = clampNumber(config.arenaNpcDamageOver100Multiplier, 0, 5, 1.05)
config.arenaCombatPursuitGuaranteedDistance = clampInteger(config.arenaCombatPursuitGuaranteedDistance, 0, 100000, 200)
config.arenaCombatPursuitDoorMaxDistance = clampInteger(config.arenaCombatPursuitDoorMaxDistance,
    config.arenaCombatPursuitGuaranteedDistance, 100000, 800)
config.arenaCombatPursuitMinimumChance = clampNumber(config.arenaCombatPursuitMinimumChance, 0, 1, 0.10)
config.arenaCombatPursuitMaxActors = clampInteger(config.arenaCombatPursuitMaxActors, 0, 100, 3)
config.arenaCombatPursuitMaxDistance = clampInteger(config.arenaCombatPursuitMaxDistance, 0, 1000000, 4000)
config.arenaActorsProcessingRange = clampInteger(config.arenaActorsProcessingRange, 500, 1000000, 4000)
if config.arenaCombatPursuitMaxDistance > 0 then
    config.arenaActorsProcessingRange = math.max(config.arenaActorsProcessingRange,
        config.arenaCombatPursuitMaxDistance)
end
config.arenaActorsProcessingRange = math.max(config.arenaActorsProcessingRange,
    config.arenaCombatPursuitDoorMaxDistance)
config.arenaGlobalXpMultiplier = clampNumber(config.arenaGlobalXpMultiplier, 0.01, 100, 1.0)
config.arenaXpRateMultiplier = clampNumber(config.arenaXpRateMultiplier, 0.05, 10.0, 1.0)
config.xpLeveling["xp gain multiplier"] = config.arenaXpRateMultiplier
config.rawConfigReloadInterval = clampInteger(config.rawConfigReloadInterval, 1, 60, 2)

local serverLanguageAliases = {
    auto = "AUTO", client = "AUTO", clients = "AUTO", detected = "AUTO",
    ru = "RU", russian = "RU", ["ru-ru"] = "RU",
    en = "EN", english = "EN", ["en-us"] = "EN", ["en-gb"] = "EN"
}

if type(config.serverLanguage) == "string" then
    config.serverLanguage = serverLanguageAliases[string.lower(config.serverLanguage)] or "AUTO"
else
    config.serverLanguage = "AUTO"
end

if type(config.defaultLanguage) == "string" and string.upper(config.defaultLanguage) == "RU" then
    config.defaultLanguage = "RU"
else
    config.defaultLanguage = "EN"
end

local friendlyFireAliases = {
    disabled = "disabled", off = "disabled", ["false"] = "disabled", ["0"] = "disabled",
    enabled = "enabled", on = "enabled", ["true"] = "enabled", ["1"] = "enabled",
    group = "group", party = "group", allies = "group", ally = "group"
}

if type(config.friendlyFireMode) == "string" then
    config.friendlyFireMode = friendlyFireAliases[string.lower(config.friendlyFireMode)] or "group"
else
    config.friendlyFireMode = "group"
end

-- The game settings to enforce for players
-- Any regular OpenMW [Game] setting can be added to this table. ArenaMP settings
-- above are inserted automatically so they remain easy to edit from config.lua
-- and from the ArenaMP launcher.
config.gameSettings = {
    { name = "best attack", value = false },
    { name = "prevent merchant equipping", value = true },
    { name = "enchanted weapons are magical", value = true },
    { name = "rebalance soul gem values", value = true },
    { name = "barter disposition change is permanent", value = true },
    { name = "strength influences hand to hand", value = 1 },
    { name = "use magic item animations", value = true },
    { name = "normalise race speed", value = false },
    { name = "uncapped damage fatigue", value = false },
    { name = "swim upward correction", value = false },
    { name = "trainers training skills based on base skill", value = true },
    { name = "projectiles enchant multiplier", value = 1 },
    { name = "always allow stealing from knocked out actors", value = false }
}

local function setGameSetting(name, value)
    for _, setting in ipairs(config.gameSettings) do
        if setting.name == name then
            setting.value = value
            return
        end
    end

    table.insert(config.gameSettings, { name = name, value = value })
end

-- ID_GAME_SETTINGS carries arbitrary string keys. ArenaMP keeps protocol number 806; clients route
-- keys in this form to the requested Settings::Manager category, letting the
-- server authoritatively configure gameplay categories beyond [Game].
local function setCategorySetting(category, name, value)
    setGameSetting("@ArenaMP|" .. category .. "|" .. name, value)
end

setGameSetting("tactical combat", config.arenaTacticalCombat)
setGameSetting("combat weapon sheathe delay", config.arenaCombatWeaponSheatheDelay)
setGameSetting("combat pursuit through doors", config.arenaCombatPursuitThroughDoors)
setGameSetting("combat pursuit guaranteed distance", config.arenaCombatPursuitGuaranteedDistance)
setGameSetting("combat pursuit door max distance", config.arenaCombatPursuitDoorMaxDistance)
setGameSetting("combat pursuit minimum chance", config.arenaCombatPursuitMinimumChance)
setGameSetting("combat pursuit max actors", config.arenaCombatPursuitMaxActors)
setGameSetting("combat pursuit max distance", config.arenaCombatPursuitMaxDistance)
setGameSetting("followers attack on sight", config.arenaFollowersAttackOnSight)
setGameSetting("NPCs avoid collisions", config.arenaNpcAvoidCollisions)
setGameSetting("NPCs give way", config.arenaNpcGiveWay)
setGameSetting("allow actors to follow over water surface", config.arenaAllowActorsFollowOverWater)
setGameSetting("actors processing range", config.arenaActorsProcessingRange)
setGameSetting("can loot during death animation", config.arenaCanLootDuringDeathAnimation)
setGameSetting("weapon sheathing", config.arenaWeaponSheathing)
setGameSetting("shield sheathing", config.arenaShieldSheathing)
setGameSetting("graphic herbalism", config.arenaGraphicHerbalism)
setGameSetting("long blades use agility for damage scaling", config.arenaLongBladesUseAgility)
setGameSetting("two handed weapons receive an accuracy penalty", config.arenaTwoHandedAccuracyPenalty)
setGameSetting("staves receive accuracy bonus instead of two handed penalty", config.arenaStavesAccuracyBonus)
setGameSetting("skill books have level limit", config.arenaSkillBooksLevelLimit)
setGameSetting("use new constant effect difficulty logic", config.arenaNewConstantEffectDifficulty)
setGameSetting("global XP gain multiplier", config.arenaGlobalXpMultiplier)
setGameSetting("npcs use best attack", config.arenaNpcsUseBestAttack)
setGameSetting("combat magic bias", config.arenaCombatMagicBias)
setGameSetting("combat heal threshold", config.arenaCombatHealThreshold)
setGameSetting("npc damage low level multiplier", config.arenaNpcDamageLowLevelMultiplier)
setGameSetting("npc damage level 100 multiplier", config.arenaNpcDamageLevel100Multiplier)
setGameSetting("npc damage over 100 multiplier", config.arenaNpcDamageOver100Multiplier)
setGameSetting("friendly fire mode", config.friendlyFireMode)

for name, value in pairs(config.xpLeveling) do
    setCategorySetting("XP Leveling", name, value)
end

for name, value in pairs(config.equipmentRequirements) do
    setCategorySetting("Equipment Requirements", name, value)
end

for name, value in pairs(config.arrowStick) do
    setCategorySetting("Arrow Stick", name, value)
end


for name, value in pairs(config.refinedAlchemy) do
    setCategorySetting("ArenaMW Alchemy", name, value)
end

for name, value in pairs(config.alchemyGameplay) do
    setCategorySetting("Alchemy", name, value)
end

-- The VR settings to enforce for players
config.vrSettings = {
    { name = "realistic combat minimum swing velocity", value = 1.0 },
    { name = "realistic combat maximum swing velocity", value = 4.0 }
}

-- The world time used for a newly created world
config.defaultTimeTable = { year = 427, month = 7, day = 16, hour = 9,
    daysPassed = 1, dayTimeScale = 30, nightTimeScale = 40 }

-- The chat window instructions that show up when players join the server.
-- Keep the original compact ArenaMP/Nirn formatting here so it is easy to edit.
config.chatWindowInstructions = color.SkyBlue .. "[" .. color.Turquoise .. "Y" .. color.SkyBlue .. ": нажмите — быстрый ввод в чат; удерживайте — меню игрока]\n"

-- English equivalent used only for EN clients when per-client localization is enabled.
config.chatWindowInstructionsEN = color.SkyBlue .. "[" .. color.Turquoise .. "Y" .. color.SkyBlue .. ": tap — quick chat input; hold — Player Menu]\n"

-- ArenaMP X049: radius in Morrowind world units for the Local channel.
-- Local messages are also restricted to the same cell. 5000 is roughly a
-- conversation / nearby-area distance without becoming a zone-wide broadcast.
config.localChatRadius = 5000

-- ArenaMP X052: nickname colours in chat.
--   enabled              - master switch for the whole feature;
--   assignOnFirstLogin   - hand out an unused palette colour to new players so
--                          a fresh server is readable without anyone opening
--                          the menu; set to false to start everyone on white;
--   escapeUserColorCodes - rewrite a bare "#" typed by a player into "##" so a
--                          message cannot inject MyGUI colour tags and imitate
--                          another player's colour or a staff rank.
config.chatNameColors = {
    enabled = true,
    assignOnFirstLogin = true,
    escapeUserColorCodes = true
}

-- Compact pre-login greeting shown before the password/registration dialog.
-- Placeholders: {name} = player name, {count} = online count, {seconds} = login timeout.
config.startupMessage = {
    EN = {
        login = color.SkyBlue .. "Welcome to the server" .. color.Default .. "\nArenaMP, " ..
            color.Turquoise .. "{name}" .. color.Default .. "\n" .. color.SkyBlue .. "Players online: " ..
            color.Turquoise .. "{count}" .. color.SkyBlue .. ".\nYou have " .. color.Turquoise .. "{seconds}" ..
            color.SkyBlue .. " sec. to" .. color.Default .. " log in.\n",
        register = color.SkyBlue .. "Welcome to the server" .. color.Default .. "\nArenaMP, " ..
            color.Turquoise .. "{name}" .. color.Default .. "\n" .. color.SkyBlue .. "Players online: " ..
            color.Turquoise .. "{count}" .. color.SkyBlue .. ".\nYou have " .. color.Turquoise .. "{seconds}" ..
            color.SkyBlue .. " sec. to" .. color.Default .. " register.\n"
    },
    RU = {
        login = color.SkyBlue .. "Добро пожаловать на сервер" .. color.Default .. "\nArenaMP, " ..
            color.Turquoise .. "{name}" .. color.Default .. "\n" .. color.SkyBlue .. "На сервере сейчас " ..
            color.Turquoise .. "{count}" .. color.SkyBlue .. " игрок(ов).\nУ вас есть " .. color.Turquoise .. "{seconds}" ..
            color.SkyBlue .. " сек. чтобы" .. color.Default .. " авторизоваться.\n",
        register = color.SkyBlue .. "Добро пожаловать на сервер" .. color.Default .. "\nArenaMP, " ..
            color.Turquoise .. "{name}" .. color.Default .. "\n" .. color.SkyBlue .. "На сервере сейчас " ..
            color.Turquoise .. "{count}" .. color.SkyBlue .. " игрок(ов).\nУ вас есть " .. color.Turquoise .. "{seconds}" ..
            color.SkyBlue .. " сек. чтобы" .. color.Default .. " зарегистрироваться.\n"
    }
}


-- X035: server-authored quest extension layer. Definitions live in
-- server/data/custom/quests and player progress lives in player customVariables.
-- Moderators can always create/edit drafts. Set moderatorsCanPublish=false if
-- an administrator must approve every publish operation.
config.serverQuests = {
    enabled = true,
    moderatorsCanPublish = true,
    greenTopicColor = "#61D879",
    maxSyncedTopicsPerNpc = 64,

    -- X050b: runtime quest notifications are intentionally quiet by default.
    -- The master flag suppresses every automatic [Quest] line while leaving
    -- explicit /quest and Quest Studio feedback intact. Category flags become
    -- active only when chatNotificationsEnabled=true.
    chatNotificationsEnabled = false,
    chatProgressMessages = false,
    chatRewardMessages = false,
    chatRuntimeErrors = false,

    -- Keep one compact journal popup without mirroring the same text into chat.
    journalUpdatePopup = true
}

-- X042: modifying original Morrowind journal stages is disabled by default.
-- Add exact vanilla quest IDs here only when an administrator intentionally
-- wants a server quest to advance those original quests.
config.serverQuestVanillaJournalWhitelist = {}

-- Exactly as in the old server: keep this effectively empty so no second startup banner is printed.
config.startupScriptsInstructions = color.SkyBlue .. " \n"


-- Active content collection: NIRN (requiredDataFiles.json already supplied by server(2)).
-- Do not run MFR startup scripts or create its globals against Nirn_Core.ESP.
config.contentCollection = "NIRN"
config.mfrCompatibility = { enabled = false }


-- Which ingame startup scripts should be run via the /runstartup command
-- Note: These affect the world and must not be run for every player who joins.
config.worldStartupScripts = {"Startup", "BMStartUpScript"}

-- Which ingame startup scripts should be run on every player who joins
-- Note: These pertain to game mechanics that wouldn't work otherwise, such as vampirism checks
config.playerStartupScripts = {"VampireCheck", "WereCheckScript"}

-- Whether the world time should continue passing when there are no players on the server
config.passTimeWhenEmpty = false

-- The hours at which night is regarded as starting and ending, used to pass time using a
-- different timescale when it's night
config.nightStartHour = 20
config.nightEndHour = 6

-- Whether players should be allowed to use the ingame tilde (~) console by default
config.allowConsole = false

-- Whether players should be allowed to rest in bed by default
config.allowBedRest = true

-- Whether players should be allowed to rest in the wilderness by default
config.allowWildernessRest = false

-- Whether players should be allowed to wait by default
config.allowWait = false

-- Whether journal entries should be shared across the players on the server or not
-- Y032: MMO keeps story progression personal. CO-OP can enable this through
-- the Launcher gameplay preset.
config.shareJournal = false

-- Whether faction ranks should be shared across the players on the server or not
config.shareFactionRanks = false

-- Whether faction expulsion should be shared across the players on the server or not
config.shareFactionExpulsion = false

-- Whether faction reputation should be shared across the players on the server or not
config.shareFactionReputation = false

-- Whether dialogue topics should be shared across the players on the server or not
-- Y032: MMO keeps known dialogue topics personal.
config.shareTopics = false

-- Whether crime bounties should be shared across players on the server or not
config.shareBounty = false

-- Whether reputation should be shared across players on the server or not
config.shareReputation = false

-- Whether map exploration should be shared across players on the server or not
config.shareMapExploration = false

-- Whether ingame videos should be played for other players when triggered by one player
config.shareVideos = false

-- Which clientside script records should be blanked out so they are not run
-- Note: By default, the original character generation scripts are included
--       because they're not suitable for multiplayer
config.disabledClientScriptIds = {
    -- original character generation's scripts
    "CharGenRaceNPC", "CharGenClassNPC", "CharGenStatsSheet", "CharGenDoorGuardTalker",
    "CharGenBed", "CharGenStuffRoom", "CharGenFatigueBarrel", "CharGenDialogueMessage",
    "CharGenDoorExitCaptain", "CharGenJournalMessage",
    -- OpenMW's default blacklist
    "Museum", "MockChangeScript", "doortestwarp", "WereChange2Script", "wereDreamScript2",
    "wereDreamScript3"
}

-- Which clientside scripts should have all of their variables synchronized across players
-- Warning: Make sure whatever scripts you add in here don't cause infinite packet spam
--          through variable changes that clients cannot agree on
-- Start of AMP addition
--
-- Whether only the cell authority is believed about the local variables of scripts running
-- on world objects.
--
-- Local scripts run on every client that has the cell loaded, so with this off, two players
-- standing in the same room both advance the same quest timer and both report it, which is
-- what makes timed quest stages run at multiple times normal speed and makes counters
-- oscillate instead of settling. Leave it on unless you are debugging.
config.enforceScriptAuthority = true

-- Whether the local variables of scripts attached to a character are stored in that
-- character's profile rather than in whatever cell they were standing in.
--
-- With this off, every player's copy of the same script shares one entry in the cell data
-- and they overwrite each other's quest state.
config.syncPlayerScriptLocals = true

-- H014 pilot: observe script-local packets (one log line per script and cell).
-- Turn off after investigation; no automatic personalization of unknown scripts.
config.personalScriptLocalsDiagnostics = true
-- Only EXACT reviewed (cell + ref + script + unique index) matches are
-- personalized. Global quest stages, NPC AI, mechanisms and timers stay shared.
-- To disable the pilot without deleting stored progress, use an empty table.
config.personalScriptLocalRules = {}


-- End of AMP addition

config.synchronizedClientScriptIds = {
    -- mechanisms
    "GG_OpenGate1", "GG_OpenGate2", "Arkn_doors", "nchuleftingthWrong1", "nchuleftingthWrong2",
    "nchulfetingthRight", "Akula_innerdoors", "Dagoth_doors", "SothaLever1", "SothaLever2",
    "SothaLever3", "SothaLever4", "SothaLever5", "SothaLever6", "SothaLever7", "SothaLever8",
    "SothaLever9", "SothaLever10", "SothaLever11", "SothaOilLever", "LocalState",
    -- quest stages and timers
    "helsethScript", "KarrodMovement"
}


-- ArenaMP C21: mechanism/local-state scripts discovered from the supplied
-- Tamriel_Data.esm and TR_Mainland.esm. Only scripts attached to physical
-- world records and using local state + world-changing commands were selected.
-- This avoids synchronizing generic NPC/UI/timer scripts.
do
    local c21SynchronizedClientScriptIds = {
        -- Tamriel_Data.esm
        "T_ScObj_DaeWardA",
        "T_ScObj_DaeWardB",
        "T_ScObj_DaeWardC",
        "T_ScObj_DaeWardD",
        "T_ScObj_DaeWardE",
        "T_ScObj_DaeWardF",
        "T_ScObj_DaeWardG",
        "T_ScObj_DaeWardH",
        "T_ScObj_DaeWardI",
        "T_ScObj_DaeWardJ",
        "T_ScObj_DaeWardK",
        "T_ScObj_DaeWardL",
        "T_ScObj_DaeWardM",
        "T_ScObj_DaeWardN",
        "T_ScObj_DaeWardO",
        "T_ScObj_DaeWardP",
        "T_ScObj_DaeWardQ",
        "T_ScObj_DaeWardR",
        "T_ScObj_DaeWardS",
        "T_ScObj_DaeWardT",
        "T_ScObj_DaeWardU",
        "T_ScObj_DaeWardV",
        "T_ScObj_DaeWardW",
        "T_ScObj_DaeWardZ",
        -- TR_Mainland.esm
        "TR_FM_Act_RecallFixer_FMGH_sc",
        "TR_FM_Act_RecallFixer_FMK_sc",
        "TR_FM_Act_RecallFixer_FMKT_sc",
        "TR_FM_Act_RecallFixer_SWI_sc",
        "TR_FM_Act_RecallFixer_SWLH_sc",
        "TR_FM_Act_SkullTrap_sc",
        "TR_m1_bthalastatue_sc",
        "TR_m1_bthalight_sc",
        "TR_m1_FW_TG6_BoneFragScr",
        "TR_M1_FW_TG6_Display",
        "TR_m1_IL_teleportscript",
        "TR_m1_q_FG_BoatStuffDisable",
        "TR_m2_445_sc_switch",
        "TR_m2_445_sc_templegrate",
        "TR_M2_MzankhDoorCrank_sc",
        "TR_m3_Btharch_Mousetrap1",
        "TR_m3_BtharchPipe_sc",
        "TR_m3_Hal_FelmsShrineAct_sc",
        "TR_m3_HH_GM_ritualshrine_scr",
        "TR_m3_MG_OE_Q7_CeilingDoor4_sc",
        "TR_m3_MG_OE_Q7_CeilingDoor5_sc",
        "TR_m3_MoraAshpitEmpty_sc",
        "TR_m3_MoraTSancGGFence_sc",
        "TR_m3_OE_CuriaVaultHole01SCP",
        "TR_m3_OE_CuriaVaultHole02SCP",
        "TR_m3_OE_CuriaVaultResetSCP",
        "TR_m3_OE_fiendchest_scr1",
        "TR_m3_OE_fiendchest_scr2",
        "TR_m3_OE_fiendchest_scr3",
        "TR_m3_OE_GhoulBusines_Coffin",
        "TR_m3_OE_RumaDisableSCP",
        "TR_m3_OE_TG_RaathimTrapSc",
        "TR_m3_Oth_TT_Punavit_1_sc",
        "TR_m3_Oth_TT_Punavit_2_sc",
        "TR_m3_speakercandle_scr",
        "TR_m3_TT_Dar_2_Sand_sc",
        "TR_m3_Vr_BoundHelp_Cart",
        "TR_m4_AA_DamiloCrate_scp",
        "TR_m4_AA_DeadBelBetu_scp",
        "TR_m4_AA_MasterSehutu_Script",
        "TR_m4_AA_SehutuBarrier_scp",
        "TR_m4_AA_SehutuLid1Script",
        "TR_m4_AA_SehutuRod2_Scp",
        "TR_m4_AA_SehutuRod_Scp",
        "TR_m4_AA_SehutuTalk02_scp",
        "TR_m4_AA_SehutuTalk06_scp",
        "TR_m4_AlynuChestScript",
        "TR_m4_AlynuPileScript",
        "TR_m4_And_IncThreat_Dw_sc",
        "TR_m4_AndasHiddenSwitch_scp",
        "TR_m4_AndasPrayerStool_scp",
        "TR_m4_AndasSewerCrank2_script",
        "TR_m4_AndasSewerCrank_script",
        "TR_m4_AndasSewerLock_Script",
        "TR_m4_Ando_FaulerChestScript",
        "TR_m4_Ando_NevusaBarrelScript",
        "TR_m4_AndoHH_CrateDisable",
        "TR_m4_AndoHH_ShipDoorScript",
        "TR_m4_AndoHH_SujammaCrate",
        "TR_m4_Andoth_AndasLift_scp",
        "TR_m4_Andoth_Rivenwake_Scp",
        "TR_m4_AndothDweBarDart1_scp",
        "TR_m4_AndothDweCoinPurse01_scp",
        "TR_m4_AndothDweCrank_scp",
        "TR_m4_AndothDweDummy01_scp",
        "TR_m4_AndothDweLift_scp",
        "TR_m4_AndothDweLock01_scp",
        "TR_m4_BahrundActivator_scp",
        "TR_m4_Bal_AmuletCaster_Sc",
        "TR_m4_FelmsCargoLift_scp",
        "TR_m4_HH_DelayedCaravanSc",
        "TR_m4_HH_GreefCrate",
        "TR_m4_HH_GreefDisable",
        "TR_m4_HH_SavrethiDeskScript",
        "TR_m4_NirnboundBoulder_Scp",
        "TR_m4_NirnboundDisabled_Scp",
        "TR_m4_OmaynisInnBanner_script",
        "TR_m4_orlukhgate01_scr",
        "TR_m4_orlukhgate02_scr",
        "TR_m4_q_AAB_containerscript",
        "TR_m4_ShalmuratCrank_script",
        "TR_m4_ShalmuratLock_Script",
        "TR_m4_T_Nuccius_mover_1_sc",
        "TR_m4_TG_BaseNPCMover_scp",
        "TR_m4_TG_GeldrasUrn_scp",
        "TR_m4_TG_GeldrasUrnTracker_scp",
        "TR_m4_TG_VendicciCoach01_scp",
        "TR_m4_TG_VendicciCoach02_scp",
        "TR_m4_TJ_GhostFence_sc",
        "TR_m4_TT_DonationBoxScript",
        "TR_m4_TT_LastWillEnable_sc",
        "TR_m4_TT_OlmsDoor_sc",
        "TR_m4_TT_TempleCoffers_sc",
        "TR_m4_TT_VathriDoorLocked",
        "TR_m4_TT_VathriDoorOrig",
        "TR_m4_UshkuKurPlat_Scp",
        "TR_m4_UshuKurPlat2_Scp",
        "TR_m4_UshuKurRockFall_scp",
        "TR_m4_UshuKurRockSink_scp",
        "TR_m4_VA_andasbattle_scr",
        "TR_m7_HH_Alvynu2_Desk_sc",
        "TR_m7_HH_Alvynu_4_Urn_sc",
        "TR_m7_HH_Alvynu_7_Ship_sc",
        "TR_m7_HH_Alvynu_7_ShipExpls_sc",
        "TR_m7_HH_GM_perfume_scr",
        "TR_m7_HOGuide_Ruin_scr",
        "TR_m7_Ns_ArenaDuelManager_sc",
        "TR_m7_Ns_MG_Arch02_Cont_sc",
        "TR_m7_Oth_MG_ScVandirRubble",
        "TR_RothRoryn_Monastery_AlmSc",
    }

    local known = {}
    for _, scriptId in ipairs(config.synchronizedClientScriptIds) do
        known[string.lower(scriptId)] = true
    end

    for _, scriptId in ipairs(c21SynchronizedClientScriptIds) do
        local key = string.lower(scriptId)
        if not known[key] then
            table.insert(config.synchronizedClientScriptIds, scriptId)
            known[key] = true
        end
    end
end

-- Location shown behind the login/register interface before authentication.
-- "default" keeps the client hardcoded exterior cell 0, -7.
-- Other examples: "0, 0", "Balmora", "Balmora, Guild of Mages".
config.startLocation = "Seyda Neen"

-- Per-player persistent interior instances. The first entry makes Caius Cosades'
-- house private for every account while keeping the original interior as the
-- template. Destination overrides route normal doors directly to the player's
-- own copy; eventHandler also catches coc/script/admin teleports as a fallback.
config.privateCellInstances = {
    caiusHouse = {
        enabled = true,
        baseCellDescription = "Balmora, Caius Cosades' House",
        instanceSuffix = " - Instance for ",
        neverReset = true,
        noticeKey = "private_caius_instance_notice",
        noticeEveryEntry = false
    }
}

-- Whether the instanced spawn should be used instead of the noninstanced one
config.useInstancedSpawn = true

-- Where players will be spawned if an instanced spawn is desired, with a different clean copy of
-- this cell existing for each player
-- Warning: Only interior cells can be instanced
config.instancedSpawn = {
    cellDescription = "Seyda Neen, Census and Excise Office",
    position = {1130.3388671875, -387.14947509766, 193},
    rotation = {0.09375, 1.5078122615814},
    text = "Multiplayer skips several minutes of the game's introduction and places you at the first quest giver." ..
        "\n\nYou will be able to meet other players only after you leave this room.",
    items = {{refId = "chargen statssheet", count = 1, charge = -1, enchantmentCharge = -1, soul = ""}}    
}

-- Where players will be spawned if an instanced spawn is not desired
config.noninstancedSpawn = {
    cellDescription = "-3, -2",
    position = {-23894.0, -15079.0, 505},
    rotation = {0, 1.2},
    text = "Multiplayer skips over the original character generation." ..
        "\n\nAs a result, you start out with Caius Cosades' package.",
    items = {{refId = "bk_a1_1_caiuspackage", count = 1, charge = -1, enchantmentCharge = -1, soul = ""}}
}

-- The location that players respawn at, unless overridden below by other respawn options
config.defaultRespawn = {
    cellDescription = "Balmora, Temple",
    position = {4700.5673828125, 3874.7416992188, 14758.990234375},
    rotation = {0.25314688682556, 1.570611000061}
}

-- Whether the default respawn location should be ignored in favor of respawning the
-- player at the nearest Imperial shrine
config.respawnAtImperialShrine = true

-- Whether the default respawn location should be ignored in favor of respawning the
-- player at the nearest Tribunal temple
-- Note: When both this and the Imperial shrine option are enabled, there is a 50%
--       chance of the player being respawned at either
config.respawnAtTribunalTemple = true

-- The cells that players are forbidden from entering, with any attempt to enter them
-- transporting them to the last location in their previous cell
config.forbiddenCells = { "ToddTest" }

-- The maximum value that any attribute except Speed is allowed to have
config.maxAttributeValue = 200

-- The maximum value that Speed is allowed to have
-- Note: Speed is given special treatment because of the Boots of Blinding Speed
config.maxSpeedValue = 365

-- The maximum value that any skill except Acrobatics is allowed to have
config.maxSkillValue = 200

-- The maximum value that Acrobatics is allowed to have
-- Note: Acrobatics is given special treatment because of the Scroll of Icarian Flight
config.maxAcrobaticsValue = 1200

-- Allow modifier values to bypass allowed skill values
-- Y092: legacy option; caps now always apply only to base values.
config.ignoreModifierWithMaxSkill = false

-- The refIds of items that players are not allowed to equip for balancing reasons
config.bannedEquipmentItems = { "helseth's ring" }

-- Whether players should respawn when dying
config.playersRespawn = true

-- Time to stay dead before being respawned, in seconds. Y039 never auto-respawns
-- before the XP decay window below has completed.
config.deathTime = 30

-- Y039 death recovery. Current-level XP drains smoothly to zero over the first
-- 30 seconds after death. Restore Health potions or a nearby player can
-- recover the player in place before the normal respawn timer expires.
config.deathRecovery = {
    ["xp decay seconds"] = 30,
    ["xp tick seconds"] = 1,
    ["potion revive health fraction"] = 0.25,
    ["touch revive health fraction"] = 0.10,
    ["ally revive distance"] = 256,

    -- U035: after any successful recovery (own potion or an ally) the player
    -- cannot be revived again for this many seconds. Dying inside the window is
    -- a plain death: the incapacitated state is not entered at all, so nobody
    -- lies there pressing E for nothing. Set to 0 to disable.
    ["revive lockout seconds"] = 60,

    -- Y040: the revive request names an inventory item, so the server checks that
    -- the item really restores health before spending it. Custom potions are
    -- validated against their stored record; vanilla ones against these Lua
    -- patterns (lowercase refId). Add your own ids here when using mods.
    ["validate potion refIds"] = true,
    ["restore health refId patterns"] = { "^p_restore_health" }
}

-- Note: the client HUD interpolates its countdown over a fixed 10 seconds, so
-- changing "xp decay seconds" makes the bar finish earlier or later than the real
-- decay. The displayed XP itself is clamped to the authoritative value the server
-- sends each tick, so it never overstates what the player still has.

-- The number of days spent in jail as a penalty for dying, when respawning
config.deathPenaltyJailDays = 2

-- Whether players' bounties are reset to 0 after dying
config.bountyResetOnDeath = false

-- Whether players spend time in jail proportional to their bounty after dying
-- Note: If deathPenaltyJailDays is also enabled, that penalty will be added to
--       this one
config.bountyDeathPenalty = false

-- Whether players should be allowed to use the /suicide command
config.allowSuicideCommand = true

-- Whether players should be allowed to use the /fixme command
config.allowFixmeCommand = true

-- How many seconds need to pass between uses of the /fixme command by a player
config.fixmeInterval = 30

-- The colors used for different ranks on the server
config.rankColors = { serverOwner = color.Orange, admin = color.Red, moderator = color.Green }

-- Which numerical IDs should be used by custom menus implemented in the Lua scripts,
-- to prevent other menu inputs from being taken into account for them
-- ===========================================================================
-- Z002: подмена игровых MWScript
-- ---------------------------------------------------------------------------
-- Файл со скриптом в server/data/custom/MWScripts/ заменяет одноимённый скрипт
-- Morrowind у всех клиентов. Имя файла без расширения — это id подменяемого
-- скрипта, поэтому DoorBarricaded.es3 подменяет скрипт DoorBarricaded.
--
-- Каталог читается командой, соответствующей системе. Если io.popen в вашей
-- сборке запрещён, задайте files явным списком — тогда листинг не нужен вовсе.
-- ===========================================================================
config.mwScriptOverrides = {
    enabled = true,
    directory = "custom/MWScripts",
    extensions = { ".es3" },

    -- files = { "DoorBarricaded.es3", "SpyMasterScript.es3" },

    adminRank = 2,
    warnOnBeginMismatch = true
}

-- ===========================================================================
-- Z001: система недвижимости (дома, магазины, таверны, гильдейские залы)
-- ---------------------------------------------------------------------------
-- Ячейки не нужно переделывать под дома: купленный объект получает личную
-- копию (инстанс) исходного интерьера, из которой автоматически убираются NPC
-- и разложенные предметы. Подходящие по названию интерьеры регистрируются в
-- server/data/custom/arenaHouses.json автоматически, при первом обращении.
--
-- Любое поле можно не указывать — тогда действует значение по умолчанию из
-- houseHelper.lua.
-- ===========================================================================
config.houseSystem = {
    enabled = true,

    -- Автоматически заносить в реестр интерьеры, чьё название похоже на дом,
    -- лавку, таверну или гильдию.
    autoRegister = true,

    -- Суффикс личной копии ячейки. Менять после запуска сервера нельзя:
    -- существующие инстансы перестанут опознаваться.
    legacyInstanceSuffix = " - Home of ", -- compatibility only; new instances use " - home"

    -- Базовые цены до применения модификаторов по названию и региону.
    basePrice = { house = 8000, shop = 25000, tavern = 35000, guild = 60000 },

    -- Минимальный уровень покупателя.
    minimumLevel = { house = 10, shop = 20, tavern = 15, guild = 25 },

    -- Сколько объектов каждого типа может держать один аккаунт.
    -- Z002: общий предел — один объект любого типа на аккаунт.
    ownershipLimit = { house = 1, shop = 1, tavern = 1, guild = 1 },

    -- Доля уплаченной цены, возвращаемая при продаже.
    refundRatio = 0.5,

    -- После 30 дней с момента регистрации аккаунта сундуки в его собственном
    -- домашнем инстансе становятся бездонными: реальный ESM-вес/лимит
    -- контейнера больше не ограничивает помещение предметов.
    bottomlessStorageAgeDays = 30,

    -- Ранг персонала для админских операций с недвижимостью.
    adminRank = 2
}

config.customMenuIds = { menuHelper = 9001, confiscate = 9002, recordPrint = 9003,
    -- X035-X038 server quest editor/runtime GUI ids. Keep these separate from menuHelper.
    questEditorMain = 9010, questEditorList = 9011, questEditorDetail = 9012, questEditorInput = 9013,
    questPlayerList = 9014, questPlayerJournal = 9015,
    questEditorTopics = 9016, questEditorTopicList = 9017, questEditorTopicDetail = 9018,
    questEditorOffer = 9019, questEditorChoices = 9020, questEditorChoiceList = 9021, questEditorChoiceDetail = 9022,
    questEditorStages = 9023, questEditorStageList = 9024, questEditorStageDetail = 9025,
    questEditorRequirements = 9026, questEditorRequirementList = 9027, questEditorRewards = 9028,
    questEditorRewardList = 9029, questEditorTransitions = 9030, questEditorTransitionList = 9031,
    questEditorConfirm = 9032, questEditorStageFlags = 9033, questEditorGiver = 9034,
    -- Z001: профиль (9040-9043), недвижимость (9060-9061) и админский
    -- интерфейс (9070-9079). Диапазоны разведены, чтобы OnGUIAction никогда не
    -- перепутал два окна разных подсистем.
    profileMain = 9040, profileReturns = 9041, profileDeletePassword = 9042, profileDeleteConfirm = 9043,
    houseDoorChoice = 9060, houseBuyConfirm = 9061,
    adminMain = 9070, adminHouses = 9071, adminHouseList = 9072, adminHouseDetail = 9073,
    adminHouseInput = 9074, adminHouseKind = 9075, adminPlayers = 9076, adminServer = 9077,
    adminResets = 9078, adminConfirm = 9079,
    -- Z002: подмена скриптов (9080-9083).
    adminScripts = 9080, adminScriptList = 9081, adminScriptDetail = 9082, adminScriptConfirm = 9083 }

-- The menu files that should be loaded for menuHelper, from the scripts/menu subfolder
config.menuHelperFiles = { "help", "defaultCrafting", "advancedExample" }

-- What the difference in ping needs to be in favor of a new arrival to a cell or region
-- compared to that cell or region's current player authority for the new arrival to become
-- the authority there
-- Note: Setting this too low will lead to constant authority changes which cause more lag
config.pingDifferenceRequiredForAuthority = 40

-- X022: keep actor authority stable while the current cell authority is still
-- present. NPC AI state is client-authoritative, so swapping ownership merely
-- because another visitor has lower ping can reset combat/return-home state in
-- populated areas. Authority still transfers immediately when the current
-- authority unloads/leaves the cell. Set false to restore legacy ping takeovers.
config.stableCellAuthority = true

-- X024: remember where an actor lived before it first walked out of its own cell,
-- and hand that anchor to whichever client currently owns the actor.
-- The client-side return-home package lives in AiState and is destroyed on every
-- actor authority hand-off, so an NPC that started a fight under one player and
-- finished it under another had nothing left telling it where to go back to.
-- With this enabled the server keeps the anchor in the actor's objectData and
-- issues a hidden Travel order, which BaseCell:LoadActorAuthority replays to each
-- new authority in turn.
config.rememberActorHomes = true

-- How many seconds an actor may stay outside its home cell before the server
-- starts telling its authority to walk it back. The order is stacked *under* an
-- active AiCombat package, so an NPC that is still fighting keeps fighting and
-- only walks home once the fight is actually over.
config.actorHomeReturnDelay = 8

-- How often the stray-actor sweep runs, in seconds. Raise this on servers with
-- very many loaded cells.
config.actorHomeReturnInterval = 5

-- Distance in game units within which an actor counts as having arrived home.
config.actorHomeArrivalDistance = 320

-- The log level enforced on clients by default, determining how much debug information
-- is displayed in their debug window and logs
-- Note 1: Set this to -1 to allow clients to use whatever log level they have set in
--         their client settings
-- Note 2: If you set this to 0 or 1, clients will be able to read about the movements
--         and actions of other players that they would otherwise not know about,
--         while also incurring a framerate loss on highly populated servers
config.enforcedLogLevel = -1

-- The physics framerate used by default
-- Note: In OpenMW, the physics framerate is 60 by default
config.physicsFramerate = 60

-- Whether players are allowed to interact with containers located in unloaded cells.
config.allowOnContainerForUnloadedCells = false

-- Whether players should collide with other actors
config.enablePlayerCollision = true

-- Whether actors should collide with other actors
config.enableActorCollision = true

-- Whether placed objects should collide with actors
config.enablePlacedObjectCollision = false

-- Enforce collision for certain placed object refIds even when enablePlacedObjectCollision
-- is false
config.enforcedCollisionRefIds = { "misc_uni_pillow_01", "misc_uni_pillow_02" }

-- Whether placed object collision (when turned on) resembles actor collision, in that it
-- prevents players from standing on top of the placed objects without slipping
config.useActorCollisionForPlacedObjects = false

-- Prevent certain object refIds from being activated as a result of player-sent packets
config.disallowedActivateRefIds = {}

-- Prevent certain object refIds from being deleted as a result of player-sent packets
config.disallowedDeleteRefIds = { "m'aiq" }

-- Prevent certain object refIds from being placed or spawned as a result of player-sent packets
config.disallowedCreateRefIds = {}

-- Prevent certain object refIds from being locked or unlocked as a result of player-sent packets
config.disallowedLockRefIds = {}

-- Prevent certain object refIds from being trapped or untrapped as a result of player-sent packets
config.disallowedTrapRefIds = {}

-- Prevent certain object refIds from being enabled or disabled as a result of player-sent packets
config.disallowedStateRefIds = {}

-- Prevent certain door refIds from being opened or closed as a result of player-sent packets
config.disallowedDoorStateRefIds = {}

-- Prevent object scales from being set this high or higher
config.maximumObjectScale = 20

-- The prefix used for automatically generated record IDs
-- Note 1: Records with automatically generated IDs get erased when there are no more instances of
-- them in player inventories/spellbooks or in cells
-- Note 2: By default, records created through regular gameplay (i.e. player-created spells, potions,
-- enchantments and enchanted items) use automatically generated record IDs, as do records created
-- via the /createrecord command when no ID is specified there
config.generatedRecordIdPrefix = "$custom"

-- The types of record stores used on this server in the order in which they should be loaded for
-- players, with the correct order ensuring that enchantments are loaded before items that might be
-- using those enchantments or ensuring that NPCs are loaded after the items they might have in their
-- inventories
-- Note: Cells are loaded first before anything else so players already inside custom cells are moved
-- to them correctly on other clients
config.recordStoreLoadOrder = {
    { "cell" },
    { "gamesetting", "script", "spell", "potion", "enchantment", "bodypart", "armor", "clothing",
      "book", "weapon", "ingredient", "apparatus", "lockpick", "probe", "repair", "light",
      "miscellaneous", "creature", "npc", "container", "door", "activator", "static", "sound" }
}

-- The types of records that can be enchanted and therefore have links to enchantment records
config.enchantableRecordTypes = { "armor", "book", "clothing", "weapon" }

-- The types of records that can be stored by players and therefore have links to players,
-- listed in the order in which they should be loaded
config.carriableRecordTypes = { "spell", "potion", "armor", "book", "clothing", "weapon", "ingredient",
    "apparatus", "lockpick", "probe", "repair", "light", "miscellaneous" }

-- The types of records that cannot be placed in the world and should not display a message
-- about how to place them
config.unplaceableRecordTypes = { "spell", "cell", "script", "gamesetting" }

-- The settings which are accepted as input for different record types when using /storerecord
config.validRecordSettings = {
    activator = { "baseId", "id", "name", "model", "script" },
    apparatus = { "baseId", "id", "name", "model", "icon", "script", "subtype", "weight", "value",
        "quality" },
    armor = { "baseId", "id", "name", "model", "icon", "script", "enchantmentId", "enchantmentCharge",
        "subtype", "weight", "value", "health", "armorRating" },
    bodypart = { "baseId", "id", "subtype", "part", "model", "race", "vampireState", "flags" },
    book = { "baseId", "id", "name", "model", "icon", "script", "enchantmentId", "enchantmentCharge",
        "text", "weight", "value", "scrollState", "skillId" },
    cell = { "baseId", "id" },
    clothing = { "baseId", "id", "name", "model", "icon", "script", "enchantmentId", "enchantmentCharge",
        "subtype", "weight", "value" },
    container = { "baseId", "id", "name", "model", "script", "weight", "flags" },
    creature = { "baseId", "id", "name", "model", "script", "scale", "bloodType", "subtype", "level",
        "health", "magicka", "fatigue", "soulValue", "damageChop", "damageSlash", "damageThrust",
        "aiFight", "aiFlee", "aiAlarm", "aiServices", "flags" },
    door = { "baseId", "id", "name", "model", "openSound", "closeSound", "script" },
    enchantment = { "baseId", "id", "subtype", "cost", "charge", "flags", "effects" },
    gamesetting = { "baseId", "id", "intVar", "floatVar", "stringVar" },
    ingredient = { "baseId", "id", "name", "model", "icon", "script", "weight", "value" },
    light = { "baseId", "id", "name", "model", "icon", "sound", "script", "weight", "value", "time",
        "radius", "color", "flags" },
    lockpick = { "baseId", "id", "name", "model", "icon", "script", "weight", "value", "quality", "uses" },
    miscellaneous = { "baseId", "id", "name", "model", "icon", "script", "weight", "value", "keyState" },
    npc = { "baseId", "inventoryBaseId", "id", "name", "script", "flags", "gender", "race", "model", "hair",
        "head", "class", "faction", "level", "health", "magicka", "fatigue", "aiFight", "aiFlee", "aiAlarm",
        "aiServices", "autoCalc" },
    potion = { "baseId", "id", "name", "model", "icon", "script", "weight", "value", "autoCalc" },
    probe = { "baseId", "id", "name", "model", "icon", "script", "weight", "value", "quality", "uses" },
    repair = { "baseId", "id", "name", "model", "icon", "script", "weight", "value", "quality", "uses" },
    script = { "baseId", "id", "scriptText" },
    spell = { "baseId", "id", "name", "subtype", "cost", "flags", "effects" },
    static = { "baseId", "id", "model" },
    weapon = { "baseId", "id", "name", "model", "icon", "script", "enchantmentId", "enchantmentCharge",
        "subtype", "weight", "value", "health", "speed", "reach", "damageChop", "damageSlash", "damageThrust",
        "flags" },
    sound = { "baseId", "id", "sound", "volume", "pitch" }
}

-- The settings which need to be provided when creating a new record that isn't based at all
-- on an existing one, i.e. a new record that is missing a baseId
config.requiredRecordSettings = {
    activator = { "name", "model" },
    apparatus = { "name", "model" },
    armor = { "name", "model" },
    bodypart = { "subtype", "part", "model" },
    book = { "name", "model" },
    cell = { "id" },
    clothing = { "name", "model" },
    container = { "name", "model" },
    creature = { "name", "model" },
    door = { "name", "model" },
    enchantment = {},
    gamesetting = { "id" },
    ingredient = { "name", "model" },
    light = { "model" },
    lockpick = { "name", "model" },
    miscellaneous = { "name", "model" },
    npc = { "name", "race", "class" },
    potion = { "name", "model" },
    probe = { "name", "model" },
    repair = { "name", "model" },
    script = { "id" },
    spell = { "name" },
    static = { "model" },
    weapon = { "name", "model" },
    sound = { "sound" }
}

-- The record type settings that are mutually exclusive with each other and remove each other when one of
-- them is set
config.mutuallyExclusiveRecordSettings = {
    gamesetting = { "intVar", "floatVar", "stringVar" }
}

-- The record type settings whose input should be converted to numerical values when using /storerecord
config.numericalRecordSettings = { "subtype", "charge", "cost", "value", "weight", "quality", "uses",
    "time", "radius", "health", "armorRating", "speed", "reach", "scale", "part", "bloodType", "level",
    "magicka", "fatigue", "soulValue", "aiFight", "aiFlee", "aiAlarm", "aiServices", "autoCalc", "gender",
    "flags", "enchantmentCharge", "intVar", "floatVar" }

-- The record type settings whose input should be converted to booleans when using /storerecord
config.booleanRecordSettings = { "scrollState", "keyState", "vampireState" }

-- The record type settings whose input should be converted to tables with a min and a max numerical value
config.minMaxRecordSettings = { "damageChop", "damageSlash", "damageThrust" }

-- The record type settings whose input should be converted to tables with 3 color values
config.rgbRecordSettings = { "color" }

-- The types of object and actor packets stored in cell data
config.cellPacketTypes = { "delete", "place", "spawn", "move", "rotate", "lock", "trap", "scale", "state", "miscellaneous",
    "doorState", "clientScriptLocal", "container", "equipment", "ai", "death", "actorList", "position",
    "statsDynamic", "spellsActive", "cellChangeTo", "cellChangeFrom" }

-- Y013-fix-01: minimum seconds between two quicksaves of the same destination
-- cell triggered by an actor cell change. Interior transitions still persist
-- promptly; exterior hops rely on the normal periodic save.
config.actorCellChangeSaveInterval = 5

-- Y019: minimum seconds between two quicksaves of a cell triggered by incoming
-- ActorEquipment packets. Equipment is always re-sent in full, so a throttled
-- save cannot lose state, and a single long fight otherwise wrote the cell file
-- to disk dozens of times per minute.
config.actorEquipmentSaveInterval = 5

-- Y092-EQ: seconds after an ActorCellChange during which missing NPC apparel
-- slots (0..15) are restored from the pre-transfer equipment snapshot. This
-- protects clothing/armor from transient authority-resync packets without
-- locking weapon, shield or ammunition changes in combat.
config.actorEquipmentTransferGuardSeconds = 8

-- Y019: repair of equipment-sync duplicates in actor containers.
--
-- A DedicatedActor can re-add an equipped item when its equipment slot is rebuilt
-- after a cell/authority hand-off and the existing copy has an older condition or
-- charge. On death that inflated client inventory can become the authoritative
-- corpse. This server-side pass collapses those synchronization duplicates.
--
-- Only entries carrying a real condition value (charge >= 0 or enchantment
-- charge >= 0) are ever considered, so stacked ammunition and ordinary loot are
-- untouched.
config.actorLootRepair = {
    enabled = true,

    -- Only repair refIds the actor actually has in an equipment slot. Leave this
    -- true unless you are chasing duplicates that survive it: with false, any
    -- actor container holding several charged copies of one refId is collapsed,
    -- which would also hit an NPC legitimately carrying two damaged copies of the
    -- same item.
    requireEquipmentMatch = true
}

-- Arena Y020 protected physics-grab policy.
-- Ordinary players may hold Activate/E and reposition only items that were
-- actually dropped by a player. Original cell references/statics are protected
-- from client-gameplay ObjectMove/ObjectRotate. Moderator+ bypasses the rule.
-- Future house/RP-cell decoration permissions should be added as a separate,
-- explicit allow-list layer instead of changing this safe default.
config.objectGrabPermissions = {
    enabled = true,
    moderatorBypass = true,
    allowPlayerDropped = true,
    -- Only owner/co-owner can rearrange native furniture in their property.
    -- Import exact IDs from Morrowind JSON here after reviewing record types.
    homeStaticAllowIds = {}, -- ["exact_static_record_id"] = true
    homeStaticDenyIds = {}, -- ["exact_structural_record_id"] = true
    allowFurniturePrefixes = true,

    -- A held object can publish transforms many times per second. Rollback is
    -- still sent for every denial; only the explanatory popup is rate-limited.
    denyQuietSeconds = 4
}

-- Whether the server should enforce that all clients connect with a specific list of data files
-- defined in data/requiredDataFiles.json
-- Warning: Only set this to false if you trust the people connecting and are sure they know
--          what they're doing. Otherwise, you risk getting corrupt server data from
--          their usage of unshared plugins.
config.enforceDataFiles = true

-- Whether the server should avoid crashing when Lua script errors occur
-- Warning: Only set this to true if you want to have a highly experimental server where
--          important data can potentially stay unloaded or get overwritten
config.ignoreScriptErrors = true

-- The type of database or data format used by the server
-- Valid values: json, sqlite3
-- Note: The latter is only partially implemented as of now
config.databaseType = "json"

-- The location of the database file
-- Note: Not applicable when using json
config.databasePath = config.dataPath .. "/database.db" -- Path where database is stored

-- Disallow players from including the following in their own names or the names of their custom items
-- Note: Unfortunately, these are based on real names that trolls have been using on servers
config.disallowedNameStrings = { "bitch", "blowjob", "blow job", "cocksuck", "cunt", "ejaculat",
    "faggot", "fellatio", "fuck", "gas the ", "Hitler", "jizz", "nigga", "nigger", "smegma", "vagina", "whore" }

-- The order in which table keys should be saved to JSON files
config.playerKeyOrder = { "login", "name", "passwordHash", "passwordSalt", "timestamps", "settings",
    "character", "customClass", "location", "stats", "fame", "shapeshift", "attributes",
    "attributeSkillIncreases", "skills", "skillProgress", "recordLinks", "equipment", "inventory",
    "spellbook", "books", "factionRanks", "factionReputation", "factionExpulsion", "mapExplored",
    "ipAddresses", "customVariables", "admin", "difficulty", "enforcedLogLevel", "physicsFramerate",
    "consoleAllowed", "bedRestAllowed", "wildernessRestAllowed", "waitAllowed", "gender", "race",
    "head", "hair", "class", "birthsign", "cell", "posX", "posY", "posZ", "rotX", "rotZ", "healthBase",
    "healthCurrent", "magickaBase", "magickaCurrent", "fatigueBase", "fatigueCurrent" }

config.cellKeyOrder = { "packets", "entry", "lastVisit", "recordLinks", "objectData", "refId", "count",
    "charge", "enchantmentCharge", "location", "home", "actorList", "ai", "summon", "stats", "cellChangeFrom",
    "cellChangeTo", "container", "death", "delete", "doorState", "equipment", "inventory", "lock",
    "place", "position", "scale", "spawn", "state", "statsDynamic", "trap" }

config.recordstoreKeyOrder = { "general", "permanentRecords", "generatedRecords", "recordLinks",
    "id", "baseId", "name", "subtype", "gender", "race", "hair", "head", "class", "faction", "cost",
    "value", "charge", "weight", "autoCalc", "flags", "icon", "model", "script", "attribute", "skill",
    "rangeType", "area", "duration", "magnitudeMax", "magnitudeMin", "effects", "players", "cells", "global" }

config.worldKeyOrder = { "general", "time", "topics", "kills", "journal", "customVariables", "type",
    "index", "quest", "actorRefId", "year", "month", "day", "hour", "daysPassed", "timeScale" }


-- Z003: смерть видят только игроки в той же ячейке, в этом радиусе (единицы мира).
config.deathNotificationDistance = 2500

-- Housing generation: players / admins / off (manual registration).
config.houseEnabled = true
config.houseAutoGeneration = "players"
-- Independent categories, classified by the interior cell name.
config.houseGenerateHouses = true
config.houseGenerateTaverns = false
config.houseGenerateGuilds = false
config.houseGenerateShops = false

-- Add bounded premiums for known furniture, containers, beds and doors.
-- Exact counts may be supplied in custom/houseInteriorProfiles.json.
config.houseDynamicPrices = true
config.houseInteriorProfiles = {} -- ["Cell name"] = {objects=40, containers=6, beds=2, doors=3}


-- 30 online seconds per full 1000 personal bounty; incompatible with shareBounty.
config.bountyRespawnEnabled = true

-- Additional record IDs which must not be sold through player displays.
config.vitrineBannedItems = {}


-- Y082: exact extra bound-equipment IDs for mods overriding sMagicBound* GMSTs.
config.arenaBoundItemRefIds = {}

return config
