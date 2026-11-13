-- Physical items in player property: price, personal lock, server-owned pickup.
-- Design inspired by kanaHousing/kanaFurniture; independent ArenaMP implementation.
local M={}
local catalog=require('arenaFurnitureAutoCatalog')
local PREFIX='@@AMP_HOUSE@@'
local PRICE,BUY,WALLET=923470,923471,923472
local sessions,recordCache={},{}
local recordsLoaded=false
local pendingGains={}
local portable={WEAP=true,ARMO=true,CLOT=true,ALCH=true,INGR=true,BOOK=true,APPA=true,LOCK=true,PROB=true,REPA=true,MISC=true,LIGH=true}
local storeTypes={weapon=true,armor=true,clothing=true,potion=true,ingredient=true,book=true,apparatus=true,lockpick=true,probe=true,repair=true,miscellaneous=true,light=true}
local function key(cell,index) return cell..'\n'..index end
local function tr(pid,ru,en) return Players[pid].language=='RU' and ru or en end
local function notice(pid,ru,en) tes3mp.MessageBox(pid,-1,tr(pid,ru,en)) end
local function copy(v) return tableHelper.deepCopy(v) end
local function valid(pid)
    local p=Players[pid]
    return p and p:IsLoggedIn() and (p.data.stats.healthCurrent or 0)>0 and not p.deathRecoveryActive
        and not (bountyRespawn and bountyRespawn.Pending(p)) and playerVitrines.HouseItems()~=nil
end
function M.Property(cell)
    local base=houseHelper.GetBaseCellForInstance(cell)
    local prop=houseHelper.GetProperty(base or cell)
    if not prop or not prop.owner or prop.owner=='' then return nil end
    if prop.mode=='original' then return not base and prop or nil end
    return base and houseHelper.GetInstanceDescription(base,prop.owner)==cell and prop or nil
end
local function editor(pid,prop)
    local access=houseHelper.GetAccessLevel(pid,prop)
    return access=='owner' or access=='coowner'
end
local function record(id)
    local store=logicHandler.GetRecordStoreByRecordId(id)
    if store and storeTypes[store.storeType] then
        return store.data.generatedRecords[id] or store.data.permanentRecords[id],store
    end
    local k=id:lower()
    if not recordsLoaded then
        local all=catalog.findActiveRecords(config,jsonInterface,setmetatable({}, {__index=function() return true end}))
        if not all then return nil end
        recordCache=all;recordsLoaded=true
    end
    local r=recordCache[k]
    if not r or not portable[r.kind] then return nil end
    if r.kind=='LIGH' and math.floor((r.flags or 0)/2)%2~=1 then return nil end
    return r
end
-- HS1: is this a real inventory item (for houseStorage, when a home is sold)?
-- true = item, false = not an item, nil = unknown (active plugins unreadable).
function M.IsPortable(id)
    if type(id)~='string' or id=='' then return false end
    local ok,rec=pcall(record,id)
    if not ok then return nil end
    if rec then return true end
    if not recordsLoaded and not logicHandler.GetRecordStoreByRecordId(id) then return nil end
    return false
end
local function isBookRecord(rec,store)
    return (store and store.storeType=='book') or (rec and rec.kind=='BOOK')
end
local function canTakeBook(pid,prop,cell)
    local access=houseHelper.GetAccessLevel(pid,prop)
    -- Y069: visitors may read, but only owner/co-owner/friend (or somebody who
    -- already has the ordinary take permission) may remove a book from the home.
    return access=='owner' or access=='coowner' or access=='friend' or houseHelper.CanTake(pid,cell)
end

local function deleted(room,index)
    for _,i in pairs(room.data.packets.delete or {}) do if i==index then return true end end
    return false
