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


local snapshots={}
for pid,p in pairs(Players) do
 p.QuicksaveToDrive=function(self)
  snapshots[pid]={housingEntryMode=(self.data.customVariables or {}).housingEntryMode}
 end
end
local home=h.Register('Town, House','house',100);assert(h.Buy(1,home.cell))
home.locked=true;home.friends={'Bob'}
local venue=h.Register('Town, Shop','shop',100);assert(h.Buy(3,venue.cell))
local function request(pid,cell,hold,locked)
 cells[pid]='0, 0';dialogs[pid]=nil;messages={}
 h.HandleDoorRequest(pid,cell,hold or false,{posX=1,posY=2,posZ=3},locked or false)
end
local function preference(pid,cell,index)
 request(pid,cell,true)
 local _,separators=dialogs[pid][3]:gsub(';','')
 gui(pid,9060,separators-1)
 assert(dialogs[pid][3]:find('Назад',1,true) or dialogs[pid][3]:find('Back',1,true))
 gui(pid,9060,index)
 assert(cells[pid]=='0, 0','saving must not teleport')
end
-- The preference is per account, not per door, and survives profile reload.
preference(2,home.cell,0);assert(snapshots[2].housingEntryMode=='instance')
Players[2].data.customVariables=snapshots[2]
request(2,home.cell);assert(cells[2]==h.GetInstanceDescription(home.cell,home.owner))
request(2,venue.cell);assert(cells[2]==h.GetInstanceDescription(venue.cell,venue.owner))
assert(Players[1].data.customVariables==nil)
-- No access / vacant destinations cannot create an instance or bypass rights.
home.friends={};request(2,home.cell);assert(cells[2]=='0, 0' and hasMessage('@@AMP_HOUSE@@GO'))
local free=h.Register('Empty House','house',100);request(2,free.cell);assert(not dialogs[2] and cells[2]=='0, 0')
home.friends={'Bob'}
-- Explicit original overrides automatic ESP entry; client still enforces lock.
preference(2,home.cell,1);request(2,home.cell,false,true)
assert(cells[2]=='0, 0' and hasMessage('@@AMP_HOUSE@@GO'))
assert(snapshots[2].housingEntryMode=='original')
-- Explicit menu, including an ESP lock: no automatic teleport.
preference(2,home.cell,2);request(2,home.cell,false,true)
assert(dialogs[2] and dialogs[2][1]==9060 and cells[2]=='0, 0')
gui(2,9060,1);assert(cells[2]==h.GetInstanceDescription(home.cell,home.owner))
request(2,venue.cell);assert(dialogs[2] and cells[2]=='0, 0')
-- Guests can always reach settings using hold, without owning any property.
preference(4,venue.cell,0);request(4,venue.cell)
assert(cells[4]==h.GetInstanceDescription(venue.cell,venue.owner))
request(4,venue.cell,true);assert(dialogs[4] and cells[4]=='0, 0')
-- Save errors roll back the preference; another player's mode is untouched.
Players[4].QuicksaveToDrive=function() error('disk unavailable') end
preference(4,venue.cell,1);assert(Players[4].data.customVariables.housingEntryMode=='instance')
assert(Players[2].data.customVariables.housingEntryMode=='menu')
-- Cancel settings does not save or move. Invalid values use the default route.
request(2,home.cell,true);local _,n=dialogs[2][3]:gsub(';','');gui(2,9060,n-1);gui(2,9060,4)
assert(Players[2].data.customVariables.housingEntryMode=='menu' and cells[2]=='0, 0')
Players[2].data.customVariables.housingEntryMode='bad';request(2,home.cell)
assert(hasMessage('@@AMP_HOUSE@@GO') and not dialogs[2])
-- Linked rooms stay inside the same instance regardless of preference.
assert(h.LinkCell(home.cell,'Town, Basement'));Players[1].data.customVariables={housingEntryMode='original'}
cells[1]=h.GetInstanceDescription(home.cell,home.owner)
h.HandleDoorRequest(1,'Town, Basement',false,{posX=1,posY=2,posZ=3},false)
assert(cells[1]==h.GetInstanceDescription('Town, Basement',home.owner))
print('PASS Z011: profile save/reload, all cells, per-account isolation, rights, original/menu/instance, ESP precedence, hold settings, failure rollback, cancellation, linked rooms')
