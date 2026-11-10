-- H015 reference-only furniture + localized names + manual overrides + style filtering.
package.path = "server/scripts/?.lua;" .. package.path
local storage, messages={},{}
local reference={categories={"Мебель","Кровати","Контейнеры","Декор","Двери"},items={
    {refId="furn_nord_table",category="Мебель",nameRu="Нордский стол",nameEn="Nordic table",price=1200,style="nordic"},
    {refId="furn_dunmer_chair",category="Мебель",nameRu="Данмерский стул",nameEn="Dunmer chair",price=1400,style="dunmer"},
    {refId="missing_prop",category="Декор",nameRu="Нет",nameEn="Missing",price=100,style="other"},
    {refId="furn_scripted_bed",category="Кровати",nameRu="Кровать со скриптом",nameEn="Scripted bed",price=550,style="other"},
    {refId="furn_deleted_bed",category="Кровати",nameRu="Удалённая кровать",nameEn="Deleted bed",price=550,style="other"},
    {refId="furn_quest",category="Мебель",nameRu="Квест",nameEn="Quest",price=100,style="other"},
    {refId="furn_loot_chest",category="Контейнеры",nameRu="Лут",nameEn="Loot",price=100,style="other"}}}
local mockRecords={furn_nord_table={kind="STAT",refId="furn_nord_table",model="f\\table.nif"},
    furn_dunmer_chair={kind="STAT",refId="furn_dunmer_chair",model="f\\chair.nif"},
    furn_scripted_bed={kind="ACTI",refId="furn_scripted_bed",model="f\\furn_bed.nif",script="bed_sleep_script"},
    furn_deleted_bed=false,
    furn_quest={kind="ACTI",refId="furn_quest",model="f\\quest.nif",script="quest_script"},
    furn_loot_chest={kind="CONT",refId="furn_loot_chest",model="f\\chest.nif",hasContents=true},
    new_safe_lamp={kind="STAT",refId="new_safe_lamp",model="f\\lamp.nif",name="Lamp"}}
local player={accountName="Admin",language="EN",data={settings={staffRank=2},inventory={},customVariables={}}}
function player:IsLoggedIn() return true end
function player:Save() end
Players={[1]=player}
config={}
enumerations={log={ERROR=1,INFO=2},inventory={ADD=1,REMOVE=2}}
tableHelper={cleanNils=function() end}
customEventHooks={registerHandler=function()end}
customCommandHooks={registerCommand=function()end}
LoadedCells={Room={description="Room",data={objectData={["0-7"]={refId="new_safe_lamp"}}}}}
tes3mp={GetCell=function()return "Room" end,LogMessage=function()end,MessageBox=function()end,CustomMessageBox=function()end,
    SendMessage=function(_,msg)messages[#messages+1]=msg end}
local function candidate(r)
    return r and not r.script and not r.hasContents and r.kind~="DOOR" and r.model and
        {refId=r.refId,name=r.name or r.refId,category="Декор",price=200} or nil
end
local auto={
    build=function(_,_,requested)
        assert(requested.furn_nord_table and requested.missing_prop)
        local raw={}
        for id in pairs(requested) do raw[id]=mockRecords[id] end
        return {items={},source="active-esm-esp"},nil,raw
    end,
    findActiveRecord=function(_,_,id)return mockRecords[id]end,
    manualCandidate=candidate,
    referenceBedCandidate=require("arenaFurnitureAutoCatalog").referenceBedCandidate
}
local json={load=function(p) if p=="custom/arenaFurnitureReference.json" then return reference end return storage[p] end,
    save=function(p,val)storage[p]=val;return true end}
local originalRequire=require
_G.require=function(name)
    return ({jsonInterface=json,houseHelper={GetBaseCellForInstance=function()end,GetProperty=function()end},
        inventoryHelper={},arenaFurnitureAutoCatalog=auto})[name] or originalRequire(name)
end
local shop=assert(loadfile("server/scripts/arenaFurniture.lua"))()
_G.require=originalRequire
local function last(needle)
    for _,msg in ipairs(messages) do if msg:find(needle,1,true) then return true end end
    return false
end
local snap=assert(storage["custom/arenaFurnitureGenerated.json"])
assert(snap.source=="reference-intersection" and #snap.items==3,"curated scripted beds and other verified items become stock")
assert(shop.HasCatalogItem("furn_scripted_bed"),"scripted ACTI bed must be retained when its reference is curated")
assert(not shop.HasCatalogItem("furn_deleted_bed"),"active plugin deletion must block a curated bed")
assert(shop.HasCatalogItem("furn_nord_table") and not shop.HasCatalogItem("missing_prop"))
assert(not shop.HasCatalogItem("furn_quest") and not shop.HasCatalogItem("furn_loot_chest"))
shop.OpenNative(1)
assert(last("FS_CATEGORY\tМебель\tfurniture"),"ASCII category localization key preserves the ID")
shop.NativeCommand(1,"CATEGORY","Мебель","1","all")
assert(last("FS_ITEM\tfurn_nord_table\tNordic table"),"English names must travel to player")
shop.NativeCommand(1,"CATEGORY","Мебель","1","dunmer")
local dunmer=false
for _,msg in ipairs(messages) do if msg:find("FS_ITEM\tfurn_dunmer_chair",1,true) then dunmer=true end end
assert(dunmer,"style filter must include dunmer furniture")
assert(shop.Edit(1,"furn_nord_table","Контейнеры",2300,"Нордский стол, новый","Nordic table, new"),"admin may edit reference item")
assert(storage["custom/arenaFurnitureManual.json"].furn_nord_table.price==2300)
assert(shop.HasCatalogItem("furn_nord_table"))
assert(shop.Capture(1,"new_safe_lamp","0-7"),"verified staff capture")
assert(shop.Edit(1,"new_safe_lamp","Освещение",315,"Лампа","Lamp"))
assert(shop.HasCatalogItem("new_safe_lamp"))
assert(shop.Refresh(1),"refresh regenerates intersection and preserves admin edits")
local final=storage["custom/arenaFurnitureGenerated.json"]
assert(#final.items==4 and final.items[1].price,"snapshot contains allowed references, scripted bed and manual items")
player.language="RU";shop.OpenNative(1)
shop.NativeCommand(1,"CATEGORY","Контейнеры","1","all")
assert(last("FS_ITEM\tfurn_nord_table\tНордский стол, новый"),"Russian override travels to player")
player.data.settings.staffRank=0
assert(not shop.Edit(1,"furn_nord_table","Мебель",1,"x","y"),"non-admin must not edit")
assert(not shop.Capture(1,"new_safe_lamp","0-7"),"non-admin must not capture")
print("PASS H015 reference: scripted curated ACTI bed included, deleted bed/quest/loot excluded, styles, RU/EN, staff edits, durable snapshot, permissions")
