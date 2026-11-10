-- H036: use the configured data root and the Windows Unicode-aware IO layer.
local M = {}
local serverStartedAt = os.time()
local lastErrorAt = 0
function M.write()
    local count = 0
    for _, player in pairs(Players or {}) do
        -- Player login state is exposed as a method. The old field check
        -- (player.isLoggedIn) was always nil, so the master list was stuck at 0.
        if player and player.IsLoggedIn and player:IsLoggedIn() then
            count = count + 1
        end
    end

    -- Publish the values used by the running RakNet server itself. This keeps
    -- the directory capacity/port accurate even when the launcher-side cfg
    -- path is relocated or a portable userdata file is used.
    local maxPlayers = tonumber(tes3mp.GetMaxPlayers()) or 100
    local port = tonumber(tes3mp.GetPort()) or 25565
    local passwordRequired = tes3mp.HasPassword() and true or false

    local ok, result = pcall(function()
        return jsonInterface.writeToFile("custom/arenaMasterStatus.json",
            string.format('{"players":%d,"max_players":%d,"port":%d,"password_required":%s,"timestamp":%d,"started_at":%d}',
                count, maxPlayers, port, tostring(passwordRequired), os.time(), serverStartedAt))
    end)
    if not ok or result ~= true then
        if os.time() - lastErrorAt >= 60 then
            lastErrorAt = os.time()
            tes3mp.LogMessage(enumerations.log.WARN, '[ArenaMaster] Cannot write ' ..
                tostring(config.dataPath) .. '/custom/arenaMasterStatus.json: ' .. tostring(result))
        end
        return false
    end
    return true
end
return M
