-- ArenaMP helperAreana: server ownership and console gating.
--
-- The first account that finishes registration on a fresh server becomes the
-- server owner: staffRank 3 and an enabled in-game console. Every other account
-- gets the console switched off unless a staff member later grants it
-- explicitly with /console <name> on.
--
-- The owner name is stored in the world record (customVariables), so it survives
-- restarts and is not tied to any player file. If the world record is wiped the
-- next account to register claims ownership.

local helperAreana = {}

local OWNER_KEY = "arenampServerOwner"

local function worldVariables()
    if WorldInstance == nil or WorldInstance.data == nil then return nil end
    if WorldInstance.data.customVariables == nil then
        WorldInstance.data.customVariables = {}
    end
    return WorldInstance.data.customVariables
end

local function saveWorld()
    if WorldInstance == nil then return end
    if type(WorldInstance.Save) == "function" then
        WorldInstance:Save()
    elseif type(WorldInstance.QuicksaveToDrive) == "function" then
        WorldInstance:QuicksaveToDrive()
    end
end

function helperAreana.GetOwnerName()
    local variables = worldVariables()
    if variables == nil then return nil end

    local name = variables[OWNER_KEY]
    if type(name) ~= "string" or name == "" then return nil end
    return name
end

local function isOwner(pid)
    local owner = helperAreana.GetOwnerName()
    if owner == nil or Players[pid] == nil then return false end
    return string.lower(owner) == string.lower(tostring(Players[pid].accountName))
end

-- Claims ownership for this account if nobody holds it yet.
local function claimOwnershipIfVacant(pid)
    if Players[pid] == nil then return false end
    if helperAreana.GetOwnerName() ~= nil then return false end

    local variables = worldVariables()
    if variables == nil then return false end

    variables[OWNER_KEY] = tostring(Players[pid].accountName)
    saveWorld()

    tes3mp.LogMessage(enumerations.log.WARN, "[ArenaMP Core] " .. tostring(Players[pid].accountName) ..
        " is the first registered account and is now the server owner")
    return true
end

-- Pushes the console permission this account should have to its client.
local function applyConsoleState(pid)
    local player = Players[pid]
    if player == nil or not player:IsLoggedIn() then return end

    if player.data.settings == nil then player.data.settings = {} end

    local allowed
    local previousRank = player.data.settings.staffRank
    local previousConsole = player.data.settings.consoleAllowed

    if isOwner(pid) then
        -- The owner is always staffRank 3 with the console on, even if an older
        -- player file says otherwise.
        if player.data.settings.staffRank ~= 3 then
            player.data.settings.staffRank = 3
            tes3mp.LogMessage(enumerations.log.INFO, "[ArenaMP Core] restored owner rank for " ..
                tostring(player.accountName))
        end
        player.data.settings.consoleAllowed = true
        allowed = true
    else
        -- "default" means "whatever the server config says". Because the config
        -- default is meant to stay off for everyone but the owner, it is resolved
        -- to an explicit false here; an explicit true set by staff is preserved.
        if player.data.settings.consoleAllowed ~= true then
            player.data.settings.consoleAllowed = false
        end
        allowed = player.data.settings.consoleAllowed == true
    end

    tes3mp.SetConsoleAllowed(pid, allowed)
    tes3mp.SendSettings(pid)

    -- Y110: sending settings does not imply the profile changed.
    if previousRank ~= player.data.settings.staffRank or previousConsole ~= player.data.settings.consoleAllowed then
        player:QuicksaveToDrive()
    end
end

-- Registration finishes with CharGen, which is where a brand new account first
-- exists as a real player file.
customEventHooks.registerHandler("OnPlayerEndCharGen", function(eventStatus, pid)
    claimOwnershipIfVacant(pid)
    applyConsoleState(pid)
end)

-- Applied again on every login, because serverCore/eventHandler push
-- config.allowConsole to the client during the login sequence and we want the
-- per-account decision to win.
customEventHooks.registerHandler("OnPlayerFinishLogin", function(eventStatus, pid)
    -- Covers a server whose owner registered before this script was installed:
    -- an existing staffRank 3 account claims the vacant slot on next login.
    if helperAreana.GetOwnerName() == nil and Players[pid] ~= nil
        and Players[pid].data.settings ~= nil and Players[pid].data.settings.staffRank == 3 then
        claimOwnershipIfVacant(pid)
    end

    applyConsoleState(pid)
end)

-- /owner shows who holds ownership. It is deliberately read-only: transferring
-- ownership is a manual edit of the world record, not a chat command.
customCommandHooks.registerCommand("owner", function(pid, cmd)
    local owner = helperAreana.GetOwnerName()
    if owner == nil then
        tes3mp.SendMessage(pid, color.Yellow .. "Владелец сервера ещё не назначен.\n", false)
    else
        tes3mp.SendMessage(pid, color.Yellow .. "Владелец сервера: " .. owner .. "\n", false)
    end
end)

return helperAreana
