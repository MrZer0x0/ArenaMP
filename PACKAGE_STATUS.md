# ArenaMP — public Windows/Linux source package status

This archive is a **public source repository**, not precompiled binaries. It contains the Windows x64 and Linux x86_64 / Steam Deck build workflows and the complete vendored Rubic0n LuaJIT 2.1 source.

## Source checks

- Complete Windows/Linux GitHub Actions jobs, Release packaging and GitHub Releases publication path.
- Release-only Windows OpenSSL vcpkg triplet and Rubic0n source verification.
- The Android application, its private APK signing key, macOS/Android build jobs and platform packaging scripts are excluded.
- Repository-local syntax and source integrity tests are performed when preparing this source ZIP; they **do not prove** successful Windows/Linux compilation or gameplay.

## Before distributing binaries

Run the Windows and Linux workflows on GitHub-hosted runners and inspect both generated packages. Test launcher startup, game data selection, server login/reconnect, cell changes, house ownership, furniture persistence, voice and NPC interactions before promoting binaries to production. Do not commit production saves or credentials.
