-- Ephemeral receipts for explicit REMOVE(old)+ADD(worn) inventory updates.
-- Nothing here is persisted; an equipment packet must never apply wear twice.
local M={}
local function same(a,b)
    return a and b and a.refId==b.refId and (a.charge or -1)==(b.charge or -1)
        and math.floor(a.enchantmentCharge or -1)==math.floor(b.enchantmentCharge or -1)
        and (a.soul or "")== (b.soul or "") and (a.poisonId or "")== (b.poisonId or "")
        and (a.poisonCharges or 0)==(b.poisonCharges or 0)
end
local function copy(a)
    local b={};for k,v in pairs(a) do b[k]=v end;return b
end
function M.observe(player,item,action,add,remove,at)
    at=at or os.time()
    player.inventoryTransitions=player.inventoryTransitions or {removed={},done={}}
    local state=player.inventoryTransitions
    for _,list in pairs(state) do
        for i=#list,1,-1 do if at-list[i].at>5 then table.remove(list,i) end end
    end
    if action==remove then
        state.removed[#state.removed+1]={item=copy(item),at=at}
    elseif action==add then
        for i=#state.removed,1,-1 do
            local old=state.removed[i]
            if old.item.refId==item.refId and old.item.count==item.count and not same(old.item,item) then
                state.done[#state.done+1]={old=old.item,new=copy(item),at=at}
                table.remove(state.removed,i);break
            end
        end
    end
    while #state.removed>32 do table.remove(state.removed,1) end
    while #state.done>32 do table.remove(state.done,1) end
end
function M.consume(player,old,new,at)
    local state=player.inventoryTransitions;if not state then return false end
    at=at or os.time();local current=old;local used={}
    for step=1,32 do
        local found=false
        for i,row in ipairs(state.done) do
            if not used[i] and at-row.at<=5 and same(current,row.old) then
                current=row.new;used[i]=true;found=true;break
            end
        end
        if not found then return false end
        if same(current,new) then
            for i=#state.done,1,-1 do if used[i] then table.remove(state.done,i) end end
            return true
        end
    end
    return false
end
return M
