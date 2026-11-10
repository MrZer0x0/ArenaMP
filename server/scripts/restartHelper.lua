-- ArenaMP Y050 embedded restart controller.
--
-- The old Nirn/ShutdownServer helper stopped the server and depended on an
-- external launcher to start it again. Y050 keeps the schedule in CoreScripts
-- and uses a reserved exit code that the dedicated server handles by spawning
-- its own replacement process. The last 30 seconds are sent to clients through
-- a hidden ChatMessage control envelope, so no packet id is added or reordered.
local restartHelper = {}

local PREFIX = "@@AMP_RESTART@@"
local DEFAULT_EXIT_CODE = 42

local state = {
    initialized = false,
    active = false,
    beginTimerId = nil,
    tickTimerId = nil,
    stopTimerId = nil,
    secondsRemaining = 0
}

local function settings()
    local cfg = type(config.embeddedRestart) == "table" and config.embeddedRestart or {}
    return {
        enabled = cfg.enabled ~= false,
        intervalHours = math.max(0.25, tonumber(cfg.intervalHours) or 12),
        countdownSeconds = math.max(5, math.floor(tonumber(cfg.countdownSeconds) or 30)),
        exitCode = math.floor(tonumber(cfg.exitCode) or DEFAULT_EXIT_CODE),
        commandName = tostring(cfg.commandName or "shutdown"),
        requiredRank = math.floor(tonumber(cfg.requiredRank) or 2)
    }
end

local function loggedIn(pid, player)
    return player ~= nil and type(player.IsLoggedIn) == "function" and player:IsLoggedIn()
end

local function sendControlTo(pid, payload)
    tes3mp.SendMessage(pid, payload, false)
end

local function sendControl(seconds)
    local payload = PREFIX .. tostring(math.max(0, math.floor(seconds or 0))) .. "\n"
    for pid, player in pairs(Players) do
        if loggedIn(pid, player) then
            sendControlTo(pid, payload)
        end
    end
end

-- Y052: without this, rescheduling or aborting a countdown left the client HUD
-- frozen on screen until the player reconnected, because the overlay was only
-- destroyed in the GUIController destructor.
local function sendCancel()
    local payload = PREFIX .. "CANCEL\n"
    for pid, player in pairs(Players) do
        if loggedIn(pid, player) then
            sendControlTo(pid, payload)
        end
    end
end

local function saveEverything()
    -- Snapshot player state first while every object needed by Save* still exists.
    for pid, player in pairs(Players) do
        if loggedIn(pid, player) then
            pcall(function() player:SaveStatsDynamic() end)
            pcall(function() player:SaveCell() end)
            pcall(function() player:SaveToDrive() end)
        end
    end

    for _, cell in pairs(LoadedCells) do
        if cell ~= nil and type(cell.SaveToDrive) == "function" then
            pcall(function() cell:SaveToDrive() end)
        end
    end

    for _, recordStore in pairs(RecordStores) do
        if recordStore ~= nil and type(recordStore.SaveToDrive) == "function" then
            pcall(function() recordStore:SaveToDrive() end)
        elseif recordStore ~= nil and type(recordStore.Save) == "function" then
            pcall(function() recordStore:Save() end)
        end
    end

    if WorldInstance ~= nil and type(WorldInstance.SaveToDrive) == "function" then
        pcall(function() WorldInstance:SaveToDrive() end)
    elseif World ~= nil and type(World.Save) == "function" then
        pcall(function() World:Save() end)
    end
end

local function kickPlayers()
    -- Use a stable pid list because Kick causes disconnect processing that mutates Players.
    local pids = {}
    for pid, player in pairs(Players) do
        if loggedIn(pid, player) then table.insert(pids, pid) end
    end
    for _, pid in ipairs(pids) do
        pcall(function() tes3mp.Kick(pid) end)
    end
end

function ArenaMPEmbeddedRestartStop()
    local cfg = settings()
    tes3mp.LogMessage(enumerations.log.WARN,
        "[ArenaMP Core] embedded 12h restart: stopping server for self-relaunch")
    tes3mp.StopServer(cfg.exitCode)
end

function ArenaMPEmbeddedRestartTick()
    if not state.active then return end

    -- Y052: the old order sent 30..0 inclusive, i.e. 31 ticks for a 30s
    -- countdown. Send the current value first, then stop once it reaches zero.
    if state.secondsRemaining <= 0 then
        state.active = false
        sendControl(0)
        saveEverything()
        kickPlayers()

        -- Give disconnect notifications and final socket writes a short grace window.
        state.stopTimerId = tes3mp.CreateTimer("ArenaMPEmbeddedRestartStop", 750)
        tes3mp.StartTimer(state.stopTimerId)
        return
    end

    sendControl(state.secondsRemaining)
    state.secondsRemaining = state.secondsRemaining - 1
    state.tickTimerId = tes3mp.CreateTimer("ArenaMPEmbeddedRestartTick", time.seconds(1))
    tes3mp.StartTimer(state.tickTimerId)
