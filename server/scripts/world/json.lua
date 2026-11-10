require("config")
tableHelper = require("tableHelper")
local BaseWorld = require("world.base")

local World = class("World", BaseWorld)

-- Y042: jsonInterface.load() hands the path straight to io2.open(), which prints
--   io2.open(): io2.file.new(): Cannot open .../world/<file> in mode "r"
-- for a file that simply does not exist yet. Optional world files must therefore
-- be probed before they are loaded, not loaded and then checked for nil.
local function dataFileExists(relativePath)
    local file = io.open((config.dataPath or ".") .. "/" .. relativePath, "r")
    if file == nil then return false end
    file:close()
    return true
end

function World:__init()
    BaseWorld.__init(self)

    self.coreVariablesFile = "coreVariables.json"
    self.worldFile = "world.json"
    self.legacySharedProgressFile = "legacySharedProgress.json"
    self.legacySharedProgress = nil

    if self.hasEntry == nil then
        -- Y042: the old probe called io.close() with no argument, which closes the
        -- default OUTPUT file rather than the handle it had just opened.
        self.hasEntry = dataFileExists("world/" .. self.worldFile)
    end
end


-- Y032: world.json is shared server state. In MMO mode journal entries and
-- dialogue topics belong to player/<account>.json, so omit those keys from the
-- world file entirely. Keep self.data tables alive in memory because CoreScripts
-- expect them to exist and because a live config switch to CO-OP may start using
-- them again.
function World:GetPersistedData()
    local persisted = {}
    for key, value in pairs(self.data or {}) do
        persisted[key] = value
    end

    if config.shareJournal ~= true then
        persisted.journal = nil
    end
    if config.shareTopics ~= true then
        persisted.topics = nil
    end

    return persisted
end

local function journalEntryKey(entry)
    if type(entry) ~= "table" then return tostring(entry) end
    return tostring(entry.type or "") .. "|" .. tostring(entry.quest or ""):lower() .. "|" .. tostring(entry.index or "")
end

local function mergeJournal(target, source)
    if type(target) ~= "table" or type(source) ~= "table" then return false end
    local known = {}
    for _, entry in pairs(target) do
        known[journalEntryKey(entry)] = true
    end

    local changed = false
    for _, entry in pairs(source) do
        local key = journalEntryKey(entry)
        if not known[key] then
            table.insert(target, tableHelper.deepCopy(entry))
            known[key] = true
            changed = true
        end
    end
    return changed
end

local function mergeTopics(target, source)
    if type(target) ~= "table" or type(source) ~= "table" then return false end
    local changed = false

    if #source > 0 then
        for _, topic in ipairs(source) do
            if not tableHelper.containsValue(target, topic) then
                table.insert(target, tableHelper.deepCopy(topic))
                changed = true
            end
        end
    else
        for key, value in pairs(source) do
            if target[key] == nil then
                target[key] = tableHelper.deepCopy(value)
                changed = true
            end
        end
    end

    return changed
end

function World:LoadLegacySharedProgress()
    if self.legacySharedProgress ~= nil then return end

    -- The backup file only exists on servers that actually ran the Y014 mixed
    -- state. On every other server it is absent by design, so its absence must be
    -- silent instead of an io2 error line during startup.
    local loaded = nil
    if dataFileExists("world/" .. self.legacySharedProgressFile) then
        loaded = jsonInterface.load("world/" .. self.legacySharedProgressFile)
    end
    if type(loaded) ~= "table" then loaded = {} end
    if type(loaded.journal) ~= "table" then loaded.journal = {} end
    if type(loaded.topics) ~= "table" then loaded.topics = {} end
    self.legacySharedProgress = loaded
end

function World:BackupLegacySharedProgress(journal, topics)
    local hasJournal = type(journal) == "table" and next(journal) ~= nil
    local hasTopics = type(topics) == "table" and next(topics) ~= nil

    -- Called unconditionally from LoadFromDrive(). With nothing to back up there is
    -- no reason to touch the backup file at all.
    if not hasJournal and not hasTopics then return end

    self:LoadLegacySharedProgress()
    local changed = false
    if hasJournal then
        changed = mergeJournal(self.legacySharedProgress.journal, journal) or changed
    end
    if hasTopics then
        changed = mergeTopics(self.legacySharedProgress.topics, topics) or changed
    end
    if changed then
        jsonInterface.save("world/" .. self.legacySharedProgressFile, self.legacySharedProgress)
    end
