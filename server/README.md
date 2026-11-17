# ArenaMP CoreScripts

The bundled server scripts implement account/world persistence and ArenaMP gameplay systems on the TES3MP server foundation. Use them with the corresponding native server revision and required Lua modules.

## Getting started

Configure the installed server through the launcher or edit `scripts/config.lua`. Select the actual content set, generate `data/requiredDataFiles.json` and use matching clients. Test locally before opening network access.

Keep working player, cell, world, housing and economy data separate from the source seed tree. Back up the entire data directory before deployment or maintenance.

[Configuration](ARENAMP_CONFIG.md) · [Security boundaries](COREARENAMP_SECURITY.md) · [Lua hooks](Tutorial.md) · [Server guide](../docs/guide/05-server.md)

Original script notices and [server/LICENSE](LICENSE) remain applicable; individual ArenaMP files may carry their own notices.

## Quest updates and persistence

Put each quest definition in `data/custom/quests/<quest-id>.json` (the JSON `id` must match the filename), alongside `index.json`. On server startup and `/quest reload` (staff only), ArenaMP scans that folder and merges newly added quests into `index.json` automatically. `_TEMPLATE.json` is never imported.

Quest JSON and the index are persistent server data, **not** world/cell reset data. The built-in `/resetall` and periodic cell resets do not remove them. The launcher updater preserves the `server/data` tree. When updating a server manually, **merge/retain your existing `server/data/custom/quests/` directory** instead of replacing `server/data` with the archive's default files. Deleting the entire data directory, or recreating the server from a clean installation, will still require a backup; no in-process script can recover files physically deleted by an external tool. Player quest progress is stored separately with player data and requires preserving that data as well.

If folder listing is blocked on a custom host, the last valid `index.json` remains the fallback; check the server log for the directory listing warning. The loader preserves old index entries when a referenced quest file is temporarily unavailable.
