require("utils")
require("enumerations")
tableHelper = require("tableHelper")
class = require("classy")
jsonInterface = require("jsonInterface")

-- Lua's default io library for input/output can't open Unicode filenames on Windows,
-- which is why on Windows it's replaced by TES3MP's io2 (https://github.com/TES3MP/Lua-io2)
if tes3mp.GetOperatingSystemType() == "Windows" then
    jsonInterface.setLibrary(require("io2"))
else
    jsonInterface.setLibrary(io)
end

require("color")
require("config")
localization = require("localization")
local coreLocaleLoaded = localization.LoadDictionary("core", "locales.core")
local coreChatLocaleLoaded = localization.LoadDictionary("coreChat", "locales.coreChat")
-- X054: wrapper strings of the server quest system. Quest content itself is
-- translated inside each quest JSON, not here.
local questLocaleLoaded = localization.LoadDictionary("serverQuests", "locales.serverQuests")
if not questLocaleLoaded then
    tes3mp.LogMessage(enumerations.log.WARN,
        "ArenaMP serverQuests localization dictionary failed to load; quest notices will be suppressed")
end
if not coreLocaleLoaded then
    tes3mp.LogMessage(enumerations.log.ERROR,
        "ArenaMP core localization dictionary failed to load; safe built-in fallbacks are active")
end
if not coreChatLocaleLoaded then
    tes3mp.LogMessage(enumerations.log.ERROR,
        "ArenaMP coreChat localization dictionary failed to load; missing chat texts will be suppressed instead of exposing keys")
end
localization.InstallWrappers()
friendlyFire = require("friendlyFire")
require("time")

customEventHooks = require("customEventHooks")
customCommandHooks = require("customCommandHooks")
-- Shared ArenaMP log identity and JSON bootstrap. Required before any
-- helper that owns a file in server/data so the file exists on first read.
ampCore = require("ampCore")
ampCore.EnsureStartupFiles() -- initialize missing JSON before any script reads it
logicHandler = require("logicHandler")
eventHandler = require("eventHandler")
masterStatus = require("masterStatus") -- H021 master player telemetry
-- X050: persistent server-authoritative group system.
groupHelper = require("groupHelper")
-- Y039: server-authoritative 10-second death XP decay and party recovery.
deathRecovery = require("deathRecovery")
bountyRespawn = require("bountyRespawn")
-- X052: nickname colour picker and the Player Menu copy of the /list roster.
chatColorHelper = require("chatColorHelper")
playerListHelper = require("playerListHelper")
-- Y049: persistent profile statistics, deletion and level-25 return slots.
profileHelper = require("profileHelper")
-- Y049: persistent coloured group map markers.
groupMapHelper = require("groupMapHelper")
-- Z001: дома, магазины, таверны и гильдейские залы.
-- Загружается до coreChat: houseHelper перехватывает собственные управляющие
-- сообщения в OnPlayerSendMessage и должен увидеть их первым.
houseHelper = require("houseHelper")
-- Shared Recall uses saved marks/return slots and revalidates online party membership.
groupRecall = require("groupRecall")
-- H001: server-owned furniture catalog, gold transactions and per-player stock.
arenaFurniture = require("arenaFurniture")
playerVitrines = require("playerVitrines")
houseItemMarket = require("houseItemMarket")
-- HS1: belongings of a sold/cleared home, delivered to the first chest of the next home.
houseStorage = require("houseStorage")
-- Z002: подмена игровых MWScript файлами из server/data/custom/MWScripts.
-- Загружается до adminInterface: у панели администратора есть вкладка,
-- работающая с этим модулем.
mwScriptHelper = require("mwScriptHelper")
-- Z001: интерфейс администратора со вкладками (в том числе управление домами).
adminInterface = require("adminInterface")
-- Y050: embedded 12-hour graceful self-restart; no external launcher loop.
restartHelper = require("restartHelper")
-- Y052: server-authoritative empty-bottle requirement for potion brewing.
alchemyBottleHelper = require("alchemyBottleHelper")
-- Y054: per-player/per-NPC training cap kept in server runtime memory.
trainingLimitHelper = require("trainingLimitHelper")
-- X050a: built-in ArenaMP owner/console helper moved out of scripts/custom.
helperAreana = require("helperAreana")
guiHelper = require("guiHelper")
animHelper = require("animHelper")
speechHelper = require("speechHelper")
menuHelper = require("menuHelper")
require("defaultCommands")
-- Y097: trusted plugin catalog and actor manipulation API for server scripts.
if doesModuleExist("contentApi") then contentApi = require("contentApi") end
craftEconomic = require("craftEconomic")
coreChat = require("coreChat")
require("customScripts")
-- X035: server-authoritative custom quest definitions/editor/runtime.
serverQuestSystem = require("serverQuestSystem")
local configHotReload = require("configHotReload")
local questIndexStore = require("questIndexStore")
-- ArenaMP C19: core cell reset helper now lives in the main scripts folder.
-- Lua require caching keeps this safe when an older customScripts.lua still
-- loads custom.periodicCellResets through the compatibility shim.
require("resetHelrer")

Database = nil
Player = nil
Cell = nil
RecordStore = nil
World = nil

pidsByIpAddress = {}
clientDataFiles = {}
clientVariableScopes = {}
speechCollections = {}

hourCounter = nil
updateTimerId = nil

banList = {}

if (config.databaseType ~= nil and config.databaseType ~= "json") and doesModuleExist("luasql." .. config.databaseType) then

    Database = require("database")
    Database:LoadDriver(config.databaseType)

    tes3mp.LogMessage(enumerations.log.INFO, "Using " .. Database.driver._VERSION .. " with " .. config.databaseType ..
        " driver")

    Database:Connect(config.databasePath)

    -- Make sure we enable foreign keys
    Database:Execute("PRAGMA foreign_keys = ON;")

    Database:CreatePlayerTables()
    Database:CreateWorldTables()

    Player = require("player.sql")
    Cell = require("cell.sql")
    RecordStore = require("recordstore.sql")
    World = require("world.sql")
else
    Player = require("player.json")
    Cell = require("cell.json")
    RecordStore = require("recordstore.json")
    World = require("world.json")
end

function LoadBanList()
    tes3mp.LogMessage(enumerations.log.INFO, "Reading banlist.json")
    banList = jsonInterface.load("banlist.json")

    if banList.playerNames == nil then
        banList.playerNames = {}
    elseif banList.ipAddresses == nil then
        banList.ipAddresses = {}
    end

    if #banList.ipAddresses > 0 then
        local message = "- Banning manually-added IP addresses:\n"

        for index, ipAddress in pairs(banList.ipAddresses) do
            message = message .. ipAddress

            if index < #banList.ipAddresses then
                message = message .. ", "
            end

            tes3mp.BanAddress(ipAddress)
        end

        tes3mp.LogAppend(enumerations.log.WARN, message)
    end

    if #banList.playerNames > 0 then
        local message = "- Banning all IP addresses stored for players:\n"

        for index, targetName in pairs(banList.playerNames) do
            message = message .. targetName

            if index < #banList.playerNames then
                message = message .. ", "
            end

            local targetPlayer = logicHandler.GetPlayerByName(targetName)

            if targetPlayer ~= nil then

                for index, ipAddress in pairs(targetPlayer.data.ipAddresses) do
                    tes3mp.BanAddress(ipAddress)
                end
            end
        end

        tes3mp.LogAppend(enumerations.log.WARN, message)
    end
end

function SaveBanList()
    jsonInterface.save("banlist.json", banList)
end

-- X031: one place for settings that can safely be re-applied at runtime after a
-- transactional RAW config.lua reload.
local function ApplyServerRuleConfig()
    tes3mp.SetGameMode(config.gameMode)
    tes3mp.SetDataFileEnforcementState(config.enforceDataFiles)
    tes3mp.SetScriptErrorIgnoringState(config.ignoreScriptErrors)

    tes3mp.SetPlayerCollisionState(config.enablePlayerCollision)
    tes3mp.SetActorCollisionState(config.enableActorCollision)
    tes3mp.SetPlacedObjectCollisionState(config.enablePlacedObjectCollision)
    tes3mp.UseActorCollisionForPlacedObjects(config.useActorCollisionForPlacedObjects)

    local consoleRuleString = config.allowConsole and "allowed" or "not allowed"
    local bedRestRuleString = config.allowBedRest and "allowed" or "not allowed"
    local wildRestRuleString = config.allowWildernessRest and "allowed" or "not allowed"
    local waitRuleString = config.allowWait and "allowed" or "not allowed"

    tes3mp.SetRuleString("enforceDataFiles", tostring(config.enforceDataFiles))
    tes3mp.SetRuleString("ignoreScriptErrors", tostring(config.ignoreScriptErrors))
    tes3mp.SetRuleValue("difficulty", config.difficulty)
    tes3mp.SetRuleValue("deathPenaltyJailDays", config.deathPenaltyJailDays)
    tes3mp.SetRuleString("console", consoleRuleString)
    tes3mp.SetRuleString("bedResting", bedRestRuleString)
    tes3mp.SetRuleString("wildernessResting", wildRestRuleString)
    tes3mp.SetRuleString("waiting", waitRuleString)
    tes3mp.SetRuleValue("enforcedLogLevel", config.enforcedLogLevel)
    tes3mp.SetRuleValue("physicsFramerate", config.physicsFramerate)
    tes3mp.SetRuleString("shareJournal", tostring(config.shareJournal))
    tes3mp.SetRuleString("shareFactionRanks", tostring(config.shareFactionRanks))
    tes3mp.SetRuleString("shareFactionExpulsion", tostring(config.shareFactionExpulsion))
    tes3mp.SetRuleString("shareFactionReputation", tostring(config.shareFactionReputation))
    tes3mp.SetRuleString("shareTopics", tostring(config.shareTopics))
    tes3mp.SetRuleString("shareBounty", tostring(config.shareBounty))
    tes3mp.SetRuleString("shareReputation", tostring(config.shareReputation))
    tes3mp.SetRuleString("shareMapExploration", tostring(config.shareMapExploration))
    tes3mp.SetRuleString("enablePlacedObjectCollision", tostring(config.enablePlacedObjectCollision))

    local respawnCell
    if config.respawnAtImperialShrine == true then
        respawnCell = "nearest Imperial shrine"
        if config.respawnAtTribunalTemple == true then
            respawnCell = respawnCell .. " or Tribunal temple"
        end
    elseif config.respawnAtTribunalTemple == true then
        respawnCell = "nearest Tribunal temple"
    else
        respawnCell = config.defaultRespawn.cellDescription
    end
    tes3mp.SetRuleString("respawnCell", respawnCell)
end

local function ApplyConfigToOnlinePlayer(pid)
    if Players[pid] == nil or not Players[pid]:IsLoggedIn() then
        return
    end

    tes3mp.SetDifficulty(pid, config.difficulty)
    tes3mp.SetConsoleAllowed(pid, config.allowConsole)
    tes3mp.SetBedRestAllowed(pid, config.allowBedRest)
    tes3mp.SetWildernessRestAllowed(pid, config.allowWildernessRest)
    tes3mp.SetWaitAllowed(pid, config.allowWait)
    tes3mp.SetPhysicsFramerate(pid, config.physicsFramerate)
    tes3mp.SetEnforcedLogLevel(pid, config.enforcedLogLevel)

    tes3mp.ClearGameSettingValues(pid)
    for _, settingPairTable in pairs(config.gameSettings) do
        tes3mp.SetGameSettingValue(pid, settingPairTable.name, tostring(settingPairTable.value))
    end

    tes3mp.ClearVRSettingValues(pid)
    for _, settingPairTable in pairs(config.vrSettings) do
        tes3mp.SetVRSettingValue(pid, settingPairTable.name, tostring(settingPairTable.value))
    end
    tes3mp.SendSettings(pid)

    -- These helpers read the new global config table on every call. Disabled
    -- client scripts are deliberately not hot-reloaded because removing a dummy
    -- replacement cannot restore the original script without reconnect/restart.
    logicHandler.SendClientScriptSettings(pid, false)
    logicHandler.SendConfigCollisionOverrides(pid, false)
end

local function ApplyLiveConfig()
    ApplyServerRuleConfig()
    for pid, _ in pairs(Players) do
        ApplyConfigToOnlinePlayer(pid)
    end
    tes3mp.LogMessage(enumerations.log.INFO,
        "[ArenaMP Core] Live settings applied to server and connected players")
end

local rawConfigReloadCountdown = 0
local arenaMasterStatusCountdown = 0

do
    local previousHourFloor = nil
    -- X024: seconds left before the next stray-actor sweep.
    local homeReturnCountdown = 0

    function UpdateTime()
        arenaMasterStatusCountdown = arenaMasterStatusCountdown - 1
        if arenaMasterStatusCountdown <= 0 then
            arenaMasterStatusCountdown = 10
            masterStatus.write()
        end

        -- X031: poll the RAW config file on the existing one-second server timer.
        -- Failed edits are rolled back by configHotReload and do not disturb the
        -- running server.
        rawConfigReloadCountdown = rawConfigReloadCountdown - 1
        if rawConfigReloadCountdown <= 0 then
            rawConfigReloadCountdown = math.max(1, math.min(60, tonumber(config.rawConfigReloadInterval) or 2))
            if configHotReload.Check() then
                ApplyLiveConfig()
            end
        end

        -- X024: periodically remind cell authorities to walk actors that are
        -- stranded outside their own cell back home. Runs on the existing one
        -- second timer instead of adding another one.
        if config.rememberActorHomes then

            homeReturnCountdown = homeReturnCountdown - 1

            if homeReturnCountdown <= 0 then
                homeReturnCountdown = math.max(1, config.actorHomeReturnInterval)

                if tableHelper.getCount(Players) > 0 then
                    logicHandler.ReturnStrayActorsHome()
                end
            end
        end

        if config.passTimeWhenEmpty or tableHelper.getCount(Players) > 0 then

            hourCounter = hourCounter + (0.0083 * WorldInstance.frametimeMultiplier)

            local hourFloor = math.floor(hourCounter)

            if previousHourFloor == nil then
                previousHourFloor = hourFloor

            elseif hourFloor > previousHourFloor then

                if hourFloor >= 24 then

                    hourCounter = hourCounter - hourFloor
                    hourFloor = 0

                    tes3mp.LogMessage(enumerations.log.INFO, "The world time day has been incremented")
                    WorldInstance:IncrementDay()
                end

                tes3mp.LogMessage(enumerations.log.INFO, "The world time hour is now " .. hourFloor)
                WorldInstance.data.time.hour = hourCounter

                WorldInstance:UpdateFrametimeMultiplier()

                if tableHelper.getCount(Players) > 0 then
                    WorldInstance:LoadTime(tableHelper.getAnyValue(Players).pid, true)
                end

                previousHourFloor = hourFloor
            end
        end

        tes3mp.RestartTimer(updateTimerId, time.seconds(1))
    end
end

function OnServerInit()

    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnServerInit\"")
    tes3mp.LogMessage(enumerations.log.INFO,
        "[CellLoad] Y105 active: batched consumed-item replay, silent placement snapshots, ActorAI logging disabled")

    local expectedVersionPrefix = "0.8.1"
    local serverVersion = tes3mp.GetServerVersion()

    if string.sub(serverVersion, 1, string.len(expectedVersionPrefix)) ~= expectedVersionPrefix then
        tes3mp.LogAppend(enumerations.log.ERROR, "- Version mismatch between server and Core scripts!")
        tes3mp.LogAppend(enumerations.log.ERROR, "- The Core scripts require a server version that starts with " ..
            expectedVersionPrefix)
        tes3mp.StopServer(1)
    end
    
    local eventStatus = customEventHooks.triggerValidators("OnServerInit", {})

    if eventStatus.validDefaultHandler then
        friendlyFire.Initialize()
        logicHandler.InitializeWorld()
        configHotReload.Initialize()
        questIndexStore.OnServerStart()

        for priorityLevel, recordStoreTypes in ipairs(config.recordStoreLoadOrder) do
            for _, storeType in ipairs(recordStoreTypes) do
                logicHandler.LoadRecordStore(storeType)
            end
        end

        hourCounter = WorldInstance.data.time.hour
        WorldInstance:UpdateFrametimeMultiplier()

        updateTimerId = tes3mp.CreateTimer("UpdateTime", time.seconds(1))
        tes3mp.StartTimer(updateTimerId)

        logicHandler.PushPlayerList(Players)

        LoadBanList()

        tes3mp.SetDataFileEnforcementState(config.enforceDataFiles)
        tes3mp.SetScriptErrorIgnoringState(config.ignoreScriptErrors)
    end

    customEventHooks.triggerHandlers("OnServerInit", eventStatus, {})
