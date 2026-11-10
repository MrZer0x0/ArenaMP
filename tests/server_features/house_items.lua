local assertions=0
local function check(v,msg) assert(v,msg);assertions=assertions+1 end
local function copy(t) if type(t)~='table' then return t end local r={} for k,v in pairs(t) do r[k]=copy(v) end return r end
tableHelper={deepCopy=copy}
local hooks={}
customEventHooks={registerHandler=function(n,f) hooks[n]=f end,registerValidator=function(n,f) hooks[n]=f end}
config={};jsonInterface={};bountyRespawn=nil
enumerations={inventory={SET=0,ADD=1,REMOVE=2}}
local cell,allowTake,owner='Home',false,'owner'
local prop={mode='original',owner='owner'}
local items,balances={},{}
local ids,dialogs,messages={},{},{}
tes3mp={GetCell=function(pid) return ids[pid] or cell end,GetPosX=function() return 0 end,GetPosY=function() return 0 end,GetPosZ=function() return 0 end,
 MessageBox=function(pid,_,m) messages[pid]=m end,SendMessage=function(pid,m) messages[pid]=m end,
 CustomMessageBox=function(pid,id,text,buttons) dialogs[pid]={id=id,text=text,buttons=buttons} end,
 InputDialog=function(pid,id) dialogs[pid]={id=id} end,
 ClearObjectList=function() end,SetObjectListPid=function() end,SetObjectListCell=function() end,
 SendObjectDelete=function() end}
packetBuilder={AddObjectDelete=function() end}
local function p(pid,name)
 return {pid=pid,accountName=name,accountFile=name..'.json',language='EN',data={stats={healthCurrent=100},inventory={{refId='gold_001',count=1000}}},
 IsLoggedIn=function() return true end,LoadInventory=function() end,LoadEquipment=function() end}
end
Players={[1]=p(1,'owner'),[2]=p(2,'guest'),[3]=p(3,'coowner')}
houseHelper={GetBaseCellForInstance=function() return nil end,GetProperty=function() prop.owner=owner;return prop end,
 GetAccessLevel=function(pid) return pid==1 and 'owner' or pid==3 and 'coowner' or 'friend' end,
 IsPurgePending=function() return false end,CanTake=function(pid) return pid==1 or pid==3 or allowTake end}
local function obj(id) return {refId=id,count=2,charge=11,enchantmentCharge=5,soul='rat',poisonId='venom',poisonCharges=3,location={posX=0,posY=0,posZ=0}} end
local function room() return {data={packets={delete={}},objectData={['0-1']=obj('sword'),['0-2']=obj('potion')}},Save=function() end,
 DeleteObjectData=function(self,index)
  self.data.objectData[index]=nil
  for i=#self.data.packets.delete,1,-1 do if self.data.packets.delete[i]==index then table.remove(self.data.packets.delete,i) end end
 end} end
