-- ArenaMP X051: persistent player position + reconnect collision guard.
--
-- Vanilla TES3MP 0.8.1 forwards ID_PLAYER_POSITION without exposing it to Lua.
-- X051's ProcessorPlayerPosition callback lets this helper continuously cache the
-- last authoritative position, so server shutdown never has to reconstruct it
-- from a half-closed native peer.

local positionSafetyHelper = {}

local function getCfg()
    local cfg = config.positionSafety or {}
    return {
        enabled = cfg.enabled ~= false,
        autosaveSeconds = math.max(1, tonumber(cfg.autosaveSeconds) or 10),
        loginGuardEnabled = cfg.loginGuardEnabled ~= false,
        loginGuardDelaysMs = type(cfg.loginGuardDelaysMs) == "table" and cfg.loginGuardDelaysMs or {400, 1200, 3000},
        loginGuardFallThreshold = math.max(4, tonumber(cfg.loginGuardFallThreshold) or 24),
        loginGuardMoveCancelDistance = math.max(16, tonumber(cfg.loginGuardMoveCancelDistance) or 96),
        maxAbsCoordinate = math.max(100000, tonumber(cfg.maxAbsCoordinate) or 100000000)
    }
end

local function finite(value, maxAbs)
    return type(value) == "number" and value == value and value ~= math.huge and value ~= -math.huge
        and math.abs(value) <= maxAbs
end

local function validTransform(x, y, z, rx, rz)
    local maxAbs = getCfg().maxAbsCoordinate
    return finite(x, maxAbs) and finite(y, maxAbs) and finite(z, maxAbs)
        and finite(rx, maxAbs) and finite(rz, maxAbs)
end

local function copyLocation(location)
    if type(location) ~= "table" then return nil end
    return {
        cell = location.cell,
        regionName = location.regionName,
        posX = location.posX,
        posY = location.posY,
        posZ = location.posZ,
        rotX = location.rotX,
        rotZ = location.rotZ
    }
end

local function quicksave(player)
    if player == nil or player.hasAccount ~= true then return false end
    if player.QuicksaveToDrive ~= nil then
        player:QuicksaveToDrive()
    else
        player:SaveToDrive()
    end
    player.positionLastQuicksaveTime = os.time()
    player.positionDirty = false
    return true
end

function positionSafetyHelper.CacheCurrentPosition(pid)
    local cfg = getCfg()
    if not cfg.enabled then return false end

    local player = Players[pid]
    if player == nil or not player:IsLoggedIn() or type(player.data) ~= "table" then return false end

    -- A private dynamic interior already has its own stricter login restore.
    -- Do not let a stale pre-restore position packet overwrite that saved transform.
    if type(player.privateCellLoginRestore) == "table" then return false end

    local x, y, z = tes3mp.GetPosX(pid), tes3mp.GetPosY(pid), tes3mp.GetPosZ(pid)
    local rx, rz = tes3mp.GetRotX(pid), tes3mp.GetRotZ(pid)
    if not validTransform(x, y, z, rx, rz) then
        tes3mp.LogAppend(enumerations.log.WARN,
            "[ArenaMP Core] Rejected invalid position packet for " .. tostring(player.accountName or pid))
        return false
    end

    if type(player.data.location) ~= "table" then player.data.location = {} end

    local guard = player.loginPositionGuard
    if type(guard) == "table" and guard.active == true and guard.cell == player.data.location.cell then
        local dx = x - (guard.posX or x)
        local dy = y - (guard.posY or y)
        local horizontalDistance = math.sqrt(dx * dx + dy * dy)
        -- Horizontal movement is a reliable sign that the player is already in
        -- control. Pure downward Z movement may be the exact fall-through bug,
        -- so it intentionally does not cancel the guard.
        if horizontalDistance >= cfg.loginGuardMoveCancelDistance then
            guard.active = false
        elseif z < (guard.posZ or z) - cfg.loginGuardFallThreshold then
            -- This is most likely the reconnect fall-through itself. Keep the
            -- pre-login transform authoritative and do not autosave the bad Z.
            return false
        end
    end

    player.data.location.posX = x
    player.data.location.posY = y
    player.data.location.posZ = z
    player.data.location.rotX = rx
    player.data.location.rotZ = rz
    player.positionDirty = true
    player.positionLastPacketTime = os.time()

    if os.time() - (player.positionLastQuicksaveTime or 0) >= cfg.autosaveSeconds then
        quicksave(player)
    end
    return true
