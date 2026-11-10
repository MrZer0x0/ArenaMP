# Third-party notices

ArenaMP contains and links third-party code and assets. Their license files and notices remain with their respective source directories where provided.

## OpenMoji

The color emoji artwork baked into `files/mygui/ArenaMPChatColor.png` is derived from OpenMoji.

All emojis designed by OpenMoji, the open-source emoji and icon project.

- Project: <https://openmoji.org/>
- License: Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0)

Selected OpenMoji glyph bitmaps were packed into a MyGUI `ResourceManualFont` atlas together with text glyphs for the ArenaMP chat interface. The OpenMoji-derived atlas material is distributed under CC BY-SA 4.0. No endorsement by the OpenMoji Project is implied.

## OpenMW and TES3MP

ArenaMP is derived from OpenMW and TES3MP. See [LICENSE](LICENSE), [AUTHORS.md](AUTHORS.md), and [CREDITS.md](CREDITS.md).

## The Elder Scrolls III: Morrowind

Morrowind and related names/assets are the property of Bethesda Softworks and their respective owners. ArenaMP does not include Bethesda game data and requires a legal game copy.


## Craft and Economic idea acknowledgements (not bundled dependencies)

The new ArenaMP Craft and Economic code is original GPL-3.0-or-later code. Enchanting UI Lua OpenMW by tachyonhiggs and Economy Plus by zenocite informed the requested feature ideas. Their archives are not included, and their code/assets have not been relicensed or copied. See [the specific notice](docs/craft-economic/AUTHORS_AND_LICENSES.md) for links and the distinction between those mods and separately licensed tools mentioned in their credits.


## Bundled dependency sources

See `extern/ARENA_DEPENDENCY_SOURCES.json` for pinned sources, per-file hashes and local changes. TES3MP 0.8.1 bundled dependencies and its exact Breakpad/LSS revisions are included. The maskedoc integration is pinned to robbowland/openmw-occlusion-culling commit 9c049b971a6e336be25bec2898dd729bdba6f458, preserving Intel Apache-2.0 and embedded SSE2NEON MIT notices. Original notices and license files remain authoritative. No permission for unrelated third-party game assets is added by this distribution.


## Rubic0n LuaJIT

The supplied Rubic0n LuaJIT sources are in `extern/LuaJIT`, based on the OpenResty/LuaJIT lineage identified by `.arenamp-upstream.txt`. Original LuaJIT/Lua copyright and MIT notices are retained in [COPYRIGHT](extern/LuaJIT/COPYRIGHT). The platform builds compile this vendored tree.