end

function OnServerPostInit()
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnServerPostInit\"")
    local eventStatus = customEventHooks.triggerValidators("OnServerPostInit", {})
    if eventStatus.validDefaultHandler then

        clientVariableScopes = require("clientVariableScopes")
        speechCollections = require("speechCollections")

        eventHandler.InitializeDefaultValidators()
        eventHandler.InitializeDefaultHandlers()

        ApplyServerRuleConfig()
    end
    customEventHooks.triggerHandlers("OnServerPostInit", eventStatus, {})

    -- Start the native-backed restart schedule only after all CoreScripts and
    -- handlers are ready, so the final save can safely touch every subsystem.
    restartHelper.Initialize()
end

function OnServerExit(errorState)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnServerExit\"")
    if errorState then
        tes3mp.LogMessage(enumerations.log.ERROR, "Error state: " .. tostring(errorState))
    else
        tes3mp.LogMessage(enumerations.log.INFO, "Error state: false")
    end
    customEventHooks.triggerHandlers("OnServerExit", customEventHooks.makeEventStatus(true, true) , {errorState})
end

function OnServerScriptCrash(errorMessage)
    tes3mp.LogMessage(enumerations.log.ERROR, "Server crash from script error!")
    customEventHooks.triggerHandlers("OnServerExit", customEventHooks.makeEventStatus(true, true), {errorMessage})
