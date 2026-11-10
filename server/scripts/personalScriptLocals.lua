-- ArenaMP H014: opt-in, per-character script locals for EXACT world references.
-- NEVER turn every object-local into a personal variable: mechanisms and timers
-- have shared world state. Only explicit config.personalScriptLocalRules entries
-- are isolated per character.
local M = {}
local observations, observationCount = {}, 0
local rejected, rejectedCount = {}, 0

local function norm(value)
    return type(value) == "string" and string.lower(value) or ""
end

local function rules()
    return type(config) == "table" and type(config.personalScriptLocalRules) == "table"
        and config.personalScriptLocalRules or {}
end

local function getRule(cell, uniqueIndex)
    if type(cell) ~= "string" or type(uniqueIndex) ~= "string" then return nil end
    for _, rule in pairs(rules()) do
        if type(rule) == "table" and norm(rule.cell) == norm(cell) and
            tostring(rule.uniqueIndex) == uniqueIndex and type(rule.scriptId) == "string" and
            type(rule.refId) == "string" and rule.scriptId ~= "" and rule.refId ~= "" then
            return rule
        end
    end
    return nil
end

function M.IsPersonalTarget(cell, uniqueIndex, object)
    local rule = getRule(cell, tostring(uniqueIndex))
    if not rule then return false end
    if object == nil then return true end
    return type(object) == "table" and norm(object.clientScriptId) == norm(rule.scriptId)
        and norm(object.refId) == norm(rule.refId)
end

local function log(level, message)
    if tes3mp and tes3mp.LogMessage then
        tes3mp.LogMessage(level, "[ArenaMP PersonalScripts] " .. message)
    end
end

-- One line per (script, cell), not per frame or per variable: diagnostics are
-- useful when finding scripts to review without flooding logs with timers.
function M.Observe(pid, cell, objects)
    if config.personalScriptLocalsDiagnostics ~= true or type(objects) ~= "table" then return end
    for uniqueIndex, object in pairs(objects) do
        if type(object) == "table" and type(object.clientScriptId) == "string" and
            object.clientScriptId ~= "" then
            local key = norm(cell) .. "|" .. norm(object.clientScriptId)
            if not observations[key] and observationCount < 128 then
                observations[key] = true
                observationCount = observationCount + 1
                log(enumerations.log.INFO, "observed " .. object.clientScriptId ..
                    " in " .. tostring(cell) .. "; " ..
                    (M.IsPersonalTarget(cell, tostring(uniqueIndex), object) and "personal candidate" or "shared/unchecked") ..
                    "; inspect before adding a new allowlist rule")
            end
        end
    end
end

local function copyAndValidate(rule, variables)
    if type(variables) ~= "table" then return nil end
    local result, count = {}, 0
    for kind, indices in pairs(variables) do
        local kindNum = tonumber(kind)
        if kindNum ~= enumerations.variableType.SHORT and
            kindNum ~= enumerations.variableType.LONG and
            kindNum ~= enumerations.variableType.FLOAT then return nil end
        if type(indices) ~= "table" then return nil end
        for index, value in pairs(indices) do
            local idx = tonumber(index)
            if idx == nil or idx ~= math.floor(idx) or idx < 0 or idx > 127 or
                type(value) ~= "number" or value ~= value or value == math.huge or
                value == -math.huge then return nil end
            if kindNum == enumerations.variableType.SHORT and
                (value ~= math.floor(value) or value < -32768 or value > 32767) then return nil end
            if kindNum == enumerations.variableType.LONG and
                (value ~= math.floor(value) or value < -2147483648 or value > 2147483647) then return nil end
            if kindNum == enumerations.variableType.FLOAT and math.abs(value) > 1e12 then return nil end
            count = count + 1
            if count > 128 then return nil end
            result[kindNum] = result[kindNum] or {}
            result[kindNum][idx] = value
        end
    end
    if count == 0 then return nil end
    return result
end

local function equals(a, b)
    if type(a) ~= "table" or type(b) ~= "table" then return false end
    for k, v in pairs(a) do
        if type(v) ~= "table" or type(b[k]) ~= "table" then return false end
        for i, val in pairs(v) do
            if b[k][i] ~= val and b[k][tostring(i)] ~= val then return false end
        end
    end
    for k, v in pairs(b) do
        if type(v) ~= "table" or type(a[tonumber(k) or k]) ~= "table" then return false end
        for i, val in pairs(v) do
            local group = a[tonumber(k) or k]
            if group[i] ~= val and group[tonumber(i) or i] ~= val then return false end
        end
    end
    return true
