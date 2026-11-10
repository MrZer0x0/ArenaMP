# Public repository scope — ArenaMP

This is the public source edition for **Windows x64** and **Linux x86_64 (including Steam Deck)**. The workflow `.github/workflows/release-builds.yml` has exactly two platform build jobs and one release-publishing job. Both builds use Release configuration, bundled Rubic0n LuaJIT, and the same source revision.

No Android project, APK signing material, Android CI job, macOS CI job, or macOS packaging scripts are included. The OpenMW-derived engine still has some platform-conditional source and CMake branches; those are left intact to avoid destabilizing the Windows and Linux builds.

To create a GitHub Release, open **Actions → Build and release (Windows, Linux) → Run workflow**, select platforms, and enable `publish_release`. Pushing a `v*` tag also publishes after successful selected builds. Ordinary pushes publish Actions artifacts only. See [BUILDING](docs/BUILDING.md) or [RU](docs/BUILDING_RU.md).

Always review new commits for passwords, player data, keys or proprietary game assets. The official Morrowind game files are not included. See [README](README.md), [License](LICENSE) and [Security](SECURITY.md).
