package.path = 'server/scripts/?.lua;' .. package.path
config = {arenaBoundItemRefIds={'mod_temporary_sword'}}
local callbacks = {}
customEventHooks = {
 registerValidator=function(name, fn) callbacks[name]=fn end,
 makeEventStatus=function(a,b) return {validDefaultHandler=a, validCustomHandlers=b} end
}
local rules = require('arenaSummonRules')
local function player(score)
 return {data={attributes={Intelligence={base=score}},skills={Conjuration={base=score}}},
 summons={}, IsLoggedIn=function() return true end}
end
assert(rules.Limit(player(-50))==1)
assert(rules.Limit(player(0))==1)
assert(rules.Limit(player(50))==5)
assert(rules.Limit(player(100))==10)
assert(rules.Limit(player(999))==10)
local p=player(100)
p.data.skills.Conjuration.base=10
p.data.skills.Conjuration.modifier=900
assert(rules.Limit(p)==1)
Players={[1]=player(100)}
for i=1,9 do Players[1].summons[tostring(i)]='scamp' end
local obj={refId='scamp',summon={hasPlayerSummoner=true,summoner={pid=1}}}
assert(rules.AllowSpawn({obj}))
assert(not rules.AllowSpawn({obj,obj})) -- atomic batch cannot exceed ten
Players[1].summons['10']='scamp'
assert(not rules.AllowSpawn({obj}))
Players[1].summons['10']=nil
assert(rules.AllowSpawn({obj})) -- deletion releases slot
assert(rules.IsBound('BOUND_LONGSWORD'))
assert(rules.IsBound('mod_temporary_sword'))
assert(not rules.IsBound('iron_longsword'))
rules.Register()
local bad=callbacks.OnObjectPlace({},1,'cell',{{refId='bound_longsword'}})
assert(bad.validDefaultHandler==false)
assert(callbacks.OnObjectPlace({},1,'cell',{{refId='iron_longsword'}})==nil)
local inv={inventory={{refId='iron_longsword'},{refId='bound_longsword'},{refId='mod_temporary_sword'}}}
callbacks.OnPlayerInventory({},1,inv)
assert(#inv.inventory==1 and inv.inventory[1].refId=='iron_longsword')
local item='bound_longsword'
tes3mp={GetObjectListSize=function() return 1 end,GetContainerChangesSize=function() return 1 end,
 GetContainerItemRefId=function() return item end}
assert(callbacks.OnContainer({},1,'cell',{}).validDefaultHandler==false)
item='iron_longsword'
assert(callbacks.OnContainer({},1,'cell',{})==nil)
print('PASS Y082: limits, buffs, packet batches, removal, bound placement/container/inventory')