end

-- Preserve progression accumulated while the server was accidentally in the
-- Y014 mixed state. Every account gets the former shared state merged into its
-- personal save once it logs in; the merge is idempotent, so keeping the backup
-- is safe across restarts and avoids losing offline players' old progression.
function World:MigrateLegacySharedProgressToPlayer(player)
    if player == nil or player.data == nil then return false, false end
    self:LoadLegacySharedProgress()

    local journalChanged = false
    local topicsChanged = false

    if config.shareJournal ~= true and next(self.legacySharedProgress.journal) ~= nil then
        if type(player.data.journal) ~= "table" then player.data.journal = {} end
        journalChanged = mergeJournal(player.data.journal, self.legacySharedProgress.journal)
    end

    if config.shareTopics ~= true and next(self.legacySharedProgress.topics) ~= nil then
        if type(player.data.topics) ~= "table" then player.data.topics = {} end
        topicsChanged = mergeTopics(player.data.topics, self.legacySharedProgress.topics)
    end

    if (journalChanged or topicsChanged) and type(player.QuicksaveToDrive) == "function" then
        player:QuicksaveToDrive()
    end

    return journalChanged, topicsChanged
end

function World:CreateEntry()
    jsonInterface.save("world/" .. self.coreVariablesFile, self.coreVariables)
    jsonInterface.save("world/" .. self.worldFile, self:GetPersistedData())
    self.hasEntry = true
end

function World:SaveToDrive()
    if self.hasEntry then
        jsonInterface.save("world/" .. self.coreVariablesFile, self.coreVariables)
        jsonInterface.save("world/" .. self.worldFile, self:GetPersistedData(), config.worldKeyOrder)
    end
end

function World:QuicksaveToDrive()
    if self.hasEntry then
        jsonInterface.quicksave("world/" .. self.coreVariablesFile, self.coreVariables)
        jsonInterface.quicksave("world/" .. self.worldFile, self:GetPersistedData())
    end
end

function World:QuicksaveCoreVariablesToDrive()
    if self.hasEntry then
        jsonInterface.quicksave("world/" .. self.coreVariablesFile, self.coreVariables)
    end
end

function World:LoadFromDrive()
    self.coreVariables = jsonInterface.load("world/" .. self.coreVariablesFile)
    self.data = jsonInterface.load("world/" .. self.worldFile)

    if self.data == nil then
        tes3mp.LogMessage(enumerations.log.ERROR, "world/" .. self.worldFile .. " cannot be read!")
        tes3mp.StopServer(2)
    else
        -- JSON doesn't allow numerical keys, but we use them, so convert
        -- all string number keys into numerical keys
        tableHelper.fixNumericalKeys(self.data)

        local hadWorldJournal = self.data.journal ~= nil
        local hadWorldTopics = self.data.topics ~= nil
        local legacyJournal = config.shareJournal ~= true and self.data.journal or nil
        local legacyTopics = config.shareTopics ~= true and self.data.topics or nil

        -- Save a one-time migration source before removing shared progression
        -- from world.json. This prevents existing MMO accounts from losing the
        -- journal/topics accumulated while Y014 accidentally had global sharing
        -- enabled. Offline accounts can be migrated on a later login as well.
        self:BackupLegacySharedProgress(legacyJournal, legacyTopics)

        if config.shareJournal ~= true then
            self.data.journal = {}
        elseif self.data.journal == nil then
            self.data.journal = {}
        end

        if config.shareTopics ~= true then
            self.data.topics = {}
        elseif self.data.topics == nil then
            self.data.topics = {}
        end

        -- Switching to MMO must remove stale shared keys from disk immediately
        -- instead of waiting for an unrelated world save.
        if (config.shareJournal ~= true and hadWorldJournal)
            or (config.shareTopics ~= true and hadWorldTopics) then
            jsonInterface.quicksave("world/" .. self.worldFile, self:GetPersistedData())
        end
    end
end


-- Deprecated functions with confusing names, kept around for backwards compatibility
function World:Save()
    self:SaveToDrive()
end

function World:Load()
    self:LoadFromDrive()
end

return World
