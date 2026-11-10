-- ArenaMP H003: furniture marketplace with server-derived ESM/ESP catalog.
-- The authoritative shop, pricing, ownership and inventory live on the server.
-- Native MyGUI/OSG layout uses FS_* server events; the legacy /furniture
-- message-box path remains for backward compatibility with H001 clients.
local jsonInterface = require("jsonInterface")
local houseHelper = require("houseHelper")
local inventoryHelper = require("inventoryHelper")
local marketplace = {}

local autoCatalog = require("arenaFurnitureAutoCatalog")
local GOLD = "gold_001"
local PAGE_SIZE = 10
local GUI_CATEGORY = 923410
local GUI_ITEMS = 923411
local GUI_BUY = 923412
local GUI_INVENTORY = 923413
local GUI_PLACE = 923414
local GUI_PLACED = 923415
local GUI_PLACED_ACTION = 923416
local GUI_PRIVATE_NAME = 923417
local SELLBACK_RATIO = 0.75 -- same rule as old kanaFurniture, applied server-side only
local selection = {}
local catalog, byId, categories = {items={}, categories={}}, {}, {}

-- Per-player language matches the ArenaHousing/player profile setting.
-- Keep stable category/refId/index values in storage and protocol; localize only display text.
local function tr(pid, ru, en)
    -- Refresh from the client's actual UI language; the server's forced chat
    -- language must not select the furniture catalogue language.
    local locale = package.loaded.localization
    if locale and type(locale.GetClientLanguage) == "function" then
        return locale.GetClientLanguage(pid) == "RU" and ru or en
    end
    local player=Players and Players[pid]
    local language=player and tostring(player.language or "EN"):lower() or "en"
    return (language=="ru" or language=="ru-ru" or language=="russian") and ru or en
end
local categoryEnglish={
    ["Столы"]="Tables", ["Сиденья"]="Seating", ["Кровати"]="Beds",
    ["Шкафы и полки"]="Cabinets and shelves", ["Ковры и текстиль"]="Rugs and textiles",
    ["Растения"]="Plants", ["Декор"]="Decor", ["Контейнеры"]="Containers",
    ["Освещение"]="Lighting", ["Камины"]="Fireplaces", ["Мебель"]="Furniture", ["Торговля"]="Trading"
}
local categoryKeys={
    ["Мебель"]="furniture",["Контейнеры"]="containers",["Декор"]="decor",
    ["Освещение"]="lighting",["Камины"]="fireplaces",["Растения"]="plants",
    ["Ковры и текстиль"]="textiles",["Торговля"]="trading",["Столы"]="tables",
    ["Сиденья"]="seating",["Кровати"]="beds",["Шкафы и полки"]="storage"
}
local function categoryName(pid, value)
    return tr(pid, value, categoryEnglish[value] or value)
end

local function say(pid, message)
    if Players[pid] then tes3mp.MessageBox(pid, -1, tostring(message)) end
end

local function cleanLine(text)
    return tostring(text or ""):gsub("[\\\r\n\t;]", " ")
end

-- H015: the supplied reference is a curated allow-list. Most stock is
-- intersected with active server ESM/ESP records; curated beds may remain
-- available when their plugin is client-side only, with local model checks.
local referencePath="custom/arenaFurnitureReference.json"
local manualPath="custom/arenaFurnitureManual.json"
local snapshotPath="custom/arenaFurnitureGenerated.json"
local manualEntries={}
local validCategories={}
local defaultCategoryEnglish={
    ["Мебель"]="Furniture",["Кровати"]="Beds",["Контейнеры"]="Containers",["Декор"]="Decor",
    ["Освещение"]="Lighting",["Камины"]="Fireplaces",["Растения"]="Plants",
    ["Ковры и текстиль"]="Rugs and textiles",["Торговля"]="Trading"
}
local function staff(pid)
    local player=Players and Players[pid]
    return player and player:IsLoggedIn() and tonumber(player.data.settings and player.data.settings.staffRank or 0)>=2
end
local function canDecorate(pid)
    if staff(pid) then return true end
    if not Players[pid] or not Players[pid]:IsLoggedIn() then return false end
    local cell=tes3mp.GetCell(pid)
    local base=houseHelper.GetBaseCellForInstance(cell) or cell
    local property=houseHelper.GetProperty(base)
    if not property then return false end
    local access=houseHelper.GetAccessLevel(pid,property)
    return (access=="owner" or access=="coowner") and
        (houseHelper.GetInstanceDescription(base,property.owner)==cell)
end
local function saveManual()
    local ok,result=pcall(jsonInterface.saveAtomic or jsonInterface.save,manualPath,manualEntries)
    return ok and result~=false
end
local function textValid(value)
    return type(value)=="string" and #value>=1 and #value<=190 and
        not value:find("[\\\r\n\t;|%c]")
end
local function isReferenceCandidate(record, category)
    -- Preserve the old anti-duplication protections for base containers, and
    -- do not sell a door or an arbitrary scripted quest object just because it is in JSON.
    -- Curated scripted beds are intentionally permitted by referenceBedCandidate().
    if not record or not record.refId then return false end
    local id=record.refId:lower()
    if id:find("door",1,true) or id:find("portal",1,true) then return false end
    if autoCatalog.manualCandidate(record)~=nil then return true end
    -- Some curated beds are scripted ACTI/MISC records rather than plain STAT.
    -- Allow these exact model-backed beds without removing their sleep scripts.
    return category=="Кровати" and autoCatalog.referenceBedCandidate(record)~=nil
end

-- A private price=0 preset is created only after the server has verified the
-- concrete ESM reference in the player's own home. Random-content containers are
-- intentionally absent from the public shop, but after the actual instance is
-- emptied they must remain packable and placeable. Revalidate the active record
-- while ignoring only the base-content marker; scripts, doors, organic/respawn
-- flags, invalid models and every other manualCandidate deny rule stay intact.
local function isPrivateReferenceCandidate(record, category)
    if isReferenceCandidate(record,category) then return true end
    if type(record)~="table" or record.kind~="CONT" or not record.hasContents then return false end
    local copy={}
    for key,value in pairs(record) do copy[key]=value end
    copy.hasContents=false
    return isReferenceCandidate(copy,category)
end
local function styleFor(name)
    local n=tostring(name or ""):lower()
    if n:find("Данмер",1,true) or n:find("данмер",1,true) or
        n:find("Телван",1,true) or n:find("телван",1,true) or
        n:find("Индорил",1,true) or n:find("индорил",1,true) then return "dunmer" end
    if n:find("Импер",1,true) or n:find("импер",1,true) then return "imperial" end
    if n:find("Норд",1,true) or n:find("норд",1,true) then return "nordic" end
    return "other"
end
-- A single localized ESM name is not a bilingual translation. Use the curated
-- pair where available, otherwise retain the authored name and an honest refId
-- fallback for a missing English label. Never write these fallbacks over edits.
local function catalogNames(item, reference)
    local ru = item.nameRu or item.name or (reference and reference.nameRu) or item.refId
    local en = item.nameEn or (reference and reference.nameEn) or item.name or item.refId
    if not textValid(ru) then ru = (reference and reference.nameRu) or item.refId end
    if not textValid(en) or en:find("[\208\209][\128-\191]") then
        local fallback = reference and reference.nameEn
        en = textValid(fallback) and not fallback:find("[\208\209][\128-\191]") and fallback or item.refId
    end
    return ru, en
end
local function localName(pid,item)
    return tr(pid,item.nameRu or item.name or item.refId,item.nameEn or item.name or item.refId)
