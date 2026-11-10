-- ArenaMP Core: shared logging and JSON bootstrap helper.
--
-- Two jobs:
--
--   1. One log identity. Every ArenaMP-owned script used to stamp its own
--      module tag and, worse, the patch number it happened to ship in
--      ("[groupHelper] X050 loaded", "[X031 ConfigReload] Watching ...").
--      Those tags went stale the moment the next patch landed and made the
--      server log read like a changelog. Everything now logs as
--      "[ArenaMP Core]" and says what happened instead of when it was added.
--
--   2. One place that guarantees a JSON data file exists. jsonInterface.load
--      returns nil for a missing file, and on Windows the underlying io2
--      library prints its own "Cannot open ... in mode r" line to stdout
--      before that nil ever reaches Lua, so a pcall around the load cannot
--      silence it. The only real fix is to make sure the file is on disk, so
--      EnsureJson writes the default the first time and the message never
--      comes back.
--
-- Load order: this module needs jsonInterface to already have its io library
-- selected, so serverCore requires it immediately after that selection.

local ampCore = {}

ampCore.LOG_TAG = "[ArenaMP Core]"

-- Data files ArenaMP owns and wants present before any helper reads them.
-- EnsureRegisteredFiles walks this on OnServerPostInit.
local registeredFiles = {}

--- Strip a leading module tag and any bare patch token from a message.
-- "[groupHelper] X050 loaded: groups" -> "loaded: groups"
-- "[X031 ConfigReload] Watching x"    -> "Watching x"
-- A message that carries no tag is returned untouched, so callers can pass
-- plain text without thinking about it.
function ampCore.StripLegacyTag(message)
    message = tostring(message or "")
    -- Only one leading bracketed tag is removed. Chat-facing tags such as
    -- [Group] or [Local] never reach this function; they are sent through
    -- tes3mp.SendMessage, not through the log.
    message = message:gsub("^%s*%[[%w%s_%-]+%]%s*", "")
    -- A patch token that survived as a bare word at the start of the text.
    message = message:gsub("^[Xx]%d%d%d[a-z]?%s+", "")
    return message
end

--- Write one line to the server log under the shared ArenaMP identity.
function ampCore.Log(level, message)
    tes3mp.LogMessage(level, ampCore.LOG_TAG .. " " .. ampCore.StripLegacyTag(message))
end

--- Convenience wrapper so a helper can keep its existing local log() shape:
---   local log = ampCore.Logger()
function ampCore.Logger()
    return function(level, message)
        ampCore.Log(level, message)
    end
end

function ampCore.LogInfo(message) ampCore.Log(enumerations.log.INFO, message) end
function ampCore.LogWarn(message) ampCore.Log(enumerations.log.WARN, message) end
function ampCore.LogError(message) ampCore.Log(enumerations.log.ERROR, message) end

--- Absolute path of a file inside server/data.
function ampCore.ResolveDataPath(relativePath)
    return tostring(config.dataPath or ".") .. "/" .. tostring(relativePath or "")
end

--- Positive-only existence probe.
--
-- Returns true when the standard Lua io library can open the file. A false
-- result means "unknown", not "missing": on Windows the data path can contain
-- non-ASCII characters (a user profile such as C:\Users\Зэро\...), and plain
-- io cannot open those at all. That is the entire reason TES3MP swaps in io2.
-- Treating a false here as proof of absence would let EnsureJson overwrite a
-- perfectly good file, so callers must fall back to an actual load attempt.
function ampCore.FileDefinitelyExists(relativePath)
    local ok, file = pcall(io.open, ampCore.ResolveDataPath(relativePath), "r")
    if ok and file ~= nil then
        file:close()
        return true
    end
    return false
end

--- Best-effort directory creation, used only after a write has already failed.
-- Never called on the happy path, because spawning a shell on every server
-- start to create a directory that is almost always present is not worth it.
function ampCore.EnsureDirectory(relativeDirectory)
    local path = ampCore.ResolveDataPath(relativeDirectory)
    local command
    if tes3mp.GetOperatingSystemType() == "Windows" then
        command = 'mkdir "' .. path:gsub("/", "\\") .. '" >nul 2>nul'
    else
        command = 'mkdir -p "' .. path .. '" >/dev/null 2>&1'
    end
    pcall(os.execute, command)
end

local function directoryOf(relativePath)
    return tostring(relativePath or ""):match("^(.*)/[^/]*$")
end

