# ArenaMP changelog

Public releases are documented here. Internal development snapshots are not separate public releases.

## First public release

ArenaMP's first public source distribution targets Windows x64 and Linux x86_64 / Steam Deck.

### Applications and hosting

- Desktop multiplayer client, integrated launcher and dedicated server with bundled CoreScripts.
- Launcher game-build setup, plugin order, server directory, direct and relay connections, local-server console and configuration.
- Launcher chat, microphone selection and manifest-based native updater.
- Automatic GitHub Releases publication after all selected Release builds and package checks succeed.
- Repository source archive, build metadata and SHA-256 checksums attached to each automatic release.

### Runtime and gameplay

- Vendored Rubic0n LuaJIT sources and platform build integration.
- Active guard, parrying, quick casting, stamina and progression rules.
- Multi-tier quick assignments, multiplayer animation handling, local and party voice.
- Player collision dimensions, NPC give-way behaviour and automatic lock/trap tool handling.
- Housing, private interiors, furniture and beds, access rules and item displays.
- Group travel and markers, server economy, crafting, enchanting and administration.

### Documentation and distribution

- English default README with a Russian version.
- Player, launcher, server, build, configuration and troubleshooting guides.
- Preserved upstream authorship, licences and component notices.

This source distribution is intended to be compiled and tested by its maintainer. See [validation status](PACKAGE_STATUS.md) and [known issues](docs/guide/09-troubleshooting.md) for the verification scope.
