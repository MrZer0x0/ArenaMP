-- ArenaMP Craft and Economic. Original ArenaMP implementation, GPL-3.0-or-later.
-- No region, province, material or plugin-name list is required.
return {
    personalCoefficients=true, -- neutral region; pricing policy comes from economyRules.lua
    enabled=true, protocol=1, quoteSeconds=20, maxLines=64, maxCount=10000,
    maxGold=100000000, maxRequestsPerSecond=12, maxEffects=8,
    regionalSwing=0.12, marketPeriodDays=7, pressureStep=0.002, pressureLimit=0.18,
    taxRate=0.03, enchantedTaxMultiplier=1.25, staplesTaxMultiplier=0.5,
    untaxedBelow=30, buyMarkup=1.10, sellShare=0.45, expertSellShare=0.65,
    bulkDiscount=0.025, quoteDistance=512,
    constantEffectNewLogic=true, fenceShare=0.70, fenceItemLimit=2000,
    consignmentDays=3, consignmentFee=0.15, maxConsignments=32,
    maxShare=0.5, shareStep=0.05, dividendShare=0.2,
    regions={}, -- [region record ID]={taxRate=0.04}
    fences={},  -- [NPC record ID]=true; intent cannot be inferred from arbitrary mod names
    fenceFactions={},
    recipes={}, -- trusted server recipes; no client-defined output records
}
