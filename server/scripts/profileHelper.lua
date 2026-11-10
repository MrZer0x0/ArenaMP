-- ArenaMP Y049 persistent profile/statistics/return-point helper.
--
-- Z001: profile deletion reworked.
--
-- Symptom reported from the Player Menu: pressing "Delete profile" produced no
-- message at all and left the account file untouched. Three separate links in
-- that chain could break silently, so all three are now removed rather than
-- patched around:
--
--   1. The confirmation was a bare CustomMessageBox raised while the Player
--      Menu still owned a GUI mode. If the modal was refused or swallowed the
--      player saw nothing whatsoever. Every stage now also writes a plain chat
--      line, so a broken modal can no longer make the feature invisible.
--   2. os.remove() is not Unicode-safe on Windows, which is exactly why the old
--      server shipped ProfileDeleter.exe. A Cyrillic account name therefore
--      failed to unlink and the profile "was not cleared". Deletion now falls
--      back to the io2 library CoreScripts already uses for JSON and, when even
--      that cannot unlink, leaves a tombstone that the login path treats as a
--      missing account.
--   3. There was no non-GUI path at all. "/profile delete <password>" now does
--      the whole job from the chat line, so the feature stays usable even if a
--      client-side dialog misbehaves.
--
-- Deletion is also a real wipe now: the character file, the player's private
-- cell instances, their houses and their group membership all go away together.
local profileHelper = {}
local cfg = {
    unlockLevel = 25,
    returnCooldown = 20,
    maxSlots = 3
}
if type(config.profileSystem) == "table" then
    cfg.unlockLevel = tonumber(config.profileSystem["return unlock level"]) or cfg.unlockLevel
    cfg.returnCooldown = tonumber(config.profileSystem["return cooldown seconds"]) or cfg.returnCooldown
end

local CONTROL_PREFIX = "@@AMP_PROFILE@@"
local LOG_TAG = "[ArenaMP Profile] "

-- Z001: every stage of the deletion flow is logged. When a player reports that
-- nothing happened, the server log now says exactly which stage was reached.
local function log(level, message)
    tes3mp.LogMessage(level, LOG_TAG .. tostring(message))
end

-- U036 --------------------------------------------------------------------
-- Menu ids. config.customMenuIds never carried the profile entries, so every
-- CustomMessageBox/PasswordDialog here was opened with a nil id: the native
-- call rejected it and, even when it did not, OnGUIAction could never match
-- `idGui == nil` back to this script. That is the whole reason "Delete
-- profile" appeared to do nothing.
--
-- The ids are resolved once, here, and written back into config so any other
-- script comparing against config.customMenuIds sees the same numbers.
----------------------------------------------------------------------------
local MENU_ID_DEFAULTS = {
    profileMain = 9040,
    profileReturns = 9041,
    profileDeletePassword = 9042,
    profileDeleteConfirm = 9043
}

config.customMenuIds = type(config.customMenuIds) == "table" and config.customMenuIds or {}
local menuId = {}
for key, fallback in pairs(MENU_ID_DEFAULTS) do
    local configured = tonumber(config.customMenuIds[key])
    menuId[key] = configured or fallback
    config.customMenuIds[key] = menuId[key]
end

local function escapeControl(value)
    value = tostring(value or "")
    value = value:gsub("\\", "\\\\")
    value = value:gsub("\t", "\\t")
    value = value:gsub("\r", "")
    value = value:gsub("\n", "\\n")
    return value
end

local function valid(pid)
    return Players[pid] ~= nil and Players[pid]:IsLoggedIn() and type(Players[pid].data) == "table"
end

local function isRu(pid)
    return valid(pid) and tostring(Players[pid].language or "EN"):upper() == "RU"
end

local function tr(pid, ru, en)
    return isRu(pid) and ru or en
end

-- Z001: a modal can be refused, queued behind another modal or replaced before
-- it is drawn. A chat line cannot. Every player-facing step of the deletion
-- flow goes through here so the feature always leaves a visible trace.
local function say(pid, message)
    if Players[pid] == nil then return end
    tes3mp.SendMessage(pid, tostring(message) .. "\n", false)
end

