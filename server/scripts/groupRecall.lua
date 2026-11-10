-- Shared Recall: destinations come only from saved server profiles, never the
-- request. The client opens this after a successful Recall effect and consumes
-- exactly one matching GO response without changing its own Mark.
local recall = {}
local PREFIX = "@@AMP_RECALL@@"
local pending, lastOpen = {}, {}
local serial = 0
local lifetime = 40
local fields = {"cell", "posX", "posY", "posZ", "rotX", "rotZ"}

local function valid(pid)
    return Players[pid] and Players[pid]:IsLoggedIn() and type(Players[pid].data) == "table"
end
local function alive(pid)
    return valid(pid) and (tonumber((Players[pid].data.stats or {}).healthCurrent) or 0) > 0
        and not (bountyRespawn and bountyRespawn.Pending(Players[pid]))
end
local function account(pid)
    return valid(pid) and tostring(Players[pid].accountName or Players[pid].name or "") or ""
end
local function tr(pid, ru, en)
    return tostring(Players[pid] and Players[pid].language or "EN"):upper() == "RU" and ru or en
end
local function escape(value)
    -- gsub returns both the string and replacement count. Return only the
    -- string: this helper is the final expression in the GO table constructor.
    -- Otherwise Lua appends that count as a ninth wire field and the client
    -- rejects the whole teleport response.
    return (tostring(value):gsub("\\", "\\\\"):gsub("\t", "\\t"):gsub("\r", "\\r"):gsub("\n", "\\n"))
end
local function label(value)
    return tostring(value):gsub("#%x%x%x%x%x%x", ""):gsub("[%c#]", " ")
end
local function control(pid, text)
    tes3mp.SendMessage(pid, PREFIX .. text .. "\n", false)
end
local function cancel(pid, state, message)
    pending[pid] = nil
    if valid(pid) then
        control(pid, "CANCEL\t" .. state.request)
        if message then tes3mp.MessageBox(pid, -1, message) end
    end
end
local function point(pid, kind)
    if not valid(pid) then return nil end
    local data = Players[pid].data
    local value
    if kind == "mark" then
        value = (data.miscellaneous or {}).markLocation
    else
        local profile = data.profile or {}
        if not profile.returnUnlocked then return nil end
        value = (profile.returnSlots or {})[kind]
    end
    if type(value) ~= "table" or type(value.cell) ~= "string" or value.cell == ""
        or #value.cell > 1024 or value.cell:find("[%c]") then return nil end
    local result = {cell = value.cell}
    for i = 2, #fields do
        local key = fields[i]
        local n = tonumber(value[key])
        if not n or n ~= n or math.abs(n) == math.huge then return nil end
        result[key] = n
    end
    return result
end
local function samePoint(a, b)
    if not a or not b then return false end
    for _, key in ipairs(fields) do if a[key] ~= b[key] then return false end end
    return true
end
local function accessible(pid, cell, prepare)
    if not houseHelper then return true end
    local base = houseHelper.GetBaseCellForInstance(cell)
    local property = houseHelper.GetProperty(base or cell)
    -- Public/unowned authored interiors retain ordinary Recall semantics;
    -- private instances and locked owned houses still require house access.
    if property and property.owner and property.owner ~= ""
        and houseHelper.GetAccessLevel(pid, property) == "none" then return false end
    if base then
        if not property or not property.owner or property.owner == "" then return false end
        if prepare and not houseHelper.EnsureInstanceRecord(pid, cell, base) then return false end
    end
    return true
end
local function sendChoice(pid, state, choice)
    if not alive(pid) or os.time() > state.expires or account(pid) ~= state.account
        or tes3mp.GetCell(pid) ~= state.origin then
        cancel(pid, state, tr(pid, "Возврат отменён. Примените заклинание снова.", "Recall cancelled. Cast again."))
        return
    end
    local owner = choice.pid
    if not valid(owner) or account(owner) ~= choice.account
        or (owner ~= pid and (groupHelper.GetPlayerGroupId(pid) ~= state.group
            or not groupHelper.ArePlayersInSameGroup(pid, owner))) then
        cancel(pid, state, tr(pid, "Владелец метки вышел из игры или группы.", "The mark owner went offline or left the group."))
        return
    end
    local destination = point(owner, choice.kind)
    if not samePoint(destination, choice.point) then
        cancel(pid, state, tr(pid, "Метка изменилась. Примените Возврат снова.", "The mark changed. Cast Recall again."))
        return
    end
    if not accessible(pid, destination.cell, true) then
        cancel(pid, state, tr(pid, "Нет доступа к этой недвижимости.", "You cannot enter this property."))
        return
    end
    if choice.kind ~= "mark" then
        -- Shared A/B/C slots use the caster's existing return cooldown.
        local data = Players[pid].data
        data.profile = data.profile or {}
        local cooldown = math.max(0, tonumber((config.profileSystem or {})["return cooldown seconds"]) or 20)
        local last = tonumber(data.profile.lastReturn) or 0
        if os.time() < last + cooldown then
            cancel(pid, state, tr(pid, "Точки A/B/C ещё перезаряжаются.", "Return slots A/B/C are still on cooldown."))
            return
        end
        data.profile.lastReturn = os.time()
        if Players[pid].QuicksaveToDrive then Players[pid]:QuicksaveToDrive() end
    end
    pending[pid] = nil -- Consume before sending; repeat GUI replies cannot teleport again.
    local output = {"GO", state.request, escape(destination.cell)}
    for i = 2, #fields do output[#output + 1] = tostring(destination[fields[i]]) end
    control(pid, table.concat(output, "\t"))
