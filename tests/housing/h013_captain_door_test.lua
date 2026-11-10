local configFile = assert(io.open('server/scripts/config.lua', 'r'))
local configText = configFile:read('*a')
configFile:close()

local disabledBlock = assert(configText:match('config%.disabledClientScriptIds%s*=%s*{(.-)}'))
assert(not disabledBlock:find('"CharGenDoorEnterCaptain"', 1, true),
    'CharGenDoorEnterCaptain must remain enabled in config.disabledClientScriptIds')

local override = io.open('server/data/custom/MWScripts/CharGenDoorEnterCaptain.es3', 'r')
assert(override == nil, 'CharGenDoorEnterCaptain.es3 override must not be shipped')
if override then override:close() end

print('PASS: CharGenDoorEnterCaptain remains enabled in config and has no .es3 override')