end
local function resolve(pid,cell,index,refId)
    if not valid(pid) or tes3mp.GetCell(pid)~=cell or not index:match('^%d+%-%d+$') then return nil end
    local prop=M.Property(cell);local room=LoadedCells[cell]
    if not prop or not room or houseHelper.IsPurgePending(cell) or deleted(room,index) then return nil end
    local old=playerVitrines.HouseItems()[key(cell,index)]
    if old and old.consumed then return nil end
    local obj=room.data.objectData[index]
    if index:match('^[1-9]%d*%-0$') then
        local base=catalog.findInteriorReference(config,jsonInterface,houseHelper.GetBaseCellForInstance(cell) or cell,index,refId)
        if not base then return nil end
        for k,v in pairs(obj or {}) do base[k]=v end
        obj=base
    end
    if not obj or type(obj.refId)~='string' or obj.refId:lower()~=refId:lower() or not obj.location then return nil end
    local rec,store=record(obj.refId)
    if not rec then return nil end
    local loc=obj.location
    if not loc.posX or not loc.posY or not loc.posZ then return nil end
    if (tes3mp.GetPosX(pid)-loc.posX)^2+(tes3mp.GetPosY(pid)-loc.posY)^2+(tes3mp.GetPosZ(pid)-loc.posZ)^2>300^2 then return nil end
    local count=tonumber(obj.count) or 1
    if count<1 or count~=math.floor(count) or count>1000000 then return nil end
    -- Global lock identity is (cell, uniqueIndex), not just a cross-cell ESM number.
    local meta=copy(old or {cell=cell,index=index,refId=obj.refId,version=0})
    if meta.owner~=prop.owner:lower() then
        -- An old owner's sale must not credit that owner after gifting the house.
        meta.price=nil;meta.seller=nil;meta.owner=prop.owner:lower()
    end
    return {prop=prop,room=room,obj=obj,meta=meta,record=rec,store=store,count=count,key=key(cell,index)}
end
local function sendState(pid,r)
    tes3mp.SendMessage(pid,PREFIX..'HITEM_STATE\t'..r.meta.cell..'\t'..r.meta.index..'\t'..
        tostring(r.meta.price or 0)..'\t'..(r.meta.locked and '1' or '0')..'\n',false)
end
local function persist(pid,r)
    local items=copy(playerVitrines.HouseItems());r.meta.version=(r.meta.version or 0)+1
    items[r.key]=r.meta
    if not playerVitrines.CommitHouse(pid,items) then return false end
    for other,p in pairs(Players) do
        if p:IsLoggedIn() and tes3mp.GetCell(other)==r.meta.cell then sendState(other,r) end
    end
    return true
end
local function gold(inv)
    local total=0;for _,i in pairs(inv) do if i.refId:lower()=='gold_001' then total=total+i.count end end;return total
end
local function removeGold(inv,n)
    for i=#inv,1,-1 do local item=inv[i]
        if item.refId:lower()=='gold_001' then
            local amount=math.min(n,item.count);item.count=item.count-amount;n=n-amount
            if item.count==0 then table.remove(inv,i) end
            if n==0 then return end
        end
    end
end
local function consumeWorld(pid,meta)
    local room=LoadedCells[meta.cell]
    if not room then return end
    if deleted(room,meta.index) and not room.data.objectData[meta.index] then return end
    logicHandler.DeleteObject(pid,meta.cell,meta.index,true)
    room:Save()
end
function M.Take(pid,r,buy)
    if r.meta.locked then notice(pid,'Предмет заблокирован. Сначала снимите замок.','This item is locked. Unlock it first.');return false end
    local price=r.meta.price
    if buy then
        if not price or r.meta.seller==Players[pid].accountFile then return false end
    elseif price or (not houseHelper.CanTake(pid,r.meta.cell) and not (isBookRecord(r.record,r.store) and canTakeBook(pid,r.prop,r.meta.cell))) then return false end
    local p=Players[pid]
    if price and gold(p.data.inventory)<price then notice(pid,'Недостаточно золота.','Not enough gold.');return false end
    if r.store and logicHandler.IsGeneratedRecord(r.obj.refId) then
        -- Pin the dynamic record before deleting its last world reference.
        p:AddLinkToRecord(r.store.storeType,r.obj.refId)
        r.store:LoadGeneratedRecords(pid,r.store.data.generatedRecords,{r.obj.refId},false)
    end
    local profile=copy(p.data)
    if price then removeGold(profile.inventory,price) end
    local itemId,itemCount=r.obj.refId,r.count
    if itemId:lower():match('^gold_%d+$') then
        local value=(r.count==1 and (tonumber(r.obj.goldValue) or 0)>1) and r.obj.goldValue or (tonumber(r.record.value) or 1)
        itemId='gold_001';itemCount=r.count*value
        if itemCount<1 or itemCount~=math.floor(itemCount) or gold(profile.inventory)+itemCount>2000000000 then return false end
    end
    profile.inventory[#profile.inventory+1]={refId=itemId,count=itemCount,
        charge=r.obj.charge or -1,enchantmentCharge=r.obj.enchantmentCharge or -1,soul=r.obj.soul or '',
        poisonId=r.obj.poisonId or '',poisonCharges=r.obj.poisonCharges or 0}
    local items=copy(playerVitrines.HouseItems());r.meta.consumed=true;r.meta.version=(r.meta.version or 0)+1
    items[r.key]=r.meta
    -- The durable tombstone and inventory receipt precede world deletion.
    if not playerVitrines.CommitHouse(pid,items,profile,price and r.meta.seller,price) then return false end
    consumeWorld(pid,r.meta)
    for other,online in pairs(Players) do if online:IsLoggedIn() then M.PushWallet(other) end end
    return true
