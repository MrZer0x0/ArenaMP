-- H005 locale and disabled-mod furniture persistence regression.
local messages, boxes, network = {}, {}, {}
local player={accountName="Tester", language="EN",data={inventory={{refId="gold_001",count=1000}},
    customVariables={arenaFurniture={items={mod_disabled_chair=2}}}}}
function player:IsLoggedIn() return true end
function player:Save() end
function player:LoadItemChanges() end
Players={[1]=player}
enumerations={log={ERROR=1,INFO=1}, inventory={REMOVE=1,ADD=2}}
tableHelper={cleanNils=function() end}
customEventHooks={registerHandler=function() end}
local commands={}
customCommandHooks={registerCommand=function(name,fn) commands[name]=fn end}
local house={GetBaseCellForInstance=function() end,GetProperty=function() end}
local inventoryHelper={removeClosestItem=function() end,addItem=function() end}
local catalog={build=function() return {items={{refId="furn_chair_test",name="Wooden chair",category="Сиденья",price=100}}} end}
local required={jsonInterface={},houseHelper=house,inventoryHelper=inventoryHelper,arenaFurnitureAutoCatalog=catalog}
local oldRequire=require
_G.require=function(name) return required[name] or oldRequire(name) end
local tes3mp={LogMessage=function() end,MessageBox=function(_,__,message) messages[#messages+1]=message end,
    CustomMessageBox=function(_,__,label,buttons)boxes[#boxes+1]={label=label,buttons=buttons}end,
    SendMessage=function(_,line,broadcast)assert(broadcast==false);network[#network+1]=line end,
    GetCell=function() return "No property" end}
_G.tes3mp=tes3mp
local store=assert(loadfile("server/scripts/arenaFurniture.lua"))()
_G.require=oldRequire
local function latest(prefix)
    for i=#network,1,-1 do
        if network[i]:find("@@AMP_HOUSE@@"..prefix,1,true)==1 then return network[i] end
    end
end
store.OpenNative(1)
assert(latest("FS_STOCK\tmod_disabled_chair\t2\t"),"orphaned furniture must remain visible on native UI")
assert(store.GetOwned(1).mod_disabled_chair==2,"disabled mod furniture must not be removed")
store.NativeCommand(1,"BUY","no_such_item",1,"")
assert(latest("FS_NOTICE\tThis item is unavailable in the catalog."),"English notice")
assert(store.SellInventory(1,"mod_disabled_chair")==false,"disabled mod item must not become sale currency")
assert(store.GetOwned(1).mod_disabled_chair==2)
commands.furniture(1,{"furniture","shop"})
assert(boxes[#boxes].label=="Furniture: choose a category" and boxes[#boxes].buttons:find("My inventory",1,true),"English fallback GUI")
player.language="RU"
store.NativeCommand(1,"BUY","no_such_item",1,"")
assert(latest("FS_NOTICE\tПредмет не продаётся или не найден в каталоге."),"Russian notice")
commands.furniture(1,{"furniture","shop"})
assert(boxes[#boxes].label=="Мебель: выберите категорию" and boxes[#boxes].buttons:find("Мой инвентарь",1,true),"Russian fallback GUI")
assert(store.GetOwned(1).mod_disabled_chair==2)
print("PASS H005: RU/EN personalized messages and menus, stable catalog IDs, orphaned inventory preserved and not sellable")