end

local function ParseDataFileEntries(entries, label)
    local dataFileList = {}
    if type(entries) ~= "table" then
        return dataFileList
    end

    tableHelper.fixNumericalKeys(entries, true)
    for listIndex, pluginEntry in ipairs(entries) do
        for entryName, checksumStringArray in pairs(pluginEntry) do
            local entry = { name = entryName, checksums = {} }
            local debugMessage = ("- %s %d: \"%s\": ["):format(label, listIndex, entryName)

            if type(checksumStringArray) == "table" then
                for _, checksumString in ipairs(checksumStringArray) do
                    local checksum = tonumber(checksumString, 16)
                    if checksum ~= nil then
                        debugMessage = debugMessage .. ("%X, "):format(checksum)
                        table.insert(entry.checksums, checksum)
                    end
                end
            end

            if #entry.checksums > 0 then
                debugMessage = debugMessage .. "\b\b]"
            else
                debugMessage = debugMessage .. "any]"
            end
            tes3mp.LogAppend(enumerations.log.INFO, debugMessage)
            table.insert(dataFileList, entry)
        end
    end
    return dataFileList
end

-- FIX12 supports both the original top-level array and the ArenaMP v2 object:
-- { "content": [...], "groundcover": [...] }. Groundcover is always optional.
function LoadDataFileManifest(filename)
    tes3mp.LogMessage(enumerations.log.INFO, "Reading " .. filename)
    local jsonManifest = jsonInterface.load(filename)

    if jsonManifest == nil then
        tes3mp.LogMessage(enumerations.log.ERROR, "Data file list " .. filename .. " cannot be read!")
        tes3mp.StopServer(2)
        return {}, {}
    end

    local contentEntries = jsonManifest
    local groundcoverEntries = {}
    if type(jsonManifest.content) == "table" or type(jsonManifest.groundcover) == "table" then
        contentEntries = jsonManifest.content or {}
        groundcoverEntries = jsonManifest.groundcover or {}
    end

    return ParseDataFileEntries(contentEntries, "content"),
        ParseDataFileEntries(groundcoverEntries, "groundcover(optional)")
