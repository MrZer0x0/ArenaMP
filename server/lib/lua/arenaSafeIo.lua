-- Safe replacement for TES3MP's 2019 io2.dll on Windows. This module never
-- loads the native library. Use normal Lua io for relative and UTF-8 paths;
-- on Russian Windows ACP 1251 also try a CP1251 spelling of Cyrillic names.
-- No shell commands, temporary files, or destructive write probes.
local M = {}
local function ansi1251(path)
    if type(path) ~= "string" then return nil end
    local out, changed, i = {}, false, 1
    while i <= #path do
        local a = path:byte(i)
        local b = path:byte(i + 1)
        if a == 0xD0 and b and b >= 0x90 and b <= 0xBF then
            -- U+0410..U+043F => 0xC0..0xEF, excluding earlier Cyrillic chars.
            out[#out+1] = string.char(b + 0x30)
            changed = true
            i = i + 2
        elseif a == 0xD1 and b and b >= 0x80 and b <= 0x8F then
            -- U+0440..U+044F => 0xF0..0xFF.
            out[#out+1] = string.char(b + 0x70)
            changed = true
            i = i + 2
        elseif a == 0xD0 and b == 0x81 then
            out[#out+1] = string.char(0xA8); changed = true; i = i + 2
        elseif a == 0xD1 and b == 0x91 then
            out[#out+1] = string.char(0xB8); changed = true; i = i + 2
        else
            out[#out+1] = string.char(a)
            i = i + 1
        end
    end
    return changed and table.concat(out) or nil
end
function M.open(path, mode)
    local file, err = io.open(path, mode)
    if file then return file end
    -- Do not try alternate encodings for writes if an existing UTF-8 path was
    -- opened successfully above; never alter the requested mode.
    local ansi = ansi1251(path)
    if ansi then
        local candidate, alternateError = io.open(ansi, mode)
        if candidate then return candidate end
        return nil, alternateError or err
    end
    return nil, err
end
function M.remove(path)
    local ok, err = os.remove(path)
    if ok then return ok end
    local ansi = ansi1251(path)
    if ansi then return os.remove(ansi) end
    return nil, err
end
function M.rename(oldPath, newPath)
    local ok, err = os.rename(oldPath, newPath)
    if ok then return ok end
    local left, right = ansi1251(oldPath), ansi1251(newPath)
    if left or right then return os.rename(left or oldPath, right or newPath) end
    return nil, err
end
M.close = io.close
M.flush = io.flush
M.input = io.input
M.output = io.output
M.lines = io.lines
M.read = io.read
M.write = io.write
M.tmpfile = io.tmpfile
M.type = io.type
return M
