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
enumerations={log={INFO=1,WARN=2,ERROR=3}, inventory={ADD=1,REMOVE=2},recordType={CELL=1},containerSub={REPLY_TO_REQUEST=4},container={SET=0}}
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
local house=h.Register('Balmora, Test Home','house',100)
assert(h.Buy(1,house.cell))
local instance=h.GetInstanceDescription(house.cell,house.owner)
assert(h.IsPurgePending(instance))
local chest={refId='chest_a',inventory={{refId='gold_001',count=1000}}}
local room={description=instance,data={objectData={['2-0']=chest},packets={container={'2-0'}}},ready=false}
function room:HasFullContainerData() return self.ready end
LoadedCells={[instance]=room}
-- An ACK before the full initial container snapshot must not finish preparation.
assert(not h.AcknowledgePurge(instance) and h.IsPurgePending(instance))
assert(not h.PreparePurchasedContainers(room,99,enumerations.containerSub.REPLY_TO_REQUEST))
assert(#chest.inventory==1)
assert(h.PreparePurchasedContainers(room,enumerations.container.SET,enumerations.containerSub.REPLY_TO_REQUEST))
assert(#chest.inventory==0)
assert(h.IsPurgePending(instance))
room.Save=function() end
room.ready=true
assert(h.AcknowledgePurge(instance,1,0))
assert(not h.IsPurgePending(instance))
chest.inventory={{refId='book_favorite',count=1}}
assert(not h.PreparePurchasedContainers(room,enumerations.container.SET,enumerations.containerSub.REPLY_TO_REQUEST))
assert(#chest.inventory==1,'later owner belongings may not be wiped')
-- An original (non-instanced) cell is explicitly not cleared by the private sweep.
local public={description=house.cell,data={objectData={['2-0']={inventory={{refId='gold_001',count=30}}}},packets={container={'2-0'}}}}
assert(not h.PreparePurchasedContainers(public,enumerations.container.SET,enumerations.containerSub.REPLY_TO_REQUEST))
assert(#public.data.objectData['2-0'].inventory==1)
print('PASS H001: first-only private container cleaning, split ACK/snapshot, original room untouched')
