# ArenaMP

**[English](README.md) | [Русский](README_RU.md)**

ArenaMP is a multiplayer Morrowind project for **Windows x64 and Linux x86_64, including Steam Deck**. It builds on OpenMW, TES3MP and EncoreMP, with its own launcher, dedicated server, combat rules, voice communication, housing and economy systems.

This is the **first public source release**. The repository contains the client, desktop launcher, native updater, server, Lua scripts and game resources. Build the project with GitHub Actions or follow the local build guide. Morrowind game data must be supplied from your own game installation.

[Build and publish](docs/BUILDING.md) · [Launcher guide](docs/LAUNCHER.md) · [Documentation](docs/guide/INDEX.md) · [Release notes](CHANGELOG.md)

## What is included

| Component | Purpose |
|---|---|
| ArenaMP client | Multiplayer world, combat, interface, animation and voice integration |
| Desktop launcher | Game-data setup, server browser, connection, local hosting, chat and settings |
| Dedicated server and CoreScripts | Player accounts, world persistence, groups, housing, economy and administration |
| Native updater | Manifest-based client and content updates configured by a server operator |
| Server and relay agents | Server-list registration and relay connections through a compatible master service |
| Rubic0n LuaJIT | Vendored Lua runtime built from `extern/LuaJIT` for both supported platforms |

## The launcher

The launcher brings player setup and local server management into one application. Start `arenamp-launcher.exe` on Windows or the `arenamp-launcher` wrapper on Linux.

### Set up a game build

Choose a folder containing `Morrowind.esm` and your server's plugins. The setup dialog reads an existing `build.ini`, or creates one from your chosen build name, language and plugin order. It detects BSA archives and recognises groundcover plugins. Russian, English and Polish content encodings can be selected in build setup.

The **Data Files** page manages content selection and load order. **Graphics** and the other settings pages expose display, rendering, sound and advanced options. Build selection and connection settings are saved between starts. Stop a running local server before switching builds.

### Find and join a server

- Browse the server directory inside the launcher and refresh the list.
- Inspect a server's name, build, mode, player count and connection method.
- Open its website or view the requirements and file manifest supplied by its operator.
- Find a private server by its invitation ID, where the master service supports it.
- Select a direct connection or use the integrated relay agent when the service offers a tunnel.
- Enter a host and port manually when you already know the address.

The connection card displays the selected endpoint, online status, player count and uptime when the server supplies that information. Password-protected servers prompt for the game-server password. Press **Play** after choosing the server and matching its content list.

### Host a local server

The local-server controls start and stop the bundled dedicated server without a separate console window. Set its name, password, player limit, port and network interface. Choose the available co-op or MMO settings, edit gameplay options through grouped forms, or inspect the raw `config.lua` editor.

The embedded console shows the server log, offers an encoding selector and exposes automatic restart. The launcher can refresh the server's required plugin hashes from the selected build. It also provides explicit, confirmed actions for clearing saved cells and resetting server data; back up a world before using them.

Enable **Show in server list** to register a running server with a compatible master service. An optional website/build URL accompanies its listing. Direct hosting requires reachable network ports; relay hosting depends on the configured service. Registration does not configure a router or firewall.

### Chat, microphone and updates

The **Chat** page connects to a compatible game server using an existing character account. It presents available channels, message history, player presence, mention indicators and access to `Chat.log`. Create the character in game before signing in from the launcher.

Microphone controls select a recording device, enable or disable transmission and configure the voice key. In-game voice supports local and party communication according to server settings.

The launcher checks for updates described by the selected build's manifest and can start the native updater. The updater presents download/install progress and verifies supplied hashes. Operators must configure update URLs and publish the appropriate manifests; creating a GitHub Release alone does not configure in-game updates. See the [updater guide](docs/arena-updater/README_RU.md).

## Gameplay

ArenaMP combines action-oriented combat with Morrowind's equipment, skills and spell effects. It includes active guard and parrying, quick casting, stamina rules, progression, a multi-tier quick bar and multiplayer animation handling.

Groups support shared communication, markers and eligible Recall destinations. Housing provides ownership, access rules, personal interiors, furniture placement and priced display items. Server modules provide trading coefficients, regional markets, crafting, enchanting and administration. Available rules depend on the server's configuration and content set.

| Default key | Action |
|---|---|
| W A S D | Move |
| E | Activate; supported contexts also use a long press |
| Left / right mouse | Attack or use / inventory |
| F | Ready weapon; active guard with eligible equipment |
| R | Quick Cast; hold to prepare magic |
| 1–9, 0 | Quick assignments |
| F1 | Quick-slot editor |
| V | Voice transmission control |
| Z | Animation menu |

Saved bindings can override these defaults. Detailed rules are in the [player guide](docs/guide/02-controls.md) and [combat reference](docs/guide/03-combat.md).

## Build and automatic publication

1. Put the contents of `ArenaMP/` at the root of your GitHub repository, including `.github` and the vendored dependencies.
2. Enable GitHub Actions and push to `main` or `master`, or open **Actions → Build and release (Windows, Linux) → Run workflow**.
3. For a manual run, select Windows, Linux or both. All packages use **Release** configuration.
4. After every selected build and package check succeeds, the workflow automatically publishes the packages under **Releases**.

The release includes a Windows ZIP and/or Linux portable archive, an archive of the repository at the built commit, build metadata and SHA-256 checksums. Failed or cancelled builds do not publish a release. A `v*` tag uses that tag as the release version; other runs use a unique `build-<run>-<attempt>` tag.

See [Building ArenaMP](docs/BUILDING.md) for prerequisites, local commands and repository permissions. The [validation status](PACKAGE_STATUS.md) records the checks available for this source distribution and the remaining runtime verification.

## Compatibility and server data

Use matching ArenaMP client and server revisions, the same plugins and the same plugin order. The supplied protocol identifier is defined in [Version.hpp](components/openmw-mp/Version.hpp); shared ancestry does not guarantee compatibility with stock TES3MP or another fork.

Keep live accounts, world saves and operator configuration outside the source repository. Back up the complete server data directory before deploying a new build. The [server guide](docs/guide/05-server.md) describes configuration and persistent data.

## Credits and licences

ArenaMP is an independent project based on community work. OpenMW, TES3MP, EncoreMP and the authors of included components retain their authorship and applicable notices. This is not an official Bethesda, OpenMW or TES3MP release.

See [LICENSE](LICENSE), [AUTHORS](AUTHORS.md), [CREDITS](CREDITS.md), [third-party notices](THIRD_PARTY_NOTICES.md) and the [component register](docs/guide/07-credits-licenses.md). Component-specific licences remain with their sources and resources.

[Contributing](CONTRIBUTING.md) · [Security](SECURITY.md) · [Roadmap](ROADMAP_RU.md)
