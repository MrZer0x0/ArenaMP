package.path='server/scripts/?.lua;server/lib/lua/?.lua;'..package.path
local n=0
local function check(v,label) assert(v,label);n=n+1 end
local json=require('dkjson')
local logs={};enumerations={log={ERROR=3,INFO=1,WARN=2}}
local manifest={content={{['One.esm']={}},{['Two.esp']={}}},groundcover={{['Grass.esp']={}}}}
jsonInterface={load=function() return manifest end}
local diskFiles={['base:One.esm']='one.ESM',['base:Two.esp']='Two.esp',['patch:Two.esp']='two.ESP'}
tes3mp={GetCaseInsensitiveFilename=function(dir,name) return diskFiles[dir..':'..name] or 'invalid' end}
local S=require('contentSources')
local options={files={},manifest='requiredDataFiles.json',dataDirectories={'base','patch'},encoding='utf8'}
local resolved=S.Resolve(options)
check(resolved.files[1]=='base/one.ESM' and resolved.files[2]=='patch/two.ESP','ordered manifest, case lookup and later data directory precedence')
check(#resolved.files==2 and #options.files==0,'groundcover excluded; options not mutated')
check(S.Resolve({files={'custom.esm'}}).files[1]=='custom.esm','explicit list independent of manifest')
check(not S.Resolve({files='bad'}),'invalid explicit file list rejected')
manifest={content={{['Missing.esm']={}}}};check(not S.Resolve(options),'missing plugin fails closed')
manifest={content={{['../One.esm']={}}}};check(not S.Resolve(options),'manifest traversal rejected')
manifest={content={{['One.esm']={},['Two.esp']={}}}};check(not S.Resolve(options),'ambiguous entry rejected')
manifest={content={[1]={['One.esm']={}},[3]={['Two.esp']={}}}};check(not S.Resolve(options),'sparse load order rejected')
manifest={content={{['One.esm']={}}}}
-- Actual content API: cache lifetime, detached results and live reload guard.
local calls,commits,fail=0,0,false
config={contentApi={enabled=false,files={'One.esm'},encoding='utf8'}}
customEventHooks={registerHandler=function() end}
Players={}
tes3mp.ContentCatalogGetVersion=function() return 1 end
tes3mp.ContentCatalogBegin=function() return true end
tes3mp.ContentCatalogAddFile=function() return true end
local nativeError='';local generation=1
local function rec(kind,id) return {type=kind,id=id,deleted=false,inventory={},slots={},generation=generation} end
tes3mp.ContentCatalogCommit=function() commits=commits+1;nativeError=fail and 'bad_file' or '';return not fail end
tes3mp.ContentCatalogGetInfo=function() nativeError='';return json.encode({contentKey='key-'..generation,records=1000}) end
tes3mp.ContentCatalogGetError=function() return nativeError end
tes3mp.ContentCatalogNormalizeId=function(v) return v:lower() end
tes3mp.ContentCatalogGetRecord=function(kind,id)
 calls=calls+1;nativeError=''
 if id=='error' then nativeError='backend_failed';return 'null' end
 return id=='missing' and 'null' or json.encode(rec(kind,id))
end
tes3mp.LogMessage=function(_,text) logs[#logs+1]=text end
local C=require('contentApi');check(C.Reload(),'initial load')
local record=C.GetRecord('NPC_','arrille');record.inventory[1]={refId='bad'}
check(C.GetRecord('NPC_','ARRILLE').inventory[1]==nil and calls==1,'cache normalizes IDs and returns detached data')
C.GetRecord('NPC_','missing');C.GetRecord('NPC_','missing');check(calls==2,'missing records cached')
fail=true;check(not C.Reload(),'failed reload');fail=false
check(C.GetRecord('NPC_','arrille').generation==1 and calls==2,'failed reload preserves previous cache')
generation=2;check(C.Reload() and C.GetRecord('NPC_','arrille').generation==2 and calls==3,'successful reload invalidates cache')
local before=commits;Players[1]={}
local _,why=C.Reload();check(why=='catalog_reload_requires_empty_server' and commits==before,'online catalog parse blocked before native work')
Players={}
for i=1,300 do C.GetRecord('MISC','item-'..i) end
before=calls;C.GetRecord('NPC_','arrille');check(calls==before+1,'cache evicts old entries at bounded capacity')
local _,why=C.GetItem('error');check(why=='backend_failed','item lookup preserves backend errors')
-- Make rock unambiguous: the stub previously supplied every type.
tes3mp.ContentCatalogGetRecord=function(kind,id) nativeError='';return kind=='MISC' and json.encode(rec(kind,id)) or (kind=='NPC_' and id=='arrille' and json.encode(rec(kind,id)) or 'null') end
C.Reload()
-- Actual actor transaction must not clone unrelated actors or mutate packet lists on failure.
local actor={refId='arrille',inventory={},equipment={}}
local other={refId='other',inventory={}}
local data={packets={container={},equipment={}},objectData={['1-0']=actor,['2-0']=other},metadata={value=1}}
LoadedCells={Home={entryFile='Home.json',data=data}}
local saved=false
jsonInterface={quicksaveAtomic=function() return saved end}
packetBuilder={GetRelevantPlayersForCell=function() return {} end,UpdateContainerForRelevantPlayers=function() end}
local _,why=C.Actors.AddItem('Home','1-0','rock',1)
check(why=='cell_save_failed' and data.objectData['1-0']==actor and next(data.packets.container)==nil,'failed save leaves original actor and packet lists intact')
saved=true
check(C.Actors.AddItem('Home','1-0','rock',1),'mutation succeeds')
check(LoadedCells.Home.data.objectData['2-0']==other and LoadedCells.Home.data.metadata==data.metadata,'unrelated cell data is shared, not recursively copied')
check(#actor.inventory==0 and next(data.packets.container)==nil,'successful copy-on-write does not mutate old cell snapshot')
local object=LoadedCells.Home.data.objectData['1-0'];object.equipment={[8]=object.inventory[1]}
C.Actors.ObserveSnapshot(object,object.equipment);local snapshot=object.arenaContentSnapshots.auto
C.Actors.ObserveSnapshot(object,object.equipment);check(object.arenaContentSnapshots.auto==snapshot,'unchanged auto outfit retains snapshot and timestamp')
-- Actual JSON native path, including write/replace/read failures.
config={dataPath='/data'};doesModuleExist=function() return false end
local disk={};local ioError='';local writeFail,replaceFail=false,false
local J=dofile('server/lib/lua/jsonInterface.lua');J.setLibrary({open=function() error('narrow IO must not run') end})
tes3mp.WriteFileUtf8=function(path,content) if writeFail then return false end;disk[path]=content;return true end
tes3mp.ReadFileUtf8=function(path) ioError=disk[path] and '' or 'missing';return disk[path] or '' end
tes3mp.GetFileIoError=function() return ioError end
tes3mp.ReplaceFileAtomic=function(src,dst) if replaceFail then return false end;disk[dst]=disk[src];disk[src]=nil;return true end
check(J.saveAtomic('player/Игрок.json',{v=1}) and J.load('player/Игрок.json').v==1,'Unicode JSON save/load through native IO')
writeFail=true;check(not J.saveAtomic('player/Игрок.json',{v=2}) and J.load('player/Игрок.json').v==1,'failed native write retains account')
writeFail=false;replaceFail=true;check(not J.saveAtomic('player/Игрок.json',{v=3}) and J.load('player/Игрок.json').v==1,'failed replace retains account')
replaceFail=false;disk['/data/player/Игрок.json.bak']=disk['/data/player/Игрок.json'];disk['/data/player/Игрок.json']=nil
check(J.load('player/Игрок.json').v==1,'Unicode backup load')
-- Actual player/json with only the engine/base class mocked.
package.loaded.config={};package.loaded['player.base']={__init=function(self,pid,name) self.pid=pid;self.accountName=name or 'Игрок';self.data={} end}
package.loaded.tableHelper={containsValue=function(t,v) for _,x in pairs(t) do if x==v then return true end end;return false end}
package.loaded.fileHelper=nil
function string.trim(s) return s:match('^%s*(.-)%s*$') end
class=function(_,base) return setmetatable({}, {__index=base}) end
package.loaded.profileHelper={IsTombstoneFile=function() return false end}
jsonInterface=J;config={dataPath='/data'}
local files={}
tes3mp.GetCaseInsensitiveFilename=function(_,name) return files[name] and name or 'invalid' end
tes3mp.GetSHA256Hash=function() return 'digest' end
tes3mp.RenameFileUtf8=function(src,dst)
 src=src:match('[^/]+$');dst=dst:match('[^/]+$')
 if files[dst] or not files[src] then return false end;files[dst]=files[src];files[src]=nil;return true
end
local P=dofile('server/scripts/player/json.lua')
local function player(name) local p=setmetatable({},{__index=P});p:__init(1,name);return p end
local p=player('Игрок');check(p.accountFile=='Игрок.json' and not p.hasAccount,'new profile uses player name')
files['_utf8_digest.json']='legacy';files['_utf8_digest.json.bak']='old-backup';p=player('Игрок');check(p.hasAccount and p.accountFile=='Игрок.json' and files['Игрок.json']=='legacy','legacy hash migrates without changing profile bytes')
check(files['_utf8_digest.json.bak']==nil and files['Игрок.json.bak']=='old-backup','migration moves legacy backup with main profile')
files['_utf8_digest.json']='different';p=player('Игрок');check(files['_utf8_digest.json']=='different' and files['Игрок.json']=='legacy','existing named account not overwritten')
files={};p=player(nil);check(p.accountFile=='Игрок.json','missing optional playerName uses base player account name')
files['_utf8_digest.json.bak']='backup';p=player('Игрок');check(p.hasAccount and p.accountFile=='Игрок.json','hashed backup recovered and migrated')
files={};local write=tes3mp.WriteFileUtf8;tes3mp.WriteFileUtf8=nil;p=player('Игрок');check(p.accountFile=='_utf8_digest.json','old binary retains safe hash fallback');tes3mp.WriteFileUtf8=write
print('PASS Y098 server: '..n..' checks')
