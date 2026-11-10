# Building and publishing ArenaMP

[Русский](BUILDING_RU.md) · [Project](../README.md)

## Supported packages

| Target | Build environment | Output |
|---|---|---|
| Windows x64 | Visual Studio 2022, Ninja, `windows-2022` | ZIP with launcher, updater, client, server and resources |
| Linux x86_64 / Steam Deck | Ubuntu 22.04, GCC, Ninja | Portable `.tar.gz` with entry-point wrappers and runtime libraries |

Both use Release configuration and vendored Rubic0n LuaJIT. A build needs Internet access for toolchains and external dependencies. Morrowind game data is supplied separately.

## First repository setup

Extract the source archive and place the **contents** of `ArenaMP/` at the repository root. `CMakeLists.txt`, `apps/`, `components/`, `server/`, `extern/` and `.github/` must be siblings. Preserve the local Git repository metadata. Use Git to add the complete tree, including vendored Makefiles:

```bash
git add -A
python3 CI/verify-rubic0n-vendor.py
git commit -m "Prepare ArenaMP first public release"
git push origin main
```

Use your repository's actual branch name. The verifier checks the Git index, so run it after staging files. Do not require a generated `src/luajit.h` in the unbuilt source tree: Rubic0n creates it during compilation.

## Automatic build and release

The [workflow](../.github/workflows/release-builds.yml) runs on pushes to `main` or `master`, pushes of `v*` tags, and manual dispatch. In **Actions → Build and release (Windows, Linux) → Run workflow**, select the desired platforms and **`publish_release`** when you want a GitHub Release:

| Input | Default | Meaning |
|---|---|---|
| `build_windows` | Enabled | Build Windows x64 |
| `build_linux` | Enabled | Build Linux x86_64 / Steam Deck |
| `clear_build_cache` | Disabled | Ignore reusable compiled build caches |
| `publish_release` | Disabled | Also publish verified packages to the repository's **Releases** tab |
| `release_tag` | Blank | Optional name for a manually published release; empty uses a run-derived tag |

Push events build both platforms. A manual run can build one or both. Selecting neither produces no release. Download caches can still be reused when compiled caches are cleared.

Normal `main`/`master` pushes and manual runs without `publish_release` create downloadable **Actions artifacts only**. The publication job runs if `publish_release` is checked or a `v*` tag was pushed. Every selected platform must build and pass tests/packaging before publication. A failed or cancelled selected job blocks publication. Unselected platforms may be skipped. Only the publication job requests repository write permission; it uses `GITHUB_TOKEN`, without a separate personal token.

Enable Actions in repository settings. Repository or organisation policy must permit the publication job to write repository contents. A policy denying that permission will leave successful build artifacts in Actions but prevent release creation.

### Published files

A successful run publishes under **Releases**:

- `ArenaMP-Windows-x64-<run>-<attempt>.zip`, if selected;
- `ArenaMP-Linux-x86_64-SteamDeck-<run>-<attempt>.tar.gz`, if selected;
- `ArenaMP-Source-<run>-<attempt>.tar.gz`, from the exact repository commit;
- `BUILD_INFO.json` and `SHA256SUMS.txt`.

Diagnostics stay in Actions artifacts. The source archive contains the checked-in build scripts and notices; downloaded toolchains and build-time external dependencies are described by the workflow and dependency register.

A `v*` tag supplies the release tag. Other runs receive `build-<run>-<attempt>`. The publishing action creates or updates the designated release only after the checked packages and source assets have been prepared. Use a new tag for a new tagged release rather than overwriting an existing public tag.

To choose a version yourself, push a tag such as:

```bash
git tag v1.0.0
git push origin v1.0.0
```

This is an example version name, not a change to the engine's protocol or internal version identifiers. Each push to a watched branch also starts a build, but does not publish automatically unless it is a watched `v*` tag or publication is explicitly requested from manual dispatch.

## Windows local build

Use the same Visual Studio/MSYS-compatible environment and dependencies as the Windows workflow. From the repository root:

```bash
bash CI/before_script.msvc.sh -c Release -p x64 -v 2022 -N -V -i "$PWD/install"
source MSVC2022_64_Ninja/activate_msvc.sh
cmake --build MSVC2022_64_Ninja --config Release --parallel 2
cmake --install MSVC2022_64_Ninja --config Release
```

The workflow records the full dependency setup and verifies the install tree. Keep the native updater, agents, server Lua modules and resources with the executables when packaging.

## Linux local build

Ubuntu 22.04 is the reference build host. Run from the repository root:

```bash
bash CI/linux/install-deps.sh
bash CI/ensure-bundled-deps.sh
export RUNNER_TEMP="$PWD/.build-tools"
mkdir -p "$RUNNER_TEMP"
bash CI/build-rubic0n-posix.sh
cmake -S . -B build -G Ninja   -DCMAKE_BUILD_TYPE=Release   -DCMAKE_INSTALL_PREFIX="$PWD/stage"   -DCMAKE_C_COMPILER_LAUNCHER=ccache   -DCMAKE_CXX_COMPILER_LAUNCHER=ccache   -DBUILD_SHARED_LIBS=OFF -DBUILD_BROWSER=OFF   -DBUILD_OPENCS=OFF -DBUILD_WIZARD=OFF -DBUILD_UNITTESTS=OFF   -DUSE_SYSTEM_TINYXML=ON -DOPENMW_USE_SYSTEM_MYGUI=ON   -DOPENMW_USE_SYSTEM_OSG=ON -DOPENMW_USE_SYSTEM_BULLET=ON   -DMORROWIND_DATA_FILES=data -DOPENMW_RESOURCE_FILES=resources   -DLuaJit_INCLUDE_DIR="$RUNNER_TEMP/arenamp-rubic0n/include/luajit-2.1"   -DLuaJit_LIBRARY="$RUNNER_TEMP/arenamp-rubic0n/lib/libluajit-5.1.so"
BUILD_JOBS=2 bash CI/linux/build-package.sh
```

The bundled CrabNet source is used when an external RakNet library is not supplied. Local packages are written under `artifacts/`. Use the portable launcher wrapper rather than bypassing its Qt/runtime setup.

## Before distribution

Check the workflow result, archive checksums, installed launchers and local server. Verify at least two matching clients, cell transitions, housing persistence, voice and reconnect. Keep production data out of source commits and test on a copy of the world. See [validation](guide/VALIDATION.md), [dependency provenance](guide/DEPENDENCY_PROVENANCE.md) and [component notices](guide/07-credits-licenses.md).

GitHub Releases publication and the launcher's manifest-based update service are separate configurations. The [updater guide](arena-updater/README_RU.md) describes the latter.

## Rubic0n LuaJIT and Release-only OpenSSL

The complete `extern/LuaJIT` source tree, particularly authored `src/Makefile` and `src/luajit_rolling.h`, must be present after checkout. `src/luajit.h` is **generated during compilation**. Run `git add -A` and `python3 CI/verify-rubic0n-vendor.py` before pushing; on Actions the verifier rejects missing or untracked authored inputs. Do not switch to a stock/system LuaJIT.

The Windows workflow uses `CI/vcpkg-triplets/x64-windows-release.cmake` with `VCPKG_BUILD_TYPE=release`; publisher OpenSSL installs into `C:/vcpkg/installed/x64-windows-release`, eliminating the unnecessary Debug variant. This is separate from the Qt 5 TLS compatibility runtime.
