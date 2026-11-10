# CoreScripts event hooks

Use `customEventHooks` to integrate trusted server Lua modules. Validators run before default event logic; handlers run after it. A callback receives `eventStatus` and the event's own arguments.

## Status

`eventStatus.validDefaultHandler` controls default handling. `eventStatus.validCustomHandlers` tells custom handlers whether they should act. Handlers must inspect that status; registering one does not bypass other modules' decisions.

A validator can return `customEventHooks.makeEventStatus(validDefaultHandler, validCustomHandlers)` to change the outcome. Return no override when the existing status should remain in effect.

## Handler example

```lua
customEventHooks.registerHandler("OnPlayerDeath", function(eventStatus, pid)
    if not eventStatus.validCustomHandlers then return end
    -- Apply your server-owned gameplay rule here.
    -- Check permissions and persistent-state requirements before mutation.
end)
```

Look up the actual event signatures and call order in `scripts/eventHandler.lua` and `scripts/customEventHooks.lua`. Use existing module patterns for localisation, inventory identity, permission checks and profile saves. Do not infer signatures from a different TES3MP fork.

Test rejected events, normal events, absent/disconnected players and interaction with other modules. For account/world operations, test persistence across server restart.

[Server guide](../docs/guide/05-server.md) · [Configuration](ARENAMP_CONFIG.md)
