-- Server-owned online-time sentence. Never use a client timestamp or timer.
local M = {}
local serial = 0
local function clock() return tes3mp.GetMillisecondsSinceServerStart() / 1000 end
local function state(p) return p and p.data.customVariables and p.data.customVariables.ampBountyRespawn end
function M.Pending(p) return state(p) ~= nil end
local function save(p)
    if jsonInterface.saveAtomic("player/" .. p.accountFile, p.data, config.playerKeyOrder) then return true end
    -- No clearing of a sentence when storage is unavailable.
    tes3mp.LogMessage(enumerations.log.ERROR, "[BountyRespawn] could not persist " .. p.accountName)
    tes3mp.StopServer(2)
    return false
end
local function send(p)
    local s = state(p)
    tes3mp.SendMessage(p.pid, string.format("@@AMP_REVIVE@@BOUNTY\t%.2f\t%.2f\n",
        s and s.remaining or 0, s and s.total or 0), false)
end
local function settle(p)
    local s = state(p)
    if s and p.ampBountyClock then
        s.remaining = math.max(0, s.remaining - math.max(0, clock() - p.ampBountyClock))
        p.ampBountyClock = clock()
    end
end
function M.Enforce(p)
    if not M.Pending(p) then return false end
    p.data.stats.healthCurrent = 0
    p:LoadStatsDynamic()
    local loc = state(p).location
    if loc then
        if tes3mp.GetCell(p.pid) ~= loc.cell then
            tes3mp.SetCell(p.pid, loc.cell)
            tes3mp.SendCell(p.pid)
        end
        tes3mp.SetPos(p.pid, loc.posX, loc.posY, loc.posZ)
        tes3mp.SendPos(p.pid)
    end
    return true
end
local function schedule(p)
    p.ampBountyTimer = tes3mp.CreateTimerEx("ArenaMP_BountyRespawnTick", time.seconds(1), "isi",
        p.pid, p.accountName, p.ampBountyToken)
    tes3mp.StartTimer(p.ampBountyTimer)
end
function M.Resume(p)
    if not M.Pending(p) or p.ampBountyClock then return end
    tes3mp.SetRespawnLocked(p.pid, true)
    for _, cell in pairs(LoadedCells) do
        if type(cell.GetAuthority) == "function" and cell:GetAuthority() == p.pid then cell:SetAuthority(p.pid) end
    end
    serial = serial + 1
    p.ampBountyToken = serial
    p.ampBountyClock = clock()
    M.Enforce(p)
    send(p)
    schedule(p)
end
function M.Begin(p)
    if M.Pending(p) then return true end
    if config.bountyRespawnEnabled == false or not config.playersRespawn then return false end
    -- Shared crime is a different game mode; never forgive everyone else's bounty.
    if config.shareBounty then return false end
    local bounty = math.max(0, tonumber(tes3mp.GetBounty(p.pid)) or 0)
    local seconds = math.floor(bounty / 1000) * 30
    if seconds == 0 then return false end
    p.data.customVariables = p.data.customVariables or {}
    local total = math.max(seconds, tonumber(config.deathTime) or 0)
    p.data.customVariables.ampBountyRespawn = {remaining=total, total=total,
        bounty=bounty, location={cell=tes3mp.GetCell(p.pid), posX=tes3mp.GetPosX(p.pid),
        posY=tes3mp.GetPosY(p.pid), posZ=tes3mp.GetPosZ(p.pid)}}
    -- Bounty and sentence are saved in the same profile write, before notifying the client.
    p.data.fame = p.data.fame or {}
    p.data.fame.bounty = 0
    p.data.stats.healthCurrent = 0
    p.data.spellsActive = {}
    p.data.stats.experience = 0
    if deathRecovery then deathRecovery.Cancel(p, false) end
    if not save(p) then return true end
    tes3mp.SetBounty(p.pid, 0)
    tes3mp.SendBounty(p.pid)
    p:LoadLevel()
    M.Resume(p)
    return true
end
function M.AllowRespawn(p)
    if not M.Pending(p) then return true end
    settle(p)
    if state(p).remaining > 0 then send(p); return false end
    return p.ampBountyCompleting == true
end
function M.Complete(p)
    if not M.Pending(p) or not p.ampBountyCompleting then return end
    p.data.stats.healthCurrent = math.max(1, tonumber(p.data.stats.healthBase) or 1)
    p:LoadStatsDynamic()
    p.data.customVariables.ampBountyRespawn = nil
    p.ampBountyClock = nil
    p.ampBountyCompleting = nil
    -- A crash before this save leaves a zero-time sentence, retried on login.
    if save(p) then send(p) end
end
function M.Tick(pid, account, token)
    local p = Players[pid]
    if not p or not p:IsLoggedIn() or p.accountName ~= account or p.ampBountyToken ~= token or not M.Pending(p) then return end
    p.ampBountyTimer = nil
    settle(p)
    if not save(p) then return end
    if state(p).remaining <= 0 then
        p.ampBountyCompleting = true
        tes3mp.SetRespawnLocked(p.pid, false)
        p:Resurrect()
        return
    end
    M.Enforce(p)
    send(p)
    schedule(p)
end
function M.Disconnect(p)
    if not M.Pending(p) then return end
    settle(p)
    p.data.stats.healthCurrent = 0
    if p.ampBountyTimer then tes3mp.StopTimer(p.ampBountyTimer) end
    p.ampBountyToken = nil
    p.ampBountyClock = nil
    save(p)
end
function ArenaMP_BountyRespawnTick(pid, account, token) M.Tick(pid, account, token) end
customEventHooks.registerHandler("OnPlayerAuthentified", function(status, pid)
    if status.validDefaultHandler and Players[pid] then M.Resume(Players[pid]) end
end)
customEventHooks.registerHandler("OnServerInit", function()
    for _, name in ipairs({"SetRespawnLocked", "ReplaceFileAtomic", "GetFileStatusUtf8"}) do
        if type(tes3mp[name]) ~= "function" then
            tes3mp.LogMessage(enumerations.log.ERROR, "[ArenaMP] rebuild the server: missing API " .. name)
            tes3mp.StopServer(2)
            return
        end
    end
end)
return M