end
local function request(pid,verb,index,refId)
    if verb=='WALLET' then
        if not valid(pid) then return end
        sessions[pid]={mode='wallet',account=Players[pid].accountName}
        tes3mp.CustomMessageBox(pid,WALLET,tr(pid,'Выручка: ','Proceeds: ')..playerVitrines.Balance(pid),
            tr(pid,'Получить выручку;Вернуть товары старых витрин;Закрыть','Collect proceeds;Recover legacy listings;Close'))
        return
    end
    local cell=tes3mp.GetCell(pid)
    local r=resolve(pid,cell,index or '',refId or '')
    if not r then notice(pid,'Предмет недоступен или ещё не синхронизирован.','Item unavailable or still synchronizing.');return end
    if verb=='STATE' then sendState(pid,r);return end
    if verb=='ACTIVATE' then
        if r.meta.locked then notice(pid,'Предмет заблокирован.','This item is locked.');return end
        if r.meta.price then
            if r.meta.seller==Players[pid].accountFile then
                notice(pid,'Чтобы забрать свой товар, сначала уберите цену.','Remove the price before taking your own item.');return
            end
            sessions[pid]={mode='buy',cell=cell,index=index,refId=refId,price=r.meta.price,version=r.meta.version,
                expires=os.time()+60,account=Players[pid].accountName}
            tes3mp.CustomMessageBox(pid,BUY,tr(pid,'Купить: ','Buy: ')..refId..' x'..r.count..' — '..r.meta.price..
                tr(pid,' золота за весь лот?',' gold for the entire lot?'),tr(pid,'Купить;Отмена','Buy;Cancel'))
        elseif houseHelper.CanTake(pid,cell) or (isBookRecord(r.record,r.store) and canTakeBook(pid,r.prop,cell)) then M.Take(pid,r,false)
        else notice(pid,'Владелец запретил брать предметы.','The owner does not allow taking items.') end
        return
    end
    if not editor(pid,r.prop) then notice(pid,'Нет прав на управление предметом.','You cannot manage this item.');return end
    if verb=='LOCK' then r.meta.locked=true;persist(pid,r)
    elseif verb=='UNLOCK' then r.meta.locked=false;persist(pid,r)
    elseif verb=='PRICE' then
        if houseHelper.GetAccessLevel(pid,r.prop)~='owner' then return end
        sessions[pid]={mode='price',cell=cell,index=index,refId=refId,expires=os.time()+90,account=Players[pid].accountName}
        tes3mp.InputDialog(pid,PRICE,tr(pid,'Цена за весь предмет/стопку (золото)','Price for the whole item/stack (gold)'),tostring(r.meta.price or ''))
    elseif verb=='CLEAR' then
        if houseHelper.GetAccessLevel(pid,r.prop)~='owner' then return end
        r.meta.price=nil;r.meta.seller=nil;persist(pid,r)
    elseif verb=='TAKE' then
        if not M.Take(pid,r,false) then notice(pid,'Снимите замок и уберите цену перед взятием.','Unlock the item and remove its price before taking it.') end
    elseif verb=='MOVE' then
        if r.meta.locked or r.meta.price then notice(pid,'Для перемещения снимите замок и уберите цену.','Unlock the item and remove its price before moving it.');return end
        tes3mp.SendMessage(pid,PREFIX..'HITEM_MOVE\t'..cell..'\t'..index..'\t'..refId..'\n',false)
    end
end
function M.HandleControl(pid,message)
    local verb,index,refId=message:match('^@@AMP_HOUSE@@HITEM\t([A-Z]+)\t([^\t]+)\t([^\t\r\n]+)')
    if not verb then return false end
    if valid(pid) then request(pid,verb,index,refId) end
    return true
