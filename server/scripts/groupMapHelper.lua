-- ArenaMP Y049 persistent personal/group map markers.
--
-- Personal markers live inside the player's JSON profile. Group markers live in
-- one server-owned JSON file keyed by persistent group id. Both are replicated
-- through hidden ChatMessage control envelopes, so Y049 does not add or reorder
-- any TES3MP packet id.
local groupMapHelper = {}

local DATA_PATH = "custom/groupMapMarkers.json"
local GROUP_PREFIX = "@@AMP_GMARK@@"
local PERSONAL_PREFIX = "@@AMP_PMARK@@"
local data = { version = 1, groups = {} }

local cfg = {
    maxPersonal = 128,
    maxGroup = 64
}
if type(config.mapMarkerSystem) == "table" then
    cfg.maxPersonal = math.max(1, tonumber(config.mapMarkerSystem["max personal markers"]) or cfg.maxPersonal)
    cfg.maxGroup = math.max(1, tonumber(config.mapMarkerSystem["max group markers"]) or cfg.maxGroup)
end

local allowedKind = { ["?"] = true, ["!"] = true, A = true, B = true, C = true }
local allowedColor = { red=true, green=true, blue=true, yellow=true, white=true, purple=true, orange=true }

local function valid(pid)
    return Players[pid] ~= nil and Players[pid]:IsLoggedIn() and type(Players[pid].data) == "table"
end

local function accountName(pid)
    if not valid(pid) then return "" end
    return tostring(Players[pid].accountName or Players[pid].name or "")
end

local function esc(v)
    v = tostring(v or "")
    v = v:gsub("\\", "\\\\"):gsub("\t", "\\t"):gsub("\r", ""):gsub("\n", "\\n")
    return v
end

local function decode(v)
    v = tostring(v or "")
    return (v:gsub("%%(%x%x)", function(hex) return string.char(tonumber(hex, 16)) end))
end

local function truncateUtf8Bytes(v, maxLen)
    if #v <= maxLen then return v end
    local cut = maxLen
    -- Never leave the string ending in the middle of an UTF-8 continuation
    -- sequence. Marker descriptions are capped in bytes for packet safety.
    while cut > 0 do
        local b = v:byte(cut + 1)
        if not b or b < 0x80 or b >= 0xC0 then break end
        cut = cut - 1
    end
    return v:sub(1, cut)
end

local function safeText(v, maxLen)
    v = decode(v):gsub("[%c|]", " "):gsub("%s+", " ")
    maxLen = maxLen or 160
    return truncateUtf8Bytes(v, maxLen)
end

local function normalizeKind(v)
    v = tostring(v or "?"):upper()
    return allowedKind[v] and v or "?"
end

local function normalizeColor(v)
    v = tostring(v or "yellow"):lower()
    return allowedColor[v] and v or "yellow"
end

local function load()
    local loaded = ampCore.EnsureJson(DATA_PATH, {version=1, groups={}}, DATA_PATH)
    if type(loaded) == "table" then data = loaded end
    data.version = 1
    data.groups = type(data.groups) == "table" and data.groups or {}
end

local function saveGroupData()
    ampCore.SaveJson(DATA_PATH, data, DATA_PATH)
end

local function savePlayer(pid)
    if valid(pid) and type(Players[pid].QuicksaveToDrive) == "function" then
        Players[pid]:QuicksaveToDrive()
    end
end

local function groupData(id)
    id = tostring(id or "")
    if id == "" then return nil end
    data.groups[id] = type(data.groups[id]) == "table" and data.groups[id] or {nextId=1, markers={}}
    local g = data.groups[id]
    g.nextId = tonumber(g.nextId) or 1
    g.markers = type(g.markers) == "table" and g.markers or {}
    return g
end

local function personalData(pid)
    if not valid(pid) then return nil end
    local p = Players[pid]
    p.data.profile = type(p.data.profile) == "table" and p.data.profile or {}
    local d = p.data.profile
    d.mapMarkers = type(d.mapMarkers) == "table" and d.mapMarkers or {}
    d.nextMapMarkerId = tonumber(d.nextMapMarkerId) or 1
    return d
end

local function sendGroupClear(pid)
    tes3mp.SendMessage(pid, GROUP_PREFIX .. "CLEAR\n", false)
end

local function sendPersonalClear(pid)
    tes3mp.SendMessage(pid, PERSONAL_PREFIX .. "CLEAR\n", false)
end