end

function positionSafetyHelper.FlushCachedPosition(pid)
    local player = Players[pid]
    if player == nil or not player:IsLoggedIn() then return false end
    -- Never query native GetPos here: OnPlayerDisconnect can be running after
    -- the peer has started closing. The caller's normal SaveToDrive() persists
    -- the last PlayerPosition/CellChange transform already accepted in memory.
    player.positionDirty = false
    return true
end

function positionSafetyHelper.StartLoginGuard(pid)
    local cfg = getCfg()
    if not cfg.enabled or not cfg.loginGuardEnabled then return false end

    local player = Players[pid]
    if player == nil or not player:IsLoggedIn() or type(player.data) ~= "table"
        or type(player.data.location) ~= "table" then return false end

    local location = copyLocation(player.data.location)
    if location == nil or type(location.cell) ~= "string" or location.cell == ""
        or not validTransform(location.posX, location.posY, location.posZ, location.rotX, location.rotZ) then
        return false
    end

    player.loginPositionGuard = location
    player.loginPositionGuard.active = true
    player.loginPositionGuard.accountName = player.accountName
    player.loginPositionGuard.startedAt = os.time()
    player.loginPositionGuard.remainingTimers = 0

    for _, delayValue in ipairs(cfg.loginGuardDelaysMs) do
        local delay = math.floor(tonumber(delayValue) or 0)
        if delay > 0 then
            player.loginPositionGuard.remainingTimers = player.loginPositionGuard.remainingTimers + 1
            local timerId = tes3mp.CreateTimerEx("ArenaMP_PositionGuardTimer", delay, "is", pid, player.accountName)
            tes3mp.StartTimer(timerId)
        end
    end
    return true
end

function positionSafetyHelper.ReassertLoginPosition(pid, accountName)
    local cfg = getCfg()
    if not cfg.enabled or not cfg.loginGuardEnabled then return false end

    local player = Players[pid]
    if player == nil or not player:IsLoggedIn() or player.accountName ~= accountName then return false end
    local guard = player.loginPositionGuard
    if type(guard) ~= "table" or guard.active ~= true then return false end

    guard.remainingTimers = math.max(0, (guard.remainingTimers or 1) - 1)
    local function finish(result)
        if guard.remainingTimers <= 0 then guard.active = false end
        return result
    end

    -- Dynamic personal interiors finish their own CELL-record handshake first.
    if type(player.privateCellLoginRestore) == "table" then return finish(false) end

    local currentCell = tes3mp.GetCell(pid)
    if currentCell ~= guard.cell then
        guard.active = false
        return finish(false)
    end

    local x, y, z = tes3mp.GetPosX(pid), tes3mp.GetPosY(pid), tes3mp.GetPosZ(pid)
    if not finite(x, cfg.maxAbsCoordinate) or not finite(y, cfg.maxAbsCoordinate)
        or not finite(z, cfg.maxAbsCoordinate) then
        x, y, z = guard.posX, guard.posY, guard.posZ
    end

    local dx = x - guard.posX
    local dy = y - guard.posY
    local horizontalDistance = math.sqrt(dx * dx + dy * dy)
    if horizontalDistance >= cfg.loginGuardMoveCancelDistance then
        guard.active = false
        return finish(false)
    end

    local shouldReassert = guard.sentOnce ~= true or z < guard.posZ - cfg.loginGuardFallThreshold
    if shouldReassert then
        tes3mp.SetPos(pid, guard.posX, guard.posY, guard.posZ)
        tes3mp.SetRot(pid, guard.rotX, guard.rotZ)
        tes3mp.SendPos(pid)
        guard.sentOnce = true
        tes3mp.LogAppend(enumerations.log.INFO,
            "[ArenaMP Core] Reasserted login transform for " .. tostring(accountName) ..
            " in " .. tostring(guard.cell))
    end
    return finish(shouldReassert)
end

-- Timer callbacks are looked up by global function name by TES3MP.
function ArenaMP_PositionGuardTimer(pid, accountName)
    positionSafetyHelper.ReassertLoginPosition(pid, accountName)
end

customEventHooks.registerHandler("OnPlayerAuthentified", function(eventStatus, pid)
    positionSafetyHelper.StartLoginGuard(pid)
end)

return positionSafetyHelper