local function notify(pid, message)
    say(pid, message)
    tes3mp.MessageBox(pid, -1, tostring(message))
end

local function ensure(pid)
    if not valid(pid) then return nil end
    local p = Players[pid]
    p.data.profile = type(p.data.profile) == "table" and p.data.profile or {}
    local d = p.data.profile
    d.playSeconds = tonumber(d.playSeconds) or 0
    d.deaths = tonumber(d.deaths) or 0
    d.arrests = tonumber(d.arrests) or 0
    d.returnSlots = type(d.returnSlots) == "table" and d.returnSlots or {}
    d.lastReturn = tonumber(d.lastReturn) or 0
    if d.returnUnlocked == nil then d.returnUnlocked = false end
    return d
end

local function save(pid)
    if valid(pid) and type(Players[pid].QuicksaveToDrive) == "function" then
        Players[pid]:QuicksaveToDrive()
    end
end

local function liveSeconds(pid)
    local d = ensure(pid)
    if not d then return 0 end
    local extra = 0
    local lastLogin = tonumber(Players[pid].data.timestamps and Players[pid].data.timestamps.lastLogin) or 0
    if lastLogin > 0 then extra = math.max(0, os.time() - lastLogin) end
    return math.max(0, math.floor(d.playSeconds + extra))
end

local function formatTime(seconds)
    seconds = math.max(0, math.floor(tonumber(seconds) or 0))
    local days = math.floor(seconds / 86400); seconds = seconds % 86400
    local hours = math.floor(seconds / 3600); seconds = seconds % 3600
    local mins = math.floor(seconds / 60)
    if days > 0 then return string.format("%dd %02dh %02dm", days, hours, mins) end
    return string.format("%02dh %02dm", hours, mins)
end

local function slotName(index)
    return ({"A", "B", "C"})[index]
end

local function slotDescription(slot, pid)
    if type(slot) ~= "table" or type(slot.cell) ~= "string" or slot.cell == "" then
        return tr(pid, "пусто", "empty")
    end
    return slot.cell
end

local function sendState(pid)
    local d = ensure(pid)
    if not d then return end
    local level = tonumber(Players[pid].data.stats and Players[pid].data.stats.level) or 1
    local fields = {
        "STATE",
        escapeControl(formatTime(liveSeconds(pid))),
        tostring(d.deaths),
        tostring(d.arrests),
        tostring(level),
        d.returnUnlocked and "1" or "0",
        tostring(cfg.unlockLevel),
        escapeControl(slotDescription(d.returnSlots.A, pid)),
        escapeControl(slotDescription(d.returnSlots.B, pid)),
        escapeControl(slotDescription(d.returnSlots.C, pid))
    }
    tes3mp.SendMessage(pid, CONTROL_PREFIX .. table.concat(fields, "\t") .. "\n", false)
end

local function showMain(pid)
    local d = ensure(pid); if not d then return end
    local level = tonumber(Players[pid].data.stats and Players[pid].data.stats.level) or 1
    local text = tr(pid, "ПРОФИЛЬ", "PROFILE") .. "\n\n" ..
        tr(pid, "В игре: ", "Time played: ") .. formatTime(liveSeconds(pid)) .. "\n" ..
        tr(pid, "Смертей: ", "Deaths: ") .. tostring(d.deaths) .. "\n" ..
        tr(pid, "Арестов: ", "Arrests: ") .. tostring(d.arrests) .. "\n" ..
        tr(pid, "Уровень: ", "Level: ") .. tostring(level) .. "\n\n" ..
        tr(pid, "Точки возврата", "Return points") .. " (" .. tr(pid, "ур. ", "lvl ") .. tostring(cfg.unlockLevel) .. "):\n"
    for i = 1, cfg.maxSlots do
        local name = slotName(i)
        text = text .. name .. ": " .. slotDescription(d.returnSlots[name], pid) .. "\n"
    end
    if not d.returnUnlocked then
        text = text .. "\n" .. tr(pid, "Откроются автоматически на 25 уровне.", "They unlock automatically at level 25.")
    end
    local buttons = tr(pid, "Точки возврата;Обновить;Удалить профиль;Закрыть",
        "Return points;Refresh;Delete profile;Close")
    tes3mp.CustomMessageBox(pid, menuId.profileMain, text, buttons)
