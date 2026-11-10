local dkjson = require("dkjson")
local cjson
local cjsonExists = doesModuleExist("cjson")

if cjsonExists then
    cjson = require("cjson")
    cjson.encode_sparse_array(true)
    cjson.encode_invalid_numbers("null")
    cjson.encode_empty_table_as_object(false)
    cjson.decode_null_as_lightuserdata(false)
else
    tes3mp.LogMessage(enumerations.log.ERROR, "Could not find Lua CJSON! The decoding and encoding of JSON files will always use dkjson and be slower as a result.")
end

local jsonInterface = {}

jsonInterface.libraryMissingMessage = "No input/output library selected for JSON interface!"

function jsonInterface.setLibrary(ioLibrary)
    jsonInterface.ioLibrary = ioLibrary
end

-- Remove all text from before the actual JSON content starts
function jsonInterface.removeHeader(content)

    local closestBracketIndex

    local bracketIndex1 = content:find("\n%[")
    local bracketIndex2 = content:find("\n{")

    if bracketIndex1 and bracketIndex2 then
        closestBracketIndex = math.min(bracketIndex1, bracketIndex2)
    else
        closestBracketIndex = bracketIndex1 or bracketIndex2
    end

    return content:sub(closestBracketIndex)
end

function jsonInterface.load(fileName)

    if jsonInterface.ioLibrary == nil then
        tes3mp.LogMessage(enumerations.log.ERROR, jsonInterface.libraryMissingMessage)
        return nil
    end

    local home = config.dataPath .. "/"
    local content
    if type(tes3mp.ReadFileUtf8)=="function" and type(tes3mp.GetFileIoError)=="function" then
        content=tes3mp.ReadFileUtf8(home..fileName)
        if tes3mp.GetFileIoError()~="" then
            content=tes3mp.ReadFileUtf8(home..fileName..".bak")
            if tes3mp.GetFileIoError()~="" then content=nil end
        end
    else
        local file = jsonInterface.ioLibrary.open(home .. fileName, 'r')
        if file == nil then file = jsonInterface.ioLibrary.open(home .. fileName .. '.bak', 'r') end
        if file then content=file:read("*a");file:close() end
    end

    if content ~= nil then

        if cjsonExists then
            -- Lua CJSON does not support comments before the JSON data, so remove them if
            -- they are present
            if content:sub(1, 2) == "//" then
                content = jsonInterface.removeHeader(content)
            end

            local decodedContent
            local status, result = pcall(function() decodedContent = cjson.decode(content) end)

            if status then
                return decodedContent
            else
                tes3mp.LogMessage(enumerations.log.ERROR, "Could not load " .. fileName .. " using Lua CJSON " ..
                    "due to improperly formatted JSON! Error:\n" .. result .. "\n" .. fileName .. " is being read " ..
                    "via the slower dkjson instead.")
            end
        end

        return dkjson.decode(content)
    else
        return nil
    end
end


function jsonInterface.writeAtomic(fileName, content)

    if jsonInterface.ioLibrary == nil then
        tes3mp.LogMessage(enumerations.log.ERROR, jsonInterface.libraryMissingMessage)
        return false
    end

    -- Write a complete temporary file before replacing the previous save. A
    -- Windows rename may require moving the old destination aside first.
    local path = config.dataPath .. "/" .. fileName
    if type(tes3mp.WriteFileUtf8)=="function" and type(tes3mp.ReplaceFileAtomic)=="function" then
        if not tes3mp.WriteFileUtf8(path..".tmp",content) then return false end
        return tes3mp.ReplaceFileAtomic(path..".tmp",path)
    end
    local file = jsonInterface.ioLibrary.open(path .. ".tmp", "w+b")
    if file == nil then return false end
    local ok, result = pcall(function()
        assert(file:write(content))
        assert(file:flush())
        assert(file:close())
        return true
    end)
    if not ok or not result then pcall(function() file:close() end); return false end
    if type(tes3mp.ReplaceFileAtomic) == "function" then
        -- The native UTF-8 operation replaces the destination atomically on
        -- Windows as well; never fall back to a non-atomic overwrite on failure.
        return tes3mp.ReplaceFileAtomic(path .. ".tmp", path)
    end
    local rename = jsonInterface.ioLibrary.rename or os.rename
    local remove = jsonInterface.ioLibrary.remove or os.remove
    if rename(path .. ".tmp", path) then return true end
    local previous = jsonInterface.ioLibrary.open(path, "r")
    if previous == nil then return false end
    previous:close()
    remove(path .. ".bak")
    if not rename(path, path .. ".bak") then return false end
    if rename(path .. ".tmp", path) then remove(path .. ".bak"); return true end
    rename(path .. ".bak", path)
    return false
end

function jsonInterface.writeToFile(fileName, content)

    if jsonInterface.ioLibrary == nil then
        tes3mp.LogMessage(enumerations.log.ERROR, jsonInterface.libraryMissingMessage)
        return false
    end

    local home = config.dataPath .. "/"
    if type(tes3mp.WriteFileUtf8)=="function" then return tes3mp.WriteFileUtf8(home..fileName,content) end
    local file = jsonInterface.ioLibrary.open(home .. fileName, 'w+b')

    if file ~= nil then
        file:write(content)
        file:close()
        return true
    else
        return false
    end
end


-- Save data to JSON in a slower but human-readable way, with identation and a specific order
-- to the keys, provided via dkjson
function jsonInterface.save(fileName, data, keyOrderArray)

    local content = dkjson.encode(data, { indent = true, keyorder = keyOrderArray })

    return jsonInterface.writeToFile(fileName, content)
end

-- Save data to JSON in a fast but minimized way, provided via Lua CJSON, ideal for large files
-- that need to be saved over and over
function jsonInterface.quicksave(fileName, data)

    if cjsonExists then
        local content = cjson.encode(data)
        return jsonInterface.writeToFile(fileName, content)
    else
        return jsonInterface.save(fileName, data)
    end
end

function jsonInterface.saveAtomic(fileName, data, keyOrderArray)
    return jsonInterface.writeAtomic(fileName, dkjson.encode(data, {indent=true, keyorder=keyOrderArray}))
end

function jsonInterface.quicksaveAtomic(fileName, data)
    if cjsonExists then return jsonInterface.writeAtomic(fileName, cjson.encode(data)) end
    return jsonInterface.saveAtomic(fileName, data)
end

return jsonInterface
