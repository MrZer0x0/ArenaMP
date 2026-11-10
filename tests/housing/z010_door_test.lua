local root = 'server/scripts/'
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
tableHelper = {containsValue=function(t,v) for _,x in pairs(t) do if x==v then return true end end return false end, cleanNils=function() end, concatenateFromIndex=function(t,i) return table.concat(t,' ',i) end}
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

local home=h.Register('Town, House','house',100)
assert(h.Buy(1,home.cell));home.locked=true;home.friends={'Bob'};home.coOwners={'Carol'}
assert(h.LinkCell(home.cell,'Town, House, Basement'))
local function request(pid,locked,hold)
 cells[pid]='0, 0';messages={};dialogs[pid]=nil
 h.HandleDoorRequest(pid,home.cell,hold or false,{posX=1,posY=2,posZ=3},locked)
end
for _,pid in ipairs({1,2,3}) do
 request(pid,false);assert(not dialogs[pid] and hasMessage('@@AMP_HOUSE@@GO'))
 request(pid,false,true)
 assert(dialogs[pid] and dialogs[pid][1]==9060, 'eligible visitor must choose')
 assert(cells[pid]=='0, 0', 'menu must not teleport')
 gui(pid,9060,1)
 assert(cells[pid]==h.GetInstanceDescription(home.cell,home.owner))
 request(pid,true)
 assert(not dialogs[pid], 'ESP lock: trusted player enters without menu')
 assert(cells[pid]==h.GetInstanceDescription(home.cell,home.owner))
end
home.locked=false;request(4,false);assert(not dialogs[4]);home.locked=true;assert(hasMessage('@@AMP_HOUSE@@GO'))
request(4,true);assert(cells[4]=='0, 0' and not dialogs[4])
-- Every kind: guest tap enters the original; hold enters the private venue
-- via the hold menu (Z011). Trusted visitors retain automatic default ESP entry.
for _,kind in ipairs({'house','tavern','shop','guild'}) do
 home.kind=kind;home.locked=(kind=='shop' or kind=='guild')
 request(4,false);assert(not dialogs[4] and hasMessage('@@AMP_HOUSE@@GO'),kind)
 request(4,false,true);assert(dialogs[4]);gui(4,9060,1);assert(cells[4]==h.GetInstanceDescription(home.cell,home.owner))
 request(4,true,true);assert(dialogs[4]);gui(4,9060,1);assert(cells[4]==h.GetInstanceDescription(home.cell,home.owner))
 for _,pid in ipairs({1,2,3}) do
  request(pid,false);assert(not dialogs[pid] and hasMessage('@@AMP_HOUSE@@GO'))
  request(pid,false,true);assert(dialogs[pid] and dialogs[pid][1]==9060)
  request(pid,true);assert(not dialogs[pid]);assert(cells[pid]==h.GetInstanceDescription(home.cell,home.owner))
 end
end
-- Revoking permission while menu is open cannot be bypassed by a stale click.
home.kind='house';home.locked=true;request(2,false,true);home.friends={}
gui(2,9060,1);assert(cells[2]=='0, 0')
-- Multi-room instance transitions remain automatic even when the base door has a lock.
cells[1]=h.GetInstanceDescription(home.cell,home.owner);dialogs[1]=nil
h.HandleDoorRequest(1,home.cells[2],false,{posX=1,posY=2,posZ=3},true)
assert(not dialogs[1]);assert(cells[1]==h.GetInstanceDescription(home.cells[2],home.owner))
-- Default names are marked for CEL localization, player-authored titles are not.
cells[1]=home.cell;home.title=home.cell;messages={};h.SendClientState(1)
assert(hasMessage('^celltitle^1^instance'))
home.title='Дом у реки';messages={};h.SendClientState(1)
assert(hasMessage('^customtitle^1^instance'))
-- Unowned properties keep the Z009 tap/hold distinction and no auto-popup.
local free=h.Register('Other House','house',100);dialogs[4]=nil
h.HandleDoorRequest(4,free.cell,false,{posX=1,posY=2,posZ=3},false);assert(not dialogs[4])
h.HandleDoorRequest(4,free.cell,true,{posX=1,posY=2,posZ=3},false);assert(dialogs[4][1]==9060)
print('PASS Z010: tap choice, ESP-lock trusted entry, public shop/guild, stale access, linked rooms, title metadata, vacant doors')
