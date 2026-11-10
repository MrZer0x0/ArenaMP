-- Real binary TES3 fixtures exercise canonical references, not a mocked parser.
local checks=0
local function check(v) assert(v);checks=checks+1 end
local function sub(t,b) return t..string.pack('<I4',#b)..b end
local function rec(t,b) return t..string.pack('<I4I4I4',#b,0,0)..b end
local function cell(name,refs) return rec('CELL',sub('NAME',name..'\0')..sub('DATA',string.pack('<I4I4I4',1,0,0))..refs) end
local function ref(n,count) return sub('FRMR',string.pack('<I4',n))..sub('NAME','sword\0')..sub('NAM9',string.pack('<I4',count))..sub('DATA',string.pack('<ffffff',1,2,3,0,0,0)) end
local tmp=os.tmpname();os.remove(tmp)
local base=tmp..'Base.esm';local patch=tmp..'Patch.esp'
local f=assert(io.open(base,'wb'));f:write(rec('TES3','')..cell('',ref(90,1))..cell('bad\nname',ref(91,1))..cell('Keep',ref(42,1))..cell('Gone',ref(43,1))..cell('Moved',ref(44,1))..cell('DeletedRoom',ref(45,1)));f:close()
f=assert(io.open(patch,'wb'));f:write(rec('TES3',sub('MAST','Base.esm\0'))..cell('Keep',ref(16777216+42,3))..cell('Gone',ref(16777216+43,1)..sub('DELE',string.pack('<I4',0)))..cell('Moved',sub('MVRF',string.pack('<I4',16777216+44))..sub('CNDT',string.pack('<ii',0,0)))..cell('DeletedRoom',sub('DELE',string.pack('<I4',0))));f:close()
package.loaded.jsonInterface={load=function() return {files={['Base.esm']=base,['Patch.esp']=patch}} end}
tableHelper=dofile('server/lib/lua/tableHelper.lua')
local catalog=dofile('server/scripts/arenaFurnitureAutoCatalog.lua')
local json={load=function() return {{['Base.esm']={}},{['Patch.esp']={}}} end}
local r=catalog.findInteriorReference({},json,'Keep','42-0','sword')
check(r and r.count==3 and r.location.posZ==3)
check(catalog.findInteriorReference({},json,'Gone','43-0','sword')==nil)
check(catalog.findInteriorReference({},json,'Moved','44-0','sword')==nil)
check(catalog.findInteriorReference({},json,'DeletedRoom','45-0','sword')==nil)
check(catalog.findInteriorReference({},json,'Keep','42-0','fake')==nil)
r.count=99;check(catalog.findInteriorReference({},json,'Keep','42-0','sword').count==3)
os.remove(base);os.remove(patch)
print('TES3 item references: '..checks..' assertions passed')
