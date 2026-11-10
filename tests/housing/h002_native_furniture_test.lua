-- Run: python tests/housing/run_lua.py tests/housing/h001_furniture_test.lua
local handlers, commands, messages, nativeMessages = {}, {}, {}, {}
local account = { accountName="Tester", data={inventory={{refId="gold_001",count=2000}},customVariables={}}, saveCount=0 }
function account:IsLoggedIn() return true end
function account:Save() self.saveCount=self.saveCount+1 end
function account:LoadItemChanges(changes, action) self.lastChange={changes,action} end
Players={[1]=account}
enumerations={log={ERROR=1},inventory={REMOVE=2,ADD=1}}
tableHelper={cleanNils=function(items)
    for index=#items,1,-1 do if items[index]==nil or items[index].count<=0 then table.remove(items,index) end end
end}
customEventHooks={registerHandler=function(name,fn) handlers[name]=fn end}
customCommandHooks={registerCommand=function(name,fn) commands[name]=fn end}
local home={owner="Tester",mode="instance"}
local loadedCell="Balmora, Test - home"
local instancePending=false
local housing={
    GetInstanceDescription=function() return "Balmora, Test - home" end,
    GetBaseCellForInstance=function(cell) if cell=="Balmora, Test - home" then return "Balmora, Test" end end,
    GetProperty=function(cell) if cell=="Balmora, Test" then return home end end,
    GetAccessLevel=function(pid,property) if property==home then return "owner" end return "none" end,
    IsInstanceCell=function(cell) return cell=="Balmora, Test - home" end,
    IsPurgePending=function(cell) return instancePending end
}
local inventoryHelper={removeClosestItem=function(inv,id,amount)
    for _,item in ipairs(inv) do if item.refId==id then item.count=item.count-amount;return end end
end,addItem=function(inv,id,amount)
    for _,item in ipairs(inv) do if item.refId==id then item.count=item.count+amount;return end end
    inv[#inv+1]={refId=id,count=amount}
end}
local jsonInterface={load=function(path)
    if path=="custom/arenaFurnitureReference.json" then
        return {categories={"Мебель"},items={{refId="furn_chair_test",category="Мебель",
            nameRu="Стул",nameEn="Chair",price=500}}}
    end
end,save=function() return true end}
local created=0
local cell={description=loadedCell,data={objectData={}}}
LoadedCells={[loadedCell]=cell}
logicHandler={DeleteObjectForPlayer=function() end,CreateObjectAtLocation=function(locationCell,location,obj,action)
    assert(locationCell==loadedCell and obj.refId=="furn_chair_test" and action=="place")
    created=created+1
    cell.data.objectData["0-"..created]={refId=obj.refId}
    return "0-"..created
end,DeleteObjectForEveryone=function(locationCell,uniqueIndex)
    assert(locationCell==loadedCell)
    cell.data.objectData[uniqueIndex]=nil
end}
function cell:Save() self.saved=true end
local tes3mp={
    LogMessage=function(...) end,
    MessageBox=function(pid,id,text) messages[#messages+1]=text end,
    SendMessage=function(pid, message, broadcast)
        assert(pid==1 and broadcast==false, "native furniture must be private")
        nativeMessages[#nativeMessages+1]=message
    end,
    CustomMessageBox=function(...) end,
    GetCell=function(pid) return loadedCell end,
    GetRotZ=function(pid) return 0 end,
    GetPosX=function(pid) return 100 end,
    GetPosY=function(pid) return 200 end,
    GetPosZ=function(pid) return 300 end
}
_G.tes3mp=tes3mp
local autoCatalog={build=function() return {items={{refId="furn_chair_test",name="Стул",category="Мебель",price=500}}} end}
local required={jsonInterface=jsonInterface,houseHelper=housing,inventoryHelper=inventoryHelper,arenaFurnitureAutoCatalog=autoCatalog}
local originalRequire=require
_G.require=function(name) return required[name] or originalRequire(name) end
local market=assert(loadfile("server/scripts/arenaFurniture.lua"))()
_G.require=originalRequire
-- H002: test the native catalog and server-authoritative command protocol.
market.OpenNative(1)
local function received(prefix)
    for _,line in ipairs(nativeMessages) do
        if line:sub(1, #("@@AMP_HOUSE@@"..prefix)) == "@@AMP_HOUSE@@"..prefix then return true end
    end
    return false
end
assert(received("FS_BEGIN"), "native UI must initialize")
assert(received("FS_CATEGORY\tМебель"), "category list")
assert(received("FS_CATEGORY\tМебель\tfurniture"), "stable localization key")
assert(received("FS_ITEM\tfurn_chair_test"), "first furniture page")
assert(received("FS_STOCK_END") and received("FS_PLACED_END"), "two inventories")
local originalGold=account.data.inventory[1].count
market.NativeCommand(1,"BUY","non_existing",1,"*")
assert(account.data.inventory[1].count==originalGold, "client cannot buy a nonexistent refId")
market.NativeCommand(1,"BUY","furn_chair_test",1,"*")
assert(account.data.inventory[1].count==originalGold-500, "native UI uses server catalog price")
assert(market.GetOwned(1).furn_chair_test==1)
market.NativeCommand(1,"SELL","furn_chair_test",1,"*")
assert(account.data.inventory[1].count==originalGold-125, "native UI uses server sellback price")
assert(market.GetOwned(1).furn_chair_test==nil)
market.NativeCommand(1,"CATEGORY","Мебель",1,"non-existent subcategory")
assert(received("FS_ITEMS_BEGIN\tМебель"))
market.NativeCommand(1,"INVENTORY","_",1,"*")
market.NativeCommand(1,"PLACED","_",1,"*")
assert(received("FS_NOTICE"), "commands should return a status")
account.data.inventory[1].count=2000 -- reset mock budget for the inherited H001 regression cases
assert(market.Buy(1,"UNLISTED") == false, "unlisted items must not be purchasable")
assert(account.data.inventory[1].count==2000)
assert(market.Buy(1,"furn_chair_test"))
assert(account.data.inventory[1].count==1500)
assert(market.GetOwned(1).furn_chair_test==1)
assert(market.Buy(1,"furn_chair_test"))
assert(market.GetOwned(1).furn_chair_test==2)
assert(market.Buy(1,"furn_chair_test"))
assert(market.Buy(1,"furn_chair_test"))
assert(market.GetOwned(1).furn_chair_test==4)
assert(market.Buy(1,"furn_chair_test") == false,"no gold: no additional furniture")
assert(market.Place(1,"unlisted") == false)
loadedCell="Balmora, Test"
assert(market.Place(1,"furn_chair_test")==false,"must not place furniture into shared original cell")
loadedCell="Balmora, Test - home"
instancePending=true
assert(market.Place(1,"furn_chair_test")==false,"must not place before first cleanup")
instancePending=false
assert(market.Place(1,"furn_chair_test"))
assert(created==1 and market.GetOwned(1).furn_chair_test==3)
assert(cell.data.objectData["0-1"].arenaFurnitureOwner=="tester")
assert(cell.data.objectData["0-1"].arenaFurnitureOriginalPrice==500)
assert(market.ReturnToInventory(1,"bogus") == false)
assert(market.ReturnToInventory(1,"0-1"))
assert(created==1 and cell.data.objectData["0-1"]==nil)
assert(market.GetOwned(1).furn_chair_test==4)
assert(market.ReturnToInventory(1,"0-1") == false,"cannot duplicate by reclaiming removed object")
assert(market.SellInventory(1,"furn_chair_test"))
assert(market.GetOwned(1).furn_chair_test==3)
assert(account.data.inventory[1].count==375)
assert(market.SellInventory(1,"unlisted")==false)
home.owner="Other"
housing.GetAccessLevel=function() return "none" end
assert(market.Place(1,"furn_chair_test")==false,"a non-owner cannot place furniture")
assert(market.GetOwned(1).furn_chair_test==3,"denied placement must preserve stock")
assert(type(commands.furniture)=="function" and type(handlers.OnGUIAction)=="function")
print("PASS H002: native private catalog/subcategories, authoritative UI buy/sell, stock, place, ownership and home guards")