end
-- This gate runs before default and custom handlers, preserving lock precedence.
function M.HandleObject(pid,cell,packet,objects)
    local prop=M.Property(cell)
    if not prop then return false end
    if packet=='ObjectDelete' and houseHelper.IsPurgePending(cell) then
        for index,obj in pairs(objects) do
            if not houseHelper.IsPurgeDelete(pid,cell,index,obj) then return true end
        end
        return false
    end
    for index,obj in pairs(objects) do
        local meta=playerVitrines.HouseItems() and playerVitrines.HouseItems()[key(cell,index)]
        if packet=='ObjectActivate' and obj.activatingPid==pid then
            local rec,store=record(obj.refId or '')
            if rec then
                local isBook=isBookRecord(rec,store)
                -- Books/notes always open the reader, even when a home/item is
                -- locked or priced. This grants no inventory receipt: taking,
                -- deleting and moving the world item remain protected. Purchases
                -- and owner pickups still use the explicit house-item actions.
                if isBook then return false end
                request(pid,'ACTIVATE',index,obj.refId);return true
            end
        end
        if meta and (meta.locked or (meta.price and meta.owner==prop.owner:lower()) or meta.consumed) then
            if packet=='ObjectMove' or packet=='ObjectRotate' or packet=='ObjectScale' or packet=='ObjectDelete'
                or packet=='ObjectState' or packet=='ObjectActivate' then
                if packet=='ObjectDelete' and Players[pid] then Players[pid]:LoadInventory() end
                return true
            end
        end
        -- Y069: opening a BOOK is read-only for ordinary visitors. The native
        -- book/scroll Take button is enabled only for owner/co-owner/friend (or
        -- when normal house pickup is allowed), and the server repeats the same
        -- permission check here so forged ObjectDelete packets cannot bypass it.
        if packet=='ObjectDelete' then
            local rec,store=record(obj.refId or '')
            if rec then
                if isBookRecord(rec,store) and not (meta and (meta.locked or meta.price or meta.consumed)) then
                    if not canTakeBook(pid,prop,cell) then
                        Players[pid]:LoadInventory();return true
                    end
                    local r=resolve(pid,cell,index,obj.refId or '')
                    local tomb
                    if r then
                        tomb=r.meta
                    elseif index:match('^[1-9]%d*%-0$') then
                        -- Static ESM BOOK references may reach us from BookWindow
                        -- after their live objectData was trimmed. Verify the
                        -- reference against the original interior catalog and
                        -- persist a narrow tombstone without requiring location.
                        local base=catalog.findInteriorReference(config,jsonInterface,
                            houseHelper.GetBaseCellForInstance(cell) or cell,index,obj.refId or '')
                        if not base or type(base.refId)~='string' or base.refId:lower()~=(obj.refId or ''):lower() then
                            Players[pid]:LoadInventory();return true
                        end
                        tomb=copy(meta or {cell=cell,index=index,refId=obj.refId,version=0})
                        tomb.cell=cell;tomb.index=index;tomb.refId=obj.refId;tomb.owner=prop.owner:lower()
                    else
                        Players[pid]:LoadInventory();return true
                    end
                    local items=copy(playerVitrines.HouseItems())
                    tomb.consumed=true;tomb.version=(tomb.version or 0)+1
                    items[key(cell,index)]=tomb
                    if not playerVitrines.CommitHouse(pid,items) then
                        Players[pid]:LoadInventory();return true
                    end
                    return false
                end
                -- Other portable home items still use the server transaction above.
                Players[pid]:LoadInventory();return true
            end
        end
    end
    return false
end
-- A rejected ObjectDelete alone is insufficient: client inventory ADD/SET
-- arrives separately. Refuse unreceipted gains of protected display items.
function M.AllowInventoryGain(pid,refId,count)
    if not valid(pid) or count<=0 then return end
    local p=Players[pid];local receipt=pendingGains[pid]
    if not receipt or receipt.account~=p.accountName or receipt.expires<os.time() then
        receipt={account=p.accountName,expires=os.time()+10,counts={}};pendingGains[pid]=receipt
    end
    local id=refId:lower();receipt.counts[id]=(receipt.counts[id] or 0)+count
end
function M.GuardInventory(pid,packet)
    if not valid(pid) or packet.action==enumerations.inventory.REMOVE then return false end
    local cell=tes3mp.GetCell(pid);local protected={}
    for _,meta in pairs(playerVitrines.HouseItems() or {}) do
        if meta.cell==cell and not meta.consumed and (meta.locked or meta.price) then protected[meta.refId:lower()]=true end
    end
    local current,incoming={},{}
    for _,i in pairs(Players[pid].data.inventory or {}) do current[i.refId:lower()]=(current[i.refId:lower()] or 0)+i.count end
    for _,i in pairs(packet.inventory or {}) do
        local id=i.refId:lower();incoming[id]=(incoming[id] or 0)+(tonumber(i.count) or 0)
    end
    local receipt=pendingGains[pid]
    if receipt and (receipt.expires<os.time() or receipt.account~=Players[pid].accountName) then receipt=nil;pendingGains[pid]=nil end
    local used={}
    for id,count in pairs(incoming) do
        local gain=packet.action==enumerations.inventory.ADD and count or math.max(0,count-(current[id] or 0))
        if protected[id] and gain>0 then
            if not receipt or (receipt.counts[id] or 0)<gain then
                Players[pid]:LoadInventory();return true
            end
            used[id]=gain
        end
    end
    if receipt then for id,count in pairs(used) do receipt.counts[id]=receipt.counts[id]-count end end
    return false
