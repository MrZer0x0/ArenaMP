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

local copy=tableHelper.deepCopy
local disk,fail={},nil
local registry="custom/arenaHouses.json"
package.loaded.jsonInterface={
 load=function(path) return copy(disk[path]) end,
 saveAtomic=function(path,data)
  if fail=="journal" and path==registry and data.pendingPayment then return false end
  if fail=="profile" and path:find("^player/") then return false end
  if fail=="finalize" and path==registry and not data.pendingPayment then return false end
  disk[path]=copy(data);return true
 end
}
tes3mp.StopServer=function() error("STOP") end
for pid,p in pairs(Players) do
 p.accountFile=p.accountName..".json"
 p.LoadInventory=function() end
 disk["player/"..p.accountFile]=copy(p.data)
end
local function reload()
 return dofile(root.."houseHelper.lua")
end
local h=reload()
local home=h.Register("Audit House","house",100)
local before=Players[1].data.inventory[1].count
fail="journal"
assert(not h.Buy(1,home.cell))
assert(not home.owner and Players[1].data.inventory[1].count==before)
fail=nil
assert(h.Buy(1,home.cell))
assert(disk[registry].pendingPayment==nil and disk[registry].properties[home.cell].owner=="Alice")
assert(disk["player/Alice.json"].inventory[1].count==before-100)
fail="journal"
local debit=Players[1].data.inventory[1].count
assert(not h.Sell(1,home.cell))
assert(home.owner=="Alice" and Players[1].data.inventory[1].count==debit)
fail=nil
assert(h.Sell(1,home.cell))
assert(not disk[registry].properties[home.cell].owner)
assert(disk["player/Alice.json"].inventory[1].count==Players[1].data.inventory[1].count)
-- Failure between the registry and profile writes is replayed at next startup.
for _,stage in ipairs({"profile","finalize"}) do
 local name="Audit "..stage
 h.Register(name,"house",200)
 fail=stage
 local ok=pcall(h.Buy,2,name)
 assert(not ok and disk[registry].pendingPayment)
 local expected=copy(disk[registry].pendingPayment.profile)
 fail=nil;h=reload()
 assert(h.GetProperty(name).owner=="Bob")
 assert(disk[registry].pendingPayment==nil)
 assert(disk["player/Bob.json"].inventory[1].count==expected.inventory[1].count)
 Players[2].data=copy(disk["player/Bob.json"])
 assert(h.Sell(2,name))
end
print("PASS housing journal: immediate debit/refund, rollback, interrupted profile/finalization recovery")