end

-- Compatibility for custom scripts that still call the old helper directly.
function LoadDataFileList(filename)
    local content = LoadDataFileManifest(filename)
    return content
end

function OnRequestDataFileList()

    -- This value is embedded in the pre-init response, before the client creates
    -- its world, so worldimp.cpp can replace the hardcoded 0, -7 location.
    local startLocation = config.startLocation
    if type(startLocation) ~= "string" or startLocation == "" then
        startLocation = "default"
    end
    tes3mp.SetStartLocation(startLocation)

    local dataFileList, groundcoverList = LoadDataFileManifest("requiredDataFiles.json")

    for _, entry in ipairs(dataFileList) do
        local name = entry.name
        table.insert(clientDataFiles, name)

        if tableHelper.isEmpty(entry.checksums) then
            tes3mp.AddDataFileRequirement(name, "")
        else
            for _, checksum in ipairs(entry.checksums) do
                tes3mp.AddDataFileRequirement(name, checksum)
            end
        end
    end

    -- Groundcover is an instruction, not an admission requirement. A FIX12
    -- client enables it only if the file exists locally (and its optional CRC
    -- matches); otherwise it is skipped without rejecting or crashing.
    for _, entry in ipairs(groundcoverList) do
        if tableHelper.isEmpty(entry.checksums) then
            tes3mp.AddGroundcoverRequirement(entry.name, "")
        else
            for _, checksum in ipairs(entry.checksums) do
                tes3mp.AddGroundcoverRequirement(entry.name, checksum)
            end
        end
    end
