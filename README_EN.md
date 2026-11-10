# ArenaMP — your favorite Morrowind as a shared living world

**English** · **[Русский](README_RU.md)**

[Public Windows/Linux repository scope](PUBLIC_REPOSITORY.md) · [Build and release guide](docs/BUILDING.md)

ArenaMP is an independent multiplayer project built on **OpenMW, TES3MP and EncoreMP**. Its goal is to make Morrowind easy to play together and develop it toward a full MMO: a persistent world where characters grow, form parties, own homes, decorate, trade and create their own stories.

The project preserves Morrowind's freedom, distinctive world, magic and character development while adding what a shared world needs: accessible setup, communication, persistent ownership and server-checked rules. The launcher, game interface and server tools belong to one project, helping players spend more time adventuring and less time preparing a session.

**MMO describes the project's direction, not a promised player capacity.** ArenaMP already provides systems for a persistent multiplayer world; a particular server's capacity depends on hardware, content, scripts and workload. Small cooperative adventures remain a supported use case.

This public source edition targets **Windows x64 and Linux x86_64**, including portable Linux packaging for Steam Deck. The GitHub Actions workflow builds and publishes these two platforms only. This archive contains ArenaMP source and resources; original Morrowind game data must be installed separately. ArenaMP is independent of Bethesda and is not an official OpenMW or TES3MP release.

## What players can do

| Goal | ArenaMP features |
|---|---|
| Start playing | Build profiles, plugin selection and load order, server browser and direct joining |
| Adventure together | Parties, local and party voice, chat, markers and shared travel |
| Develop a character | Experience, point allocation, archetypes, active defense and quick casting |
| Settle in the world | Property purchases, private interiors, co-owners, friends and access rules |
| Create an interior | Furniture shop with previews, placement, rotation, storage and displays |
| Trade and craft | Configurable prices and services, regional economy and registered recipes |
| Host a world | Integrated local dedicated server, console, settings and server-list registration |

The host chooses the rules. Read a server's description before joining: cooperative play, PvP, economy, progression and property settings can differ.

## Quick start

### Players

1. Obtain a **Release** package for your operating system from the project's GitHub **Releases** page, after its maintainer has published and validated one.
2. Unpack the entire package into a writable folder. On Windows open `arenamp-launcher.exe`; on Linux run the bundled `./arenamp-launcher` wrapper (it prepares the portable Qt/runtime environment).
3. Select an existing legal installation of Morrowind, including `Morrowind.esm` and the data/plugins required by your chosen multiplayer server. ArenaMP does not distribute Bethesda's game assets.
4. In the launcher, set up or select a **game build**. Verify plugin selection, order and content hashes before connecting.
5. Open the server list, choose a compatible server, enter its password if required, and select **Play**. You can also enter a server address manually or start a local server from the launcher.

A server may require extra plugins, BSA archives or its own published content package. Matching the engine alone does not guarantee network or content compatibility.

### Server operators

Use the packaged dedicated server and `server/` CoreScripts. Configure the gameplay mode, network interface, port, required content, server identity, registration method and persistent storage **before** opening the server to other players. Start and inspect it through the launcher's **Local Server** page or run the dedicated server directly. Back up world and account data before updating or resetting. The launcher does not automatically open firewall/router ports.

### Developers

Unpack the complete source archive so that `CMakeLists.txt`, `.github/`, `CI/`, `apps/`, `components/`, `extern/`, `files/`, `server/` and `docs/` sit at repository root. The CI workflow is the maintained reference for exact build dependencies, packaging and validation. Follow **[Building and releases](docs/BUILDING.md)**; do not rely on a stock OpenMW or system LuaJIT installation.

## What is included?

