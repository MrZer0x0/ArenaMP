local root = 'builder/ArenaMP_Builder/server/scripts/'
local probe=io.open(root..'houseHelper.lua','r')
if probe then probe:close() else root='server/scripts/' end
config = {houseSystem = {}, dataPath = '/tmp/arena-tests'}
package.loaded.config = config
local saved
package.loaded.jsonInterface = {load = function() return saved end, save = function(_, data) saved=data end}
package.loaded.fileHelper = {fixFilename=function(s) return s end}
package.loaded.inventoryHelper = {
 removeClosestItem=function(inv, _, n) inv[1].count=inv[1].count-n end,
 addItem=function(inv, _, n) inv[1].count=inv[1].count+n end}
package.loaded.packetBuilder = {AddCellRecord=function() end}
tableHelper = {cleanNils=function() end, concatenateFromIndex=function(t,i) return table.concat(t,' ',i) end}
local handlers, validators, commands = {}, {}, {}
customEventHooks = {
 registerHandler=function(k,f) handlers[k]=handlers[k] or {};table.insert(handlers[k],f) end,
 registerValidator=function(k,f) validators[k]=validators[k] or {};table.insert(validators[k],f) end,
 makeEventStatus=function(a,b) return {validDefaultHandler=a,validCustomHandlers=b} end}
customCommandHooks={registerCommand=function(k,f) commands[k]=f end}
enumerations={log={INFO=1,WARN=2,ERROR=3}, inventory={ADD=1,REMOVE=2},recordType={CELL=1},containerSub={REPLY_TO_REQUEST=4}}
local cells, messages, dialogs, resets = {}, {}, {}, {}
tes3mp=setmetatable({
 GetCell=function(pid) return cells[pid] end,
 SetCell=function(pid,cell) cells[pid]=cell end,
 SendMessage=function(pid,msg) table.insert(messages,{pid,msg}) end,
 MessageBox=function(pid,id,text) table.insert(messages,{pid,text}); dialogs[pid]={id,text} end,
 ListBox=function(pid,id,text,opts) dialogs[pid]={id,text,opts} end,
 CustomMessageBox=function(pid,id,text,opts) dialogs[pid]={id,text,opts} end,
 InputDialog=function(pid,id,text) dialogs[pid]={id,text} end,
 GetObjectListContainerSubAction=function() return 2 end
}, {__index=function() return function() end end})
Players={}
for id,name in ipairs({'Alice','Bob','Carol','Dan'}) do
 Players[id]={accountName=name,language='RU',data={stats={level=50},settings={staffRank=2},inventory={{refId='gold_001',count=1000000}},location={cell='0, 0',posX=1,posY=2,posZ=3}},IsLoggedIn=function() return true end,LoadItemChanges=function() end}
 cells[id]='0, 0'
end
RecordStores={cell={data={permanentRecords={}},Save=function() end}}
periodicCellResets={fullResetCell=function(cell) table.insert(resets,cell);return true end}
tableHelper.deepCopy=dofile('server/lib/lua/tableHelper.lua').deepCopy
local h=dofile(root..'houseHelper.lua')
local function gui(pid,id,data) for _,f in ipairs(handlers.OnGUIAction) do f({},pid,id,tostring(data)) end end
local function hasMessage(text) for _,m in ipairs(messages) do if m[2]:find(text,1,true) then return true end end end

local function snapshot()
    messages={}
    for _,property in pairs(saved.properties) do
        for _,member in ipairs(property.cells) do saved.doors[member]={'0, 0'} end
    end
    h.SendClientState(1)
    local result, expected, ended = '', nil, false
    for _,message in ipairs(messages) do
        local wire=message[2]
        assert(#wire < 65536, 'wire length overflow')
        local payload=wire:sub(#'@@AMP_HOUSE@@'+1,-2)
        if payload:sub(1,12)=='STATE_BEGIN\t' then
            assert(expected==nil)
            expected=tonumber(payload:sub(13))
        elseif payload:sub(1,11)=='STATE_PART\t' then
            assert(expected and not ended)
            result=result..payload:sub(12)
        elseif payload=='STATE_END' then
            assert(expected==#result)
            ended=true
        elseif payload=='STATE' or payload:sub(1,6)=='STATE\t' or payload:sub(1,11)=='CELL_STATE\t' then
            assert(expected==nil)
            result=payload
        end
    end
    assert(expected==nil or ended, 'incomplete snapshot')
    result=result:gsub('^CELL_STATE[^\t]*\t[^\t]*\t','')
    local found={}
    for record in result:gmatch('[^\t]+') do
        local cell=record:match('^(.-)%^')
        if cell then assert(not found[cell]); found[cell]=true end
    end
    local count=0
    for _,property in pairs(saved.properties) do
        if property.enabled~=false then
            for _,cell in ipairs(property.cells) do
                assert(found[cell], 'missing property cell '..cell)
                count=count+1
            end
        end
    end
    return result, count, expected~=nil
end
saved={properties={['Small House']={cell='Small House',cells={'Small House'},price=100}},doors={}}
local text,count,chunked=snapshot()
assert(count==1 and not chunked)
for i=1,500 do
    local cell='Город, Тестовый дом '..i
    saved.properties[cell]={cell=cell,cells={cell,cell..', Подвал'},price=100,title='Дом \\ сад ^ кириллица '..i}
end
text,count,chunked=snapshot()
assert(count==1001 and chunked and #text>65535)
saved.properties['Small House'].owner='Alice'
text,count,chunked=snapshot()
assert(count==1001 and chunked)
-- Repeated full snapshots must retain every entry, including the first and last.
local again,againCount=snapshot()
assert(text==again and count==againCount)
print('PASS: small/large registries, owner state, UTF-8, 1001 rooms, repeat sync, bounded packets')
