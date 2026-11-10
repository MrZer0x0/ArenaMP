-- Regression: lost full snapshot, deferred corpse request, and no sync chat spam.
local function method(path, prefix, name, nextName, target)
    local f = assert(io.open(path)); local source = f:read('*a'); f:close()
    local first = assert(source:find(prefix .. name .. ' = function', 1, true)
        or source:find('function ' .. prefix .. name .. '(', 1, true), name)
    local last = assert(source:find(nextName, first + 1, true), name .. ' end')
    assert(load(source:sub(first, last - 1)))()
end
local base = 'server/scripts/cell/base.lua'
BaseCell = {}
local function loadMethod(name, nextName)
    method(base, 'BaseCell:', name, 'function BaseCell:' .. nextName, BaseCell)
end
loadMethod('HasFullContainerData', 'EnsureFullContainerData')
loadMethod('EnsureFullContainerData', 'RetryUnusableContainers')
loadMethod('RetryUnusableContainers', 'HasFullActorList')
loadMethod('RequestContainers', 'RequestActorList')
local now = 100
local oldTime = os.time
os.time = function() return now end
local sent, chat = {}, {}
tableHelper = {
    containsValue = function(arr, value)
        for _, entry in pairs(arr) do if entry == value then return true end end
        return false
    end,
    removeValue = function(arr, value)
        for k, entry in pairs(arr) do if entry == value then arr[k] = nil end end
    end,
    getIndexByValue = function() return 'CLIENT_GAMEPLAY' end,
}
enumerations = { container = { SET = 0, REQUEST = 3 }, containerSub = { NONE = 0, REPLY_TO_REQUEST = 4 },
    log = { INFO = 1 }, packetOrigin = { CLIENT_GAMEPLAY = 0 } }
logicHandler = { GetChatName = function() return 'test-player' end,
    IsPacketFromConsole = function() return false end,
    DoesPacketOriginRequireLoadedCell = function() return true end }
config = { allowOnContainerForUnloadedCells = false }
string.split = function(self, delimiter) local a,b = self:match('^(%d+)%-(%d+)$'); return {a,b} end
local size, subaction, action, origin = 0, 0, 0, 0
tes3mp = setmetatable({
    GetObjectListSize = function() return size end,
    GetObjectListContainerSubAction = function() return subaction end,
    GetObjectListAction = function() return action end,
    GetObjectListOrigin = function() return origin end,
    SetObjectListAction = function(v) action = v end,
    SetObjectListContainerSubAction = function(v) subaction = v end,
    SetObjectRefNum = function() end, SetObjectMpNum = function() end,
    SendContainer = function() sent[#sent + 1] = { action = action, subaction = subaction } end,
    LogAppend = function() end, LogMessage = function() end,
}, { __index = function() return function() end end })
local c = setmetatable({description = 'cell', visitors = {1, 2},
    data = {loadState = {hasFullContainerData=false}, objectData = {}},
    unusableContainerUniqueIndexes = {}, pendingContainerRefreshes = {}, deferredContainerRequests = {},
    isRequestingContainerData = false}, {__index=BaseCell})
LoadedCells = { cell = c }
Players = {[1] = {IsLoggedIn=function() return true end, Message=function(_, message) chat[#chat + 1] = message end},
    [2] = {IsLoggedIn=function() return true end, Message=function(_, message) chat[#chat + 1] = message end}}
assert(c:EnsureFullContainerData(1))
assert(#sent == 1 and c.containerRequestPid == 1)
assert(not c:EnsureFullContainerData(2) and #sent == 1)
c:RequestContainers(1, {'11-0'})
assert(#sent == 1 and c.deferredContainerRequests['11-0'] and c.containerRequestPid == 1)
now = now + 5
assert(c:EnsureFullContainerData(2) and #sent == 2 and c.containerRequestPid == 2)
-- A gameplay action during the handshake must initiate retry but never chat-spam.
eventHandler = {}
local f = assert(io.open('server/scripts/eventHandler.lua'))
local source = f:read('*a'); f:close()
local first = assert(source:find('eventHandler.OnContainer = function',1,true))
local last = assert(source:find('eventHandler.OnVideoPlay = function', first+1,true))
assert(load(source:sub(first,last-1)))()
eventHandler.OnContainer(2,'cell')
assert(#chat == 0 and #sent == 2, 'blocked transactions should be silent and rate limited')
-- Stale reply from the previous requester's cell must not finish the handshake.
subaction = 4; action = 0
eventHandler.OnContainer(1,'cell')
assert(#chat == 0 and c.containerRequestPid == 2 and not c:HasFullContainerData())
-- Fully process a matching response (empty cell), then issue deferred partial.
c.SaveContainers = function(self, pid)
    assert(pid == 2)
    self.isRequestingContainerData = false
    self.containerRequestPid = nil
    self.data.loadState.hasFullContainerData = true
    local deferred = self.deferredContainerRequests
    self.deferredContainerRequests = {}
    local ids = {}
    for id in pairs(deferred) do table.insert(ids,id) end
    if #ids > 0 then self:RequestContainers(pid,ids) end
end
customEventHooks = {triggerValidators=function() return {validDefaultHandler=true} end,
    triggerHandlers=function() end}
packetReader = {GetObjectPacketTables=function() return {objects={},players={}} end}
eventHandler.OnContainer(2,'cell')
assert(c:HasFullContainerData() and #sent == 3 and not c.isRequestingContainerData)
-- The corpse refresh must not reopen the full-snapshot gate.
c.unusableContainerUniqueIndexes = {'11-0'}
assert(c:RetryUnusableContainers(2,'11-0'))
assert(#sent == 4 and not c.isRequestingContainerData)
assert(not c:RetryUnusableContainers(2,'11-0'))
now = now + 5
assert(c:RetryUnusableContainers(2,'11-0') and #sent == 5)
assert(#chat == 0)
os.time = oldTime
print('PASS: silent container gate; timeout retry; late reply rejected; partial request does not steal full handshake; corpse retry')
