-- Auto furniture detection regression; synthetic TES3 record files, no price JSON.
package.path = "server/scripts/?.lua;" .. package.path
local auto=require("arenaFurnitureAutoCatalog")
local function u32(n)
    local b={}
    for i=1,4 do b[i]=string.char(n%256); n=math.floor(n/256) end
    return table.concat(b)
end
local function sub(k,v) return k..u32(#v)..v end
local function record(k,subs) return k..u32(#subs)..string.rep("\0",8)..subs end
local function chair(name,id,script)
    return record("STAT",sub("NAME",id.."\0")..sub("MODL","f\\furn_chair_01.nif\0")..
        sub("FNAM",name.."\0")..(script and sub("SCRI",script.."\0") or ""))
end
local function chest(id, hasLoot)
    return record("CONT",sub("NAME",id.."\0")..sub("MODL","f\\furn_chest_01.nif\0")..
        sub("FNAM",string.char(0xC0).."\0")..sub("CNDT",string.char(0,0,200,66))..sub("FLAG",u32(8))..
        (hasLoot and sub("NPCO",u32(1).."gold_001"..string.rep("\0",24)) or ""))
end
local folder="tests/housing/"
local function write(name,bytes)
    local f=assert(io.open(folder..name,"wb")); assert(f:write(bytes));f:close()
end
write("H003_TEST.esm",record("TES3","")..chair("Simple chair","furn_chair_test")..
    chair("Wall part","furn_chair_wall")..chair("Scripted chair","furn_chair_scripted","some_script")..
    chest("furn_chest_test")..chest("furn_chest_loot",true)..record("DOOR",sub("NAME","furn_door\0")..sub("MODL","f\\furn_chair_01.nif\0")))
write("H003_OVERRIDE.esp",chair("Scripted override","furn_chair_test","dangerous_script")..
    chair("Fine chair","furn_chair_new")..record("STAT",sub("NAME","furn_chair_deleted\0")..sub("DELE","")))
local config={furnitureDataDirs={folder}}
local json={load=function(filename)
    if filename=="requiredDataFiles.json" then return {content={{["H003_TEST.esm"]={"0x0"}}}} end
    return nil
end}
local first,err=auto.build(config,json)
assert(first,err)
local names={}
for _,item in ipairs(first.items) do names[item.refId]=item end
assert(names.furn_chair_test and names.furn_chair_test.category=="Сиденья")
assert(names.furn_chair_test.price>=25 and names.furn_chair_test.price<=5000)
assert(names.furn_chest_test and names.furn_chest_test.category=="Контейнеры")
assert(not names.furn_chair_wall and not names.furn_chair_scripted and not names.furn_door and not names.furn_chest_loot)
assert(names.furn_chest_test.name=="А", "legacy CP1251 name must be converted into UTF-8")
local safeMiscBed=auto.referenceBedCandidate({
    kind="MISC",refId="furn_bed_test",model="f\\furn_bed_test.nif",name="Test bed",value=50
})
assert(safeMiscBed and safeMiscBed.category=="Кровати",
    "curated bed MISC records with a safe model must be accepted")
assert(auto.referenceBedCandidate({
    kind="MISC",refId="furn_bed_scripted",model="f\\furn_bed_test.nif",
    name="Scripted bed",script="bed_sleep_script",value=50
}), "curated scripted MISC bed must be accepted with its sleep script")
assert(auto.referenceBedCandidate({
    kind="ACTI",refId="active_de_r_bed_17",model="f\\furn_bed_01.nif",
    name="Dunmer bed",script="bed_sleep_script"
}), "curated scripted ACTI bed must be accepted with its sleep script")
assert(not auto.referenceBedCandidate({
    kind="ACTI",refId="furn_quest",model="f\\quest_switch.nif",
    name="Quest lever",script="quest_script"
}), "non-bed scripted activators must remain excluded")
assert(not auto.manualCandidate({
    kind="ACTI",refId="furn_quest",model="f\\quest_switch.nif",
    name="Quest lever",script="quest_script"
}), "ordinary manual capture must still reject scripted quest objects")
assert(not auto.referenceBedCandidate({
    kind="MISC",refId="misc_gold_001",model="m\\gold_001.nif",name="Gold",value=1
}), "ordinary MISC loot must not become a bed")
local second=auto.build(config,{load=function(filename)
    if filename=="requiredDataFiles.json" then
        return {content={{["H003_TEST.esm"]={"0x0"}},{["H003_OVERRIDE.esp"]={"0x0"}}}}
    end
    return nil
end})
local overrides={}
for _,v in ipairs(second.items) do overrides[v.refId]=v end
assert(not overrides.furn_chair_test, "scripted override must remove previously permitted static")
assert(overrides.furn_chair_new, "later plugin new models must appear automatically")
assert(not overrides.furn_chair_deleted, "deleted records cannot be offered")
-- H012: manual staff capture must read the same active plugin manifest and
-- honor later overrides instead of trusting a client-provided refId or price.
local active={load=function(path)
    if path=="requiredDataFiles.json" then return {content={
        {["H003_TEST.esm"]={"0x0"}}, {["H003_OVERRIDE.esp"]={"0x0"}} }} end
end}
local captured, captureErr=auto.findActiveRecord(config,active,"furn_chair_new")
assert(captured and not captureErr and auto.manualCandidate(captured), "staff can capture real active furniture")
local raw=assert(auto.findActiveRecords(config,active,{furn_chair_new=true,furn_chair_test=true,furn_chair_wall=true}))
assert(raw.furn_chair_new and raw.furn_chair_test and not auto.manualCandidate(raw.furn_chair_test), "later scripted override blocks capture")
assert(raw.furn_chair_wall and not auto.manualCandidate(raw.furn_chair_wall), "architecture cannot be captured")
local unknown=auto.findActiveRecord(config,active,"fabricated_chair")
assert(unknown==nil, "a made-up refId cannot be captured")
local missing,why=auto.build({furnitureDataDirs={"missing/nonexistent/"}},json)
assert(missing==nil and why:find("not readable"), "unavailable ESM must fail closed")
write("H003_BROKEN.esm",record("STAT",sub("NAME","furn_chair_bad\0")):sub(1,-4))
local broken,why2=auto.build(config,{load=function(filename)
    if filename=="requiredDataFiles.json" then return {content={{["H003_BROKEN.esm"]={"0x0"}}}} end
end})
assert(broken==nil and why2:find("error reading"),"truncated file must not create a partial shop")
for _,name in ipairs({"H003_TEST.esm","H003_OVERRIDE.esp","H003_BROKEN.esm"}) do os.remove(folder..name) end
print("PASS H003: auto ESM/ESP records, generated prices, exclusions, mod overrides, UTF-8 fallback, fail-closed")
