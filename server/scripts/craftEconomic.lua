-- ArenaMP Craft and Economic v1 / Y107. Original GPL-3.0-or-later implementation.
-- Regions and values come from the server's actual plugin catalog. No external
-- Economy/Enchanting UI source, price tables, assets or player-side cheats used.
local M={version=1,ready=false}
local C=require("craftEconomic.config")
local Coefficients=require("economyCoefficients")
local F=require("craftEconomic.model")
local Storage=require("craftEconomic.storage")
local Phase=require("questItemPhasing")
local Content=require("contentApi")
local json=require("dkjson")
local prefix="@@ARENA_CE1@@"
local state={version=1,regions={},shops={}}
local sessions,quotes,limits,guards,menus={},{},{},{},{}
local cellGuards={}
local counter=0
local itemCache,referenceCache={},{}
local function normalize(s) return Content.NormalizeId(tostring(s or "")) end
local function now() return os.time() end
local function day() return tonumber(WorldInstance and WorldInstance.data and WorldInstance.data.time and WorldInstance.data.time.daysPassed) or 0 end
local function player(pid)
    local p=Players and Players[pid]
    return p and p:IsLoggedIn() and p or nil
end
local function reply(pid,value)
    value.v=1;local raw=json.encode(value)
    if #raw>60000 then raw=json.encode({v=1,seq=value.seq,ok=false,error="response_limit"}) end
    tes3mp.SendMessage(pid,prefix..raw,false)
end
local function errorReply(pid,seq,err) reply(pid,{seq=seq,ok=false,error=err}) end
local function skill(p,name)
    local v=(p.data.skills or {})[name];return F.clamp(tonumber(type(v)=="table" and v.base or v) or 0,0,100)
end
local function attribute(p,name)
    local v=(p.data.attributes or {})[name];return F.clamp(tonumber(type(v)=="table" and v.base or v) or 0,0,1000)
end
local recordKinds={weapon="WEAP",armor="ARMO",clothing="CLOT",book="BOOK",potion="ALCH",ingredient="INGR",miscellaneous="MISC"}
local function itemRecord(id,depth)
    depth=depth or 0;if depth>8 then return nil,"record_inheritance_limit" end
    local cacheKey=normalize(id)
    if itemCache[cacheKey] then return F.copy(itemCache[cacheKey]) end
    local r=Content.GetItem(id)
    if r then
        if not next(itemCache) or M.itemCacheSize<1024 then itemCache[cacheKey]=F.copy(r);M.itemCacheSize=M.itemCacheSize+1 end
        return r
    end
    -- Generated records inherit their authoritative base from the catalog.
    for kind,tag in pairs(recordKinds) do
        local store=RecordStores and RecordStores[kind]
        local dynamic=store and (store.data.generatedRecords[id] or store.data.permanentRecords[id])
        if dynamic and dynamic.baseId then
            local base=Content.GetRecord(tag,dynamic.baseId) or itemRecord(dynamic.baseId,depth+1)
            if base then
                base.nativeBase=base.nativeBase or base.id
                base.generatedOverlay=base.generatedOverlay or {}
                for k,v in pairs(dynamic) do if k~="baseId" then base.generatedOverlay[k]=F.copy(v) end end
                base.id=id;base.name=dynamic.name or base.name;base.enchantment=dynamic.enchantmentId or base.enchantment
                if dynamic.value~=nil then base.data.value=dynamic.value end
                return base
            end
        end
    end
    return nil,"unknown_item"
end
M.itemCacheSize=0
local function reference(cell,uid,refId)
    local key=cell..":"..uid..":"..refId
    if referenceCache[key]~=nil then return referenceCache[key] or nil end
    local refNum,mpNum=uid:match("^(%d+)%-(%d+)$")
    if not refNum or tonumber(mpNum)~=0 then return nil end
    local found=Content.GetCellReference(cell,tonumber(refNum),refId)
    if M.referenceCacheSize<512 then referenceCache[key]=found or false;M.referenceCacheSize=M.referenceCacheSize+1 end
    return found
end
M.referenceCacheSize=0
local function regionFor(p,cell)
    if C.personalCoefficients~=false then return "personal-pricing", "Personal pricing" end
    local record=Content.GetRecord("CELL",cell)
    local base=cell
    for _=1,8 do
        if record then break end
        local store=RecordStores and RecordStores.cell
        local dynamic=store and (store.data.generatedRecords[base] or store.data.permanentRecords[base])
        if not dynamic or not dynamic.baseId then break end
        base=dynamic.baseId;record=Content.GetRecord("CELL",base)
    end
    local region=record and (record.economyRegion or record.region)
    if not region or region=="" then region=tes3mp.GetRegion(p.pid) end
    if not region or region=="" then region=(p.data.location or {}).regionName end
    if not region or region=="" then return normalize("cell:"..cell),cell end
    return normalize(region),region
end
local function economicData(p)
    local data=F.copy(p.data.craftEconomic or {})
    data.receipts=data.receipts or {};data.recent=data.recent or {};data.recentOrder=data.recentOrder or {}
    data.stolen=data.stolen or {};data.shares=data.shares or {};data.consignments=data.consignments or {}
    return data
end
local function identifier(p)
    counter=counter+1
    return tes3mp.GetSHA256Hash(p.accountName..":"..now()..":"..counter..":"..tostring({})):sub(1,32)
end
local function currentCell(p) return tes3mp.GetCell(p.pid) end
local function actorContext(p,merchant,needServices)
    if type(merchant)~="table" or type(merchant.uid)~="string" or not merchant.uid:match("^%d+%-%d+$")
        or type(merchant.refId)~="string" or #merchant.refId>256 then return nil,"invalid_merchant" end
    local cellName=currentCell(p);local cell=LoadedCells and LoadedCells[cellName]
    if cell and cell.HasFullContainerData and not cell:HasFullContainerData() then return nil,"inventory_pending" end
    local object=cell and cell.data.objectData[merchant.uid]
    if not object or normalize(object.refId)~=normalize(merchant.refId) then return nil,"merchant_not_loaded" end
    local npc=Content.GetRecord("NPC_",object.refId) or Content.GetRecord("CREA",object.refId)
    if not npc then return nil,"invalid_merchant" end
    local proof=sessions[p.pid]
    if not proof or proof.player~=p or proof.cell~=cellName or proof.uid~=merchant.uid or proof.untilTime<now() then return nil,"dialogue_required" end
    local position=object.location or object.position or reference(cellName,merchant.uid,object.refId)
    if position then
        local x=position.posX or position.x;local y=position.posY or position.y;local z=position.posZ or position.z
        if x and y and z then
            local loc=p.data.location or {}
            if ((loc.posX or 0)-x)^2+((loc.posY or 0)-y)^2+((loc.posZ or 0)-z)^2>C.quoteDistance^2 then return nil,"merchant_out_of_range" end
        end
    end
    if needServices and npc.type=="NPC_" and not F.bit(npc.data.services or 0,needServices) then return nil,"service_unavailable" end
    if object.deathState and object.deathState~=0 or object.stats and (object.stats.healthCurrent or 1)<=0 then return nil,"merchant_dead" end
    proof.untilTime=now()+300
    local key=cellName..":"..merchant.uid
    return {cell=cell,cellName=cellName,object=object,npc=npc,uid=merchant.uid,key=key}
