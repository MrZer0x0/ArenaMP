require("config")
fileHelper = require("fileHelper")
tableHelper = require("tableHelper")
local BasePlayer = require("player.base")

local Player = class("Player", BasePlayer)

-- Y098: native UTF-8 IO keeps profiles readable by player name on Windows.
-- _utf8_ names remain only as an old-server fallback and migration source.
local function getLegacyAccountName(accountName)
    if not accountName:find("[\128-\255]") then return accountName end
    local digest = tes3mp.GetSHA256Hash(accountName)
    if type(digest) ~= "string" or digest == "" then
        local bytes={}
        for i=1,#accountName do bytes[#bytes+1]=string.format("%02X",accountName:byte(i)) end
        digest=table.concat(bytes)
    end
    return "_utf8_"..digest
end
local function hasUtf8IO()
    return type(tes3mp.ReadFileUtf8)=="function" and type(tes3mp.WriteFileUtf8)=="function"
        and type(tes3mp.GetFileIoError)=="function" and type(tes3mp.ReplaceFileAtomic)=="function"
end

local function findAccountFile(stem)
    return tes3mp.GetCaseInsensitiveFilename(config.dataPath .. "/player/", stem .. ".json")
end

local function findAccountBackup(stem)
    return tes3mp.GetCaseInsensitiveFilename(config.dataPath .. "/player/", stem .. ".json.bak")
end

function Player:__init(pid, playerName)
    BasePlayer.__init(self, pid, playerName)

    -- Ensure filename is valid
    self.accountName = fileHelper.fixFilename(self.accountName)
    local legacyName=getLegacyAccountName(self.accountName)
    self.storageAccountName = hasUtf8IO() and self.accountName or legacyName
    self.accountFile = findAccountFile(self.storageAccountName)
    if self.accountFile == "invalid" then
        self.accountFile = findAccountFile(legacyName)
    end
    if self.accountFile == "invalid" then
        self.accountFile = findAccountFile(self.accountName)
    end
    if self.accountFile == "invalid" then
        local backup = findAccountBackup(self.storageAccountName)
        if backup == "invalid" then backup = findAccountBackup(legacyName) end
        if backup == "invalid" and self.storageAccountName ~= self.accountName then
            backup = findAccountBackup(self.accountName)
        end
        if backup ~= "invalid" then
            local recovered = backup:sub(1, -5)
            local rename = tes3mp.RenameFileUtf8 or jsonInterface.ioLibrary.rename or os.rename
            if rename(config.dataPath .. "/player/" .. backup, config.dataPath .. "/player/" .. recovered) then
                self.accountFile = recovered
            else
                error("Unable to recover player backup: " .. recovered)
            end
        end
    end
    -- Move only when the destination is absent; never overwrite an account or
    -- leave a legacy backup that could resurrect a deleted named profile.
    if hasUtf8IO() and self.accountFile ~= "invalid" and self.accountFile ~= self.accountName..".json"
        and self.accountFile:sub(1,6)=="_utf8_" and type(tes3mp.RenameFileUtf8)=="function" then
        local home=config.dataPath.."/player/"
        local destination=self.accountName..".json"
        local backup=findAccountBackup(self.accountFile:sub(1,-6))
        local backupMoved=false
        local mayMove=true
        if backup~="invalid" then
            mayMove=findAccountBackup(self.accountName)=="invalid"
                and tes3mp.RenameFileUtf8(home..backup,home..destination..".bak")
            backupMoved=mayMove
        end
        if mayMove and tes3mp.RenameFileUtf8(home..self.accountFile,home..destination) then
            tes3mp.LogMessage(enumerations.log.INFO,"[ArenaMP Profile] Migrated "..self.accountFile.." to "..destination)
            self.accountFile=destination
        elseif backupMoved then
            tes3mp.RenameFileUtf8(home..destination..".bak",home..backup)
        end
    end
    if self.accountFile == "invalid" then
        self.hasAccount = false
        self.accountFile = self.storageAccountName .. ".json"
    else
        self.hasAccount = true
    end

    -- Z001: a deleted profile whose file could not be unlinked (Windows keeps a
    -- handle, or the account name is non-ASCII and the C runtime cannot even
    -- name the file) is left behind as a tombstone rather than a real account.
    -- Treat it as a missing account so the player gets the registration flow
    -- instead of being asked for the password they just destroyed.
    if self.hasAccount then
        local ok, helper = pcall(require, "profileHelper")
        if ok and type(helper) == "table" and type(helper.IsTombstoneFile) == "function" then
            local path = config.dataPath .. "/player/" .. self.accountFile
            local okCheck, isTombstone = pcall(helper.IsTombstoneFile, path)
            if okCheck and isTombstone then
                tes3mp.LogMessage(enumerations.log.INFO,
                    "[ArenaMP Profile] " .. self.accountFile .. " is a deletion tombstone; treating as a new account")
                pcall(tes3mp.RemoveFileUtf8 or os.remove, path)
                self.hasAccount = false
                self.accountFile = self.storageAccountName .. ".json"
            end
        end
    end
end

function Player:CreateAccount()
    self.hasAccount = jsonInterface.saveAtomic("player/" .. self.accountFile, self.data)

    if self.hasAccount then
        tes3mp.LogMessage(enumerations.log.INFO, "Successfully created JSON file for player " .. self.accountName ..
            " as " .. self.accountFile)
        return true
    end

    tes3mp.LogMessage(enumerations.log.ERROR,
        "[ArenaMP CharGen] Failed to create player profile " .. self.accountFile ..
        " for " .. self.accountName .. "; CharGen cannot be committed")
    tes3mp.SendMessage(self.pid,
        "Failed to save the new character profile. Please reconnect and try again.\n", true)
    self:Kick()
    return false
end

function Player:SaveToDrive()
    if self.hasAccount then
        tes3mp.LogMessage(enumerations.log.INFO, "Saving player " .. logicHandler.GetChatName(self.pid))
        return jsonInterface.saveAtomic("player/" .. self.accountFile, self.data, config.playerKeyOrder)
    end
end

function Player:QuicksaveToDrive()
    if self.hasAccount then
        return jsonInterface.quicksaveAtomic("player/" .. self.accountFile, self.data)
    end
end

function Player:LoadFromDrive()
    local timing = require("loginSyncHelper")
    local started = timing.Now()
    self.data = jsonInterface.load("player/" .. self.accountFile)

    if self.data == nil then
        tes3mp.LogMessage(enumerations.log.ERROR, "player/" .. self.accountFile .. " cannot be read!")
        tes3mp.StopServer(2)
    else
        -- JSON doesn't allow numerical keys, but we use them, so convert
        -- all string number keys into numerical keys
        tableHelper.fixNumericalKeys(self.data)


        if self.data.login.password ~= nil and self.data.login.passwordHash == nil then
            self:ConvertPlaintextPassword()
        end
    end
    timing.LogSlow("profile disk read and decode", started, self.pid)
end

-- Deprecated functions with confusing names, kept around for backwards compatibility
function Player:Save()
    return self:SaveToDrive()
end

function Player:Load()
    self:LoadFromDrive()
end

return Player
