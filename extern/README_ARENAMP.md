# ArenaMP bundled sources — V21

This directory restores the previously omitted bundled dependencies. The exact upstream commits, source locations and local changes are in ARENA_DEPENDENCY_SOURCES.json. Original copyright notices and license texts are retained.

The reconstruction uses TES3MP 0.8.1 bundled libraries, an explicitly pinned OpenMW maskedoc integration, CrabNet library/voice sources, the TES3MP Breakpad gitlink and its exact LSS DEPS revision. It is not represented as a recovered historical ArenaMP dependency tree.

CrabNet's unrelated GUI samples and prebuilt example binaries are omitted. Keep CRABNET_ENABLE_SAMPLES disabled. System SDKs and generated build products are not included. Follow the existing platform build instructions and V21_EXTERN_RESTORE_RU.md.
