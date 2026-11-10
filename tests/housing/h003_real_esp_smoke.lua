package.path="server/scripts/?.lua;" .. package.path
local scanner=require("arenaFurnitureAutoCatalog")
local records={}
local count, err=scanner.scanFile("extras/encoremp/EncoreMPV093.ESP",records)
assert(count,err)
local found=0
for _,v in pairs(records) do if v then found=found+1 end end
print("PASS H003 REAL ESP: scanned "..count.." TES3 records; safe furniture "..found)
