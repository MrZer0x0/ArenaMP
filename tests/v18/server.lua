-- Run from repository root, using Lua 5.4 (no live server needed).
package.path="server/scripts/?.lua;server/lib/lua/?.lua;"..package.path
require("utils")
local transitions=require("inventoryTransitions")
inventoryHelper=require("inventoryHelper")
tableHelper=require("tableHelper")
local cfg=require("economyRules")
local validators={}
customEventHooks={registerHandler=function() end,registerValidator=function(n,f) validators[n]=f end,
 makeEventStatus=function(a,b) return {validDefaultHandler=a,validCustomHandlers=b} end}
local eco=require("economyCoefficients")
local function item(charge,count) return {refId="pick",charge=charge,count=count or 1,enchantmentCharge=-1,soul="",poisonId="",poisonCharges=0} end
local player={data={customVariables={}},accountName="Tester"}
cfg.buy=1.2;cfg.sell=0.8;cfg.services.trade=1.5;cfg.services.training=2
cfg.items.iron={buy=0.5,sell=3}
local p=eco.Policy(player)
assert(math.abs(eco.Factor(p,"trade","IRON",true)-.9)<1e-9)
assert(math.abs(eco.Factor(p,"trade","iron",false)-.9)<1e-9)
assert(math.abs(eco.Factor(p,"training","",true)-2.4)<1e-9)
cfg.players.Tester={buy=2,services={training=.5}}
p=eco.Policy(player);assert(math.abs(eco.Factor(p,"training","",true)-2.4)<1e-9)
eco.RecordSales(player.data,{{refId="iron",count=100}},1000)
assert(player.data.customVariables.economyRevision==1)
assert(player.data.customVariables.economySales.iron.units==100)
for i=1,200 do eco.RecordSales(player.data,{{refId="item"..i,count=1}},1000+i) end
local n=0;for _ in pairs(player.data.customVariables.economySales) do n=n+1 end;assert(n==128)
assert(validators.OnPlayerSendMessage(nil,0,"@@ARENA_ECO2@@{}").validDefaultHandler==false)
cfg.enabled=false;p=eco.Policy(player);assert(eco.Factor(p,"training","iron",true)==1)
-- Exercise the real SaveEquipment/SaveInventory bodies, without starting a server.
local file=assert(io.open("server/scripts/player/base.lua"));local code=file:read("*a");file:close()
BasePlayer={};config={bannedEquipmentItems={}}
local function method(name,nextAnchor)
 local a=assert(code:find("function BasePlayer:"..name,1,true))
 local b=assert(code:find(nextAnchor,a+1,true))
 local chunk=code:sub(a,b-1)
 assert(load(chunk,"@base.lua:"..name,"t",setmetatable({inventoryTransitions=transitions},{__index=_G})))()
end
method("SaveEquipment(playerPacket)","-- Iterate through inventory items")
method("SaveInventory(playerPacket)","-- Iterate through spells")
enumerations={inventory={SET=0,ADD=1,REMOVE=2},log={INFO=1}}
tes3mp={LogMessage=function() end,LogAppend=function() end}
logicHandler={IsGeneratedRecord=function() return false end}
function BasePlayer:RejectGeneratedInventoryLossDuringLogin() return false end
function BasePlayer:QuicksaveToDrive() end
function BasePlayer:Message() error("unexpected banned equipment") end
local function new(inventory,old)
 return setmetatable({pid=0,data={inventory=inventory,equipment={}},previousEquipment={[0]=old}}, {__index=BasePlayer})
end
local function uses(p,charge)
 local n=0;for _,v in pairs(p.data.inventory) do if (v.charge or -1)==charge then n=n+v.count end end;return n
end
local function equip(p,it) p:SaveEquipment({equipment={[0]=it}}) end
local function inv(p,it,action) p:SaveInventory({inventory={it},action=action}) end
-- Explicit wear updates must not consume pristine items twice.
local t=new({item(-1,10)},item(-1,10))
inv(t,item(-1),2);inv(t,item(24),1);equip(t,item(24))
assert(uses(t,-1)==9 and uses(t,24)==1)
-- Rapid attempts may precede the periodic equipment update.
inv(t,item(24),2);inv(t,item(23),1);inv(t,item(23),2);inv(t,item(22),1);equip(t,item(22))
assert(uses(t,-1)==9 and uses(t,22)==1 and uses(t,23)==0)
equip(t,item(22));assert(uses(t,-1)==9 and uses(t,22)==1)
-- Switching between existing states of the same refId must not change inventory.
t=new({item(-1,4),item(24,2)},item(-1,4));equip(t,item(24))
assert(uses(t,-1)==4 and uses(t,24)==2)
-- Exhaustion removes only the exhausted instance.
t=new({item(-1,3),item(1)},item(1));inv(t,item(1),2)
equip(t,{refId="",count=0,charge=-1,enchantmentCharge=-1})
assert(uses(t,-1)==3 and uses(t,1)==0)
local awarded={item(12)};inventoryHelper.addItem(awarded,"pick",3)
assert(#awarded==2 and awarded[1].charge==12 and awarded[1].count==1 and awarded[2].charge==-1 and awarded[2].count==3)
-- Nil legacy fields are the canonical -1/empty instance, not a wildcard.
t=new({{refId="pick",count=3},item(20)},item(-1));equip(t,item(19))
assert(uses(t,-1)==2 and uses(t,20)==1 and uses(t,19)==1)
local expired={};transitions.observe(expired,item(-1),2,1,2,0);transitions.observe(expired,item(24),1,1,2,0)
assert(not transitions.consume(expired,item(-1),item(24),6))
local F=require("craftEconomic.model");local c=require("craftEconomic.config")
cfg.enabled=true;cfg.players={};cfg.buy=1;cfg.sell=1;cfg.services.trade=1;cfg.items={};cfg.sales.enabled=false
local ctx={region="neutral",day=1,skill=50,merchantSkill=30,taxRate=.03,coefficients=eco.Policy({data={}})}
local rec={id="iron",type="weapon",data={value=100,health=100}}
local it={refId="iron",count=1,charge=100}
local buy=assert(F.price(rec,it,"buy",ctx,c)).total;local sell=assert(F.price(rec,it,"sell",ctx,c)).total;assert(buy>sell)
ctx.region="another-plugin-region";assert(F.price(rec,it,"buy",ctx,c).total==buy)
ctx.coefficients.buy=2;assert(F.price(rec,it,"buy",ctx,c).total==buy*2)
-- Authored IDs are case-insensitive; edits persist and invalidate quotes.
cfg.items={IRON={buy=1.3}};assert(math.abs(eco.Factor(eco.Policy({data={}}),"trade","iron",true)-1.3)<1e-9)
local sent={};tes3mp.SendMessage=function(pid,text) sent[#sent+1]=text end
Players={[7]={pid=7,accountName="Editor",data={},IsLoggedIn=function() return true end,
 QuicksaveToDrive=function(self) self.saved=(self.saved or 0)+1 end}}
assert(eco.SetPlayer(7,{buy=1.4,services={training=.5}}))
assert(Players[7].data.customVariables.economyRevision==1 and Players[7].saved==1)
assert(#sent==1 and sent[1]:find("@@ARENA_ECO2@@",1,true)==1)
local huge={};for i=1,257 do huge["id"..i]={buy=1} end
assert(not eco.SetPlayer(7,{items=huge}))
assert(Players[7].data.customVariables.economyRevision==1 and Players[7].saved==1)
assert(Players[7].data.customVariables.economyCoefficients.buy==1.4)
print("server pricing and inventory regression cases passed")