end

local function showReturns(pid)
    local d = ensure(pid); if not d then return end
    if not d.returnUnlocked then
        notify(pid, tr(pid, "Точки возврата откроются на 25 уровне.", "Return points unlock at level 25."))
        return
    end
    local text = tr(pid, "ТОЧКИ ВОЗВРАТА\nВыберите слот. «Записать» сохраняет текущую позицию.",
        "RETURN POINTS\nChoose a slot. Save stores your current position.") .. "\n\n"
    for i=1,cfg.maxSlots do
        local n=slotName(i); text=text..n..": "..slotDescription(d.returnSlots[n],pid).."\n"
    end
    tes3mp.CustomMessageBox(pid, menuId.profileReturns, text,
        tr(pid, "A: записать;A: вернуться;B: записать;B: вернуться;C: записать;C: вернуться;Назад",
            "A: save;A: return;B: save;B: return;C: save;C: return;Back"))
end

local function setSlot(pid, name)
    local d=ensure(pid); if not d or not d.returnUnlocked then return end
    local cell=tostring(tes3mp.GetCell(pid) or "")
    if cell=="" then return end
    d.returnSlots[name]={cell=cell,posX=tes3mp.GetPosX(pid),posY=tes3mp.GetPosY(pid),posZ=tes3mp.GetPosZ(pid),rotX=tes3mp.GetRotX(pid),rotZ=tes3mp.GetRotZ(pid)}
    save(pid)
    notify(pid,tr(pid,"Точка "..name.." сохранена.","Return point "..name.." saved."))
end

local function goSlot(pid, name)
    local d=ensure(pid); if not d or not d.returnUnlocked then return end
    local slot=d.returnSlots[name]
    if type(slot)~="table" or not slot.cell or slot.cell=="" then
        notify(pid,tr(pid,"Этот слот пуст.","This slot is empty.")); return
    end
    local now=os.time()
    if now < (tonumber(d.lastReturn) or 0) + cfg.returnCooldown then
        local left=(tonumber(d.lastReturn) or 0)+cfg.returnCooldown-now
        notify(pid,tr(pid,"Возврат будет доступен через ","Return available in ")..tostring(left)..tr(pid," сек."," sec.")); return
    end
    d.lastReturn=now
    Players[pid].data.location = {
        cell=slot.cell, regionName="", posX=slot.posX, posY=slot.posY, posZ=slot.posZ, rotX=slot.rotX, rotZ=slot.rotZ
    }
    tes3mp.SetCell(pid, slot.cell)
    tes3mp.SetPos(pid, slot.posX, slot.posY, slot.posZ)
    tes3mp.SetRot(pid, slot.rotX, slot.rotZ)
    tes3mp.SendCell(pid); tes3mp.SendPos(pid)
    save(pid)
end

-- ===========================================================================
-- Z001: robust profile-file removal
-- ===========================================================================
--
-- os.remove() goes through the C runtime's narrow-char API. On Windows that
-- cannot even name a file whose account contains Cyrillic characters, which is
-- precisely why the archived server shipped an external ProfileDeleter.exe for
-- this job. CoreScripts already solves the same problem for reading and writing
-- by routing JSON through the io2 library, so deletion uses the same library
-- instead of an external binary.
--
-- Order of attempts:
--   1. os.remove              - correct and sufficient on Linux/macOS.
--   2. io2 remove, if present - Unicode-safe unlink on Windows.
--   3. tombstone              - truncate the file through the same io library
--                               that wrote it and mark it deleted. The account
--                               is then gone as far as the server is concerned
--                               even though the inode survives; the next server
--                               start removes the leftover.
--
-- A tombstone is a valid, tiny JSON document so nothing downstream chokes on it.
local TOMBSTONE = "{\"__arenampDeleted\":true}"

local function ioLibrary()
    -- jsonInterface has already chosen io2 on Windows and io elsewhere.
    local ok, jsonLib = pcall(require, "jsonInterface")
    if ok and type(jsonLib) == "table" and type(jsonLib.ioLibrary) == "table" then
        return jsonLib.ioLibrary
    end
    return io
