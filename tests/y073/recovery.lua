-- Run from repository root: lua tests/y073/recovery.lua
local validators = {}
local commands = {}
local revived, popups = {}, {}
config = {deathRecovery = {["restore health refId patterns"] = {"^p_restore_health"}}}
enumerations = {log={WARN=1,INFO=2},inventory={REMOVE=1},resurrect={REGULAR=0}}
customEventHooks = {
    registerValidator=function(name, fn) validators[name]=fn end,
    registerHandler=function() end,
    makeEventStatus=function(a,b) return {a,b} end
}
customCommandHooks={registerCommand=function(name,fn) commands[name]=fn end}
package.preload.nirnRestoreHealth=function() return {} end
contentFixer={UnequipDeadlyItems=function() end}
logicHandler={GetChatName=function(pid) return 'Player'..pid end, IsCellLoaded=function() return true end}
bountyRespawn={Pending=function(p) return p.bounty == true end}
-- Public assistance must no longer consult group membership at all.
groupHelper={ArePlayersInSameGroup=function() error('unexpected group restriction') end}
RecordStores={potion={data={generatedRecords={not_healing={effects={{id=1,magnitudeMax=10}}}}}}}
tableHelper={cleanNils=function() end, concatenateFromIndex=function(t,i) return table.concat(t,' ',i) end}
inventoryHelper={removeClosestItem=function(inv,id,n)
 for _,v in pairs(inv) do if v.refId==id then local take=math.min(n,v.count);v.count=v.count-take;n=n-take end end
 assert(n==0)
end}
local function makePlayer(pid)
 return {pid=pid,name='Player'..pid,accountName='Player'..pid,cell='Balmora',x=0,logged=true,
 data={stats={healthCurrent=100,healthBase=100,experience=100,level=1},inventory={},customVariables={},location={cell='Balmora'}},
 IsLoggedIn=function(p) return p.logged end,Save=function() end,LoadItemChanges=function() end,LoadStatsDynamic=function() end,LoadLevel=function() end}
end
local function reset()
 Players={[1]=makePlayer(1),[2]=makePlayer(2)}
 Players[1].data.inventory={{refId='p_restore_health_b',count=5}}
 Players[2].deathRecoveryActive=true
 Players[2].data.stats.healthCurrent=0
 Players[2].deathRecoveryInitialXp=100
 Players[2].deathRecoveryStartedAt=0
 Players[2].deathRecoveryDuration=30
 revived={};popups={}
end
tes3mp={GetMillisecondsSinceServerStart=function() return 10000 end,
 GetName=function(pid) return Players[pid].name end,GetCell=function(pid) return Players[pid].cell end,
 GetPosX=function(pid) return Players[pid].x end,GetPosY=function() return 0 end,GetPosZ=function() return 0 end,
 SendMessage=function() end,MessageBox=function(pid,id,text) popups[#popups+1]={pid=pid,text=text} end,
 LogMessage=function() end,StopTimer=function() end,Resurrect=function(pid) revived[#revived+1]=pid end}
reset()
dofile('server/scripts/deathRecovery.lua')
local function request(action)
 validators.OnPlayerSendMessage({},1,'@@AMP_REVIVE@@'..action..'\tPlayer2\tp_restore_health_b')
end
request('TOUCH');assert(#revived==1 and Players[2].data.stats.healthCurrent==10)
request('TOUCH');assert(#revived==1,'duplicate resurrection')
reset();request('ALLY_POTION');assert(#revived==1 and Players[1].data.inventory[1].count==4)
reset();Players[2].data.stats.level=10;request('ALLY_POTION');assert(#revived==1 and Players[1].data.inventory[1].count==2)
for _,deny in ipairs({
 function() Players[2].cell='Other' end,
 function() Players[2].x=900 end,
 function() Players[2].bounty=true end,
 function() Players[2].data.customVariables.ampLastRevive=os.time() end,
 function() Players[1].data.stats.healthCurrent=0 end,
 function() Players[1].deathRecoveryActive=true end,
 function() Players[2].logged=false end,
 function() Players[2].deathRecoveryActive=false end
}) do
 reset();deny();request('ALLY_POTION');assert(#revived==0);assert(Players[1].data.inventory[1].count==5)
end
reset();Players[1].data.inventory[1].count=0;request('ALLY_POTION');assert(#revived==0)
reset();Players[1].data.inventory={{refId='not_healing',count=5}}
validators.OnPlayerSendMessage({},1,'@@AMP_REVIVE@@ALLY_POTION\tPlayer2\tnot_healing')
assert(#revived==0 and Players[1].data.inventory[1].count==5)
reset();Players[1].data.stats.healthCurrent=0;request('TOUCH');assert(#revived==0)
reset();Players[2].x=900;request('TOUCH');assert(#revived==0)
print('PASS: public rescue, once-only recovery, inventory quantities, range/cell/death/bounty/lockout rejection')
-- Load the real RP chat module and exercise both cross-mode speech paths.
reset()
color=setmetatable({}, {__index=function(_,k) return '#'..(k=='Gray' and '888888' or 'FFFFFF') end})
localization={Get=function(pid,module,key) return key end}
LoadedCells={Balmora={visitors={1,2}}}
dofile('server/scripts/coreChat.lua')
for _,rp in ipairs({true,false}) do
 Players[1].data.customVariables.RPStatus=rp
 Players[2].data.customVariables.RPStatus=not rp
 popups={};commands.say(1,{'say','Привет'})
 assert(#popups==1 and popups[1].pid==2 and popups[1].text:find('Привет',1,true))
 assert(not popups[1].text:find('footer',1,true) and not popups[1].text:find('#888888',1,true))
 popups={};commands.say(1,{'say',string.rep('я',200)})
 assert(#popups==1 and utf8.len(popups[1].text)~=nil and #popups[1].text<=250)
end
print('PASS: RP/non-RP messages have no grey footer; long Cyrillic remains valid UTF-8')