| Component | Purpose |
|---|---|
| **ArenaMP game client** | Native OpenMW-derived renderer, character controls, UI, networked entities, animation, combat and spells |
| **ArenaMP Launcher** | One interface for builds and plugins, server discovery, direct/relay joining, local hosting, chat, microphone settings and updates |
| **Dedicated server** | Multiplayer simulation, progression, world and player persistence, permission checks, gameplay policies and administration |
| **CoreScripts and modules** | Lua-driven server rules, player and NPC systems, houses, groups, trading, crafting and configurable extensions |
| **Native updater** | Manifest-directed updates, download staging, progress and integrity checks |
| **Master/relay agents** | Optional registration and relay workflows with a compatible directory/master service |
| **Rubic0n LuaJIT 2.1** | Vendored runtime source in `extern/LuaJIT`, integrated in all supported platform build pipelines |
| **Docs, CI and tests** | Build recipes, security checks, validation scripts, user and operator references |

## Integrated desktop launcher

The launcher is a standalone native desktop application and the recommended entry point for both players and hosts. **Keep its bundled server, updater, helpers, Qt libraries and resources in the package**, even if you do not use every feature.

### Game builds and content

The first-run setup accepts a Morrowind data directory or `Data Files` child. It can load an existing `build.ini` or establish a new build profile with a display name, content language, enabled plugins and their order. It detects the original ESM/ESP content, BSA assets and recognized groundcover plugins. Legacy string decoding is chosen per content (Russian/Polish/Western variants) rather than assuming all servers use the same encoding. Profiles and client settings persist between sessions.

The **Data Files** page handles plugin selection and ordering; graphics and settings pages expose rendering, display, audio, UI and other client configuration. Do not change a build while its local server is running. A change of content or plugin order may require regenerating server-side required-file hashes.

### Server browser and connection

The main server page can refresh a compatible directory service, list public entries and show the server's declared build, mode, population and connectivity. Selecting a row opens its connection card, where available status, uptime, plugin requirements, website/build link and password prompts are displayed. Supported private entries can be found through an invitation ID; you may also type a host and port manually.

**Direct** joining requires a reachable game endpoint. **Relay** joining uses the included agent and a compatible master/relay service when configured. Server listing and public reachability are different operations: a listed server can still be blocked by a firewall or require missing game files.

### Create and manage a local server

The **Local Server** page controls a packaged server process from the same launcher. Configure its name, optional password, player limit, port and bind address; select available CO-OP/MMO policies; edit grouped gameplay controls or the underlying `config.lua`. The integrated console exposes logs, encoding, session information and restart controls. You can refresh the list of required plugins/hashes from the selected game build.

An optional **Show in server list** switch registers the running server with an available compatible directory and can publish a server website/build URL. Resetting character/world data and clearing stored cells are intentionally separate confirmed operations. Export backups before either operation, and preserve custom house/furniture records with world state.

### Chat, voice and updates

The launcher offers a chat view for a character account already created on a compatible game server: channels, message history, online presence, mentions and `Chat.log` access. Microphone setup selects the input device, enables/disables capture and configures transmission controls. In-game voice supports local and party communication subject to the server's policy.

The **native updater** can install files identified by a build-specific manifest, report download/install progress and verify hashes. Operators must publish correct manifests and update URLs: **GitHub Releases alone does not set up client auto-update**. See [updater design and deployment](docs/arena-updater/README_RU.md).

## Gameplay and everyday convenience

### Character creation and progression

The introduction explains ArenaMP before the adventure begins. Race and archetype selection help define a character's role, with their properties available in the interface. Progression uses experience and points for skills and attributes. The project supports levels up to 150; the host chooses rewards and restrictions.

Server checks track permitted progression, points and saved character state. Cooperative presets can differ from persistent MMO rules. Operators still need to configure staff accounts and server access.

### Combat, magic and equipment

Morrowind's skills, attributes, resistances, enchantments and spell effects remain central. ArenaMP adds active defense and parrying, quick casting, magic preparation, attack combinations and combat feedback. Fatigue, equipment, skills and level differences participate in combat rules; PvE and PvP can use separate damage limits.

Tap **R** to quick-cast or hold it to prepare magic. The available path depends on free hands and equipped items. **F** controls weapon readiness and supported active defense; bows and crossbows have their own states. Summons obey server limits, and summoned equipment is restricted from becoming ordinary persistent property.

Death, recovery, bounty and penalties are server settings. Numerical formulas and caps are documented in the [combat guide](docs/guide/03-combat.md); a cap is not guaranteed damage on every hit.

