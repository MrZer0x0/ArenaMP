-- Original ArenaMP Craft and Economic model. GPL-3.0-or-later.
-- Pure Lua 5.1: no engine calls, client input or disk access in price formulas.
local M={version=1}
function M.copy(v)
    if type(v)~="table" then return v end
    local out={};for k,x in pairs(v) do out[k]=M.copy(x) end;return out
end
function M.number(v,lo,hi,integer)
    v=tonumber(v)
    if not v or v~=v or v<lo or v>hi or (integer and v~=math.floor(v)) then return nil end
    return v
end
function M.clamp(v,lo,hi) return math.max(lo,math.min(hi,v)) end
function M.bit(v,b) return math.floor((tonumber(v) or 0)/b)%2==1 end
function M.hash(s)
    local n=5381;for i=1,#s do n=(n*33+s:byte(i))%2147483647 end
    -- Multiplication remains within Lua's exact integer range.
    return (n*48271)%2147483647
end
function M.item(v)
    if type(v)~="table" or type(v.refId)~="string" or #v.refId<1 or #v.refId>256 or v.refId:find("%z") then return nil end
    local count=M.number(v.count,1,2147483647,true)
    local charge=M.number(v.charge or -1,-1,100000000,true)
    local enchant=M.number(v.enchantmentCharge or -1,-1,100000000)
    local poison=M.number(v.poisonCharges or 0,0,1000000,true)
    if not count or not charge or not enchant or not poison or type(v.soul or "")~="string"
        or type(v.poisonId or "")~="string" or #(v.soul or "")>256 or #(v.poisonId or "")>256 then return nil end
    return {refId=v.refId,count=count,charge=charge,enchantmentCharge=enchant,soul=v.soul or "",
        poisonId=v.poisonId or "",poisonCharges=poison}
