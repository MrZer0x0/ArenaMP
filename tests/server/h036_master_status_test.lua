-- Run from the ArenaMP project root: lua tests/server/h036_master_status_test.lua
local logs = {}
local now = 1000
os.time = function() return now end
enumerations = { log = { WARN = 1, ERROR = 2 } }
tes3mp = { GetMaxPlayers=function() return 100 end, GetPort=function() return 25565 end, HasPassword=function() return false end, LogMessage = function(_, text) logs[#logs + 1] = text end }
config = { dataPath = 'D:/Игры/ДругойСервер/data' }
doesModuleExist = function() return false end
package.preload.dkjson = function() return {} end
jsonInterface = dofile('server/lib/lua/jsonInterface.lua')
logs = {}
local contents, writtenPath
local mode = 'success'
local customIo = { open = function(path, flags)
    writtenPath = path
    assert(flags == 'w+b')
    if mode == 'missing' then return nil end
    if mode == 'exception' then error('write denied') end
    return { write = function(_, text) contents = text end, close = function() end }
end }
jsonInterface.setLibrary(customIo)
-- Windows IO must go through the configured library, never ordinary io.open.
io.open = function() error('wrong IO library') end
Players = { [0] = { IsLoggedIn = function() return true end }, [1] = { IsLoggedIn = function() return false end }, [2] = { IsLoggedIn = function() return true end } }
local status = dofile('server/scripts/masterStatus.lua')
assert(status.write())
assert(writtenPath == config.dataPath .. '/custom/arenaMasterStatus.json')
assert(contents == '{"players":2,"max_players":100,"port":25565,"password_required":false,"timestamp":1000,"started_at":1000}')
now = 1010; Players[2] = nil
assert(status.write())
assert(contents == '{"players":1,"max_players":100,"port":25565,"password_required":false,"timestamp":1010,"started_at":1000}')
mode = 'missing'
assert(status.write() == false and #logs == 1)
assert(logs[1]:find(config.dataPath, 1, true))
assert(status.write() == false and #logs == 1) -- rate-limited failure
now = 1071; mode = 'exception'
assert(status.write() == false and #logs == 2)
mode = 'success'; Players = {}
assert(status.write())
assert(contents == '{"players":0,"max_players":100,"port":25565,"password_required":false,"timestamp":1071,"started_at":1000}')
print('PASS: configured data path, Unicode IO routing, real player count, restart timestamp, failure reporting and recovery')
