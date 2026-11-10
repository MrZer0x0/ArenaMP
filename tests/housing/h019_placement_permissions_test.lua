config={objectGrabPermissions={}}
package.loaded.config=config
package.loaded.tableHelper={containsValue=function(t,x) for _,v in pairs(t) do if v==x then return true end end return false end}
local access='owner'
local property={owner='Alice',mode='instance'}
package.loaded.houseHelper={
 GetBaseCellForInstance=function(cell) return cell:match('^(House) %- ') end,
 GetProperty=function(cell) if cell=='House' then return property end end,
 GetAccessLevel=function() return access end,
 GetInstanceDescription=function() return property.mode=='original' and 'House' or 'House - home' end
}
local rank=0
Players={[1]={IsLoggedIn=function() return true end,IsModerator=function() return rank>=1 end}}
enumerations={packetOrigin={CLIENT_GAMEPLAY=1}}
local origin=1
tes3mp={GetObjectListOrigin=function() return origin end}
local data={objectData={['0-1']={refId='iron_sword',droppedByPlayer=true}},packets={place={'0-1'}}}
LoadedCells={['House - home']={data=data},['House - stale']={data=data},House={data=data},['0, 0']={data=data}}
local p=dofile('server/scripts/objectPlacementPermissions.lua')
local object={['0-1']={refId='iron_sword'}}
assert(p.IsAllowed(1,'House - home',object))
access='coowner'; assert(p.IsAllowed(1,'House - home',object))
for _,role in ipairs({'friend','public','none'}) do access=role; assert(not p.IsAllowed(1,'House - home',object)) end
access='owner'
assert(not p.IsAllowed(1,'0, 0',object))
assert(not p.IsAllowed(1,'House',object))
assert(not p.IsAllowed(1,'House - stale',object))
rank=1; assert(p.IsAllowed(1,'0, 0',object))
rank=2; assert(p.IsAllowed(1,'0, 0',object))
rank=0; config.objectGrabPermissions.enabled=false
assert(not p.IsAllowed(1,'0, 0',object))
config.objectGrabPermissions.enabled=true
property.mode='original'; assert(p.IsAllowed(1,'House',object))
assert(not p.IsAllowed(1,'House - home',object))
property.mode='instance'; origin=2; assert(p.IsAllowed(1,'0, 0',object)); origin=1
object['0-1'].refId='forged'; assert(not p.IsAllowed(1,'House - home',object))
object['0-1'].refId='iron_sword'; object['0-2']={refId='in_wall'}
assert(not p.IsAllowed(1,'House - home',object))
print('PASS: owner/coowner, guests, street, moderator/admin, original/private/stale instance, forgery, mixed packet, script origin')
