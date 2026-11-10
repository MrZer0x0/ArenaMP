-- Windows io2 doesn't offer a regular io.file:seek. Simulate its documented
-- open/read/close subset, including paths with Cyrillic (real IO here is UTF-8).
package.path='server/scripts/?.lua;'..package.path
local base='tests/server/h014_io2_tmp'
os.execute('mkdir -p "'..base..'/custom"')
local lib={open=function(path,mode)
    local f=io.open(path,mode)
    if not f then return nil end
    return {read=function(_,format) assert(format=='*all');return f:read('*a') end,
            close=function() f:close() end,
            write=function(_,contents) f:write(contents) end}
end}
config={dataPath=base}
enumerations={log={INFO=1,WARN=2,ERROR=3}}
local notes={}
tes3mp={LogMessage=function(_,s) notes[#notes+1]=s end,
 GetOperatingSystemType=function()return 'Windows' end}
tableHelper={deepCopy=function(src)
 local t={} for k,v in pairs(src) do t[k]=type(v)=='table' and tableHelper.deepCopy(v) or v end return t
end}
customEventHooks={registerHandler=function()end}
jsonInterface={ioLibrary=lib,
 load=function(name)
  local f=assert(lib.open(base..'/'..name,'r'))
  local s=f:read('*all');f:close()
  if s:find('"survived":true',1,true) then return {survived=true} end
  if s:find('"created":true',1,true) then return {created=true} end
  return nil
 end,
 save=function(name, value)
  local f=assert(lib.open(base..'/'..name,'w+b'))
  f:write(value.created and '{"created":true}' or '{"survived":true}')
  f:close();return true
 end}
local core=require('ampCore')
local data,created=core.EnsureJson('custom/arenaHouses.json',{created=true})
assert(created and data.created)
local file=assert(io.open(base..'/custom/arenaHouses.json','wb'))
file:write('{"survived":true}');file:close()
data,created=core.EnsureJson('custom/arenaHouses.json',{created=true})
assert(not created and data.survived)
file=assert(io.open(base..'/custom/arenaHouses.json','wb'))
file:write('{corrupted and private}');file:close()
data,created=core.EnsureJson('custom/arenaHouses.json',{created=true})
assert(not created and data.created)
file=assert(io.open(base..'/custom/arenaHouses.json','rb'))
assert(file:read('*a')=='{corrupted and private}');file:close()
core.EnsureStartupFiles()
for _,name in ipairs({'arenaFurnitureSources.json','arenaFurnitureManual.json','arenaFurnitureGenerated.json',
    'groupHelper.json','profileTombstones.json','groupMapMarkers.json','mwScriptOverrides.json',
    'cellResetTimers.json'}) do
    assert(io.open(base..'/custom/'..name,'rb'),'missing '..name)
end
for _,msg in ipairs(notes) do assert(not msg:find('Cannot inspect',1,true)) end
os.execute('rm -rf "'..base..'"')
print('PASS H014 io2 compatibility: no seek, missing/valid/corrupt JSON, startup preflight')
