# ArenaMP CoreScripts

The bundled server scripts implement account/world persistence and ArenaMP gameplay systems on the TES3MP server foundation. Use them with the corresponding native server revision and required Lua modules.

## Getting started

Configure the installed server through the launcher or edit `scripts/config.lua`. Select the actual content set, generate `data/requiredDataFiles.json` and use matching clients. Test locally before opening network access.

Keep working player, cell, world, housing and economy data separate from the source seed tree. Back up the entire data directory before deployment or maintenance.

[Configuration](ARENAMP_CONFIG.md) · [Security boundaries](COREARENAMP_SECURITY.md) · [Lua hooks](Tutorial.md) · [Server guide](../docs/guide/05-server.md)

Original script notices and [server/LICENSE](LICENSE) remain applicable; individual ArenaMP files may carry their own notices.
