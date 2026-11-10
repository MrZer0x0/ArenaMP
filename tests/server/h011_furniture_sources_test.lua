-- Regression: Windows account path with Cyrillic letters + explicit active mod path.
package.path="server/scripts/?.lua;"..package.path
local originalOpen=io.open
local dirname="tests/server/h011_tmp_Зэро"
os.execute('mkdir -p "'..dirname..'"')
local function u32(n)
    local out={}
    for i=1,4 do out[i]=string.char(n%256);n=math.floor(n/256) end
    return table.concat(out)
end
local function sub(tag,data) return tag..u32(#data)..data end
local function record(tag,data) return tag..u32(#data)..string.rep("\0",8)..data end
local filename="H011_TEST.esm"
local exact=dirname.."/"..filename
local f=assert(originalOpen(exact,"wb"))
f:write(record("TES3","")..record("STAT",sub("NAME","furn_chair_unicode\0")..
    sub("MODL","f\\furn_chair_01.nif\0")..sub("FNAM","Chair\0")))
f:close()
local usedUnicodeIo=false
package.preload.jsonInterface=function()
    return {ioLibrary={open=function(path,mode)
        if path==exact then usedUnicodeIo=true end
        return originalOpen(path,mode)
    end}, load=function(path)
        if path=="custom/arenaFurnitureSources.json" then
            return {dataDirs={"missing-mod-directory"},files={[filename]=exact}}
        end
    end}
end
tes3mp={GetOperatingSystemType=function() return "Windows" end}
io.open=function(path,mode)
    if path:find("Зэро",1,true) then return nil,"simulated Windows io Unicode failure" end
    return originalOpen(path,mode)
end
local auto=require("arenaFurnitureAutoCatalog")
local data,why=auto.build({furnitureDataDirs={"missing-mod-directory"}},
    {load=function()
        return {
            content = {
                { [filename] = {"0x00000000"} }
            }
        }
    end})
io.open=originalOpen
assert(data,why)
assert(usedUnicodeIo,"io2 Unicode-safe opener was not used")
local found=false
for _,item in ipairs(data.items) do if item.refId=="furn_chair_unicode" then found=true end end
assert(found,"the furniture model was not found via the explicit mod path")
os.remove(exact)
os.execute('rmdir "'..dirname..'"')
print("PASS H011 furniture: explicit active source and Unicode-safe Windows file access")
