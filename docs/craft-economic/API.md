# ArenaMP Craft and Economic script API

`craftEconomic` is loaded by `serverCore.lua`. The following calls are available to trusted server Lua scripts, not as an arbitrary client RPC. IDs are taken from the actual server plugin catalogue. APIs return a copied table (or a value) on success, otherwise `nil, errorCode`.

```lua
local ce = require("craftEconomic")

local regionId, displayName = ce.GetRegion(pid)
local market = ce.GetMarket(regionId) -- copy: pressure, revision, day, treasury
local unit = {refId=myItemId,count=1,charge=-1,enchantmentCharge=-1,
              soul="",poisonId="",poisonCharges=0}
local price, err = ce.GetPrice(pid, unit, "buy")
-- price.total, tax, subtotal, family, regional, base

ce.RegisterFence(myNpcRecordId)       -- explicitly designated NPC
ce.RegisterFence(myFactionId, true)   -- explicitly designated faction
ce.MarkStolen(pid, unit, 1)           -- trusted theft integration; no client claim

ce.RegisterRecipe("my.recipe", {
    name="My recipe", skill="Armorer", minSkill=25, gold=10,
    regions={[regionId]=true},       -- omit for all regions
    ingredients={{refId=ingredientId,count=2,charge=-1,enchantmentCharge=-1,soul=""}},
    output={refId=existingOutputId,count=1,charge=-1,enchantmentCharge=-1,soul=""}
})
-- The output record must already exist on the server. Unknown recipes and
-- client-supplied output records are rejected.
local quote, err = ce.Quote(pid, {kind="recipe", recipe="my.recipe"})
-- Display quote and require the player's explicit confirmation in your UI.
local result, err = ce.Commit(pid, quote.token)
```

Trade quotes require a current accepted BARTER dialogue and a merchant descriptor `{uid="refNum-mpNum",refId=npcRecordId}`. Enchant quotes require the selected item/gem, name, cast type, known effect descriptors and optionally a merchant with enchanting service and accepted dialogue. Merchant distance, cell, full stock, actual player inventory and quote revision are rechecked at confirmation. Never expose `RegisterRecipe` or `RegisterFence` directly to clients.

Default recipes/fences are intentionally empty: arbitrary TES3 mods have no universal recipe or fence-role field. Register them from your trusted gameplay modules or edit the module's configuration. This package does not edit existing world/player data to guess those roles.

`MarkStolen` updates the player's ledger in memory and follows the normal profile save lifecycle unless a Craft and Economic transaction saves it first. Call it only after your own authoritative theft check. It is not an import of a client's old stolen-item ledger.

The internal native extension `ContentCatalogGetCellReference(cell, refNum, refId)` is exposed as `contentApi.GetCellReference(...)`. It returns a merged static reference with owner, faction and position, or nil for unknown, deleted or ambiguous references. It never returns an invented owner. Catalogue records also expose `CELL.economyRegion`, MGEF school/cost/effective flags, creature soul/gold, item value/capacity and NPC merchant/service metadata.

Wire messages use the reserved `@@ARENA_CE1@@` prefix on the existing reliable private player channel. Protocol version remains 809; the feature schema is version 1. Only `hello`, `quote`, `commit` and `menu` are dispatched. The native bridge suppresses these messages in normal chat. Token, sequence, size, nesting, rate and numerical limits are enforced. Completed tokens are retained in a bounded per-profile recent-results list; evicted or unknown tokens cannot execute a new transaction without a fresh quote.
