-- Original ArenaMP implementation: furniture display + escrow + redo journal.
-- No client-supplied prices, stock, seller identities or inventory deltas are trusted.
local M = {}
local FILE = "playerVitrines.json"
local GOLD = "gold_001"
local GUI, PRICE = 923460, 923461
local db, menus, armed = nil, {}, {}
local function copy(t) return tableHelper.deepCopy(t) end
local function tr(pid, ru, en) return Players[pid].language == "RU" and ru or en end
local function say(pid, ru, en) tes3mp.SendMessage(pid, tr(pid, ru, en) .. "\n", false) end
local function alive(pid)
    local p = Players[pid]
    return db and p and p:IsLoggedIn() and (tonumber(p.data.stats.healthCurrent) or 0) > 0
        and not p.deathRecoveryActive and not (bountyRespawn and bountyRespawn.Pending(p))
end
local function fatal()
    db = nil
    tes3mp.LogMessage(enumerations.log.ERROR, "[PlayerVitrines] persistence failure; stopping to preserve transaction journal")
    tes3mp.StopServer(2)
end
local function persist(value)
    if not jsonInterface.saveAtomic(FILE, value) then fatal(); return false end
    return true
end
-- Single Lua callback is the transaction boundary. WAL first; profile second;
-- final journal commit last. Startup replays an interrupted profile write once.
local function commit(pid, nextDb, profile)
    if profile then nextDb.pending = {file=Players[pid].accountFile, data=profile} end
    if not persist(nextDb) then return false end
    db = nextDb
    if profile then
        Players[pid].data = profile
        if not jsonInterface.saveAtomic("player/" .. Players[pid].accountFile, profile, config.playerKeyOrder) then fatal(); return false end
        db.pending = nil
        if not persist(db) then return false end
        Players[pid]:LoadInventory()
        Players[pid]:LoadEquipment()
    end
    return true
end
local function key(cell, index) return cell .. "\n" .. index end
local function object(cell, index)
    local c = LoadedCells[cell]
    return c and c.data.objectData and c.data.objectData[index]
end
local function near(pid, cell, index)
    if tes3mp.GetCell(pid) ~= cell then return false end
    local o = object(cell, index)
    local p = o and o.location
    if not p or not p.posX or not p.posY or not p.posZ then return false end
    return (tes3mp.GetPosX(pid)-p.posX)^2 + (tes3mp.GetPosY(pid)-p.posY)^2 + (tes3mp.GetPosZ(pid)-p.posZ)^2 <= 300^2
end
local function ownsRoom(pid, cell)
    local prop = houseHelper.GetProperty(houseHelper.GetBaseCellForInstance(cell) or cell)
    return prop and houseHelper.GetAccessLevel(pid, prop) == "owner"
        and (prop.mode == "original" or houseHelper.IsInstanceCell(cell))
        and not houseHelper.IsPurgePending(cell)
end
local function validStand(stand)
    local o = object(stand.cell, stand.index)
    local prop = houseHelper.GetProperty(houseHelper.GetBaseCellForInstance(stand.cell) or stand.cell)
    return o and o.arenaFurnitureOwner == stand.owner and o.refId == o.arenaFurnitureRefId and prop and tostring(prop.owner):lower() == stand.owner
        and (prop.mode == "original" or houseHelper.IsInstanceCell(stand.cell))
end
local function standFor(pid)
    local k = db.owners[Players[pid].accountFile]
    return k and db.stands[k], k
end
local function clean(s) return tostring(s):gsub('[;\r\n\t]', ' '):gsub('#', ''):sub(1,100) end
local function countGold(inv)
    local total = 0
    for _, i in pairs(inv) do if i.refId:lower() == GOLD then total=total+i.count end end
    return total
end
local function debit(inv, amount)
    for n=#inv,1,-1 do
        local i=inv[n]
        if i.refId:lower()==GOLD then
            local take=math.min(i.count,amount); i.count=i.count-take; amount=amount-take
            if i.count==0 then table.remove(inv,n) end
            if amount==0 then return true end
        end
    end
    return amount==0
