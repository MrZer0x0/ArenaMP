-- ArenaMP X031 - transactional RAW config.lua hot reload.
--
-- The watcher compares file contents, so it does not depend on platform-specific
-- timestamp APIs. A candidate config is executed through Lua's normal module
-- loader only after saving the old global table. Any syntax/runtime failure rolls
-- back atomically and is logged once for that exact broken file contents.

local configHotReload = {}

local fileIo = io
if tes3mp.GetOperatingSystemType() == "Windows" then
    local ok, io2 = pcall(require, "io2")
    if ok and io2 ~= nil and type(io2.open) == "function" then
        fileIo = io2
    end
end

local lastAppliedContents = nil
local lastAttemptedContents = nil
local lastAttemptFailed = false
local resolvedPath = nil

local function sourceDirectory()
    if debug == nil or type(debug.getinfo) ~= "function" then
        return nil
    end

    local info = debug.getinfo(1, "S")
    if info == nil or type(info.source) ~= "string" then
        return nil
    end

    local source = info.source
    if string.sub(source, 1, 1) == "@" then
        source = string.sub(source, 2)
    end

    return string.match(source, "^(.*)[/\\][^/\\]+$")
end

local function candidatePaths()
    local paths = {}
    local dir = sourceDirectory()
    if dir ~= nil and dir ~= "" then
        table.insert(paths, dir .. "/config.lua")
    end
    table.insert(paths, "server/scripts/config.lua")
    table.insert(paths, "scripts/config.lua")
    table.insert(paths, "config.lua")
    return paths
end

local function readAll(path)
    local handle = fileIo.open(path, "rb")
    if handle == nil then
        return nil
    end

    local ok, contents = pcall(function() return handle:read("*a") end)
    pcall(function() handle:close() end)
    if not ok or type(contents) ~= "string" then
        return nil
    end
    return contents
end

local function readConfigFile()
    if resolvedPath ~= nil then
        local contents = readAll(resolvedPath)
        if contents ~= nil then
            return contents
        end
        resolvedPath = nil
    end

    for _, path in ipairs(candidatePaths()) do
        local contents = readAll(path)
        if contents ~= nil then
            resolvedPath = path
            return contents
        end
    end
    return nil
end

local function validateCandidate(candidate)
    if type(candidate) ~= "table" then
        return false, "config did not produce a table"
    end

    local requiredTables = {
        "gameSettings", "vrSettings", "xpLeveling", "equipmentRequirements",
        "arrowStick", "refinedAlchemy", "alchemyGameplay", "defaultRespawn",
        "recordStoreLoadOrder"
    }
    for _, name in ipairs(requiredTables) do
        if type(candidate[name]) ~= "table" then
            return false, "required table config." .. name .. " is missing (file may still be mid-save)"
        end
    end

    if type(candidate.difficulty) ~= "number" or
       type(candidate.allowConsole) ~= "boolean" or
       type(candidate.physicsFramerate) ~= "number" or
       type(candidate.enforceDataFiles) ~= "boolean" then
        return false, "required scalar settings are incomplete (file may still be mid-save)"
    end

    return true, nil
end

function configHotReload.Initialize()
    local contents = readConfigFile()
    if contents == nil then
        tes3mp.LogMessage(enumerations.log.WARN,
            "[ArenaMP Core] Could not locate config.lua; live reload is disabled until the file becomes readable")
        return false
    end

    lastAppliedContents = contents
    lastAttemptedContents = contents
    lastAttemptFailed = false
    tes3mp.LogMessage(enumerations.log.INFO,
        "[ArenaMP Core] Watching " .. tostring(resolvedPath))
    return true
end

--- Returns true only when a new config was loaded successfully.
function configHotReload.Check()
    local contents = readConfigFile()
    if contents == nil then
        return false
    end

    if lastAppliedContents == nil then
        lastAppliedContents = contents
        lastAttemptedContents = contents
        lastAttemptFailed = false
        return false
    end

    if contents == lastAppliedContents then
        return false
    end

    -- Do not spam the same syntax/runtime error once per second. Saving the file
    -- again changes its contents and immediately enables another attempt.
    if lastAttemptFailed and contents == lastAttemptedContents then
        return false
    end

    lastAttemptedContents = contents
    lastAttemptFailed = false

    -- Compile the exact bytes we just watched instead of asking require() to
    -- resolve a path again. This also lets us reject syntax errors before touching
    -- the live global config table. LuaJIT/Lua 5.1 provides loadstring; newer Lua
    -- provides load for a source string.
    local compiler = loadstring or load
    local chunk, compileError = compiler(contents, "@" .. tostring(resolvedPath or "config.lua"))
    if chunk == nil then
        lastAttemptFailed = true
        tes3mp.LogMessage(enumerations.log.ERROR,
            "[ArenaMP Core] RAW config.lua syntax rejected; previous working config remains active: " ..
            tostring(compileError))
        return false
    end

    local previousConfig = config
    config = nil

    local ok, result = pcall(chunk)
    local valid, validationError = false, nil
    if ok then
        valid, validationError = validateCandidate(config)
    end

    if not ok or not valid then
        config = previousConfig
        lastAttemptFailed = true
        local reason = ok and validationError or result
        tes3mp.LogMessage(enumerations.log.ERROR,
            "[ArenaMP Core] RAW config.lua reload rejected; previous working config remains active: " ..
            tostring(reason))
        return false
    end

    -- package.loaded['config'] intentionally stays untouched. Modules which
    -- required config during startup already hold the normal cached marker, while
    -- runtime code reads the replaced global config table.
    lastAppliedContents = contents

    tes3mp.LogMessage(enumerations.log.INFO,
        "[ArenaMP Core] RAW config.lua reloaded successfully; applying live server settings")
    return true
end

function configHotReload.GetPath()
    return resolvedPath
end

return configHotReload