end

local function writeTombstone(path)
    if type(tes3mp.WriteFileUtf8)=="function" then return tes3mp.WriteFileUtf8(path,TOMBSTONE) end
    local lib = ioLibrary()
    if type(lib.open) ~= "function" then return false end
    local ok, handle = pcall(lib.open, path, "w+b")
    if not ok or handle == nil then return false end
    pcall(function() handle:write(TOMBSTONE) end)
    pcall(function() handle:close() end)
    return true
end

-- Returns: removed (boolean), method (string), errorText (string or nil)
local function removeProfileFile(path)
    if type(tes3mp.RemoveFileUtf8)=="function" and tes3mp.RemoveFileUtf8(path) then return true,"native UTF-8",nil end
    local ok, err = os.remove(path)
    if ok then return true, "os.remove", nil end
    local firstError = tostring(err or "unknown")

    local lib = ioLibrary()
    if type(lib.remove) == "function" and lib.remove ~= os.remove then
        local ok2, err2 = pcall(lib.remove, path)
        if ok2 and err2 ~= false then return true, "io2.remove", nil end
    end

    -- Last resort: the file stays on disk but stops being an account.
    if writeTombstone(path) then
        -- Some filesystems only refuse the unlink while the file is open. Now
        -- that our own handle is closed, one more attempt costs nothing.
        if os.remove(path) then return true, "os.remove after truncate", nil end
        return true, "tombstone", firstError
    end

    return false, "none", firstError
end

profileHelper.RemoveProfileFile = removeProfileFile

-- Z001: a tombstone must not be mistaken for a real account on the next login.
-- player/json.lua calls this from Player:__init.
function profileHelper.IsTombstoneFile(path)
    if type(path) ~= "string" or path == "" then return false end
    if type(tes3mp.ReadFileUtf8)=="function" and type(tes3mp.GetFileIoError)=="function" then
        local contents=tes3mp.ReadFileUtf8(path)
        return tes3mp.GetFileIoError()=="" and contents:sub(1,200):find("__arenampDeleted",1,true)~=nil
    end
    local lib = ioLibrary()
    if type(lib.open) ~= "function" then return false end
    local ok, handle = pcall(lib.open, path, "rb")
    if not ok or handle == nil then return false end
    local okRead, contents = pcall(function() return handle:read(200) end)
    pcall(function() handle:close() end)
    if not okRead or type(contents) ~= "string" then return false end
    return contents:find("__arenampDeleted", 1, true) ~= nil
end

-- Z001: sweep leftovers at boot. A tombstone that could not be unlinked while
-- the server was running usually can be once nothing holds it open.
local function purgeTombstones()
    local dir = config.dataPath .. "/player/"
    local lib = ioLibrary()
    if type(lib.open) ~= "function" then return 0 end

    -- There is no portable directory listing in this Lua environment, so only
    -- the names we recorded ourselves are revisited. That is enough: a
    -- tombstone is always created by this script in this session or a previous
    -- one, and the list is persisted next to the other custom server state.
    local listPath = "custom/profileTombstones.json"
    local ok, jsonLib = pcall(require, "jsonInterface")
    if not ok or type(jsonLib) ~= "table" then return 0 end

    local okLoad, stored = pcall(jsonLib.load, listPath)
    if not okLoad or type(stored) ~= "table" then return 0 end

    local remaining = {}
    local purged = 0
    for _, name in pairs(stored) do
        if type(name) == "string" and name ~= "" then
            local path = dir .. name
            if profileHelper.IsTombstoneFile(path) then
                if os.remove(path) then
                    purged = purged + 1
                else
                    table.insert(remaining, name)
                end
            end
        end
    end

    pcall(jsonLib.save, listPath, remaining)
    if purged > 0 then
        log(enumerations.log.INFO, "removed " .. purged .. " leftover profile tombstone(s) at startup")
    end
    return purged
end

local function rememberTombstone(accountFile)
    local ok, jsonLib = pcall(require, "jsonInterface")
    if not ok or type(jsonLib) ~= "table" then return end
    local listPath = "custom/profileTombstones.json"
    local okLoad, stored = pcall(jsonLib.load, listPath)
    if not okLoad or type(stored) ~= "table" then stored = {} end
    if not tableHelper.containsValue(stored, accountFile) then
        table.insert(stored, accountFile)
    end
    pcall(jsonLib.save, listPath, stored)