end
function M.key(v)
    local parts={v.refId:lower(),v.charge or -1,string.format("%.9g",v.enchantmentCharge or -1),
        v.soul or "",v.poisonId or "",v.poisonCharges or 0}
    local out={};for _,part in ipairs(parts) do part=tostring(part);out[#out+1]=#part..":"..part end
    return table.concat(out,"|")
end
function M.inventory(values)
    local map={}
    for _,v in pairs(values or {}) do
        local item=M.item(v);if not item then return nil,"invalid_inventory" end
        -- Only trusted inventory records use this path. Keep server annotations;
        -- request items are separately reduced to the wire schema by M.item.
        local trusted=M.copy(v);for field,value in pairs(item) do trusted[field]=value end;item=trusted
        local phase=item.questPhaseEligible==true and M.clamp(tonumber(item.questPhaseCount) or item.count,0,item.count) or 0
        if item.questPhaseEligible==true then item.questPhaseCount=phase end
        local k=M.key(item)
        if map[k] then
            local previous=map[k];local previousPhase=previous.questPhaseEligible==true and (previous.questPhaseCount or previous.count) or 0
            for field,value in pairs(item) do if previous[field]==nil then previous[field]=M.copy(value) end end
            previous.count=previous.count+item.count
            if phase+previousPhase>0 then
                previous.questPhaseEligible=true;previous.questPhaseCount=phase+previousPhase;previous.questPhaseExplicitShared=false
            end
        else map[k]=item end
    end
    return map
end
function M.array(map)
    local keys={};for k,v in pairs(map) do if v.count>0 then keys[#keys+1]=k end end;table.sort(keys)
    local out={};for _,k in ipairs(keys) do out[#out+1]=M.copy(map[k]) end;return out
end
function M.fingerprint(values)
    local map=M.inventory(values);if not map then return "invalid" end
    local rows={};for k,v in pairs(map) do rows[#rows+1]=k.."="..v.count end;table.sort(rows)
    return table.concat(rows,"\n") -- compare canonical data, not a collision-prone hash
end
function M.take(map,item,count)
    local k=M.key(item);local have=map[k]
    if not have or have.count<count then return false end
    have.count=have.count-count;return true
end
function M.give(map,item,count)
    local k=M.key(item)
    if not map[k] then map[k]=M.copy(item);map[k].count=0 end
    for field,value in pairs(item) do if map[k][field]==nil then map[k][field]=M.copy(value) end end
    map[k].count=map[k].count+count
end
function M.gold(map)
    local amount=0;for _,v in pairs(map) do if v.refId:lower()=="gold_001" then amount=amount+v.count end end;return amount
end
function M.changeGold(map,amount)
    if amount<0 then
        local remaining=-amount
        for _,v in pairs(map) do if v.refId:lower()=="gold_001" then
            local take=math.min(v.count,remaining);v.count=v.count-take;remaining=remaining-take
        end end
        return remaining==0
    end
    if amount>0 then M.give(map,{refId="gold_001",charge=-1,enchantmentCharge=-1,soul="",poisonId="",poisonCharges=0},amount) end
    return true
end
function M.family(record)
    local t=record.type
    if t=="WEAP" then return "arms" end
    if t=="ARMO" then return "armor" end
    if t=="CLOT" then return "clothing" end
    if t=="INGR" then return "ingredients" end
    if t=="ALCH" then return "potions" end
    if t=="BOOK" then return "books" end
    if t=="APPA" or t=="LOCK" or t=="PROB" or t=="REPA" then return "tools" end
    if record.enchantment and record.enchantment~="" then return "magic" end
    return "goods"
end
function M.market(region,family,day,cfg,pressure)
    local period=math.max(1,cfg.marketPeriodDays);local at=day/period;local week=math.floor(at)
    local t=at-week;t=t*t*(3-2*t)
    local function value(w) return (M.hash(region.."|"..family.."|"..w)%20001)/10000-1 end
    return M.clamp(1+cfg.regionalSwing*(value(week)*(1-t)+value(week+1)*t)
        +(pressure or 0),0.65,1.65)
end
function M.price(record,item,side,context,cfg)
    local base=M.number((record.data or {}).value or record.value or 0,0,cfg.maxGold)
    if not base then return nil,"invalid_item_value" end
    local family=M.family(record);local skill=M.clamp(context.skill or 0,0,100)
    local health=(record.data or {}).health
    if health and health>0 and item.charge>=0 then base=base*M.clamp(item.charge/health,0,1) end
    local pressure=((context.pressure or {})[family] or 0)
    local regional=cfg.personalCoefficients~=false and 1 or M.market(context.region,family,context.day,cfg,pressure)
    local aptitude=skill/100
    local merchant=M.clamp(context.merchantSkill or 30,0,100)/100
    local buy=math.max(1,base*regional*(cfg.buyMarkup+0.12*merchant-0.08*aptitude))
    local sell=math.max(0,base*regional*(cfg.sellShare+(cfg.expertSellShare-cfg.sellShare)*aptitude-0.05*merchant))
    if item.count>=10 and skill>=70 then buy=buy*(1-cfg.bulkDiscount) end
    sell=math.min(sell,buy*0.8)
    local rate=context.taxRate or cfg.taxRate
    if family=="ingredients" or family=="goods" then rate=rate*cfg.staplesTaxMultiplier end
    if record.enchantment and record.enchantment~="" then rate=rate*cfg.enchantedTaxMultiplier end
    rate=rate*(1-M.clamp(context.factionDiscount or 0,0,1))
    if skill>=75 then rate=rate*0.8 end
    local untaxed=side=="buy" and buy or sell
    local subtotal=(side=="buy" and math.ceil(untaxed-1e-8) or math.floor(untaxed+1e-8))*item.count
    local tax=base<cfg.untaxedBelow and 0 or math.ceil(subtotal*rate-1e-8)
    local total=side=="buy" and subtotal+tax or math.max(0,subtotal-tax)
    if context.fence then
        if base>cfg.fenceItemLimit then return nil,"fence_limit" end
        total=side=="sell" and math.floor(sell*item.count*cfg.fenceShare) or math.ceil(buy*item.count)
        tax=0
    end
    local policy=context.coefficients
    if policy then
        local service=context.service or "trade"
        local function n(v) return M.number(v,0.01,100) or 1 end
        local row=(policy.items or {})[string.lower(record.id or item.refId or "")] or {}
        local buyFactor=n(policy.buy)*n((policy.services or {})[service])*n(row.buy)
        local sellFactor=math.min(n(policy.sell)*n((policy.services or {})[service])*n(row.sell),buyFactor)
        local factor=M.clamp(side=="buy" and buyFactor or sellFactor,0.01,100)
        total=side=="buy" and math.ceil(total*factor-1e-8) or math.floor(total*factor+1e-8)
        subtotal=side=="buy" and math.ceil(subtotal*factor-1e-8) or math.floor(subtotal*factor+1e-8)
        tax=side=="buy" and math.max(0,total-subtotal) or math.max(0,subtotal-total)
    end
    if total>cfg.maxGold then return nil,"price_limit" end
    return {total=total,tax=tax,subtotal=subtotal,unit=total/item.count,
        family=family,regional=regional,base=base,taxRate=rate}
end
function M.enchant(effects,kind,record,soul,player,lookup,cfg)
    if type(effects)~="table" or #effects<1 or #effects>cfg.maxEffects then return nil,"invalid_effects" end
    local allowed=record.type=="BOOK" and (record.data or {}).scroll==1 and kind==0
        or (record.type=="ARMO" or record.type=="CLOT") and (kind==2 or kind==3)
        or record.type=="WEAP" and (kind==1 or kind==2 or kind==3)
    if not allowed then return nil,"invalid_enchantment_type" end
    local weaponType=tonumber((record.data or {}).type) or -1
    if record.type=="WEAP" then
        if (weaponType==9 or weaponType==10) and kind==1 then return nil,"invalid_enchantment_type" end
        if weaponType>=11 and kind==3 then return nil,"invalid_enchantment_type" end
    end
    if record.enchantment and record.enchantment~="" then return nil,"already_enchanted" end
    local function gmst(id,fallback) local r=lookup("GMST",id);return tonumber(r and r.data and r.data.value) or fallback end
    if kind==3 and soul<gmst("iSoulAmountForConstantEffect",400) then return nil,"weak_soul" end
    local capacity=math.floor(math.sqrt(math.max(0,((record.data or {}).enchantmentPoints or 0)+5))*100*gmst("fEnchantmentMult",0.1))
    local points,floored,service=0,0,0;local out={};local seen={}
    local schoolSkills={[0]="Alteration",[1]="Conjuration",[2]="Destruction",[3]="Illusion",[4]="Mysticism",[5]="Restoration"}
    for _,v in ipairs(effects) do
        local id=M.number(v.id,0,32767,true);local effect=id and lookup("MGEF",tostring(id))
        if not effect or not effect.data then return nil,"unknown_effect" end
        if not M.number(effect.data.baseCost,0,100000000) then return nil,"invalid_effect_cost" end
        local flags=effect.data.effectFlags or 0
        if not M.bit(flags,1024) then return nil,"effect_not_enchantable" end
        if not (player.knownEffects or {})[id] then return nil,"effect_not_known" end
        local e={id=id,range=M.number(v.range,0,2,true),area=M.number(v.area or 0,0,100,true),
            duration=M.number(v.duration or 0,0,1440,true),magnitudeMin=M.number(v.magnitudeMin or 0,0,1000,true),
            magnitudeMax=M.number(v.magnitudeMax or 0,0,1000,true),skill=M.number(v.skill or -1,-1,26,true),
            attribute=M.number(v.attribute or -1,-1,7,true)}
        for _,key in ipairs({"range","area","duration","magnitudeMin","magnitudeMax","skill","attribute"}) do if e[key]==nil then return nil,"invalid_effect" end end
        if e.magnitudeMin>e.magnitudeMax or not M.bit(flags,64*2^e.range) or (kind==3 and e.range~=0) then return nil,"invalid_effect_range" end
        if M.bit(flags,1) and e.skill<0 or M.bit(flags,2) and e.attribute<0 then return nil,"missing_effect_parameter" end
        if not M.bit(flags,1) then e.skill=-1 end;if not M.bit(flags,2) then e.attribute=-1 end
        if e.range==0 then e.area=0 end
        if kind==3 then e.duration=0 end
        local unique=id..":"..e.skill..":"..e.attribute
        if seen[unique] then return nil,"duplicate_effect" end;seen[unique]=true
        local low,high=e.magnitudeMin,e.magnitudeMax
        if M.bit(flags,8) then low,high=1,1;e.magnitudeMin,e.magnitudeMax=0,0 end
        local duration=kind==3 and gmst("fEnchantmentConstantDurationMult",100) or e.duration
        if kind~=3 and M.bit(flags,4) then duration=1;e.duration=0 end
        local raw=((math.max(1,low)+math.max(1,high))*math.max(1,duration)+math.max(1,e.area))
            *effect.data.baseCost*gmst("fEffectCostMult",5)*0.05
        local mult=(e.range==2 and 1.5 or 1)*(kind==1 and 2 or 1)
        if effect.data.school==2 or effect.data.school==5 then mult=mult*2 end
        if id==86 or id==88 then mult=mult*2 end
        local cost=(1+math.max(0,raw-1)*mult)*(kind==0 and 0.5 or 1)
        points=points+cost;floored=floored+math.floor(cost)
        local skill=M.clamp((player.skills or {})[schoolSkills[effect.data.school]] or 0,0,100)
        service=service+cost*(1+3*(1-skill/100)^2);out[#out+1]=e
    end
    if floored>capacity then return nil,"capacity_exceeded" end
    local skill=player.enchant or 0;local adjusted=points;local bonus=0
    if soul>=400 then bonus=30 elseif soul>=180 then bonus=30+(soul-180)/22
    elseif soul>=120 then bonus=15+(soul-120)/12 elseif soul>=60 then bonus=10+(soul-60)/12
    elseif soul>=30 then bonus=5+(soul-30)/6 else bonus=soul/6 end
    local count=record.type=="WEAP" and weaponType>=11 and M.clamp(player.itemCount or 1,1,20) or 1
    local ammo=record.type=="WEAP" and weaponType>=11
    local d=kind~=3 and 5+bonus or 0
    if (kind==0 or ammo) and points<10 then d=d+40-points*4 end
    if kind~=3 and points>30 then adjusted=30+(points-30)*1.5 end
    local fatigue=gmst("fFatigueBase",1.25)-gmst("fFatigueMult",0.5)*M.clamp(1-(player.fatigue or 1),0,1)
    local chance=((skill+d)-adjusted*gmst("fEnchantmentChanceMult",3)*(ammo and 1/count or 1)*count
        +(player.intelligence or 0)*0.2+(player.luck or 0)*0.1)*fatigue
    if kind==3 then
        if cfg.constantEffectNewLogic then
            local s=math.min(skill,player.enchantBase or skill)
            local limit=s<60 and -1 or s<70 and 5+s-60 or s<80 and 15+(s-70)*2 or s<90 and 35+(s-80)*3 or 65+(s-90)*4
            chance=(s>=100 or limit>=points) and 100 or 0
        else chance=chance*gmst("fEnchantmentConstantChanceMult",0.5) end
    end
    if kind~=3 and service>30 then service=30+(service-30)*1.5 end
    local price=service*gmst("fEnchantmentValueMult",10)*count*(ammo and 0.05 or 1)
        *(kind==3 and 2 or kind==0 and 0.04 or 1)
    return {effects=out,points=points,capacity=capacity,cost=kind==3 and 0 or floored,charge=kind==3 and 0 or soul,
        count=count,chance=M.clamp(math.floor(chance),0,100),servicePrice=math.ceil(math.max(1,price))}
end
return M