end
function recall.Open(pid, request)
    if not alive(pid) or type(request) ~= "string" or not request:match("^%d+$") or #request > 10 then return end
    local now = os.time()
    if lastOpen[pid] == now then
        control(pid, "CANCEL\t" .. request)
        return
    end
    lastOpen[pid] = now
    local state = {request=request, account=account(pid), group=groupHelper.GetPlayerGroupId(pid),
        origin=tes3mp.GetCell(pid), expires=now+lifetime, choices={}}
    pending[pid] = state
    local owners = {pid}
    for _, member in ipairs(groupHelper.GetGroupMembers(pid, false)) do
        if member ~= pid then owners[#owners + 1] = member end
    end
    local lines = {}
    for _, owner in ipairs(owners) do
        for _, kind in ipairs({"mark", "A", "B", "C"}) do
            local destination = point(owner, kind)
            if destination and accessible(pid, destination.cell, false) then
                state.choices[#state.choices + 1] = {pid=owner, account=account(owner), kind=kind, point=destination}
                local who = owner == pid and tr(pid, "Вы", "You") or label(Players[owner].name or account(owner))
                local what = kind == "mark" and tr(pid, "Пометка", "Mark") or tr(pid, "Точка ", "Slot ") .. kind
                lines[#lines + 1] = who .. " | " .. what .. " | " .. label(destination.cell)
            end
        end
    end
    if #lines == 0 then
        cancel(pid, state, tr(pid, "Нет доступных меток. Установите Пометку или точку A/B/C.",
            "No available marks. Set a Mark or a return slot A/B/C."))
        return
    end
    -- One destination needs no modal (normal solo Recall stays immediate).
    if #lines == 1 then sendChoice(pid, state, state.choices[1]); return end
    -- CoreArenaMPSecurity accepts GUI replies only in [0, 1000000].
    -- Reserve 800001..900000, away from the furniture/market IDs 9234xx.
    -- Keep a distinct ID per menu so a stale reply cannot select a new mark.
    serial = serial % 100000 + 1
    state.gui = 800000 + serial
    lines[#lines + 1] = tr(pid, "Отмена", "Cancel")
    tes3mp.ListBox(pid, state.gui, tr(pid,
        "Возврат: выберите метку. Доступны точки сопартийцев онлайн.",
        "Recall: choose a mark. Online party members' points are available."), table.concat(lines, "\n"))
end
customEventHooks.registerValidator("OnPlayerSendMessage", function(eventStatus, pid, message)
    if type(message) ~= "string" or message:sub(1, #PREFIX) ~= PREFIX then return end
    local request = message:match("^@@AMP_RECALL@@OPEN\t(%d+)$")
    if request and eventStatus.validDefaultHandler then recall.Open(pid, request) end
    return customEventHooks.makeEventStatus(false, false)
end)
customEventHooks.registerValidator("OnGUIAction", function(eventStatus, pid, id, value)
    local state = pending[pid]
    if not state or id ~= state.gui then return end
    if not eventStatus.validDefaultHandler then cancel(pid, state); return end
    local n = tonumber(value)
    local choice = n and n == math.floor(n) and state.choices[n + 1]
    if not choice then cancel(pid, state)
    else sendChoice(pid, state, choice) end
    return customEventHooks.makeEventStatus(false, false)
end)
local function ready(eventStatus, pid)
    pending[pid], lastOpen[pid] = nil, nil
    if valid(pid) then control(pid, "READY\t1") end
end
customEventHooks.registerHandler("OnPlayerAuthentified", ready)
customEventHooks.registerHandler("OnPlayerEndCharGen", ready)
customEventHooks.registerHandler("OnPlayerDisconnect", function(eventStatus, pid)
    pending[pid], lastOpen[pid] = nil, nil
end)
customEventHooks.registerHandler("OnPlayerDeath", function(eventStatus, pid)
    if pending[pid] then cancel(pid, pending[pid]) end
end)
return recall
