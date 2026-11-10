-- Execute production transition functions, including return-home observer replay.
local f=assert(io.open('server/scripts/cell/base.lua'));local source=f:read('*a');f:close()
BaseCell={}
local function method(name,nextName)
 local a=assert(source:find('function BaseCell:'..name,1,true))
 local b=assert(source:find('function BaseCell:'..nextName,a+1,true))
 assert(load(source:sub(a,b-1)))()
end
method('MoveObjectData','SaveLastVisit')
method('SaveActorCellChanges','LoadActorPackets')
tableHelper={}
function tableHelper.containsValue(t,v) for _,x in pairs(t) do if x==v then return true end end return false end
function tableHelper.removeValue(t,v) for k,x in pairs(t) do if x==v then t[k]=nil end end end
function tableHelper.insertValueIfMissing(t,v) if not tableHelper.containsValue(t,v) then table.insert(t,v) end end
config={rememberActorHomes=false}
enumerations={log={INFO=1,WARN=2,ERROR=3},ai={TRAVEL=1}}
function isTransitionalExteriorCell() return false end
local dest='B';local replay=0
local noop=function() end
tes3mp=setmetatable({GetActorListSize=function() return 1 end,GetActorRefNum=function() return 42 end,GetActorMpNum=function() return 0 end,GetActorCell=function() return dest end,GetActorPosX=function() return 1 end,GetActorPosY=function() return 2 end,GetActorPosZ=function() return 3 end,GetActorRotX=function() return 0 end,GetActorRotY=function() return 0 end,GetActorRotZ=function() return 0 end,SendActorCellChange=function() replay=replay+1 end},{__index=function() return noop end})
logicHandler={GetChatName=function() return 'tester' end,IsGeneratedRecord=function() return false end}
local function cell(name,visitor)
 local c=setmetatable({description=name,isExterior=false,visitors={visitor},saves=0,loads=0,data={objectData={},packets={cellChangeTo={},cellChangeFrom={},spawn={},delete={},actorList={},statsDynamic={},ai={}}}},{__index=BaseCell})
 function c:QuicksaveToDrive() self.saves=self.saves+1 end
 function c:LoadActorPackets(pid,data,ids) self.loads=self.loads+1;assert(data[ids[1]].statsDynamic.health==17) end
 return c
end
local a,b=cell('A',2),cell('B',3);LoadedCells={A=a,B=b}
local id='42-0';local live={refId='npc',statsDynamic={health=17,magicka=8,fatigue=31},location={posX=0,posY=0,posZ=0}}
a.data.objectData[id]=live;a.data.packets.statsDynamic={id};a.data.packets.actorList={id}
a:SaveActorCellChanges(1)
assert(b.data.objectData[id]==live and a.data.objectData[id].cellChangeTo=='B')
assert(b.loads==1 and b.saves==1)
-- Old deletion marker at the origin must not delete the returning actor.
a.data.packets.delete={id};dest='A'
b:SaveActorCellChanges(1)
assert(a.data.objectData[id]==live and b.data.objectData[id]==nil)
assert(live.statsDynamic.health==17 and live.statsDynamic.magicka==8 and live.statsDynamic.fatigue==31)
assert(live.cellChangeFrom==nil and live.cellChangeTo==nil)
assert(not tableHelper.containsValue(a.data.packets.delete,id))
assert(not tableHelper.containsValue(a.data.packets.cellChangeTo,id))
assert(tableHelper.containsValue(a.data.packets.statsDynamic,id))
assert(a.loads==1 and replay==2 and a.saves==2 and b.saves==2)
assert(not b:MoveObjectData(id,a) and a.data.objectData[id]==live)
assert(not a:MoveObjectData(id,a) and a.data.objectData[id]==live)
print('PASS Z012: A -> B -> A stats, observer replay, deletion/redirect cleanup, immediate persistence, stale move')