### Controls and quick access

| Default key | Action |
|---|---|
| W / A / S / D | Movement |
| E | Activate; hold for additional actions in supported contexts |
| F | Weapon readiness and active defense with suitable equipment |
| R / hold R | Quick Cast / prepare magic |
| 1–9, 0 | Quick-slot assignments |
| F1 | Configure quick slots |
| V | Voice transmission control |
| Z | Animation menu |

Saved bindings take precedence. The quick bar has ten columns; additional rows unlock every 30 levels, up to six rows. Holding a number edits an assignment. Supported lock and trap interactions can prepare an appropriate tool and restore valid equipment afterward. See [controls](docs/guide/02-controls.md).

### Parties, communication and travel

Invite companions through the game interface. Parties bring together membership, markers, voice and ally rules. Journal and dialogue-topic synchronization are separate choices: ordinary joining should not force a change to quest progress. Eligible shared actions award experience according to server policy.

Recall can offer your own Mark and available destinations of party members. The server checks membership, presence and travel restrictions. Chat supports role-play and out-of-character channels; voice supports local and party communication, with microphone status indicators.

### Property: a home in a shared world

Hold activation on an eligible door to open property actions. Vacant property displays its price and purchase conditions; owned property shows its owner and available actions. Ordinary entry and entry into a private instance are distinct, with an entry preference setting.

The **My Home** tab manages the title, co-owners, friends, locks, visitor rules and available transfer/sale actions. Several linked rooms may form one property. A private instance persists separately and is protected from ordinary cell resets; its original interior follows the shared world's rules. Operators can also configure property in original-cell mode.

Base minimum levels are **10** for houses, **15** for taverns, **20** for shops and **25** for guild premises. The default overall limit is one property; hosts can change types, limits and prices. Permission to read a book is distinct from permission to take it. Visitors, friends and co-owners have different access, and a locked entrance does not remove a trusted player's rights.

### Furniture, decoration and display sales

Open the furniture shop from the player interface or use `/furniture`. Browse categories and styles, preview models, buy and place objects. Supported actions include movement, rotation, returning furniture to storage and permitted sale/removal operations. Purchased furniture from a temporarily disabled mod remains accounted for; its model still requires the corresponding content.

The supplied reference contains **515 records**, each with separate Russian and English names. It is a reference, not a promise of 515 products on every server: the shop checks active plugins and object eligibility. Scripted, quest-related and unsuitable objects do not become ordinary stock. The server validates prices and permissions.

Supported placed items can become sale displays with pricing, locking, purchase and management actions. House rules constrain visitors. Furniture taken from an original interior is tracked separately from commercial stock and cannot be converted into free gold through shop resale.

### Economy, crafting, alchemy and quests

Trade coefficients control buying, selling, training, enchanting and other services, including per-player policies. Craft and Economic provides regional markets, demand, taxes, trade offers and registered recipes. Operators need to choose content, supply recipes and configure rules to create a meaningful economy; simply starting a server does not populate all of this automatically.

Alchemy systems include ingredient discovery, mixtures and supported poison application. Server quests can define stages, objectives, rewards and translations, while personal quest state helps manage progression in a shared world. Individual quests and recipes depend on the enabled modules and plugins.

Advanced politics, large-scale transport and all proposed weather scenarios remain development directions until implemented by a specific module. See [gameplay](docs/guide/04-gameplay.md), [economy](docs/craft-economic/README_RU.md), [alchemy](docs/ARENAMP_REFINED_ALCHEMY.md) and the [roadmap](ROADMAP_RU.md). Some detailed developer guides are currently in Russian.


### Movement, interface and language

`[Game] npc player passage scale` controls horizontal movement separation between NPCs and players; it does not shrink combat hitboxes or world collision. Animations, quick actions and menus make common operations accessible without long chat commands.

ArenaMP's interface and furniture catalog support **RU/EN**. Interface language, server-message language and plugin language are separate: changing menus does not translate a third-party ESM. Custom names remain user-authored. Operators can enter both names for new mod furniture in its editor; an absent English translation falls back to the record ID instead of presenting a Russian label as English.

## Technology and source layout

