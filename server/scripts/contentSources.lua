-- Resolve an ordered, server-owned build manifest. Never guess load order by
-- alphabetically scanning plugins, and never use paths supplied by players.
local Sources={}
function Sources.Resolve(options)
    if type(options)~="table" then return nil,"invalid_catalog_options" end
    if options.files~=nil and type(options.files)~="table" then return nil,"invalid_plugin_array" end
    if type(options.files)=="table" and next(options.files) then return options end
    local directories=options.dataDirectories
    if type(directories)~="table" or #directories==0 then return nil,"data_directories_required" end
    if type(options.manifest)~="string" or options.manifest=="" then return nil,"content_manifest_required" end
    if not jsonInterface or type(jsonInterface.load)~="function" then return nil,"json_interface_required" end
    local ok,manifest=pcall(jsonInterface.load,options.manifest)
    if not ok or type(manifest)~="table" then return nil,"content_manifest_unreadable" end
    local entries=manifest.content or manifest
    if type(entries)~="table" or #entries==0 or #entries>1024 then return nil,"invalid_content_manifest" end
    for k in pairs(entries) do
        if type(k)~="number" or k%1~=0 or k<1 or k>#entries then return nil,"invalid_manifest_array" end
    end
    local result={};for k,v in pairs(options) do result[k]=v end;result.files={}
    for _,entry in ipairs(entries) do
        local name
        if type(entry)=="string" then name=entry
        elseif type(entry)=="table" then
            for k in pairs(entry) do if name then return nil,"ambiguous_manifest_entry" end;name=k end
        end
        if type(name)~="string" or name=="" or name:find("[/\\:%z]") or name=="." or name==".." then
            return nil,"invalid_manifest_plugin_name"
        end
        local resolved
        -- OpenMW data directory precedence: later directories override earlier.
        for i=#directories,1,-1 do
            local directory=directories[i]
            if type(directory)~="string" or directory=="" or directory:find("%z") then return nil,"invalid_data_directory" end
            local actual=tes3mp.GetCaseInsensitiveFilename(directory,name)
            if actual and actual~="invalid" then resolved=directory.."/"..actual;break end
        end
        if not resolved then return nil,"content_plugin_missing: "..name end
        result.files[#result.files+1]=resolved
    end
    return result
end
return Sources