end

-- Older server builds will call an "OnRequestPluginList" event instead of
-- "OnRequestDataFileList", so keep this around for backwards compatibility
function OnRequestPluginList()
    OnRequestDataFileList()
end

function OnPlayerConnect(pid)

    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerConnect\" for pid " .. pid)

    local playerName = tes3mp.GetName(pid)

    if string.len(playerName) > 35 then
        playerName = string.sub(playerName, 0, 35)
    end

    if not logicHandler.IsNameAllowed(playerName) then
        local message = playerName .. " (" .. pid .. ") " .. "joined and tried to use a disallowed name.\n"
        tes3mp.SendMessage(pid, message, true)
        tes3mp.Kick(pid)
    elseif logicHandler.IsPlayerNameLoggedIn(playerName) then
        local message = playerName .. " (" .. pid .. ") " .. "joined and tried to use an existing player's name.\n"
        tes3mp.SendMessage(pid, message, true)
        tes3mp.Kick(pid)
    else
        tes3mp.LogAppend(enumerations.log.INFO, "- New player is named " .. playerName)
        eventHandler.OnPlayerConnect(pid, playerName)
        masterStatus.write()
    end
end

function OnPlayerDisconnect(pid)

    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerDisconnect\" for " .. logicHandler.GetChatName(pid))

    eventHandler.OnPlayerDisconnect(pid)
    masterStatus.write()