| Path | Contents |
|---|---|
| `apps/openmw/`, `components/`, `files/` | C++ game client, engine services and shared resources |
| `apps/launcher/`, `apps/` | Native launcher, updater, networking utilities and auxiliary applications |
| `server/` | Server Lua logic, configuration, rules, data templates and modules |
| `extern/LuaJIT/` | **Rubic0n LuaJIT** vendored source, license and upstream provenance |
| `extern/` | Other bundled third-party components and required notices |
| `CI/`, `.github/workflows/` | Release-only platform builds, caching, package checks and GitHub publication |
| `docs/`, `tests/` | Player/operator manuals, module information and validation programs |

### Why Rubic0n LuaJIT?

Rubic0n LuaJIT is shipped **with** ArenaMP instead of being downloaded from a generic LuaJIT build or silently replaced by system LuaJIT. Windows uses the MSVC vendor build; Linux uses the local POSIX vendor source. `src/luajit.h` is **generated during compilation**. The authored `extern/LuaJIT/src/Makefile` must be committed to Git; `.gitignore` explicitly permits it. After staging the repository, run `python3 CI/verify-rubic0n-vendor.py` to confirm that the full source will actually be available in GitHub Actions.

## Build, test and publish Release packages

Open **Actions → Build and release (Windows, Linux) → Run workflow**. Select the platform checkboxes and optionally enable **`publish_release`**. Each platform is built in **Release** configuration, then packaged and validated. Windows OpenSSL is compiled with a custom **release-only vcpkg triplet**; its Debug variants are not built. Artifacts stay under GitHub **Actions** unless the release publication checkbox is selected or a `v*` Git tag triggers publication. The publication job attaches successful platform packages, a source snapshot without platform signing credentials, checksums and build metadata to **Releases**.

A failed or cancelled selected platform blocks that joint publication. Set `release_tag` for manual releases or leave it blank to generate a unique run-based tag. A `v*` tag publishes under that tag. The workflow uses `GITHUB_TOKEN` with write permission in its publication job; ensure repository settings allow it. When preparing the archive for GitHub, do **not** omit `.github/` or the Rubic0n source Makefiles.

See [complete build guide](docs/BUILDING.md), [validation](docs/guide/VALIDATION.md) and [release history](CHANGELOG.md). Building the source is not equivalent to testing voice, multiplayer persistence, network reconnection and platform-specific graphics on real devices.

## Data, compatibility, maintenance and security

- **Game data:** Morrowind assets are required separately; mods must match the target server's required files, hash list and order.
- **Network compatibility:** ArenaMP's protocol and gameplay extensions differ from stock TES3MP and other forks. Use matching server and client revisions; the protocol definition is in [`components/openmw-mp/Version.hpp`](components/openmw-mp/Version.hpp).
- **Persistent data:** keep live player accounts, world saves, housing records, custom furniture references and server secrets outside public Git commits. Restore data from backups before allowing players into a migrated world.
- **Updates:** validate a new build with two clients on a test copy of the world; check login/reconnect, cell transitions, NPC equipment, combat, houses, furniture, voice and logout before upgrading the production server.
- **Support:** use the [troubleshooting guide](docs/guide/09-troubleshooting.md), [server guide](docs/guide/05-server.md), [`PACKAGE_STATUS.md`](PACKAGE_STATUS.md) and CI diagnostics when debugging a build. The source archive has not been certified by a successful cloud build or real-world multiplayer session in this preparation environment.

## Licensing and credits

ArenaMP inherits and extends the contributions of the **OpenMW**, **TES3MP** and **EncoreMP** communities, together with the authors of its included native libraries, scripts, assets and tools. Different third-party components carry their own terms and provenance. ArenaMP does not grant rights to distribute Bethesda's original game assets.

Read [LICENSE](LICENSE), [AUTHORS](AUTHORS.md), [CREDITS](CREDITS.md), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), the [component license register](docs/guide/07-credits-licenses.md), [CONTRIBUTING.md](CONTRIBUTING.md) and [SECURITY.md](SECURITY.md). More design and feature plans are tracked in [ROADMAP_RU.md](ROADMAP_RU.md).
