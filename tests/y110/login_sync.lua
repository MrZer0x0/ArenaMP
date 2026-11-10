unpack = table.unpack or unpack
local root = SERVER_ROOT or 'server'
package.path = root .. '/scripts/?.lua;' .. root .. '/lib/lua/?.lua;' .. package.path
config = {dataPath = 'test-data', loginPerformance = {mapTilesPerTick = 16, mapTickMs = 25, mapBudgetMs = 2}}
enumerations = {log = {WARN = 2, INFO = 1}}
patterns = dofile(root .. '/lib/lua/patterns.lua')
customEventHooks = dofile(root .. '/scripts/customEventHooks.lua')
local clock, readCost, diskReads, fileChecks = 0, 0, 0, 0
local timers, sends, logs, buffer = {}, {}, {}, {}
local id = 0
tes3mp = {
    GetMillisecondsSinceServerStart = function() return clock end,
    CreateTimerEx = function(name, delay, signature, value)
        assert(name == 'ArenaLoginSync_MapTick' and delay >= 10 and signature == 'i')
        id = id + 1; timers[#timers + 1] = {name = name, delay = delay, value = value}; return id
    end,
    StartTimer = function() end,
    ClearMapChanges = function() buffer = {} end,
    DoesFilePathExist = function(path)
        fileChecks = fileChecks + 1; clock = clock + readCost
        return not path:find('99999,', 1, true)
    end,
    LoadMapTileImageFile = function(x, y, path)
        diskReads = diskReads + 1
        buffer[#buffer + 1] = {x = x, y = y, path = path}
    end,
    SendWorldMap = function(pid, others, skip)
        assert(others == false and skip == false, 'map must go only to the joining player')
        assert(#buffer <= 16, 'unbounded map packet')
        sends[#sends + 1] = {pid = pid, tiles = buffer}
    end,
    LogMessage = function(_, message) logs[#logs + 1] = message end
}
Players = {}
local function player(pid)
    local p = {loggedIn = true, data = {settings = {staffRank = 0, consoleAllowed = false}}, saves = 0}
    function p:IsLoggedIn() return self.loggedIn end
    function p:QuicksaveToDrive() self.saves = self.saves + 1 end
    Players[pid] = p; return p
end
local function tick()
    assert(#timers == 1, 'scheduler must use one global timer')
    local timer = table.remove(timers, 1); clock = clock + timer.delay
    _G[timer.name](timer.value)
end
local function drain()
    local n = 0
    while #timers > 0 do tick(); n = n + 1; assert(n < 10000, 'stuck map queue') end
    return n
end
local function count(pid)
    local n = 0
    for _, send in ipairs(sends) do if send.pid == pid then n = n + #send.tiles end end
    return n
end
local function tiles(n, y)
    local t = {}; for x = 1, n do t[x] = x .. ', ' .. (y or 0) end; return t
end
local sync = require('loginSyncHelper')
player(1); local explored = tiles(1662)
sync.QueueMap(1, explored)
assert(diskReads == 0 and fileChecks == 0 and #sends == 0, 'login must not read map files')
assert(drain() == 104 and count(1) == 1662 and #explored == 1662)
print('PASS 1662 map files: 0 synchronous reads at login, 104 bounded ticks, complete delivery')

player(2); readCost = 1
local reads = fileChecks; sync.QueueMap(2, tiles(10)); tick()
assert(fileChecks - reads == 2, 'elapsed-time budget must stop a slow disk batch')
drain(); readCost = 0
player(3); player(4)
sync.QueueMap(3, tiles(32)); sync.QueueMap(4, tiles(32))
local n = #sends; tick(); tick()
assert(sends[n + 1].pid == 3 and sends[n + 2].pid == 4, 'simultaneous logins must be fair')
drain()

player(5); sync.QueueMap(5, tiles(50)); tick(); local before = count(5)
player(5) -- reuse PID before the stale timer fires, including an identical account name
drain(); assert(count(5) == before, 'old session data reached a new Player object')
player(6); sync.QueueMap(6, tiles(20))
customEventHooks.triggerHandlers('OnPlayerDisconnect', customEventHooks.makeEventStatus(true, true), {6})
drain(); assert(count(6) == 0, 'disconnect failed to cancel map sync')
player(7); sync.QueueMap(7, tiles(100)); sync.QueueMap(7, tiles(3)); drain()
assert(count(7) == 3, 'a replacement map job did not supersede the old one')
player(8); sync.QueueMap(8, {[1] = 'bad', [4] = '../1, 0', [8] = '1, 2', [9] = '99999, 0'})
drain(); assert(count(8) == 1, 'sparse arrays/invalid paths/missing images were not handled')
print('PASS time budget, simultaneous logins, reused PIDs, disconnect, replacement and malformed tiles')

class = function() return {} end
local state = dofile(root .. '/scripts/stateHelper.lua')
player(9); local profile = {data = {mapExplored = tiles(25), factionExpulsion = {}}}
profile.saves = 0
function profile:QuicksaveToDrive() self.saves = self.saves + 1 end
reads = diskReads; state:LoadMap(9, profile)
assert(diskReads == reads and #profile.data.mapExplored == 25)
drain(); assert(count(9) == 25)
tes3mp.ClearFactionChanges = function() end
tes3mp.SetFactionChangesAction = function() end
tes3mp.SetFactionId = function() end
tes3mp.SetFactionExpulsionState = function() end
tes3mp.AddFaction = function() end
tes3mp.SendFactionChanges = function() end
tes3mp.ClearClientGlobals = function() end
tes3mp.AddClientGlobalInteger = function() end
tes3mp.AddClientGlobalFloat = function() end
tes3mp.SendClientScriptGlobal = function() end
enumerations.faction = {EXPULSION = 1}; enumerations.variableType = {SHORT = 0, LONG = 1, FLOAT = 2}
clientVariableScopes = {globals = {factionExpulsion = {'expelled'}}}
state:ClearFactionExpulsionForLogin(9, profile, true)
state:ClearFactionExpulsionGlobalsForLogin(9, profile, {data = {}}, true)
assert(profile.saves == 0, 'unchanged profile was written to disk')
profile.data.factionExpulsion = {fighters = true}
profile.data.clientVariables = {globals = {expelled = {variableType = 0, intValue = 1}}}
state:ClearFactionExpulsionForLogin(9, profile, true)
state:ClearFactionExpulsionGlobalsForLogin(9, profile, {data = {}}, true)
assert(profile.saves == 2 and next(profile.data.factionExpulsion) == nil
    and profile.data.clientVariables.globals.expelled == nil, 'real changes must still be persisted')

local order = {}
customEventHooks.registerHandler('OnPlayerAuthentified', function(status)
    order[#order + 1] = 1; clock = clock + 30
    return customEventHooks.makeEventStatus(false, true)
end)
customEventHooks.registerHandler('OnPlayerAuthentified', function(status)
    assert(status.validDefaultHandler == false); order[#order + 1] = 2
end)
customEventHooks.triggerHandlers('OnPlayerAuthentified', customEventHooks.makeEventStatus(true, true), {9})
assert(order[1] == 1 and order[2] == 2 and logs[#logs]:find('OnPlayerAuthentified', 1, true))
print('PASS actual StateHelper integration, conditional persistence and timed hook ordering')

tes3mp.SetConsoleAllowed = function() end
tes3mp.SendSettings = function() end
customCommandHooks = {registerCommand = function() end}
WorldInstance = {data = {customVariables = {arenampServerOwner = 'owner'}}}
local helper = dofile(root .. '/scripts/helperAreana.lua')
local hooks = customEventHooks.handlers.OnPlayerFinishLogin
local normal = player(10); normal.accountName = 'normal'
hooks[#hooks]({}, 10); assert(normal.saves == 0)
normal.data.settings.consoleAllowed = 'default'
hooks[#hooks]({}, 10); assert(normal.saves == 1 and normal.data.settings.consoleAllowed == false)
hooks[#hooks]({}, 10); assert(normal.saves == 1)
local owner = player(11); owner.accountName = 'owner'
hooks[#hooks]({}, 11); assert(owner.saves == 1 and owner.data.settings.staffRank == 3 and owner.data.settings.consoleAllowed)
hooks[#hooks]({}, 11); assert(owner.saves == 1)
print('PASS console permissions are saved only on change; owner/admin behavior retained')
