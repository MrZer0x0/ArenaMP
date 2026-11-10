-- H012 regression: single-list paging, staff tools, server-side catalog
-- regeneration and capture without trusting client-provided price/record type.
local messages,storage,commands={}, {}, {}
local player={accountName="Admin",language="RU",data={settings={staffRank=2},inventory={},customVariables={}}}
function player:IsLoggedIn() return true end
function player:Save() end
Players={[1]=player}
enumerations={log={ERROR=1,INFO=1},inventory={REMOVE=1,ADD=2}}
tableHelper={cleanNils=function() end}
customEventHooks={registerHandler=function() end}
customCommandHooks={registerCommand=function(name,fn)commands[name]=fn end}
local house={GetBaseCellForInstance=function()end,GetProperty=function()end,IsInstanceCell=function()return false end}
local items={}
for i=1,106 do
    items[i]={refId="furn_chair_"..i,name="Chair "..i,category="Сиденья",price=100+i}
end
local captured={kind="STAT",refId="furn_lounge_special",name="Special seat",model="f\\chair.nif"}
local reference={categories={"Сиденья","Декор"},items=items}
local auto={build=function(_,_,requested)
    local raw={}
    for _,item in ipairs(items) do
        if requested[item.refId:lower()] then raw[item.refId:lower()]={refId=item.refId,model="f\\chair.nif",kind="STAT"} end
    end
    if requested[captured.refId:lower()] then raw[captured.refId:lower()]=captured end
    return {items=items,source="active-esm-esp"},nil,raw
end,
    findActiveRecord=function(_,_,refId)if refId==captured.refId then return captured end end,
    findActiveRecords=function(_,_,requested)
        if requested[captured.refId:lower()] then return {[captured.refId:lower()]=captured} end
        return {}
    end,
    manualCandidate=function(record)
        if record then return {refId=record.refId,name=record.name or record.refId,category="Декор",price=200} end
    end}
local json={load=function(path)
    if path=="custom/arenaFurnitureReference.json" then return reference end
    return storage[path]
end,
    save=function(path,value) storage[path]=value;return true end}
local deps={jsonInterface=json,houseHelper=house,inventoryHelper={},arenaFurnitureAutoCatalog=auto}
local originalRequire=require
_G.require=function(name)return deps[name] or originalRequire(name) end
LoadedCells={Room={description="Room",data={objectData={["0-7"]={refId=captured.refId}}}}}
tes3mp={LogMessage=function()end,MessageBox=function()end,CustomMessageBox=function()end,
    GetCell=function()return "Room"end,
    SendMessage=function(pid,payload,isBroadcast)
        assert(isBroadcast==false)
        messages[#messages+1]=payload
    end}
local store=assert(loadfile("server/scripts/arenaFurniture.lua"))()
_G.require=originalRequire
local function seen(needle)
    for _,msg in ipairs(messages) do if msg:find(needle,1,true) then return true end end
    return false
end
store.Role(1)
assert(seen("FS_ROLE\t1\t1"),"staff sees Decoration outside owned homes")
store.Menu(1)
assert(seen("FS_MENU_BEGIN\tcategories"),"one list opens with categories")
store.Menu(1,"Сиденья",2)
assert(seen("FS_MENU_BEGIN\titems\t2\t3"),"large categories must be paged")
assert(seen("FS_MENU_ITEM\t@prev") and seen("FS_MENU_ITEM\t@next"),"page navigation shares the list")
assert(not store.Capture(1,"fake_chair","0-7"),"server rejects relabeled reference")
assert(store.Capture(1,captured.refId,"0-7"),"staff may capture verified furniture")
assert(storage["custom/arenaFurnitureManual.json"][captured.refId:lower()],"captured record persisted")
assert(store.HasCatalogItem(captured.refId),"new verified record appears in catalogue")
assert(store.Refresh(1),"admin may regenerate current source-based catalogue")
local snapshot=storage["custom/arenaFurnitureGenerated.json"]
assert(snapshot and #snapshot.items==107,"generated JSON contains the approved manual furniture")
player.data.settings.staffRank=0
store.Role(1)
assert(seen("FS_ROLE\t0\t0"),"ordinary players outside own home cannot see Decoration")
assert(not store.Refresh(1),"ordinary player cannot refresh the catalogue")
assert(not store.Capture(1,captured.refId,"0-7"),"ordinary player cannot add furniture")
local before=#messages
store.Menu(1)
assert(#messages==before,"no private decoration menu if player is outside own house")
print("PASS H012: role-gated decoration, one-list pagination, staff capture, manual/generated JSON, authoritative source validation")