local function sendGroupMarker(pid, m)
    local fields = {
        "ADD", tostring(m.id or ""), m.paged and "1" or "0", esc(m.cell),
        tostring(tonumber(m.cellX) or 0), tostring(tonumber(m.cellY) or 0),
        tostring(tonumber(m.x) or 0), tostring(tonumber(m.y) or 0),
        esc(normalizeKind(m.kind)), esc(normalizeColor(m.color)), esc(m.text), esc(m.owner)
    }
    tes3mp.SendMessage(pid, GROUP_PREFIX .. table.concat(fields, "\t") .. "\n", false)
end

local function sendPersonalMarker(pid, m)
    local fields = {
        "ADD", tostring(m.id or ""), m.paged and "1" or "0", esc(m.cell),
        tostring(tonumber(m.cellX) or 0), tostring(tonumber(m.cellY) or 0),
        tostring(tonumber(m.x) or 0), tostring(tonumber(m.y) or 0),
        esc(normalizeKind(m.kind)), esc(normalizeColor(m.color)), m.group and "1" or "0", esc(m.text)
    }
    tes3mp.SendMessage(pid, PERSONAL_PREFIX .. table.concat(fields, "\t") .. "\n", false)
end

function groupMapHelper.SyncPersonal(pid)
    if not valid(pid) then return end
    sendPersonalClear(pid)
    local d = personalData(pid)
    if not d then return end
    for _, marker in ipairs(d.mapMarkers) do
        sendPersonalMarker(pid, marker)
    end
end

function groupMapHelper.SyncGroup(pid)
    if not valid(pid) then return end
    sendGroupClear(pid)
    local gid = groupHelper.GetPlayerGroupId(pid)
    if not gid then return end
    local g = groupData(gid)
    if not g then return end
    for _, marker in ipairs(g.markers) do
        sendGroupMarker(pid, marker)
    end
end

function groupMapHelper.Sync(pid)
    groupMapHelper.SyncPersonal(pid)
    groupMapHelper.SyncGroup(pid)
end

function groupMapHelper.BroadcastGroup(groupId)
    if not groupId then return end
    for pid, player in pairs(Players) do
        if player and player:IsLoggedIn() and tostring(groupHelper.GetPlayerGroupId(pid) or "") == tostring(groupId) then
            groupMapHelper.SyncGroup(pid)
        end
    end
end

function groupMapHelper.DeleteGroup(groupId)
    groupId = tostring(groupId or "")
    if groupId ~= "" and data.groups[groupId] ~= nil then
        data.groups[groupId] = nil
        saveGroupData()
    end
end

local function sameLocation(m, paged, cell, x, y)
    return m ~= nil
        and (m.paged == true) == paged
        and tostring(m.cell or "") == tostring(cell or "")
        and math.abs((tonumber(m.x) or 0) - x) < 2
        and math.abs((tonumber(m.y) or 0) - y) < 2
end

local function personalCommand(pid, cmd)
    if not valid(pid) then return end
    local sub = tostring(cmd[2] or "sync"):lower()
    if sub == "sync" then groupMapHelper.SyncPersonal(pid); return end

    local d = personalData(pid)
    if not d then return end

    if sub == "add" then
        local paged = tostring(cmd[3] or "0") == "1"
        local cell = safeText(cmd[4], 128)
        local cellX, cellY = tonumber(cmd[5]) or 0, tonumber(cmd[6]) or 0
        local x, y = tonumber(cmd[7]), tonumber(cmd[8])
        local kind = normalizeKind(cmd[9])
        local color = normalizeColor(cmd[10])
        local share = tostring(cmd[11] or "0") == "1"
        local text = safeText(cmd[12], 160)
        if not x or not y or cell == "" then return end

        local existing = nil
        for _, marker in ipairs(d.mapMarkers) do
            if sameLocation(marker, paged, cell, x, y) then existing = marker; break end
        end
        if existing then
            existing.paged, existing.cell = paged, cell
            existing.cellX, existing.cellY = cellX, cellY
            existing.x, existing.y = x, y
            existing.kind, existing.color, existing.group, existing.text = kind, color, share, text
        else
            local id = tostring(d.nextMapMarkerId)
            d.nextMapMarkerId = d.nextMapMarkerId + 1
            table.insert(d.mapMarkers, {
                id=id, paged=paged, cell=cell, cellX=cellX, cellY=cellY,
                x=x, y=y, kind=kind, color=color, group=share, text=text
            })
        end
        while #d.mapMarkers > cfg.maxPersonal do table.remove(d.mapMarkers, 1) end
        savePlayer(pid)
        groupMapHelper.SyncPersonal(pid)
        return
    end

    if sub == "deleteat" then
        local paged = tostring(cmd[3] or "0") == "1"
        local cell = safeText(cmd[4], 128)
        local x, y = tonumber(cmd[5]), tonumber(cmd[6])
        if not x or not y then return end
        for i = #d.mapMarkers, 1, -1 do
            if sameLocation(d.mapMarkers[i], paged, cell, x, y) then
                table.remove(d.mapMarkers, i)
                savePlayer(pid)
                groupMapHelper.SyncPersonal(pid)
                return
            end
        end
    end
