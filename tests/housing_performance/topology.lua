-- Run from repository root: luajit tests/housing_performance/topology.lua [helper]
local registry={properties={},doors={}}
for i=1,1739 do
 local cell='House '..i
 registry.properties[cell]={cell=cell,cells={cell,cell..' Basement'},price=8000,priceMode='manual',
 owner=i<=10 and ('owner'..i) or nil,locked=true,coOwners={'coowner'},friends={'friend'}}
 registry.doors[cell]={'Street '..i}
end
registry.properties['Дом']={cells={'Дом','Подвал'},price=8000}
config={houseDynamicPrices=false}
package.preload.config=function() return config end
package.preload.jsonInterface=function() return {load=function() return registry end,save=function() return true end} end
for _,name in ipairs({'fileHelper','inventoryHelper','packetBuilder'}) do package.preload[name]=function() return {} end end
customEventHooks={registerHandler=function() end,registerValidator=function() end}
customCommandHooks={registerCommand=function() end}
enumerations={log={INFO=1,WARN=2,ERROR=3}}
tableHelper={containsValue=function(t,v) for _,x in pairs(t) do if x==v then return true end end return false end}
local current='Street 1'
local messages={}
tes3mp={GetCell=function() return current end,SendMessage=function(_,msg) messages[#messages+1]=msg end,LogAppend=function() end,LogMessage=function() end}
Players={[1]={accountName='guest',language='EN',data={settings={},stats={level=100}},IsLoggedIn=function() return true end}}
LoadedCells={}
local h=dofile(arg[1] or 'server/scripts/houseHelper.lua')
-- Isolate transport-independent home-summary formatting; exercise full door/room state.
h.SendHomeState=function() end
h.RefreshAll=function() end
local function snapshot(label)
 messages={};h.SendClientState(1)
 -- Packet order among independent rooms is not protocol-significant.
 table.sort(messages)
 io.write(label,'\n',table.concat(messages))
end
assert(h.GetProperty('hOuSe 1 basement')==registry.properties['House 1'])
assert(h.GetProperty('ПОДВАЛ')==registry.properties['Дом'])
assert(h.GetProperty('missing')==nil)
for _,name in ipairs({'guest','owner1','coowner','friend'}) do
 Players[1].accountName=name;snapshot(name)
end
Players[1].accountName='guest'
assert(h.SetEnabled('House 1',false));snapshot('disabled')
assert(h.SetEnabled('House 1',true));assert(h.SetPrice('House 1',12345))
registry.properties['House 1'].friends={'guest'} -- permissions must never be cached
snapshot('new friend')
assert(h.LinkCell('House 1','Annex'))
assert(h.GetProperty('annex')==registry.properties['House 1'])
current='Annex';snapshot('linked room')
assert(h.Unregister('House 1'));assert(h.GetProperty('annex')==nil);snapshot('removed')
assert(h.Register('New House','house',1200,false,1,true));assert(h.GetProperty('NEW HOUSE'))
current='New House';snapshot('registered')
-- Warm lookups and full login state against a large mostly unowned registry.
local start=os.clock()
for i=1,200 do h.GetProperty('not a house');h.GetProperty('HOUSE 2 BASEMENT');messages={};h.SendClientState(1) end
io.stderr:write(string.format('200 lookup/state iterations: %.3f ms\n',(os.clock()-start)*1000))