--- Load a JSON file, creating it from defaultValue when it is not there yet.
--
-- Returns loadedTable, wasCreated. The returned table is always safe to index:
-- if both the load and the bootstrap write fail, a deep copy of defaultValue
-- comes back so the caller keeps running in memory instead of erroring out.
function ampCore.EnsureJson(relativePath, defaultValue, description)
    defaultValue = defaultValue or {}
    description = description or relativePath
    local absolute = ampCore.ResolveDataPath(relativePath)
    local lib = jsonInterface.ioLibrary or io

    -- A read-only "r" probe of a missing file makes io2 print an error BEFORE
    -- Lua can catch it. "a+" creates missing files without truncating existing
    -- ones, including paths with Cyrillic characters on Windows. Never use "w"
    -- to probe an existing account/world file.
    local function openWithoutTruncation()
        local ok, handle = pcall(lib.open, absolute, "a+")
        return ok and handle or nil
    end
    local handle = openWithoutTruncation()
    if not handle then
        local directory = directoryOf(relativePath)
        if directory then ampCore.EnsureDirectory(directory) end
        handle = openWithoutTruncation()
    end
    if not handle then
        -- An existing read-only file may still be readable. Do not overwrite it.
        local ok, existing = pcall(jsonInterface.load, relativePath)
        if ok and type(existing) == "table" then return existing, false end
        ampCore.LogError("Cannot read/create " .. description ..
            "; check the data directory permissions. Existing data was left untouched")
        return tableHelper.deepCopy(defaultValue), false
    end

    -- TES3MP Windows io2 is NOT a stock Lua file handle: file:seek() may be
    -- unavailable. The old H011 probe returned "unknown size" for every file.
    -- A native crash was also reported, but its exact cause is not yet proven.
    -- Use only the read("*all") API that TES3MP jsonInterface itself uses.
    pcall(function() handle:close() end)
    local readOk, contents = pcall(function()
        local reader = lib.open(absolute, "r")
        if reader == nil then return nil end
        local okContent, result = pcall(function() return reader:read("*all") end)
        pcall(function() reader:close() end)
        if okContent then return result end
        return nil
    end)
    if not readOk or type(contents) ~= "string" then
        ampCore.LogError("Cannot read " .. description ..
            "; preserving file and using in-memory defaults")
        return tableHelper.deepCopy(defaultValue), false
    end
    if #contents > 0 then
        -- Do not overwrite any existing nonempty file, even corrupted JSON.
        local ok, loaded = pcall(jsonInterface.load, relativePath)
        if ok and type(loaded) == "table" then return loaded, false end
        ampCore.LogError("Invalid JSON in " .. description ..
            "; preserving the original file and using in-memory defaults")
        return tableHelper.deepCopy(defaultValue), false
    end

    if not ampCore.SaveJson(relativePath, defaultValue, description) then
        ampCore.LogError("Could not initialize " .. description .. "; defaults will not persist")
        return tableHelper.deepCopy(defaultValue), false
    end
    ampCore.LogInfo("Created missing/empty data file " .. description)
    return tableHelper.deepCopy(defaultValue), true
end

-- Run before any helper's top-level jsonInterface.load(). On a fresh install
-- no "Cannot open ... mode r" messages are printed by io2 for these files.
-- Crucially: this NEVER resets existing populated player/server data.
function ampCore.EnsureStartupFiles()
    -- The custom directory may not exist on a completely fresh standalone
    -- install. Create it once before opening any JSON through io2.
    ampCore.EnsureDirectory("custom")
    local files = {
        {"custom/arenaFurnitureSources.json", {dataDirs={}}},
        {"custom/arenaFurnitureManual.json", {}},
        {"custom/arenaFurnitureGenerated.json", {items={}}},
        {"custom/groupHelper.json", {version=1, nextId=1, groups={}}},
        {"custom/profileTombstones.json", {}},
        {"custom/groupMapMarkers.json", {version=1, groups={}}},
        {"custom/arenaHouses.json", {properties={}, doors={}, version=1}},
        {"custom/mwScriptOverrides.json", {manifest={}, disabled={}}},
        {"custom/cellResetTimers.json", {}}
    }
    for _, entry in ipairs(files) do
        ampCore.EnsureJson(entry[1], entry[2], entry[1])
    end
end

--- Save a table, reporting failure under the shared log identity.
function ampCore.SaveJson(relativePath, data, description)
    local ok, result = pcall(jsonInterface.save, relativePath, data)
    if not ok then
        ampCore.LogError("Could not save " .. tostring(description or relativePath) .. ": " .. tostring(result))
        return false
    end
    if result == false then
        ampCore.LogError("Could not save " .. tostring(description or relativePath) ..
            "; the data path may be read-only or missing")
        return false
    end
    return true
end

--- Register a data file to be created, if missing, at OnServerPostInit.
-- Helpers that read their own file at load time should call EnsureJson
-- directly instead; this list is for files that are only touched later and
-- would otherwise produce their first "cannot open" line mid-session.
function ampCore.RegisterDataFile(relativePath, defaultValue, description)
    table.insert(registeredFiles, {
        path = relativePath,
        default = defaultValue or {},
        description = description or relativePath
    })
end

function ampCore.EnsureRegisteredFiles()
    for _, entry in ipairs(registeredFiles) do
        ampCore.EnsureJson(entry.path, entry.default, entry.description)
    end
end

customEventHooks.registerHandler("OnServerPostInit", function()
    ampCore.EnsureRegisteredFiles()
end)

return ampCore
