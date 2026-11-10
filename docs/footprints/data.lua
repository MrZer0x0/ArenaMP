-- MWSE Footprints (data.lua + the lists in main.lua)
local data = {}

-- meshes/abot/fp/<type>_<side>.nif.
data.MESH_DIR = 'meshes/abot/fp/'

data.fpStaTypes = { 'anml', 'boot', 'boot2', 'foot', 'guar', 'hoof',
    'mech', 'mech2', 'paw', 'rat', 'rept', 'skel', 'taln' }

-- mesh id ('foot_l') -> true
data.meshSet = {}
for _, t in ipairs(data.fpStaTypes) do
    data.meshSet[t .. '_l'] = true
    data.meshSet[t .. '_r'] = true
end

-- recognized creature mesh base names
data.creStaTypes = {
    ['almelexia'] = 'foot',
    ['almelexia_warrior'] = 'foot',
    ['ashghoul'] = 'foot',
    ['ashvampire'] = 'foot',
    ['ashzombie'] = 'foot',
    ['atro_storm_monarch'] = 'boot2',
    ['atronach_fire'] = 'boot2',
    ['atronach_frost'] = 'boot',
    ['atronach_storm'] = 'boot',
    ['bear_black_larger'] = 'paw',
    ['bear_blond_larger'] = 'paw',
    ['bear_brown_larger'] = 'paw',
    ['bonewalker'] = 'foot',
    ['byagram'] = 'taln',
    ['cavemudcrab'] = 'taln',
    ['centurionspider'] = 'taln',
    ['clannfear'] = 'rept',
    ['clannfear_daddy'] = 'paw',
    ['corprus_stalker'] = 'foot',
    ['cr_draugr'] = 'boot2',
    ['daedroth'] = 'foot',
    ['dagothr'] = 'boot',
    ['draugrlord'] = 'boot2',
    ['dremora'] = 'boot2',
    ['durzog'] = 'rept',
    ['durzog_collar'] = 'rept',
    ['duskyalit'] = 'rept',
    ['fabricant'] = 'mech',
    ['fabricant_hulking'] = 'taln',
    ['fabricant_imperfect'] = 'boot',
    ['frostgiant'] = 'paw',
    ['goblin01'] = 'rept',
    ['goblin02'] = 'rept',
    ['goblin03'] = 'rept',
    ['golden saint'] = 'boot2',
    ['greatbonewalker'] = 'boot',
    ['guar'] = 'guar',
    ['guar_white'] = 'guar',
    ['guar_withpack'] = 'guar',
    ['hircine'] = 'boot',
    ['hircine_bear_larger'] = 'paw',
    ['hircinewolf'] = 'paw',
    ['hunger'] = 'mech',
    ['ice troll'] = 'paw',
    ['iceminion'] = 'boot',
    ['icemraider'] = 'hoof',
    ['kwama queen'] = 'rept',
    ['kwama warior'] = 'rept',
    ['kwama worker'] = 'rept',
    ['lame_corprus'] = 'foot',
    ['leastkagouti'] = 'rept',
    ['liche_king'] = 'skel',
    ['lordvivec'] = 'foot',
    ['minescrib'] = 'taln',
    ['mount'] = 'hoof',
    ['nixhound'] = 'rept',
    ['packrat'] = 'rat',
    ['rust rat'] = 'rat',
    ['scamp_fetch'] = 'rept',
    ['scrib'] = 'taln',
    ['shalk'] = 'taln',
    ['skeleton'] = 'skel',
    ['skinnpc'] = 'paw',
    ['steam_centurions'] = 'mech2',
    ['tr_siltstrider_vo'] = 'taln',
    ['udyrfrykte'] = 'paw',
    ['undeadwolf_2'] = 'paw',
    ['war_wt_armor'] = 'rept',
    ['war_wt_glowy'] = 'rept',
    ['war_wt_grunda'] = 'rept',
    ['wingedtwilight'] = 'rept',
    ['wolf_black'] = 'paw',
    ['wolf_red'] = 'paw',
    ['wolf_white'] = 'paw',
}

data.texWhitelist = { 'grass', 'dirt', 'snow', 'ice', 'sand', 'ash', 'scrub', 'grave', 'mud', 'moss', 'salt', 'muck' }
data.texDirtyWhitelist = { 'dirt', 'snow', 'ice', 'sand', 'ash', 'mud', 'salt', 'muck' }
data.texBlacklist = { 'glass' }
data.meshBlacklist = { 'water', 'tree', 'ex_v_ban' }
-- weather display names that make feet dirty
data.badWeatherNames = {
    ['Rain']         = true,
    ['Thunderstorm'] = true,
    ['Ashstorm']     = true,
    ['Blight']       = true,
    ['Snow']         = true,
    ['Blizzard']     = true,
}
-- staircase model keywords
data.stairWords = { 'stair', 'stariway', 'steps' }
-- plain substrings
data.dummyWords = { 'dummy', 'mannequin', 'manequin', 'target', 'invis' }

-- first word of `list` found in `s` (plain find), or nil
function data.findAny(s, list)
    for i = 1, #list do
        if s:find(list[i], 1, true) then return list[i] end
    end
end

-- 'Meshes\\R\\Guar.NIF' -> 'guar'
function data.baseName(path)
    local p = path:lower():gsub('\\', '/')
    return p:match('([^/]+)%.%w+$') or p:match('([^/]+)$') or p
end

-- footprint type of a creature model, nil for creatures without footprints
function data.creatureType(model)
    return data.creStaTypes[data.baseName(model or '')]
end

-- practice dummies, mannequins, archery targets, invisible helpers
function data.isDummy(id, name, model)
    local s = (id or ''):lower() .. '|' .. (name or ''):lower() .. '|' .. (model or ''):lower()
    return data.findAny(s, data.dummyWords) ~= nil
end

-- nil = no footprints, 1 = footprints, 2 = footprints + dirty feet, 3 = lava crust (same as 2)
function data.classifyTexture(tex)
    if tex:find('lava', 1, true) then
        return tex:find('crust', 1, true) and 3 or nil
    end
    if data.findAny(tex, data.texBlacklist) then return nil end
    if data.findAny(tex, data.texDirtyWhitelist) then return 2 end
    if data.findAny(tex, data.texWhitelist) then return 1 end
    return nil
end

return data