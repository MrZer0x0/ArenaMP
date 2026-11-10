-- Y069: visitors may read books, trusted home members may also take them.
local function copy(v)
    if type(v)~='table' then return v end
    local r={};for k,x in pairs(v) do r[k]=copy(x) end;return r
end
tableHelper={deepCopy=copy}
local staticBook={refId='note',kind='BOOK',location={posX=0,posY=0,posZ=0}}
package.preload.arenaFurnitureAutoCatalog=function()
    return {
        findActiveRecords=function() return {note={kind='BOOK'},sword={kind='WEAP'}} end,
        findInteriorReference=function(_,_,_,index,refId)
            if index=='123-0' and refId=='note' then return copy(staticBook) end
        end
    }
end
customEventHooks={registerHandler=function() end,registerValidator=function() end}
config={};jsonInterface={};bountyRespawn=nil
enumerations={inventory={SET=0,ADD=1,REMOVE=2}}
local access='public';local normalTake=false
local property={owner='owner',mode='original'}
houseHelper={
 GetBaseCellForInstance=function() return nil end,
 GetProperty=function() return property end,
 GetAccessLevel=function() return access end,
 CanTake=function() return normalTake end,
 IsPurgePending=function() return false end,
}
local reloads=0
Players={[1]={accountName='tester',accountFile='tester.json',language='EN',data={stats={healthCurrent=100},inventory={}},
 IsLoggedIn=function() return true end,LoadInventory=function() reloads=reloads+1 end}}
tes3mp={GetCell=function() return 'home' end,GetPosX=function() return 0 end,GetPosY=function() return 0 end,GetPosZ=function() return 0 end,
 MessageBox=function() end,SendMessage=function() end}
LoadedCells={home={data={packets={delete={}},objectData={['123-0']=copy(staticBook)}}}}
logicHandler={GetRecordStoreByRecordId=function() end,IsGeneratedRecord=function() return false end}
local metadata={}
playerVitrines={HouseItems=function() return metadata end,CommitHouse=function(_,items) metadata=copy(items);return true end}
local market=dofile('server/scripts/houseItemMarket.lua')
local function activate()
 return market.HandleObject(1,'home','ObjectActivate',{['123-0']={refId='note',activatingPid=1}})
end
local function take()
 return market.HandleObject(1,'home','ObjectDelete',{['123-0']={refId='note'}})
end
local function reset()
 metadata={};LoadedCells.home.data.objectData['123-0']=copy(staticBook)
end
-- Everyone can reach the normal reader.
for _,role in ipairs({'public','friend','coowner','owner'}) do access=role;normalTake=false;reset();assert(activate()==false,role..' must be able to read') end
-- Ordinary visitor is read-only when house-wide take is disabled.
access='public';normalTake=false;reset();local before=reloads
assert(take()==true,'public visitor must not take book')
assert(reloads==before+1,'public rejected take must restore inventory')
assert(metadata['home\n123-0']==nil,'public visitor must not create tombstone')
-- Trusted members can take even while ordinary item pickup is disabled.
for _,role in ipairs({'friend','coowner','owner'}) do
 access=role;normalTake=false;reset()
 assert(take()==false,role..' must be able to take book')
 assert(metadata['home\n123-0'] and metadata['home\n123-0'].consumed,role..' take must persist tombstone')
end
-- Public visitors inherit book pickup only when ordinary take is explicitly enabled.
access='public';normalTake=true;reset();assert(take()==false,'public with allowTake must take book')
assert(metadata['home\n123-0'].consumed,'allowTake book pickup must persist tombstone')
-- Item-specific lock still overrides trusted membership.
access='owner';normalTake=false;reset();metadata['home\n123-0']={cell='home',index='123-0',refId='note',owner='owner',locked=true}
before=reloads;assert(take()==true,'locked book must stay protected');assert(reloads==before+1,'locked rejection reloads inventory')
print('Y069 house books: read-only visitor + friend/coowner/owner pickup passed')
