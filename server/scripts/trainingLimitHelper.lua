local trainingLimitHelper = {}

local CONTROL_PREFIX = "@@AMP_TRAIN@@"
local counts = {}

local function cfgLimit()
    if type(config.trainingLimit) == "table" then
        return math.max(1, math.floor(tonumber(config.trainingLimit["per npc per restart"]) or 3))
    end
    return 3
end

local function accountKey(player)
    return tostring(player.accountName or player.name or player.pid)
end

local function safeTrainerKey(value)
    if type(value) ~= "string" or value == "" or #value > 192 then return nil end
    if value:find("[\t\r\n]") then return nil end
    return value
end

local function getCount(player, trainerKey)
    local account = accountKey(player)
    counts[account] = counts[account] or {}
    return tonumber(counts[account][trainerKey]) or 0
end

local function setCount(player, trainerKey, value)
    local account = accountKey(player)
    counts[account] = counts[account] or {}
    counts[account][trainerKey] = math.max(0, math.floor(tonumber(value) or 0))
end

local function sendState(pid, player, trainerKey)
    local count = getCount(player, trainerKey)
    tes3mp.SendMessage(pid, CONTROL_PREFIX .. "STATE\t" .. trainerKey .. "\t" ..
        tostring(count) .. "\t" .. tostring(cfgLimit()) .. "\n", false)
end

local function parse(message)
    if type(message) ~= "string" or message:sub(1, #CONTROL_PREFIX) ~= CONTROL_PREFIX then return nil end
    local fields = {}
    local payload = message:sub(#CONTROL_PREFIX + 1)
    for part in string.gmatch(payload .. "\t", "([^\t]*)\t") do table.insert(fields, part) end
    return fields
end

local function validator(eventStatus, pid, message)
    local fields = parse(message)
    if fields == nil then return nil end

    local player = Players and Players[pid] or nil
    if player == nil or not player:IsLoggedIn() then
        return customEventHooks.makeEventStatus(false, false)
    end

    local action = fields[1] or ""
    local trainerKey = safeTrainerKey(fields[2])
    if trainerKey == nil then return customEventHooks.makeEventStatus(false, false) end

    if action == "QUERY" then
        sendState(pid, player, trainerKey)
    elseif action == "USE" then
        local count = getCount(player, trainerKey)
        local limit = cfgLimit()
        if count < limit then
            setCount(player, trainerKey, count + 1)

            -- Y092-SP: the patched client reports the trained skill only after a
            -- successful paid training action. Record a server-owned exemption so
            -- the general skill/SP budget does not mistake training for SP spend.
            local skillId = tonumber(fields[3])
            if skillId ~= nil and skillId >= 0 and math.floor(skillId) == skillId then
                local antiCheat = require("arenaAntiCheat")
                if antiCheat ~= nil and type(antiCheat.AuthorizeTraining) == "function" then
                    antiCheat.AuthorizeTraining(pid, skillId)
                end
            end
        end
        sendState(pid, player, trainerKey)
    end

    return customEventHooks.makeEventStatus(false, false)
end

customEventHooks.registerValidator("OnPlayerSendMessage", validator)

return trainingLimitHelper