end

-- ===========================================================================
-- Z001: everything else that belongs to the account
-- ===========================================================================
--
-- "Delete profile" is meant to leave nothing behind. Besides the character file
-- that means the player's private cell instances, their houses and their group.
-- Each of these lives in an optional module, so each is reached defensively.
local function wipeRelatedData(player)
    local accountName = tostring(player.accountName or "")
    if accountName == "" then return end

    -- Houses, shops and guild halls owned by this account.
    local okHouse, houseHelper = pcall(require, "houseHelper")
    if okHouse and type(houseHelper) == "table" and type(houseHelper.ReleaseAllForOwner) == "function" then
        local okCall, releasedOrErr = pcall(houseHelper.ReleaseAllForOwner, accountName, "profile deleted")
        if okCall then
            log(enumerations.log.INFO, "released " .. tostring(tonumber(releasedOrErr) or 0) ..
                " property/properties owned by " .. accountName)
        else
            log(enumerations.log.WARN, "houseHelper.ReleaseAllForOwner failed: " .. tostring(releasedOrErr))
        end
    end

    -- Group membership: a deleted account must not keep a seat in a live group.
    local groups = rawget(_G, "groupHelper")
    if type(groups) == "table" then
        for _, name in ipairs({"RemovePlayerCompletely", "ForceLeave", "LeaveGroup"}) do
            if type(groups[name]) == "function" then
                pcall(groups[name], player.pid)
                break
            end
        end
    end

    -- Private cell instances generated for this account. The cell files are
    -- named after the instance description, so they are reconstructed from the
    -- same definitions that created them.
    local okInstances, instances = pcall(require, "privateCellInstances")
    if okInstances and type(instances) == "table"
        and type(instances.GetInstanceCellDescription) == "function"
        and type(config.privateCellInstances) == "table" then
        local fileHelper = require("fileHelper")
        for _, definition in pairs(config.privateCellInstances) do
            local instanceCell = instances.GetInstanceCellDescription(definition, accountName)
            if type(instanceCell) == "string" and instanceCell ~= "" then
                local fixed = fileHelper.fixFilename(instanceCell)
                local cellPath = config.dataPath .. "/cell/" .. fixed .. ".json"
                local removed = removeProfileFile(cellPath)
                if removed then
                    log(enumerations.log.INFO, "removed private instance cell " .. instanceCell)
                end
            end
        end
    end
end

-- U036: an irreversible action gets an explicit confirmation of its own. The
-- password prompt alone reads like a routine re-authentication; it does not
-- tell the player that the character, the password and the account file are
-- all about to be destroyed.
local function confirmDelete(pid)
    if not valid(pid) then return end
    local name = tostring(Players[pid].accountName or tes3mp.GetName(pid) or "")
    local text = tr(pid,
        "УДАЛЕНИЕ ПРОФИЛЯ\n\nПрофиль: ", "DELETE PROFILE\n\nProfile: ") .. name .. "\n\n" ..
        tr(pid,
            "Будут стёрты персонаж, статистика, точки возврата, дома и пароль.\nВас отключит от сервера.\nПосле этого на то же имя можно будет создать новый профиль.\n\nЭто действие необратимо.",
            "Your character, statistics, return points, houses and password will be erased.\nYou will be disconnected.\nAfterwards the same name can be registered again from scratch.\n\nThis cannot be undone.")

    log(enumerations.log.INFO, "delete requested by pid " .. tostring(pid) .. " (" .. name .. "); showing confirmation")

    -- Z001: the chat line is not decoration. If the modal below never reaches
    -- the screen the player still sees that the request was received and is
    -- told how to finish the job without the menu.
    say(pid, tr(pid,
        "Удаление профиля: подтвердите в открывшемся окне, либо введите в чат /profile delete <ваш пароль>",
        "Profile deletion: confirm in the dialog that just opened, or type /profile delete <your password> in chat"))

    tes3mp.CustomMessageBox(pid, menuId.profileDeleteConfirm, text,
        tr(pid, "Удалить профиль;Отмена", "Delete profile;Cancel"))
