local checks=0
local function check(value, text) assert(value,text);checks=checks+1 end
local function copy(t)
    if type(t)~="table" then return t end
    local r={};for k,v in pairs(t) do r[k]=copy(v) end;return r
end
tableHelper={deepCopy=copy}
local handlers,commands={},{}
customEventHooks={registerHandler=function(name,fn) handlers[name]=fn end,
    registerValidator=function(name,fn) handlers[name]=fn end}
customCommandHooks={registerCommand=function(name,fn) commands[name]=fn end}
local files,failFile={},nil
jsonInterface={save=function(path,data)
    if path==failFile then return false end
    files[path]=copy(data);return true
end,load=function(path) return copy(files[path]) end,ioLibrary={open=function() return nil end}}
jsonInterface.saveAtomic=jsonInterface.save
config={dataPath='/unused',playersRespawn=true,shareBounty=false,deathTime=10,vitrineBannedItems={'quest_item'}}
enumerations={log={ERROR=1}}
time={seconds=function(n) return n*1000 end}
local now,timer,bounty=0,0,0
local nativeLock={}
local activeCells={}
tes3mp=setmetatable({GetMillisecondsSinceServerStart=function() return now*1000 end,
    CreateTimerEx=function() timer=timer+1;return timer end,
    GetFileStatusUtf8=function(path) return files[path:sub(#config.dataPath+2)] and 1 or 0 end,
    GetBounty=function() return bounty end,SetBounty=function(_,n) bounty=n end,
    GetCell=function(pid) return activeCells[pid] or 'Home' end,
    GetPosX=function() return 0 end,GetPosY=function() return 0 end,GetPosZ=function() return 0 end,
    SetRespawnLocked=function(pid,v) nativeLock[pid]=v end,
    StopServer=function() error('simulated stop') end}, {__index=function() return function() end end})
logicHandler={IsGeneratedRecord=function(id) return id:sub(1,1)=='$' end}
local prop={owner='seller',mode='original'}
houseHelper={GetProperty=function() return prop end,GetBaseCellForInstance=function() return nil end,
    GetAccessLevel=function(pid) return pid==1 and 'owner' or 'none' end,
    IsInstanceCell=function() return false end,IsPurgePending=function() return false end}
LoadedCells={Home={data={objectData={['0-1']={refId='table',arenaFurnitureOwner='seller',
    arenaFurnitureRefId='table',location={posX=0,posY=0,posZ=0}}}}}}
local function player(pid,name,inv)
    return {pid=pid,accountName=name,accountFile=name..'.json',language='EN',loggedIn=true,
        data={stats={healthCurrent=100,healthBase=100,experience=20},customVariables={},fame={bounty=0},inventory=inv or {},equipment={}},
        IsLoggedIn=function(p) return p.loggedIn end,LoadStatsDynamic=function() end,
        LoadLevel=function() end,LoadInventory=function() end,LoadEquipment=function() end}
end
Players={[1]=player(1,'seller',{{refId='sword',count=4,charge=17,enchantmentCharge=9,soul='soul',poisonId='poison',poisonCharges=2}}),
    [2]=player(2,'buyer',{{refId='gold_001',count=1000}}),[3]=player(3,'other',{{refId='gold_001',count=1000}})}
local function bootMarket()
    playerVitrines=dofile('server/scripts/playerVitrines.lua')
    handlers.OnServerPostInit()
end
files['playerVitrines.json']={version=1,nextId=1,stands={['Home\n0-1']={cell='Home',index='0-1',owner='seller',seller='seller.json'}},
    owners={['seller.json']='Home\n0-1'},listings={},balances={}}
bootMarket()
check(commands.vitrina==nil,'no vitrine chat command registered')
check(not playerVitrines.IsStand('Home','0-1'),'legacy furniture no longer reserved')
local sword=copy(Players[1].data.inventory[1])
check(not playerVitrines.ListItem(1,sword,-5,1),'negative price')
check(not playerVitrines.ListItem(1,sword,1.5,1),'fractional price')
check(not playerVitrines.ListItem(1,sword,10,99),'insufficient stock')
Players[1].data.equipment[0]={refId='sword'}
check(not playerVitrines.ListItem(1,sword,100,1),'equipped item refused')
Players[1].data.equipment={}
check(playerVitrines.ListItem(1,sword,300,2),'listing')
check(Players[1].data.inventory[1].count==2,'escrow removes quantity')
check(not playerVitrines.HandleObject(2,'Home','ObjectDelete',{['0-1']={}}),'legacy stand packet interception disabled')
check(not playerVitrines.Buy(1,'1','Home\n0-1'),'self purchase refused')
activeCells[2]='Elsewhere'
check(not playerVitrines.Buy(2,'1','Home\n0-1'),'remote purchase refused')
activeCells[2]=nil
prop.owner='newowner'
check(not playerVitrines.Buy(2,'1','Home\n0-1'),'house transfer invalidates display')
prop.owner='seller'
check(playerVitrines.Buy(2,'1','Home\n0-1'),'purchase')
check(not playerVitrines.Buy(3,'1','Home\n0-1'),'second buyer cannot buy sold lot')
check(Players[2].data.inventory[1].count==700,'buyer debited')
local received=Players[2].data.inventory[2]
check(received.count==2 and received.charge==17 and received.enchantmentCharge==9 and received.soul=='soul' and received.poisonCharges==2,'item attributes preserved')
bootMarket()
check(playerVitrines.Collect(1),'persistent proceeds')
check(not playerVitrines.Collect(1),'withdraw replay rejected')
check(Players[1].data.inventory[2].count==300,'seller credited once')
check(playerVitrines.ListItem(1,sword,50,1),'second listing')
check(playerVitrines.Cancel(1,'2'),'cancel returns item')
check(not playerVitrines.Cancel(1,'2'),'cancel replay rejected')
check(playerVitrines.ListItem(1,sword,50,1),'third listing')
failFile='player/buyer.json'
local ok=pcall(function() playerVitrines.Buy(2,'3','Home\n0-1') end)
check(not ok and files['playerVitrines.json'].pending~=nil,'failure leaves redo journal')
failFile=nil
bootMarket()
check(files['playerVitrines.json'].pending==nil,'redo journal recovered')
check(files['player/buyer.json'].inventory[1].count==650,'recovery persisted one debit')
check(not playerVitrines.Buy(3,'3','Home\n0-1'),'recovered transaction cannot replay')
check(playerVitrines.Collect(1),'recovered sale credits seller')
-- All sentence timing is independent of offline wall time and client events.
bountyRespawn=dofile('server/scripts/bountyRespawn.lua')
local p=player(4,'criminal');Players[4]=p
p.Resurrect=function(self)
    if not bountyRespawn.AllowRespawn(self) then return end
    check(not nativeLock[self.pid],'only server expiry unlocks native gate')
    self.revived=(self.revived or 0)+1
    bountyRespawn.Complete(self)
end
bounty=999;check(not bountyRespawn.Begin(p),'999 normal death')
bounty=1999;check(bountyRespawn.Begin(p),'1999 starts penalty')
check(p.data.customVariables.ampBountyRespawn.total==30 and bounty==0,'full thousand floor and bounty forgiveness')
check(nativeLock[4] and files['player/criminal.json'].customVariables.ampBountyRespawn~=nil,'lock persisted')
now=10;bountyRespawn.Tick(4,'criminal',p.ampBountyToken)
check(p.data.customVariables.ampBountyRespawn.remaining==20,'online decrement')
check(bountyRespawn.Begin(p) and p.data.customVariables.ampBountyRespawn.remaining==20,'duplicate death no extension')
p:Resurrect();check(not p.revived,'early server timer rejected')
local oldToken=p.ampBountyToken
now=12;bountyRespawn.Disconnect(p)
check(files['player/criminal.json'].customVariables.ampBountyRespawn.remaining==18,'disconnect checkpoint')
now=1000;bountyRespawn.Resume(p)
check(p.data.customVariables.ampBountyRespawn.remaining==18,'offline time does not count')
bountyRespawn.Tick(4,'criminal',oldToken)
check(p.data.customVariables.ampBountyRespawn.remaining==18,'stale session timer ignored')
now=1017.9;bountyRespawn.Tick(4,'criminal',p.ampBountyToken)
check(not p.revived,'fractional remaining does not respawn early')
now=1018;bountyRespawn.Tick(4,'criminal',p.ampBountyToken)
check(p.revived==1 and not bountyRespawn.Pending(p),'expiry resurrects once')
check(p.data.stats.healthCurrent==100,'respawn restores durable living health')
bountyRespawn.Tick(4,'criminal',p.ampBountyToken)
check(p.revived==1,'timer replay no second resurrection')
bounty=3000;check(bountyRespawn.Begin(p),'second sentence')
now=1028;bountyRespawn.Tick(4,'criminal',p.ampBountyToken)
local persisted=copy(files['player/criminal.json'])
local q=player(4,'criminal');q.data=persisted;q.Resurrect=p.Resurrect;Players[4]=q
now=99999;bountyRespawn=dofile('server/scripts/bountyRespawn.lua');bountyRespawn.Resume(q)
check(q.data.customVariables.ampBountyRespawn.remaining==80,'restart restores remaining duration')
config.shareBounty=true
check(not bountyRespawn.Begin(player(5,'shared')),'shared crime is not forgiven for everyone')
print('Server regressions: '..checks..' assertions passed')
-- House turnover clears only this property's durable locks/receipts.
bootMarket()
check(playerVitrines.CommitHouse(1,{a={cell='Home',locked=true},b={cell='Other',price=10}}),'seed house locks')
check(playerVitrines.ClearHouseCell('Home'),'system house reset commits')
check(playerVitrines.HouseItems().a==nil and playerVitrines.HouseItems().b.price==10,'other property metadata retained')
bootMarket()
check(playerVitrines.HouseItems().a==nil,'house reset survives restart')

-- Exercise real JSON encoder/atomic writer against an in-memory Windows-like
-- filesystem: no overwrite rename, failed replacement, restart backup recovery.
package.path='server/lib/lua/?.lua;'..package.path
doesModuleExist=function() return false end
local disk={}
local failInstall=false
local fio={}
fio.open=function(path,mode)
    if mode=='r' then
        if disk[path]==nil then return nil end
        return {read=function() return disk[path] end,close=function() return true end}
    end
    local bytes=''
    return {write=function(_,s) bytes=bytes..s;return true end,
        flush=function() return true end,
        close=function() disk[path]=bytes;return true end}
end
fio.remove=function(path) disk[path]=nil;return true end
fio.rename=function(a,b)
    if failInstall and a:sub(-4)=='.tmp' then return nil end
    if disk[a]==nil or disk[b]~=nil then return nil end
    disk[b]=disk[a];disk[a]=nil;return true
end
tes3mp.ReplaceFileAtomic=false -- exercise legacy Windows fallback too
-- This mock fabricates functions for every absent key; explicitly disable the
-- new native IO surface when exercising the legacy writer.
tes3mp.ReadFileUtf8=false;tes3mp.WriteFileUtf8=false;tes3mp.GetFileIoError=false
local realJson=dofile('server/lib/lua/jsonInterface.lua')
realJson.setLibrary(fio)
check(realJson.saveAtomic('test.json',{v=1}),'atomic initial write')
check(realJson.saveAtomic('test.json',{v=2}),'Windows non-overwrite rename')
check(realJson.load('test.json').v==2,'new file decodes')
check(disk['/unused/test.json.bak']==nil,'successful replacement removes obsolete backup')
failInstall=true
check(not realJson.saveAtomic('test.json',{v=3}),'failed install reported')
check(realJson.load('test.json').v==2,'failed replacement preserves previous save')
failInstall=false
disk['/unused/test.json.bak']=disk['/unused/test.json'];disk['/unused/test.json']=nil
check(realJson.load('test.json').v==2,'interrupted Windows rename backup readable')
print('Including persistence regressions: '..checks..' assertions passed')