end
local function loadCatalog()
    local ok,reference=pcall(jsonInterface.load,referencePath)
    if not ok or type(reference)~="table" or type(reference.items)~="table" or #reference.items==0 then
        -- Older/partial server updates sometimes preserved data/ while missing the
        -- newly shipped reference file. A previously generated non-empty snapshot
        -- is a safe recovery source until the packaged JSON is copied in place.
        local snapshotOk,snapshot=pcall(jsonInterface.load,snapshotPath)
        if snapshotOk and type(snapshot)=="table" and type(snapshot.items)=="table" and #snapshot.items>0 then
            reference={version=snapshot.version or 4,categories={},items=snapshot.items,categoryLabels=snapshot.categoryLabels}
            for _,entry in ipairs(snapshot.items) do
                if type(entry)=="table" and type(entry.category)=="string" then
                    reference.categories[#reference.categories+1]=entry.category
                end
            end
            tes3mp.LogMessage(enumerations.log.WARN or enumerations.log.ERROR,
                "[ArenaFurniture] reference file missing; recovered catalogue from "..snapshotPath)
        else
            tes3mp.LogMessage(enumerations.log.ERROR,
                "[ArenaFurniture] reference catalogue missing or invalid: "..referencePath)
            return false
        end
    end
    for id, labels in pairs(reference.categoryLabels or {}) do
        if type(id)=="string" and type(labels)=="table" then
            if textValid(labels.nameEn) then categoryEnglish[id]=labels.nameEn end
            if type(labels.key)=="string" and labels.key:match("^[a-z]+$") then categoryKeys[id]=labels.key end
        end
    end
    validCategories={}
    for _,category in ipairs(reference.categories or {}) do
        if type(category)=="string" and category~="Двери" then validCategories[category]=true end
    end
    -- A recovered snapshot may contain categories only in the item rows.
    for _,entry in ipairs(reference.items) do
        if type(entry)=="table" and type(entry.category)=="string" and entry.category~="Двери" then
            validCategories[entry.category]=true
        end
    end
    for name in pairs(defaultCategoryEnglish) do validCategories[name]=true end

    ok,manualEntries=pcall(jsonInterface.load,manualPath)
    manualEntries=(ok and type(manualEntries)=="table" and manualEntries) or {}
    local requested={}
    for _,entry in ipairs(reference.items) do
        if type(entry)=="table" and type(entry.refId)=="string" then
            requested[entry.refId:lower()]=true
        end
    end
    for key in pairs(manualEntries) do requested[tostring(key):lower()]=true end

    -- Prefer an active-ESM intersection for ordinary furniture. Some curated bed
    -- plugins are intentionally client-side and are not part of the server's
    -- requiredDataFiles manifest; those exact allow-listed beds are retained
    -- below. The client rejects records absent from its local load order.
    local parsed,reason,raw=autoCatalog.build(config,jsonInterface,requested)
    local verified=type(parsed)=="table" and type(raw)=="table"
    if not verified then
        raw={}
        tes3mp.LogMessage(enumerations.log.WARN or enumerations.log.ERROR,
            "[ArenaFurniture] active ESM scan unavailable; using curated reference catalogue: "..tostring(reason))
    end

    local items,present={},{}
    local curatedBedFallbackCount=0
    local function append(original)
        if type(original)~="table" or type(original.refId)~="string" then return end
        local key=original.refId:lower()
        if present[key] or #key<1 or #key>120 or key:find("[|\r\n\t]") then return end
        if key:find("door",1,true) or key:find("portal",1,true) then return end
        local record=raw[key]
        local override=manualEntries[key]
        local privatePreset=type(override)=="table" and tonumber(override.price)==0
        -- The reference file is a curated allow-list. A nil raw record means its
        -- plugin is not part of the server's requiredDataFiles manifest; false
        -- means an active plugin explicitly deleted the record. Keep curated
        -- beds in the first case so clients with the model loaded can use them.
        -- The client checks its own active ESM/ESP before enabling purchase/place.
        local curatedBedFallback=not privatePreset and original.category=="Кровати" and record==nil
        if verified and not ((privatePreset and isPrivateReferenceCandidate(record,original.category))
            or (not privatePreset and (isReferenceCandidate(record,original.category) or curatedBedFallback))) then return end
        if verified and curatedBedFallback then curatedBedFallbackCount=curatedBedFallbackCount+1 end
        local category=(type(override)=="table" and override.category) or original.category
        local ru,en=catalogNames(type(override)=="table" and override or original, original)
        local price=(type(override)=="table" and override.price) or original.price
        if not validCategories[category] or not textValid(ru) or not textValid(en) or
            type(price)~="number" or price<0 or price>10000000 or price~=math.floor(price) then return end
        present[key]=true
        items[#items+1]={refId=(record and record.refId) or original.refId,name=ru,nameRu=ru,nameEn=en,
            category=category,style=original.style or styleFor(ru),price=price}
    end
    for _,entry in ipairs(reference.items) do append(entry) end
    for _,entry in pairs(manualEntries) do append(entry) end
    table.sort(items,function(a,b) return a.refId:lower()<b.refId:lower() end)

    local grouped,fresh,names={}, {}, {}
    for _,item in ipairs(items) do
        fresh[item.refId:lower()]=item
        if not grouped[item.category] then grouped[item.category]={}; names[#names+1]=item.category end
        grouped[item.category][#grouped[item.category]+1]=item
    end
    table.sort(names)
    for _,list in pairs(grouped) do table.sort(list,function(a,b)return a.nameRu<b.nameRu end) end

    local source=verified and (curatedBedFallbackCount>0 and "reference-intersection+curated-beds" or "reference-intersection")
        or "curated-reference-fallback"
    local saved,err=pcall(jsonInterface.saveAtomic or jsonInterface.save,snapshotPath,{version=6,source=source,categoryLabels=reference.categoryLabels,items=items})
    if not saved or err==false then
        tes3mp.LogMessage(enumerations.log.ERROR,"[ArenaFurniture] could not write catalogue snapshot: "..tostring(err))
    end
    catalog,byId,categories=grouped,fresh,names
    tes3mp.LogMessage(enumerations.log.INFO or enumerations.log.ERROR,
        "[ArenaFurniture] reference "..tostring(#reference.items)..", available "..tostring(#items)..
        (verified and " (ESM verified; curated beds without server records: "..tostring(curatedBedFallbackCount)..")"
            or " (fallback; client filters local records)"))
    return #items>0
end

local function furnitureState(pid)
    local player = Players[pid]
    if not player or not player:IsLoggedIn() then return nil end
    player.data.customVariables = player.data.customVariables or {}
    local state = player.data.customVariables.arenaFurniture
    if type(state) ~= "table" then
        state = {items={}, nonCommercial={}, version=2}
        player.data.customVariables.arenaFurniture = state
    end
    state.items = state.items or {}
    -- Counts taken from the original cell are tracked separately. They can be
    -- placed again, but never converted into shop gold even if an administrator
    -- later assigns a sale price to the same refId.
    state.nonCommercial = state.nonCommercial or {}
    state.version = math.max(tonumber(state.version) or 1, 2)
    return state
end

local function inventory(pid)
    local state=furnitureState(pid)
    return state and state.items or nil
end

local function nonCommercial(pid)
    local state=furnitureState(pid)
    return state and state.nonCommercial or nil
end

local function goldCount(pid)
    local total = 0
    for _, item in pairs(Players[pid].data.inventory or {}) do
        if item.refId and item.refId:lower() == GOLD then total = total + (tonumber(item.count) or 0) end
    end
    return total
end

function marketplace.Buy(pid, refId)
    local stock = inventory(pid)
    local item = type(refId)=="string" and byId[refId:lower()] or nil
    if not stock or not item or item.price<1 then return false, tr(pid,"Предмет не продаётся или не найден в каталоге.","This item is unavailable in the catalog.") end
    if goldCount(pid) < item.price then return false, tr(pid,"Недостаточно золота: ","Not enough gold: ") .. tostring(item.price) end
    -- Never trust price/quantity from GUI or client packets.
    inventoryHelper.removeClosestItem(Players[pid].data.inventory, GOLD, item.price)
    tableHelper.cleanNils(Players[pid].data.inventory)
    stock[item.refId] = (tonumber(stock[item.refId]) or 0) + 1
    Players[pid]:Save()
    Players[pid]:LoadItemChanges({{refId=GOLD, count=item.price}}, enumerations.inventory.REMOVE)
    return true, tr(pid,"Куплено: ","Purchased: ") .. localName(pid,item) .. tr(pid,". В мебельном инвентаре: ",". In furniture inventory: ") .. stock[item.refId]
end

local function ownedRoom(pid)
    local cell = tes3mp.GetCell(pid)
    if type(cell) ~= "string" or cell == "" then return nil, tr(pid,"Не удалось определить помещение.","Could not determine the room.") end
    local base = houseHelper.GetBaseCellForInstance(cell) or cell
    local property = houseHelper.GetProperty(base)
    if not property or houseHelper.GetAccessLevel(pid, property) ~= "owner" and
        houseHelper.GetAccessLevel(pid, property) ~= "coowner" then
        return nil, tr(pid,"Размещение разрешено только владельцу и совладельцам дома.","Only the owner and co-owners may place furniture here.")
    end
    if houseHelper.GetInstanceDescription(base, property.owner) ~= cell then
        return nil, tr(pid,"Войдите в личный экземпляр дома, а не в общую ячейку.","Enter your private home instance instead of the shared cell.")
    end
    local purgeBlocksFurniture
    if type(houseHelper.IsPurgeBlockingFurniture) == "function" then
        purgeBlocksFurniture = houseHelper.IsPurgeBlockingFurniture(cell)
    else
        purgeBlocksFurniture = houseHelper.IsPurgePending(cell)
    end
    if purgeBlocksFurniture then
        return nil, tr(pid,"Подготовка дома не завершена. Повторите попытку после синхронизации.","The home is still being prepared. Try again after synchronization.")
    end
    return cell
end

function marketplace.Place(pid, refId)
    local stock = inventory(pid)
    local item = type(refId)=="string" and byId[refId:lower()] or nil
    if not stock or not item or (tonumber(stock[item.refId]) or 0) < 1 then
        return false, tr(pid,"Этого предмета нет в мебельном инвентаре.","This item is not in your furniture inventory.")
    end
    local cell, why = ownedRoom(pid)
    if not cell then return false, why end
    local protectedStock=nonCommercial(pid)
    local useProtected=protectedStock and (tonumber(protectedStock[item.refId]) or 0)>0
    local rot = tonumber(tes3mp.GetRotZ(pid)) or 0
    local location = {
        posX = tes3mp.GetPosX(pid) + 100 * math.sin(rot),
        posY = tes3mp.GetPosY(pid) + 100 * math.cos(rot),
        posZ = tes3mp.GetPosZ(pid) + 40,
        rotX=0,rotY=0,rotZ=rot
    }
    -- A client cannot supply a refId, location or cell for this operation.
    local created = logicHandler.CreateObjectAtLocation(cell, location, {
        refId=item.refId, count=1, charge=-1, enchantmentCharge=-1, soul=""
    }, "place")
    if type(created) ~= "string" or created == "" then
        return false, tr(pid,"Не удалось создать предмет; мебель осталась в инвентаре.","Could not place the item; it remains in your inventory.")
    end
    local room = LoadedCells[cell]
    if not room or not room.data or not room.data.objectData[created] then
        -- Object was placed but storage is missing: do not decrement the stock.
        tes3mp.LogMessage(enumerations.log.ERROR, "[ArenaFurniture] placed object has no server state: " .. tostring(created))
        return false, tr(pid,"Ошибка сохранения размещённого предмета.","Could not save the placed item.")
    end
    -- Persistent ownership marker is server generated, never accepted from a
    -- client-origin object-place packet; other players cannot claim this piece.
    local object = room.data.objectData[created]
    object.arenaFurnitureOwner = tostring(Players[pid].accountName):lower()
    object.arenaFurnitureOriginalPrice = item.price
    object.arenaFurnitureRefId = item.refId
    object.arenaFurnitureNonCommercial = useProtected and true or nil
    room:Save()
    stock[item.refId] = stock[item.refId] - 1
    if useProtected then
        protectedStock[item.refId]=(tonumber(protectedStock[item.refId]) or 0)-1
        if protectedStock[item.refId]<=0 then protectedStock[item.refId]=nil end
    end
    if stock[item.refId] <= 0 then stock[item.refId] = nil end
    Players[pid]:Save()
    return true, tr(pid,"Предмет размещён. Теперь его можно переместить существующим режимом захвата.","Furniture placed. You can now move it with the existing grab mode.")
end

-- This is NOT a generic object-delete API: only server-marked purchases in
-- the current owned home are ever eligible. Chests with contents cannot be
-- packed away or sold, preventing accidental inventory loss/duplication.
local function ownedFurniture(pid, uniqueIndex)
    if type(uniqueIndex) ~= "string" or not uniqueIndex:match("^%d+%-%d+$") then
        return nil, nil, tr(pid,"Недопустимый идентификатор предмета.","Invalid item identifier.")
    end
    local cell, reason = ownedRoom(pid)
    if not cell then return nil, nil, reason end
    local room = LoadedCells[cell]
    local object = room and room.data and room.data.objectData and room.data.objectData[uniqueIndex]
    local name = tostring(Players[pid].accountName):lower()
    if not object or object.arenaFurnitureOwner ~= name or
        not object.arenaFurnitureRefId or object.refId ~= object.arenaFurnitureRefId or
        not byId[object.refId:lower()] then
        return nil, nil, tr(pid,"Предмет не принадлежит вам или не куплен в магазине мебели.","You do not own this item or it was not purchased from the furniture shop.")
    end
    if type(object.inventory) == "table" then
        for _, item in pairs(object.inventory) do
            if tonumber(item.count) and item.count > 0 then
                return nil, nil, tr(pid,"Сначала освободите контейнер от вещей.","Empty the container first.")
            end
        end
    end
    return room, object
end

-- DeleteObjectForEveryone is authoritative, but a client may still have a local
-- physics/activation handle for the reference that opened the quick-action menu.
-- Re-send the already-authorized delete directly to the initiating player after
-- persistent state has been updated. This is idempotent and prevents one ESM prop
-- from visually lingering until the next cell reload while another disappears.
local function refreshDeletedObjectForPlayer(pid, room, uniqueIndex)
    if not Players[pid] or not room or not room.description then return end
    logicHandler.DeleteObjectForPlayer(pid,room.description,uniqueIndex)
end

function marketplace.ReturnToInventory(pid, uniqueIndex)
    local stock = inventory(pid)
    if not stock then return false, tr(pid,"Игрок не авторизован.","Player is not logged in.") end
    local room, object, why = ownedFurniture(pid, uniqueIndex)
    if not room then return false, why end
    local refId = object.refId
    local cell = room.description
    logicHandler.DeleteObjectForEveryone(cell, uniqueIndex)
    -- The server's deletion is authoritative. Never credit stock if the
    -- object remains in persistent objectData after a failed deletion.
    if room.data.objectData[uniqueIndex] ~= nil then
        return false, tr(pid,"Не удалось удалить предмет из ячейки.","Could not remove the item from the cell.")
    end
    stock[refId] = (tonumber(stock[refId]) or 0) + 1
    if object.arenaFurnitureNonCommercial then
        local protectedStock=nonCommercial(pid)
        protectedStock[refId]=(tonumber(protectedStock[refId]) or 0)+1
    end
    room:Save()
    Players[pid]:Save()
    refreshDeletedObjectForPlayer(pid,room,uniqueIndex)
    return true, tr(pid,"Мебель возвращена в инвентарь.","Furniture returned to inventory.")
end

-- A quick-actions request refers to a concrete, server-validated object index.
-- The deletion must finish in persistent room state BEFORE inventory or gold is credited.
function marketplace.DeletePlaced(pid, uniqueIndex)
    local room, object, why = ownedFurniture(pid, uniqueIndex)
    if not room then return false, why end
    logicHandler.DeleteObjectForEveryone(room.description, uniqueIndex)
    if room.data.objectData[uniqueIndex] ~= nil then
        return false, tr(pid,"Не удалось удалить предмет.","Could not remove the object.")
    end
    room:Save()
    refreshDeletedObjectForPlayer(pid,room,uniqueIndex)
    return true, tr(pid,"Предмет удалён из мира.","Object removed from the world.")
end

function marketplace.SellPlaced(pid, uniqueIndex)
    local room, object, why = ownedFurniture(pid, uniqueIndex)
    if not room then return false, why end
    local item=byId[object.refId:lower()]
    if object.arenaFurnitureNonCommercial then
        return false,tr(pid,"Эта мебель взята из исходной ячейки и не продаётся.",
            "Furniture taken from the original cell cannot be sold.")
    end
    if not item or item.price<1 then
        return false,tr(pid,"Этот пресет не продаётся.","This preset cannot be sold.")
    end
    local value=math.max(1,math.floor(item.price*SELLBACK_RATIO))
    logicHandler.DeleteObjectForEveryone(room.description,uniqueIndex)
    if room.data.objectData[uniqueIndex] ~= nil then
        return false,tr(pid,"Не удалось удалить предмет.","Could not remove the object.")
    end
    inventoryHelper.addItem(Players[pid].data.inventory,GOLD,value,-1,-1,"","",0)
    room:Save()
    Players[pid]:Save()
    Players[pid]:LoadItemChanges({{refId=GOLD,count=value,charge=-1,enchantmentCharge=-1,soul=""}},
        enumerations.inventory.ADD)
    refreshDeletedObjectForPlayer(pid,room,uniqueIndex)
    return true,tr(pid,"Предмет продан за ","Object sold for ")..value..tr(pid," золота."," gold.")
end

-- Register an ESM/ESP-verified, non-commercial preset and pack one existing
-- decoration in the owner's PRIVATE home. Neither client prices nor arbitrary
-- model names are accepted; catalog price=0 permanently excludes shop/sellback.
-- Naming an existing world plant does not delete it or mint furniture inventory.
-- An untrusted cell-index/refId pair is NOT proof of ownership of an ESM ref.
-- Registering only an ESM-verified zero-price catalog entry is safe; only a
-- separately server-tagged owner reference may ever be packed/deleted/sold.
function marketplace.RegisterPresetName(pid, refId, name)
    local cell,why=ownedRoom(pid)
    if not cell or not canDecorate(pid) or not houseHelper.IsInstanceCell(cell) then
        return false,why or tr(pid,"Доступно только в личном доме.","Private homes only.")
    end
    if type(refId)~="string" or #refId>120 or not textValid(name) then
        return false,tr(pid,"Неверное имя или предмет.","Invalid name or object.")
    end
    if byId[refId:lower()] then
        return false,tr(pid,"Такой пресет уже существует.","Preset already exists.")
    end
    local original=autoCatalog.findActiveRecord(config,jsonInterface,refId)
    local safe=original and autoCatalog.manualCandidate(original)
    if not safe then
        return false,tr(pid,"Неизвестная или небезопасная модель.","Unknown or unsafe model.")
    end
    local key=refId:lower()
    manualEntries[key]={refId=safe.refId,category=safe.category or "Растения",price=0,
        nameRu=name,nameEn=select(2,catalogNames({name=name,refId=safe.refId})),style=styleFor(name)}
    if not saveManual() or not loadCatalog() or not byId[key] then
        manualEntries[key]=nil
        saveManual()
        return false,tr(pid,"Не удалось сохранить пресет.","Could not save preset.")
    end
    return true,tr(pid,"Пресет сохранён без цены. Исходный предмет остался на месте.",
        "Free private preset saved. The original world object stays in place.")
end

function marketplace.RegisterPrivate(pid, refId, uniqueIndex, name)
    local stock=inventory(pid)
    if not stock or not canDecorate(pid) then
        return false,tr(pid,"Нет прав на управление предметами.","No decoration permission.")
    end
    local cell, why=ownedRoom(pid)
    if not cell or not houseHelper.IsInstanceCell(cell) then
        return false,why or tr(pid,"Доступно только в личном доме.","Private homes only.")
    end
    if type(refId)~="string" or #refId>120 or not textValid(name) or
        type(uniqueIndex)~="string" or not uniqueIndex:match("^%d+%-%d+$") then
        return false,tr(pid,"Неверный предмет или название.","Invalid object or preset name.")
    end
    local room=LoadedCells[cell]
    if not room or not room.data then return false,tr(pid,"Ячейка ещё не загружена.","Cell not loaded.") end
    -- Never mint inventory for an arbitrary ESM index supplied by a client.
    -- The server must have created and tagged this very furniture reference.
    local object=room.data.objectData and room.data.objectData[uniqueIndex]
    if not object or type(object.refId)~="string" or
        object.refId:lower()~=refId:lower() or
        object.arenaFurnitureOwner~=tostring(Players[pid].accountName):lower() or
        type(object.arenaFurnitureRefId)~="string" or
        object.arenaFurnitureRefId:lower()~=refId:lower() then
        return false,tr(pid,"Можно регистрировать только свою размещённую мебель; растения исходной ячейки можно перемещать, но нельзя присваивать.",
            "Only server-owned placed furniture can be registered; original-cell plants may be moved but not claimed.")
    end
    if type(object.inventory)=="table" then
        for _,content in pairs(object.inventory) do
            if tonumber(content.count) and content.count>0 then
                return false,tr(pid,"Сначала освободите контейнер.","Empty the container first.")
            end
        end
    end
    if byId[refId:lower()] then
        return false,tr(pid,"Пресет уже существует: используйте «Убрать в инвентарь».",
            "Preset already exists: use Return to inventory.")
    end
    local original,reason=autoCatalog.findActiveRecord(config,jsonInterface,refId)
    local safe=original and autoCatalog.manualCandidate(original)
    if not safe then
        return false,tr(pid,"Пресет небезопасен или отсутствует в модах.","Unsafe or unloaded preset.")
    end
    local key=refId:lower()
    local existing=byId[key]
    if not existing then
        local category=safe.category or "Мебель"
        manualEntries[key]={refId=safe.refId,category=category,price=0,nameRu=name,nameEn=select(2,catalogNames({name=name,refId=safe.refId})),style=styleFor(name)}
        if not saveManual() or not loadCatalog() or not byId[key] then
            manualEntries[key]=nil
            saveManual()
            return false,tr(pid,"Не удалось сохранить пресет.","Could not save the preset.")
        end
        existing=byId[key]
    end
    logicHandler.DeleteObjectForEveryone(cell,uniqueIndex)
    if room.data.objectData and room.data.objectData[uniqueIndex] then
        return false,tr(pid,"Не удалось убрать предмет из мира.","Could not pack the object.")
    end
    stock[existing.refId]=(tonumber(stock[existing.refId]) or 0)+1
    local protectedStock=nonCommercial(pid)
    protectedStock[existing.refId]=(tonumber(protectedStock[existing.refId]) or 0)+1
    room:Save()
    Players[pid]:Save()
    return true,tr(pid,"Пресет добавлен без цены, предмет в мебельном инвентаре: ",
        "Private preset saved, object moved to furniture inventory: ")..name
end

local function baseDeleted(room, uniqueIndex)
    local list=room and room.data and room.data.packets and room.data.packets.delete or {}
    for _,value in ipairs(list) do if value==uniqueIndex then return true end end
    return false
end

-- Validate an original (mpNum 0) decoration inside a home the player may edit.
-- This intentionally accepts only ESM/ESP-verified furniture records and never
-- doors, architecture, scripted objects or loot-bearing base containers.
local function originalFurniture(pid, refId, uniqueIndex, allowDestroyContents)
    if type(refId)~="string" or #refId>120 or type(uniqueIndex)~="string" or
        not uniqueIndex:match("^[1-9]%d*%-0$") then
        return nil,nil,nil,tr(pid,"Недопустимый исходный предмет.","Invalid original object.")
    end
    local cell,why=ownedRoom(pid)
    if not cell or not canDecorate(pid) then return nil,nil,nil,why or tr(pid,"Нет прав.","No permission.") end
    local room=LoadedCells[cell]
    if not room or not room.data then return nil,nil,nil,tr(pid,"Ячейка ещё не загружена.","Cell is not loaded yet.") end
    if baseDeleted(room,uniqueIndex) then return nil,nil,nil,tr(pid,"Предмет уже удалён.","Object is already removed.") end
    local object=room.data.objectData and room.data.objectData[uniqueIndex]
    if object and (type(object.refId)~="string" or object.refId:lower()~=refId:lower()) then
        return nil,nil,nil,tr(pid,"Предмет изменился; наведитесь на него снова.","Object changed; target it again.")
    end
    if not allowDestroyContents and object and type(object.inventory)=="table" then
        for _,content in pairs(object.inventory) do
            if tonumber(content.count) and content.count>0 then
                return nil,nil,nil,tr(pid,"Сначала освободите контейнер.","Empty the container first.")
            end
        end
    end
    local item=byId[refId:lower()]
    local safe
    if item then
        safe={refId=item.refId,name=localName(pid,item),category=item.category}
    else
        local record=autoCatalog.findActiveRecord(config,jsonInterface,refId)
        safe=record and autoCatalog.manualCandidate(record) or nil
        -- A number of vanilla home sacks/chests have base contents, so the
        -- catalogue correctly refuses to sell them. Once the ACTUAL home
        -- instance has been emptied, however, the owner should be able to pack
        -- that piece of furniture. Re-run the furniture classifier with only
        -- the base-content marker cleared; scripts/doors/organic containers and
        -- all other deny rules remain in force.
        if not safe and record and record.kind=="CONT" and record.hasContents then
            local emptied = allowDestroyContents == true
            if not emptied and object and type(object.inventory)=="table" then
                emptied=true
                for _,content in pairs(object.inventory) do
                    if tonumber(content.count) and content.count>0 then emptied=false;break end
                end
            end
            if emptied then
                local copy={}
                for k,v in pairs(record) do copy[k]=v end
                copy.hasContents=false
                safe=autoCatalog.manualCandidate(copy)
            end
        end
    end
    if not safe then
        return nil,nil,nil,tr(pid,"Этот объект нельзя забирать как мебель.","This object is not eligible furniture.")
    end
    return room,object,{item=item,safe=safe}
end

local function ensureOriginalPreset(pid, refId, candidate)
    local key=refId:lower()
    local item=byId[key]
    if item then return item end
    local safe=candidate and candidate.safe
    if not safe then return nil end
    manualEntries[key]={refId=safe.refId,category=safe.category or "Декор",price=0,
        nameRu=safe.name or safe.refId,nameEn=select(2,catalogNames(safe)),style=styleFor(safe.name or safe.refId)}
    if not saveManual() or not loadCatalog() then
        manualEntries[key]=nil;saveManual();return nil
    end
    return byId[key]
end

function marketplace.TakeWorld(pid, refId, uniqueIndex)
    -- Shop-placed furniture keeps its existing ownership/provenance path.
    local _,owned=ownedFurniture(pid,uniqueIndex)
    if owned then return marketplace.ReturnToInventory(pid,uniqueIndex) end
    local room,object,candidate,why=originalFurniture(pid,refId,uniqueIndex)
    if not room then return false,why end
    local item=ensureOriginalPreset(pid,refId,candidate)
    if not item then return false,tr(pid,"Не удалось создать безопасный пресет.","Could not create a safe preset.") end
    logicHandler.DeleteObjectForEveryone(room.description,uniqueIndex)
    if not baseDeleted(room,uniqueIndex) then
        return false,tr(pid,"Не удалось убрать исходный предмет.","Could not remove the original object.")
    end
    houseHelper.RecordDeletedFurniture(room.description,uniqueIndex,
        (object and object.refId) or item.refId or refId)
    local stock=inventory(pid);local protectedStock=nonCommercial(pid)
    stock[item.refId]=(tonumber(stock[item.refId]) or 0)+1
    protectedStock[item.refId]=(tonumber(protectedStock[item.refId]) or 0)+1
    room:Save();Players[pid]:Save()
    refreshDeletedObjectForPlayer(pid,room,uniqueIndex)
    return true,tr(pid,"Мебель убрана в инвентарь (без права продажи).",
        "Furniture packed into inventory (not sellable).")
end

function marketplace.DeleteWorld(pid, refId, uniqueIndex)
    local _,owned=ownedFurniture(pid,uniqueIndex)
    if owned then return marketplace.DeletePlaced(pid,uniqueIndex) end
    local room,object,candidate,why=originalFurniture(pid,refId,uniqueIndex,true)
    if not room then return false,why end
    logicHandler.DeleteObjectForEveryone(room.description,uniqueIndex)
    if not baseDeleted(room,uniqueIndex) then
        return false,tr(pid,"Не удалось удалить исходный предмет.","Could not delete the original object.")
    end
    houseHelper.RecordDeletedFurniture(room.description,uniqueIndex,
        (object and object.refId) or refId)
    room:Save()
    refreshDeletedObjectForPlayer(pid,room,uniqueIndex)
    return true,tr(pid,"Исходная мебель удалена из этой ячейки.",
        "Original furniture removed from this cell.")
end

function marketplace.SellInventory(pid, refId)
    local stock = inventory(pid)
    local item = type(refId)=="string" and byId[refId:lower()] or nil
    if not stock or not item or (tonumber(stock[item.refId]) or 0) < 1 then
        return false, tr(pid,"Этой мебели нет в инвентаре.","This furniture is not in your inventory.")
    end
    if item.price<1 then return false,tr(pid,"Этот пресет не продаётся.","This preset cannot be sold.") end
    local protectedStock=nonCommercial(pid)
    local protectedCount=protectedStock and (tonumber(protectedStock[item.refId]) or 0) or 0
    if (tonumber(stock[item.refId]) or 0)-protectedCount<1 then
        return false,tr(pid,"Эта мебель была взята из исходной ячейки и не продаётся.",
            "This furniture was taken from the original cell and cannot be sold.")
    end
    local value = math.max(1, math.floor(item.price * SELLBACK_RATIO))
    stock[item.refId] = stock[item.refId]-1
    if stock[item.refId] <= 0 then stock[item.refId]=nil end
    inventoryHelper.addItem(Players[pid].data.inventory, GOLD, value, -1, -1, "", "", 0)
    Players[pid]:Save()
    Players[pid]:LoadItemChanges({{refId=GOLD,count=value,charge=-1,enchantmentCharge=-1,soul=""}},
        enumerations.inventory.ADD)
    return true, tr(pid,"Мебель продана за ","Furniture sold for ") .. value .. tr(pid," золота."," gold.")
end

function marketplace.GetOwned(pid)
    return inventory(pid)
end

local function showCategories(pid)
    local labels = {}
    for _, name in ipairs(categories) do labels[#labels+1] = cleanLine(categoryName(pid,name)) end
    labels[#labels+1] = tr(pid,"Мой инвентарь","My inventory")
    labels[#labels+1] = tr(pid,"Закрыть","Close")
    selection[pid] = {kind="categories", values=categories}
    tes3mp.CustomMessageBox(pid, GUI_CATEGORY, tr(pid,"Мебель: выберите категорию","Furniture: choose a category"), table.concat(labels, ";"))
end

local function showItems(pid, category, page)
    local entries = catalog[category] or {}
    page = math.max(1,math.min(math.ceil(#entries/PAGE_SIZE), tonumber(page) or 1))
    local start = (page-1)*PAGE_SIZE+1
    local labels, values = {}, {}
    for i=start, math.min(#entries,start+PAGE_SIZE-1) do
        local item = entries[i]
        labels[#labels+1] = cleanLine(localName(pid,item)) .. " (" .. item.price .. tr(pid," зм)"," gold)")
        values[#values+1] = item.refId
    end
    local nextPage = #entries > start+PAGE_SIZE-1
    if nextPage then labels[#labels+1]=tr(pid,"Далее","Next") end
    if page>1 then labels[#labels+1]=tr(pid,"Назад","Back") end
    labels[#labels+1] = tr(pid,"Категории","Categories")
    selection[pid] = {kind="items", category=category, page=page, values=values,
        hasNext=nextPage, hasPrev=page>1}
    tes3mp.CustomMessageBox(pid, GUI_ITEMS, tr(pid,"Магазин: ","Shop: ") .. cleanLine(categoryName(pid,category)) .. " (" .. page .. ")", table.concat(labels,";"))
end

local function showInventory(pid)
    local stock = inventory(pid)
    if not stock then return end
    local refs = {}
    for refId, count in pairs(stock) do
        if tonumber(count) and count > 0 then refs[#refs+1] = refId end
    end
    table.sort(refs)
    local labels = {}
    for _, refId in ipairs(refs) do
        local item=byId[refId:lower()]
        labels[#labels+1] = cleanLine(item and localName(pid,item) or refId) .. " x" .. tostring(stock[refId]) ..
            (item and "" or tr(pid," (мод отключён)"," (mod disabled)"))
    end
    labels[#labels+1] = tr(pid,"Мебель в доме","Furniture in this home")
    labels[#labels+1] = tr(pid,"Категории","Categories")
    selection[pid] = {kind="inventory", values=refs}
    tes3mp.CustomMessageBox(pid, GUI_INVENTORY, tr(pid,"Моя мебель (выберите для установки)","My furniture (select an item to place)"), table.concat(labels,";"))
end

function marketplace.ShowPlaced(pid)
    if not inventory(pid) then return end
    local cell=ownedRoom(pid)
    if not cell then say(pid,tr(pid,"Войдите в свой дом, чтобы управлять размещённой мебелью.","Enter your home to manage placed furniture."));return end
    local room=LoadedCells[cell]
    if not room or type(room.data)~="table" or type(room.data.objectData)~="table" then
        say(pid,tr(pid,"Ячейка ещё не загружена. Повторите попытку после входа в дом.","The cell has not loaded yet. Try again after entering your home."))
        return
    end
    local refs={}
    for uniqueIndex,object in pairs(room and room.data.objectData or {}) do
        if object.arenaFurnitureOwner==tostring(Players[pid].accountName):lower() and
            object.arenaFurnitureRefId==object.refId and byId[object.refId:lower()] then
            refs[#refs+1]=uniqueIndex
        end
    end
    table.sort(refs)
    local labels={}
    -- The GUI menu is kept bounded. A separate full layout with scrolling is
    -- required before showing more than the first 12 placed objects at once.
    for i=1,math.min(#refs,12) do
        local item=room.data.objectData[refs[i]]
        labels[#labels+1]=cleanLine(localName(pid,byId[item.refId:lower()])).." ("..refs[i]..")"
    end
    local visible={}
    for i=1,math.min(#refs,12) do visible[i]=refs[i] end
    labels[#labels+1]=tr(pid,"Мой инвентарь","My inventory")
    selection[pid]={kind="placed",values=visible}
    tes3mp.CustomMessageBox(pid,GUI_PLACED,tr(pid,"Мебель в доме","Furniture in this home"),table.concat(labels,";"))
end

-- H002 native furniture protocol. The old message-box path remains available
-- to an H001 client; the newer engine requests FURNITURE_UI explicitly.
-- All currency, placement, prices and ownership remain server authoritative.
local NATIVE_PAGE = 18
local CONTROL_PREFIX = "@@AMP_HOUSE@@"
local function safeField(value)
    return tostring(value or ""):gsub("[\\\r\n\t]", " ")
end
local function deliver(pid, ...)
    local fields = {...}
    for i=1,#fields do fields[i]=safeField(fields[i]) end
    tes3mp.SendMessage(pid, CONTROL_PREFIX .. table.concat(fields,"\t") .. "\n", false)
end

-- The category is a property of the server catalog, not a user-supplied refId.
-- Subcategories are derived from stable ID fragments and human-readable names
-- to work with mod-added furniture without a second hand-curated JSON file.
local function subcategory(item)
    return item.style or styleFor(item.nameRu or item.name)
end

local function sendInventory(pid)
    local stock=inventory(pid)
    if not stock then return end
    deliver(pid,"FS_STOCK_BEGIN")
    local refs={}
    for refId,count in pairs(stock) do
        if tonumber(count) and count>0 then refs[#refs+1]=refId end
    end
    table.sort(refs)
    for _,refId in ipairs(refs) do
        local item=byId[refId:lower()]
        deliver(pid,"FS_STOCK",refId,math.floor(stock[refId]),item and localName(pid,item) or "",
            item and item.nameRu or "",item and item.nameEn or "")
    end
    deliver(pid,"FS_STOCK_END")
end

local function sendPlaced(pid)
    local cell=ownedRoom(pid)
    deliver(pid,"FS_PLACED_BEGIN")
    if cell then
        local room=LoadedCells[cell]
        if room and room.data and room.data.objectData then
            local indices={}
            local name=tostring(Players[pid].accountName):lower()
            for index,object in pairs(room.data.objectData) do
                if object.arenaFurnitureOwner==name and object.arenaFurnitureRefId and
                    object.refId==object.arenaFurnitureRefId and byId[object.refId:lower()] then
                    indices[#indices+1]=index
                end
            end
            table.sort(indices)
            for _,index in ipairs(indices) do
                local object=room.data.objectData[index]
                local item=byId[object.refId:lower()]
                deliver(pid,"FS_PLACED",index,object.refId,localName(pid,item),item.nameRu,item.nameEn)
            end
        end
    end
    deliver(pid,"FS_PLACED_END")
end

local function sendCategory(pid, category, page, filter)
    if type(category)~="string" or not catalog[category] then
        deliver(pid,"FS_NOTICE",tr(pid,"Категория недоступна.","Category unavailable."))
        return
    end
    local entries={}
    if filter~="nordic" and filter~="dunmer" and filter~="imperial" and filter~="other" then filter="*" end
    for _,item in ipairs(catalog[category]) do
        if item.price>0 and (filter=="*" or subcategory(item)==filter) then
            entries[#entries+1]=item
        end
    end
    local pages=math.max(1,math.ceil(#entries/NATIVE_PAGE))
    page=math.max(1,math.min(pages,math.floor(tonumber(page) or 1)))
    deliver(pid,"FS_ITEMS_BEGIN",category,page,pages,filter or "")
    for i=(page-1)*NATIVE_PAGE+1,math.min(page*NATIVE_PAGE,#entries) do
        local item=entries[i]
        deliver(pid,"FS_ITEM",item.refId,localName(pid,item),item.category,subcategory(item),item.price,item.nameRu,item.nameEn)
    end
    deliver(pid,"FS_ITEMS_END")
end

function marketplace.Menu(pid, category, page)
    if not canDecorate(pid) then return end
    if category and catalog[category] then
        local list={}
        for _,item in ipairs(catalog[category]) do
            if item.price>0 then list[#list+1]=item end
        end
        local pageSize=45
        local pages=math.max(1,math.ceil(#list/pageSize))
        page=math.max(1,math.min(pages,math.floor(tonumber(page) or 1)))
        deliver(pid,"FS_MENU_BEGIN","items",page,pages)
        if page>1 then deliver(pid,"FS_MENU_ITEM","@prev",tr(pid,"◀ Предыдущие","◀ Previous")) end
        for i=(page-1)*pageSize+1, math.min(page*pageSize,#list) do
            local item=list[i]
            deliver(pid,"FS_MENU_ITEM",item.refId,localName(pid,item).." — "..item.price)
        end
        if page<pages then deliver(pid,"FS_MENU_ITEM","@next",tr(pid,"Следующие ▶","Next ▶")) end
    else
        deliver(pid,"FS_MENU_BEGIN","categories",1,1)
        if #categories==0 then
            deliver(pid,"FS_MENU_ITEM","@empty",tr(pid,
                "Каталог пуст. Проверьте пути ESM/ESP и обновите список.",
                "Catalog empty. Check ESM/ESP paths and refresh the list."))
        end
        for _,cat in ipairs(categories) do
            deliver(pid,"FS_MENU_ITEM",cat,categoryName(pid,cat).." ("..#catalog[cat]..")")
        end
    end
    deliver(pid,"FS_MENU_END")
end
-- Used by the server's placement permission hook. A client cannot add to this
-- list: it is built from real active records or staff-reviewed manual entries.
function marketplace.HasCatalogItem(refId)
    return type(refId)=="string" and byId[refId:lower()]~=nil
end
function marketplace.Role(pid)
    if not Players[pid] or not Players[pid]:IsLoggedIn() then return end
    deliver(pid,"FS_ROLE",staff(pid) and "1" or "0",canDecorate(pid) and "1" or "0")
end
function marketplace.Refresh(pid)
    if not staff(pid) then return false,tr(pid,"Нет прав.","No permission.") end
    if not loadCatalog() then return false,tr(pid,"Файлы ESM/ESP недоступны. Проверьте пути в arenaFurnitureSources.json.",
        "ESM/ESP are unavailable. Check arenaFurnitureSources.json.") end
    local count=0
    for _,item in pairs(byId) do count=count+1 end
    return true,tr(pid,"Каталог обновлён, предметов: ","Catalog refreshed; items: ")..tostring(count)
end
function marketplace.Capture(pid, refId, uniqueIndex)
    if not staff(pid) then return false,tr(pid,"Нет прав.","No permission.") end
    local cell=tes3mp.GetCell(pid)
    local room=LoadedCells[cell]
    local object=room and room.data and room.data.objectData and room.data.objectData[uniqueIndex]
    local baseRef=type(uniqueIndex)=="string" and uniqueIndex:match("^(%d+)%-0$")
    local baseStatic=baseRef and tonumber(baseRef)>0 and not object
    if type(refId)~="string" or #refId>120 or
       (not baseStatic and (not object or type(object.refId)~="string" or object.refId:lower()~=refId:lower())) then
        return false,tr(pid,"Выберите существующий предмет в текущей ячейке.","Select an existing object in the current cell.")
    end
    local record,err=autoCatalog.findActiveRecord(config,jsonInterface,refId)
    local safe=record and autoCatalog.manualCandidate(record)
    if not safe then return false,tr(pid,"Это не разрешённая мебель (скрипт, архитектура, контейнер с вещами или мод недоступен).",
        "Not eligible furniture (script, architecture, non-empty base container, or missing mod).") end
    if byId[refId:lower()] then return true,tr(pid,"Мебель уже в каталоге. Выберите её и измените свойства.","Already in catalog. Select it to edit its properties.") end
    safe.nameRu,safe.nameEn=catalogNames(safe);safe.style=styleFor(safe.name)
    manualEntries[refId:lower()]=safe
    if not saveManual() then
        manualEntries[refId:lower()]=nil
        return false,tr(pid,"Не удалось сохранить каталог вручную.","Cannot save the manual catalog.")
    end
    if not loadCatalog() then return false,tr(pid,"Каталог недоступен.","Catalog unavailable.") end
    return true,tr(pid,"Мебель добавлена. Выберите её в магазине, чтобы изменить категорию, название и цену: ",
        "Furniture added. Select it in the shop to edit its category, names and price: ")..safe.name
end
function marketplace.Edit(pid, refId, category, price, nameRu, nameEn)
    if not staff(pid) then return false,tr(pid,"Нет прав.","No permission.") end
    local item=type(refId)=="string" and byId[refId:lower()]
    price=tonumber(price)
    if not item or not validCategories[category] or not price or price~=math.floor(price) or
        price<0 or price>10000000 or not textValid(nameRu) or not textValid(nameEn) then
        return false,tr(pid,"Некорректные данные мебели.","Invalid furniture settings.")
    end
    -- The active record is already validated and present in the allow-list.
    local key=item.refId:lower()
    local previous=manualEntries[key]
    manualEntries[key]={refId=item.refId,category=category,price=price,nameRu=nameRu,
        nameEn=nameEn,style=item.style}
    if not saveManual() then
        manualEntries[key]=previous
        return false,tr(pid,"Не удалось сохранить изменения.","Could not save changes.")
    end
    if not loadCatalog() then return false,tr(pid,"Каталог недоступен.","Catalog unavailable.") end
    return true,tr(pid,"Свойства мебели сохранены.","Furniture settings saved.")
end
function marketplace.OpenNative(pid)
    if not inventory(pid) then return end
    deliver(pid,"FS_BEGIN")
    for _,category in ipairs(categories) do
        deliver(pid,"FS_CATEGORY",category,categoryKeys[category] or "")
        -- The H015 native window has categories and one style filter, no subcategory lists.
    end
    deliver(pid,"FS_END")
    deliver(pid,"FS_ADMIN",staff(pid) and "1" or "0")
    if staff(pid) then
        local choices={}
        for cat in pairs(validCategories) do choices[#choices+1]=cat end
        table.sort(choices)
        for _,cat in ipairs(choices) do deliver(pid,"FS_EDITOR_CATEGORY",cat,categoryKeys[cat] or "") end
    end
    sendInventory(pid)
    sendPlaced(pid)
    if #categories>0 then sendCategory(pid,categories[1],1) end
end

-- Client may submit only a command and a refId/index. Never trust it for
-- prices, inventory counts, category membership, ownership or the target room.
function marketplace.NativeCommand(pid,verb,argument,page,filter)
    if not inventory(pid) then return end
    if type(verb)~="string" or type(argument)~="string" or #argument>(verb=="EDIT" and 480 or 160) then return end
    if verb=="VIEW" then
        local item=byId[argument:lower()]
        if item then
            marketplace.OpenNative(pid)
            local entries=catalog[item.category] or {}
            for i,entry in ipairs(entries) do if entry.refId==item.refId then
                sendCategory(pid,item.category,math.ceil(i/NATIVE_PAGE))
                deliver(pid,"FS_SELECT",item.refId)
                break
            end end
        end
        return
    end
    if verb=="ROLE" then marketplace.Role(pid);return end
    if verb=="EDIT" then
        -- Names are separated by | inside a single protocol argument. The
        -- sender cannot forge permission, price or object existence.
        local id,cat,cost,ru,en=argument:match("^([^|]+)|([^|]+)|([^|]+)|([^|]+)|([^|]+)$")
        local ok,message=marketplace.Edit(pid,id,cat,cost,ru,en)
        deliver(pid,"FS_NOTICE",message)
        if ok then marketplace.OpenNative(pid) end
        return
    end
    if verb=="MENU" then marketplace.Menu(pid,argument~="_" and argument or nil,page);return end
    if verb=="REFRESH" then
        local ok,message=marketplace.Refresh(pid)
        deliver(pid,"FS_NOTICE",message)
        if ok then marketplace.OpenNative(pid) end
        return
    end
    if verb=="NAME_PRIVATE" then
        local ref,index=argument:match("^([^|]+)|(%d+%-%d+)$")
        local room=LoadedCells[tes3mp.GetCell(pid)]
        local object=room and room.data and room.data.objectData and room.data.objectData[index]
        local fromBase=type(index)=="string" and index:match("^[1-9]%d*%-0$")
        if not ref or not canDecorate(pid) or not ownedRoom(pid) or
            (not object and not fromBase) or (object and object.refId~=ref) then
            deliver(pid,"FS_NOTICE",tr(pid,"Предмет недоступен для регистрации.","Item cannot be registered."))
            return
        end
        local isOwned=object and object.arenaFurnitureOwner==tostring(Players[pid].accountName):lower()
            and object.arenaFurnitureRefId==ref
        selection[pid]={kind="private_name",refId=ref,uniqueIndex=index,isOwned=isOwned}
        tes3mp.InputDialog(pid,GUI_PRIVATE_NAME,tr(pid,"Название нового пресета (без цены)",
            "Name your private preset (not for sale)"),tr(pid,"Введите имя растения или мебели",
            "Enter a plant or furniture name"))
        return
    end
    if verb=="REGISTER_PRIVATE" then
        local ref,index,name=argument:match("^([^|]+)|(%d+%-%d+)|([^|]+)$")
        local ok,message=marketplace.RegisterPrivate(pid,ref,index,name)
        deliver(pid,"FS_NOTICE",message)
        if ok then sendInventory(pid) end
        return
    end
    if verb=="CAPTURE" then
        local ref,index=argument:match("^(.-)|(%d+%-%d+)$")
        local ok,message=marketplace.Capture(pid,ref,index)
        deliver(pid,"FS_NOTICE",message)
        if ok then marketplace.OpenNative(pid) end
        return
    end
    if verb=="CATEGORY" then
        sendCategory(pid,argument,page,filter)
        return
    end
    if verb=="INVENTORY" then sendInventory(pid);return end
    if verb=="PLACED" then sendPlaced(pid);return end
    local ok,message
    if verb=="TAKE_WORLD" or verb=="DELETE_WORLD" then
        local ref,index=argument:match("^([^|]+)|(%d+%-%d+)$")
        if verb=="TAKE_WORLD" then ok,message=marketplace.TakeWorld(pid,ref,index)
        else ok,message=marketplace.DeleteWorld(pid,ref,index) end
    elseif verb=="BUY" then ok,message=marketplace.Buy(pid,argument)
    elseif verb=="PLACE" then ok,message=marketplace.Place(pid,argument)
    elseif verb=="SELL" then ok,message=marketplace.SellInventory(pid,argument)
    elseif verb=="TAKE" then ok,message=marketplace.ReturnToInventory(pid,argument)
    elseif verb=="DELETE_PLACED" then ok,message=marketplace.DeletePlaced(pid,argument)
    elseif verb=="SELL_PLACED" then ok,message=marketplace.SellPlaced(pid,argument)
    else return end
    tes3mp.LogMessage(enumerations.log.INFO, "[ArenaFurniture] " .. tostring(verb) ..
        " pid=" .. tostring(pid) .. " arg=" .. tostring(argument) .. " ok=" .. tostring(ok) ..
        " message=" .. tostring(message))
    deliver(pid,"FS_NOTICE",message or (ok and tr(pid,"Готово","Done") or tr(pid,"Операция отклонена","Operation denied")))
    sendInventory(pid)
    sendPlaced(pid)
end

function marketplace.Open(pid)
    if not inventory(pid) then return end
    showCategories(pid)
end

customCommandHooks.registerCommand("furniture", function(pid, cmd)
    if not inventory(pid) then return end
    local action = tostring(cmd[2] or "")
    if action == "" or action == "shop" then marketplace.Open(pid)
    elseif action == "inventory" then showInventory(pid)
    elseif action == "place" then
        local ok, message = marketplace.Place(pid, tostring(cmd[3] or ""))
        say(pid, message)
    elseif action == "take" then
        local ok, message = marketplace.ReturnToInventory(pid, tostring(cmd[3] or ""))
        say(pid, message)
    elseif action == "refresh" then
        local ok,message=marketplace.Refresh(pid);say(pid,message)
    elseif action == "capture" then
        local ok,message=marketplace.Capture(pid,tostring(cmd[3] or ""),tostring(cmd[4] or ""));say(pid,message)
    elseif action == "sell" then
        local ok, message = marketplace.SellInventory(pid, tostring(cmd[3] or ""))
        say(pid, message)
    else say(pid, tr(pid,"Использование: /furniture [shop|inventory|place <refId>|take <uniqueIndex>|sell <refId>]","Usage: /furniture [shop|inventory|place <refId>|take <uniqueIndex>|sell <refId>]")) end
end)

customEventHooks.registerHandler("OnGUIAction", function(eventStatus, pid, id, data)
    if id ~= GUI_PRIVATE_NAME and id ~= GUI_CATEGORY and id ~= GUI_ITEMS and id ~= GUI_BUY and
        id ~= GUI_INVENTORY and id ~= GUI_PLACE and id ~= GUI_PLACED and
        id ~= GUI_PLACED_ACTION then return end
    if not inventory(pid) then return end
    local state = selection[pid]
    if not state then return end
    if id==GUI_PRIVATE_NAME then
        selection[pid]=nil
        if state.kind=="private_name" and textValid(data) then
            local ok,message
            if state.isOwned then
                ok,message=marketplace.RegisterPrivate(pid,state.refId,state.uniqueIndex,data)
            else
                ok,message=marketplace.RegisterPresetName(pid,state.refId,data)
            end
            say(pid,message)
            if ok then sendInventory(pid) end
        end
        return
    end
    local index = (tonumber(data) or -1)+1
    if index < 1 then selection[pid]=nil; return end
    if id==GUI_CATEGORY and state.kind=="categories" then
        if index <= #categories then showItems(pid, categories[index], 1)
        elseif index == #categories+1 then showInventory(pid)
        else selection[pid]=nil end
    elseif id==GUI_ITEMS and state.kind=="items" then
        if index <= #state.values then
            local refId=state.values[index]
            local item=byId[refId:lower()]
            selection[pid]={kind="buy",refId=refId}
            tes3mp.CustomMessageBox(pid,GUI_BUY,cleanLine(item.name).." — "..item.price..tr(pid," золота. Купить?"," gold. Buy?"), tr(pid,"Купить;Назад","Buy;Back"))
        else
            local pos=#state.values
            if state.hasNext then pos=pos+1; if index==pos then showItems(pid,state.category,state.page+1);return end end
            if state.hasPrev then pos=pos+1; if index==pos then showItems(pid,state.category,state.page-1);return end end
            showCategories(pid)
        end
    elseif id==GUI_BUY and state.kind=="buy" then
        if index==1 then local ok,message=marketplace.Buy(pid,state.refId);say(pid,message) end
        showCategories(pid)
    elseif id==GUI_INVENTORY and state.kind=="inventory" then
        if index<=#state.values then
            local refId=state.values[index]
            selection[pid]={kind="place",refId=refId}
            local item=byId[refId:lower()]
            if not item then
                say(pid,tr(pid,"Мод, содержащий эту мебель, отключён. Предмет сохранён в инвентаре.",
                    "The mod containing this furniture is disabled. Your item remains in inventory."))
                showInventory(pid)
                return
            end
            tes3mp.CustomMessageBox(pid,GUI_PLACE,"«"..cleanLine(item.name)..tr(pid,"»: выберите действие","»: choose an action"), tr(pid,"Установить;Продать магазину;Назад","Place;Sell to store;Back"))
        elseif index==#state.values+1 then
            marketplace.ShowPlaced(pid)
        else showCategories(pid) end
    elseif id==GUI_PLACE and state.kind=="place" then
        if index==1 then local ok,message=marketplace.Place(pid,state.refId);say(pid,message)
        elseif index==2 then local ok,message=marketplace.SellInventory(pid,state.refId);say(pid,message) end
        showInventory(pid)
    elseif id==GUI_PLACED and state.kind=="placed" then
        if index<=#state.values then
            local chosen=state.values[index]
            local room=LoadedCells[tes3mp.GetCell(pid)]
            local object=room and room.data and room.data.objectData and room.data.objectData[chosen]
            if not object then marketplace.ShowPlaced(pid);return end
            selection[pid]={kind="placed_action",uniqueIndex=chosen}
            tes3mp.CustomMessageBox(pid,GUI_PLACED_ACTION,
                cleanLine(object.refId).." ("..chosen..")", tr(pid,"Убрать в инвентарь;Назад","Return to inventory;Back"))
        else showInventory(pid) end
    elseif id==GUI_PLACED_ACTION and state.kind=="placed_action" then
        if index==1 then local ok,message=marketplace.ReturnToInventory(pid,state.uniqueIndex);say(pid,message) end
        marketplace.ShowPlaced(pid)
    end
end)

customEventHooks.registerHandler("OnPlayerDisconnect",function(eventStatus,pid)
    selection[pid]=nil
end)

if not loadCatalog() then
    tes3mp.LogMessage(enumerations.log.ERROR,"[ArenaFurniture] shop disabled: no usable furniture reference catalogue")
end
return marketplace
