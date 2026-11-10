package.path='server/scripts/?.lua;server/lib/lua/?.lua;'..package.path
require('utils');tableHelper=require('tableHelper');enumerations=require('enumerations')
local checks=0
local function check(v,label) assert(v,label);checks=checks+1 end
config={houseSystem={},dataPath='/unused'};package.loaded.config=config
local store={properties={A={cell='A',cells={'A'},owner='owner',instanceNaming='home',price=100}},doors={}}
local writes=0;local messages={};local cell='A - home';local clock=100
package.loaded.jsonInterface={load=function() return store end,save=function() writes=writes+1;return true end}
package.loaded.fileHelper={fixFilename=function(s) return s end}
package.loaded.inventoryHelper={}
package.loaded.packetBuilder={}
local handlers,validators={},{}
customEventHooks={registerHandler=function(k,f) handlers[k]=handlers[k] or {};table.insert(handlers[k],f) end,
 registerValidator=function(k,f) validators[k]=validators[k] or {};table.insert(validators[k],f) end,
 makeEventStatus=function(a,b) return {validDefaultHandler=a,validCustomHandlers=b} end}
customCommandHooks={registerCommand=function() end}
tes3mp=setmetatable({GetCell=function() return cell end,GetMillisecondsSinceServerStart=function() return clock*1000 end,
 SendMessage=function(_,msg) messages[#messages+1]=msg end}, {__index=function() return function() end end})
Players={[1]={accountName='guest',data={settings={},stats={level=50},timestamps={created=1}},IsLoggedIn=function() return true end}}
local time=os.time;os.time=function() return clock end
local h=dofile('server/scripts/houseHelper.lua')
local function interest(target,dest)
 local msg='@@AMP_HOUSE@@INTEREST\t'..target..(dest and '\t'..dest or '')
 for _,fn in ipairs(validators.OnPlayerSendMessage) do fn({},1,msg) end
end
local function count(prefix) local n=0;for _,m in ipairs(messages) do if m:find(prefix,1,true) then n=n+1 end end;return n end
interest(cell,'A')
check(count('CELL_STATE\t')==1 and count('CELL_ACK\t')==1,'first interest receives one snapshot and acknowledgement')
local firstMessages=#messages
for i=1,100 do clock=clock+1;interest(cell,'A') end
check(count('CELL_STATE\t')==1 and count('CELL_ACK\t')==101,'100 unchanged retries acknowledged without full state rebuild')
interest(cell,'B');check(count('CELL_STATE\t')==2,'new destination triggers one updated snapshot')
local before=#messages;interest('Wrong Cell','C');check(#messages==before,'stale cell interest ignored')
clock=clock+1;before=count('CELL_ACK\t')
for i=1,100 do interest(cell,'A') end
check(count('CELL_ACK\t')-before==8,'burst limiter retained')
check(writes==0 and store.doors.B==nil,'read requests do not modify persistent house registry')
-- Duplicate full instance delivery is suppressed only for the active visit.
local replays=0
local c={description=cell,data={lastVisit={guest=0}},visitors={1},initialDataSentTo={}}
c.LoadInitialCellData=function(self,pid) replays=replays+1;self.initialDataSentTo[pid]=Players[pid] end
LoadedCells={[cell]=c}
h.RefreshInstanceCell(1,cell)
h.RefreshInstanceCell(1,cell)
check(replays==1,'cell-change refresh cannot duplicate initial replay')
c.initialDataSentTo[1]=nil;h.RefreshInstanceCell(1,cell)
check(replays==2,'new visit receives one full replay')
Players[1]={accountName='other',data={settings={}},IsLoggedIn=function() return true end}
h.RefreshInstanceCell(1,cell);check(replays==3,'reused player pid receives current session replay')
c.initialDataSentTo[1]=nil;c.visitors={};h.RefreshInstanceCell(1,cell)
check(replays==3,'refresh waits for visitor registration and generated records')
os.time=time
print('PASS Y100 housing: '..checks..' assertions')
