package.path = 'server/scripts/?.lua;' .. package.path

local packets, warnings = {}, {}
local function mkplayer()
    return {data={}, QuicksaveToDrive=function(self) self.saved=(self.saved or 0)+1 end}
end

Players={[1]=mkplayer(),[2]=mkplayer(),[3]=mkplayer()}
config={personalScriptLocalsDiagnostics=true,personalScriptLocalRules={
    {cell='Arena Test Cell',uniqueIndex='100-0',refId='arena test object',scriptId='ArenaTestScript'}
}}
enumerations={variableType={SHORT=0,LONG=1,FLOAT=2},
    packetOrigin={CLIENT_SCRIPT_LOCAL=3},log={INFO=1,WARN=2}}
tes3mp={LogMessage=function(_,msg) warnings[#warnings+1]=msg end,
    ClearObjectList=function() packets[#packets+1]={objects={}} end,
    SetObjectListPid=function(pid) packets[#packets].pid=pid end,
    SetObjectListCell=function(cell) packets[#packets].cell=cell end,
    SendClientScriptLocal=function(all,skip)
        assert(all==false and skip==false,'must send only to intended player')
        packets[#packets].sent=true
    end}
packetBuilder={AddClientScriptLocal=function(index,object)
    packets[#packets].objects[index]=object
end}

local module=require('personalScriptLocals')
local function object(script,value)
    return {clientScriptId=script or 'ArenaTestScript',refId='arena test object',variables={[0]={[0]=value}}}
end
local cell='Arena Test Cell'

-- A configured reference is removed from the shared stream and stored only for
-- the player who sent it.
local first={['100-0']=object(nil,1),['different-0']=object('SharedScript',42)}
module.Observe(1,cell,first)
module.Observe(1,cell,first)
module.Capture(1,cell,first,3)
assert(first['100-0']==nil and first['different-0']~=nil,'personal ref must not leak to shared cell data')
assert(Players[1].saved==1)
assert(not Players[2].data.personalScriptLocals,'another player must not receive personal state')
local stored=Players[1].data.personalScriptLocals[cell]['100-0']
assert(stored.variables[0][0]==1)

-- Sending identical state should not cause another profile write.
module.Capture(1,cell,{['100-0']=object(nil,1)},3)
assert(Players[1].saved==1,'identical state must not force disk writes')

-- Wrong script/origin must not become personal state.
local wrongScript={['100-0']=object('OtherScript',7)}
module.Capture(2,cell,wrongScript,3)
assert(wrongScript['100-0']==nil and not Players[2].data.personalScriptLocals)
local wrongOrigin={['100-0']=object(nil,7)}
module.Capture(2,cell,wrongOrigin,4)
assert(wrongOrigin['100-0']==nil and not Players[2].data.personalScriptLocals)

-- Restore goes only to the owning player/cell.
assert(module.Restore(2,cell)==0)
assert(module.Restore(1,cell)==1)
assert(packets[1].pid==1 and packets[1].cell==cell and packets[1].sent)
assert(packets[1].objects['100-0'].variables[0][0]==1)
assert(module.Restore(1,'Other Cell')==0)

-- JSON may turn numeric variable keys into strings; restore must accept that.
Players[3].data.personalScriptLocals={[cell]={['100-0']={
    scriptId=stored.scriptId,refId=stored.refId,variables={['0']={['0']=1}}}}}
assert(module.Restore(3,cell)==1 and packets[2].pid==3)

local count=0
for _,msg in ipairs(warnings) do
    if msg:find('observed ArenaTestScript',1,true) then count=count+1 end
end
assert(count==1,'diagnostics must not spam each frame')
print('PASS H014 personal locals: generic allowlist, isolation, persistence, origin validation, bounded diagnostics')
