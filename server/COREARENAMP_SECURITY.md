# Server validation boundaries

## Native player-data checks

The included native guard validates protected numeric player state, including non-finite values, invalid negative values and implausible ranges. Covered packet paths include statistics, progression, inventory/equipment, spell/effect state and attack/cast-related values. Rejected protected player fields use transactional rollback. Client-side outgoing sanity checks are an additional layer, not a server trust boundary.

## Separate gameplay checks

Lua modules validate their own ownership, access, progression, housing, economy and administrative operations. Read their configuration and implementation before extending them. The native guard does not automatically validate every custom script's state transition.

## Limits

This guard does not provide general speed/teleport detection, universal packet rate limiting, NPC/container interaction leases, additional blanket actor/cell authority enforcement or a complete server simulation of combat. Base transport routing is not made safe solely by numeric checks.

Use required-content validation, trusted scripts, controlled permissions, backups and matching revisions. Report vulnerabilities according to [SECURITY.md](../SECURITY.md).
