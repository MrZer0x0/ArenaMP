-- ArenaMP Y097: trusted content catalog and server-script API. This module does
-- not expose an RPC, console command or arbitrary client file-reading endpoint.
local json = require("dkjson")
local Content = { version = 1 }
local settings = (config and config.contentApi) or require("contentApiConfig")
local key = ""
-- Bounded FIFO of decoded records, including misses. Callers receive copies.
local cache,queue,head,tail,cacheBytes={}, {},1,0,0
local function resetCache() cache,queue,head,tail,cacheBytes={}, {},1,0,0 end
local function cachePut(k,value,bytes)
    while tail-head+1>=256 or cacheBytes+bytes>8*1024*1024 do
        if head>tail then break end
        local old=queue[head];queue[head]=nil;head=head+1
        cacheBytes=cacheBytes-cache[old].bytes;cache[old]=nil
    end
    if head>tail then resetCache() end
    tail=tail+1;queue[tail]=k;cache[k]={value=value,bytes=bytes};cacheBytes=cacheBytes+bytes
end
Content.settings = settings

local aliases = {NPC="NPC_", CLOTHING="CLOT", ARMOR="ARMO", WEAPON="WEAP", SPELL="SPEL", CREATURE="CREA", CONTAINER="CONT"}
function Content.Type(value)
    if type(value) ~= "string" then return nil end
    value=value:upper(); value=aliases[value] or value
    if #value ~= 4 or value:find("[^A-Z0-9_]") then return nil end
    return value
end
function Content.Copy(value)
    if type(value) ~= "table" then return value end
    local result={}; for k,v in pairs(value) do result[k]=Content.Copy(v) end; return result
end
function Content.Number(value, low, high)
    return type(value)=="number" and value==value and value>=low and value<=high and value==math.floor(value)
end
local function nativeAvailable()
    return type(tes3mp.ContentCatalogGetVersion)=="function" and tes3mp.ContentCatalogGetVersion()==1
end
local function decode(raw)
    local value,_,err=json.decode(raw,1,nil)
    if err then return nil,"invalid_native_json: "..err end
    return value
end
local function errorText()
    return type(tes3mp.ContentCatalogGetError)=="function" and tes3mp.ContentCatalogGetError() or "native_content_api_unavailable"
end
function Content.IsReady() return key~="" end
function Content.GetKey() return key end
function Content.NormalizeId(value)
    value=tostring(value or "")
    if type(tes3mp.ContentCatalogNormalizeId)=="function" then return tes3mp.ContentCatalogNormalizeId(value) end
    return value:lower()
end
function Content.GetInfo()
    if not nativeAvailable() then return nil,"native_content_api_unavailable" end
    local raw=tes3mp.ContentCatalogGetInfo();local err=errorText()
    if err~="" then return nil,err end
    return decode(raw)
