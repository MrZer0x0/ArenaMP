-- H011 first-run regression. Run from ArenaMP_Build with Lua 5.1+.
package.path="server/scripts/?.lua;"..package.path
local base="tests/server/h011_tmp_data"
os.execute('mkdir -p "'..base..'/custom"')
local store={}
config={dataPath=base}
enumerations={log={INFO=1,WARN=2,ERROR=3}}
local notices={}
tes3mp={LogMessage=function(_,v) notices[#notices+1]=v end,
    GetOperatingSystemType=function() return "Linux" end}
tableHelper={deepCopy=function(source)
    local dest={}
    for k,v in pairs(source) do dest[k]=type(v)=="table" and tableHelper.deepCopy(v) or v end
    return dest
end}
customEventHooks={registerHandler=function() end}
local attempts={}
local library={open=function(path,mode)
    attempts[#attempts+1]=mode
    -- Unmatched missing READ attempts must never happen on first run.
    if mode=="r" and not io.open(path,"rb") then error("missing-file read attempted: "..path) end
    return io.open(path,mode)
end}
package.preload.jsonInterface=function() return {
    ioLibrary=library,
    load=function(relative)
        local f=assert(library.open(base.."/"..relative,"r"))
        local contents=f:read("*a");f:close()
        if contents:find('"marker":42',1,true) then return {marker=42} end
        if contents:find('"default":true',1,true) then return {default=true} end
        return nil
    end,
    save=function(relative, value)
        local f=assert(library.open(base.."/"..relative,"wb"))
        f:write(value.marker and '{"marker":42}' or '{"default":true}')
        f:close();return true
    end
} end
jsonInterface=require("jsonInterface")
local core=require("ampCore")
local f=assert(io.open(base.."/custom/arenaHouses.json","wb"))
f:write('{"marker":42}');f:close()
local data,created=core.EnsureJson("custom/arenaFurnitureSources.json",{default=true})
assert(created and data.default)
local original=assert(io.open(base.."/custom/arenaHouses.json","rb")):read("*a")
local loaded,createdExisting=core.EnsureJson("custom/arenaHouses.json",{default=true})
assert(not createdExisting and loaded.marker==42)
local after=assert(io.open(base.."/custom/arenaHouses.json","rb")):read("*a")
assert(original==after,"existing houses were overwritten")
local corrupt=assert(io.open(base.."/custom/mwScriptOverrides.json","wb"))
corrupt:write("{ invalid json }");corrupt:close()
local _,createdCorrupt=core.EnsureJson("custom/mwScriptOverrides.json",{default=true})
assert(not createdCorrupt)
local retained=assert(io.open(base.."/custom/mwScriptOverrides.json","rb")):read("*a")
assert(retained=="{ invalid json }","invalid existing file must not be overwritten")
local first=attempts[1]
assert(first=="a+","missing JSON must be created before any read probe")
core.EnsureStartupFiles()
for _,name in ipairs({"arenaFurnitureSources.json","groupHelper.json","profileTombstones.json",
    "groupMapMarkers.json","arenaHouses.json","mwScriptOverrides.json","cellResetTimers.json"}) do
    assert(io.open(base.."/custom/"..name,"rb"),"startup file missing: "..name)
end
assert(#notices>0)
os.execute('rm -rf "'..base..'"')
print("PASS H011 first-run: creates missing files before reads, preserves existing/corrupt data")