end

local function groupCommand(pid, cmd)
    if not valid(pid) then return end
    local gid = groupHelper.GetPlayerGroupId(pid)
    if not gid then
        local isRu = tostring(Players[pid].language or "EN"):upper() == "RU"
        tes3mp.SendMessage(pid, isRu and "Групповые метки карты доступны только в группе.\n"
            or "Group map markers require a group.\n", false)
        return
    end

    local sub = tostring(cmd[2] or "sync"):lower()
    if sub == "sync" then groupMapHelper.SyncGroup(pid); return end
    local g = groupData(gid)
    if not g then return end

    if sub == "add" then
        local paged = tostring(cmd[3] or "0") == "1"
        local cell = safeText(cmd[4], 128)
        local cellX, cellY = tonumber(cmd[5]) or 0, tonumber(cmd[6]) or 0
        local x, y = tonumber(cmd[7]), tonumber(cmd[8])
        local kind = normalizeKind(cmd[9])
        local color = normalizeColor(cmd[10])
        local text = safeText(cmd[11], 160)
        if not x or not y or cell == "" then return end

        local owner = accountName(pid)
        local existing = nil
        for _, marker in ipairs(g.markers) do
            if tostring(marker.owner or "") == owner and sameLocation(marker, paged, cell, x, y) then
                existing = marker
                break
            end
        end
        if existing then
            existing.paged, existing.cell = paged, cell
            existing.cellX, existing.cellY = cellX, cellY
            existing.x, existing.y = x, y
            existing.kind, existing.color, existing.text = kind, color, text
        else
            local id = tostring(g.nextId)
            g.nextId = g.nextId + 1
            table.insert(g.markers, {
                id=id, paged=paged, cell=cell, cellX=cellX, cellY=cellY,
                x=x, y=y, kind=kind, color=color, text=text, owner=owner
            })
        end
        while #g.markers > cfg.maxGroup do table.remove(g.markers, 1) end
        saveGroupData()
        groupMapHelper.BroadcastGroup(gid)
        return
    end

    if sub == "deleteat" then
        local paged = tostring(cmd[3] or "0") == "1"
        local cell = safeText(cmd[4], 128)
        local x, y = tonumber(cmd[5]), tonumber(cmd[6])
        if not x or not y then return end
        local owner = accountName(pid)
        local leader = groupHelper.IsLeader(pid)
        for i = #g.markers, 1, -1 do
            local marker = g.markers[i]
            if (leader or tostring(marker.owner or "") == owner) and sameLocation(marker, paged, cell, x, y) then
                table.remove(g.markers, i)
                saveGroupData()
                groupMapHelper.BroadcastGroup(gid)
                return
            end
        end
        return
    end

    if sub == "delete" then
        local id = tostring(cmd[3] or "")
        local owner = accountName(pid)
        local leader = groupHelper.IsLeader(pid)
        for i = #g.markers, 1, -1 do
            local marker = g.markers[i]
            if tostring(marker.id or "") == id and (leader or tostring(marker.owner or "") == owner) then
                table.remove(g.markers, i)
                saveGroupData()
                groupMapHelper.BroadcastGroup(gid)
                return
            end
        end
        return
    end

    if sub == "clear" and groupHelper.IsLeader(pid) then
        g.markers = {}
        saveGroupData()
        groupMapHelper.BroadcastGroup(gid)
    end
end

customCommandHooks.registerCommand("mapmark", personalCommand)
customCommandHooks.registerCommand("groupmark", groupCommand)
customCommandHooks.registerCommand("gmark", groupCommand)
customEventHooks.registerHandler("OnServerPostInit", function() load() end)
customEventHooks.registerHandler("OnPlayerAuthentified", function(eventStatus, pid)
    if eventStatus.validDefaultHandler then groupMapHelper.Sync(pid) end
end)

return groupMapHelper
