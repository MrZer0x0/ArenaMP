package.path='server/scripts/?.lua;server/lib/lua/?.lua;'..package.path
require('utils');class=require('classy');enumerations=require('enumerations');tableHelper=require('tableHelper')
local json=require('dkjson')
local checks=0
local function check(v,label) assert(v,label);checks=checks+1 end
local function fixture(name)
 local path=os.getenv(name);if not path then return end
 local file=assert(io.open(path,'rb'));local raw=file:read('*a');file:close()
 local value=assert(json.decode(raw));tableHelper.fixNumericalKeys(value);return value,raw,path
end
local data,cellRaw,cellPath=fixture('ARENA_Y100_FIXTURE')
local db,dbRaw,dbPath=fixture('ARENA_Y105_VITRINE_FIXTURE')
local name='Ebonheart, Manor Darkwoll - home'
config={cellPacketTypes={'actorList','ai','cellChangeFrom','cellChangeTo','container','death','delete','doorState','equipment','lock','place','position','scale','spawn','state','statsDynamic','trap','rotate','clientScriptLocal','spellsActive','move','miscellaneous'}}
package.loaded.config=config
package.loaded.contentFixer={FixCell=function() end}
package.loaded.questIndexStore={IsQuestItem=function() return false end}
package.loaded.questItemPhasing={GetVisibleContainerCount=function(_,_,_,item) return item.count end}
package.loaded.actorLootRepair={Repair=function() return 0 end}
package.loaded.arenaFurnitureAutoCatalog={}
customEventHooks={registerHandler=function() end,registerValidator=function() end}
local packet,deletedPackets,placePackets={}, {},{}
local refNum,mpNum,flag,attached,packetCell
tes3mp=setmetatable({
 ClearObjectList=function() packet={} end,
 SetObjectListPid=function(pid) attached=pid end,SetObjectListCell=function(cell) packetCell=cell end,
 SetObjectRefNum=function(v) refNum=tostring(v) end,SetObjectMpNum=function(v) mpNum=tostring(v) end,
 SetObjectDroppedByPlayerState=function(v) flag=v end,
 AddObject=function() packet[#packet+1]={index=refNum..'-'..mpNum,dropped=flag} end,
 SendObjectDelete=function(broadcast,skip) deletedPackets[#deletedPackets+1]={items=packet,broadcast=broadcast,skip=skip,pid=attached,cell=packetCell} end,
 SendObjectPlace=function(broadcast) placePackets[#placePackets+1]={items=packet,broadcast=broadcast} end,
 GetEquipmentSize=function() return 19 end,GetMillisecondsSinceServerStart=function() return 0 end,
},{__index=function() return function() end end})
logicHandler={GetChatName=tostring,IsGeneratedRecord=function() return false end}
Players={[1]={accountName='reader',IsLoggedIn=function() return true end}}
packetBuilder=require('packetBuilder')
local BaseCell=dofile('server/scripts/cell/base.lua')
local room=BaseCell(name)
if data then room.data=data end
local items=db and db.houseItems or {}
if not db then
 for i=1,1977 do items[name..'\n0-'..i]={cell=name,index='0-'..i,refId='old_book',consumed=true} end
 for i=1,886 do
  local id='0-'..(10000+i)
  room.data.objectData[id]={refId='book',count=1,droppedByPlayer=true,location={posX=0,posY=0,posZ=0,rotX=0,rotY=0,rotZ=0}}
  room.data.packets.place[i]=id
 end
end
LoadedCells={[name]=room}
playerVitrines={HouseItems=function() return items end}
local saves=0;room.Save=function() saves=saves+1 end
local before=json.encode(room.data);local beforeItems=json.encode(items)
local oldCalls=0
local legacyPath=os.getenv('ARENA_Y105_LEGACY_MARKET')
if legacyPath then
 local f=assert(io.open(legacyPath,'rb'));local code=f:read('*a');f:close()
 local deleted=assert(code:match('(local function deleted.-\nend)'))
 local consume=assert(code:match('(local function consumeWorld.-\nend)'))
 local legacy=assert(load(deleted..'\n'..consume..'\nreturn consumeWorld'))()
 logicHandler.DeleteObject=function() oldCalls=oldCalls+1 end
 for _,meta in pairs(items) do if meta.cell==name and meta.consumed then legacy(1,meta) end end
 check(oldCalls==1977 and saves==1977,'old real handler reproduces 1977 deletes and 1977 full cell saves')
 saves=0
end
local market=dofile('server/scripts/houseItemMarket.lua')
local count,changed=market.RestoreConsumed(1,name)
check(count==1977 and not changed and saves==0,'existing 1977 tombstones cause no cell writes')
check(#deletedPackets==16,'1977 deletions grouped in 16 packets')
local seen={}
for _,batch in ipairs(deletedPackets) do
 check(#batch.items<=128 and not batch.broadcast and batch.skip==false and batch.pid==1 and batch.cell==name,'replay targets joining reader only, bounded packet')
 for _,entry in ipairs(batch.items) do check(not seen[entry.index],'no repeated deletion');seen[entry.index]=true end
end
check(json.encode(room.data)==before and json.encode(items)==beforeItems,'replay changes neither saved cell nor market metadata')
deletedPackets={};market.RestoreConsumed(1,name)
check(saves==0 and #deletedPackets==16,'repeat replay still performs no writes')
-- The actual full snapshot must suppress only transient drop effects.
placePackets={};room:LoadInitialCellData(1)
local placements=0
for _,batch in ipairs(placePackets) do
 check(#batch.items<=128,'snapshot placement batch bounded')
 for _,entry in ipairs(batch.items) do check(entry.dropped==false,'snapshot cannot trigger drop sound or OnPCDrop');placements=placements+1 end
end
check(placements==886,'all real library placements retained')
check(json.encode(room.data)==before,'historic droppedByPlayer provenance preserved')
local liveIndex
for _,index in pairs(room.data.packets.place) do if room.data.objectData[index].droppedByPlayer then liveIndex=index;break end end
check(liveIndex~=nil,'fixture includes player-dropped items')
placePackets={};room:LoadObjectsByPacketType('ObjectPlace',1,room.data.objectData,{liveIndex},true)
check(#placePackets==1 and placePackets[1].items[1].dropped==true,'fresh gameplay drop keeps sound/script flag')
-- Repair genuinely stale live/static state once, without weakening tombstones.
local repair=BaseCell('repair');LoadedCells.repair=repair
repair.data.objectData['0-1']={refId='old_book'}
repair.data.objectData['0-2']={refId='keep_book'}
repair.data.packets.place={'0-1','0-2'}
repair.data.packets.delete={'123-0'}
items={a={cell='repair',index='0-1',refId='old_book',consumed=true},
 b={cell='repair',index='456-0',refId='static_book',consumed=true},
 c={cell='repair',index='123-0',refId='already_deleted',consumed=true},
 duplicate={cell='repair',index='0-1',refId='old_book',consumed=true},
 keep={cell='repair',index='0-2',refId='keep_book',consumed=false}}
local repairSaves=0;repair.Save=function() repairSaves=repairSaves+1 end
deletedPackets={};count,changed=market.RestoreConsumed(1,'repair')
check(count==3 and changed and repairSaves==1,'multiple real repairs saved once, duplicate metadata deduplicated')
check(repair.data.objectData['0-1']==nil and repair.data.objectData['0-2']~=nil,'consumed live object removed; unconsumed book retained')
check(#repair.data.packets.delete==2 and tableHelper.containsValue(repair.data.packets.delete,'456-0'),'static deletion persists once')
check(#deletedPackets==2 and deletedPackets[1].broadcast==false and deletedPackets[2].broadcast==true,'only actual repairs broadcast to other observers')
market.RestoreConsumed(1,'repair');check(repairSaves==1,'repaired cell not saved again')
check(market.RestoreConsumed(1,'missing')==0,'missing cell ignored')
for _,pair in ipairs({{cellPath,cellRaw},{dbPath,dbRaw}}) do
 if pair[1] then local f=assert(io.open(pair[1],'rb'));check(f:read('*a')==pair[2],'uploaded fixture bytes unchanged');f:close() end
end
print('PASS Y105: '..checks..' assertions; 1977 tombstones -> 16 packets, 0 writes; 886 silent snapshot placements (native/network mocked)')
