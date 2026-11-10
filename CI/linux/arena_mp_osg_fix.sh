#!/usr/bin/env bash
# Y111: also works with an existing 3.6.5 Linux build, without rebuilding it.
# Extract next to arena_mp_start.sh and run: bash ./arena_mp_osg_fix.sh
set -euo pipefail
gamedir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
version="${ARENAMP_OSG_VERSION:-3.6.5}"
selected=''
for dir in "$gamedir/lib/osgPlugins-$version" "$gamedir/lib64/osgPlugins-$version" \
    "$gamedir/lib/osgPlugins" "$gamedir/osgPlugins-$version" "$gamedir/osgPlugins"; do
    [[ -d "$dir" ]] || continue
    complete=1
    for name in dds png tga jpeg bmp freetype osg serializers_osg; do
        [[ -f "$dir/osgdb_$name.so" ]] || complete=0
    done
    if [[ "$complete" == 1 ]]; then selected="$dir"; break; fi
done
if [[ -z "$selected" ]]; then
    printf '%s\n' \
        'ArenaMP: the matching OSG image plugins are missing from this archive.' \
        "Required directory: lib/osgPlugins-$version" \
        'Required plugins: dds png tga jpeg bmp freetype osg serializers_osg.' \
        'Use a complete rebuilt Linux package; changing the path cannot replace missing files.' >&2
    exit 2
fi
if ! command -v ldd >/dev/null 2>&1; then
    printf '%s\n' 'ArenaMP: ldd is required to check the image plugin dependencies.' >&2
    exit 2
fi
for name in dds png tga jpeg bmp freetype osg serializers_osg; do
    if ! output="$(env -u LD_LIBRARY_PATH ldd "$selected/osgdb_$name.so" 2>&1)" \
        || [[ "$output" == *'not found'* ]]; then
        printf 'ArenaMP: unresolved plugin dependency: %s\n%s\n' "osgdb_$name.so" "$output" >&2
        exit 2
    fi
done
printf 'ArenaMP: using OSG plugins from %s\n' "$selected" >&2
if [[ "${1:-}" == --check ]]; then exit 0; fi
cd -- "$gamedir"
exec env OSG_LIBRARY_PATH="$selected" bash ./arena_mp_start.sh "$@"
