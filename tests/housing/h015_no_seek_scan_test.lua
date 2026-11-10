package.path="server/scripts/?.lua;"..package.path
local auto=require("arenaFurnitureAutoCatalog")
local function u32(n)
    local bytes={}
    for i=1,4 do bytes[i]=string.char(n%256);n=math.floor(n/256) end
    return table.concat(bytes)
end
local function sub(k,value)return k..u32(#value)..value end
local record='STAT'..u32(0)..string.rep('\0',8)
local payload=sub('NAME','furn_chair_safe\0')..sub('MODL','f\\furn_chair_safe.nif\0')..sub('FNAM','Test chair\0')
local bytes=record..'TES3'..u32(256)..string.rep('\0',8)..string.rep('x',256)..'STAT'..u32(#payload)..string.rep('\0',8)..payload
local open=io.open
io.open=function(path,mode)
    if path~='virtual.esm' then return open(path,mode) end
    local obj={pos=1}
    function obj:read(size)
        if self.pos>#bytes then return nil end
        local data=bytes:sub(self.pos,self.pos+size-1);self.pos=self.pos+#data;return data
    end
    function obj:seek()error('io2 does not implement seek')end
    function obj:close()end
    return obj
end
local records={}
local count,err=auto.scanFile('virtual.esm',records)
io.open=open
assert(count==3 and records.furn_chair_safe,err or 'scanner failed')
print('PASS H015 file reader: no-seek stream, chunk skipping and record parsing')