end
local function walletFor(shop)
    local object=shop.object
    local amount=F.clamp(tonumber(object.goldPool) or tonumber(shop.npc.data.gold) or 0,0,C.maxGold)
    local at=tonumber(object.craftEconomicGoldDay) or tonumber(object.lastGoldRestockDay) or day()
    if day()>at then amount=math.max(amount,F.clamp(tonumber(shop.npc.data.gold) or 0,0,C.maxGold)) end
    return amount
end
local services={WEAP=1,ARMO=2,CLOT=4,BOOK=8,INGR=16,LOCK=32,PROB=64,LIGH=128,APPA=256,REPA=512,MISC=1024,ALCH=8192}
local function accepts(shop,record)
    local flags=shop.npc.data.services or 0
    return F.bit(flags,services[record.type] or 1073741824)
        or record.enchantment and record.enchantment~="" and F.bit(flags,4096)
end
local function equipped(object)
    local out={}
    for _,value in pairs(object.equipment or {}) do
        local item=F.copy(value);item.count=1;item=F.item(item)
        if item then out[F.key(item)]=(out[F.key(item)] or 0)+1 end
    end
    return out
end
local function contextFor(p,shop)
    local region,display=regionFor(p,currentCell(p))
    local market=state.regions[region] or {pressure={},revision=0,day=day()}
    local override=C.regions[region] or {};local tax=override.taxRate or C.taxRate
    local faction=shop and shop.npc.faction
    local rank=faction and (p.data.factionRanks or {})[faction]
    if type(rank)=="table" then rank=rank.rank end
    local discount=type(rank)=="number" and rank>=0 and F.clamp((rank+1)/10,0,1) or 0
    local fence=shop and (C.fences[normalize(shop.npc.id)] or C.fenceFactions[normalize(faction)]) or false
    local pressure=F.copy(market.pressure)
    local decay=math.exp(-math.max(0,day()-(market.day or day()))/7)
    for k,v in pairs(pressure) do pressure[k]=v*decay end
    return {region=region,display=display,day=day()+F.clamp(tonumber(WorldInstance.data.time.hour) or 0,0,24)/24,skill=skill(p,"Mercantile"),merchantSkill=shop and shop.npc.data.mercantile or 30,
        factionDiscount=discount,taxRate=F.clamp(tax,0,0.5),fence=fence,pressure=pressure,
        coefficients=Coefficients.Policy(p),pricingRevision=(p.data.customVariables or {}).economyRevision or 0,
        revision=market.revision or 0}
