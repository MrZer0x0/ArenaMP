# ArenaMP — CHANGELOG

[Project overview](README.md) · [Русское описание](README_RU.md)

The **first public source release** consolidates the previously developed ArenaMP engine, multiplayer systems, launcher, dedicated server and tools into a single coherent repository. Historical internal cumulative archives and migration notes are not individual releases and are not listed here. This document describes included functionality and noteworthy integration changes rather than promising that every platform has already been successfully compiled or play-tested.

## ArenaMP RC2 — cumulative update (Windows / Linux)

This cumulative source update combines Y111, Quest Wizard (QW1), Player Menu (PM1), furniture/inventory/waypoint improvements (FC1, IF1, WF1), combat balance (CB1) and housing safeguards (HS1). It is an update to an existing ArenaMP source tree, **not a standalone game or prebuilt release**.

### Multiplayer, moderation and quests

- Reworked sequential player-packet processing and optimized cell-file lookup and merchant-cell saves to reduce server interaction pauses when players connect.
- Added the **seven-step Quest Wizard** (`/quest`) with item collection, kill, location and dialogue-choice templates, in-world target selection, rewards, drafts and publishing. The advanced editor remains available via `/quest studio`.
- Unified staff permissions for furniture administration, cell management, quest editing and admin commands.

### Homes and furniture

- Added safe storage of owned items when a home is sold or reset, with restoration in a chest in the next home; purchased furniture is returned to the owner's furniture inventory. Added an optional first-home chest offer.
- Added confirmations for house sale/reset, furniture deletion/sale and major group actions.
- Improved staff capture of large decorative objects, catalogue retention and protection against taking original building structures.

### Interface, map and gameplay

- Redesigned the player menu with a compact icon sidebar; added golden action icons to Chat, Group and Home controls, and icon-based inventory filters.
- Added a personal persistent world-map waypoint shown on the map and outdoor compass. The map now uses a **32 px gold flag icon** instead of long Set Flag / Remove Flag labels, with contextual tooltips.
- Added passive blocking, physical hit evasion from Light/Sanctuary effects and level-scaled creature attack speeds with configurable limits.
- Expanded RU/EN interface strings, added custom icons and improved the sizing of quest-editing windows.

**Package scope:** Windows/Linux client source, server source/scripts, shared assets and documentation only. Updated source components require recompilation; in-game Release testing is still recommended.

## First public release — ArenaMP v24 source

### Client, launcher, server and network

- Native multiplayer Morrowind client, built on OpenMW/TES3MP/EncoreMP, with compatible dedicated server and bundled CoreScripts.
- Integrated Windows/Linux launcher with build/data-file profiles, ordered plugins, server browser and details, invitation IDs where supported, direct/relay joining and a local server management page.
- Local server settings and console: CO-OP/MMO selection, game policy controls, logs, start/stop, restart, plugin hash updates and optional server listing with a compatible master service.
- Launcher-side chat, microphone settings, local/party voice communication and the manifest-driven native updater.
- Reconnect/identity handling, multiplayer world state, animation syncing and server-authoritative checks used by the included modules.

### Gameplay, characters and persistence

- Active blocking/parrying, quick casting, stamina and server-configurable combat rules; multiplayer progression and a configurable quick bar.
- Groups, eligible travel destinations, markers, player-owned homes and persistent private interiors.
- Furniture placement, decorative objects, priced vitrines and access/read/take permissions, supported by server-side state management.
- Trade coefficients, economic and crafting modules, alchemy/enchantment support and operator configuration.
- Ordinary NPC physical collision bodies use a permanent 95% horizontal footprint (width and depth); vertical height, rendered model size and player collision remain unchanged. The former movement-only NPC passage scaling has been removed.

### Rubic0n LuaJIT 2.1 (Windows and Linux)

- Vendored **Rubic0n LuaJIT** source is bundled in `extern/LuaJIT` and consumed by the platform builds without substituting stock/system LuaJIT.
- LuaJIT verification checks real authored sources (including `src/Makefile` and `src/luajit_rolling.h`) and no longer incorrectly requires generated `src/luajit.h` before compilation.
- `.gitignore` explicitly allows the required vendored Makefiles, preventing missing files after a GitHub Actions checkout.

### Build and GitHub Releases

- Windows x64 and Linux x86_64 / Steam Deck CI build in **Release** mode.
- Windows publisher OpenSSL uses a dedicated **release-only vcpkg triplet** to avoid compiling and caching a separate Debug variant.
- The workflow has an explicit **`publish_release`** checkbox and **`release_tag`** input. A tagged `v*` build can also trigger release publication; normal branch pushes still produce downloadable Actions artifacts.
- After all selected jobs succeed, publish verified platform packages, a source archive, `BUILD_INFO.json` and `SHA256SUMS.txt` through GitHub Releases.
- Obsolete cumulative change notes were consolidated into this changelog.

### Packaging, documentation and maintenance

- Complete source layout, game/server scripts, resources, dependency provenance, tests, licences and authorship notices.
- Expanded default English README and full Russian translation, including setup, game build management, server hosting, network access, gameplay and release instructions.
- Technical background, developer references and known limitations remain under `docs/`; public release information is maintained centrally here rather than in many historical cumulative files.

## Verification and limitations

The source packaging process checks file completeness, Git inclusion rules, workflow structure, documentation links and isolated tests where available. Platform CI and runtime game sessions must still be executed by the maintainer. Issues historically reported around chairs/animations, NPC gear, demanding transitions and third-party dependencies should be checked against real Release packages before production use. See [PACKAGE_STATUS.md](PACKAGE_STATUS.md) and [troubleshooting](docs/guide/09-troubleshooting.md).
