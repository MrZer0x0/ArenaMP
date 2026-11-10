-- Run from the source root: lua tests/house_reading/read_only.lua
package.preload.arenaFurnitureAutoCatalog = function()
    return {findActiveRecords=function() return {note={kind='BOOK'},sword={kind='WEAP'}} end}
end
customEventHooks = {registerHandler=function() end,registerValidator=function() end}
config={};jsonInterface={};tableHelper={}
local canTake=false
houseHelper={
 GetBaseCellForInstance=function() return nil end,
 GetProperty=function() return {owner='owner',mode='original'} end,
 CanTake=function() return canTake end,
 IsPurgePending=function() return false end,
}
local bookStore={storeType='book',data={generatedRecords={generated_book={}},permanentRecords={}}}
logicHandler={GetRecordStoreByRecordId=function(id) if id=='generated_book' then return bookStore end end}
local metadata={}
playerVitrines={HouseItems=function() return metadata end}
local reloads=0
Players={[1]={LoadInventory=function() reloads=reloads+1 end}}
local market=dofile('server/scripts/houseItemMarket.lua')
local function activate(id)
 return market.HandleObject(1,'home','ObjectActivate',{['1-0']={refId=id,activatingPid=1}})
end
for _,take in ipairs({false,true}) do
 canTake=take
 for _,id in ipairs({'note','generated_book'}) do
  metadata['home\n1-0']={locked=true,price=100,owner='owner'}
  assert(activate(id)==false,'reading must reach the normal book handler')
  assert(market.HandleObject(1,'home','ObjectMove',{['1-0']={refId=id}})==true,'reading must not unlock moving')
  assert(market.HandleObject(1,'home','ObjectDelete',{['1-0']={refId=id}})==true,'reading must not unlock taking')
 end
end
assert(reloads==4,'rejected deletion must restore authoritative inventory')
print('House reading: static/generated books, take allowed/denied, lock/price and deletion guards passed')
