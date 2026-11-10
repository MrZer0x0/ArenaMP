-- Функция проверки существования файла
function file_exists(fileName)
    local f = io.open(fileName, "r")
    if f then f:close() return true end
    return false
end

-- Функция чтения содержимого файла
function read_file_content(fileName)
    local content = ""
    for line in io.lines(fileName) do 
        content = content..line.."\n"
    end
    return content
end

-- Основная функция обработки
local function OnServerPostInit(eventStatus)
    local scriptStore = RecordStores["script"]
    local scriptPath = tes3mp.GetModDir().."/custom/MWScripts/"
    local doSave = false
    
    -- Получаем список файлов в директории
    local handle = io.popen('dir "'..scriptPath..'" /b')
    local result = handle:read("*a")
    handle:close()
    
    -- Обрабатываем каждый файл .es3
    for fileName in result:gmatch("[^\r\n]+") do
        if fileName:match("%.es3$") then
            local scriptName = fileName:gsub("%.es3$", "")
            local fullPath = scriptPath..fileName
            
            if file_exists(fullPath) then
                local scriptContent = read_file_content(fullPath)
                scriptStore.data.permanentRecords[scriptName] = {
                    scriptText = scriptContent
                }
                doSave = true
                print("Added script: "..scriptName)
            end
        end
    end
    
    if doSave then
        scriptStore:Save()
    end
end

customEventHooks.registerHandler("OnServerPostInit", OnServerPostInit)