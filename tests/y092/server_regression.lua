-- Executes real changed Lua modules against a deterministic TES3MP mock.
local root = ROOT
local checks = 0
local function check(ok, label) assert(ok, label); checks = checks + 1 end
local function copy(t) if type(t) ~= 'table' then return t end local v={} for k,x in pairs(t) do v[k]=copy(x) end return v end
tableHelper = {deepCopy=copy, cleanNils=function() end,
 containsValue=function(t,v) for _,x in pairs(t) do if x==v then return true end end return false end,
 insertValueIfMissing=function(t,v) if not tableHelper.containsValue(t,v) then table.insert(t,v) end end,
 removeValue=function(t,v) for k,x in pairs(t) do if x==v then t[k]=nil end end end}
local realRequire=require
require=function(name) if name=='tableHelper' then return tableHelper end if name=='actorApparelGuard' then return dofile(root..'/server/scripts/actorApparelGuard.lua') end return {} end
class=function() return {} end
config={maxAttributeValue=100,maxSpeedValue=100,maxSkillValue=100,maxAcrobaticsValue=100,menuHelperFiles={}}
enumerations=dofile(root..'/server/scripts/enumerations.lua')
local sent, cleared, travels, paid, saves, acks, notices = 0,0,0,0,0,0,{}
tes3mp={GetAttributeId=function() return 0 end,GetSkillId=function() return 0 end,
 ClearAttributeModifier=function() cleared=cleared+1 end,ClearSkillModifier=function() cleared=cleared+1 end,
 SendAttributes=function() end,SendSkills=function() end,SendMessage=function(pid,s) table.insert(notices,s) end,
 GetCell=function() return 'Test' end,GetPosX=function() return 0 end,GetPosY=function() return 0 end,GetPosZ=function() return 0 end,
 ClearActorList=function() end,SetActorListPid=function() end,SetActorListCell=function() end,
 SendActorAI=function(a,b) check(a==true and b==false,'broadcast to all visitors');travels=travels+1 end,
 CopyReceivedObjectListToStore=function() end,SendObjectDialogueChoice=function() acks=acks+1 end,
 LogAppend=function() end}
local BasePlayer=dofile(root..'/server/scripts/player/base.lua')
local player=setmetatable({pid=1,data={attributes={Strength={base=95}},skills={Destruction={base=95}}}}, {__index=BasePlayer})
player.LoadAttributes=function() sent=sent+1 end;player.LoadSkills=player.LoadAttributes
player:SaveAttributes({attributes={Strength={base=95,modifier=50,damage=0,skillIncrease=0}}})
player:SaveSkills({skills={Destruction={base=95,modifier=50,damage=0,progress=0}}})
check(cleared==0 and sent==0,'fortify > cap survives')
check(player.data.attributes.Strength.base==95 and player.data.attributes.Strength.modifier==nil,'modified never stored as base')
player:SaveAttributes({attributes={Strength={base=101,modifier=0}}})
player:SaveSkills({skills={Destruction={base=101,modifier=0}}})
check(sent==2 and player.data.skills.Destruction.base==95,'base caps retained')
local BaseCell=dofile(root..'/server/scripts/cell/base.lua')
local cell=setmetatable({data={packets={ai={}},objectData={['1-0']={home={cell='Old'}}}},QuicksaveToDrive=function() end},{__index=BaseCell})
cell.ContainsObject=function(self,k) return self.data.objectData[k]~=nil end
cell:SaveActorAI({['1-0']={ai={action=enumerations.ai.FOLLOW,targetPlayer='A'}}})
check(cell.data.objectData['1-0'].home==nil,'follow clears stale home immediately')
check(cell.data.objectData['1-0'].ai.action==enumerations.ai.FOLLOW,'follow persisted')

-- Y092-EQ: an authority hand-off may report an NPC with empty apparel slots.
-- Preserve apparel 0..15 from the transfer snapshot, but keep combat slots
-- 16..18 dynamic. No visitors are attached in this unit test, so no packet send.
config.actorEquipmentTransferGuardSeconds=8
local eqCell=setmetatable({description='Exterior',visitors={},lastActorEquipmentSave=os.time(),
 actorEquipmentTransferGuards={['2-0']={toCell='Exterior',expiresAt=os.time()+8,apparel={
   [enumerations.equipment.SHIRT]={refId='common_shirt_01',count=1,charge=-1,enchantmentCharge=-1},
   [enumerations.equipment.ROBE]={refId='common_robe_01',count=1,charge=-1,enchantmentCharge=-1}}}},
 data={packets={equipment={}},objectData={['2-0']={equipment={}}}},QuicksaveToDrive=function() end},{__index=BaseCell})
