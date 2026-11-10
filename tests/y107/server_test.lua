-- Exercises the actual server module. All disk/network/engine calls below are doubles.
package.path="server/scripts/?.lua;server/lib/lua/?.lua;"..package.path
local F=require("craftEconomic.model")
local C=require("craftEconomic.config")
local json=require("dkjson")
local count=0
local function check(x,why) count=count+1;assert(x,why or ("assertion "..count)) end
local clock=1000
os.time=function() return clock end
local files,messages,writes,stopped={},{},0,false
config={dataPath="/test-only",generatedRecordIdPrefix="$custom",enchantableRecordTypes={"weapon","armor","clothing","book"}}
enumerations={log={ERROR=1,WARN=2,INFO=3},inventory={SET=0},container={SET=0},packetOrigin={CLIENT_GAMEPLAY=0}}
jsonInterface={load=function(path)return F.copy(files[path])end,
    quicksaveAtomic=function(path,data)writes=writes+1;files[path]=F.copy(data);return true end}
tes3mp={GetCell=function()return "Shop"end,GetRegion=function()return ""end,
    GetSHA256Hash=function(s)local n=F.hash(s);return string.format("%032x",n)..string.rep("0",32) end,
    SendMessage=function(pid,s)messages[pid]=assert(json.decode(s:sub(#"@@ARENA_CE1@@"+1)))end,
    LogMessage=function()end,StopServer=function()stopped=true end,
    DoesFilePathExist=function(p)return files[p:sub(#config.dataPath+2)]~=nil end,
    MessageBox=function()end,CustomMessageBox=function()end,GetInventoryChangesAction=function()return 1 end,
    GetObjectListAction=function()return 0 end}
local hooks,validators={},{},{}
customEventHooks={registerHandler=function(name,fn)hooks[name]=fn end,
    registerValidator=function(name,fn)validators[name]=fn end,makeEventStatus=function(a,b)return {validDefaultHandler=a,validCustomHandlers=b}end}
customCommandHooks={registerCommand=function()end}
local records={}
local function record(t,id,data,extra)
    local r={type=t,id=id,name=id,data=data or {}};for k,v in pairs(extra or {})do r[k]=v end
    records[t..":"..id:lower()]=r;return r
end
record("CELL","Shop",{}, {economyRegion="Новый регион"})
record("NPC_","Trader",{gold=1000,services=1+2+4+8+16+32+64+128+256+512+1024+4096+8192+65536,mercantile=40})
record("CLOT","robe",{value=100,enchantmentPoints=100})
record("MISC","gem",{value=50})
record("MISC","gold_001",{value=1})
record("CREA","soul",{soul=400})
record("SPEL","spell",{type=0},{effects={{id=14}}})
record("MGEF","14",{school=0,baseCost=1,effectFlags=1024+64+128+256})
local content={NormalizeId=function(s)return s:lower()end,IsReady=function()return true end,
    GetKey=function()return "test-plugin-key"end,GetRecord=function(t,id)return F.copy(records[t..":"..tostring(id):lower()])end}
content.GetItem=function(id)
    for _,t in ipairs({"CLOT","MISC","WEAP","BOOK"})do local r=content.GetRecord(t,id);if r then return r end end
end
content.GetCellReference=function(_,num,id) return {owner=num==2 and "Trader" or "",x=0,y=0,z=0}end
package.loaded.contentApi=content
WorldInstance={data={time={daysPassed=1}}}
local function item(id,n,extra)
    local v={refId=id,count=n,charge=-1,enchantmentCharge=-1,soul="",poisonId="",poisonCharges=0}
    for k,x in pairs(extra or {})do v[k]=x end;return v
end
local cell={entryFile="shop.json",visitors={0,1},data={packets={container={"1-0","2-0"}},objectData={
    ["1-0"]={refId="Trader",inventory={item("robe",5)},equipment={[0]=item("robe",1)},goldPool=1000},
    ["2-0"]={refId="chest",inventory={item("robe",2)}}}}}
local loads=0
function cell:LoadContainers()loads=loads+1 end
function cell:LoadObjectsMiscellaneous()end
LoadedCells={Shop=cell}
Players={}
for pid=0,1 do
    local p={pid=pid,accountName="Тест"..pid,accountFile="test"..pid..".json",language="RU",generatedRecordsReceived={},
        data={inventory={item("gold_001",10000),item("robe",1),item("gem",2,{soul="soul"})},
        equipment={},spellbook={"spell"},stats={fatigueCurrent=100,fatigueBase=100},recordLinks={},
        location={posX=0,posY=0,posZ=0},skills={Enchant={base=100},Mercantile={base=75}},
        attributes={Intelligence={base=100},Luck={base=100}}}}
    function p:IsLoggedIn()return true end
    function p:LoadInventory()loads=loads+1 end
    function p:LoadEquipment()end
    Players[pid]=p
end
RecordStores={}
for _,kind in ipairs({"enchantment","weapon","armor","clothing","book"})do
    RecordStores[kind]={recordstoreFile=kind..".json",data={generatedRecords={},permanentRecords={},recordLinks={}},LoadGeneratedRecords=function()end}
end
package.loaded.questIndexStore={Hash=function(s)return tostring(F.hash(s))end,IsTrusted=function()return true end,IsQuestItem=function(id)return id=="quest_book" end}
questIndexStore=package.loaded.questIndexStore
local Phase=require("questItemPhasing")
local M=require("craftEconomic")
M.Initialize();check(M.ready,"initialization")
local merchant={refId="Trader",uid="1-0"}
local seq=0
local function request(pid,value)
    seq=seq+1;value.v=1;value.seq=value.seq or seq;messages[pid]=nil
    clock=clock+1 -- below the per-second rate limit in regular cases
    check(M.HandleMessage(pid,"@@ARENA_CE1@@"..json.encode(value)))
    return messages[pid]
end
local function trade(pid,buy,sell)
    M.OnDialogue(pid,"Shop",{["1-0"]={dialogueChoiceType=3}})
    return request(pid,{op="quote",kind="trade",merchant=merchant,buy=buy or {},sell=sell or {}})
end
check(not M.HandleMessage(0,"ordinary chat"))
local denied=request(0,{op="quote",kind="trade",merchant=merchant})
check(denied.error=="dialogue_required")
local q=trade(0,{item("robe",1)})
check(q.ok and q.quote.region=="Новый регион" and q.quote.net<0 and q.quote.tax>0,"regional price")
local net=q.quote.net;local before=F.gold(F.inventory(Players[0].data.inventory))
local token=q.quote.token
local r=request(0,{op="commit",token=token})
check(r.ok and F.gold(F.inventory(Players[0].data.inventory))==before+net,"paid exactly once")
check(cell.data.objectData["1-0"].inventory[1].count==4,"merchant inventory retains equipped robe")
local after=F.fingerprint(Players[0].data.inventory);local writeCount=writes
r=request(0,{op="commit",token=token})
check(r.ok and writes==writeCount and F.fingerprint(Players[0].data.inventory)==after,"duplicate commit is read-only")
check(files["craftEconomic-pending.json"].writes[1]==nil,"journal checkpoint cleared")
check(trade(0,{item("robe",10)}).error=="stock_changed","cannot buy equipped or unavailable items")
q=trade(0,{item("robe",1),item("robe",1)})
check(q.ok and q.quote.lines[1].count==2,"matching variants pooled")
local old=q.quote.token
local other=trade(1,{item("robe",1)})
check(request(1,{op="commit",token=other.quote.token}).ok)
check(request(0,{op="commit",token=old}).error=="market_changed","concurrent quote revision")
q=trade(0,{}, {item("robe",1)})
token=q.quote.token;Players[0].data.inventory[#Players[0].data.inventory+1]=item("gem",1)
check(request(0,{op="commit",token=token}).error=="inventory_changed","snapshot invalidation")
q=trade(0,{}, {item("robe",1)})
clock=clock+30
check(request(0,{op="commit",token=q.quote.token}).error=="quote_expired")
local e={op="quote",kind="enchant",item=item("robe",1),gem=item("gem",1,{soul="soul"}),castType=2,name="Роба света",
    effects={{id=14,range=0,area=0,duration=1,magnitudeMin=1,magnitudeMax=1,skill=-1,attribute=-1}}}
q=request(0,F.copy(e));check(q.ok and q.quote.chance==100,"server enchant quote")
local oldGem=F.inventory(Players[0].data.inventory)[F.key(item("gem",1,{soul="soul"}))].count
token=q.quote.token;r=request(0,{op="commit",token=token})
check(r.ok and r.created and r.refId and RecordStores.clothing.data.generatedRecords[r.refId],"server generated gear")
check(F.inventory(Players[0].data.inventory)[F.key(item("gem",1,{soul="soul"}))].count==oldGem-1,"one soul gem consumed")
after=F.fingerprint(Players[0].data.inventory);writeCount=writes
check(request(0,{op="commit",token=token}).ok and writes==writeCount and F.fingerprint(Players[0].data.inventory)==after,"enchant retry")
e.effects[1].id=999;check(request(0,e).error=="unknown_effect")
check(validators.OnRecordDynamic({},0,{{enchantmentId="cheat"}},"clothing").validDefaultHandler==false)
check(validators.OnRecordDynamic({},0,{{name="written book"}},"book")==nil,"writing remains available")
check(M.MarkStolen(0,item("robe",1),1))
check(trade(0,{}, {item("robe",1)}).error=="stolen_goods")
C.fences.trader=true
check(trade(0,{}, {item("robe",1)}).ok,"configured fence")
C.fences.trader=nil
Players[0].data.craftEconomic.stolen={}
M.RegisterRecipe("repair",{name="Test recipe",ingredients={item("robe",1)},output=item("gem",1),gold=3})
q=request(0,{op="quote",kind="recipe",recipe="repair"});check(q.ok)
check(request(0,{op="commit",token=q.quote.token}).ok,"trusted recipe transaction")
check(request(0,{op="quote",kind="recipe",recipe="client-invented"}).error=="unknown_recipe")
-- Malformed JSON and PID isolation cannot execute a transaction.
for _,raw in ipairs({"{",string.rep("[",17)..string.rep("]",17),"{}x",string.rep(" ",50000)})do
    messages[0]=nil;check(M.HandleMessage(0,"@@ARENA_CE1@@"..raw));check(messages[0]==nil)
end
q=trade(1,{},{});check(request(0,{op="commit",token=q.quote.token}).error=="quote_expired","token belongs to one player")
-- Buying container stock never consumes the merchant's worn clothes.
cell.data.objectData["1-0"].inventory={item("robe",1)}
cell.data.objectData["1-0"].equipment={[0]=item("robe",1)}
cell.data.objectData["2-0"].inventory={item("robe",2)}
q=trade(0,{item("robe",1)})
check(q.ok and request(0,{op="commit",token=q.quote.token}).ok)
check(cell.data.objectData["1-0"].inventory[1].count==1 and cell.data.objectData["2-0"].inventory[1].count==1,"NPC worn robe remains after buying owned chest stock")
-- Stale client wallets cannot overwrite a managed merchant balance.
local misc={["1-0"]={refId="Trader",goldPool=999999,lastGoldRestockDay=99999,lastGoldRestockHour=0}}
validators.OnObjectMiscellaneous({},0,"Shop",misc)
check(misc["1-0"].goldPool==cell.data.objectData["1-0"].goldPool,"authoritative wallet")
-- Advanced operations use the same review/commit path as barter.
local function menuChoice(pid,index)
    check(M.OnGUIAction(pid,43821,tostring(index)))
end
M.OnDialogue(0,"Shop",{["1-0"]={dialogueChoiceType=3}})
request(0,{op="menu",merchant=merchant});menuChoice(0,1);menuChoice(0,0) -- invest then confirm
check(Players[0].data.craftEconomic.shares["Shop:1-0"]==C.shareStep,"persistent shop share")
-- A second confirmation of the same GUI has no effect.
after=F.fingerprint(Players[0].data.inventory);menuChoice(0,0)
check(after==F.fingerprint(Players[0].data.inventory))
-- A quotation retransmission is stable, including its token.
local duplicate={op="quote",kind="trade",merchant=merchant,buy={},sell={},seq=9000}
local first=request(0,F.copy(duplicate));local again=request(0,F.copy(duplicate))
check(first.quote.token==again.quote.token,"quote retry has stable token")
-- No quote accepts item count, gold, unknown services or client output records as authority.
check(trade(0,{item("robe",C.maxCount+1)}).error=="invalid_item")
check(trade(0,{item("gold_001",1)}).error=="invalid_item")
-- Server-created records keep normal record-type IDs and transfer their links atomically.
Players[0].data.inventory[#Players[0].data.inventory+1]=item("robe",1)
local create=F.copy(e);create.effects[1].id=14
q=request(0,create);check(q.ok)
r=request(0,{op="commit",token=q.quote.token});check(r.ok and r.created and r.refId:match("^%$custom_clothing_ce_"))
local created=r.refId;local generatedItem=item(created,1,{enchantmentCharge=400})
q=trade(0,{}, {generatedItem});check(q.ok and request(0,{op="commit",token=q.quote.token}).ok)
check(cell.data.recordLinks.clothing[created][1]=="1-0","cell retains sold generated item")
check(RecordStores.clothing.data.recordLinks[created].cells[1]=="Shop","store retains cell link")
check(not F.inventory(Players[0].data.inventory)[F.key(generatedItem)],"generated item moved, not duplicated")
local reject=validators.OnContainer({},1,"Shop",{{uniqueIndex="1-0"}})
check(reject and reject.validDefaultHandler==false,"other visitor cannot overwrite stock with stale SET")
q=trade(1,{generatedItem});check(q.ok and request(1,{op="commit",token=q.quote.token}).ok)
check(Players[1].data.recordLinks.clothing and Players[1].data.recordLinks.clothing[#Players[1].data.recordLinks.clothing]==created,"buyer retains generated record")
check(not cell.data.recordLinks.clothing[created],"last cell inventory link removed after purchase")
-- A committed result remains pending on a snapshot-delivery exception; retry cannot repeat payment.
q=trade(1,{}, {item("robe",1)});check(q.ok)
local oldLoad=Players[1].LoadInventory
Players[1].LoadInventory=function()error("simulated delivery failure")end
local goldBefore=F.gold(F.inventory(Players[1].data.inventory))
r=request(1,{op="commit",token=q.quote.token})
check(r.error=="delivery_pending" and F.gold(F.inventory(Players[1].data.inventory))==goldBefore+q.quote.net)
writeCount=writes;Players[1].LoadInventory=oldLoad
check(request(1,{op="commit",token=q.quote.token}).ok and writes==writeCount,"delivery retry is read-only")
-- Inventory annotations and personal quest sources survive a business transaction.
record("BOOK","quest_book",{value=10,scroll=0})
cell.data.objectData["2-0"].inventory[#cell.data.objectData["2-0"].inventory+1]=item("quest_book",1,{questPhaseEligible=true,questPhaseCount=1,serverTag="keep"})
q=trade(0,{item("quest_book",1)});check(q.ok)
check(request(0,{op="commit",token=q.quote.token}).ok)
local canonical=F.inventory(cell.data.objectData["2-0"].inventory)[F.key(item("quest_book",1))]
check(canonical.count==1 and canonical.questPhaseCount==1 and canonical.serverTag=="keep","canonical quest stack retained")
check(Phase.GetVisibleContainerCountForData(Players[0].data,"Shop","2-0",canonical)==0,"one claim for buyer")
check(Phase.GetVisibleContainerCountForData(Players[1].data,"Shop","2-0",canonical)==1,"other player's copy remains")
check(trade(0,{item("quest_book",1)}).error=="stock_changed","cannot buy canonical quest source twice")
q=trade(1,{item("quest_book",1)});check(q.ok and request(1,{op="commit",token=q.quote.token}).ok)
check(F.inventory(cell.data.objectData["2-0"].inventory)[F.key(item("quest_book",1))].count==1)
-- A normal unrelated item retains arbitrary server-owned metadata too.
Players[0].data.inventory[#Players[0].data.inventory+1]=item("gem",1,{serverTag="custom module"})
q=trade(0,{}, {item("robe",1)});check(q.ok and request(0,{op="commit",token=q.quote.token}).ok)
check(F.inventory(Players[0].data.inventory)[F.key(item("gem",1))].serverTag=="custom module","untouched metadata preserved")
-- Selling a personal copy back creates shared stock, not a new personal source.
q=trade(0,{}, {item("quest_book",1)});check(q.ok and request(0,{op="commit",token=q.quote.token}).ok)
local shared=F.inventory(cell.data.objectData["1-0"].inventory)[F.key(item("quest_book",1))]
check(shared.questPhaseExplicitShared==true and shared.questPhaseEligible==false,"sale marked shared")
clock=clock+30
local delayed=validators.OnContainer({},1,"Shop",{{uniqueIndex="1-0"}})
check(delayed and not delayed.validDefaultHandler,"managed stock rejects delayed private/full snapshots after the short guard expires")
-- A rejected journal preparation cannot claim a quest source in the live profile.
content.GetCellReference=function(_,num)return {owner=(num==2 or num==3) and "Trader" or "",x=0,y=0,z=0}end
cell.data.objectData["3-0"]={refId="other-chest",inventory={item("quest_book",1,{questPhaseEligible=true,questPhaseCount=1})}}
q=trade(0,{item("quest_book",2)});check(q.ok) -- one shared copy and one new personal source
local beforeClaim=F.copy(Players[0].data.questItemPhasing)
local goldBeforeClaim=F.gold(F.inventory(Players[0].data.inventory));local saveBeforeClaim=jsonInterface.quicksaveAtomic
jsonInterface.quicksaveAtomic=function(path,data)if path=="craftEconomic-pending.json" then return false end;return saveBeforeClaim(path,data)end
check(request(0,{op="commit",token=q.quote.token}).error=="journal_write_failed")
jsonInterface.quicksaveAtomic=saveBeforeClaim
check(F.gold(F.inventory(Players[0].data.inventory))==goldBeforeClaim,"failed prepare cannot spend gold")
check(Phase.GetVisibleContainerCountForData(Players[0].data,"Shop","3-0",cell.data.objectData["3-0"].inventory[1])==1,"failed prepare cannot consume a claim")
-- Merchant enchanting posts the same exact tax to the regional treasury.
Players[0].data.inventory[#Players[0].data.inventory+1]=item("robe",1)
Players[0].data.inventory[#Players[0].data.inventory+1]=item("gem",1,{soul="soul"})
M.OnDialogue(0,"Shop",{["1-0"]={dialogueChoiceType=7}})
local service=F.copy(create);service.merchant=merchant
q=request(0,service);check(q.ok and q.quote.chance==100 and q.quote.net<0)
local region=M.GetRegion(0);local treasury=M.GetMarket(region).treasury or 0
check(request(0,{op="commit",token=q.quote.token}).ok)
check((M.GetMarket(region).treasury or 0)==treasury+q.quote.tax,"enchant tax journaled with payment")
-- A prepared journal survives partial materialization and replays after-images.
local S=require("craftEconomic.storage")
local oldSave=jsonInterface.quicksaveAtomic;local attempts=0
jsonInterface.quicksaveAtomic=function(path,data)
    attempts=attempts+1;if attempts==3 then return false end
    return oldSave(path,data)
end
local ok=S.commit({{path="player/recover.json",data={version=7}},{path="craftEconomic.json",data={version=1,regions={},shops={}}}},"test")
check(not ok and stopped and files[S.journal].id=="test","fail closed after prepared journal")
jsonInterface.quicksaveAtomic=oldSave
check(S.recover() and files["player/recover.json"].version==7 and #files[S.journal].writes==0,"recovery replays after-images")
check(not S.apply({version=1,writes={{path="../outside.json",data={}}}}),"journal path validation")
-- Price continuity and bid/ask spread hold for arbitrary regions and skills.
for skill=0,100,5 do for d=0,30 do
    local ctx={region="region-"..d,day=d+0.9999,skill=skill,merchantSkill=40}
    local a=F.price(records["CLOT:robe"],item("robe",1),"buy",ctx,C)
    local b=F.price(records["CLOT:robe"],item("robe",1),"sell",ctx,C)
    check(a.total>b.total,"no same-market immediate arbitrage")
    local x=F.market(ctx.region,"clothing",d+0.99999,C,0)
    local y=F.market(ctx.region,"clothing",d+1.00001,C,0)
    check(math.abs(x-y)<0.0001,"continuous market")
end end
print("PASS CraftEconomic server/model/storage: "..count.." assertions (real Lua modules; engine/storage/network doubled)")
