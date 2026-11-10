package.path='server/lib/lua/?.lua;'..package.path
local checks=0
local function check(v,label) assert(v,label);checks=checks+1 end
tableHelper=require('tableHelper');logicHandler={}
local f=assert(io.open('server/scripts/logicHandler.lua'));local code=f:read('*a');f:close()
for _,name in ipairs({'GetLowestPingPid','LoadCellForPlayer','LoadRegionForPlayer','UnloadRegionForPlayer'}) do
 local block=assert(code:match('(logicHandler%.'..name..' = function.-\nend)'))
 assert(load(block))()
end
Players={[1]={IsLoggedIn=function() return true end},[2]={IsLoggedIn=function() return true end}}
local queried={};local pings={[1]=60,[2]=20}
tes3mp={GetAvgPing=function(pid) assert(Players[pid],'native ping queried for disconnected player');queried[#queried+1]=pid;return pings[pid] end,LogMessage=function() end,LogAppend=function() end}
enumerations={log={WARN=2,INFO=1}}
config={stableCellAuthority=true,pingDifferenceRequiredForAuthority=50}
bountyRespawn={Pending=function(player) return player and player.locked end}
check(logicHandler.GetLowestPingPid({3,1,2})==2 and #queried==2,'stale IDs skipped in authority selection')
pings[2]=-1;check(logicHandler.GetLowestPingPid({3,1,2})==1,'negative native ping cannot win authority')
Players[2].locked=true;check(logicHandler.GetLowestPingPid({2,3})==nil,'no valid authority returns nil')
local authority=3;local c={visitors={3},isResetting=false}
c.AddVisitor=function(self,pid) self.visitors[#self.visitors+1]=pid end
c.GetAuthority=function() return authority end
c.SetAuthority=function(_,pid) assert(pid~=nil);authority=pid end
c.LoadActorAuthority=function() end
LoadedCells={Home=c};logicHandler.LoadCell=function() end;logicHandler.GetChatName=tostring
logicHandler.LoadCellForPlayer(1,'Home');check(authority==1,'cell with disconnected authority gets current visitor')
local r={authority=3,visitors={3}}
WorldInstance={storedRegions={Region=r},AddRegionVisitor=function(_,pid) r.visitors[#r.visitors+1]=pid end,
 GetRegionAuthority=function() return r.authority end,SetRegionAuthority=function(_,pid) assert(pid~=nil);r.authority=pid end,
 LoadRegionWeather=function() end,AddForcedWeatherUpdatePid=function() end,
 RemoveRegionVisitor=function(_,pid) tableHelper.removeValue(r.visitors,pid) end}
queried={};logicHandler.LoadRegionForPlayer(1,'Region',false)
check(r.authority==1 and #queried==0,'disconnected region authority replaced without invalid native ping')
r.visitors={3,1};r.authority=1
logicHandler.UnloadRegionForPlayer(1,'Region')
check(r.authority==nil,'region with only stale visitors does not set nil native authority')
print('PASS Y100 authority: '..checks..' assertions')
