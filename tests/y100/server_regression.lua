package.path='server/scripts/?.lua;server/lib/lua/?.lua;'..package.path
require('utils');class=require('classy');enumerations=require('enumerations');tableHelper=require('tableHelper')
local checks=0
local function check(v,label) assert(v,label);checks=checks+1 end
config={cellPacketTypes={'actorList','ai','cellChangeFrom','cellChangeTo','container','death','delete','doorState','equipment','lock','place','position','scale','spawn','state','statsDynamic','trap','rotate','clientScriptLocal','spellsActive','move','miscellaneous'}}
package.loaded.config=config
package.loaded.contentFixer={FixCell=function() end}
package.loaded.questIndexStore={IsQuestItem=function() return false end}
package.loaded.questItemPhasing={GetVisibleContainerCount=function(_,_,_,item) return item.count end}
package.loaded.actorLootRepair={Repair=function() return 0 end}
local logs,packets,current,sentIds={}, {},0,{}
local id='';local refNum,mpNum
local writes=0
local now=0
logicHandler={GetChatName=tostring,IsGeneratedRecord=function() return false end}
tes3mp=setmetatable({
 LogMessage=function(_,s) logs[#logs+1]=s end,LogAppend=function(_,s) logs[#logs+1]=s end,
 ClearObjectList=function() current=0;sentIds={} end,
 SetObjectRefNum=function(v) refNum=tostring(v) end,SetObjectMpNum=function(v) mpNum=tostring(v) end,
 SetObjectRefId=function(v) id=v end,
 AddObject=function() current=current+1;sentIds[#sentIds+1]={uniqueIndex=refNum..'-'..mpNum,refId=id} end,
 SendObjectPlace=function(broadcast) packets[#packets+1]={size=current,broadcast=broadcast,ids=sentIds} end,
 GetEquipmentSize=function() return 19 end,
 GetMillisecondsSinceServerStart=function() return now end,
}, {__index=function() return function() end end})
packetBuilder=require('packetBuilder')
local BaseCell=dofile('server/scripts/cell/base.lua')
Players={[1]={accountName='test',data={timestamps={lastLogin=20}},IsLoggedIn=function() return true end,
 AddCellLoaded=function() end,RemoveCellLoaded=function() end}}
local cell=BaseCell('Large Library - home');LoadedCells={[cell.description]=cell}
cell.data.loadState.hasFullActorList=true;cell.data.loadState.hasFullContainerData=true
cell.QuicksaveToDrive=function() writes=writes+1 end
for i=1,886 do
 local key='0-'..i
 cell.data.objectData[key]={refId='book_'..i,location={posX=i,posY=0,posZ=1,rotX=0,rotY=0,rotZ=0},count=1}
 cell.data.packets.place[i]=key
end
local json=require('dkjson');local original=json.encode(cell.data)
cell:LoadObjectsPlaced(1,cell.data.objectData,cell.data.packets.place,true)
check(#packets==7,'886 placements split into 7 packets')
local all={};local total=0
for _,p in ipairs(packets) do
 check(p.size<=128 and p.broadcast==true,'every batch respects size and broadcast semantics')
 for _,item in ipairs(p.ids) do check(not all[item.uniqueIndex],'no duplicate placement in replay');all[item.uniqueIndex]=true;total=total+1 end
end
check(total==886 and json.encode(cell.data)==original,'all books retained; loading changes no saved cell data')
cell.data.objectData['0-bad']={refId='bad',location={posX=0}}
cell.data.packets.place[#cell.data.packets.place+1]='0-bad'
cell:LoadObjectsPlaced(1,cell.data.objectData,{'0-bad'})
check(cell.data.objectData['0-bad']~=nil and cell.data.packets.place[#cell.data.packets.place]=='0-bad','invalid transform skipped without deleting data')
cell.data.objectData['0-bad']=nil;cell.data.packets.place[#cell.data.packets.place]=nil
-- Initial delivery marker follows the actual visit rather than login timestamps.
cell:LoadInitialCellData(1)
check(cell.initialDataSentTo[1]==Players[1],'successful initial replay records session identity outside cell.data')
cell.visitors={1};cell:RemoveVisitor(1)
check(cell.initialDataSentTo[1]==nil,'leaving clears initial replay marker')
cell.data.lastVisit.test=nil;cell:LoadInitialCellData(1)
local oldPlayer=Players[1];Players[1]={accountName='new',IsLoggedIn=function() return true end}
check(cell.initialDataSentTo[1]~=Players[1],'reused pid does not inherit prior session delivery')
Players[1]=oldPlayer
-- Only changed authored AI incurs a save; combat heartbeats remain transient.
cell.data.objectData['1-0']={refId='follower'};writes=0;logs={}
local update={['1-0']={ai={action=enumerations.ai.FOLLOW,targetPlayer='test',shouldRepeat=false}}}
for _=1,1000 do cell:SaveActorAI(update) end
check(writes==1 and #logs==0,'1000 identical follower packets cause 1 write and no ActorAI logs')
update['1-0'].ai.targetPlayer='other';cell:SaveActorAI(update);check(writes==2,'changed AI target persists')
cell.data.objectData['1-0'].home='Old Cell';cell:SaveActorAI(update)
check(writes==3 and cell.data.objectData['1-0'].home==nil,'clearing stale home is a real change')
cell:SaveActorAI({['1-0']={ai={action=enumerations.ai.COMBAT,targetPid=1}}});check(writes==3,'combat heartbeat performs no disk save')
cell:SaveActorAI({['1-0']={ai={action=enumerations.ai.CANCEL}}});cell:SaveActorAI({['1-0']={ai={action=enumerations.ai.CANCEL}}})
check(writes==4 and cell.data.objectData['1-0'].ai==nil,'cancel saves once')
-- Execute actual public ActorAI callback without booting the game server.
local f=assert(io.open('server/scripts/serverCore.lua'));local core=f:read('*a');f:close()
local callback=assert(core:match('(function OnActorAI%(pid, cellDescription%).-\nend)'))
local calls=0;eventHandler={OnActorAI=function() calls=calls+1 end}
assert(load(callback))();logs={}
for _=1,1000 do OnActorAI(1,cell.description) end
check(calls==1000 and #logs==0,'ActorAI dispatch remains active without per-packet log')
-- Real chat path still logs ordinary messages, excluding only INTEREST protocol.
customEventHooks={triggerValidators=function() return {validDefaultHandler=false,validCustomHandlers=false} end,triggerHandlers=function() end}
f=assert(io.open('server/scripts/eventHandler.lua'));local events=f:read('*a');f:close()
local handler=assert(events:match('(eventHandler.OnPlayerSendMessage = function%(pid, message%).-\nend)'))
assert(load(handler))();logs={}
for _=1,1000 do eventHandler.OnPlayerSendMessage(1,'@@AMP_HOUSE@@INTEREST\tLarge Library - home') end
check(#logs==0,'door-interest protocol omitted from ordinary chat log')
eventHandler.OnPlayerSendMessage(1,'hello');check(#logs==1 and logs[1]:find('hello',1,true),'ordinary chat logging retained')
-- Optional copied fixture is READ ONLY; never save or include it in releases.
local fixture=os.getenv('ARENA_Y100_FIXTURE')
if fixture then
 local file=assert(io.open(fixture,'rb'));local raw=file:read('*a');file:close()
 local data=assert(json.decode(raw));tableHelper.fixNumericalKeys(data)
 local real=BaseCell('Ebonheart, Manor Darkwoll - home');real.data=data
 real.QuicksaveToDrive=function() error('fixture must never be saved') end
 packets={};local began=os.clock();real:LoadInitialCellData(1)
 local n=0;for _,packet in ipairs(packets) do check(packet.size<=128,'fixture bounded batch');n=n+packet.size end
 check(n==886,'all 886 uploaded library placements replayed')
 file=assert(io.open(fixture,'rb'));check(file:read('*a')==raw,'fixture file bytes unchanged');file:close()
 print(string.format('PASS read-only uploaded library replay: %d placements, %d packets, %.2f ms (native/network mocked)',n,#packets,(os.clock()-began)*1000))
end
print('PASS Y100 cell/AI/chat: '..checks..' assertions')
