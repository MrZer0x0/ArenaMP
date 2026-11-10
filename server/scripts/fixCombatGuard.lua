-- Transient combat state only: never persist active fights in character/cell JSON.
local guard = {}
local actors = {}
local recentHits = setmetatable({}, {__mode = 'k'})
local function clock()
    return tes3mp.GetMillisecondsSinceServerStart() / 1000
end
function guard.observeAI(updates)
    for id, actor in pairs(updates or {}) do
        local ai = actor.ai
        if ai then
            if ai.action == 2 and ai.targetPid ~= nil and Players[ai.targetPid] then
                actors[id] = {player = Players[ai.targetPid], expires = clock() + 15}
            else
                actors[id] = nil
            end
        end
    end
end
function guard.actorDied(id) actors[id] = nil end
function guard.hit(pid)
    if Players[pid] then recentHits[Players[pid]] = clock() + 10 end
end
function guard.blocked(pid)
    local player = Players[pid]
    if not player then return true end
    if player.deathRecoveryActive then return true end
    local now = clock()
    if (recentHits[player] or 0) > now then return true end
    for id, state in pairs(actors) do
        if state.expires <= now then actors[id] = nil
        elseif state.player == player then return true end
    end
    return false
end
return guard