end
local function add(inv, item)
    -- Preserve charges, souls and poison exactly; merging is optional.
    inv[#inv+1]=copy(item)
end
local function permitted(p, i)
    if not i or not i.refId or i.refId:lower()==GOLD or (tonumber(i.count) or 0)<1 then return false end
    -- Dynamic records need a separate cross-account lifetime protocol. Refuse
    -- them rather than produce broken or duplicate custom enchantments/potions.
    if logicHandler.IsGeneratedRecord(i.refId) then return false end
    for _, e in pairs(p.data.equipment or {}) do
        if e.refId and e.refId:lower()==i.refId:lower() then return false end
    end
    for _, banned in pairs(config.vitrineBannedItems or {}) do
        if i.refId:lower()==banned:lower() then return false end
    end
    return true
end
local function menu(pid, state, caption, labels)
    state.account=Players[pid].accountName
    state.expires=os.time()+90
    menus[pid]=state
    labels[#labels+1]=tr(pid,"Закрыть","Close")
    tes3mp.CustomMessageBox(pid, GUI, caption, table.concat(labels,";"))
end
local function list(pid, mode, standKey, page)
    if not alive(pid) then return end
    page=math.max(1,page or 1)
    local p=Players[pid]
    local entries={}
    if mode=="inventory" then
        for _, i in pairs(p.data.inventory) do if permitted(p,i) then entries[#entries+1]=copy(i) end end
    else
        for id,l in pairs(db.listings) do
            if (mode=="stock" and l.seller==p.accountFile) or (mode=="browse" and l.stand==standKey) then
                entries[#entries+1]={id=id, item=l.item, price=l.price}
            end
        end
        table.sort(entries,function(a,b) return tonumber(a.id)<tonumber(b.id) end)
    end
    local pages=math.max(1,math.ceil(#entries/10));page=math.min(page,pages)
    local state={mode=mode,stand=standKey,page=page,choices={}}
    local labels={}
    for n=(page-1)*10+1,math.min(page*10,#entries) do
        local e=entries[n];state.choices[#state.choices+1]=e
        local i=e.item or e
        labels[#labels+1]=clean(i.refId).." x"..i.count..(e.price and " — "..e.price..tr(pid," зол."," gold") or "")
    end
    if page>1 then state.choices[#state.choices+1]="prev";labels[#labels+1]=tr(pid,"Назад","Previous") end
    if page<pages then state.choices[#state.choices+1]="next";labels[#labels+1]=tr(pid,"Далее","Next") end
    menu(pid,state,tr(pid,"Витрина","Player display").." ["..page.."/"..pages.."]",labels)
end
local function matches(a,b)
    for _,k in ipairs({"refId","charge","enchantmentCharge","soul","poisonId","poisonCharges"}) do
        if a[k]~=b[k] then return false end
    end
    return true
end
function M.ListItem(pid, snapshot, price, count)
    if not alive(pid) then return false end
    if not price or price~=math.floor(price) or price<1 or price>100000000 then return false end
    if not count or count~=math.floor(count) or count<1 or count>10000 then return false end
    local p=Players[pid]
    local stand,k=standFor(pid)
    if not stand or not validStand(stand) or not near(pid,stand.cell,stand.index) then return false end
    local total=0
    for _,l in pairs(db.listings) do if l.seller==p.accountFile then total=total+1 end end
    if total>=30 then return false end
    local profile=copy(p.data)
    for n,i in ipairs(profile.inventory) do
        if matches(i,snapshot) and i.count>=count and permitted(p,i) then
            local item=copy(i);item.count=count
            i.count=i.count-count
            if i.count==0 then table.remove(profile.inventory,n) end
            local nextDb=copy(db);local id=tostring(nextDb.nextId);nextDb.nextId=nextDb.nextId+1
            nextDb.listings[id]={stand=k,seller=p.accountFile,item=item,price=price}
            return commit(pid,nextDb,profile)
        end
    end
    return false
end
function M.Buy(pid,id,standKey)
    if not alive(pid) then return false end
    local l=db.listings[id];local stand=l and db.stands[l.stand]
    local p=Players[pid]
    if not l or l.stand~=standKey or l.seller==p.accountFile or not stand or not validStand(stand)
        or not near(pid,stand.cell,stand.index) then return false end
    local profile=copy(p.data)
    if countGold(profile.inventory)<l.price then return false end
    if not debit(profile.inventory,l.price) then return false end
    add(profile.inventory,l.item)
    local nextDb=copy(db)
    nextDb.listings[id]=nil
    nextDb.balances[l.seller]=(nextDb.balances[l.seller] or 0)+l.price
    if nextDb.balances[l.seller]>2000000000 then return false end
    return commit(pid,nextDb,profile)
end
function M.Cancel(pid,id)
    if not alive(pid) then return false end
    local l=db.listings[id];local p=Players[pid]
    if not l or l.seller~=p.accountFile then return false end
    local profile=copy(p.data);add(profile.inventory,l.item)
    local nextDb=copy(db);nextDb.listings[id]=nil
    return commit(pid,nextDb,profile)
end
function M.Collect(pid)
    if not alive(pid) then return false end
    local p=Players[pid];local amount=db.balances[p.accountFile] or 0
    if amount<=0 or countGold(p.data.inventory)+amount>2000000000 then return false end
    local profile=copy(p.data);add(profile.inventory,{refId=GOLD,count=amount,charge=-1,enchantmentCharge=-1,soul=""})
    local nextDb=copy(db);nextDb.balances[p.accountFile]=nil
    return commit(pid,nextDb,profile)
end
function M.IsStand(cell,index) return false end
function M.HasAssets(p)
    if not db then return true end
    if db.owners[p.accountFile] or (db.balances[p.accountFile] or 0)>0 then return true end
    for _,l in pairs(db.listings) do if l.seller==p.accountFile then return true end end
    return false
end
function M.HandleObject(pid,cell,packet,objects)
    return false
end
-- No chat commands. The native property pane owns entry to the wallet.
function M.HouseItems() return db and (db.houseItems or {}) end
-- Property reset is a server action, independent of the seller's health/session.
function M.ClearHouseCell(cell)
    if not db then return false end
    local nextDb=copy(db)
    for k,item in pairs(nextDb.houseItems or {}) do
        if item.cell==cell then nextDb.houseItems[k]=nil end
    end
    return commit(nil,nextDb)
end
function M.CommitHouse(pid, items, profile, seller, price)
    if not alive(pid) then return false end
    local nextDb=copy(db);nextDb.houseItems=items
    if seller and price then
        nextDb.balances[seller]=(nextDb.balances[seller] or 0)+price
        if nextDb.balances[seller]>2000000000 then return false end
    end
    return commit(pid,nextDb,profile)
end
function M.RecoverLegacy(pid)
    if not alive(pid) then return end
    for id,l in pairs(copy(db.listings)) do
        if l.seller==Players[pid].accountFile then M.Cancel(pid,id) end
    end
    local _,k=standFor(pid)
    if k then
        local nextDb=copy(db);nextDb.stands[k]=nil;nextDb.owners[Players[pid].accountFile]=nil
        commit(pid,nextDb)
    end
end
function M.Balance(pid) return db and (db.balances[Players[pid].accountFile] or 0) or 0 end
customEventHooks.registerHandler("OnGUIAction",function(status,pid,id,data)
    if id~=GUI and id~=PRICE then return end
    local s=menus[pid];menus[pid]=nil
    if not status.validDefaultHandler or not alive(pid) or not s or s.account~=Players[pid].accountName or s.expires<os.time() then return end
    if id==PRICE and s.mode=="price" then
        local price,count=tostring(data):match("^%s*(%d+)%s+(%d+)%s*$")
        if not M.ListItem(pid,s.item,tonumber(price),tonumber(count)) then
            say(pid,"Не выставлено. Укажите: цена_за_весь_лот количество. Стойте рядом со своей витриной; снимите предмет с экипировки.",
                "Not listed. Enter: total_lot_price quantity. Stand beside your display and unequip the item.")
        end
        return
    end
    if id~=GUI then return end
    local selected=tonumber(data)
    if not selected or selected~=math.floor(selected) then return end
    local e=s.choices and s.choices[selected+1]
    if e=="prev" or e=="next" then list(pid,s.mode,s.stand,s.page+(e=="next" and 1 or -1));return end
    if not e then return end
    if s.mode=="inventory" then
        menus[pid]={mode="price",item=e,account=s.account,expires=os.time()+90}
        tes3mp.InputDialog(pid,PRICE,tr(pid,"Цена за весь лот и количество (например: 500 2)","Total lot price and quantity (example: 500 2)"),"")
    elseif s.mode=="browse" then
        menu(pid,{mode="buy",stand=s.stand,choices={e}},clean(e.item.refId).." x"..e.item.count.." — "..e.price,
            {tr(pid,"Купить","Buy")})
    elseif s.mode=="buy" then
        if not M.Buy(pid,e.id,s.stand) then say(pid,"Покупка недоступна: проверьте золото, расстояние и наличие товара.","Purchase unavailable: check gold, distance and stock.") end
    elseif s.mode=="stock" then
        if M.Cancel(pid,e.id) then say(pid,"Товар возвращён.","Item returned.") end
    end
end)
customEventHooks.registerValidator("OnPlayerDisconnect",function(_,pid) menus[pid]=nil;armed[pid]=nil end)
customEventHooks.registerHandler("OnServerPostInit",function()
    if config.databaseType and config.databaseType ~= "json" then
        tes3mp.LogMessage(enumerations.log.ERROR, "[PlayerVitrines] JSON profiles are required for the transaction journal")
        fatal();return
    end
    local path=config.dataPath.."/"..FILE
    local exists=tes3mp.GetFileStatusUtf8(path)
    if exists==0 then
        local backup=tes3mp.GetFileStatusUtf8(path..".bak")
        if backup==1 then
            if not tes3mp.ReplaceFileAtomic(path..".bak",path) then fatal();return end
            exists=1
        elseif backup~=0 then fatal();return end
    elseif exists~=1 then fatal();return end
    if exists==0 then
        db={version=1,nextId=1,stands={},owners={},listings={},balances={}}
        if not persist(db) then return end
    else
        db=jsonInterface.load(FILE)
        if type(db)~="table" then fatal();return end
    end
    if db.version~=1 or type(db.listings)~="table" or type(db.stands)~="table" or type(db.owners)~="table" or type(db.balances)~="table" then fatal();return end
    if db.pending then
        if not jsonInterface.saveAtomic("player/"..db.pending.file,db.pending.data,config.playerKeyOrder) then fatal();return end
        db.pending=nil;persist(db)
    end
end)
return M
