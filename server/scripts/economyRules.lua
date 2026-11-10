-- V18: prices are coefficients, not ESP/region-dependent economy rules.
return {
    enabled = true,
    all = 1.0, buy = 1.0, sell = 1.0,
    services = {trade=1.0, enchant=1.0, training=1.0, spellbuy=1.0,
        spellmaking=1.0, repair=1.0, travel=1.0},
    items = {}, -- ["item_refid"] = {buy=1.0, sell=1.0}
    players = {}, -- [accountName] = {all=1.0,buy=1.0,sell=1.0,services={},items={}}
    -- Confirmed managed trades only. Buying does not erase a sale history.
    sales = {enabled=true, perUnit=0.015, minimum=0.35, recoveryHours=24, maxItems=128},
}