end
function Content.Reload(options)
    options=options or settings
    if not nativeAvailable() then return nil,"native_content_api_unavailable" end
    -- Parsing and hashing large plugins is synchronous. Keep it off live gameplay.
    if type(Players)=="table" and next(Players)~=nil then return nil,"catalog_reload_requires_empty_server" end
    local resolved,sourceError=require("contentSources").Resolve(options)
    if not resolved then return nil,sourceError end
    options=resolved
    if type(options.files)~="table" or #options.files==0 then return nil,"content_files_required" end
    if #options.files>1024 then return nil,"too_many_plugins" end
    if (options.encoding~=nil and type(options.encoding)~="string") or
        (options.expectedKey~=nil and type(options.expectedKey)~="string") then return nil,"invalid_catalog_options" end
    for k in pairs(options.files) do
        if not Content.Number(k,1,#options.files) then return nil,"invalid_plugin_array" end
    end
    for _,path in ipairs(options.files) do
        if type(path)~="string" or path=="" or path:find("%z") then return nil,"invalid_plugin_path" end
    end
    if not tes3mp.ContentCatalogBegin(options.encoding or "win1251",options.expectedKey or "") then return nil,errorText() end
    for _,path in ipairs(options.files) do
        if not tes3mp.ContentCatalogAddFile(path) then return nil,errorText() end
    end
    if not tes3mp.ContentCatalogCommit() then return nil,errorText() end
    local info,err=Content.GetInfo();if not info then return nil,err end
    key=info.contentKey or ""
    resetCache()
    return info
end
function Content.GetRecord(recordType,refId,includeDeleted)
    recordType=Content.Type(recordType)
    if not Content.IsReady() then return nil,"catalog_not_loaded" end
    if not recordType or type(refId)~="string" or refId=="" or #refId>1024 then return nil,"invalid_record_identity" end
    local cacheKey=recordType..":"..Content.NormalizeId(refId)
    local cached=cache[cacheKey]
    if cached then
        if cached.value==false then return nil,"record_not_found" end
        if cached.value.deleted and not includeDeleted then return nil,"record_deleted" end
        return Content.Copy(cached.value)
    end
    local raw=tes3mp.ContentCatalogGetRecord(recordType,refId)
    local nativeError=errorText();if nativeError~="" then return nil,nativeError end
    local record,err=decode(raw)
    if err then return nil,err end
    cachePut(cacheKey,record or false,#raw)
    if not record then return nil,"record_not_found" end
    if record.deleted and not includeDeleted then return nil,"record_deleted" end
    return Content.Copy(record)
end
function Content.Search(recordType,prefix,offset,limit)
    recordType=Content.Type(recordType);prefix=prefix or "";offset=offset or 0;limit=limit or 100
    if not Content.IsReady() then return nil,"catalog_not_loaded" end
    if not recordType or type(prefix)~="string" or not Content.Number(offset,0,2000000) or not Content.Number(limit,1,1000) then
        return nil,"invalid_search_arguments"
    end
    local raw=tes3mp.ContentCatalogSearch(recordType,prefix,offset,limit);local err=errorText()
    if err~="" then return nil,err end
    return decode(raw)
end
function Content.Count(recordType)
    if not Content.IsReady() then return nil,"catalog_not_loaded" end
    if recordType~=nil then recordType=Content.Type(recordType);if not recordType then return nil,"invalid_record_type" end end
    local value=tes3mp.ContentCatalogGetRecordCount(recordType or "");local err=errorText()
    if err~="" then return nil,err end
    return value
end
function Content.GetSubrecord(recordType,refId,tag,occurrence,format)
    recordType=Content.Type(recordType);occurrence=occurrence or 0;format=format or "hex"
    if not Content.IsReady() then return nil,"catalog_not_loaded" end
    if format~="hex" and format~="text" and format~="uint32" and format~="int32" and format~="float32" then return nil,"invalid_subrecord_format" end
    if not recordType or type(refId)~="string" or type(tag)~="string" or #tag~=4
        or not Content.Number(occurrence,0,65535) then return nil,"invalid_subrecord_arguments" end
    local raw=tes3mp.ContentCatalogGetSubrecord(recordType,refId,tag:upper(),occurrence,format)
    local err=errorText();if err~="" then return nil,err end
    if format=="uint32" or format=="int32" or format=="float32" then return tonumber(raw) end
    return raw
end
function Content.GetNpcInventory(refId)
    local record,err=Content.GetRecord("NPC_",refId);if not record then return nil,err end
    return record.inventory
end
function Content.GetCellReference(cell,refNum,refId)
    if not Content.IsReady() or type(tes3mp.ContentCatalogGetCellReference)~="function" then return nil,"reference_api_unavailable" end
    if type(cell)~="string" or type(refId)~="string" or not Content.Number(refNum,0,4294967295) then return nil,"invalid_reference" end
    return decode(tes3mp.ContentCatalogGetCellReference(cell,refNum,refId))
end
function Content.GetSubrecordChunk(recordType,refId,tag,occurrence,offset,limit)
    recordType=Content.Type(recordType);occurrence=occurrence or 0;offset=offset or 0;limit=limit or 65536
    if not Content.IsReady() then return nil,"catalog_not_loaded" end
    if not recordType or type(refId)~="string" or type(tag)~="string" or #tag~=4
        or not Content.Number(occurrence,0,65535) or not Content.Number(offset,0,16777216)
        or not Content.Number(limit,1,65536) then return nil,"invalid_chunk_arguments" end
    local raw=tes3mp.ContentCatalogGetSubrecordChunk(recordType,refId,tag:upper(),occurrence,offset,limit)
    local err=errorText();if err~="" then return nil,err end
    return decode(raw)
end
local itemTypes={"CLOT","ARMO","WEAP","BOOK","ALCH","INGR","MISC","LIGH","LOCK","PROB","REPA","APPA"}
function Content.GetItem(refId)
    if not Content.IsReady() then return nil,"catalog_not_loaded" end
    local found
    for _,kind in ipairs(itemTypes) do
        local record,err=Content.GetRecord(kind,refId)
        if not record and err~="record_not_found" and err~="record_deleted" then return nil,err end
        if record then
            if found then return nil,"ambiguous_item_id" end
            found=record
        end
    end
    if not found then return nil,"item_not_found" end
    return found
end
function Content.GetItemSlots(refId)
    local record,err=Content.GetItem(refId);if not record then return nil,err end
    return record.slots
end
function Content.GetDependencies(recordType,refId)
    local record,err=Content.GetRecord(recordType,refId);if not record then return nil,err end
    local dependencies={records={},assets={}};local seen={}
    local function reference(kind,id)
        if type(id)~="string" or id=="" then return end
        local k=kind..":"..Content.NormalizeId(id);if seen[k] then return end;seen[k]=true
        local target=Content.GetRecord(kind,id)
        dependencies.records[#dependencies.records+1]={type=kind,id=id,exists=target~=nil}
    end
    for field,kind in pairs({script="SCPT",enchantment="ENCH",race="RACE",class="CLAS",faction="FACT",head="BODY",hair="BODY"}) do reference(kind,record[field]) end
    for _,spell in ipairs(record.spells or {}) do reference("SPEL",spell) end
    for _,entry in ipairs(record.inventory or {}) do
        local item=Content.GetItem(entry.refId)
        if item then reference(item.type,item.id) else reference("ITEM",entry.refId) end
    end
    for field,kind in pairs({model="mesh",icon="texture"}) do
        if record[field] and record[field]~="" then dependencies.assets[#dependencies.assets+1]={kind=kind,path=record[field]} end
    end
    return dependencies -- declared direct references; assets are not opened here
end
Content.Actors = require("contentActors")(Content)
function Content.Initialize()
    if not settings.enabled then return true end
    local info,err=Content.Reload(settings)
    if not info then
        tes3mp.LogMessage(enumerations.log.ERROR,"[ContentAPI] Catalog load failed: "..tostring(err))
        return false
    end
    tes3mp.LogMessage(enumerations.log.INFO,"[ContentAPI] Loaded "..tostring(info.records).." records; key="..key)
    return true
end
customEventHooks.registerHandler("OnServerPostInit",function() Content.Initialize() end)
return Content
