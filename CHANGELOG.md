# ArenaMP — CHANGELOG

[Project overview](README.md) · [Русское описание](README_RU.md)

The **first public source release** consolidates the previously developed ArenaMP engine, multiplayer systems, launcher, dedicated server and tools into a single coherent repository. Historical internal cumulative archives and migration notes are not individual releases and are not listed here. This document describes included functionality and noteworthy integration changes rather than promising that every platform has already been successfully compiled or play-tested.

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
- Baseline V17 player/world collision geometry and configurable NPC/player movement separation. `npc player passage scale = 0.2` means 20% of original horizontal spacing; combat hit geometry remains separate.

### Rubic0n LuaJIT 2.1 (Windows and Linux)

- Vendored **Rubic0n LuaJIT** source is bundled in `extern/LuaJIT` and consumed by the platform builds without substituting stock/system LuaJIT.
- LuaJIT verification checks real authored sources (including `src/Makefile` and `src/luajit_rolling.h`) and no longer incorrectly requires generated `src/luajit.h` before compilation.
- `.gitignore` explicitly allows the required vendored Makefiles, preventing missing files after a GitHub Actions checkout.

### Build and GitHub Releases

- Windows x64 and Linux x86_64 / Steam Deck CI build in **Release** mode. Android and macOS build jobs are not included in this public repository.
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
