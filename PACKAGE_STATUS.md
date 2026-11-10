# First public source release: validation status

This distribution contains the ArenaMP source tree for Windows x64 and Linux x86_64 / Steam Deck. It includes the launcher, updater, server, CoreScripts, resources and vendored Rubic0n LuaJIT.

## Preparation checks

- Source archive integrity, required build files and vendored LuaJIT Git inclusion.
- English/Russian documentation structure and local file links.
- Workflow structure and shell syntax.
- Release asset selection, exact-commit source packaging, metadata and SHA-256 generation in a local rehearsal without contacting GitHub.
- Existing isolated gameplay checks described in [VALIDATION.md](docs/guide/VALIDATION.md).

## Runtime status

Full Windows and Linux engine builds and a multiplayer gameplay session have not been run during archive preparation. The included workflow performs platform build/package checks before automatic publication; the maintainer must inspect the successful run and exercise the resulting packages.

The reported chair-sitting height problem remains open. See [known issues](docs/guide/09-troubleshooting.md) and [component notices](docs/guide/07-credits-licenses.md) for the remaining runtime and distribution questions.

[Build and publish](docs/BUILDING.md) · [Русское руководство](docs/BUILDING_RU.md)