end
local function sanitizeLines(lines)
    if type(lines)~="table" or #lines>C.maxLines then return nil,"basket_limit" end
    local out,keys={},{}
    for k,v in pairs(lines) do
        if not F.number(k,1,#lines,true) then return nil,"invalid_basket" end
        local item=F.item(v)
        if not item or item.count>C.maxCount or normalize(item.refId)=="gold_001" then return nil,"invalid_item" end
        local key=F.key(item)
        if keys[key] then
            local previous=out[keys[key]];previous.count=previous.count+item.count
            if previous.count>C.maxCount then return nil,"basket_limit" end
        else out[#out+1]=item;keys[key]=#out end
    end
    return out
end
local function stockSources(shop)
    local out={};local cell=shop.cell
    for uid,object in pairs(cell.data.objectData) do
        if type(object.inventory)=="table" then
            local owns=uid==shop.uid
            if not owns then
                local ref=reference(shop.cellName,uid,object.refId or "")
                owns=ref and ref.owner~="" and normalize(ref.owner)==normalize(shop.npc.id)
            end
            if owns then out[#out+1]={uid=uid,object=object,map=F.inventory(object.inventory)} end
        end
    end
    table.sort(out,function(a,b) return a.uid<b.uid end)
    if type(shop.object.inventory)~="table" then return nil,"inventory_pending" end
    for _,source in ipairs(out) do if not source.map then return nil,"invalid_stock" end end
    return out
end
local function quoteTrade(p,request)
    local shop,err=actorContext(p,request.merchant)
    if not shop then return nil,err end
    local sources; sources,err=stockSources(shop);if not sources then return nil,err end
    local buy,sell;buy,err=sanitizeLines(request.buy or {});if not buy then return nil,err end
    sell,err=sanitizeLines(request.sell or {});if not sell then return nil,err end
    local inventory=F.inventory(p.data.inventory);if not inventory then return nil,"invalid_inventory" end
    local context=contextFor(p,shop);local total,taxes=0,0;local breakdown={};local data=economicData(p)
    local sourceFingerprints={};for _,source in ipairs(sources) do sourceFingerprints[source.uid]=F.fingerprint(source.object.inventory) end
    for _,side in ipairs({"buy","sell"}) do
        for _,item in ipairs(side=="buy" and buy or sell) do
            local record=itemRecord(item.refId);if not record then return nil,"unknown_item" end
            if not accepts(shop,record) then return nil,"merchant_item_type" end
            local price;price,err=F.price(record,item,side,context,C);if not price then return nil,err end
            if side=="sell" then
                if (data.stolen[F.key(item)] or 0)>0 and not context.fence then return nil,"stolen_goods" end
                if not F.take(inventory,item,item.count) then return nil,"inventory_changed" end
                -- Same-region buyback never pays more than the actual receipt.
                local receipt=(data.receipts[F.key(item)] or {})[1]
                if receipt and receipt.region==context.region then price.total=math.min(price.total,math.floor(receipt.unit*0.8)*item.count) end
            else
                local left=item.count
                for _,source in ipairs(sources) do
                    local have=source.map[F.key(item)]
                    local reserved=(equipped(source.object))[F.key(item)] or 0
                    local visible=have and Phase.GetVisibleContainerCountForData(p.data,shop.cellName,source.uid,have) or 0
                    local take=math.min(left,math.max(0,visible-reserved))
                    if take>0 then F.take(source.map,item,take);left=left-take end
                end
                if left>0 then return nil,"stock_changed" end
            end
            total=total+(side=="sell" and price.total or -price.total);taxes=taxes+price.tax
            if math.abs(total)>C.maxGold then return nil,"price_limit" end
            price.refId=item.refId;price.count=item.count;price.side=side;price.variantKey=F.key(item);breakdown[#breakdown+1]=price
        end
    end
    local wallet=walletFor(shop)
    local blocked=total<0 and F.gold(F.inventory(p.data.inventory)) < -total and "insufficient_gold"
        or wallet-total-taxes<0 and "merchant_gold" or nil
    return {kind="trade",shop=shop,context=context,buy=buy,sell=sell,net=total,taxes=taxes,wallet=wallet,
        fingerprint=F.fingerprint(p.data.inventory),stock=sourceFingerprints,lines=breakdown,blocked=blocked}
end
local function knownEffects(p)
    local out={}
    for _,id in pairs(p.data.spellbook or {}) do
        local record=Content.GetRecord("SPEL",id)
        if not record then local store=RecordStores and RecordStores.spell;record=store and store.data.generatedRecords[id] end
        if record and ((record.data and record.data.type) or record.subtype or 0)==0 then
            for _,effect in pairs(record.effects or {}) do
                local id=tonumber(effect.id or effect.effectId);if id then out[id]=true end
            end
        end
    end
    return out
end
local function quoteEnchant(p,request)
    local item=F.item(request.item);local gem=F.item(request.gem)
    local kind=F.number(request.castType,0,3,true)
    if not item or not gem or not kind or type(request.name)~="string" or #request.name<1 or #request.name>256
        or request.name:find("[%z\1-\31]") then return nil,"invalid_enchantment" end
    if logicHandler and not logicHandler.IsNameAllowed(request.name) then return nil,"invalid_name" end
    local map=F.inventory(p.data.inventory);if not map then return nil,"invalid_inventory" end
    if not F.take(map,item,1) or not F.take(map,gem,1) then return nil,"inventory_changed" end
    local record=itemRecord(item.refId);local soul=Content.GetRecord("CREA",gem.soul)
    local gemRecord=itemRecord(gem.refId)
    if not record or not gemRecord or gemRecord.type~="MISC" or not soul or (soul.data.soul or 0)<=0 then return nil,"invalid_soul" end
    local shop,err
    if request.merchant then shop,err=actorContext(p,request.merchant,65536);if not shop then return nil,err end end
    local stats=p.data.stats or {};local skills={}
    for _,name in ipairs({"Alteration","Conjuration","Destruction","Illusion","Mysticism","Restoration"}) do skills[name]=skill(p,name) end
    local model;model,err=F.enchant(request.effects,kind,record,soul.data.soul,{enchant=skill(p,"Enchant"),enchantBase=skill(p,"Enchant"),
        intelligence=attribute(p,"Intelligence"),luck=attribute(p,"Luck"),knownEffects=knownEffects(p),skills=skills,
        fatigue=(stats.fatigueCurrent or 0)/math.max(1,stats.fatigueBase or 1),itemCount=(F.inventory(p.data.inventory)[F.key(item)] or {}).count},
        function(t,id) return Content.GetRecord(t,id) end,C)
    if not model then return nil,err end
    local ctx=contextFor(p,shop);local price,tax=0,0
    if shop then
        ctx.service="enchant"
        local synthetic={id=item.refId,type="MISC",enchantment="enchanting-service",data={value=model.servicePrice}}
        local offer=F.price(synthetic,{count=1,charge=-1},"buy",ctx,C);if not offer then return nil,"price_limit" end;price=offer.total;tax=offer.tax;model.chance=100
    end
    if F.gold(map)<price then return nil,"insufficient_gold" end
    return {kind="enchant",shop=shop,context=ctx,model=model,item=item,gem=gem,name=request.name,castType=kind,
        record=record,soulSize=soul.data.soul,net=-price,taxes=tax,fingerprint=F.fingerprint(p.data.inventory)}
end
local function publicQuote(q)
    local lines={}
    for _,line in ipairs(q.lines or {}) do
        lines[#lines+1]={refId=line.refId,count=line.count,side=line.side,total=line.total,tax=line.tax,family=line.family,regional=line.regional}
    end
    return {kind=q.kind,token=q.token,expires=q.expires,region=q.context.display,net=q.net,tax=q.taxes,wallet=q.wallet,
        lines=lines,blocked=q.blocked,points=q.model and q.model.points,capacity=q.model and q.model.capacity,
        chance=q.model and q.model.chance,charge=q.model and q.model.charge,cost=q.model and q.model.cost,count=q.model and q.model.count}
end
local function remember(data,token,result)
    data.recent[token]=F.copy(result);data.recentOrder[#data.recentOrder+1]=token
    while #data.recentOrder>64 do data.recent[table.remove(data.recentOrder,1)]=nil end
end
local function removeEquipment(data,inventory)
    local map=F.inventory(inventory)
    for slot,item in pairs(data.equipment or {}) do
        local normalized=F.item(item)
        if normalized and not map[F.key(normalized)] then data.equipment[slot]=nil end
    end
end
local function addRecord(records,storeType,id,record,pData,account)
    local store=RecordStores[storeType];if not store or not store.recordstoreFile then return nil,"atomic_json_backend_required" end
    local data=records[storeType] or F.copy(store.data);records[storeType]=data
    data.generatedRecords[id]=record;data.recordLinks[id]=data.recordLinks[id] or {}
    if storeType~="enchantment" then
        data.recordLinks[id].players=data.recordLinks[id].players or {};table.insert(data.recordLinks[id].players,account)
        pData.recordLinks=pData.recordLinks or {};pData.recordLinks[storeType]=pData.recordLinks[storeType] or {}
        table.insert(pData.recordLinks[storeType],id)
    end
    return true
end
local function has(list,value)
    for _,v in pairs(list or {}) do if v==value then return true end end
    return false
end
local function put(list,value) if not has(list,value) then list[#list+1]=value end end
local function drop(list,value)
    local kept={};for _,v in pairs(list) do if v~=value then kept[#kept+1]=v end end
    for k in pairs(list) do list[k]=nil end
    for i,v in ipairs(kept) do list[i]=v end
end
local function ids(inventory)
    local out={};for _,item in pairs(inventory or {}) do if item.count>0 then out[item.refId]=true end end;return out
end
local function recordStore(id,patches)
    local kind=id:match("_(%a+)_")
    local data=kind and (patches[kind] or (RecordStores[kind] and RecordStores[kind].data))
    if data and data.generatedRecords[id] then return kind,data end
    for k,store in pairs(RecordStores) do
        data=patches[k] or store.data
        if data.generatedRecords[id] then return k,data end
    end
end
local function reconcileLinks(p,pData,cellData,shop,affected,patches)
    local function touch(kind)
        if not patches[kind] then patches[kind]=F.copy(RecordStores[kind].data) end
        return patches[kind]
    end
    local before,after=ids(p.data.inventory),ids(pData.inventory);local all=F.copy(before)
    for id in pairs(after) do all[id]=true end
    pData.recordLinks=pData.recordLinks or {}
    for id in pairs(all) do
        local kind,data=recordStore(id,patches)
        if kind then
            local links=data.recordLinks[id] or {};local own=(pData.recordLinks[kind] or {})
            if after[id] and (not has(links.players,p.accountName) or not has(own,id)) then
                data=touch(kind);links=data.recordLinks[id] or {};data.recordLinks[id]=links
                links.players=links.players or {};put(links.players,p.accountName)
                pData.recordLinks[kind]=own;put(own,id)
            elseif not after[id] and has(own,id) then
                data=touch(kind);links=data.recordLinks[id] or {};data.recordLinks[id]=links
                drop(own,id);links.players=links.players or {};drop(links.players,p.accountName)
                data.unlinkedRecordsToCheck=data.unlinkedRecordsToCheck or {};put(data.unlinkedRecordsToCheck,id)
            end
        end
    end
    if not cellData then return end
    cellData.recordLinks=cellData.recordLinks or {}
    for _,uid in ipairs(affected) do
        before=ids(shop.cell.data.objectData[uid].inventory);after=ids(cellData.objectData[uid].inventory);all=F.copy(before)
        for id in pairs(after) do all[id]=true end
        for id in pairs(all) do
            local kind,data=recordStore(id,patches)
            if kind then
                local group=cellData.recordLinks[kind] or {};cellData.recordLinks[kind]=group
                local uses=group[id] or {};group[id]=uses
                local links=data.recordLinks[id] or {}
                if after[id] then
                    put(uses,uid)
                    if not has(links.cells,shop.cellName) then
                        data=touch(kind);links=data.recordLinks[id] or {};data.recordLinks[id]=links
                        links.cells=links.cells or {};put(links.cells,shop.cellName)
                    end
                else
                    drop(uses,uid)
                    if #uses==0 then
                        group[id]=nil;data=touch(kind);links=data.recordLinks[id] or {};data.recordLinks[id]=links
                        links.cells=links.cells or {};drop(links.cells,shop.cellName)
                        data.unlinkedRecordsToCheck=data.unlinkedRecordsToCheck or {};put(data.unlinkedRecordsToCheck,id)
                    end
                end
            end
        end
    end
end
local function sendMissingRecords(pid,candidates)
    local p=player(pid);if not p then return end
    local received={};for _,id in pairs(p.generatedRecordsReceived or {}) do received[id]=true end
    local grouped={}
    for id in pairs(candidates) do
        if not received[id] then
            local kind=recordStore(id,{})
            if kind then grouped[kind]=grouped[kind] or {};grouped[kind][#grouped[kind]+1]=id end
        end
    end
    for kind,list in pairs(grouped) do RecordStores[kind]:LoadGeneratedRecords(pid,RecordStores[kind].data.generatedRecords,list,false) end
end
local function synchronize(p,shop,sourceIds)
    sendMissingRecords(p.pid,ids(p.data.inventory))
    p:LoadInventory();p:LoadEquipment()
    if shop then
        if sourceIds and #sourceIds>0 then
            local candidates={}
            for _,uid in pairs(sourceIds) do
                for id in pairs(ids((shop.cell.data.objectData[uid] or {}).inventory)) do candidates[id]=true end
            end
            for _,pid in pairs(shop.cell.visitors or {}) do
                if player(pid) then
                    sendMissingRecords(pid,candidates)
                    shop.cell:LoadContainers(pid,shop.cell.data.objectData,sourceIds)
                end
            end
        end
        shop.cell:LoadObjectsMiscellaneous(p.pid,shop.cell.data.objectData,{shop.uid},true)
    end
    guards[p.pid]={untilTime=now()+3,player=p}
    if shop and sourceIds then
        local entries=cellGuards[shop.cellName] or {};cellGuards[shop.cellName]=entries
        for _,uid in pairs(sourceIds) do entries[uid]=now()+3 end
    end
end
local function resultShop(p,result)
    local sync=result.sync
    if sync and currentCell(p)==sync.cell then
        local cell=LoadedCells[sync.cell]
        if cell and cell.data.objectData[sync.uid] then
            local targets={}
            for _,uid in ipairs(sync.sources or {}) do
                local object=cell.data.objectData[uid]
                if object and type(object.inventory)=="table" then targets[#targets+1]=uid end
            end
            return {cell=cell,cellName=sync.cell,uid=sync.uid},targets
        end
    end
end
local function execute(p,q,action)
    if not p.accountFile or q.shop and not q.shop.cell.entryFile then return nil,"atomic_json_backend_required" end
    if F.fingerprint(p.data.inventory)~=q.fingerprint then return nil,"inventory_changed" end
    if currentCell(p)~=q.cell then return nil,"location_changed" end
    local current=contextFor(p,q.shop)
    if current.region~=q.context.region or current.revision~=q.context.revision
        or current.pricingRevision~=q.context.pricingRevision then return nil,"market_changed" end
    if q.shop then
        local checked,err=actorContext(p,{uid=q.shop.uid,refId=q.shop.npc.id});if not checked then return nil,err end
        q.shop=checked
        for uid,fingerprint in pairs(q.stock or {}) do
            if not q.shop.cell.data.objectData[uid] or F.fingerprint(q.shop.cell.data.objectData[uid].inventory)~=fingerprint then return nil,"stock_changed" end
        end
    end
    local pData=F.copy(p.data);local economic=economicData(p);pData.craftEconomic=economic
    local inventory=F.inventory(pData.inventory);local cellData=q.shop and F.copy(q.shop.cell.data)
    local changedSources={}
    local newState=F.copy(state);local records={};local affected={}
    local function touchSource(uid) if not changedSources[uid] then changedSources[uid]=true;affected[#affected+1]=uid end end
    local result={ok=true,kind=q.kind,token=q.token,net=q.net}
    if q.kind=="trade" then
        local sources=stockSources(q.shop);if not sources then return nil,"inventory_pending" end
        if action=="consign" then
            if #economic.consignments>=C.maxConsignments or #q.sell==0 then return nil,"consignment_limit" end
            local payout=0
            for _,line in ipairs(q.lines) do if line.side=="sell" then payout=payout+line.total end end
            for _,item in ipairs(q.sell) do if not F.take(inventory,item,item.count) then return nil,"inventory_changed" end end
            economic.consignments[#economic.consignments+1]={id=q.token,shop=q.shop.key,region=q.context.region,
                day=day()+C.consignmentDays,payout=math.floor(payout*(1-C.consignmentFee)),items=F.copy(q.sell)}
            result.net=0;result.consigned=true
        else
            local xpValue=0
            for _,item in ipairs(q.buy) do
                local left=item.count
                for _,source in ipairs(sources) do
                    local have=source.map[F.key(item)]
                    local reserved=(equipped(source.object))[F.key(item)] or 0
                    local visible=have and Phase.GetVisibleContainerCountForData(pData,q.shop.cellName,source.uid,have) or 0
                    local take=math.min(left,math.max(0,visible-reserved))
                    if take>0 then
                        local personal=Phase.ClaimContainerForData(pData,q.shop.cellName,source.uid,have,take)
                        local shared=math.min(take-personal,math.max(0,have.count-Phase.GetContainerPhaseCount(have)))
                        local accepted=personal+shared
                        local transferred=F.copy(have)
                        transferred.questPhaseEligible=nil;transferred.questPhaseCount=nil
                        transferred.questPhaseExplicitShared=nil;transferred.questSourceId=nil
                        if shared>0 then F.take(source.map,item,shared) end
                        if accepted>0 then F.give(inventory,transferred,accepted);left=left-accepted;touchSource(source.uid) end
                    end
                end
                if left>0 then return nil,"stock_changed" end
                local batches=economic.receipts[F.key(item)] or {};economic.receipts[F.key(item)]=batches
                local paid=0;for _,line in ipairs(q.lines) do if line.side=="buy" and line.variantKey==F.key(item) then paid=line.total/item.count end end
                batches[#batches+1]={count=item.count,region=q.context.region,unit=paid,day=day()}
                while #batches>64 do table.remove(batches,1) end
            end
            for _,item in ipairs(q.sell) do
                local transferred=F.copy(inventory[F.key(item)] or item)
                if not F.take(inventory,item,item.count) then return nil,"inventory_changed" end
                local merchantMap
                for _,source in ipairs(sources) do if source.uid==q.shop.uid then merchantMap=source.map end end
                if not merchantMap then return nil,"inventory_pending" end
                F.give(merchantMap,transferred,item.count);touchSource(q.shop.uid)
                local sold=merchantMap[F.key(item)]
                if sold.questPhaseEligible or questIndexStore and questIndexStore.IsQuestItem(item.refId) then Phase.MarkContainerSharedDrop(sold) end
                local left=item.count;local batches=economic.receipts[F.key(item)] or {}
                local bought=0;for _,batch in ipairs(batches) do bought=bought+batch.count end
                local eligible=math.max(0,item.count-bought)
                for _,line in ipairs(q.lines) do
                    if line.side=="sell" and line.variantKey==F.key(item) then xpValue=xpValue+line.total*eligible/item.count end
                end
                while left>0 and #batches>0 do
                    local take=math.min(left,batches[1].count);left=left-take;batches[1].count=batches[1].count-take
                    if batches[1].count==0 then table.remove(batches,1) end
                end
                economic.stolen[F.key(item)]=math.max(0,(economic.stolen[F.key(item)] or 0)-item.count)
            end
            result.traded=true
            result.xp={skill="Mercantile",usage=0,factor=xpValue/50}
            if not F.changeGold(inventory,q.net) then return nil,"insufficient_gold" end
            local wallet=walletFor(q.shop)
            if wallet-q.net<0 then return nil,"merchant_gold" end
            cellData.objectData[q.shop.uid].goldPool=wallet-q.net-q.taxes
            if cellData.objectData[q.shop.uid].goldPool<0 then return nil,"merchant_gold" end
            for _,source in ipairs(sources) do
                if changedSources[source.uid] then
                    local object=cellData.objectData[source.uid];object.inventory=F.array(source.map)
                    object.craftEconomicStockVersion=(object.craftEconomicStockVersion or 0)+1
                end
            end
            local market=newState.regions[q.context.region] or {pressure={},revision=0};newState.regions[q.context.region]=market
            market.pressure=F.copy(q.context.pressure);market.revision=market.revision+1;market.day=day()
            market.treasury=(market.treasury or 0)+q.taxes
            for _,line in ipairs(q.lines) do market.pressure[line.family]=F.clamp((market.pressure[line.family] or 0)
                +(line.side=="buy" and 1 or -1)*math.sqrt(line.count)*C.pressureStep,-C.pressureLimit,C.pressureLimit) end
            local business=newState.shops[q.shop.key] or {shares={},profits=0};newState.shops[q.shop.key]=business
            business.profits=(business.profits or 0)+math.max(0,-q.net-q.taxes)*C.dividendShare
        end
    elseif q.kind=="enchant" then
        local success=math.random(0,99)<q.model.chance;result.created=success
        if not F.take(inventory,q.gem,1) then return nil,"inventory_changed" end
        if normalize(q.gem.refId)=="misc_soulgem_azura" then
            local empty=F.copy(q.gem);empty.soul="";F.give(inventory,empty,1)
        end
        if success then
            if not F.take(inventory,q.item,q.model.count) then return nil,"inventory_changed" end
            if not F.changeGold(inventory,q.net) then return nil,"insufficient_gold" end
            local recordType=({WEAP="weapon",ARMO="armor",CLOT="clothing",BOOK="book"})[q.record.type]
            local idBase=config.generatedRecordIdPrefix or "$custom"
            local enchantId=idBase.."_enchantment_ce_"..q.token;local itemId=idBase.."_"..recordType.."_ce_"..q.token
            local ench={subtype=q.castType,cost=q.model.cost,charge=q.model.charge,flags=0,effects={}}
            for _,effect in ipairs(q.model.effects) do
                ench.effects[#ench.effects+1]={id=effect.id,skill=effect.skill,attribute=effect.attribute,rangeType=effect.range,
                    area=effect.area,duration=effect.duration,magnitudeMin=effect.magnitudeMin,magnitudeMax=effect.magnitudeMax}
            end
            local ok,err=addRecord(records,"enchantment",enchantId,ench,pData,p.accountName);if not ok then return nil,err end
            local generated=F.copy(q.record.generatedOverlay or {})
            generated.baseId=q.record.nativeBase or q.item.refId;generated.name=q.name
            generated.enchantmentId=enchantId;generated.enchantmentCharge=q.model.charge
            ok,err=addRecord(records,recordType,itemId,generated,pData,p.accountName)
            if not ok then return nil,err end
            records.enchantment.recordLinks[enchantId].records={[recordType]={itemId}}
            F.give(inventory,{refId=itemId,charge=q.item.charge,enchantmentCharge=q.model.charge,soul="",poisonId="",poisonCharges=0},q.model.count)
            result.refId=itemId;result.name=q.name
            if not q.shop then
                local soul=q.soulSize or q.model.charge
                -- Same self-enchant skill-use factor as the existing ArenaMP mechanics.
                local factor=soul>=400 and 4 or soul>=180 and 3+(soul-180)/220
                    or soul>=60 and 2+(soul-60)/120 or 1+soul/60
                result.xp={skill="Enchant",usage=2,factor=factor}
            end
            if cellData then
                cellData.objectData[q.shop.uid].goldPool=walletFor(q.shop)-q.net-q.taxes
                local market=newState.regions[q.context.region] or {pressure={},revision=0,day=day()};newState.regions[q.context.region]=market
                market.treasury=(market.treasury or 0)+q.taxes;market.revision=(market.revision or 0)+1
                local business=newState.shops[q.shop.key] or {shares={},profits=0};newState.shops[q.shop.key]=business
                business.profits=(business.profits or 0)+math.max(0,-q.net-q.taxes)*C.dividendShare
            end
        else result.net=0 end
    elseif q.kind=="recipe" then
        for _,ingredient in ipairs(q.recipe.ingredients) do if not F.take(inventory,ingredient,ingredient.count) then return nil,"inventory_changed" end end
        F.give(inventory,q.recipe.output,q.recipe.output.count)
        if not F.changeGold(inventory,q.net) then return nil,"insufficient_gold" end
    elseif q.kind=="invest" or q.kind=="collect" then
        local business=newState.shops[q.shop.key] or {shares={},profits=0};newState.shops[q.shop.key]=business
        if q.kind=="invest" then
            local total=0;for _,share in pairs(business.shares) do total=total+share end
            if total+C.shareStep>1.000001 or (business.shares[p.accountName] or 0)+C.shareStep>C.maxShare then return nil,"share_limit" end
            if not F.changeGold(inventory,q.net) then return nil,"insufficient_gold" end
            business.shares[p.accountName]=(business.shares[p.accountName] or 0)+C.shareStep
            cellData.objectData[q.shop.uid].goldPool=walletFor(q.shop)-q.net
            economic.shares[q.shop.key]=(economic.shares[q.shop.key] or 0)+C.shareStep
        else
            local due=0
            for i=#economic.consignments,1,-1 do
                local entry=economic.consignments[i]
                if entry.shop==q.shop.key and day()>=entry.day then due=due+entry.payout;table.remove(economic.consignments,i) end
            end
            local paid=economic.dividendsPaid or {};economic.dividendsPaid=paid
            local share=business.shares[p.accountName] or 0
            local dividend=math.max(0,math.floor((business.profits or 0)*share)-(paid[q.shop.key] or 0))
            local wallet=walletFor(q.shop)
            dividend=math.min(dividend,wallet);paid[q.shop.key]=(paid[q.shop.key] or 0)+dividend
            cellData.objectData[q.shop.uid].goldPool=wallet-dividend
            result.net=due+dividend;F.changeGold(inventory,result.net)
        end
    else return nil,"unknown_operation" end
    if cellData then
        local object=cellData.objectData[q.shop.uid];object.craftEconomicGoldDay=day()
        object.lastGoldRestockDay=day();object.lastGoldRestockHour=0
        cellData.packets=cellData.packets or {};cellData.packets.miscellaneous=cellData.packets.miscellaneous or {}
        local found=false;for _,uid in pairs(cellData.packets.miscellaneous) do if uid==q.shop.uid then found=true end end
        if not found then table.insert(cellData.packets.miscellaneous,q.shop.uid) end
    end
    if q.kind=="trade" then Coefficients.RecordSales(pData,q.sell,os.time()) end
    pData.inventory=F.array(inventory);removeEquipment(pData,pData.inventory)
    if F.gold(inventory)>C.maxGold then return nil,"price_limit" end
    reconcileLinks(p,pData,cellData,q.shop,affected,records)
    if q.shop then result.sync={cell=q.shop.cellName,uid=q.shop.uid,sources=F.copy(affected)} end
    remember(economic,q.token,result)
    local writes={}
    for kind,data in pairs(records) do writes[#writes+1]={path="recordstore/"..RecordStores[kind].recordstoreFile,data=data,recordType=kind} end
    writes[#writes+1]={path="player/"..p.accountFile,data=pData}
    if cellData then writes[#writes+1]={path="cell/"..q.shop.cell.entryFile,data=cellData} end
    writes[#writes+1]={path="craftEconomic.json",data=newState}
    local ok,err=Storage.commit(writes,q.token);if not ok then return nil,err end
    -- Only publish after every after-image has reached its atomic destination.
    p.data=pData;state=newState;if cellData then q.shop.cell.data=cellData end
    for kind,data in pairs(records) do RecordStores[kind].data=data end
    local delivered,deliveryError=pcall(function()
        for _,kind in ipairs({"enchantment","weapon","armor","clothing","book"}) do
            local data=records[kind]
            if data then
                local ids={};for id in pairs(data.generatedRecords) do if id:find(q.token,1,true) then ids[#ids+1]=id end end
                RecordStores[kind]:LoadGeneratedRecords(p.pid,data.generatedRecords,ids,true)
            end
        end
        synchronize(p,q.shop,affected[1] and affected or nil)
        Coefficients.Push(p.pid)
    end)
    if not delivered then
        tes3mp.LogMessage(enumerations.log.ERROR,"[CraftEconomic] committed; snapshot delivery deferred: "..tostring(deliveryError))
        return nil,"delivery_pending"
    end
    return result
end

local GUI=43821
local text={
    title={"ArenaMP Craft and Economic","ArenaMP Craft and Economic"},
    unavailable={"System unavailable: server plugin catalog required.","Система недоступна: серверу нужен каталог плагинов."},
    region={"Region: ","Регион: "},tax={"Base tax: ","Базовый налог: "},
    invest={"Buy a %g%% shop share","Купить %g%% магазина"},collect={"Collect income and consignments","Получить доход и выручку"},
    consign={"Consign selected sale items","Отдать выбранные вещи на комиссию"},
    confirm={"Confirm","Подтвердить"},cancel={"Cancel","Отмена"},recipes={"Crafting recipes","Рецепты крафта"},
    retry_delivery={"Saved. Confirm again to receive the updated inventory.","Сохранено. Повторите подтверждение для получения обновлённого инвентаря."},
    done={"Operation completed.","Операция выполнена."},failed={"Operation refused: ","Операция отклонена: "},
}
local function tr(pid,key)
    local value=text[key][(Players[pid] and Players[pid].language=="RU") and 2 or 1]
    return key=="invest" and string.format(value,C.shareStep*100) or value
end
local function quoteRecipe(p,id)
    local recipe=C.recipes[id]
    if type(recipe)~="table" then return nil,"unknown_recipe" end
    if recipe.skill and skill(p,recipe.skill)<(recipe.minSkill or 0) then return nil,"skill_required" end
    local out=F.copy(recipe);out.output=F.item(out.output)
    if not out.output or not itemRecord(out.output.refId) or type(out.ingredients)~="table" or #out.ingredients<1 or #out.ingredients>16 then return nil,"invalid_recipe" end
    if out.regions then
        local region=contextFor(p).region
        if not out.regions[region] then return nil,"recipe_region" end
    end
    local inventory=F.inventory(p.data.inventory)
    for i,item in ipairs(out.ingredients) do
        item=F.item(item);if not item or not itemRecord(item.refId) then return nil,"invalid_recipe" end
        out.ingredients[i]=item;if not F.take(inventory,item,item.count) then return nil,"ingredients_required" end
    end
    local cost=F.number(out.gold or 0,0,C.maxGold,true);if not cost or F.gold(inventory)<cost then return nil,"insufficient_gold" end
    return {kind="recipe",recipe=out,net=-cost,taxes=0,context=contextFor(p),fingerprint=F.fingerprint(p.data.inventory)}
end
local nextGuardCleanup=0
local function prepare(p,request)
    if now()>=nextGuardCleanup then
        nextGuardCleanup=now()+10
        for cell,entries in pairs(cellGuards) do
            for uid,untilTime in pairs(entries) do if untilTime<now() then entries[uid]=nil end end
            if next(entries)==nil then cellGuards[cell]=nil end
        end
    end
    local previous=quotes[p.pid]
    if request.seq~=nil and previous and previous.player==p and previous.sequence==request.seq and previous.expires>=now() then return previous end
    local q,err
    if request.kind=="trade" then q,err=quoteTrade(p,request)
    elseif request.kind=="enchant" then q,err=quoteEnchant(p,request)
    elseif request.kind=="recipe" then q,err=quoteRecipe(p,request.recipe)
    else return nil,"unknown_operation" end
    if q then
        q.player=p;q.cell=currentCell(p);q.expires=now()+C.quoteSeconds;q.token=identifier(p);q.sequence=request.seq
        quotes[p.pid]=q
    end
    if not q then quotes[p.pid]=nil end
    return q,err
end
function M.Commit(pid,token,action)
    local p=player(pid);if not p then return nil,"not_logged_in" end
    local data=economicData(p)
    if data.recent[token] then
        local result=data.recent[token]
        local delivered=pcall(function()
            if result.refId then
                for _,kind in ipairs({"weapon","armor","clothing","book"}) do
                    local store=RecordStores[kind]
                    if store and store.data.generatedRecords[result.refId] then store:LoadGeneratedRecords(pid,store.data.generatedRecords,{result.refId},false) end
                end
            end
            local shop,targets=resultShop(p,result);synchronize(p,shop,targets)
        end)
        if not delivered then return nil,"delivery_pending" end
        return F.copy(result)
    end
    local q=quotes[pid]
    if not q or q.player~=p or q.token~=token or q.expires<now() then return nil,"quote_expired" end
    if action~=nil and action~="consign" then return nil,"unknown_operation" end
    if q.kind=="trade" and #q.buy+#q.sell==0 then return nil,"empty_basket" end
    if q.blocked and action~="consign" then return nil,q.blocked end
    local result,err=execute(p,q,action)
    if result then quotes[pid]=nil end
    return result,err
end
local function showMenu(pid,merchant)
    local p=player(pid);if not p then return end
    if not M.ready then tes3mp.MessageBox(pid,GUI,tr(pid,"unavailable"));return end
    local shop=merchant and actorContext(p,merchant)
    local ctx=contextFor(p,shop)
    local choices={};local labels={}
    local function add(name,op) choices[#choices+1]=op;labels[#labels+1]=tostring(name):gsub("[;\r\n]"," ") end
    if shop then
        add(tr(pid,"consign"),"consign");add(tr(pid,"invest"),"invest");add(tr(pid,"collect"),"collect")
    else
        local ids={};for id in pairs(C.recipes) do ids[#ids+1]=id end;table.sort(ids)
        for i=1,math.min(32,#ids) do add(C.recipes[ids[i]].name or ids[i],"recipe:"..ids[i]) end
    end
    add(tr(pid,"cancel"),"cancel")
    menus[pid]={player=p,shop=shop,choices=choices,untilTime=now()+60}
    local economic=economicData(p);local due=0
    for _,entry in ipairs(economic.consignments) do if day()>=entry.day then due=due+entry.payout end end
    local label=tr(pid,"title").."\n"..tr(pid,"region")..ctx.display.."\n"..tr(pid,"tax")
        ..string.format("%.1f%%",ctx.taxRate*100).."\n"
    if shop then label=label..(shop.npc.name or shop.npc.id).."\n"..string.format("%g%%: %d | %d",C.shareStep*100,math.ceil(math.max(100,shop.npc.data.gold or 100)*2*C.shareStep),due) end
    tes3mp.CustomMessageBox(pid,GUI,label,table.concat(labels,";"))
end
function M.OnGUIAction(pid,id,data)
    if id~=GUI then return false end
    local menu=menus[pid];local p=player(pid)
    if not p or not menu or menu.player~=p or menu.untilTime<now() then menus[pid]=nil;return true end
    local index=tonumber(data);local choice=index and menu.choices[index+1]
    if not choice or choice=="cancel" then menus[pid]=nil;return true end
    if choice=="confirm" then
        local result,err=M.Commit(pid,menu.token,menu.action)
        if not result and err=="delivery_pending" then
            menu.untilTime=now()+60
            tes3mp.CustomMessageBox(pid,GUI,tr(pid,"title").."\n"..tr(pid,"retry_delivery"),tr(pid,"confirm")..";"..tr(pid,"cancel"))
            return true
        end
        tes3mp.MessageBox(pid,GUI,result and tr(pid,"done") or tr(pid,"failed")..tostring(err))
        menus[pid]=nil;return true
    end
    local q,err
    if choice:sub(1,7)=="recipe:" then q,err=quoteRecipe(p,choice:sub(8))
    elseif choice=="consign" then
        q=quotes[pid]
        if not q or q.player~=p or q.kind~="trade" or not menu.shop or q.shop.key~=menu.shop.key then q=nil;err="select_sale_items" end
    else
        local shop=menu.shop
        if not shop then err="dialogue_required" else
            q={kind=choice,shop=shop,context=contextFor(p,shop),fingerprint=F.fingerprint(p.data.inventory),taxes=0,
                net=choice=="invest" and -math.ceil(math.max(100,shop.npc.data.gold or 100)*2*C.shareStep) or 0}
        end
    end
    if not q then tes3mp.MessageBox(pid,GUI,tr(pid,"failed")..tostring(err));return true end
    q.player=p;q.cell=currentCell(p);q.expires=now()+C.quoteSeconds;q.token=identifier(p);quotes[pid]=q
    menus[pid]={player=p,choices={"confirm","cancel"},token=q.token,action=choice=="consign" and "consign" or nil,untilTime=q.expires}
    local description=choice=="consign" and tr(pid,"consign") or choice=="invest" and tr(pid,"invest")
        or choice=="collect" and tr(pid,"collect") or (q.recipe and q.recipe.name) or tr(pid,"recipes")
    tes3mp.CustomMessageBox(pid,GUI,tr(pid,"title").."\n"..description.."\n"..tostring(q.net),tr(pid,"confirm")..";"..tr(pid,"cancel"))
    return true
end
local function boundedJson(raw)
    if #raw>49152 then return nil end
    local depth,quoted,escaped=0,false,false
    for i=1,#raw do
        local c=raw:sub(i,i)
        if quoted then
            if escaped then escaped=false elseif c=="\\" then escaped=true elseif c=='"' then quoted=false end
        elseif c=='"' then quoted=true
        elseif c=="{" or c=="[" then depth=depth+1;if depth>16 then return nil end
        elseif c=="}" or c=="]" then depth=depth-1;if depth<0 then return nil end end
    end
    if depth~=0 or quoted then return nil end
    local value,at,err=json.decode(raw,1,nil)
    if err or type(value)~="table" or raw:sub(at):find("%S") then return nil end
    return value
end
function M.HandleMessage(pid,message)
    if type(message)~="string" or message:sub(1,#prefix)~=prefix then return false end
    local p=player(pid);if not p then return true end
    local window=limits[pid]
    if not window or window.player~=p or window.second~=now() then window={player=p,second=now(),count=0};limits[pid]=window end
    window.count=window.count+1;if window.count>C.maxRequestsPerSecond then return true end
    local request=boundedJson(message:sub(#prefix+1));if not request then return true end
    local seq=F.number(request.seq,1,1000000000,true)
    if not seq or request.v~=1 then return true end
    if request.op=="hello" then reply(pid,{seq=seq,ok=true,enabled=M.ready,name="ArenaMP Craft and Economic",contentKey=Content.GetKey()});return true end
    if not M.ready or Storage.fatal then errorReply(pid,seq,"system_unavailable");return true end
    local ok,err=pcall(function()
        if request.op=="quote" then
            local q,reason=prepare(p,request)
            if q then reply(pid,{seq=seq,ok=true,quote=publicQuote(q)}) else errorReply(pid,seq,reason) end
        elseif request.op=="commit" then
            if type(request.token)~="string" or #request.token~=32 then errorReply(pid,seq,"invalid_token");return end
            local result,reason=M.Commit(pid,request.token)
            if result then result.seq=seq;result.sync=nil;reply(pid,result) else errorReply(pid,seq,reason) end
        elseif request.op=="menu" then showMenu(pid,request.merchant);reply(pid,{seq=seq,ok=true,menu=true})
        else errorReply(pid,seq,"unknown_operation") end
    end)
    if not ok then tes3mp.LogMessage(enumerations.log.ERROR,"[CraftEconomic] request failed: "..tostring(err));errorReply(pid,seq,"internal_error") end
    return true
end
function M.GetRegion(pid)
    local p=player(pid);if not p then return nil,"not_logged_in" end
    return regionFor(p,currentCell(p))
end
function M.GetPrice(pid,item,side,merchant)
    local p=player(pid);item=F.item(item)
    if not M.ready or not p or not item or (side~="buy" and side~="sell") then return nil,"invalid_price_request" end
    local shop,err
    if merchant then shop,err=actorContext(p,merchant);if not shop then return nil,err end end
    local record=itemRecord(item.refId);if not record then return nil,"unknown_item" end
    return F.price(record,item,side,contextFor(p,shop),C)
end
function M.Quote(pid,request)
    local p=player(pid);if not M.ready or not p or type(request)~="table" then return nil,"system_unavailable" end
    local q,err=prepare(p,F.copy(request));return q and publicQuote(q),err
end
function M.RegisterFence(refId,isFaction)
    assert(type(refId)=="string" and #refId>0 and #refId<=256,"invalid fence ID")
    local target=isFaction and C.fenceFactions or C.fences;target[normalize(refId)]=true
end
function M.GetMarket(region)
    return F.copy(state.regions[normalize(region)] or {pressure={},revision=0,day=day()})
end
function M.RegisterRecipe(id,recipe)
    assert(type(id)=="string" and #id<=128 and type(recipe)=="table","invalid recipe")
    C.recipes[id]=F.copy(recipe)
end
function M.MarkStolen(pid,item,count)
    local p=player(pid);item=F.item(item)
    if not p or not item or not F.number(count,1,1000000,true) then return false end
    p.data.craftEconomic=economicData(p);local ledger=p.data.craftEconomic.stolen
    ledger[F.key(item)]=(ledger[F.key(item)] or 0)+count
    return true
end
function M.OnDialogue(pid,cell,objects)
    local p=player(pid);if not p then return end
    for uid,object in pairs(objects or {}) do
        local kind=object.dialogueChoiceType
        if kind==3 or kind==7 then
            sessions[pid]={player=p,uid=uid,cell=cell,untilTime=now()+300}
            return
        end
    end
end
function M.GuardInventory(pid)
    local guard=guards[pid]
    if guard and guard.player==Players[pid] and guard.untilTime>=now()
        and tes3mp.GetInventoryChangesAction(pid)==enumerations.inventory.SET then
        Players[pid]:LoadInventory();return true
    end
    return false
end
function M.Initialize()
    M.ready=false
    local ok,writes=Storage.recover();if not ok then return end
    for _,write in ipairs(Database==nil and (writes or {}) or {}) do
        if tableHelper and tableHelper.fixNumericalKeys then tableHelper.fixNumericalKeys(write.data) end
        if write.recordType and RecordStores[write.recordType] then RecordStores[write.recordType].data=write.data end
        for _,cell in pairs(LoadedCells or {}) do if cell.entryFile and write.path=="cell/"..cell.entryFile then cell.data=write.data end end
    end
    if not C.enabled then return end
    for key,range in pairs({maxGold={1,2000000000},maxLines={1,64},maxCount={1,1000000},quoteSeconds={1,120},
        marketPeriodDays={1,365},regionalSwing={0,0.5},taxRate={0,0.5},pressureLimit={0,0.5},pressureStep={0,0.1},
        maxRequestsPerSecond={1,60},maxEffects={1,8},shareStep={0.01,0.5},maxShare={0.01,1},consignmentFee={0,1}}) do
        if not F.number(C[key],range[1],range[2]) then
            tes3mp.LogMessage(enumerations.log.ERROR,"[CraftEconomic] invalid config field: "..key);return
        end
    end
    if Database~=nil then
        tes3mp.LogMessage(enumerations.log.WARN,"[CraftEconomic] JSON persistence required; existing SQL gameplay retained")
        return
    end
    local saved=jsonInterface.load("craftEconomic.json")
    if not saved and (Storage.exists("craftEconomic.json") or Storage.exists("craftEconomic.json.bak")) then
        tes3mp.LogMessage(enumerations.log.ERROR,"[CraftEconomic] unreadable state; disabled without replacing it");return
    end
    state=saved or state
    if type(state)~="table" or state.version~=1 or type(state.regions)~="table" or type(state.shops)~="table" then
        tes3mp.LogMessage(enumerations.log.ERROR,"[CraftEconomic] invalid saved state; disabled without replacing it");return
    end
    if not Content.IsReady() then
        local loaded,err=Content.Reload(Content.settings)
        if not loaded then tes3mp.LogMessage(enumerations.log.WARN,"[CraftEconomic] catalog unavailable: "..tostring(err));return end
    end
    M.ready=true
    tes3mp.LogMessage(enumerations.log.INFO,"[CraftEconomic] Y107 active: regional markets, taxes, authoritative trade and enchanting")
end
customEventHooks.registerHandler("OnServerPostInit",function() M.Initialize() end)
customEventHooks.registerHandler("OnObjectDialogueChoice",function(status,pid,cell,objects)
    if status.validDefaultHandler then M.OnDialogue(pid,cell,objects) end
end)
customEventHooks.registerHandler("OnPlayerAuthentified",function(_,pid)
    reply(pid,{seq=0,ok=true,enabled=M.ready,name="ArenaMP Craft and Economic",contentKey=Content.GetKey()})
end)
customEventHooks.registerValidator("OnRecordDynamic",function(_,pid,records,kind)
    if not M.ready then return end
    if kind=="enchantment" then return customEventHooks.makeEventStatus(false,false) end
    if kind=="weapon" or kind=="armor" or kind=="clothing" or kind=="book" then
        for _,record in pairs(records or {}) do
            if record.enchantmentId and record.enchantmentId~="" then return customEventHooks.makeEventStatus(false,false) end
        end
    end
end)
customEventHooks.registerValidator("OnObjectMiscellaneous",function(_,pid,cellName,objects)
    if not M.ready then return end
    local cell=LoadedCells[cellName];if not cell then return end
    for uid,object in pairs(objects) do
        local saved=cell.data.objectData[uid]
        if saved and saved.craftEconomicGoldDay~=nil then
            -- A client-side restock or stale wallet cannot overwrite the ledger.
            object.goldPool=saved.goldPool or 0
            object.lastGoldRestockDay=saved.lastGoldRestockDay or 0
            object.lastGoldRestockHour=saved.lastGoldRestockHour or 0
        end
    end
end)
customEventHooks.registerValidator("OnContainer",function(_,pid,cellName,objects)
    if not M.ready or tes3mp.GetObjectListAction()~=enumerations.container.SET then return end
    local entries=cellGuards[cellName] or {};local cell=LoadedCells[cellName];if not cell then return end
    local complete=not cell.HasFullContainerData or cell:HasFullContainerData()
    local ids={}
    for _,object in pairs(objects) do
        local uid=object.uniqueIndex;local untilTime=entries[uid];local saved=cell.data.objectData[uid]
        local requested=has(cell.unusableContainerUniqueIndexes,uid)
        local managed=complete and saved and saved.craftEconomicStockVersion~=nil
            and (not saved.deathState or saved.deathState==0) and not requested
        if managed or untilTime and untilTime>=now() then ids[#ids+1]=uid end
    end
    if #ids>0 then
        local cell=LoadedCells[cellName];if cell then cell:LoadContainers(pid,cell.data.objectData,ids) end
        return customEventHooks.makeEventStatus(false,false)
    end
    for uid,untilTime in pairs(entries) do if untilTime<now() then entries[uid]=nil end end
    if next(entries)==nil then cellGuards[cellName]=nil end
end)
customEventHooks.registerHandler("OnCellUnload",function(_,pid,cellName)
    if type(cellName)=="string" and not LoadedCells[cellName] then cellGuards[cellName]=nil end
end)
customEventHooks.registerHandler("OnPlayerDisconnect",function(_,pid)
    sessions[pid]=nil;quotes[pid]=nil;guards[pid]=nil;menus[pid]=nil;limits[pid]=nil
end)
if customCommandHooks then
    customCommandHooks.registerCommand("economy",function(pid) showMenu(pid) end)
    customCommandHooks.registerCommand("craft",function(pid) showMenu(pid) end)
end
M.Model=F;M.Config=C
return M
