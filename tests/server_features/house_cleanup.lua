-- Exercise the real house state machine with synthetic cells, never player data.
local checks=0
local function check(v,msg) assert(v,msg);checks=checks+1 end
local function copy(v) if type(v)~='table' then return v end local r={} for k,x in pairs(v) do r[k]=copy(x) end return r end
local handlers,validators={},{}
customEventHooks={registerHandler=function(n,f) handlers[n]=f end,registerValidator=function(n,f) validators[n]=f end,
 makeEventStatus=function(a,b) return {validDefaultHandler=a,validCustomHandlers=b} end}
customCommandHooks={registerCommand=function() end}
local cell='House - home'
local data={properties={House={owner='owner',instanceNaming='home',kind='house',mode='instance',price=1000,locked=true,cells={'House'}}},purged={[cell]=true},doors={}}
local snapshots={}
jsonInterface={load=function(path) if path=='custom/arenaHouses.json' then return copy(data) end end,
 save=function(path,v) snapshots[path]=copy(v);return true end}
config={dataPath='/unused',houseCellNameEncoding='utf8',recordStoreLoadOrder={}}
package.loaded.config=config;package.loaded.jsonInterface=jsonInterface
package.loaded.fileHelper={fixFilename=function(s) return s end}
package.loaded.inventoryHelper={};package.loaded.packetBuilder={}
local current=cell
local resetsSent,teleports={},{}
tes3mp=setmetatable({GetCell=function() return current end,SetCell=function(pid,c) current=c;teleports[#teleports+1]=c end,
 SendCellReset=function(pid) resetsSent[pid]=true end,GetDataPath=function() error('house reset must not inspect/restore backups') end},
 {__index=function() return function() end end})
enumerations={log={INFO=1,WARN=2,ERROR=3,VERBOSE=4},container={SET=0},containerSub={REPLY_TO_REQUEST=1}}
color={Yellow='',White=''}
Players={[1]={name='owner',accountName='owner',language='EN',data={stats={level=20}},cellsLoaded={cell},IsLoggedIn=function() return true end,Message=function() end},
 [2]={name='stranger',accountName='stranger',cellsLoaded={},IsLoggedIn=function() return true end}}
tableHelper={deepCopy=copy,containsValue=function(t,x) for _,v in pairs(t or {}) do if v==x then return true end end return false end}
local saved,loaded,requested=0,0,0
local function room(full)
 return {description=cell,visitors={1},data={lastVisit={owner=9999},loadState={hasFullContainerData=full},
 packets={container={'11-0','0-9'}},objectData={['11-0']={inventory={{refId='bottle',count=5}}},['0-9']={inventory={{refId='sword',count=1}}}}},
 HasFullContainerData=function(self) return self.data.loadState.hasFullContainerData end,
 EnsureFullContainerData=function() requested=requested+1 end,Save=function() saved=saved+1 end,
 LoadContainers=function() loaded=loaded+1 end,EnsurePacketTables=function(self) self.data.packets.container={} end,
 ClearRecordLinks=function() end}
end
LoadedCells={[cell]=room(true)}
houseHelper=dofile('server/scripts/houseHelper.lua')
check(houseHelper.MarkForPurge(cell),'legacy true marker must be repaired')
check(houseHelper.IsPurgePending(cell),'repair blocks pickups')
check(not houseHelper.IsPurgeDelete(2,cell,'42-0',{}),'no permission')
check(not houseHelper.IsPurgeDelete(1,cell,'0-9',{}),'placed items never belong to initial sweep')
check(houseHelper.IsPurgeDelete(1,cell,'42-0',{}),'owner may sweep original reference')
check(not houseHelper.AcknowledgePurge(cell,1,1),'ack before accepted deletion cannot complete')
check(#LoadedCells[cell].data.objectData['11-0'].inventory==0,'preloaded authored container cleared')
check(#LoadedCells[cell].data.objectData['0-9'].inventory==1,'placed container content retained')
check(loaded==1,'empty authoritative contents sent to visitors')
check(LoadedCells[cell].data.housePurgeBackup['11-0'][1].count==5,'legacy original-container contents recoverable after migration')
handlers.OnObjectDelete({validDefaultHandler=false},1,cell,{['42-0']={}})
check(houseHelper.IsPurgePending(cell),'rejected deletion cannot complete purge')
handlers.OnObjectDelete({validDefaultHandler=true},1,cell,{['42-0']={}})
check(not houseHelper.IsPurgePending(cell),'accepted late deletion completes purge')
check(snapshots['custom/arenaHouses.json'].purged[cell]==2,'completion stored immediately')
check(not houseHelper.MarkForPurge(cell),'completed cleanup is not repeated')
local prop=houseHelper.GetProperty('House')
periodicCellResets={fullResetCell=function() return false,'busy' end}
check(not houseHelper.Sell(1,'House'),'failed reset cancels sale')
check(prop.owner=='owner','failed sale retains ownership')
periodicCellResets.fullResetCell=function() error('I/O failure') end
check(not houseHelper.Release('House'),'exception cancels release')
check(prop.owner=='owner','exception retains ownership')
local cleared=false
playerVitrines={ClearHouseCell=function(c) cleared=c==cell;return true end}
RecordStores={}
periodicCellResets.fullResetCell=function() return true end
check(houseHelper.Release('House'),'successful release')
check(cleared and prop.owner==nil,'cleared market metadata and ownership')
-- Same instance description reused after repurchase: handshake must restart.
prop.owner='owner';prop.instanceNaming='home';prop.locked=true
LoadedCells[cell]=room(false)
check(houseHelper.MarkForPurge(cell),'repurchase starts new cleanup')
check(not houseHelper.AcknowledgePurge(cell,1,0) and requested==1,'missing container snapshot requested')
check(houseHelper.PreparePurchasedContainers(LoadedCells[cell],0,1),'new full snapshot prepared')
LoadedCells[cell].data.loadState.hasFullContainerData=true
check(houseHelper.TryFinalizePurge(cell),'new empty room completes once snapshot ready')
-- Exercise actual reset helper, including cached clients and backup bypass.
package.loaded.houseHelper=houseHelper
logicHandler={UnloadCell=function(c) LoadedCells[c]=nil end,LoadCell=function(c) LoadedCells[c]=room(true) end}
time={seconds=function(v) return v*1000 end}
local resets=dofile('server/scripts/resetHelrer.lua')
LoadedCells[cell]=room(true)
LoadedCells[cell].data.objectData['0-20']={refId='dropped_sword'}
local old=LoadedCells[cell]
check(resets.fullResetCell(cell,true),'forced sale reset works')
check(next(old.data.objectData)==nil and next(old.data.lastVisit)==nil,'world objects and visitor cache cleared')
check(LoadedCells[cell]==nil,'live cell cannot write back old furniture')
check(resetsSent[1] and resetsSent[2],'all clients invalidate cached instance')
check(#teleports==1 and teleports[1]~=cell,'seller not returned to released home')
print('House cleanup: '..checks..' assertions passed')
