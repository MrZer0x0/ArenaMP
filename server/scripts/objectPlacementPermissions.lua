require("config")
local tableHelper = require("tableHelper")

-- H019: native hold-Activate placement is restricted to the current owner's
-- or co-owner's actual home, including player-dropped objects. Moderators and admins
-- may edit elsewhere. Script-origin transforms remain outside this validator.

local placement = {}
local denyState = {}

local function getConfig()
    return config.objectGrabPermissions or {}
end

local function wasDroppedByPlayer(cell, uniqueIndex)
    if cell == nil or type(cell.data) ~= "table" then return false end
    if type(cell.data.objectData) ~= "table" or type(cell.data.packets) ~= "table" then return false end
    local stored = cell.data.objectData[uniqueIndex]
    if type(stored) ~= "table" or stored.droppedByPlayer ~= true then return false end
    local placed = cell.data.packets.place
    return type(placed) == "table" and tableHelper.containsValue(placed, uniqueIndex)
end

-- Vanilla statics do not carry a trustworthy client-supplied record type.
-- A narrow furniture-ID convention is the fallback until the operator imports
-- exact static record IDs from the game's JSON database.
local function isStructural(id, cfg)
    if type(id) ~= "string" or id == "" then return true end
    id = id:lower()
    local blocked = cfg.structuralRefIdFragments or {
        "wall", "floor", "ceiling", "roof", "door", "window", "stair",
        "pillar", "column", "beam", "foundation", "archway", "_arch_",
        "_gate", "_bridge", "_stairs", "_ramp", "_terrain", "_cliff",
        "_exterior", "_interior", "_building", "_house_"
    }
    for _, fragment in ipairs(blocked) do
        if id:find(fragment:lower(), 1, true) then return true end
    end
    return false
end

local function staticAllowed(id, cfg)
    if type(id) ~= "string" then return false end
    id = id:lower()
    local deny = cfg.homeStaticDenyIds or {}
    if deny[id] == true then return false end
    if isStructural(id, cfg) then return false end

    -- Beds are often scripted activators rather than plain STAT furniture.
    -- Client-side placement preserves the script; this allow-list only permits
    -- the owner/co-owner to publish the new transform in their own home.
    if id:find("bed", 1, true) or id:find("bedroll", 1, true)
        or id:find("bunk", 1, true) or id:find("cot", 1, true)
        or id:find("mattress", 1, true) or id:find("hammock", 1, true)
        or id:find("sleep", 1, true) or id:find("pallet", 1, true) then return true end

    local allow = cfg.homeStaticAllowIds or {}
    if allow[id] == true then return true end
    -- A server-generated catalogue knows records from all active mods, including
    -- containers and beds whose refIds do not start with vanilla furn_ prefixes.
    -- This must never rely on a category/refId claimed by the moving client.
    local shop=rawget(_G,"arenaFurniture")
    if type(shop)=="table" and type(shop.HasCatalogItem)=="function" and
        shop.HasCatalogItem(id) then return true end
    -- Opt-in common furniture record family only; unknown architectural statics
    -- remain protected until identified from the actual Morrowind JSON.
    return cfg.allowFurniturePrefixes ~= false and
        (id:match("^furn_") ~= nil or id:match("^in_furn_") ~= nil
            or id:match("^com_furn_") ~= nil or id:match("^de_furn_") ~= nil)
end

local function isHomeEditor(pid, cellDescription)
    local ok, houseHelper = pcall(require, "houseHelper")
    if not ok or type(houseHelper) ~= "table" then return false end
    local base = houseHelper.GetBaseCellForInstance(cellDescription) or cellDescription
    local home = houseHelper.GetProperty(base)
    if type(home) ~= "table" or not home.owner or home.owner == "" then return false end
    local access = houseHelper.GetAccessLevel(pid, home)
    local actualRoom = houseHelper.GetInstanceDescription(base, home.owner)
    return (access == "owner" or access == "coowner") and
        type(actualRoom) == "string" and actualRoom:lower() == cellDescription:lower()
end

function placement.IsAllowed(pid, cellDescription, objects)
    local player = Players[pid]
    if player == nil or not player:IsLoggedIn() then return false end

    local cfg = getConfig()
    -- Y111: staff placement privilege comes from config.staffPermissions.
    if require("staffPermissions").Has(pid, "placeAnywhere") then return true end

    -- The native E/Activate physics placement path publishes transforms as
    -- CLIENT_GAMEPLAY. Do not apply this anti-grief rule to script/server moves.
    if tes3mp.GetObjectListOrigin() ~= enumerations.packetOrigin.CLIENT_GAMEPLAY then
        return true
    end

    local cell = LoadedCells[cellDescription]
    if cell == nil or type(objects) ~= "table" then return false end

    local homeEditor = isHomeEditor(pid, cellDescription)
    if not homeEditor then return false end
    if cfg.enabled == false then return true end
    -- Validate every entry atomically; a crafted multi-object packet must not
    -- smuggle a protected door/wall together with a valid dropped object.
    for uniqueIndex, object in pairs(objects) do
        local stored = type(cell.data.objectData) == "table"
            and cell.data.objectData[uniqueIndex] or nil
        local id = object.refId
        if type(stored) == "table" and type(stored.refId) == "string" then
            -- A known persistent reference cannot be relabelled by the client.
            if type(id) ~= "string" or id:lower() ~= stored.refId:lower() then return false end
            id = stored.refId
        end
        -- Structural pieces are never player-movable, even if an old save has
        -- an incorrect droppedByPlayer marker.
        if isStructural(id, cfg) then return false end
        local playerPlaced = cfg.allowPlayerDropped ~= false and wasDroppedByPlayer(cell, uniqueIndex)
        local homeStatic = homeEditor and staticAllowed(id, cfg)
        -- Furniture purchased from ArenaFurniture is created by the SERVER,
        -- not dropped by a client, so it has no droppedByPlayer flag. Only the
        -- owner/co-owner of this specific home may arrange such an object.
        -- The marker must come from persistent server objectData, never from
        -- a transform packet supplied by the client.
        local purchasedFurniture = homeEditor and type(stored) == "table" and
            type(stored.arenaFurnitureOwner) == "string" and
            stored.arenaFurnitureOwner ~= "" and stored.arenaFurnitureRefId == id
        if not playerPlaced and not homeStatic and not purchasedFurniture then return false end
    end

    return true
end

function placement.Deny(pid, cellDescription)
    local cfg = getConfig()
    local quiet = tonumber(cfg.denyQuietSeconds) or 4
    local now = os.time()
    local state = denyState[pid]

    local showMessage = false
    if state == nil or (now - state.last) >= quiet then
        showMessage = true
        tes3mp.LogMessage(enumerations.log.WARN, "Rejected protected world-object grab from " ..
            logicHandler.GetChatName(pid) .. " in " .. tostring(cellDescription))
    end

    denyState[pid] = { last = now }

    -- Roll back every rejected transform, but only show the explanation once per
    -- grab attempt. The client-side Y013-fix-01 rollback also works after release.
    tes3mp.SendMessage(pid, "@@AMP_PLACE_DENY@@1\t" .. (showMessage and "1" or "0") .. "\n", false)
end

function placement.Clear(pid)
    denyState[pid] = nil
end

return placement
