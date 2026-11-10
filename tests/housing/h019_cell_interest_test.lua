local root = 'builder/ArenaMP_Builder/server/scripts/'
local probe=io.open(root..'houseHelper.lua','r')
if probe then probe:close() else root='server/scripts/' end
config = {houseSystem = {}, dataPath = '/tmp/arena-tests'}
package.loaded.config = config
local saved
package.loaded.jsonInterface = {load = function() return saved end, save = function(_, data) saved=data end}
package.loaded.fileHelper = {fixFilename=function(s) return s end}
package.loaded.inventoryHelper = {
 removeClosestItem=function(inv, _, n) inv[1].count=inv[1].count-n end,
 addItem=function(inv, _, n) inv[1].count=inv[1].count+n end}
package.loaded.packetBuilder = {AddCellRecord=function() end}
tableHelper = {cleanNils=function() end, concatenateFromIndex=function(t,i) return table.concat(t,' ',i) end}
local handlers, validators, commands = {}, {}, {}
customEventHooks = {
 registerHandler=function(k,f) handlers[k]=handlers[k] or {};table.insert(handlers[k],f) end,
 registerValidator=function(k,f) validators[k]=validators[k] or {};table.insert(validators[k],f) end,
 makeEventStatus=function(a,b) return {validDefaultHandler=a,validCustomHandlers=b} end}
customCommandHooks={registerCommand=function(k,f) commands[k]=f end}
enumerations={log={INFO=1,WARN=2,ERROR=3}, inventory={ADD=1,REMOVE=2},recordType={CELL=1},containerSub={REPLY_TO_REQUEST=4}}
local cells, messages, dialogs, resets = {}, {}, {}, {}
tes3mp=setmetatable({
 GetCell=function(pid) return cells[pid] end,
 SetCell=function(pid,cell) cells[pid]=cell end,
 SendMessage=function(pid,msg) table.insert(messages,{pid,msg}) end,
 MessageBox=function(pid,id,text) table.insert(messages,{pid,text}); dialogs[pid]={id,text} end,
 ListBox=function(pid,id,text,opts) dialogs[pid]={id,text,opts} end,
 CustomMessageBox=function(pid,id,text,opts) dialogs[pid]={id,text,opts} end,
 InputDialog=function(pid,id,text) dialogs[pid]={id,text} end,
 GetObjectListContainerSubAction=function() return 2 end
}, {__index=function() return function() end end})
Players={}
for id,name in ipairs({'Alice','Bob','Carol','Dan'}) do
 Players[id]={accountName=name,language='RU',data={stats={level=50},settings={staffRank=2},inventory={{refId='gold_001',count=1000000}},location={cell='0, 0',posX=1,posY=2,posZ=3}},IsLoggedIn=function() return true end,LoadItemChanges=function() end}
 cells[id]='0, 0'
end
RecordStores={cell={data={permanentRecords={}},Save=function() end}}
periodicCellResets={fullResetCell=function(cell) table.insert(resets,cell);return true end}
tableHelper.deepCopy=dofile('server/lib/lua/tableHelper.lua').deepCopy
local h=dofile(root..'houseHelper.lua')
local function gui(pid,id,data) for _,f in ipairs(handlers.OnGUIAction) do f({},pid,id,tostring(data)) end end
local function hasMessage(text) for _,m in ipairs(messages) do if m[2]:find(text,1,true) then return true end end end

function string:split(separator)
 local result={}
 for part in self:gmatch('[^'..separator..']+') do result[#result+1]=part end
 return result
end
saved={properties={
 A={cell='A',cells={'A','A Basement'},price=100},
 B={cell='B',cells={'B'},price=200},
 C={cell='C',cells={'C'},price=300}
},doors={A={'0, 0'},B={'1, 0'}}}
local function state()
 for _,m in ipairs(messages) do
  local payload=m[2]:match('^@@AMP_HOUSE@@(CELL_STATE[^\n]*)')
  if payload then return payload end
 end
 error('missing local snapshot')
end
cells[1]='0, 0';messages={};h.SendClientState(1)
assert(state():find('\tA^',1,true))
assert(not state():find('\tB^',1,true) and not state():find('\tC^',1,true))
assert(not state():find('\tA Basement^',1,true))
local function interest(cell,destination)
 messages={}
 local message='@@AMP_HOUSE@@INTEREST\t'..cell..'\t'..destination
 for _,v in ipairs(validators.OnPlayerSendMessage) do v({},1,message) end
end
interest('0, 0','C') -- Unobserved door discovered from the client's loaded ESM.
assert(state():find('\tC^',1,true) and state():find('\tA^',1,true))
assert(saved.doors.C==nil) -- Client claims never become persistent geography.
interest('0, 0','Not Registered')
assert(saved.properties['Not Registered']==nil)
cells[1]='1, 0';messages={};h.SendClientState(1)
assert(state():find('\tB^',1,true) and not state():find('\tC^',1,true))
interest('0, 0','C'); assert(#messages==0) -- Stale client request ignored.
interest('1, 0','A') -- Door in adjacent exterior cell under the crosshair.
assert(state():find('\tA^',1,true))
for _,handler in ipairs(handlers.OnPlayerDisconnect) do handler({},1) end
messages={};h.SendClientState(1)
assert(not state():find('\tA^',1,true))
print('PASS: per-cell filtering, unseen and border doors, no client registry writes, stale request, disconnect cache reset')

-- H019a regression: an exterior UI description must map to the active grid.
cells[1]='-2, -9'
interest('Seyda Neen (-2, -9)','B')
assert(state():find('\tB^',1,true), 'region-prefixed exterior request was rejected')
assert(hasMessage('@@AMP_HOUSE@@CELL_ACK\tSeyda Neen (-2, -9)'), 'ack must echo the requested exterior identity')
interest('Seyda Neen (-1, -9)','A')
assert(#messages==0, 'a request for a different exterior grid was accepted')
print('PASS: exterior display name normalization and stale grid rejection')