eqCell.ContainsObject=function(self,k) return self.data.objectData[k]~=nil end
eqCell:SaveActorEquipment({['2-0']={equipment={
 [enumerations.equipment.CARRIED_RIGHT]={refId='iron_dagger',count=1,charge=100,enchantmentCharge=-1}}}})
check(eqCell.data.objectData['2-0'].equipment[enumerations.equipment.SHIRT].refId=='common_shirt_01','cell handoff preserves NPC shirt')
check(eqCell.data.objectData['2-0'].equipment[enumerations.equipment.ROBE].refId=='common_robe_01','cell handoff preserves NPC robe')
check(eqCell.data.objectData['2-0'].equipment[enumerations.equipment.CARRIED_RIGHT].refId=='iron_dagger','combat weapon remains dynamic')
check(eqCell.data.objectData['2-0'].equipment[enumerations.equipment.CARRIED_LEFT]==nil,'shield slot is not pinned by apparel guard')
-- Y092 hotfix: move-aside dialogue service is intentionally disabled.


-- Y092-SP: skillPoints authority, admin grants and trainer budget exemptions.
local AC=dofile(root..'/server/scripts/arenaAntiCheat.lua')
local nativeSkillPoints=nil
local levelReloads=0
tes3mp.SetSkillPoints=function(pid,value) nativeSkillPoints=value end
tes3mp.GetSkillName=function(id) if id==3 then return 'Destruction' end return 'Unknown' end
local acState={schema=2,legacy=true,pendingBaseline=false,authorizedSkillPointBonus=0,authorizedTrainingInvestment=0}
local acPlayer={pid=1,name='Target',accountName='target',data={
 stats={level=5,levelProgress=0,experience=0,skillPoints=12,xpAttributeProgress={0,0,0,0,0,0,0,0},xpRewardKeys={},healthBase=50},
 attributes={},skills={Destruction={base=50,damage=0,progress=0}},customVariables={ArenaMPAntiCheatV2=acState}},
 QuicksaveToDrive=function() saves=saves+1 end,
 LoadLevel=function() levelReloads=levelReloads+1 end}
Players={[1]=acPlayer}
local forged={stats={level=5,skillPoints=999}}
local clamped,allowed=AC.SanitizeSkillPointsPacket(1,forged)
check(clamped and allowed==12 and forged.stats.skillPoints==12 and nativeSkillPoints==12,'CE skillPoints increase clamped')
local spend={stats={level=5,skillPoints=7}}
clamped,allowed=AC.SanitizeSkillPointsPacket(1,spend)
check(not clamped and allowed==7 and spend.stats.skillPoints==7,'legitimate SP decrease allowed')
local levelup={stats={level=6,skillPoints=22}}
clamped,allowed=AC.SanitizeSkillPointsPacket(1,levelup)
check(not clamped and allowed==22,'same-packet level-up may grant 10 SP')
check(AC.AuthorizeTraining(1,3)==true and acState.authorizedTrainingInvestment==2,'trainer raise receives budget exemption')
local granted,balance=AC.GrantSkillPoints(1,33,'AdminTest')
check(granted and balance==45 and acPlayer.data.stats.skillPoints==45,'admin /addpoint grant changes server balance')
check(acState.authorizedSkillPointBonus==33 and acState.trusted.skillPoints==45,'admin grant is trusted and budget-authorized')
check(levelReloads==1 and nativeSkillPoints==45,'admin grant resyncs authoritative PlayerLevel')

-- Exercise the real slash-command branch as an administrator.
local adminMessages={}
local adminPlayer={
 IsServerOwner=function() return false end,
 IsAdmin=function() return true end,
 IsModerator=function() return false end,
 Message=function(self,text) table.insert(adminMessages,text) end}
acPlayer.Message=function(self,text) table.insert(adminMessages,text) end
Players[9]=adminPlayer
logicHandler={
 CheckPlayerValidity=function(sender,value) return tonumber(value)==1 end,
 GetChatName=function(pid) if pid==9 then return 'Admin' end return 'Target' end}
local savedRequire=require
require=function(name) if name=='arenaAntiCheat' then return AC end return savedRequire(name) end
local CommandHandler=dofile(root..'/server/scripts/commandHandler.lua')
CommandHandler.ProcessCommand(9,{'addpoint','1','33'})
require=savedRequire
check(acPlayer.data.stats.skillPoints==78 and acState.authorizedSkillPointBonus==66,'/addpoint command grants through trusted ledger')

print('Lua behavioral checks passed: '..checks)
