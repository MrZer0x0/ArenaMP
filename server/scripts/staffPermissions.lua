-- ArenaMP Y111: one place for staff privileges.
--
-- Ranks: 0 = player, 1 = moderator, 2 = admin, 3 = server owner.
-- The minimum rank of every privilege is read from config.staffPermissions on
-- each call, so a config hot-reload takes effect without a restart. A missing or
-- invalid value falls back to the default below. Values are clamped to 1..3:
-- a typo can never grant a staff privilege to ordinary players.
local M = {}

M.defaults = {
    furnitureAdmin = 1, -- add/edit/refresh the furniture catalogue
    freeFurniture = 1,  -- buy furniture without gold (stock is marked non-commercial)
    placeAnywhere = 1,  -- place and move furniture in any cell, not only in an owned home
    saveCell = 1,       -- /savecell, /clean, /clear and the other cell-reset staff tools
    questStudio = 1,    -- /quest Quest Studio and quest editing
    adminPanel = 1,     -- /admin and /panel
}

function M.Rank(pid)
    local player = Players and Players[pid]
    if player == nil or type(player.data) ~= "table" or type(player.data.settings) ~= "table" then
        return 0
    end
    return tonumber(player.data.settings.staffRank) or 0
end

function M.Required(key)
    local configured = type(config) == "table" and type(config.staffPermissions) == "table"
        and tonumber(config.staffPermissions[key]) or nil
    local value = configured or M.defaults[key] or 3
    value = math.floor(value)
    if value < 1 then value = 1 end
    if value > 3 then value = 3 end
    return value
end

function M.Has(pid, key)
    local player = Players and Players[pid]
    if player == nil or not player:IsLoggedIn() then return false end
    return M.Rank(pid) >= M.Required(key)
end

return M
