-- Death notices are visible only to nearby players in the exact same cell/instance.
local deathNotifications = {}
local function plain(value)
    return tostring(value or ""):gsub('[\r\n\t]', ' '):gsub('#%x%x%x%x%x%x', '')
end
function deathNotifications.Notify(pid)
    local victim = Players[pid]
    if not victim then return end
    local cell = tes3mp.GetCell(pid)
    local radius = math.max(0, tonumber(config.deathNotificationDistance) or
        (coreChat and coreChat.config and tonumber(coreChat.config.speakDistance)) or 2500)
    local x, y, z = tes3mp.GetPosX(pid), tes3mp.GetPosY(pid), tes3mp.GetPosZ(pid)
    local name = plain(victim.accountName or tes3mp.GetName(pid))
    local killer, suicide
    if tes3mp.DoesPlayerHavePlayerKiller(pid) then
        local killerPid = tes3mp.GetPlayerKillerPid(pid)
        suicide = killerPid == pid
        if not suicide and Players[killerPid] then killer = plain(Players[killerPid].accountName) end
    else
        local actorName = tes3mp.GetPlayerKillerName(pid)
        if actorName and actorName ~= "" then killer = plain(actorName) end
    end
    for receiverPid, player in pairs(Players) do
        if player:IsLoggedIn() and tes3mp.GetCell(receiverPid) == cell then
            local dx, dy, dz = tes3mp.GetPosX(receiverPid)-x, tes3mp.GetPosY(receiverPid)-y, tes3mp.GetPosZ(receiverPid)-z
            if dx*dx + dy*dy + dz*dz <= radius*radius then
                local ru = tostring(player.language or "EN"):upper() == "RU"
                local message
                if suicide then message = name .. (ru and " совершил самоубийство." or " committed suicide.")
                elseif killer then message = name .. (ru and " погиб. Убийца: " or " was killed by ") .. killer .. "."
                else message = name .. (ru and " погиб." or " died.") end
                tes3mp.MessageBox(receiverPid, -1, message)
            end
        end
    end
end
return deathNotifications
