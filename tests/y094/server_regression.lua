local root = os.getenv('ARENAMP_TEST_ROOT') or '.'
package.path = root .. '/scripts/?.lua;' .. root .. '/lib/lua/?.lua;' .. package.path
unpack = table.unpack
local checks = 0
local function check(value, label)
    assert(value, label); checks = checks + 1
end
enumerations = require('enumerations')
local guard = require('actorApparelGuard')
local function shirt() return {refId='shirt',count=1,charge=-1} end
local obj = {equipment={[8]=shirt()},inventory={shirt()}}
local incoming = {}
check(guard.Repair(obj,incoming) and incoming[8].refId=='shirt', 'barter empty apparel repaired')
obj.inventory={};incoming={}
check(not guard.Repair(obj,incoming), 'sold item never recreated')
obj.inventory={shirt()};obj.deathState=1
check(not guard.Repair(obj,{}), 'corpse not dressed')
obj.deathState=0;obj.stats={healthCurrent=0}
check(not guard.Repair(obj,{}), 'dying actor not dressed')
obj.stats=nil;obj.inventory=nil
check(not guard.Repair(obj,{}), 'unknown inventory without handoff not guessed')
check(guard.Repair(obj,{},obj.equipment), 'handoff retains snapshot before inventory arrives')
-- Y095: armor occupies an armor slot; slot 8 is a shirt with no durability.
obj.inventory={{refId='helmet',count=1,charge=0}}
obj.equipment={[0]={refId='helmet',count=1,charge=0}}
check(not guard.Repair(obj,{}), 'broken armor can unequip')
obj.inventory={shirt()};obj.equipment={[8]=shirt()};incoming={[8]={refId='newshirt',count=1}}
check(not guard.Repair(obj,incoming) and incoming[8].refId=='newshirt', 'replacement apparel accepted')
obj.equipment={[12]=shirt(),[13]=shirt()};incoming={};obj.inventory={shirt()}
check(guard.Repair(obj,incoming) and incoming[12] and not incoming[13], 'one item cannot equip two slots')
obj.inventory={{refId='SHIRT',count=2}};incoming={}
check(guard.Repair(obj,incoming) and incoming[12] and incoming[13], 'two identical rings allowed')
obj.inventory={shirt()}
check(guard.Prune(obj) and obj.equipment[12] and not obj.equipment[13], 'remove clears unavailable ring')
obj.inventory={}
check(guard.Prune(obj) and not obj.equipment[12], 'sold worn item pruned before replay')
obj.equipment={[16]=shirt()};obj.inventory={shirt()}
check(not guard.Repair(obj,{}) and not guard.Prune(obj), 'weapon slots remain dynamic')

obj.equipment={[1]={refId='armor',count=1,charge=100}}
obj.inventory={{refId='armor',count=1,charge=75,enchantmentCharge=12,poisonId='poison',poisonCharges=2}}
incoming={}
check(guard.Repair(obj,incoming) and incoming[1].charge==75 and incoming[1].poisonCharges==2,
 'repair uses current inventory condition without recreating old durability')
-- Execute the real house module with a small deterministic in-memory store.
config={houseAutoGeneration='off'}
package.loaded.config=config
local property={cell='Test House',cells={'Test House','Test House, Basement'},enabled=true,mode='instance',price=100}
local store={version=2,properties={['Test House']=property},doors={}}
package.loaded.jsonInterface={load=function() return store end,save=function() return true end,saveAtomic=function() return true end}
package.loaded.fileHelper={}
package.loaded.packetBuilder={}
package.loaded.inventoryHelper={}
tableHelper=require('tableHelper')
customEventHooks=require('customEventHooks')
customCommandHooks={registerCommand=function() end}
local messages={};local currentCell='Test House'
tes3mp={GetCell=function() return currentCell end,SendMessage=function(_,msg) messages[#messages+1]=msg end,
 LogAppend=function() end,SetCell=function(_,cell) currentCell=cell end,
 SetPos=function() end,SetRot=function() end,SendCell=function() end,SendPos=function() end,
 StartTimer=function() end,CreateTimerEx=function() return 1 end}
Players={[1]={accountName='Owner',language='EN',IsLoggedIn=function() return true end,
 data={location={cell='Test House',posX=1,posY=2,posZ=3},customVariables={housingEntryMode='original'}}}}
local house=require('houseHelper')
house.HandleDoorRequest(1,'Test House, Basement',false,{posX=1,posY=2,posZ=3},false)
check(messages[#messages]:find('GO\tTest House, Basement',1,true), 'vacant linked room uses original door')
property.owner='Owner'
messages={};house.HandleDoorRequest(1,'Test House, Basement',false,{},false)
check(messages[#messages]:find('GO\tTest House, Basement',1,true), 'owned public original stays public')
currentCell='Test House - home'
local entered
house.EnterInstance=function(_,option) entered=option end
house.HandleDoorRequest(1,'Test House, Basement',false,{},false)
check(entered and entered.cell=='Test House, Basement - home', 'private basement stays private')
property.mode='original';property.owner=nil
local status=customEventHooks.triggerValidators('OnPlayerCellChange',{1,{location={cell='Test House, Basement'}}})
check(status.validDefaultHandler, 'vacant original property remains enterable')
property.owner='Other';property.locked=true
local evicted=false;house.EvictOriginalVisitor=function() evicted=true end
status=customEventHooks.triggerValidators('OnPlayerCellChange',{1,{location={cell='Test House'}}})
check(not status.validDefaultHandler and evicted, 'locked original still denies strangers')
local oldLocation=Players[1].data.location
house.Warp(1,'Destination',{posX=4,posY=5,posZ=6})
check(Players[1].data.location==oldLocation and oldLocation.cell=='Test House', 'warp waits for confirmed cell before saving')
-- Run the actual cell equipment handler, including observer correction/save.
package.loaded.contentFixer={}
package.loaded.questItemPhasing={}
package.loaded.questIndexStore={}
package.loaded.actorLootRepair={}
class=function() return {} end
local BaseCell=require('cell.base')
local broadcasts,saves=0,0
local cell=setmetatable({description='Test Cell',visitors={1,2},
 data={objectData={['10-0']={refId='npc',equipment={[8]=shirt()},inventory={shirt()}}},packets={equipment={}}}}, {__index=BaseCell})
cell.LoadActorEquipment=function() broadcasts=broadcasts+1 end
cell.QuicksaveToDrive=function() saves=saves+1 end
cell:SaveActorEquipment({['10-0']={equipment={}}})
check(cell.data.objectData['10-0'].equipment[8] and broadcasts==2 and saves==1,
 'real handler corrects both observers and saves authoritative snapshot')
cell.data.objectData['10-0'].inventory={}
cell:SaveActorEquipment({['10-0']={equipment={}}})
check(not cell.data.objectData['10-0'].equipment[8] and broadcasts==2,
 'real handler accepts inventory-backed removal')
print('Regression:',checks,'checks passed')
