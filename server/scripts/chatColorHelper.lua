-- ArenaMP X054: chat state bridge between the Player Menu and coreChat.
--
-- X052 shipped this file with a private 16 colour palette stored in
-- customVariables.arenampChatColor. Nothing ever displayed that value: coreChat
-- registers an OnPlayerSendMessage validator that consumes every non-slash
-- message, so eventHandler's getArenaNameColor never ran and the colour the
-- menu wrote was simply invisible. /color meanwhile writes
-- customVariables.chatColor, and that is what coreChat and /list actually paint
-- with.
--
-- X054 deletes the second palette. This file is now a thin transport: it
-- publishes coreChat's own 40 colour palette (with coreChat's localized names)
-- to the client and applies a pick through coreChat.SetColorIndex. /chatcolor
-- and /color therefore change the very same variable, so a colour chosen in the
-- menu is live in chat, in /list and in the group roster immediately.
--
-- It also reports RP status and the active speech channel so the menu's RP/OOC
-- and channel buttons show real server state instead of a client-side guess.

local chatColorHelper = {}

local STATE_PREFIX = "@@AMP_COLOR@@"

local function isValidPid(pid)
    return Players[pid] ~= nil and Players[pid]:IsLoggedIn() and Players[pid].data ~= nil
end

local function enabled()
    return type(config.chatNameColors) ~= "table" or config.chatNameColors.enabled ~= false
end

-- coreChat is required after this file in serverCore, so it is resolved lazily
-- instead of being captured at load time.
local function core()
    return type(coreChat) == "table" and coreChat or nil
end

local function palette()
    local c = core()
    if c == nil or type(c.playerColors) ~= "table" then return {} end
    return c.playerColors
end

local function escapeField(value)
    value = tostring(value or "")
    value = value:gsub("\\", "\\\\")
    value = value:gsub("\t", "\\t")
    value = value:gsub("\r", "")
    value = value:gsub("\n", "\\n")
    -- ';' and '^' are record separators inside the palette field.
    value = value:gsub("[;%^]", " ")
    return value
end

function chatColorHelper.GetIndex(pid)
    local c = core()
    if c == nil or not isValidPid(pid) then return nil end
    return c.GetColorIndex(pid)
end

-- The colour code to prefix a player's name with. Always safe to concatenate.
function chatColorHelper.GetColor(pid)
    if not enabled() or not isValidPid(pid) then return color.White end
    local c = core()
    if c == nil then return color.White end
    return c.GetPlayerColor(pid) or color.White
end

-- coreChat already hands every player a random colour on first use, so X052's
-- separate first-login assignment is gone. Kept as an entry point for anything
-- that still calls it.
function chatColorHelper.EnsureColor(pid)
    if not enabled() or not isValidPid(pid) then return end
    local c = core()
    if c ~= nil then c.GetPlayerColor(pid) end
end

function chatColorHelper.SetIndex(pid, index, quiet)
    local c = core()
    if c == nil then return false, "coreChat is not loaded" end
    if not isValidPid(pid) then return false, "Player is not logged in" end

    if not c.SetColorIndex(pid, index) then
        return false, "Colour index must be between 1 and " .. tostring(#palette())
    end

    if not quiet then chatColorHelper.SendState(pid) end
    return true
end

function chatColorHelper.SendState(pid)
    if not isValidPid(pid) then return end
    local c = core()
    if c == nil then return end

    local index = c.GetColorIndex(pid)
    local entries = {}
    for i, hex in ipairs(palette()) do
        -- hex ^ localized name. The name travels from the server, so RU and EN
        -- players see their own language for the same 40 slots /color offers.
        entries[#entries + 1] = escapeField(hex) .. "^" .. escapeField(c.GetColorName(pid, i))
    end

    local fields = {
        "STATE",
        index ~= nil and tostring(index) or "0",
        table.concat(entries, ";"),
        c.IsRP(pid) and "1" or "0",
        tostring(c.GetChannel(pid)),
        c.IsPopupMode(pid) and "1" or "0"
    }
    tes3mp.SendMessage(pid, STATE_PREFIX .. table.concat(fields, "\t") .. "\n", false)
end

-- X054: no chat notifications at all. The Player Menu redraws itself from the
-- state message and /color shows its own MessageBox, so the old
-- "[Chat] Nickname colour: ..." lines were pure noise in the history.
local function processCommand(pid, cmd)
    if not isValidPid(pid) then return end

    local sub = tostring(cmd[2] or "state"):lower()
    if sub == "state" or sub == "" then
        chatColorHelper.SendState(pid)
        return
    end

    if sub == "menu" then
        local c = core()
        if c ~= nil then c.ShowColorMenu(pid) end
        return
    end

    chatColorHelper.SetIndex(pid, sub, true)
    chatColorHelper.SendState(pid)
end

customCommandHooks.registerCommand("chatcolor", processCommand)
customCommandHooks.registerCommand("nickcolor", processCommand)

customEventHooks.registerHandler("OnPlayerAuthentified", function(eventStatus, pid)
    if isValidPid(pid) then
        chatColorHelper.EnsureColor(pid)
        chatColorHelper.SendState(pid)
    end
end)

return chatColorHelper
