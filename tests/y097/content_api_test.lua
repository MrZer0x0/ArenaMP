package.path='server/scripts/?.lua;server/lib/lua/?.lua;'..package.path
local json=require('dkjson')
local checks=0
local function check(value,label) assert(value,label);checks=checks+1 end
local function clone(v) if type(v)~='table' then return v end local r={};for k,x in pairs(v) do r[k]=clone(x) end;return r end
local function item(id,count,charge) return {refId=id,count=count or 1,charge=charge or -1,enchantmentCharge=-1,soul='',poisonId='',poisonCharges=0} end
local function record(kind,id,slots,data) return {type=kind,id=id,slots=slots or {},data=data or {},inventory={},source='Base.esm',deleted=false} end
local records={}
local function add(r) records[r.type..':'..r.id:lower()]=r;return r end
local npc=add(record('NPC_','arrille'));npc.race='human';npc.inventory={item('shirt'),item('pants'),item('ring',2),item('shoes'),item('restock',-5)}
add(record('RACE','human',{}, {beast=0}));add(record('RACE','beast',{}, {beast=1}))
add(record('CLOT','shirt',{8},{type=2,value=5}));add(record('CLOT','pants',{9},{type=0,value=5}))
add(record('CLOT','shoes',{7},{type=1,value=5}));add(record('CLOT','ring',{12,13},{type=8,value=10}))
add(record('CLOT','sold_robe',{11},{type=4,value=100}));add(record('ARMO','armor',{1},{type=1,health=100}))
add(record('ARMO','shield',{17},{type=8,health=100}));add(record('WEAP','bow',{16},{type=9,health=100}))
add(record('WEAP','arrow',{18},{type=12,health=0}))
add(record('MISC','rock'));local deleted=add(record('MISC','deleted'));deleted.deleted=true
local handlers={};customEventHooks={registerHandler=function(name,fn) handlers[name]=fn end}
config={contentApi={enabled=false,files={'Base.esm'},encoding='win1251',autoSnapshots=true,maxSnapshotsPerActor=3,allowItemRecovery=false}}
enumerations={log={ERROR=1,INFO=2,WARN=3}}
local key,err='', '';local failReload=false;local failSave=false;local writes,publishes,gear,animations=0,0,0,0;local lastSaved
tes3mp={ContentCatalogGetVersion=function() return 1 end,ContentCatalogBegin=function() err='';return true end,
 ContentCatalogAddFile=function() return true end,ContentCatalogCommit=function()
    if failReload then err='bad_plugin';return false end key='key-v1';return true end,
 ContentCatalogGetError=function() return err end,ContentCatalogNormalizeId=function(v) return v:lower() end,
 ContentCatalogGetInfo=function() err='';return json.encode({contentKey=key,records=12}) end,
 ContentCatalogGetRecord=function(kind,id) err='';return json.encode(records[kind..':'..id:lower()]) end,
 ContentCatalogSearch=function(kind,prefix,offset,limit)
    err=''
    local ids={};for _,r in pairs(records) do if r.type==kind and not r.deleted and r.id:find(prefix,1,true)==1 then ids[#ids+1]=r.id end end
    table.sort(ids);local page={};for i=offset+1,math.min(#ids,offset+limit) do page[#page+1]=ids[i] end;return json.encode(page) end,
 ContentCatalogGetRecordCount=function(kind) err='';local n=0;for _,r in pairs(records) do if not r.deleted and (kind=='' or kind==r.type) then n=n+1 end end;return n end,
 ContentCatalogGetSubrecord=function(_,_,tag) if tag=='MODL' then err='';return '' end err='subrecord_not_found';return '' end,
 SendContentActorAnimation=function() animations=animations+1;return true end,
 LogMessage=function() end}
jsonInterface={quicksaveAtomic=function(path,data)
    check(path=='cell/Home.json','correct saved cell path')
    if failSave then return false end writes=writes+1;lastSaved=clone(data);return true
end}
packetBuilder={GetRelevantPlayersForCell=function() return {1,2} end,
 UpdateContainerForRelevantPlayers=function() publishes=publishes+1;check(lastSaved~=nil,'disk commit before publish') end}
local function newCell()
    local cell={entryFile='Home.json',description='Home',data={packets={container={},equipment={}},objectData={['331473-0']={refId='arrille',stats={healthCurrent=100},
        inventory={item('shirt'),item('ring',2),item('armor',1,75)},equipment={[8]=item('shirt')}}}},actorEquipmentTransferGuards={['331473-0']={apparel={[8]=item('shirt')}}}}
    cell.LoadActorEquipment=function() gear=gear+1;check(lastSaved~=nil,'disk commit before equipment') end
    return cell
end
LoadedCells={Home=newCell()}
local C=require('contentApi');local A=C.Actors
check(not C.IsReady(),'catalog starts unloaded')
check(not C.GetRecord('NPC_','arrille'),'queries fail before load')
check(C.Reload() and C.IsReady(),'explicit catalog load')
check(C.GetNpcInventory('ARRILLE')[3].count==2,'NPC baseline inventory')
local r,why=C.GetItem('shirt');check(r and why==nil,'successful item lookup has no error')
check(C.GetItemSlots('shirt')[1]==8,'equipment slot query')
local dependencies=C.GetDependencies('NPC_','arrille')
check(#dependencies.records>=5,'direct record dependencies reported')
check(not C.GetRecord('MISC','deleted') and C.GetRecord('MISC','deleted',true).deleted,'deleted records opt in')
check(C.Search('clothing','s',0,10)[1]=='shirt','paged alias search')
check(C.GetSubrecord('CLOT','shirt','MODL')=='','empty subrecord is not a missing subrecord')
local _,missing=C.GetSubrecord('CLOT','shirt','MISS');check(missing=='subrecord_not_found','native query error forwarded')
failReload=true;check(not C.Reload() and C.GetKey()=='key-v1','failed reload keeps active catalog key');failReload=false
check(not C.Search('CLOT','',0,-1),'invalid search count rejected')
check(A.AddItem('Home','331473-0','rock',3).added==3,'add known item')
check(writes==1 and publishes==1,'add committed and published once')
local snapshot=A.GetSnapshot('Home','331473-0');snapshot.inventory[1].count=999
check(LoadedCells.Home.data.objectData['331473-0'].inventory[1].count==1,'snapshot return is detached')
local before=LoadedCells.Home.data
failSave=true;local _,saveError=A.AddItem('Home','331473-0','rock',2)
check(saveError=='cell_save_failed' and LoadedCells.Home.data==before and publishes==1,'failed disk commit leaves memory/network unchanged');failSave=false
check(not A.AddItem('Home','331473-0','does_not_exist',1),'unknown item rejected')
check(not A.AddItem('Home','331473-0','rock',0/0),'NaN count rejected')
check(not A.RemoveItem('Home','331473-0','rock',50),'insufficient quantity rejected')
check(not A.AddItem('Offline','331473-0','rock',1),'unloaded cell is not manufactured')
LoadedCells.Home.data.objectData['331473-0'].inventory=nil
check(not A.AddItem('Home','331473-0','rock',1),'unknown inventory is not manufactured')
LoadedCells.Home=newCell()
local oldPublishes=publishes
check(A.EquipItem('Home','331473-0','ring',12),'equip left ring')
check(A.EquipItem('Home','331473-0','ring',13),'equip second identical ring')
check(publishes==oldPublishes and gear>=4,'gear-only commands avoid inventory SET/autoEquip')
check(LoadedCells.Home.actorEquipmentTransferGuards['331473-0']==nil,'explicit equipment clears stale handoff guard')
check(not A.EquipItem('Home','331473-0','shirt',11),'invalid slot rejected')
check(A.RemoveItem('Home','331473-0','ring',1).removed==1,'remove one of two rings')
local object=LoadedCells.Home.data.objectData['331473-0']
check(object.equipment[12] and not object.equipment[13],'removal prunes unavailable second equipped copy')
check(A.UnequipItem('Home','331473-0',8),'explicit unequip accepted')
check(not LoadedCells.Home.data.objectData['331473-0'].equipment[8],'unequip persisted')
check(A.EquipItem('Home','331473-0','shirt',8),'re-equip retained shirt')
check(A.CaptureOutfit('Home','331473-0','before'),'manual outfit capture')
object=LoadedCells.Home.data.objectData['331473-0'];A.ObserveSnapshot(object,object.equipment)
check(object.arenaContentSnapshots.auto.items[8].refId=='shirt','automatic good outfit snapshot')
A.ObserveSnapshot(object,{})
check(object.arenaContentSnapshots.auto.items[8].refId=='shirt','empty packet cannot erase automatic outfit')
check(A.UnequipItem('Home','331473-0',8) and A.RestoreOutfit('Home','331473-0','before'),'outfit restores existing items')
object=LoadedCells.Home.data.objectData['331473-0']
object.inventory={item('sold_robe',1,0)};object.equipment={}
local oldWrites=writes
check(not A.RestoreOutfit('Home','331473-0','before') and writes==oldWrites,'missing historical clothes are not silently recreated')
check(not A.RecoverBaseApparel('Home','331473-0',{addMissing=true,reason='repair Arrille'}),'base recovery disabled by default')
C.settings.allowItemRecovery=true
check(not A.RecoverBaseApparel('Home','331473-0',{addMissing=true}),'recovery needs an explicit reason')
check(A.RecoverBaseApparel('Home','331473-0',{addMissing=true,reason='repair Arrille'}),'explicit baseline clothing recovery')
local function total(id)
    local n=0;for _,v in ipairs(LoadedCells.Home.data.objectData['331473-0'].inventory) do if v.refId==id then n=n+v.count end end;return n
end
check(total('sold_robe')==1 and total('ring')==2 and total('restock')==0,'recovery preserves sale, restores two rings, excludes restock')
check(A.RecoverBaseApparel('Home','331473-0',{addMissing=true,reason='repeat repair'}),'repeat baseline repair accepted')
check(total('ring')==2 and total('shirt')==1,'repeat repair does not duplicate clothing')
object=LoadedCells.Home.data.objectData['331473-0'];object.stats.healthCurrent=0
check(not A.RecoverBaseApparel('Home','331473-0',{addMissing=true,reason='dead actor'}),'corpse is not dressed')
object.stats.healthCurrent=100
local oldAnimations=animations
local played=A.PlayAnimation('Home','331473-0','idle2',{count=2})
check(played.sent==2 and animations==oldAnimations+2,'animation dispatched to both relevant players')
check(not A.PlayAnimation('Home','331473-0','../../file') and not A.PlayAnimation('Home','331473-0','idle',{count=1001}),'animation arguments bounded')
-- Historical armor state must use an existing CURRENT durability variant.
LoadedCells.Home=newCell();check(A.EquipItem('Home','331473-0','armor',1),'equip current armor')
check(A.CaptureOutfit('Home','331473-0','armor_before'),'capture armor outfit')
object=LoadedCells.Home.data.objectData['331473-0'];object.inventory[3].charge=50;object.equipment={}
check(A.RestoreOutfit('Home','331473-0','armor_before'),'restore uses current inventory condition')
check(LoadedCells.Home.data.objectData['331473-0'].equipment[1].charge==50,'old durability is not recreated')
object=LoadedCells.Home.data.objectData['331473-0'];object.arenaContentSnapshots.armor_before.contentKey='other_content'
check(not A.RestoreOutfit('Home','331473-0','armor_before'),'snapshot from different content refused')
LoadedCells.Home=newCell();npc.race='beast';assert(C.Reload()) -- fixture plugin changed: invalidate decoded records
check(not A.AddItem('Home','bad-index','rock',1),'malformed actor index refused')
check(A.AddItem('Home','331473-0','shoes',1),'add shoes to beast inventory')
check(not A.EquipItem('Home','331473-0','shoes',7),'beast cannot wear boots/shoes')
npc.race='human';assert(C.Reload());check(A.AddItem('Home','331473-0','bow',1),'add bow')
check(A.AddItem('Home','331473-0','shield',1),'add shield')
check(A.EquipItem('Home','331473-0','bow',16),'equip bow')
check(not A.EquipItem('Home','331473-0','shield',17),'two-handed weapon conflicts with shield')
check(A.CaptureOutfit('Home','331473-0','temporary'),'capture additional snapshot')
check(A.DeleteOutfitSnapshot('Home','331473-0','temporary'),'snapshot deletion committed')
check(not A.DeleteOutfitSnapshot('Home','331473-0','temporary'),'deleted snapshot cannot be deleted twice')
local actor=LoadedCells.Home.data.objectData['331473-0']
actor.inventory[#actor.inventory+1]=item('arrow',10,0)
check(A.EquipItem('Home','331473-0','arrow',18),'zero-charge ammunition is not broken armor')
actor=LoadedCells.Home.data.objectData['331473-0'];actor.equipment[18].count=10
check(A.RemoveItem('Home','331473-0','arrow',3),'partial ammo removal')
check(LoadedCells.Home.data.objectData['331473-0'].equipment[18].count==7,'equipped ammo stack retains remaining count')
actor=LoadedCells.Home.data.objectData['331473-0'];actor.equipment[1]=item('armor',1,45)
check(A.RemoveItem('Home','331473-0','arrow',1),'unrelated inventory removal')
check(LoadedCells.Home.data.objectData['331473-0'].equipment[1].charge==45,'unrelated apparel not pruned by mutable-condition drift')
print('PASS Content Lua API: '..checks..' checks (actual Lua modules; native/storage/network APIs mocked)')
