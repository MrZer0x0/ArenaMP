-- Enumerate persisted cell JSON files without guessing their original cell names.
local M = {}
local alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'
local function base64(value)
    local out = {}
    for i = 1, #value, 3 do
        local a, b, c = value:byte(i, i + 2)
        local n = a * 65536 + (b or 0) * 256 + (c or 0)
        out[#out + 1] = alphabet:sub(math.floor(n / 262144) % 64 + 1, math.floor(n / 262144) % 64 + 1)
        out[#out + 1] = alphabet:sub(math.floor(n / 4096) % 64 + 1, math.floor(n / 4096) % 64 + 1)
        out[#out + 1] = b and alphabet:sub(math.floor(n / 64) % 64 + 1, math.floor(n / 64) % 64 + 1) or '='
        out[#out + 1] = c and alphabet:sub(n % 64 + 1, n % 64 + 1) or '='
    end
    return table.concat(out)
end

function M.List(directory)
    local names = {}
    local function add(name)
        if type(name) == 'string' and name:lower():match('%.json$')
            and not name:find('[/\\\r\n]') then names[#names + 1] = name end
    end
    -- LuaFileSystem, where installed, avoids spawning a process entirely.
    local ok, lfs = pcall(require, 'lfs')
    if ok then
        ok = pcall(function()
            for name in lfs.dir(directory) do
                if lfs.attributes(directory .. '/' .. name, 'mode') == 'file' then add(name) end
            end
        end)
        if ok then table.sort(names); return names, true end
        names = {}
    end
    local command
    if tes3mp.GetOperatingSystemType() == 'Windows' then
        -- ASCII command, literal path decoded inside PowerShell, UTF-8 output:
        -- works with Cyrillic profiles without cmd's OEM/ANSI filename conversion.
        local path = base64(directory)
        command = 'powershell.exe -NoLogo -NoProfile -NonInteractive -Command "' ..
            '[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; ' ..
            '$p=[System.Text.Encoding]::UTF8.GetString([Convert]::FromBase64String(\'' .. path .. '\')); ' ..
            'try { Get-ChildItem -LiteralPath $p -File -ErrorAction Stop | ForEach-Object { $_.Name }; exit 0 } catch { exit 1 }"'
    else
        local quoted = "'" .. directory:gsub("'", "'\\''") .. "'"
        -- POSIX find also works on Android (no GNU -printf dependency).
        command = 'find ' .. quoted .. ' -maxdepth 1 -type f -print'
    end
    local success = false
    ok = pcall(function()
        local pipe = io.popen(command, 'r')
        if not pipe then return end
        for line in pipe:lines() do
            line = line:gsub('^\239\187\191', ''):gsub('\r$', '')
            if tes3mp.GetOperatingSystemType() ~= 'Windows' then line = line:match('([^/]+)$') end
            add(line)
        end
        local closed = pipe:close()
        success = closed == true or closed == 0
    end)
    table.sort(names)
    return names, ok and success
end
return M
