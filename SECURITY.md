# Security policy

## Reporting

Use the repository's private Security Advisory channel when it is available. Include the affected revision, platform, prerequisites, impact and reproducible steps. Remove credentials and private player/world data from examples. Coordinate publication of exploit details with the maintainer.

## Supported deployments

Deploy a matching, trusted client/server revision and review custom Lua scripts. Keep administration credentials and live persistent data outside the source repository. Back up the entire server data directory before changes and limit the server process's filesystem/network permissions.

Required-data-file enforcement needs a generated and tested manifest for the actual content set. A client claim alone must not authorise ownership, currency, progression or administrative operations.

## Implementation boundaries

The native numeric-data checks and Lua gameplay checks cover distinct responsibilities. They do not make all combat and movement fully server-simulated. See [server/COREARENAMP_SECURITY.md](server/COREARENAMP_SECURITY.md) for the included controls and their limits.