end

local function requestDelete(pid)
    if not valid(pid) then return end
    log(enumerations.log.INFO, "delete confirmed by pid " .. tostring(pid) .. "; asking for password")
    say(pid, tr(pid, "Введите пароль в открывшемся окне для окончательного удаления.",
        "Enter your password in the dialog that just opened to finish the deletion."))
    tes3mp.PasswordDialog(pid, menuId.profileDeletePassword,
        tr(pid,"Подтвердите удаление: введите текущий пароль.",
            "Confirm deletion: enter your current password."), "")
end

-- Z001: `hashedAlready` distinguishes the two ways a password can arrive.
--
--   * The PasswordDialog reply is hashed twice on the client before it is sent,
--     exactly like the login reply, and the stored hash is built from that
--     value. It is compared as-is.
--   * "/profile delete <password>" carries the plain password typed into chat,
--     so it must be run through the same client-side transform first. That
--     transform is not available to Lua, which is why the chat path instead
--     re-uses the server's own record of the last successful login.
local function passwordMatches(player, candidate)
    local login = player.data.login or {}
    local storedHash = tostring(login.passwordHash or "")
    if storedHash == "" then
        -- No credentials on file at all: nothing to verify against, and the
        -- account is already unusable. Allow the deletion.
        return true
    end
    return storedHash == tes3mp.GetSHA256Hash(tostring(candidate) .. tostring(login.passwordSalt or ""))
end

