-- Write-ahead after-images. GPL-3.0-or-later, original ArenaMP code.
-- A failed disk operation never reports a successful transaction.
local S={journal="craftEconomic-pending.json"}
local function allowed(path)
    return type(path)=="string" and not path:find(":",1,true) and not path:find("\\",1,true)
        and not path:find("%z") and (path=="craftEconomic.json" or path:match("^player/[^/]+%.json$")
        or path:match("^cell/[^/]+%.json$") or path:match("^recordstore/[a-z]+%.json$"))
end
local function save(path,data)
    local ok,result=pcall(jsonInterface.quicksaveAtomic,path,data)
    return ok and result==true
end
local function validate(pending)
    if type(pending)~="table" or pending.version~=1 or type(pending.writes)~="table" or #pending.writes>20 then return nil,"invalid_journal" end
    local seen={}
    for index,write in pairs(pending.writes) do
        if type(index)~="number" or index<1 or index>#pending.writes or index~=math.floor(index)
            or type(write)~="table" or not allowed(write.path) or type(write.data)~="table" or seen[write.path] then
            return nil,"invalid_journal_path"
        end
        seen[write.path]=true
    end
    for i=1,#pending.writes do if type(pending.writes[i])~="table" then return nil,"invalid_journal_array" end end
    return true
end
function S.apply(pending)
    local valid,reason=validate(pending);if not valid then return nil,reason end
    for _,write in ipairs(pending.writes) do
        if not save(write.path,write.data) then return nil,"materialize_failed" end
    end
    if not save(S.journal,{version=1,writes={}}) then return nil,"journal_clear_failed" end
    return true
end
function S.commit(writes,id)
    if S.fatal then return nil,"storage_unavailable" end
    local pending={version=1,id=id,writes=writes}
    local valid,reason=validate(pending);if not valid then return nil,reason end
    if not save(S.journal,pending) then return nil,"journal_write_failed" end
    local ok,err=S.apply(pending)
    if not ok then
        -- No gameplay may run over a partially materialised committed operation.
        -- Next start replays the durable after-images before players can join.
        tes3mp.LogMessage(enumerations.log.ERROR,"[CraftEconomic] "..err.."; durable journal retained")
        S.fatal=true;tes3mp.StopServer(4)
        return nil,err
    end
    return true
end
function S.exists(path)
    return type(tes3mp.DoesFilePathExist)=="function" and tes3mp.DoesFilePathExist(config.dataPath.."/"..path)
end
function S.recover()
    local pending=jsonInterface.load(S.journal)
    if pending==nil then
        if not S.exists(S.journal) and not S.exists(S.journal..".bak") then S.fatal=false;return true end
        tes3mp.LogMessage(enumerations.log.ERROR,"[CraftEconomic] unreadable journal; recovery required")
        tes3mp.StopServer(4);return nil,"invalid_journal"
    end
    local ok,err=S.apply(pending)
    if not ok then tes3mp.LogMessage(enumerations.log.ERROR,"[CraftEconomic] recovery failed: "..err);tes3mp.StopServer(4);return nil,err end
    S.fatal=false;return true,pending.writes
end
return S
