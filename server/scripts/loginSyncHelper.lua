-- ArenaMP Y110: cooperative login map sync. Disk reads are bounded globally,
-- not separately per connecting player. No network/schema change is required.
local M = {}
local jobs, byPid = {}, {}
local timerId

local function milliseconds()
    if type(tes3mp.GetMillisecondsSinceServerStart) == "function" then
        return tes3mp.GetMillisecondsSinceServerStart()
    end
    return os.clock() * 1000
end
M.Now = milliseconds

local function option(name, fallback, low, high)
    local options = type(config.loginPerformance) == "table" and config.loginPerformance or {}
    local value = tonumber(options[name]) or fallback
    if value ~= value or value == math.huge or value == -math.huge then value = fallback end
    return math.max(low, math.min(high, value))
end

function M.LogSlow(label, started, pid)
    local elapsed = milliseconds() - started
    if elapsed >= option("slowPhaseMs", 25, 1, 60000) then
        tes3mp.LogMessage(enumerations.log.WARN, string.format(
            "[LoginSync] %s pid=%s took %.1f ms", label, tostring(pid), elapsed))
    end
end

local function armTimer()
    if timerId == nil and #jobs > 0 then
        timerId = tes3mp.CreateTimerEx("ArenaLoginSync_MapTick",
            math.floor(option("mapTickMs", 25, 10, 1000)), "i", 0)
        tes3mp.StartTimer(timerId)
    end
end

function M.CancelMap(pid)
    byPid[pid] = nil
    for i = #jobs, 1, -1 do
        if jobs[i].pid == pid then table.remove(jobs, i) end
    end
end

function M.QueueMap(pid, explored)
    M.CancelMap(pid)
    if not Players[pid] or type(explored) ~= "table" then return end
    local tiles = {}
    for _, cell in pairs(explored) do
        if type(cell) == "string" then
            local x, y = cell:match("^" .. patterns.exteriorCell)
            x, y = tonumber(x), tonumber(y)
            if x and y then tiles[#tiles + 1] = {cell = cell, x = x, y = y} end
        end
    end
    if #tiles == 0 then return end
    local job = {pid = pid, player = Players[pid], tiles = tiles, next = 1,
        started = milliseconds(), sent = 0, workMs = 0, maxTickMs = 0}
    jobs[#jobs + 1] = job
    byPid[pid] = job
    armTimer()
end

function ArenaLoginSync_MapTick()
    timerId = nil
    local job = table.remove(jobs, 1)
    if not job then return end
    -- A PID can be reused immediately after disconnect. A job belongs to the
    -- original Player object and must never reach its replacement.
    if byPid[job.pid] ~= job or Players[job.pid] ~= job.player or not job.player:IsLoggedIn() then
        if byPid[job.pid] == job then byPid[job.pid] = nil end
        armTimer()
        return
    end

    local started, count, checked = milliseconds(), 0, 0
    local limit = math.floor(option("mapTilesPerTick", 16, 1, 64))
    local budget = option("mapBudgetMs", 2, .25, 10)
    tes3mp.ClearMapChanges()
    while job.next <= #job.tiles and checked < limit do
        local tile = job.tiles[job.next]
        job.next, checked = job.next + 1, checked + 1
        local path = config.dataPath .. "/map/" .. tile.cell .. ".png"
        if tes3mp.DoesFilePathExist(path) then
            tes3mp.LoadMapTileImageFile(tile.x, tile.y, path)
            count = count + 1
        end
        -- Check after the first read so even a small budget makes progress.
        if milliseconds() - started >= budget then break end
    end
    if count > 0 then tes3mp.SendWorldMap(job.pid, false, false) end
    job.sent = job.sent + count
    local elapsed = milliseconds() - started
    job.workMs, job.maxTickMs = job.workMs + elapsed, math.max(job.maxTickMs, elapsed)
    M.LogSlow("map batch", started, job.pid)
    if job.next <= #job.tiles then
        jobs[#jobs + 1] = job -- round-robin between simultaneous logins
    else
        byPid[job.pid] = nil
        tes3mp.LogMessage(enumerations.log.INFO, string.format(
            "[LoginSync] map complete pid=%s tiles=%d work=%.1f ms maxBatch=%.1f ms elapsed=%.1f ms",
            tostring(job.pid), job.sent, job.workMs, job.maxTickMs, milliseconds() - job.started))
    end
    armTimer()
end

customEventHooks.registerHandler("OnPlayerDisconnect", function(_, pid) M.CancelMap(pid) end)
return M