LoadedCells={Home=room(),Other=room()}
logicHandler={GetRecordStoreByRecordId=function() end,IsGeneratedRecord=function() return false end,
 DeleteObject=function(pid,c,index) LoadedCells[c].data.objectData[index]=nil;LoadedCells[c].data.packets.delete[#LoadedCells[c].data.packets.delete+1]=index end}
package.loaded.arenaFurnitureAutoCatalog={findActiveRecords=function() return {sword={kind='WEAP'},potion={kind='ALCH'}} end,
 findInteriorReference=function() return nil end}
playerVitrines={HouseItems=function() return items end,Balance=function(pid) return balances[Players[pid].accountFile] or 0 end,
 HasAssets=function() return false end,CommitHouse=function(pid,nextItems,profile,seller,price)
 items=copy(nextItems);if profile then Players[pid].data=copy(profile) end
 if seller then balances[seller]=(balances[seller] or 0)+price end;return true
 end,Collect=function() end,RecoverLegacy=function() end}
local market=dofile('server/scripts/houseItemMarket.lua')
local function request(pid,verb,index,id) return market.HandleControl(pid,'@@AMP_HOUSE@@HITEM\t'..verb..'\t'..(index or '0-1')..'\t'..(id or 'sword')) end
local function respond(pid,data) local d=dialogs[pid];dialogs[pid]=nil;hooks.OnGUIAction({validDefaultHandler=true},pid,d.id,tostring(data)) end
request(1,'LOCK');check(items['Home\n0-1'].locked,'owner lock persisted')
request(1,'TAKE');check(LoadedCells.Home.data.objectData['0-1']~=nil,'owner cannot take own locked item')
allowTake=true;request(2,'ACTIVATE');check(dialogs[2]==nil and LoadedCells.Home.data.objectData['0-1'],'guest cannot take lock even when taking allowed')
request(2,'UNLOCK');check(items['Home\n0-1'].locked,'guest cannot unlock')
request(3,'UNLOCK');check(not items['Home\n0-1'].locked,'coowner can unlock')
request(3,'PRICE');check(dialogs[3]==nil,'coowner cannot redirect proceeds')
request(1,'PRICE');respond(1,200)
check(items['Home\n0-1'].price==200 and items['Home\n0-1'].seller=='owner.json','price belongs to property owner')
allowTake=false;request(2,'ACTIVATE');check(dialogs[2] and dialogs[2].id==923471,'priced item offered despite house take prohibition')
request(1,'LOCK');respond(2,0);check(LoadedCells.Home.data.objectData['0-1']~=nil,'lock after buy prompt cancels purchase')
request(1,'UNLOCK');request(2,'ACTIVATE')
request(1,'PRICE');respond(1,300);respond(2,0)
check(LoadedCells.Home.data.objectData['0-1']~=nil,'changed price invalidates stale confirmation')
request(2,'ACTIVATE');respond(2,0)
check(Players[2].data.inventory[1].count==700,'purchase debits exact new price')
check(balances['owner.json']==300,'seller proceeds credited')
check(LoadedCells.Home.data.objectData['0-1']==nil and items['Home\n0-1'].consumed,'receipt consumes world item')
local i=Players[2].data.inventory[2]
check(i.count==2 and i.charge==11 and i.enchantmentCharge==5 and i.soul=='rat' and i.poisonCharges==3,'world stack attributes preserved')
request(2,'ACTIVATE');check(dialogs[2]==nil and #Players[2].data.inventory==2,'sold item cannot be bought again')
-- Same network reference number in another property is a different lock.
cell='Other';request(1,'LOCK');check(items['Other\n0-1'].locked,'independent cell identity')
check(items['Home\n0-1'].consumed and not items['Other\n0-1'].consumed,'cross-cell identity does not collide')
check(market.GuardInventory(2,{action=1,inventory={{refId='sword',count=1}}}),'forged inventory ADD of locked display refused')
market.AllowInventoryGain(2,'sword',1)
check(not market.GuardInventory(2,{action=1,inventory={{refId='sword',count=1}}}),'validated chest withdrawal of same ID accepted')
check(market.GuardInventory(2,{action=1,inventory={{refId='sword',count=1}}}),'container receipt cannot replay')
request(1,'UNLOCK');request(1,'PRICE');respond(1,100);request(1,'LOCK');request(1,'CLEAR')
check(items['Other\n0-1'].price==nil and items['Other\n0-1'].locked,'removing price does not remove lock')
request(1,'UNLOCK');request(2,'ACTIVATE');check(LoadedCells.Other.data.objectData['0-1']~=nil,'unpriced item uses property take permission')
allowTake=true;request(2,'ACTIVATE');check(LoadedCells.Other.data.objectData['0-1']==nil,'unpriced unlocked item can be taken with permission')
cell='Home';request(1,'PRICE','0-2','potion');respond(1,50)
owner='newowner';request(2,'ACTIVATE','0-2','potion')
check(dialogs[2]==nil,'old owner price invalid after property transfer')
check(balances['owner.json']==300,'old owner receives no money after house gift')
request(1,'WALLET');check(dialogs[1].id==923472,'proceeds available via UI transport')
check(not market.HandleControl(1,'/vitrina collect'),'no chat command path')
-- Crash after receipt but before delete: cell load replays the tombstone.
LoadedCells.Other.data.objectData['0-1']=obj('sword')
hooks.OnCellLoad({},2,'Other')
check(LoadedCells.Other.data.objectData['0-1']==nil,'durable consumed marker deletes stale world copy')
local purging=true
houseHelper.IsPurgePending=function() return purging end
houseHelper.IsPurgeDelete=function(pid,c,index) return pid==1 and c=='Home' and index=='42-0' end
check(not market.HandleObject(1,'Home','ObjectDelete',{['42-0']={refId='sword'}}),'purge bypasses market pickup gate')
check(market.HandleObject(2,'Home','ObjectDelete',{['42-0']={refId='sword'}}),'guest cannot impersonate cleanup')
check(market.HandleObject(1,'Home','ObjectDelete',{['42-0']={refId='sword'},['0-1']={refId='sword'}}),'mixed cleanup/player-item packet rejected')
purging=false
check(market.HandleObject(1,'Home','ObjectDelete',{['42-0']={refId='sword'}}),'ordinary deletes still go through market transaction')
print('House item market: '..assertions..' assertions passed')
