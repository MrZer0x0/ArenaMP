-- Independent of ContentApi, ESM/ESP indexes and CraftEconomic availability.
local M = {}
local C = require("economyRules")
local json = require("dkjson")
local function clamp(v,lo,hi,fallback)
    v=tonumber(v)
    if not v or v~=v or v==math.huge or v==-math.huge then return fallback or 1 end
    return math.max(lo,math.min(hi,v))
end
local function id(v) return string.lower(tostring(v or "")) end
local function cv(data)
    data.customVariables=data.customVariables or {}
    return data.customVariables
end
local function personal(p)
    local key=tostring(p.accountName or p.name or "")
    local configured=(C.players or {})[key] or {}
    local saved=(p.data.customVariables or {}).economyCoefficients or {}
    return configured,saved
end
local function normalized(t)
    local out={};for key,row in pairs(t or {}) do
        local k=id(key)
        if out[k]~=nil then error("duplicate economy item ID: "..k) end
        out[k]=row
    end
    return out
end
local function number(t,k) return clamp(type(t)=="table" and t[k],0.01,100,1) end
local function salesFactor(data,refId,at)
    local cfg=C.sales or {}
    if cfg.enabled==false then return 1 end
    local history=(data.customVariables or {}).economySales or {}
    local row=history[id(refId)]
    if not row then return 1 end
    local hours=clamp(cfg.recoveryHours,1,8760,24)
    local units=clamp(row.units,0,100000000,0)*math.exp(-math.max(0,at-(tonumber(row.at) or at))/(hours*3600))
    return math.max(clamp(cfg.minimum,0.01,1,0.35),1/(1+units*clamp(cfg.perUnit,0,1,0.015)))
end
function M.Policy(p)
    local a,b=personal(p)
    local buy=number(C,"all")*number(C,"buy")*number(a,"all")*number(a,"buy")*number(b,"all")*number(b,"buy")
    local sell=number(C,"all")*number(C,"sell")*number(a,"all")*number(a,"sell")*number(b,"all")*number(b,"sell")
    if C.enabled==false then buy=1;sell=1 end
    local out={v=1,buy=clamp(buy,0.01,100),sell=clamp(sell,0.01,100),services={},items={}}
    out.sell=math.min(out.sell,out.buy)
    for _,service in ipairs({"trade","enchant","training","spellbuy","spellmaking","repair","travel"}) do
        out.services[service]=C.enabled==false and 1 or clamp(number(C.services,service)*number(a.services,service)*number(b.services,service),0.01,100)
    end
    if C.enabled~=false then
        local globalItems,accountItems,savedItems=normalized(C.items),normalized(a.items),normalized(b.items)
        local keys={}
        for _,map in ipairs({globalItems,accountItems,savedItems,(p.data.customVariables or {}).economySales or {}}) do
            for key in pairs(map) do keys[id(key)]=true end
        end
        local sorted={};for key in pairs(keys) do if key~="" and #key<=256 then sorted[#sorted+1]=key end end
        table.sort(sorted)
        -- The wire is bounded. Refuse an oversized authored configuration rather
        -- than silently discard one player's item rules.
        if #sorted>256 then error("economy coefficient policy exceeds 256 item IDs") end
        for _,key in ipairs(sorted) do
            local x=globalItems[key] or {};local y=accountItems[key] or {};local z=savedItems[key] or {}
            out.items[key]={buy=clamp(number(x,"buy")*number(y,"buy")*number(z,"buy"),0.01,100),
                sell=clamp(number(x,"sell")*number(y,"sell")*number(z,"sell")*salesFactor(p.data,key,os.time()),0.01,100)}
        end
    end
    if #json.encode(out)>58000 then error("economy policy exceeds 58 KB wire budget") end
    return out
end
function M.Factor(policy,service,refId,buying)
    local side=buying and "buy" or "sell"
    local row=(policy.items or {})[id(refId)] or {}
    local result=number(policy,side)*number(policy.services,service or "trade")*number(row,side)
    if not buying then
        local buy=number(policy,"buy")*number(policy.services,service or "trade")*number(row,"buy")
        result=math.min(result,buy)
    end
    return clamp(result,0.01,100)
end
function M.RecordSales(data,items,at)
    if C.enabled==false or (C.sales or {}).enabled==false then return end
    local cfg=C.sales or {};local vars=cv(data);vars.economySales=vars.economySales or {}
    vars.economyRevision=(vars.economyRevision or 0)+1
    local history=vars.economySales;at=at or os.time()
    local hours=clamp(cfg.recoveryHours,1,8760,24)
    for _,item in ipairs(items or {}) do
        local key=id(item.refId);local count=clamp(item.count,0,1000000,0)
        if key~="" and count>0 then
            local old=history[key] or {units=0,at=at}
            local units=clamp(old.units,0,100000000,0)*math.exp(-math.max(0,at-(tonumber(old.at) or at))/(hours*3600))
            history[key]={units=math.min(100000000,units+count),at=at}
        end
    end
    local keys={};for key,row in pairs(history) do keys[#keys+1]={key=key,at=tonumber(row.at) or 0} end
    table.sort(keys,function(a,b) if a.at==b.at then return a.key<b.key end return a.at<b.at end)
    local max=math.floor(clamp(cfg.maxItems,1,128,128))
    for i=1,#keys-max do history[keys[i].key]=nil end
end
function M.Push(pid)
    local p=Players and Players[pid]
    if not p or not p:IsLoggedIn() then return end
    local ok,policy=pcall(M.Policy,p)
    if not ok then tes3mp.LogMessage(enumerations.log.ERROR,"[EconomyCoefficients] invalid policy; pricing update rejected: "..tostring(policy));return end
    tes3mp.SendMessage(pid,"@@ARENA_ECO2@@"..json.encode(policy),false)
end
-- Per-player state stays in that player's existing persistent profile.
function M.SetPlayer(pid,values)
    local p=Players and Players[pid];if not p or type(values)~="table" then return false end
    local vars=cv(p.data);local previous=vars.economyCoefficients;vars.economyCoefficients=values
    local ok=pcall(M.Policy,p)
    if not ok then vars.economyCoefficients=previous;return false end
    vars.economyRevision=(vars.economyRevision or 0)+1
    p:QuicksaveToDrive();M.Push(pid);return true
end
customEventHooks.registerHandler("OnPlayerAuthentified",function(_,pid) M.Push(pid) end)
-- These are server responses, never player chat or requests.
customEventHooks.registerValidator("OnPlayerSendMessage",function(_,pid,message)
    if type(message)=="string" and message:find("@@ARENA_ECO2@@",1,true) then
        return customEventHooks.makeEventStatus(false,false)
    end
end)
return M
