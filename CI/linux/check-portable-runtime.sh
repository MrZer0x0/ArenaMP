#!/usr/bin/env bash
# Run on a trusted ArenaMP package, on the build host or in a target container.
set -euo pipefail
package="$(cd -- "${1:?usage: check-portable-runtime.sh PACKAGE}" && pwd -P)"
unset LD_LIBRARY_PATH QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH OSG_LIBRARY_PATH OSG_LD_LIBRARY_PATH
missing=0
while IFS= read -r -d '' elf; do
    file -Lb "$elf" | grep -Eq '^ELF .* (executable|shared object)' || continue
    if ! output="$(ldd "$elf" 2>&1)"; then
        printf 'Loader failed: %s\n%s\n' "$elf" "$output" >&2
        missing=1
    elif [[ "$output" == *'not found'* ]]; then
        printf 'Unresolved runtime: %s\n%s\n' "$elf" "$output" >&2
        missing=1
    fi
done < <(find "$package" -type f -print0)
[[ "$missing" == 0 ]] || exit 1
# Y111: Qt self-tests never loaded the game's DDS/TGA/PNG readers. Run the real
# client's codec check without a wrapper/root override, from outside its directory.
(cd / && timeout 30s env -u ARENAMP_PORTABLE_ROOT "$package/tes3mp.x86_64" --check-image-codecs)
# Exercise the shipped Qt/XCB platform, not merely the build host Qt/offscreen.
cd -- "$package"
timeout 60s xvfb-run -a env QT_DEBUG_PLUGINS=1 ARENAMP_QT_PLATFORM=xcb \
    bash ./arena-updater --self-test
