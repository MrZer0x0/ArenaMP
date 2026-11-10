# Contributing to ArenaMP

[Project](README.md) · [Build guide](docs/BUILDING.md)

## Scope

Contributions can affect native client/server code, CoreScripts, resources or documentation. State the intended behaviour and keep each contribution focused. Discuss protocol, persistence and architecture changes before relying on a new format. Preserve authorship and component licences.

## Pull requests

Describe the user-visible problem, resulting behaviour and affected platforms. Include relevant build and test results, content requirements, protocol or data-format changes, and migration steps when persistent data changes. Include screenshots for interface changes. Keep unrelated formatting changes separate.

## Compatibility

Use matching client/server revisions. A shared protocol number does not prove packet compatibility. Provide migrations or a compatible read path for persistent state. Add player-visible strings to both English and Russian resources and use the server localisation layer for messages.

Security-sensitive rewards, ownership and permissions must be checked by the responsible server module. Do not label client-calculated behaviour fully server-authoritative without implementing that boundary.

## Validation

Compile each affected target. For native/Lua integration, run `CI/check-lua-api-bindings.sh` and `CI/check-server-core.sh`. Validate modified JSON and layouts. Exercise relevant multiplayer behaviour with matching clients and a test server. Report targets or scenarios that were not tested.

Documentation changes must retain valid local links and describe current behaviour rather than internal patch history. Upstream notices and licence texts are preserved as original records.

## Reporting bugs

Include source revision, platform, reproduction steps, expected/actual behaviour, content order and relevant logs. Review logs and dumps for private information. For vulnerabilities, follow [SECURITY.md](SECURITY.md).
