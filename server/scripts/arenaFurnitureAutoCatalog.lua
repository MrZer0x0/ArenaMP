-- ArenaMP H003: automatic, server-verified furniture catalogue from the ACTUAL
-- active TES3 ESM/ESP files. No player-supplied IDs, models, prices or whitelist.
-- Read only record headers and candidate record payloads: no binary libraries or
-- external programs, compatible with the Lua runtime on Android and Windows.
local M = {}
local function u32(s, p)
    local a,b,c,d = s:byte(p,p+3)
    if not d then return nil end
    return a + b*256 + c*65536 + d*16777216
end
local function field(s)
    return (s or ""):match("^[^%z]*") or ""
end
local function lower(s) return (s or ""):lower() end
local function toUtf8(cp)
    if cp<128 then return string.char(cp) end
    if cp<2048 then return string.char(192+math.floor(cp/64),128+cp%64) end
    return string.char(224+math.floor(cp/4096),128+math.floor(cp/64)%64,128+cp%64)
end
local function cp1251ToUtf8(name)
    local out={}
    for i=1,#name do
        local n=name:byte(i)
        local code=n<128 and n or (n>=192 and (0x410+n-192))
        if n==168 then code=0x401 elseif n==184 then code=0x451
        elseif n==171 then code=171 elseif n==187 then code=187
        elseif n==185 then code=0x2116 elseif n==150 then code=0x2013
        elseif n==151 then code=0x2014 end
        if not code then return nil end
        out[#out+1]=toUtf8(code)
    end
    return table.concat(out)
end
local function safeName(name, id)
    name=field(name)
    if name=="" or #name>130 or name:find("[%c\t\r\n;\\]") then return id end
    -- An ESM may contain legacy CP1251/CP1252 names. Never send malformed
    -- bytes to the UTF-8 MyGUI client; the refId is always ASCII-safe.
    if name:find("[\128-\255]") then
        local i=1
        while i<=#name do
            local c=name:byte(i)
            if c<128 then i=i+1
            else
                local count=(c>=194 and c<=223 and 2) or (c>=224 and c<=239 and 3) or (c>=240 and c<=244 and 4)
                if not count or i+count-1>#name then
                    return M.nameEncoding=="cp1251" and (cp1251ToUtf8(name) or id) or id
                end
                for n=i+1,i+count-1 do
                    local x=name:byte(n)
                    if x<128 or x>191 then
                        return M.nameEncoding=="cp1251" and (cp1251ToUtf8(name) or id) or id
                    end
                end
                i=i+count
            end
        end
    end
    return name
end
local function has(hay, tokens)
    for _,term in ipairs(tokens) do if hay:find(term,1,true) then return true end end
    return false
end
local deny = {"door","portal","wall","floor","ceiling","roof","stair","pillar",
    "column","bridge","building","house_","interior","exterior","terrain",
    "landscape","foundation","cave","cliff","rock","boulder","window","gate",
    "lever","switch","trap","quest","script","marker","collision","invis",
    "furn_de_wooden_door","alchemy_app","ingredient","leveled","spawn","teleport"}
local themes = {
    {"Столы", {"table","desk","counter","bar_","furn_bar","стол"},180},
    {"Сиденья", {"chair","bench","stool","throne","seat","скам","стул"},110},
    {"Кровати", {"bed","bunk","cot","mattress","кроват"},220},
    {"Шкафы и полки", {"shelf","bookshelf","wardrobe","closet","cabinet","cupboard","dresser","sideboard","шкаф","полк"},210},
    {"Ковры и текстиль", {"rug","carpet","tapestry","curtain","banner","ковер","ковёр"},85},
    {"Растения", {"plant","flowerpot","potted","bonsai","planter","bush","shrub","fern","куст","растен"},75},
    {"Декор", {"vase","statue","sculpt","painting","picture","display","decor","furnish","urn","bowl","ornament","mirror","перегород"},95}
}
local function classify(record)
    local id, model=lower(record.refId),lower(record.model):gsub("/","\\")
    if #id>120 or not id:match("^[%w_%.%- ]+$") or #model>250 or model=="" or not model:match("%.nif$") then return nil end
    -- deny even if a token such as 'table' happens to occur in a building mesh.
    if has(id.." "..model, deny) then return nil end
    if record.script and record.script~="" then return nil end
    local hay=id.." "..model.." "..lower(record.name)
    local category,base
    if record.kind=="CONT" then
        -- NPCO references are base-template loot. Placing a purchasable chest
        -- with this inventory would mint free items on every purchase.
        if record.hasContents then return nil end
        if record.flags and record.flags%2==1 then return nil end -- organic
        if record.flags and math.floor(record.flags/2)%2==1 then return nil end -- respawning
        if not has(hay,{"chest","crate","barrel","sack","basket","trunk","strongbox","coffer","furn","wardrobe","cabinet","closet","drawer","сундук","ящик","бочка","шкаф"}) then return nil end
        category,base="Контейнеры",160
    elseif record.kind=="LIGH" then
        -- Carryable lights can be taken from the world and vendor-sold; the
        -- furniture shop sells stationary light fixtures only.
        if record.flags and math.floor(record.flags/2)%2==1 then return nil end
        if not has(hay,{"lamp","lantern","candle","chandelier","sconce","brazier","torch","light","свет","фонар"}) then return nil end
        category,base="Освещение",100
    elseif record.kind=="STAT" or record.kind=="ACTI" then
        if record.kind=="ACTI" and not has(hay,{"furn","chair","bed","table","fireplace","decor","lamp","rug","bench"}) then return nil end
        if has(hay,{"fireplace","hearth","furn_fire"}) then category,base="Камины",300
        else
            for _,theme in ipairs(themes) do
                if has(hay,theme[2]) then category,base=theme[1],theme[3];break end
            end
        end
    elseif record.kind=="MISC" then
        -- Ordinary MISC records are loot, not static decor. Selling a cheap
        -- generated copy to NPCs must not become a gold/rare-item printer.
        if record.value and record.value>100 then return nil end
        if not has(hay,{"furn","vase","ornament","pillow","cushion","decor","tapestry","painting"}) then return nil end
        category,base="Декор",70
    end
    if not category then return nil end
    -- Deterministic, bounded economy from record type, model/class, weight,
    -- capacity and native item value. Never use arbitrary JSON prices.
    local variation=0
    for i=1,#id do variation=(variation*33+id:byte(i))%53 end
    local price=base+variation*2
    if has(hay,{"large","big","double","grand","royal"}) then price=math.floor(price*1.6) end
    if has(hay,{"small","tiny","simple","poor"}) then price=math.floor(price*0.8) end
    if has(hay,{"dwemer","daedric","ebony","glass","exquisite","golden"}) then price=math.floor(price*2.3) end
    if record.capacity then price=price+math.min(250, math.max(0,math.floor(record.capacity))*2) end
    if record.value then price=math.max(price, math.min(2500, math.max(0,record.value)*2)) end
    return {refId=record.refId,name=safeName(record.name,record.refId),category=category,price=math.max(25,math.min(5000,price))}
end
M.classify=classify
local function parseSubrecords(raw)
    local fields, p={},1
    while p+7<=#raw do
        local tag=raw:sub(p,p+3)
        local size=u32(raw,p+4)
        if not size or size>#raw-p-7 then return nil end
        if tag=="NPCO" or tag=="DELE" then fields[tag]=true end
        if tag=="NAME" or tag=="MODL" or tag=="FNAM" or tag=="SCRI" or tag=="CNDT" or tag=="FLAG" or tag=="LHDT" or tag=="MCDT" then
            fields[tag]=raw:sub(p+8,p+7+size)
        end
        p=p+8+size
    end
    if p~=#raw+1 then return nil end
    return fields
end
local function entry(tag,f)
    local id=field(f.NAME)
    if id=="" then return nil end
    local r={kind=tag,refId=id,model=field(f.MODL),name=field(f.FNAM),script=field(f.SCRI)}
    if f.DELE then r.deleted=true end
    if tag=="CONT" then
        r.hasContents=(f.NPCO==true)
        -- CNDT is an IEEE-754 float holding maximum inventory weight.
        local x=f.CNDT or ""
        local b1,b2,b3,b4=x:byte(1,4)
        if b4 then
            local bits=b1+b2*256+b3*65536+b4*16777216
            local exp=math.floor(bits/8388608)%256
            if exp>0 and exp<255 then
                local capacity=(1+(bits%8388608)/8388608)*2^(exp-127)
                if capacity>0 and capacity<100000 then r.capacity=capacity end
            end
        end
        r.flags=u32(f.FLAG or "",1)
    elseif tag=="LIGH" and f.LHDT and #f.LHDT>=24 then
        r.value=u32(f.LHDT,5);r.flags=u32(f.LHDT,21)
    elseif tag=="MISC" and f.MCDT and #f.MCDT>=8 then r.value=u32(f.MCDT,5) end
    return r
end
local wanted={STAT=true,CONT=true,ACTI=true,LIGH=true,MISC=true,WEAP=true,ARMO=true,CLOT=true,ALCH=true,INGR=true,BOOK=true,APPA=true,LOCK=true,PROB=true,REPA=true}
-- Standard Lua io cannot open Unicode paths on Windows. Use TES3MP's io2
-- only for those paths; it supports Windows Unicode paths and binary reads.
-- Avoid io2 probes for missing ordinary paths: it writes a diagnostic itself.
local function openBinary(path)
    local f, err = io.open(path, "rb")
    if f then return f end
    if type(path)=="string" and path:find("[\128-\255]") and
        tes3mp and tes3mp.GetOperatingSystemType and
        tes3mp.GetOperatingSystemType()=="Windows" then
        local lib = require("jsonInterface").ioLibrary
        if lib and lib.open then
            local ok, unicodeFile = pcall(lib.open, path, "rb")
            if ok and unicodeFile then return unicodeFile end
        end
    end
    return nil, err
end
function M.scanFile(path, records, rawRecords, rawRequested)
    local f,err=openBinary(path)
    if not f then return nil,err end
    local count=0
    -- Unicode Windows paths may use io2, whose file object does not implement
    -- Lua's seek(). Never depend on seek to read the active plug-ins.
    while true do
        local header=f:read(16)
        if header=="" or header==nil then break end
        if #header~=16 then f:close();return nil,"truncated record header" end
        local tag=header:sub(1,4)
        local size=u32(header,5)
        if not size or size>134217728 then f:close();return nil,"invalid record size" end
        if wanted[tag] then
            if size>1048576 then f:close();return nil,"candidate record too large" end
            local raw=f:read(size)
            if not raw or #raw~=size then f:close();return nil,"truncated record payload" end
            local sub=parseSubrecords(raw)
            if not sub then f:close();return nil,"invalid subrecords" end
            local candidate=entry(tag,sub)
            -- Y090: raw records also serve house-item pickup/price/move actions.
            -- TES3 item IDs may contain punctuation (bungler's bane, [unique]).
            -- Do not apply the furniture-shop ID whitelist to that lookup:
            -- classify()/manualCandidate() retain their own stricter checks below.
            -- Control bytes cannot be carried by the tab/newline UI protocol.
            if candidate and not candidate.refId:find("[%c]") then
                -- Last active plugin wins, including a later override that
                -- turns previously safe furniture into a scripted record.
                local key=lower(candidate.refId)
                if rawRecords and (not rawRequested or rawRequested[key]) then
                    rawRecords[key]=(not candidate.deleted and candidate) or false
                end
                records[lower(candidate.refId)]=(not candidate.deleted and classify(candidate)) or false
            end
        else
            -- A bounded chunk drain also works on io2 without seek. No full
            -- ESM/ESP is buffered into memory, even for multi-gigabyte mods.
            local remaining=size
            while remaining>0 do
                local chunk=f:read(math.min(65536,remaining))
                if not chunk or #chunk==0 then f:close();return nil,"truncated record" end
                remaining=remaining-#chunk
            end
        end
        count=count+1
        if count>3000000 then f:close();return nil,"record-count limit" end
    end
    f:close()
    return count
end
-- Manual capture may include a new unclassified prop, but never architecture,
-- scripted objects, loot-bearing containers, or removed records. Use this only
-- after staff authorization and a reference verified against the current cell.
function M.manualCandidate(record)
    if not record or record.deleted or not record.refId or not record.model then return nil end
    local id,model=lower(record.refId),lower(record.model):gsub("/","\\")
    if #id>120 or #model>250 or not id:match("^[%w_%.%- ]+$") or
       not model:match("%.nif$") or has(id.." "..model,deny) or
       (record.script and record.script~="") or record.hasContents or
       (record.kind~="STAT" and record.kind~="CONT" and record.kind~="ACTI" and record.kind~="LIGH") then return nil end
    if record.kind=="CONT" and record.flags and record.flags%4~=0 then return nil end
    return {refId=record.refId,name=safeName(record.name,record.refId),
        category="Декор",price=200}
end
local function sourceDirs(cfg)
    local dirs={}
    local function add(v) if type(v)=="string" and #v>0 and #v<1024 then dirs[#dirs+1]=v end end
    for _,v in ipairs((cfg and cfg.furnitureDataDirs) or {}) do add(v) end
    -- Precreated by ampCore.EnsureStartupFiles before furniture is required.
    local ok, sources=pcall(function()
        return require("jsonInterface").load("custom/arenaFurnitureSources.json")
    end)
    if not ok or type(sources)~="table" then sources={dataDirs={}} end
    if type(sources.dataDirs)=="table" then
        for _,v in ipairs(sources.dataDirs) do add(v) end
    end
    if os.getenv then add(os.getenv("ARENAMP_DATA_FILES")) end
    add("Data Files");add("../Data Files");add("../../Data Files")
    return dirs, type(sources.files)=="table" and sources.files or {}
end
local function locate(name,dirs,files)
    if type(name)~="string" or #name>200 or not lower(name):match("%.es[mp]$") or
        name:find("..",1,true) or name:find("/",1,true) or name:find("\\",1,true) or
        name:find(":",1,true) or name:find("[%c]") then return nil end
    -- Exact paths come from the launcher's CRC32-verified active selection.
    -- A plugin in the second Data Files directory must not be shadowed by a
    -- same-named but different plugin in the first directory.
    local exact = files and files[name]
    if type(exact)=="string" and #exact<2048 and
        lower(exact:sub(-#name))==lower(name) then
        local handle=openBinary(exact)
        if handle then handle:close();return exact end
    end
    for _,dir in ipairs(dirs) do
        local path=dir:gsub("[/\\]+$","").."/"..name
        local file=openBinary(path)
        if file then file:close();return path end
        -- Classic Morrowind data folders may have different case on disk.
        -- On case-sensitive systems prefer exact filenames; do not execute shell.
    end
    return nil
end
function M.build(cfg, json, rawRequested)
    local manifest=json.load("requiredDataFiles.json")
    if type(manifest)~="table" then return nil,"requiredDataFiles.json unavailable" end
    local entries=manifest.content or manifest
    if type(entries)~="table" or #entries==0 then return nil,"empty active mod list" end
    M.nameEncoding=(cfg and cfg.furnitureNameEncoding)=="id" and "id" or "cp1251"
    local dirs, files=sourceDirs(cfg)
    local records={}
    local raw=rawRequested and {} or nil
    for _,v in ipairs(entries) do
        if type(v)~="table" then return nil,"invalid active mod entry" end
        local name=next(v)
        local path=locate(name,dirs,files)
        if not path then return nil,"active ESM/ESP not readable: "..tostring(name) end
        local count,err=M.scanFile(path,records,raw,rawRequested)
        if not count then return nil,"error reading "..name..": "..tostring(err) end
    end
    local items={}
    for _,record in pairs(records) do if record then items[#items+1]=record end end
    table.sort(items,function(a,b) return lower(a.refId)<lower(b.refId) end)
    return {items=items,source="active-esm-esp",version=3}, nil, raw
end
-- Only the active master files can be used for /addhome. Read TES3 CELL
-- headers/subrecords in bounded chunks; never load a whole ESM into memory.
-- This shares the exact verified mod-path lookup used by the furniture catalog.
local function cellNameUtf8(bytes, encoding)
    local name=field(bytes)
    if name=="" or #name>255 or name:find("[%z\r\n\t]") then return nil end
    -- The Russian masters normally use CP1251. Valid UTF-8 masters need no
    -- conversion. A manually forced CP1251 setting handles ambiguous input.
    if encoding=="cp1251" then return cp1251ToUtf8(name) end
    local i, valid=1,true
    while i<=#name do
        local b=name:byte(i)
        if b<128 then i=i+1 else
            local n=(b>=194 and b<=223 and 2) or (b>=224 and b<=239 and 3) or
                (b>=240 and b<=244 and 4)
            if not n or i+n-1>#name then valid=false;break end
            for j=i+1,i+n-1 do
                local c=name:byte(j)
                if c<128 or c>191 then valid=false;break end
            end
            if not valid then break end
            i=i+n
        end
    end
    if valid then return name end
    return cp1251ToUtf8(name)
end
local function skipBytes(f, count)
    -- io2 cannot seek. Both regular io and io2 support read(n).
    while count>0 do
        local part=f:read(math.min(count,65536))
        if not part or #part==0 then return false end
        count=count-#part
    end
    return true
end
local function scanMasterCells(path, plugin, cells, encoding)
    local f,err=openBinary(path)
    if not f then return nil,err end
    local records=0
    while true do
        local header=f:read(16)
        if not header or header=="" then break end
        local size=header and u32(header,5)
        if #header~=16 or not size or size>134217728 then
            f:close();return nil,"invalid TES3 record header"
        end
        if header:sub(1,4)~="CELL" then
            if not skipBytes(f,size) then f:close();return nil,"truncated TES3 record" end
        else
            local left=size
            local name,flags,inReferences,deleted=nil,nil,false,(math.floor((u32(header,13) or 0)/32)%2==1)
            while left>0 do
                if left<8 then f:close();return nil,"truncated CELL subrecord" end
                local sub=f:read(8)
                if not sub or #sub~=8 then f:close();return nil,"truncated CELL subrecord header" end
                left=left-8
                local length=u32(sub,5)
                if not length or length>left then f:close();return nil,"invalid CELL subrecord size" end
                local tag=sub:sub(1,4)
                if tag=="NAME" and not inReferences and name==nil and length<=256 then
                    local bytes=f:read(length)
                    if not bytes or #bytes~=length then f:close();return nil,"truncated CELL name" end
                    name=cellNameUtf8(bytes,encoding)
                elseif tag=="DATA" and not inReferences and length>=4 and length<=64 then
                    local raw=f:read(length)
                    if not raw or #raw~=length then f:close();return nil,"truncated CELL DATA" end
                    flags=u32(raw,1)
                elseif tag=="FRMR" then
                    inReferences=true
                    if not skipBytes(f,length) then f:close();return nil,"truncated CELL reference" end
                elseif tag=="DELE" and not inReferences then
                    deleted=true
                    if not skipBytes(f,length) then f:close();return nil,"truncated CELL deletion" end
                elseif not skipBytes(f,length) then
                    f:close();return nil,"truncated CELL payload"
                end
                left=left-length
            end
            -- A CELL override can convert an interior into an exterior or delete
            -- it. Last master wins; never register exterior or deleted cells.
            if name then
                cells[name:lower()] = (not deleted and flags and flags%2==1) and
                    {name=name,plugin=plugin} or false
            end
        end
        records=records+1
        if records>3000000 then f:close();return nil,"TES3 record-count limit" end
    end
    f:close()
    return records
end

function M.findActiveEsmInteriors(cfg, json)
    local ok,manifest=pcall(json.load,"requiredDataFiles.json")
    if not ok or type(manifest)~="table" then return nil,"requiredDataFiles.json unavailable" end
    local entries=manifest.content or manifest
    if type(entries)~="table" or #entries==0 then return nil,"empty active mod list" end
    local dirs,files=sourceDirs(cfg)
    local cells,masters,counts={},0,{}
    local encoding=cfg and cfg.houseCellNameEncoding
    for _,entry in ipairs(entries) do
        local plugin=type(entry)=="table" and next(entry) or nil
        if type(plugin)~="string" then return nil,"invalid active mod entry" end
        if plugin:lower():match("%.esm$") then
            local path=locate(plugin,dirs,files)
            if not path then return nil,"active ESM unavailable: "..plugin end
            local count,err=scanMasterCells(path,plugin,cells,encoding)
            if not count then return nil,"failed to parse "..plugin..": "..tostring(err) end
            masters=masters+1
            counts[#counts+1]=plugin
        end
    end
    if masters==0 then return nil,"no active ESM found" end
    local interiors={}
    for _,v in pairs(cells) do if v then interiors[#interiors+1]=v end end
    table.sort(interiors,function(a,b) return a.name:lower()<b.name:lower() end)
    return interiors,nil,counts
end

function M.findActiveRecords(cfg,jsonInterface,requested)
    if type(requested)~="table" then return nil,"invalid selection" end
    local data=jsonInterface.load("requiredDataFiles.json")
    if type(data)~="table" then return nil,"missing active mod list" end
    local entries=data.content or data
    if type(entries)~="table" or #entries==0 then return nil,"empty active mod list" end
    local dirs,files=sourceDirs(cfg)
    local filtered,raw={},{}
    for _,v in ipairs(entries) do
        local name=type(v)=="table" and next(v) or nil
        if type(name)~="string" then return nil,"invalid mod entry" end
        if lower(name):match("%.es[mp]$") then
            local path=locate(name,dirs,files)
            if not path then return nil,"missing active mod: "..name end
            local ok,err=M.scanFile(path,filtered,raw,requested)
            if not ok then return nil,err end
        end
    end
    for id in pairs(raw) do if not requested[id] then raw[id]=nil end end
    return raw
end
function M.findActiveRecord(cfg,jsonInterface,refId)
    if type(refId)~="string" or #refId>120 then return nil,"invalid refId" end
    local records,err=M.findActiveRecords(cfg,jsonInterface,{[lower(refId)]=true})
    return records and records[lower(refId)] or nil,err
end
-- Read canonical interior references for loose-item transactions. Never use
-- a refId/count/position supplied by a UI request as proof that an item exists.
local referenceCache={}
local function float32(s,p)
    local n=u32(s,p);if not n then return nil end
    local sign=n>=2147483648 and -1 or 1
    local exp=math.floor(n/8388608)%256;local mant=n%8388608
    if exp==255 then return nil end
    return sign*(exp==0 and mant*2^-149 or (1+mant/8388608)*2^(exp-127))
end
function M.findInteriorReference(cfg,json,cell,index,refId)
    local cacheKey=cell:lower()
    if referenceCache[cacheKey]==nil then
        local manifest=json.load("requiredDataFiles.json")
        local entries=manifest and (manifest.content or manifest)
        if type(entries)~="table" then return nil end
        local dirs,files=sourceDirs(cfg);local refs={}
        for _,v in ipairs(entries) do
            local plugin=type(v)=="table" and next(v)
            local path=plugin and locate(plugin,dirs,files)
            if not path then return nil end
            local f=openBinary(path);if not f then return nil end
            local masters={}
            while true do
                local h=f:read(16);if not h or h=="" then break end
                local size=u32(h,5)
                if #h~=16 or not size or size>134217728 then f:close();return nil end
                local tag=h:sub(1,4)
                if tag=="TES3" or tag=="CELL" then
                    local raw=f:read(size);if not raw or #raw~=size then f:close();return nil end
                    local subs={};local pos=1
                    while pos+7<=#raw do
                        local len=u32(raw,pos+4)
                        if not len or len>#raw-pos-7 then f:close();return nil end
                        subs[#subs+1]={raw:sub(pos,pos+3),raw:sub(pos+8,pos+7+len)};pos=pos+8+len
                    end
                    if pos~=#raw+1 then f:close();return nil end
                    if tag=="TES3" then
                        for _,sub in ipairs(subs) do if sub[1]=="MAST" then masters[#masters+1]=field(sub[2]):lower() end end
                    else
                        local name,interior,ref,origin,inRefs,cellDeleted
                        local function identity(n)
                            local parent=masters[math.floor(n/16777216)]
                            local normalized=parent and n%16777216 or n
                            return (parent or plugin:lower())..":"..normalized,normalized
                        end
                        local function finish()
                            if ref and origin then refs[origin]=ref end
                        end
                        for _,sub in ipairs(subs) do
                            local t,b=sub[1],sub[2]
                            if t=="FRMR" then
                                inRefs=true
                                finish();ref=nil;origin=nil
                                if name==cacheKey and interior and not cellDeleted then
                                    local n=u32(b,1)
                                    if n then
                                        local normalized
                                        origin,normalized=identity(n)
                                        ref={index=tostring(normalized).."-0",count=1,charge=-1,enchantmentCharge=-1,soul=""}
                                    end
                                end
                            elseif t=="MVRF" then
                                inRefs=true;finish();ref=nil;origin=nil
                                local n=u32(b,1)
                                if name==cacheKey and n then refs[identity(n)]={deleted=true} end
                            elseif ref then
                                if t=="NAME" then ref.refId=field(b)
                                elseif t=="DELE" then ref.deleted=true
                                elseif t=="NAM9" then ref.count=u32(b,1)
                                elseif t=="INTV" then ref.charge=u32(b,1);if ref.charge==4294967295 then ref.charge=-1 end
                                elseif t=="XCHG" then ref.enchantmentCharge=float32(b,1)
                                elseif t=="XSOL" then ref.soul=field(b)
                                elseif t=="DATA" and #b==24 then
                                    ref.location={posX=float32(b,1),posY=float32(b,5),posZ=float32(b,9),rotX=float32(b,13),rotY=float32(b,17),rotZ=float32(b,21)}
                                end
                            elseif not inRefs and not name and t=="NAME" then
                                local decoded=cellNameUtf8(b,cfg.houseCellNameEncoding)
                                name=decoded and decoded:lower() or ""
                            elseif not inRefs and t=="DELE" and name==cacheKey then
                                cellDeleted=true;refs={}
                            elseif not inRefs and t=="DATA" then interior=(u32(b,1) or 0)%2==1 end
                        end
                        finish()
                    end
                elseif not skipBytes(f,size) then f:close();return nil end
            end
            f:close()
        end
        referenceCache[cacheKey]=refs
    end
    local result
    for _,ref in pairs(referenceCache[cacheKey]) do
        if not ref.deleted and ref.index==index and ref.refId and ref.refId:lower()==refId:lower() then
            if result then return nil end -- ambiguous network index: fail closed
            result=ref
        end
    end
    return result and tableHelper.deepCopy(result)
end

return M
