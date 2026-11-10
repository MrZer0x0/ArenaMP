dofile('tests/housing/z012_original_test.lua')
local h=package.loaded.houseHelper
Players[1].data.settings.staffRank=0
config.houseEnabled=true
config.houseAutoGeneration=nil;config.houseGenerationLevel=nil
assert(h.Observe('Default House',1))
assert(not h.Observe('Default Tavern',1))
config.houseAutoGeneration='admins'
assert(not h.Observe('Player House',1))
assert(not h.Observe('Unknown House',nil))
assert(h.Observe('Admin House',4))
assert(not h.Register('Scan House',nil,nil,true,1))
assert(h.Register('Admin Scan House',nil,nil,true,4))
config.houseGenerationLevel='houses_taverns'
assert(h.Observe('Admin Tavern',4));assert(not h.Observe('Admin Guild',4))
config.houseGenerationLevel='houses_taverns_guilds'
assert(h.Observe('Admin Guild',4));assert(not h.Observe('Admin Shop',4))
config.houseAutoGeneration='off'
assert(not h.Observe('Off House',4))
assert(not h.Register('Off Scan House',nil,nil,true,4))
assert(h.Register('Manual Shop','shop',200,false))
assert(h.Observe('Admin House',1)) -- Existing properties remain usable.
config.houseEnabled=false;assert(not h.Observe('Disabled House',4))
config.houseEnabled=true;config.houseAutoGeneration='players'
local p=h.Register('Stable House','house',100,false)
assert(h.GetInstanceDescription(p.cell,'Alice')=='Stable House - home')
assert(h.GetInstanceDescription(p.cell,'Bob')=='Stable House - home')
assert(h.GetBaseCellForInstance('Stable House - home')=='Stable House')
assert(not h.ClassifyCellName('Stable House - home'))
p.owner='Alice';p.instanceOwner='Alice';p.instanceNaming='legacy'
assert(h.GetInstanceDescription(p.cell,'Alice')=='Stable House - Home of Alice')
assert(h.GetBaseCellForInstance('Stable House - Home of Alice')=='Stable House')
p.instanceNaming='home';p.owner='Bob'
assert(h.GetInstanceOwner('Stable House - home')=='Bob')
print('PASS Z014: player/admin/off modes, categories, scan authorization, manual override, new and legacy instance names')
config.houseGenerateHouses=false;config.houseGenerateTaverns=true;config.houseGenerateGuilds=false;config.houseGenerateShops=true
assert(not h.Observe('Deselected House',1))
assert(h.Observe('Selected Tavern',1))
assert(not h.Observe('Deselected Guild',1))
assert(h.Observe('Selected Shop',1))
config.houseDynamicPrices=true
config.houseInteriorProfiles={['Price House']={objects=0,containers=0,beds=0,doors=1}}
local low=h.CalculatePrice('Price House','house')
config.houseInteriorProfiles['Price House']={objects=50,containers=6,beds=2,doors=4}
local high=h.CalculatePrice('Price House','house');assert(high>low)
config.houseInteriorProfiles['Price House']={objects=50,containers=6,beds=2,doors=5}
assert(h.CalculatePrice('Price House','house')>high)
local priceHome=h.Register('Price House','house',555,false)
assert(priceHome.price==555 and priceHome.priceMode=='manual')
h.SetKind(priceHome.cell,'tavern');assert(priceHome.price==555)
assert(h.RecalculatePrice(priceHome.cell));assert(priceHome.price>555)
priceHome.owner='Bob';local before=priceHome.price
assert(not h.RecalculatePrice(priceHome.cell) and priceHome.price==before)
config.houseDynamicPrices=false;assert(h.CalculatePrice('Price House','house')==low)
print('PASS Z014: independent categories including shops; content/door premiums, manual price protection, no owned-home repricing')
