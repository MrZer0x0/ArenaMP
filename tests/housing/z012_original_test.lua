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



local home=h.Register('Mod Empty Interior','house',100)
assert(h.SetMode(home.cell,'original'));assert(h.Buy(1,home.cell))
assert(h.GetInstanceDescription(home.cell,'Alice')==home.cell)
assert(next(RecordStores.cell.data.permanentRecords)==nil)
assert(not h.MarkForPurge(home.cell) and not h.ResetInstance(home.cell))
assert(not h.SetMode(home.cell,'instance'))
assert(h.LinkCell(home.cell,'Mod Basement'))
assert(h.GetCellData('Mod Basement').house==home.cell)
assert(h.CanTake(1,home.cell));home.locked=true;assert(not h.CanTake(2,home.cell))
home.friends={'Bob'};assert(not h.CanTake(2,home.cell))
cells[2]='0, 0';h.HandleDoorRequest(2,home.cell,false,{posX=1,posY=2,posZ=3},true)
assert(cells[2]==home.cell);assert(not hasMessage('@@AMP_HOUSE@@PURGE'))
assert(next(RecordStores.cell.data.permanentRecords)==nil)
cells[3]='0, 0';h.HandleDoorRequest(3,home.cell,false,{posX=1,posY=2,posZ=3},false)
assert(cells[3]=='0, 0');assert(hasMessage('@@AMP_HOUSE@@CANCEL'))
assert(validators.OnPlayerCellChange[1]({},3,{location={cell=home.cell}}).validDefaultHandler==false)
assert(validators.OnObjectDelete[1]({},2,home.cell).validDefaultHandler==false)
assert(validators.OnContainer[1]({},2,home.cell).validDefaultHandler==false)
home.coOwners={'Bob'};assert(h.CanTake(2,home.cell))
assert(h.Release(home.cell,'test'));assert(#resets==0)
assert(h.SetMode(home.cell,'instance'));assert(h.GetInstanceDescription(home.cell,'Alice')~=home.cell)
-- Preserve uploaded fix: no repeated CELL record in one session, reconnect resends.
local sends=0;tes3mp.SendRecordDynamic=function() sends=sends+1 end
local inst=h.GetInstanceDescription(home.cell,'Alice')
h.EnsureInstanceRecord(1,inst,home.cell);h.EnsureInstanceRecord(1,inst,home.cell);assert(sends==1)
h.ForgetInstanceRecords(1);h.EnsureInstanceRecord(1,inst,home.cell);assert(sends==2)
local refreshes=0;cells[1]=inst;Players[1].initTimestamp=10
LoadedCells={[inst]={data={lastVisit={Alice=20}},LoadInitialCellData=function() refreshes=refreshes+1 end}}
h.RefreshInstanceCell(1,inst);assert(refreshes==1)
LoadedCells[inst].data.lastVisit.Alice=1;h.RefreshInstanceCell(1,inst);assert(refreshes==1)
print('PASS Z012: original mode, no copies/purge/reset, access, linked rooms, mode migration guard, user record/revisit fixes')

package.loaded.houseHelper=h;package.loaded.mwScriptHelper={}
customCommandHooks.setRankRequirement=function() end
for _,p in pairs(Players) do p.IsServerOwner=function() return false end end
local admin=dofile(root..'adminInterface.lua')
commands.admin(4,{'admin','houses'});gui(4,admin.ID.houses,6);gui(4,admin.ID.houseInput,'New Original House')
assert(h.GetProperty('New Original House').mode=='original')
cells[4]='Another Empty House';commands.admin(4,{'admin','houses'});gui(4,admin.ID.houses,5)
assert(h.GetProperty('Another Empty House').mode=='original')
gui(4,admin.ID.houseDetail,8);assert(h.GetProperty('Another Empty House').mode=='instance')
print('PASS Z012: admin original-cell registration by name/current cell and mode switch')
