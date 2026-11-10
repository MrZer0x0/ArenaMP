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
local a=h.Register('Town, First House','house',100)
local b=h.Register('Town, Second House','house',200)
local c=h.Register('Town, Tavern','tavern',300)
assert(h.LinkCell(a.cell,'Town, First House, Basement'))
assert(h.GetProperty('town, first house, basement')==a and #a.cells==2)
assert(h.Buy(1,a.cell))
local gold=Players[1].data.inventory[1].count
assert(not h.Buy(1,b.cell));assert(not h.Buy(1,c.cell));assert(Players[1].data.inventory[1].count==gold)
assert(h.Buy(3,b.cell));assert(not h.Gift(1,a,3));assert(a.owner=='Alice')
assert(not h.LinkCell(a.cell,b.cell));assert(h.GetProperty(b.cell)==b)
local first=h.GetInstanceDescription(a.cell,a.owner)
local basement=h.GetInstanceDescription(a.cells[2],a.owner)
assert(h.CanTake(1,first));assert(not h.CanTake(2,first))
a.coOwners={'Bob'};assert(h.CanTake(2,first));a.coOwners={};a.friends={'Bob'}
assert(h.GetAccessLevel(2,a)=='friend' and not h.CanTake(2,first))
a.allowTake=true;assert(h.CanTake(2,first));a.allowTake=false
h.ShowHomeMenu(1);gui(1,29310,0);gui(1,29311,'Северный дом');assert(a.title=='Северный дом')
gui(1,29310,2);gui(1,29311,'BOB');assert(h.CanTake(2,first))
gui(1,29310,3);gui(1,29311,'bob');assert(not h.CanTake(2,first))
gui(1,29310,7);assert(a.kind=='tavern');gui(1,29310,10);assert(a.allowTake)
gui(1,29310,10);assert(not a.allowTake)
-- Menu waits do not implicitly enter the origin, and inter-room doors stay in the instance.
cells[1]=first
h.HandleDoorRequest(1,a.cells[2],false,{posX=1,posY=2,posZ=3})
assert(cells[1]==basement and hasMessage('WAIT') and hasMessage('CANCEL'))
h.MarkPurgeDone(basement);messages={}
h.EnterInstance(1,{owner=a.owner,baseCell=a.cells[2],cell=basement},{posX=1,posY=2,posZ=3})
assert(not hasMessage('@@AMP_HOUSE@@PURGE'))
-- Stale menu after access revocation must not teleport.
cells[2]='0, 0';a.friends={}
h.EnterInstance(2,{owner=a.owner,baseCell=a.cell,cell=first},{posX=1,posY=2,posZ=3})
assert(cells[2]=='0, 0')
-- Gift uses stable instance identity and changes permissions, never resets contents.
local n=#resets
assert(h.Gift(1,a,2));assert(a.owner=='Bob' and #resets==n)
assert(h.GetInstanceDescription(a.cell,'Bob')==first and h.GetInstanceDescription(a.cells[2],'Bob')==basement)
assert(h.SetOwner(a.cell,'Bob') and h.GetInstanceDescription(a.cell,'Bob')==first)
assert(h.CanTake(2,first) and not h.CanTake(1,first));assert(cells[1]==a.cells[2])
assert(#h.ListOwnedBy('Alice')==0 and #h.ListOwnedBy('Bob')==1)
assert(not h.SetOwner(b.cell,'Bob'));assert(#resets==n and b.owner=='Carol')
assert(hasMessage('Таверна (Bob)'))
-- Forbidden object/container mutations are rejected; normal world is unaffected.
assert(validators.OnObjectDelete[1]({},1,first,{["0-1"]={refId="book_test"}}).validDefaultHandler==false)
assert(validators.OnContainer[1]({},1,first).validDefaultHandler==false)
assert(validators.OnObjectDelete[1]({},1,'0, 0')==nil)
messages={}
for _,f in ipairs(handlers.OnPlayerCellChange) do f({},4,{location={cell=c.cell}},'0, 0') end
assert(not hasMessage('Этот дом продаётся. Цена: 300'),'vacant doors use hold menus, not unsolicited popups')
-- All rooms released together after gifting; legacy ownership migrates without re-keying cells.
assert(h.Release(a.cell,'test'));assert(#resets==n+2 and a.instanceOwner==nil)
assert(h.GetProperty(a.cells[2])==a)
local legacy=h.Register('Legacy House','house',100);legacy.cells=nil;legacy.title=nil
assert(h.GetProperty(legacy.cell).cells[1]==legacy.cell)
print('PASS: ownership, grouped rooms, gift identity, menu, permissions, stale access, purge, sale notice, legacy migration')

local reloaded=dofile(root.."houseHelper.lua")
assert(reloaded.GetProperty(b.cell).owner=="Carol")
print("PASS: registry reload")