end

function OnPlayerResurrect(pid)
    customEventHooks.triggerHandlers("OnPlayerResurrect", customEventHooks.makeEventStatus(true, true), {pid})
end

function OnPlayerSendMessage(pid, message)
    if craftEconomic.HandleMessage(pid, message) then return end
    eventHandler.OnPlayerSendMessage(pid, message)
end

function OnPlayerDeath(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerDeath\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerDeath(pid)
end

function OnPlayerAttribute(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerAttribute\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerAttribute(pid)
end

function OnPlayerSkill(pid)
    eventHandler.OnPlayerSkill(pid)
end

function OnPlayerLevel(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerLevel\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerLevel(pid)
end

function OnPlayerShapeshift(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerShapeshift\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerShapeshift(pid)
end

function OnPlayerPosition(pid)
    -- X051: deliberately no per-packet INFO log; movement packets are frequent.
    eventHandler.OnPlayerPosition(pid)
end

function OnPlayerCellChange(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerCellChange\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerCellChange(pid)
end

function OnPlayerEquipment(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerEquipment\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerEquipment(pid)
end

function OnPlayerInventory(pid)
    if craftEconomic.GuardInventory(pid) then return end
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerInventory\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerInventory(pid)
end

function OnPlayerSpellbook(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerSpellbook\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerSpellbook(pid)
end

function OnPlayerSpellsActive(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerSpellsActive\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerSpellsActive(pid)
end

function OnPlayerCooldowns(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerCooldowns\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerCooldowns(pid)
end

function OnPlayerQuickKeys(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerQuickKeys\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerQuickKeys(pid)
end

function OnPlayerJournal(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerJournal\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerJournal(pid)
end

function OnPlayerFaction(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerFaction\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerFaction(pid)
end

function OnPlayerTopic(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerTopic\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerTopic(pid)
end

function OnPlayerQuestIndex(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerQuestIndex\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerQuestIndex(pid)
end

function OnPlayerBounty(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerBounty\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerBounty(pid)
end

function OnPlayerReputation(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerReputation\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerReputation(pid)
end

function OnPlayerBook(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerBook\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerBook(pid)
end

function OnPlayerItemUse(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerItemUse\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerItemUse(pid)
end

function OnPlayerMiscellaneous(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerMiscellaneous\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerMiscellaneous(pid)
end

function OnPlayerEndCharGen(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnPlayerEndCharGen\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnPlayerEndCharGen(pid)
end

function OnCellLoad(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnCellLoad\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnCellLoad(pid, cellDescription)
end

function OnCellUnload(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnCellUnload\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnCellUnload(pid, cellDescription)
end

function OnCellDeletion(cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnCellDeletion\" for cell " .. cellDescription)
    eventHandler.OnCellDeletion(cellDescription)
end

function OnActorList(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnActorList\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnActorList(pid, cellDescription)
end

function OnActorEquipment(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnActorEquipment\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnActorEquipment(pid, cellDescription)
end

function OnActorAI(pid, cellDescription)
    -- Y100: high-frequency AI heartbeats are processed without a log per packet.
    eventHandler.OnActorAI(pid, cellDescription)
end

function OnActorDeath(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnActorDeath\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnActorDeath(pid, cellDescription)
end

function OnActorSpellsActive(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnActorSpellsActive\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnActorSpellsActive(pid, cellDescription)
end

function OnActorCellChange(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnActorCellChange\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnActorCellChange(pid, cellDescription)
end

function OnObjectActivate(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectActivate\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectActivate(pid, cellDescription)
end

function OnObjectHit(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectHit\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectHit(pid, cellDescription)
end

function OnObjectPlace(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectPlace\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectPlace(pid, cellDescription)
end

function OnObjectSpawn(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectSpawn\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectSpawn(pid, cellDescription)
end

function OnObjectDelete(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectDelete\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectDelete(pid, cellDescription)
end

function OnObjectMove(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectMove\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectMove(pid, cellDescription)
end

function OnObjectRotate(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectRotate\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectRotate(pid, cellDescription)
end

function OnObjectLock(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectLock\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectLock(pid, cellDescription)
end

function OnObjectDialogueChoice(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectDialogueChoice\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectDialogueChoice(pid, cellDescription)
end

function OnObjectMiscellaneous(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectMiscellaneous\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectMiscellaneous(pid, cellDescription)
end

function OnObjectRestock(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectRestock\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectRestock(pid, cellDescription)
end

function OnObjectTrap(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectTrap\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectTrap(pid, cellDescription)
end

function OnObjectScale(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectScale\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectScale(pid, cellDescription)
end

function OnObjectSound(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectSound\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectSound(pid, cellDescription)
end

function OnObjectState(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnObjectState\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnObjectState(pid, cellDescription)
end

function OnDoorState(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnDoorState\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnDoorState(pid, cellDescription)
end

function OnConsoleCommand(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnConsoleCommand\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnConsoleCommand(pid, cellDescription)
end

function OnContainer(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnContainer\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnContainer(pid, cellDescription)
end

function OnVideoPlay(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnVideoPlay\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnVideoPlay(pid)
end

function OnRecordDynamic(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnRecordDynamic\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnRecordDynamic(pid)
end

function OnWorldKillCount(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnWorldKillCount\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnWorldKillCount(pid)
end

function OnWorldMap(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnWorldMap\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnWorldMap(pid)
end

function OnWorldWeather(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnWorldWeather\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnWorldWeather(pid)
end

function OnClientScriptLocal(pid, cellDescription)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnClientScriptLocal\" for " .. logicHandler.GetChatName(pid) ..
        " and cell " .. cellDescription)
    eventHandler.OnClientScriptLocal(pid, cellDescription)
end

function OnClientScriptGlobal(pid)
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnClientScriptGlobal\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnClientScriptGlobal(pid)
end

function OnGUIAction(pid, idGui, data)
    if craftEconomic.OnGUIAction(pid, idGui, data) then return end
    tes3mp.LogMessage(enumerations.log.INFO, "Called \"OnGUIAction\" for " .. logicHandler.GetChatName(pid))
    eventHandler.OnGUIAction(pid, idGui, data)
end

function OnMpNumIncrement(currentMpNum)
    eventHandler.OnMpNumIncrement(currentMpNum)
end

-- ArenaMP U034: ArenaLink/global launcher chat arrives here only after the
-- native LinkServer queue has been drained by Networking::mainLoop().  This is
-- therefore on the normal server/Lua thread and can safely reuse coreChat.
function OnLauncherGlobalChat(author, userId, level, colorRgb, text)
    if coreChat ~= nil and coreChat.SendLauncherGlobal ~= nil then
        coreChat.SendLauncherGlobal(author, userId, level, colorRgb, text)
    end
end

-- Timer-based events
function OnLoginTimeExpiration(pid, accountName)
    eventHandler.OnLoginTimeExpiration(pid, accountName)
end

function OnDeathTimeExpiration(pid, accountName)
    eventHandler.OnDeathTimeExpiration(pid, accountName)
end

function OnDeathXpDecayTick(pid, accountName)
    if deathRecovery ~= nil then deathRecovery.Tick(pid, accountName) end
end

function OnObjectLoopTimeExpiration(loopIndex)
    eventHandler.OnObjectLoopTimeExpiration(loopIndex)
end
