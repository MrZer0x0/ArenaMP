package.path = 'server/scripts/?.lua;server/lib/lua/?.lua;' .. package.path
require('utils')
class = require('classy')
enumerations = require('enumerations')
local checks = 0
local function check(value, message) assert(value, message); checks = checks + 1 end
local function copy(t) if type(t) ~= 'table' then return t end local r = {}; for k,v in pairs(t) do r[k]=copy(v) end return r end
local function item(id, count, charge) return {refId=id,count=count or 1,charge=charge or -1,enchantmentCharge=-1,soul='',poisonId='',poisonCharges=0} end
local packet, sends, requested, writes, currentPid
sends = {}
config = {actorEquipmentSaveInterval=5,cellPacketTypes={'container','equipment'}}
package.loaded.config = config
package.loaded.contentFixer = {}
package.loaded.questIndexStore = {IsQuestItem=function() return false end}
package.loaded.questItemPhasing = {
    StampInventory=function() end, GetContainerPhaseCount=function() return 0 end,
    GetVisibleContainerCount=function(_,_,_,v) return v.count end
}
package.loaded.actorLootRepair = {Repair=function() return 0 end}
local relevant = {7,8,9}
local builder = {
    GetRelevantPlayersForCell=function() return relevant end,
    UpdateContainerForRelevantPlayers=function(c, id) sends[#sends+1]={kind='SET',id=id,inventory=copy(c.data.objectData[id].inventory)} end,
    AddObjectMove=function() end, AddObjectRotate=function() end
}
package.loaded.packetBuilder=builder
logicHandler={GetChatName=tostring,IsGeneratedRecord=function() return false end}
local function object(i) return packet.objects[i+1] end
local function entry(i,j) return object(i).items[j+1] end
tes3mp = setmetatable({
    GetObjectListOrigin=function() return packet.origin end,
    GetObjectListClientScript=function() return 'test_global_script' end,
    GetObjectListAction=function() return packet.action end,
    GetObjectListContainerSubAction=function() return packet.sub end,
    GetObjectListSize=function() return #packet.objects end,
    GetObjectRefNum=function(i) return object(i).refNum end,
    GetObjectMpNum=function() return 0 end,
    GetObjectRefId=function(i) return object(i).refId end,
    GetContainerChangesSize=function(i) return #object(i).items end,
    GetContainerItemRefId=function(i,j) return entry(i,j).refId end,
    GetContainerItemCount=function(i,j) return entry(i,j).count end,
    GetContainerItemCharge=function(i,j) return entry(i,j).charge end,
    GetContainerItemEnchantmentCharge=function(i,j) return entry(i,j).enchantmentCharge end,
    GetContainerItemSoul=function(i,j) return entry(i,j).soul end,
    GetContainerItemPoisonId=function(i,j) return entry(i,j).poisonId end,
    GetContainerItemPoisonCharges=function(i,j) return entry(i,j).poisonCharges end,
    GetContainerItemActionCount=function(i,j) return entry(i,j).count end,
    SetObjectListPid=function(pid) currentPid=pid end,
    SendObjectMove=function() sends[#sends+1]={kind='MOVE',pid=currentPid} end,
    SendObjectRotate=function() sends[#sends+1]={kind='ROTATE',pid=currentPid} end,
    SendContainer=function() sends[#sends+1]={kind='ACK',pid=currentPid} end,
    GetEquipmentSize=function() return 19 end,
}, {__index=function() return function() end end})
local BaseCell = dofile('server/scripts/cell/base.lua')
local cell = BaseCell("Seyda Neen, Arrille's Tradehouse")
cell.visitors={7,8,9}
cell.QuicksaveToDrive=function() writes=(writes or 0)+1 end
cell.RequestContainers=function(_,pid,ids) requested={pid=pid,ids=ids} end
local function send(action, items, sub, origin)
    packet={action=action,sub=sub or enumerations.containerSub.NONE,
        origin=origin or enumerations.packetOrigin.CLIENT_SCRIPT_GLOBAL,
        objects={{refNum=331473,refId='arrille',items=items}}}
    cell:SaveContainers(7)
end
-- Reproduce log: global ADD before a loaded cell/baseline. No partial SET,
-- equipment pruning or disk write may occur; a full request is made instead.
cell.visitors={}
send(enumerations.container.ADD, {item('mod_startup_item')})
check(cell.data.objectData['331473-0']==nil, 'unknown delta must not manufacture inventory')
check(#sends==0 and not writes, 'unknown delta must not broadcast/persist a partial SET')
check(requested and requested.pid==7 and requested.ids==nil, 'inactive global script must request complete baseline')
-- Source already applied ADD: the full reply includes it once, plus all ESM gear.
cell.visitors={7,8,9}
send(enumerations.container.SET, {item('common_shirt_01'),item('common_pants_01'),item('mod_startup_item')},enumerations.containerSub.REPLY_TO_REQUEST)
local obj=cell.data.objectData['331473-0']
check(#obj.inventory==3, 'full source snapshot must preserve authored apparel')
send(enumerations.container.ADD, {item('sold_robe',1,0)})
check(#obj.inventory==4 and obj.inventory[3].count==1, 'later delta must not replay bootstrap ADD')
check(obj.inventory[4].refId=='sold_robe', 'sold robe coexists with original clothes')
-- Y098: barter -> stripped equipment echo -> save/reload -> actual equipment
-- replay. Original clothing must coexist with the sold robe across a cell exit.
obj.equipment={[8]=item('common_shirt_01',1,0),[9]=item('common_pants_01',1,0)}
cell:SaveActorEquipment({['331473-0']={equipment={}}})
check(obj.equipment[8] and obj.equipment[9], 'barter echo retains inventory-backed shirt and pants')
local json=require('dkjson')
local replay=BaseCell(cell.description)
replay.data=json.decode(json.encode(cell.data));tableHelper.fixNumericalKeys(replay.data)
local replayEquipment={}
tes3mp.EquipActorItem=function(slot,refId) replayEquipment[slot]=refId end
replay:LoadActorEquipment(7,replay.data.objectData,{'331473-0'})
check(replayEquipment[8]=='common_shirt_01' and replayEquipment[9]=='common_pants_01',
    'cell reload replays original equipped clothing after barter')
check(#replay.data.objectData['331473-0'].inventory==4, 'cell reload keeps sale and original inventory')
-- Empty SET really is authoritative, not a missing snapshot.
send(enumerations.container.SET, {})
check(#obj.inventory==0, 'empty full snapshot must remain empty')
send(enumerations.container.ADD, {item('legitimate_new_item')})
check(#obj.inventory==1 and obj.inventory[1].count==1, 'known empty inventory accepts delta')
-- Mixed packets cannot apply their known half before a missing baseline arrives.
packet={action=enumerations.container.ADD,origin=enumerations.packetOrigin.CLIENT_GAMEPLAY,
 sub=enumerations.containerSub.NONE,objects={{refNum=331473,refId='arrille',items={item('sold_robe')}},
 {refNum=999,refId='other_actor',items={item('another_item')}}}}
cell.data.loadState.hasFullContainerData=true
requested=nil
cell:SaveContainers(7)
check(#obj.inventory==1, 'mixed delta must not partially apply and later duplicate known items')
check(requested and #requested.ids==2 and requested.ids[1]=='331473-0' and requested.ids[2]=='999-0',
    'mixed packet snapshots both known and unknown containers without replaying deltas')
-- A genuinely removed outfit must not be recreated from previous equipment.
local guard=require('actorApparelGuard')
local worn={equipment={[8]=item('shirt',1,0),[11]=item('robe',1,0),[0]=item('broken_helmet',1,0)},
 inventory={item('shirt',1,0),item('robe',1,0),item('broken_helmet',1,0)}}
local incoming={}
check(guard.Repair(worn,incoming), 'charge=0 clothes must remain wearable')
check(incoming[8] and incoming[11] and not incoming[0], 'broken armor differs from zero-charge clothes')
worn.inventory={}; incoming={}
check(not guard.Repair(worn,incoming) and next(incoming)==nil, 'sold/removed apparel must not respawn')
worn.inventory={item('shirt')}; worn.deathState=1; incoming={}
check(not guard.Repair(worn,incoming), 'corpse looting must never be undone')
local rings={equipment={[12]=item('ring'),[13]=item('ring')},inventory={item('ring',2)}}
incoming={};guard.Repair(rings,incoming)
check(incoming[12] and incoming[13], 'two identical rings must retain two slots')
rings.inventory[1].count=1;incoming={};guard.Repair(rings,incoming)
check(incoming[12] and not incoming[13], 'one remaining ring must not occupy two slots')
-- Delayed echoes no longer rewind editor; observers and later cell reloads
-- still receive the transform. Server/script calls omit skipPid by default.
sends={}
local transform={['0-1']={refId='chair',location={posX=1,rotX=0}}}
cell:LoadObjectsByPacketType('ObjectMove',7,transform,{'0-1'},true,7)
cell:LoadObjectsByPacketType('ObjectRotate',7,transform,{'0-1'},true,7)
check(#sends==4, 'two observers receive both transform packets')
for _,p in ipairs(sends) do check(p.pid~=7, 'editor receives no stale transform echo') end
sends={};cell:LoadObjectsRotated(7,transform,{'0-1'},false)
check(#sends==1 and sends[1].pid==7, 'cell reload sends saved rotation to requester')
sends={};cell:LoadObjectsRotated(7,transform,{'0-1'},true)
check(#sends==3, 'explicit server/script rotation still reaches everyone')
print('PASS Y095 server regressions: '..checks..' assertions')
-- Exercise the actual event handler as well as the loaders: only gameplay
-- transforms skip their author; script/console transforms retain normal routing.
eventHandler={}
local f=assert(io.open('server/scripts/eventHandler.lua','rb'));local source=f:read('*a');f:close()
local first=assert(source:find('eventHandler.OnGenericObjectEvent = function',1,true))
local last=assert(source:find('eventHandler.OnObjectActivate = function',first,true))
assert(load(source:sub(first,last-1),'actual OnGenericObjectEvent'))()
Players={[7]={IsLoggedIn=function() return true end}}
LoadedCells={[cell.description]=cell}
logicHandler.IsPacketFromConsole=function() return false end
logicHandler.IsPacketFromClientScript=function(origin) return origin==enumerations.packetOrigin.CLIENT_SCRIPT_LOCAL end
logicHandler.DoesPacketOriginRequireLoadedCell=function() return true end
customEventHooks={triggerValidators=function() return {validDefaultHandler=true} end,triggerHandlers=function() end}
packetReader={GetObjectPacketTables=function() return {objects=copy(transform),players={}} end}
packet={origin=enumerations.packetOrigin.CLIENT_GAMEPLAY};sends={}
eventHandler.OnGenericObjectEvent(7,cell.description,'ObjectRotate')
check(#sends==2, 'gameplay event forwards only to observers')
for _,p in ipairs(sends) do check(p.pid~=7,'gameplay event does not echo to editor') end
packet.origin=enumerations.packetOrigin.CLIENT_SCRIPT_LOCAL;sends={}
eventHandler.OnGenericObjectEvent(7,cell.description,'ObjectRotate')
check(#sends==3,'script event includes its sender')
print('PASS Y095 event routing: '..checks..' total assertions')