end

function ArenaMPEmbeddedRestartBegin()
    if state.active then return end
    local cfg = settings()
    state.active = true
    state.secondsRemaining = cfg.countdownSeconds
    tes3mp.LogMessage(enumerations.log.WARN,
        "[ArenaMP Core] scheduled restart countdown started (" .. cfg.countdownSeconds .. "s)")
    ArenaMPEmbeddedRestartTick()
end

local function stopTimer(id)
    if id ~= nil then pcall(function() tes3mp.StopTimer(id) end) end
end

local function abortActiveCountdown()
    stopTimer(state.beginTimerId)
    stopTimer(state.tickTimerId)
    stopTimer(state.stopTimerId)
    if state.active then
        state.active = false
        sendCancel()
    end
    state.secondsRemaining = 0
end

-- Public abort: drops the schedule entirely and clears every client HUD.
function restartHelper.Cancel()
    abortActiveCountdown()
    tes3mp.LogMessage(enumerations.log.WARN, "[ArenaMP Core] scheduled restart cancelled")
end

function restartHelper.Schedule(delaySeconds)
    local cfg = settings()
    if not cfg.enabled then return end

    abortActiveCountdown()

    local secondsUntilCountdown
    if delaySeconds ~= nil then
        secondsUntilCountdown = math.max(0, tonumber(delaySeconds) or 0)
    else
        secondsUntilCountdown = math.max(0, cfg.intervalHours * 3600 - cfg.countdownSeconds)
    end

    state.beginTimerId = tes3mp.CreateTimer(
        "ArenaMPEmbeddedRestartBegin", math.floor(secondsUntilCountdown * 1000))
    tes3mp.StartTimer(state.beginTimerId)

    tes3mp.LogMessage(enumerations.log.INFO,
        "[ArenaMP Core] embedded restart scheduled in " ..
        string.format("%.2f", (secondsUntilCountdown + cfg.countdownSeconds) / 3600) .. "h")
end

function restartHelper.Initialize()
    if state.initialized then return end
    state.initialized = true
    local cfg = settings()
    if not cfg.enabled then
        tes3mp.LogMessage(enumerations.log.INFO, "[ArenaMP Core] embedded restart disabled")
        return
    end

    restartHelper.Schedule()

    -- Keep the historic /shutdown command name, but route it to the embedded
    -- restart controller. /shutdown with no argument starts the 30s countdown;
    -- /shutdown N schedules it N minutes from now and still only shows the HUD
    -- during the final countdownSeconds.
    customCommandHooks.registerCommand(cfg.commandName, function(pid, cmd)
        if Players[pid] == nil then return end
        local rank = tonumber(Players[pid].data.settings.staffRank) or 0
        if rank < cfg.requiredRank then
            tes3mp.SendMessage(pid, "[#FF0000Система#FFFFFF]: Недостаточно прав.\n", false)
            return
        end

        local argument = tostring(cmd[2] or ""):lower()
        if argument == "cancel" or argument == "abort" or argument == "stop" then
            restartHelper.Cancel()
            restartHelper.Schedule()
            tes3mp.SendMessage(pid,
                "[#FF0000Система#FFFFFF]: Перезапуск отменён, расписание сброшено.\n", false)
            return
        end

        local minutes = tonumber(cmd[2])
        if minutes == nil then
            ArenaMPEmbeddedRestartBegin()
            return
        end

        minutes = math.max(0, minutes)
        local delayToCountdown = math.max(0, minutes * 60 - cfg.countdownSeconds)
        restartHelper.Schedule(delayToCountdown)
        tes3mp.SendMessage(pid,
            "[#FF0000Система#FFFFFF]: Перезапуск запланирован через " .. tostring(minutes) .. " мин.\n", false)
    end)
end

function restartHelper.OnServerExit()
    -- On ordinary shutdowns as well as embedded restarts, persist state once.
    -- This also supersedes the user's old standalone Nirn_ResetAPI SaveEverything.
    saveEverything()
end

customEventHooks.registerHandler("OnServerExit", restartHelper.OnServerExit)

-- A player who connects during the final countdown must see the same HUD as
-- everyone else instead of being kicked with no warning at all.
customEventHooks.registerHandler("OnPlayerAuthentified", function(eventStatus, pid)
    if not eventStatus.validDefaultHandler then return end
    if not state.active or state.secondsRemaining <= 0 then return end
    sendControlTo(pid, PREFIX .. tostring(state.secondsRemaining) .. "\n")
end)

return restartHelper