end

-- Removes only allowlisted references from the SHARED path. Even malformed or
-- forged packets targeting a personal reference cannot pollute shared cell data.
-- No rule => the existing TES3MP authority-controlled behavior is unchanged.
function M.Capture(pid, cell, objects, origin)
    if type(objects) ~= "table" then return end
    local player = Players and Players[pid]
    local dirty = false
    for uniqueIndex, object in pairs(objects) do
        local rule = getRule(cell, tostring(uniqueIndex))
        if rule then
            objects[uniqueIndex] = nil
            if player and type(player.data) == "table" and
                origin == enumerations.packetOrigin.CLIENT_SCRIPT_LOCAL and
                type(object) == "table" and
                norm(object.clientScriptId) == norm(rule.scriptId) and
                norm(object.refId) == norm(rule.refId) then
                local clean = copyAndValidate(rule, object.variables)
                if clean then
                    player.data.personalScriptLocals = player.data.personalScriptLocals or {}
                    local byCell = player.data.personalScriptLocals[cell]
                    if type(byCell) ~= "table" then
                        byCell = {}
                        player.data.personalScriptLocals[cell] = byCell
                    end
                    local previous = byCell[uniqueIndex]
                    if type(previous) ~= "table" or
                        norm(previous.scriptId) ~= norm(rule.scriptId) or
                        norm(previous.refId) ~= norm(rule.refId) or
                        not equals(previous.variables, clean) then
                        byCell[uniqueIndex] = {scriptId = rule.scriptId,
                            refId = rule.refId, variables = clean}
                        dirty = true
                    end
                else
                    local key = "bad|" .. tostring(pid) .. "|" .. norm(cell) .. "|" .. norm(rule.scriptId)
                    if not rejected[key] and rejectedCount < 64 then
                        rejected[key] = true
                        rejectedCount = rejectedCount + 1
                        log(enumerations.log.WARN, "invalid variable data rejected for " .. rule.scriptId)
                    end
                end
            else
                -- Ordinary clients may report done=0 every time they first see
                -- the door. This is expected and must not flood server logs.
                local key = "unauthorized|" .. tostring(pid) .. "|" .. norm(cell) .. "|" .. norm(rule.scriptId)
                if not rejected[key] and rejectedCount < 64 and
                    config.personalScriptLocalsDiagnostics == true then
                    rejected[key] = true
                    rejectedCount = rejectedCount + 1
                    log(enumerations.log.WARN, "unauthorized personal locals rejected for " ..
                        rule.scriptId .. " in " .. tostring(cell))
                end
            end
        end
    end
    if dirty and type(player.QuicksaveToDrive) == "function" then
        player:QuicksaveToDrive()
    end
end

-- Called AFTER the canonical cell state is sent. Never broadcasts a personal
-- packet and never mutates LoadedCells or WorldInstance.
function M.Restore(pid, cell)
    local player = Players and Players[pid]
    if not player or not player.data or type(player.data.personalScriptLocals) ~= "table" then
        return 0
    end
    local byCell = player.data.personalScriptLocals[cell]
    if type(byCell) ~= "table" then return 0 end
    local pending = {}
    for uniqueIndex, stored in pairs(byCell) do
        local rule = getRule(cell, tostring(uniqueIndex))
        if rule and type(stored) == "table" and
            norm(stored.scriptId) == norm(rule.scriptId) and
            norm(stored.refId) == norm(rule.refId) then
            local clean = copyAndValidate(rule, stored.variables)
            if clean then
                pending[#pending + 1] = {uniqueIndex = uniqueIndex,
                    refId = rule.refId, clientScriptId = rule.scriptId, variables = clean}
            end
        end
    end
    if #pending == 0 then return 0 end
    tes3mp.ClearObjectList()
    tes3mp.SetObjectListPid(pid)
    tes3mp.SetObjectListCell(cell)
    for _, item in ipairs(pending) do
        packetBuilder.AddClientScriptLocal(item.uniqueIndex, item)
    end
    tes3mp.SendClientScriptLocal(false, false)
    return #pending
end

return M
