-- Mutations address a concrete saved actor, not the base NPC record. Disk commit
-- precedes network publication. No unknown/partial inventory is guessed here.
return function(Content)
    local Actors={}
    local copy=Content.Copy
    local function shallow(value)
        local result={};for k,v in pairs(value or {}) do result[k]=v end;return result
    end
    local apparelMax=15
    local lower=Content.NormalizeId
    local function dead(object)
        return (tonumber(object.deathState) or 0)~=0 or
            (type(object.stats)=="table" and tonumber(object.stats.healthCurrent) and tonumber(object.stats.healthCurrent)<=0)
    end
    local function identity(item)
        local function number(value,fallback) return string.format("%.17g",tonumber(value) or fallback) end
        return table.concat({lower(item.refId),number(item.charge,-1),number(item.enchantmentCharge,-1),
            item.soul or "",item.poisonId or "",number(item.poisonCharges,0)},"\31")
    end
    local function count(item) return math.max(0,tonumber(item.count) or 0) end
    local function equipmentItem(item,amount)
        local result=copy(item);result.count=amount or 1;return result
    end
    local function resolve(cellName,index)
        if not Content.IsReady() then return nil,nil,"catalog_not_loaded" end
        if type(cellName)~="string" or type(index)~="string" or not index:match("^%d+%-%d+$") then return nil,nil,"invalid_actor_identity" end
        local cell=LoadedCells[cellName]
        if not cell then return nil,nil,"cell_not_loaded" end
        local object=cell.data and cell.data.objectData and cell.data.objectData[index]
        if not object or type(object.refId)~="string" then return nil,nil,"actor_not_found" end
        if type(object.inventory)~="table" then return nil,nil,"inventory_snapshot_required" end
        local record=Content.GetRecord("NPC_",object.refId) or Content.GetRecord("CREA",object.refId)
        if not record then return nil,nil,"actor_record_not_found" end
        return cell,object
    end
    local function append(list,value)
        for _,v in pairs(list) do if v==value then return end end
        list[#list+1]=value
    end
    local function persist(cell,data)
        if type(cell.entryFile)~="string" or cell.entryFile=="" or not jsonInterface
            or type(jsonInterface.quicksaveAtomic)~="function" then return nil,"atomic_json_persistence_required" end
        local ok,saved=pcall(jsonInterface.quicksaveAtomic,"cell/"..cell.entryFile,data)
        if not ok or saved~=true then return nil,"cell_save_failed" end
        cell.data=data
        return true
    end
    local function same(a,b)
        if type(a)~=type(b) then return false end
        if type(a)~="table" then return a==b end
        for k,v in pairs(a) do if not same(v,b[k]) then return false end end
        for k in pairs(b) do if a[k]==nil then return false end end
        return true
    end
    local function publish(cell,index,equipmentChanged,inventoryChanged)
        local ok,err=pcall(function()
            -- A gear-only command must not rebuild inventory: Container SET
            -- invokes client autoEquip and can immediately undo UnequipItem.
            if inventoryChanged then packetBuilder.UpdateContainerForRelevantPlayers(cell,index,nil) end
            if equipmentChanged then
                for _,pid in ipairs(packetBuilder.GetRelevantPlayersForCell(cell)) do
                    cell:LoadActorEquipment(pid,cell.data.objectData,{index})
                end
            end
        end)
        if not ok then
            tes3mp.LogMessage(enumerations.log.ERROR,"[ContentAPI] Saved actor requires resync: "..tostring(err))
            return "saved_resync_required"
        end
        return "saved_and_published"
    end
    local function transaction(cellName,index,mutate)
        local cell,object,err=resolve(cellName,index);if not cell then return nil,err end
        -- Single server thread: isolate only the actor and packet lists mutated
        -- below. Other actors and immutable cell metadata need no deep copy.
        local data=shallow(cell.data);data.objectData=shallow(cell.data.objectData)
        data.objectData[index]=copy(object);data.packets=shallow(cell.data.packets)
        data.packets.container=shallow(data.packets.container)
        data.packets.equipment=shallow(data.packets.equipment)
        local changed=data.objectData[index]
        local called,ok,result,gearChanged=pcall(mutate,changed,object)
        if not called then return nil,"mutation_failed: "..tostring(ok) end
        if not ok then return nil,result end
        local inventoryChanged=not same(changed.inventory,object.inventory)
        data.packets=data.packets or {};data.packets.container=data.packets.container or {}
        append(data.packets.container,index)
        if gearChanged then data.packets.equipment=data.packets.equipment or {};append(data.packets.equipment,index) end
        local saved,why=persist(cell,data);if not saved then return nil,why end
        if gearChanged and cell.actorEquipmentTransferGuards then cell.actorEquipmentTransferGuards[index]=nil end
        return result,publish(cell,index,gearChanged,inventoryChanged)
    end
    local function findItem(object,refId,condition)
        for _,item in ipairs(object.inventory) do
            if lower(item.refId)==lower(refId) and count(item)>0 and (not condition or identity(item)==identity(condition)) then return item end
        end
    end
    local function prune(object,removedRefId)
        local available={}
        for _,item in ipairs(object.inventory) do available[identity(item)]=(available[identity(item)] or 0)+count(item) end
        local changed=false
        for slot=0,18 do
            local item=object.equipment and object.equipment[slot]
            if item and item.refId and lower(item.refId)==lower(removedRefId) then
                local k=identity(item);local need=math.max(1,count(item))
                if slot==18 and (available[k] or 0)>0 and (available[k] or 0)<need then
                    item.count=available[k];available[k]=0;changed=true
                elseif (available[k] or 0)<need then object.equipment[slot]=nil;changed=true
                else available[k]=available[k]-need end
            end
        end
        return changed
    end
    local function equip(object,refId,slot,condition)
        if dead(object) then return nil,"actor_is_dead" end
        if not Content.Number(slot,0,18) then return nil,"invalid_equipment_slot" end
        local npc=Content.GetRecord("NPC_",object.refId)
        if not npc then return nil,"npc_equipment_required" end
        local record,err=Content.GetItem(refId);if not record then return nil,err end
        local allowed=false;for _,v in ipairs(record.slots or {}) do if v==slot then allowed=true end end
        if not allowed then return nil,"item_does_not_fit_slot" end
        if record.type=="CLOT" or record.type=="ARMO" then
            local race=npc and Content.GetRecord("RACE",npc.race)
            if race and race.data and race.data.beast==1 and (slot==7 or slot==5 or slot==6) then return nil,"beast_race_slot_restricted" end
        end
        object.equipment=object.equipment or {}
        -- Reserve every other occupied slot, including two identical rings.
        local used={}
        for other=0,18 do
            local item=object.equipment[other]
            if other~=slot and item and item.refId and item.refId~="" then used[identity(item)]=(used[identity(item)] or 0)+math.max(1,count(item)) end
        end
        local item
        for _,candidate in ipairs(object.inventory) do
            if lower(candidate.refId)==lower(record.id) and (not condition or identity(candidate)==identity(condition)) then
                local k=identity(candidate)
                local available=count(candidate)-(used[k] or 0)
                if available>0 then item=candidate;break else used[k]=math.max(0,(used[k] or 0)-count(candidate)) end
            end
        end
        if not item then return nil,"item_not_available_for_equipment" end
        local usesDurability=record.type=="ARMO" or
            (record.type=="WEAP" and (tonumber(record.data.type) or -1)>=0 and (tonumber(record.data.type) or -1)<=10)
        if usesDurability and tonumber(item.charge)==0 then return nil,"item_is_broken" end
        local twoHanded={[2]=true,[4]=true,[5]=true,[6]=true,[8]=true,[9]=true,[10]=true}
        if slot==16 and record.type=="WEAP" and twoHanded[record.data.type] and object.equipment[17] then
            return nil,"two_handed_conflict"
        elseif slot==17 and object.equipment[16] then
            local weapon=Content.GetRecord("WEAP",object.equipment[16].refId)
            if weapon and twoHanded[weapon.data.type] then return nil,"two_handed_conflict" end
        end
        object.equipment[slot]=equipmentItem(item,1)
        return true
    end
    function Actors.GetSnapshot(cellName,index)
        local _,object,err=resolve(cellName,index);if not object then return nil,err end
        return {refId=object.refId,contentKey=Content.GetKey(),inventory=copy(object.inventory),equipment=copy(object.equipment or {}),
            location=copy(object.location),stats=copy(object.stats),snapshots=copy(object.arenaContentSnapshots or {})}
    end
    function Actors.AddItem(cellName,index,refId,amount)
        if not Content.Number(amount,1,1000000) then return nil,"invalid_item_count" end
        local record,err=Content.GetItem(refId);if not record then return nil,err end
        return transaction(cellName,index,function(object)
            local added={refId=record.id,count=amount,charge=-1,enchantmentCharge=-1,soul="",poisonId="",poisonCharges=0}
            local existing=findItem(object,record.id,added)
            if existing then
                if count(existing)+amount>2147483647 then return nil,"item_count_overflow" end
                existing.count=count(existing)+amount
            else object.inventory[#object.inventory+1]=added end
            return true,{added=amount,refId=record.id},false
        end)
    end
    function Actors.RemoveItem(cellName,index,refId,amount)
        if type(refId)~="string" or refId=="" or not Content.Number(amount,1,1000000) then return nil,"invalid_item_arguments" end
        return transaction(cellName,index,function(object)
            local total=0;for _,item in ipairs(object.inventory) do if lower(item.refId)==lower(refId) then total=total+count(item) end end
            if total<amount then return nil,"not_enough_items" end
            local remaining=amount
            for _,item in ipairs(object.inventory) do
                if remaining>0 and lower(item.refId)==lower(refId) then local n=math.min(remaining,count(item));item.count=count(item)-n;remaining=remaining-n end
            end
            local kept={};for _,item in ipairs(object.inventory) do if count(item)>0 then kept[#kept+1]=item end end
            object.inventory=kept
            return true,{removed=amount,refId=refId},prune(object,refId)
        end)
    end
    function Actors.EquipItem(cellName,index,refId,slot)
        return transaction(cellName,index,function(object)
            local ok,err=equip(object,refId,slot);if not ok then return nil,err end
            return true,{slot=slot,refId=object.equipment[slot].refId},true
        end)
    end
    function Actors.UnequipItem(cellName,index,slot)
        if not Content.Number(slot,0,18) then return nil,"invalid_equipment_slot" end
        return transaction(cellName,index,function(object)
            object.equipment=object.equipment or {};object.equipment[slot]=nil
            return true,{slot=slot},true
        end)
    end
    local function makeSnapshot(object,equipment)
        if dead(object) then return nil,"actor_is_dead" end
        local items={};local any=false;local available={}
        for _,item in ipairs(object.inventory) do available[identity(item)]=(available[identity(item)] or 0)+count(item) end
        for slot=0,apparelMax do
            local worn=equipment and equipment[slot]
            if worn and worn.refId and worn.refId~="" then
                local source=findItem(object,worn.refId,worn)
                if not source or (available[identity(source)] or 0)<1 then return nil,"equipment_inventory_mismatch" end
                available[identity(source)]=available[identity(source)]-1
                items[slot]=equipmentItem(source);any=true
            end
        end
        if not any then return nil,"no_worn_apparel" end
        return {version=1,refId=object.refId,contentKey=Content.GetKey(),time=os.time(),items=items}
    end
    function Actors.CaptureOutfit(cellName,index,name)
        name=name or "manual"
        if type(name)~="string" or not name:match("^[%w_%-]+$") or #name>64 or name=="auto" then return nil,"invalid_snapshot_name" end
        return transaction(cellName,index,function(object)
            local snapshot,err=makeSnapshot(object,object.equipment);if not snapshot then return nil,err end
            object.arenaContentSnapshots=object.arenaContentSnapshots or {}
            local n=0;for _ in pairs(object.arenaContentSnapshots) do n=n+1 end
            if not object.arenaContentSnapshots[name] and n>=(tonumber(Content.settings.maxSnapshotsPerActor) or 8) then return nil,"too_many_snapshots" end
            object.arenaContentSnapshots[name]=snapshot;return true,{name=name},false
        end)
    end
    function Actors.DeleteOutfitSnapshot(cellName,index,name)
        if type(name)~="string" or not name:match("^[%w_%-]+$") or #name>64 then return nil,"invalid_snapshot_name" end
        return transaction(cellName,index,function(object)
            if not object.arenaContentSnapshots or not object.arenaContentSnapshots[name] then return nil,"snapshot_not_found" end
            object.arenaContentSnapshots[name]=nil;return true,{name=name},false
        end)
    end
    -- Called only after normal ActorEquipment validation and only for complete,
    -- nonempty inventory-backed apparel. A stripped packet never overwrites auto.
    function Actors.ObserveSnapshot(object,incoming)
        if not Content.IsReady() or Content.settings.autoSnapshots==false or type(object.inventory)~="table" then return end
        local snapshot=makeSnapshot(object,incoming)
        if snapshot then
            local previous=object.arenaContentSnapshots and object.arenaContentSnapshots.auto
            if previous and previous.contentKey==snapshot.contentKey and previous.refId==snapshot.refId
                and same(previous.items,snapshot.items) then return end
            object.arenaContentSnapshots=object.arenaContentSnapshots or {};object.arenaContentSnapshots.auto=snapshot
        end
    end
    local function allowRecovery(options)
        return type(options)=="table" and options.addMissing==true and Content.settings.allowItemRecovery==true
            and type(options.reason)=="string" and #options.reason>=3
    end
    local function applyOutfit(object,items,options)
        if options.addMissing and not allowRecovery(options) then return nil,"item_recovery_not_authorized" end
        for slot=0,apparelMax do
            local desired=items[slot] or items[tostring(slot)]
            if desired then
                local ok,err=equip(object,desired.refId,slot,desired)
                -- Durability/charge may have changed since the snapshot. Prefer
                -- an existing current-condition copy before recovering anything.
                if not ok and err=="item_not_available_for_equipment" then ok,err=equip(object,desired.refId,slot) end
                if not ok and allowRecovery(options) and err=="item_not_available_for_equipment" then
                    object.inventory[#object.inventory+1]=equipmentItem(desired)
                    ok,err=equip(object,desired.refId,slot,desired)
                end
                if not ok then return nil,err end
            end
        end
        return true
    end
    function Actors.RestoreOutfit(cellName,index,name,options)
        name=name or "auto";options=options or {}
        if type(options)~="table" or type(name)~="string" then return nil,"invalid_restore_arguments" end
        return transaction(cellName,index,function(object)
            local snapshot=object.arenaContentSnapshots and object.arenaContentSnapshots[name]
            if not snapshot then return nil,"snapshot_not_found" end
            if lower(snapshot.refId)~=lower(object.refId) or snapshot.contentKey~=Content.GetKey() then return nil,"snapshot_content_mismatch" end
            local ok,err=applyOutfit(object,snapshot.items,options);if not ok then return nil,err end
            object.arenaContentLastRecovery={source=name,reason=options.reason or "existing inventory",time=os.time()}
            return true,{source=name},true
        end)
    end
    -- Explicit repair of original positive-count CLOTHING only. This is a
    -- deterministic base-apparel selection, not the engine's armor/AI autoEquip.
    function Actors.RecoverBaseApparel(cellName,index,options)
        options=options or {}
        if not allowRecovery(options) then return nil,"item_recovery_not_authorized" end
        return transaction(cellName,index,function(object)
            local npc,err=Content.GetRecord("NPC_",object.refId);if not npc then return nil,err end
            local candidates={}
            for _,entry in ipairs(npc.inventory or {}) do
                if count(entry)>0 then
                    local item=Content.GetRecord("CLOT",entry.refId)
                    if item then candidates[#candidates+1]={record=item,count=count(entry)} end
                end
            end
            table.sort(candidates,function(a,b)
                local av,bv=tonumber(a.record.data.value) or 0,tonumber(b.record.data.value) or 0
                return av==bv and lower(a.record.id)<lower(b.record.id) or av>bv
            end)
            local race=npc.race and Content.GetRecord("RACE",npc.race)
            local beast=race and race.data and race.data.beast==1
            local items={};local any=false
            for _,candidate in ipairs(candidates) do
                local remaining=candidate.count
                for _,slot in ipairs(candidate.record.slots or {}) do
                    if remaining>0 and not items[slot] and not (beast and (slot==5 or slot==6 or slot==7)) then
                        items[slot]={refId=candidate.record.id,count=1,charge=-1,enchantmentCharge=-1,soul="",poisonId="",poisonCharges=0};remaining=remaining-1;any=true
                    end
                end
            end
            if not any then return nil,"no_base_clothing" end
            local ok,why=applyOutfit(object,items,options);if not ok then return nil,why end
            object.arenaContentLastRecovery={source="base_clothing",contentKey=Content.GetKey(),reason=options.reason,time=os.time()}
            return true,{source="base_clothing"},true
        end)
    end
    function Actors.PlayAnimation(cellName,index,group,options)
        options=options or {}
        if type(options)~="table" then return nil,"invalid_animation_arguments" end
        local cell,object,err=resolve(cellName,index);if not cell then return nil,err end
        if dead(object) then return nil,"actor_is_dead" end
        local mode,countValue=options.mode or 0,options.count or 1
        if type(group)~="string" or #group<1 or #group>128 or group:find("[^%w _%-]")
            or not Content.Number(mode,0,2) or not Content.Number(countValue,1,1000) then return nil,"invalid_animation_arguments" end
        if type(tes3mp.SendContentActorAnimation)~="function" then return nil,"native_animation_api_unavailable" end
        local refNum,mpNum=index:match("^(%d+)%-(%d+)$");local sent=0;local failures={}
        for _,pid in ipairs(packetBuilder.GetRelevantPlayersForCell(cell)) do
            if tes3mp.SendContentActorAnimation(pid,cellName,tonumber(refNum),tonumber(mpNum),object.refId,group,mode,countValue,options.persist==true) then sent=sent+1
            else failures[#failures+1]={pid=pid,error=tes3mp.ContentCatalogGetError()} end
        end
        return {sent=sent,failures=failures} -- delivery requests, not client playback acknowledgements
    end
    return Actors
end