end
function M.PushWallet(pid)
    local available=playerVitrines.Balance(pid)>0 or playerVitrines.HasAssets(Players[pid])
    tes3mp.SendMessage(pid,PREFIX..'HITEM_WALLET\t'..(available and '1' or '0')..'\n',false)
end
customEventHooks.registerHandler('OnPlayerAuthentified',function(_,pid) if Players[pid] then M.PushWallet(pid) end end)
customEventHooks.registerHandler('OnGUIAction' ,function(status,pid,id,data)
    if id~=PRICE and id~=BUY and id~=WALLET then return end
    local s=sessions[pid];sessions[pid]=nil
    if not status.validDefaultHandler or not valid(pid) or not s or s.account~=Players[pid].accountName then return end
    if id==WALLET and s.mode=='wallet' then
        if tonumber(data)==0 then playerVitrines.Collect(pid)
        elseif tonumber(data)==1 then playerVitrines.RecoverLegacy(pid) end
        M.PushWallet(pid)
        return
    end
    if not s.expires or s.expires<os.time() then return end
    local r=resolve(pid,s.cell,s.index,s.refId);if not r then return end
    if id==PRICE and s.mode=='price' and houseHelper.GetAccessLevel(pid,r.prop)=='owner' then
        local price=tonumber(data)
        if price and price==math.floor(price) and price>=1 and price<=100000000 then
            r.meta.price=price;r.meta.seller=Players[pid].accountFile;persist(pid,r)
        else notice(pid,'Цена должна быть целым числом от 1 до 100000000.','Enter a whole price from 1 to 100000000.') end
    elseif id==BUY and s.mode=='buy' and tonumber(data)==0 and r.meta.price==s.price and r.meta.version==s.version then
        M.Take(pid,r,true)
    end
end)
-- Y105: old consumed dynamic refs are intentionally absent from objectData and
-- packets.delete. Re-running consumeWorld for each one wrote the whole cell on
-- every entry. Replay tombstones privately in batches; persist only real repairs.
function M.RestoreConsumed(pid,cell)
    local room=LoadedCells[cell]
    if not room or type(room.data)~='table' or type(room.data.objectData)~='table' then return 0,false end
    local packets=room.data.packets
    if type(packets)~='table' or type(packets.delete)~='table' then return 0,false end
    local knownDeleted,seen,private,broadcast={},{},{},{}
    for _,index in pairs(packets.delete) do knownDeleted[index]=true end
    local changed=false
    for _,meta in pairs(playerVitrines.HouseItems() or {}) do
        if type(meta)=='table' and meta.cell==cell and meta.consumed and
            type(meta.index)=='string' and meta.index:match('^%d+%-%d+$') and not seen[meta.index] then
            local index=meta.index
            seen[index]=true
            local refNum=tonumber(index:match('^(%d+)%-'))
            local needsBroadcast=false
            if room.data.objectData[index]~=nil then
                room:DeleteObjectData(index)
                knownDeleted[index]=nil
                changed=true
                needsBroadcast=true
            end
            if refNum>0 and not knownDeleted[index] then
                packets.delete[#packets.delete+1]=index
                knownDeleted[index]=true
                changed=true
                needsBroadcast=true
            end
            local list=needsBroadcast and broadcast or private
            list[#list+1]={index=index,refId=meta.refId}
        end
    end
    if changed then room:Save() end
    local function send(list,everyone)
        local count=0
        for _,entry in ipairs(list) do
            if count==0 then
                tes3mp.ClearObjectList()
                tes3mp.SetObjectListPid(pid)
                tes3mp.SetObjectListCell(cell)
            end
            packetBuilder.AddObjectDelete(entry.index,{refId=entry.refId})
            count=count+1
            if count==128 then tes3mp.SendObjectDelete(everyone,false);count=0 end
        end
        if count>0 then tes3mp.SendObjectDelete(everyone,false) end
    end
    send(private,false)
    send(broadcast,true)
    return #private+#broadcast,changed
end
customEventHooks.registerHandler('OnCellLoad',function(_,pid,cell) M.RestoreConsumed(pid,cell) end)
customEventHooks.registerValidator('OnPlayerDisconnect',function(_,pid) sessions[pid]=nil;pendingGains[pid]=nil end)
return M