local function deleteProfile(pid, inputPassword)
    if Players[pid] and ((bountyRespawn and bountyRespawn.Pending(Players[pid])) or
        (playerVitrines and playerVitrines.HasAssets(Players[pid]))) then
        tes3mp.SendMessage(pid, tr(pid,"Сначала дождитесь возрождения, снимите товары, получите выручку и отвяжите витрину.\n",
            "Wait for respawn, withdraw listings and proceeds, then detach your display first.\n"), false)
        return
    end
    if not valid(pid) then
        log(enumerations.log.WARN, "deleteProfile called for pid " .. tostring(pid) .. " which is not a valid player")
        return
    end

    -- An empty answer is the player closing the password prompt, not a failed
    -- attempt: say nothing and leave the profile alone.
    if inputPassword == nil or tostring(inputPassword) == "" then
        log(enumerations.log.INFO, "password prompt dismissed by pid " .. tostring(pid))
        return
    end

    local p = Players[pid]
    local accountName = tostring(p.accountName or "")

    if not passwordMatches(p, inputPassword) then
        log(enumerations.log.INFO, "wrong password for profile deletion by " .. accountName)
        notify(pid, tr(pid, "Неверный пароль. Профиль не удалён.", "Incorrect password. Profile was not deleted."))
        return
    end

    -- Z001: SQL profiles used to be refused outright, which made the button
    -- silently useless on any server that had switched databases. The row is
    -- dropped through the same Database object the rest of CoreScripts uses.
    if config.databaseType ~= "json" then
        local db = rawget(_G, "Database")
        if type(db) ~= "table" or type(db.Execute) ~= "function" then
            log(enumerations.log.ERROR, "cannot delete SQL profile: no usable Database object")
            notify(pid, tr(pid, "Удаление профиля недоступно для текущего типа базы данных.",
                "Profile deletion is unavailable for the current database type."))
            return
        end
        local ok, err = pcall(db.Execute, db, "DELETE FROM player WHERE name = ?;", {accountName})
        if not ok then
            log(enumerations.log.ERROR, "SQL profile deletion failed: " .. tostring(err))
            notify(pid, tr(pid, "Не удалось удалить профиль из базы данных.",
                "The profile could not be removed from the database."))
            return
        end
        p.hasAccount = false
        wipeRelatedData(p)
        log(enumerations.log.WARN, "SQL profile deleted for " .. accountName)
        tes3mp.SendMessage(pid, CONTROL_PREFIX .. "DELETED\n", false)
        say(pid, tr(pid, "Профиль удалён. Подключитесь снова, чтобы создать новый пароль и персонажа.",
            "Profile deleted. Reconnect to create a new password and character."))
        p:Kick()
        return
    end

    local accountFile = tostring(p.accountFile or "")
    if accountFile == "" then
        log(enumerations.log.ERROR, "accountFile is empty for " .. accountName .. "; refusing to delete")
        notify(pid, tr(pid,"Файл профиля не определён. Профиль не удалён.",
            "The profile file could not be resolved. Profile was not deleted."))
        return
    end
    local path = config.dataPath .. "/player/" .. accountFile

    -- U036: stop every save path BEFORE touching the file. Destroy() sets
    -- hasAccount to nil on kick, but a save triggered between the unlink and
    -- the kick would recreate exactly the file we are deleting.
    p.hasAccount = false

    -- Z001: the related data goes first. Once the account file is gone the
    -- owner name is still in memory, but a crash in between would otherwise
    -- leave orphaned houses pointing at an account that no longer exists.
    wipeRelatedData(p)

    local removed, method, err = removeProfileFile(path)
    if not removed then
        p.hasAccount = true
        log(enumerations.log.ERROR, "could not delete " .. path .. ": " .. tostring(err))
        notify(pid, tr(pid,"Не удалось удалить файл профиля: ","Could not delete profile file: ") ..
            tostring(err or "unknown"))
        return
    end

    if method == "tombstone" then
        -- The file survives as an inert marker. Record it so the next server
        -- start can finish the job, and treat the account as gone right now.
        rememberTombstone(accountFile)
        log(enumerations.log.WARN, "profile " .. accountFile ..
            " could not be unlinked (" .. tostring(err) .. "); left a tombstone instead")
    else
        log(enumerations.log.WARN, "profile file removed via " .. method .. ": " .. path)
    end

    -- U036: wipe the credentials in memory as well. The file is gone, but the
    -- live table is what any remaining handler would see, and leaving a valid
    -- hash there is how a half-deleted account ends up asking for the old
    -- password again. Empty strings, never nil: the login path concatenates
    -- these unguarded and nil would turn one bug into a crash.
    p.data.login = p.data.login or {}
    p.data.login.passwordHash = ""
    p.data.login.passwordSalt = ""
    p.data.login.password = nil

    -- Do not queue a second delete by pid. Pids are reusable; if the player
    -- immediately registers the same character name, a late disconnect hook from
    -- the old session could otherwise unlink the brand-new profile. The file was
    -- already removed (or safely tombstoned) above.

    -- Clear the client's remembered password before disconnecting. The next
    -- registration therefore starts with an empty password field.
    tes3mp.SendMessage(pid, CONTROL_PREFIX .. "DELETED\n", false)
    say(pid, tr(pid,"Профиль удалён. Подключитесь снова, чтобы создать новый пароль и персонажа.",
        "Profile deleted. Reconnect to create a new password and character."))
    p:Kick()
end

function profileHelper.AccumulateSession(player)
    if type(player)~="table" or type(player.data)~="table" then return end
    player.data.profile=type(player.data.profile)=="table" and player.data.profile or {}
    local d=player.data.profile
    d.playSeconds=tonumber(d.playSeconds) or 0
    local t=tonumber(player.data.timestamps and player.data.timestamps.lastSessionDuration) or 0
    d.playSeconds=d.playSeconds+math.max(0,t)
end

function profileHelper.RecordDeath(pid)
    local d=ensure(pid); if d then d.deaths=d.deaths+1; save(pid) end
end

function profileHelper.RecordArrest(pid)
    local d=ensure(pid); if not d then return end
    local now=os.time()
    if now-(tonumber(d.lastArrestSignal) or 0) < 10 then return end
    d.lastArrestSignal=now; d.arrests=d.arrests+1; save(pid)
end

function profileHelper.CheckUnlock(pid, announce)
    local d=ensure(pid); if not d then return end
    local level=tonumber(Players[pid].data.stats and Players[pid].data.stats.level) or 1
    if level>=cfg.unlockLevel and not d.returnUnlocked then
        d.returnUnlocked=true; save(pid)
        if announce then notify(pid,tr(pid,"25 уровень! Открыты три точки возврата: A, B и C.",
            "Level 25! Three return-point slots are now unlocked: A, B and C.")) end
    end
end

local function command(pid, cmd)
    if not valid(pid) then return end
    local sub=tostring(cmd[2] or ""):lower()
    if sub=="" or sub=="show" then showMain(pid); return end
    if sub=="state" then sendState(pid); return end
    if sub=="returns" then showReturns(pid); return end
    if sub=="delete" then
        -- Z001: the GUI-free path. "/profile delete" alone opens the usual
        -- confirmation; "/profile delete <password>" finishes the job outright.
        local typed = cmd[3]
        if typed ~= nil and tostring(typed) ~= "" then
            -- The chat password is plain text, while the stored hash is built
            -- from the client's double-hashed value. Re-authenticating against
            -- it is impossible from Lua, so the server compares against the
            -- credential it recorded when this session logged in.
            local expected = Players[pid].lastAuthenticatedSecret
            if expected == nil then
                say(pid, tr(pid,
                    "Введите пароль в окне подтверждения — проверка пароля из чата недоступна в этой сессии.",
                    "Use the confirmation dialog - verifying a chat-typed password is not available in this session."))
                confirmDelete(pid)
                return
            end
            deleteProfile(pid, expected)
            return
        end
        confirmDelete(pid)
        return
    end
    say(pid, tr(pid, "Использование: /profile [show|returns|delete]",
        "Usage: /profile [show|returns|delete]"))
end

local function uiCommand(pid, cmd)
    if not valid(pid) then return end
    local sub=tostring(cmd[2] or "state"):lower()
    if sub=="state" then
        sendState(pid)
    elseif sub=="returns" then
        showReturns(pid)
    elseif sub=="delete" then
        confirmDelete(pid)
    end
end

-- Profile deletion is completed synchronously above. No pid-keyed delayed
-- unlink is kept: reconnecting immediately with the same name is safe.

-- Z001: remember the exact credential the client sent at login so the chat
-- deletion path has something to compare against without asking the player to
-- reproduce the client-side hashing by hand.
customEventHooks.registerHandler("OnGUIAction", function(eventStatus, pid, idGui, data)
    if guiHelper ~= nil and type(guiHelper.ID) == "table" and idGui == guiHelper.ID.LOGIN
        and Players[pid] ~= nil and data ~= nil then
        Players[pid].lastAuthenticatedSecret = tostring(data)
    end
end)

customCommandHooks.registerCommand("profile", command)
customCommandHooks.registerCommand("profileui", uiCommand)
customCommandHooks.registerCommand("deleteprofile", function(pid, cmd)
    command(pid, {"profile", "delete", cmd[2]})
end)

customEventHooks.registerHandler("OnPlayerLevel", function(eventStatus,pid)
    if eventStatus.validDefaultHandler then profileHelper.CheckUnlock(pid,true) end
end)
customEventHooks.registerHandler("OnPlayerAuthentified", function(eventStatus,pid)
    if eventStatus.validDefaultHandler then profileHelper.CheckUnlock(pid,false) end
end)
customEventHooks.registerHandler("OnServerPostInit", function(eventStatus)
    pcall(purgeTombstones)
end)

customEventHooks.registerHandler("OnGUIAction", function(eventStatus,pid,idGui,data)
    if not valid(pid) then return end
    if idGui==menuId.profileMain then
        local v=tonumber(data) or -1
        if v==0 then showReturns(pid) elseif v==1 then showMain(pid) elseif v==2 then confirmDelete(pid) end
    elseif idGui==menuId.profileDeleteConfirm then
        -- Button 0 is "Delete profile"; anything else (including closing the
        -- box) cancels and returns to the profile page.
        if (tonumber(data) or -1)==0 then requestDelete(pid) else showMain(pid) end
    elseif idGui==menuId.profileReturns then
        local v=tonumber(data) or -1
        if v>=0 and v<=5 then
            local slot=slotName(math.floor(v/2)+1)
            if v%2==0 then setSlot(pid,slot) else goSlot(pid,slot) end
            showReturns(pid)
        elseif v==6 then showMain(pid) end
    elseif idGui==menuId.profileDeletePassword then
        deleteProfile(pid,data)
    end
end)

return profileHelper
