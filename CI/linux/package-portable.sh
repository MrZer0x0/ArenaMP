#!/usr/bin/env bash
set -euo pipefail

stage_dir="${1:-stage}"
package_dir="${2:-package}"
archive_path="${3:-}"
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
ref="${GITHUB_REF_NAME:-manual}"
ref="${ref//\//-}"
if [[ -z "$archive_path" ]]; then
    archive_path="ArenaMP-Linux-x86_64-SteamDeck-${ref}.tar.gz"
fi

rm -rf "$package_dir"
mkdir -p "$package_dir" "$package_dir/lib" "$package_dir/userdata"

copy_if_exists() {
    local src="$1"
    local dst="$2"
    if [[ -d "$src" ]]; then
        mkdir -p "$dst"
        cp -a "$src/." "$dst/"
    elif [[ -e "$src" ]]; then
        mkdir -p "$(dirname "$dst")"
        cp -a "$src" "$dst"
    fi
}

copy_first_existing() {
    local dst="$1"
    shift
    local src
    for src in "$@"; do
        if [[ -e "$src" ]]; then
            mkdir -p "$(dirname "$dst")"
            cp -a "$src" "$dst"
            return 0
        fi
    done
    return 1
}

# CMake installs Linux as a normal /usr-style tree. The release archive must be
# portable, so flatten binaries/config/resources back to a single runnable root.
if [[ -d "$stage_dir/bin" ]]; then
    cp -a "$stage_dir/bin/." "$package_dir/"
fi
find "$stage_dir" -maxdepth 1 -type f -perm -111 -exec cp -a {} "$package_dir/" \; 2>/dev/null || true

copy_first_existing "$package_dir/openmw.cfg" \
    "$stage_dir/etc/openmw/openmw.cfg" \
    "$stage_dir/openmw.cfg" \
    "$stage_dir/openmw.cfg.install" || true
copy_first_existing "$package_dir/defaults.bin" \
    "$stage_dir/etc/openmw/defaults.bin" \
    "$stage_dir/defaults.bin" || true
copy_first_existing "$package_dir/gamecontrollerdb.txt" \
    "$stage_dir/etc/openmw/gamecontrollerdb.txt" \
    "$stage_dir/gamecontrollerdb.txt" || true
copy_first_existing "$package_dir/tes3mp-client-default.cfg" \
    "$stage_dir/etc/openmw/tes3mp-client-default.cfg" \
    "$stage_dir/tes3mp-client-default.cfg" || true
copy_first_existing "$package_dir/tes3mp-server-default.cfg" \
    "$stage_dir/etc/openmw/tes3mp-server-default.cfg" \
    "$stage_dir/tes3mp-server-default.cfg" || true
copy_if_exists "$stage_dir/share/games/openmw/resources" "$package_dir/resources"
copy_if_exists "$stage_dir/resources" "$package_dir/resources"
copy_if_exists "$stage_dir/share/games/openmw/data" "$package_dir/data"
copy_if_exists "$stage_dir/data" "$package_dir/data"
copy_if_exists "$stage_dir/share/doc" "$package_dir/doc"
copy_if_exists "$stage_dir/share/licenses" "$package_dir/licenses"
copy_if_exists "$stage_dir/lib" "$package_dir/lib"

# Y077: the portable archive must never publish CMake install-prefix paths from
# the CI runner.  Keep engine resources relative to the archive root.  Morrowind
# data is user supplied, so only keep data=data when a packaged data directory
# really exists.  data-local remains untouched.
sanitize_portable_openmw_cfg() {
    local cfg="$package_dir/openmw.cfg"
    [[ -f "$cfg" ]] || return 0
    local have_data=0
    if [[ -d "$package_dir/data" ]] && find "$package_dir/data" -mindepth 1 -print -quit | grep -q .; then
        have_data=1
    elif [[ -d "$package_dir/data" ]]; then
        rmdir "$package_dir/data" 2>/dev/null || true
    fi
    awk -v have_data="$have_data" '
        /^data=/ {
            if (have_data && !data_done) { print "data=data"; data_done=1 }
            next
        }
        /^resources=/ {
            if (!resources_done) { print "resources=resources"; resources_done=1 }
            next
        }
        { print }
        END {
            if (!resources_done) print "resources=resources"
            if (have_data && !data_done) print "data=data"
        }
    ' "$cfg" > "$cfg.tmp"
    mv "$cfg.tmp" "$cfg"

    if grep -Eq '/home/runner/work/|/stage/(share|etc)/' "$cfg"; then
        echo "ERROR: non-portable CI path leaked into packaged openmw.cfg" >&2
        grep -En '/home/runner/work/|/stage/(share|etc)/' "$cfg" >&2 || true
        exit 1
    fi
}
sanitize_portable_openmw_cfg

# Use the ArenaMP server core bundled with this exact source revision. Prefer
# the CMake install tree and fall back to the checked-out source tree; never
# download a different CoreScripts revision while creating a release artifact.
copy_if_exists "$stage_dir/share/games/openmw/server" "$package_dir/server"
if [[ ! -d "$package_dir/server" && -d "server" ]]; then
    cp -a server "$package_dir/server"
fi
for required_server_file in \
    scripts/serverCore.lua \
    scripts/config.lua \
    data/banlist.json \
    data/requiredDataFiles.json \
    ARENAMP_CORE_VERSION.txt; do
    if [[ ! -f "$package_dir/server/$required_server_file" ]]; then
        echo "ERROR: bundled ArenaMP server core file is missing from the Linux package: $required_server_file" >&2
        exit 1
    fi
done
for required_data_dir in cell custom map player recordstore world; do
    [[ -d "$package_dir/server/data/$required_data_dir" ]] || {
        echo "ERROR: bundled ArenaMP server data directory is missing from the Linux package: $required_data_dir" >&2
        exit 1
    }
done
# Windows-only Lua modules cannot be loaded by the Linux server and are omitted.
find "$package_dir/server" -type f -iname '*.dll' -delete
# Repository-only marker files keep empty runtime directories in Git but must
# not be exposed as part of the shipped server data.
find "$package_dir/server" -type f \
    \( -name '.gitignore' -o -name '.gitkeep' \) -delete

# Copy runtime libraries like the official GNU/Linux tarball. Ubuntu runners
# link against system MyGUI/OSG/Bullet/Boost/etc.; without bundling them the
# archive works on the runner but misses lib/ on SteamOS/other distros.
search_dirs=(
    /lib
    /usr/lib
    /usr/local/lib
    /usr/local/lib64
    /lib/x86_64-linux-gnu
    /usr/lib/x86_64-linux-gnu
    /usr/local/lib/x86_64-linux-gnu
)

copy_library_pattern() {
    local pattern="$1"
    local dir
    for dir in "${search_dirs[@]}"; do
        [[ -d "$dir" ]] || continue
        find "$dir" -maxdepth 2 \( -type f -o -type l \) -name "$pattern" -exec cp -L --remove-destination --preserve=mode,timestamps -- {} "$package_dir/lib/" \;
    done
}

runtime_patterns=(
    'libboost_thread.so*'
    'libboost_system.so*'
    'libboost_filesystem.so*'
    'libboost_program_options.so*'
    'libboost_iostreams.so*'
    'libBulletCollision.so*'
    'libLinearMath.so*'
    'libBulletCollision-float64.so*'
    'libLinearMath-float64.so*'
    'libMyGUIEngine.so*'
    'libOpenThreads.so*'
    'libosg.so*'
    'libosgAnimation.so*'
    'libosgDB.so*'
    'libosgFX.so*'
    'libosgGA.so*'
    'libosgParticle.so*'
    'libosgShadow.so*'
    'libosgText.so*'
    'libosgUtil.so*'
    'libosgViewer.so*'
    'libosgWidget.so*'
    'libSDL2*.so*'
    'libopenal.so*'
    'libavcodec.so*'
    'libavformat.so*'
    'libavutil.so*'
    'libswresample.so*'
    'libswscale.so*'
    'libpng16.so*'
    'libbz2.so*'
    'libunshield.so*'
    'libuuid.so*'
    'libtinfo.so*'
    'libtinyxml.so*'
    'liblua5.1.so*'
    'libsndio.so*'
    'libssh*.so*'
    'libvpx.so*'
    # Y078: FFmpeg on Jammy can depend on Theora/Ogg/Vorbis even when the
    # top-level game executable does not list them directly. Keep them explicit
    # and also resolve the full recursive ELF dependency closure below.
    'libtheora.so*'
    'libtheoradec.so*'
    'libtheoraenc.so*'
    'libogg.so*'
    'libvorbis.so*'
    'libvorbisenc.so*'
    'libwebp.so*'
    'libx264.so*'
    'libx265.so*'
    'libaom.so*'
    'libcodec2.so*'
    'libshine.so*'
    'libcrystalhd.so*'
)
for pattern in "${runtime_patterns[@]}"; do
    copy_library_pattern "$pattern"
done

# ArenaMP must ship precisely the same LuaJIT that the server was linked against.
rubic0n_lib="${RUNNER_TEMP:-/tmp}/arenamp-rubic0n/lib"
[[ -f "$rubic0n_lib/libluajit-5.1.so" ]] || { echo 'Rubic0n LuaJIT runtime missing during packaging' >&2; exit 1; }
cp -a "$rubic0n_lib"/libluajit-5.1.so* "$package_dir/lib/"

# Secondary DT_NEEDED dependencies are collected after every explicit runtime
# library and plugin tree has been selected (see collect-portable-deps.py).

# OSG loads format plugins dynamically at runtime; they usually do not show up
# in ldd output for tes3mp/arenamp-launcher, so copy the plugin directory too.
for dir in /usr/lib/x86_64-linux-gnu/osgPlugins-* /usr/local/lib/x86_64-linux-gnu/osgPlugins-* \
    /usr/local/lib/osgPlugins-* /usr/lib/osgPlugins-* /usr/lib64/osgPlugins-* /usr/local/lib64/osgPlugins-*; do
    [[ -d "$dir" ]] || continue
    cp -a "$dir" "$package_dir/lib/"
done


# Y111: dynamic image loaders are invisible to the main ELF's DT_NEEDED.
# Do not publish a package whose library closure passes but textures cannot load.
for plugin in dds png tga jpeg bmp freetype osg serializers_osg; do
    if ! find "$package_dir/lib" -mindepth 2 -maxdepth 2 -name "osgdb_$plugin.so" \
        \( -type f -o -type l \) -print -quit | grep -q .; then
        echo "ERROR: required OSG image/runtime plugin missing: osgdb_$plugin.so" >&2
        exit 1
    fi
done

# Qt launcher/updater must use one self-consistent Qt 5 runtime.  Copying only
# platform plugins is not enough: libqxcb.so depends on the *private* Qt 5 XCB
# support library (libQt5XcbQpa.so.5).  If that helper is absent, a rolling
# distro can silently load its system copy and fail with Qt_5_PRIVATE_API symbol
# errors even though libqxcb.so itself came from this archive.
qt_plugin_roots=(
    /usr/lib/x86_64-linux-gnu/qt5/plugins
    /usr/lib/qt5/plugins
    /usr/local/lib/qt5/plugins
    /usr/local/lib/x86_64-linux-gnu/qt5/plugins
)
for qtbase in "${qt_plugin_roots[@]}"; do
    [[ -d "$qtbase" ]] || continue
    for subdir in platforms imageformats iconengines platformthemes xcbglintegrations styles; do
        [[ -d "$qtbase/$subdir" ]] || continue
        mkdir -p "$package_dir/lib/qt5/plugins/$subdir"
        cp -a "$qtbase/$subdir/." "$package_dir/lib/qt5/plugins/$subdir/"
    done
done

# Copy the Qt runtime as one build-host-matched set.  In particular the XCB
# platform plugin uses Qt private ABI and must never fall back to a different
# distro patch release of libQt5XcbQpa or libQt5DBus.
qt_runtime_patterns=(
    'libQt5Core.so*'
    'libQt5Gui.so*'
    'libQt5Widgets.so*'
    'libQt5Network.so*'
    'libQt5DBus.so*'
    'libQt5XcbQpa.so*'
)
for pattern in "${qt_runtime_patterns[@]}"; do
    copy_library_pattern "$pattern"
done

# Y079: ArenaMP portable intentionally uses XCB/Xwayland.  A partial Qt
# Wayland plugin is worse than no Wayland plugin: libqwayland-egl.so can load
# while its xdg-shell/wayland-egl helper integrations are absent, which makes
# Qt abort before the launcher is shown.  Keep the self-consistent XCB path
# and remove Wayland platform plugins before dependency closure is collected.
find "$package_dir/lib/qt5/plugins/platforms" -maxdepth 1 \
    -type f -name 'libqwayland*.so' -delete 2>/dev/null || true
rm -rf "$package_dir/lib/qt5/plugins/wayland-shell-integration" \
       "$package_dir/lib/qt5/plugins/wayland-graphics-integration-client" \
       "$package_dir/lib/qt5/plugins/wayland-decoration-client" 2>/dev/null || true

# Qt plugins are dependency roots too; the common collector below resolves
# their private XCB helper libraries from the same build host.

# All explicit runtime libraries and dynamic plugin trees are present now.
# Resolve the complete non-host DT_NEEDED closure once, by SONAME.
# Resolve copied aliases before dependency scanning or patchelf: package writes
# must never follow an absolute link back into the build host's /lib.
python3 "$script_dir/portable-archive.py" materialize "$package_dir"
python3 "$script_dir/collect-portable-deps.py" "$package_dir"

# Y077: graphics drivers and the C++ runtime belong to the host.  A partial
# Ubuntu GLVND/Mesa stack combined with Arch/SteamOS drivers is less portable
# than using the host stack and can break on newer GLIBCXX symbol versions.
remove_host_graphics_runtime() {
    local bad=(
        'libstdc++.so*' 'libGL.so*' 'libGLX.so*' 'libGLdispatch.so*'
        'libOpenGL.so*' 'libEGL.so*' 'libGLES*.so*' 'libGLX_mesa.so*'
        'libEGL_mesa.so*' 'libdrm.so*' 'libgbm.so*' 'libvulkan.so*'
        # Y079: the target distro must parse its own /etc/fonts configuration.
        'libfontconfig.so*'
    )
    local pattern
    for pattern in "${bad[@]}"; do
        find "$package_dir/lib" -name "$pattern" -delete 2>/dev/null || true
    done
}
remove_host_graphics_runtime

# Fail packaging instead of shipping a Qt XCB plugin that can bind to a random
# host Qt private ABI.
for required_qt in libQt5Core.so.5 libQt5Gui.so.5 libQt5Widgets.so.5 libQt5Network.so.5 libQt5DBus.so.5 libQt5XcbQpa.so.5; do
    [[ -e "$package_dir/lib/$required_qt" ]] || {
        echo "ERROR: incomplete bundled Qt runtime: missing $required_qt" >&2
        exit 1
    }
done
[[ -e "$package_dir/lib/qt5/plugins/platforms/libqxcb.so" ]] || {
    echo "ERROR: bundled Qt XCB platform plugin is missing" >&2
    exit 1
}

# Avoid a process-tree-wide LD_LIBRARY_PATH.  Give every shipped ELF its own
# relative RUNPATH instead.  Host utilities launched by ArenaMP then see their
# normal OpenSSL/Mesa/C++ runtime rather than ArenaMP's private libraries.
command -v patchelf >/dev/null 2>&1 || {
    echo "ERROR: patchelf is required to build the portable Linux package" >&2
    exit 1
}
set_portable_runpath() {
    local elf="$1"
    local dir rel runpath
    file "$elf" | grep -Eq 'ELF .* (executable|shared object)' || return 0
    dir="$(dirname "$elf")"
    if [[ "$dir" == "$package_dir" ]]; then
        runpath='$ORIGIN/lib'
    else
        rel="$(realpath --relative-to="$dir" "$package_dir/lib")"
        if [[ "$rel" == "." ]]; then
            runpath='$ORIGIN'
        else
            runpath="\$ORIGIN/$rel"
        fi
    fi
    patchelf --set-rpath "$runpath" "$elf"
}
while IFS= read -r -d '' elf; do
    set_portable_runpath "$elf"
done < <(find "$package_dir" -type f -print0)

# Y078: verify the archive itself contains every non-host runtime SONAME named
# by DT_NEEDED.  This catches missing Boost/MyGUI/Bullet/codec libraries during
# GitHub Actions instead of after a player extracts the archive on Arch Linux.
is_host_runtime_soname() {
    case "$1" in
        linux-vdso.so.*|ld-linux*.so*|libc.so.*|libm.so.*|libpthread.so.*|librt.so.*|libdl.so.*|libresolv.so.*|libgcc_s.so.*|libstdc++.so.*|\
        libGL.so.*|libGLX.so.*|libGLdispatch.so.*|libOpenGL.so.*|libEGL.so.*|libGLES*.so.*|libGLX_mesa.so.*|libEGL_mesa.so.*|libdrm.so.*|libgbm.so.*|libvulkan.so.*|libfontconfig.so.* )
            return 0 ;;
    esac
    return 1
}
verify_portable_needed_closure() {
    local elf needed missing=0
    while IFS= read -r -d '' elf; do
        file -L "$elf" 2>/dev/null | grep -Eq 'ELF .* (executable|shared object)' || continue
        while read -r needed; do
            [[ -n "$needed" ]] || continue
            is_host_runtime_soname "$needed" && continue
            if ! find "$package_dir/lib" \( -type f -o -type l \) 2>/dev/null \
                    | awk -F/ -v n="$needed" '$NF == n { found=1 } END { exit !found }'; then
                printf 'ERROR: portable ELF dependency missing: %s (needed by %s)\n' \
                    "$needed" "${elf#"$package_dir/"}" >&2
                missing=1
            fi
        done < <(readelf -d "$elf" 2>/dev/null \
            | sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p')
    done < <(find "$package_dir" -type f -print0)
    [[ "$missing" == 0 ]] || exit 1
}
verify_portable_needed_closure

# qt.conf makes the archive's plugin tree authoritative instead of allowing a
# distro-specific Qt installation to be selected by Qt's compiled-in prefix.
cat > "$package_dir/qt.conf" <<'QTCONF'
[Paths]
Plugins = lib/qt5/plugins
QTCONF

cat > "$package_dir/tes3mp-prelaunch" <<'PRELAUNCH'
#!/usr/bin/env bash
set -euo pipefail

wrapper="${1:-}"
gamedir="$(cd "$(dirname "$0")" && pwd -P)"
userdata="$gamedir/userdata"
mkdir -p "$userdata"

# Prefer portable userdata for cfg files only. The server core always runs
# directly from the bundled server directory next to the binaries.
case "$wrapper" in
    tes3mp-server)
        if [[ ! -f "$userdata/tes3mp-server.cfg" && -f "$gamedir/tes3mp-server-default.cfg" ]]; then
            cp -f "$gamedir/tes3mp-server-default.cfg" "$userdata/tes3mp-server.cfg"
        fi
        ;;
    tes3mp|arenamp-launcher|*)
        if [[ ! -f "$userdata/tes3mp-client.cfg" && -f "$gamedir/tes3mp-client-default.cfg" ]]; then
            cp -f "$gamedir/tes3mp-client-default.cfg" "$userdata/tes3mp-client.cfg"
        fi
        ;;
esac

# Y078: first-run Morrowind data configuration.  The portable release cannot
# ship Bethesda game data, but it should not drop the player into OpenMW's
# cryptic `No content file given` abort either.
user_openmw_cfg="$userdata/openmw.cfg"
has_content_cfg() {
    local cfg
    for cfg in "$user_openmw_cfg" "$gamedir/openmw.cfg"; do
        [[ -f "$cfg" ]] || continue
        grep -Eq '^[[:space:]]*content[[:space:]]*=' "$cfg" && return 0
    done
    return 1
}
find_morrowind_data() {
    local candidate
    local candidates=(
        "${ARENAMP_MORROWIND_DATA:-}"
        "$HOME/.local/share/Steam/steamapps/common/Morrowind/Data Files"
        "$HOME/.steam/steam/steamapps/common/Morrowind/Data Files"
        "$HOME/.steam/root/steamapps/common/Morrowind/Data Files"
        "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/Morrowind/Data Files"
        "$gamedir/Data Files"
    )
    for candidate in "${candidates[@]}"; do
        [[ -n "$candidate" && -d "$candidate" ]] || continue
        find "$candidate" -maxdepth 1 -type f -iname 'Morrowind.esm' -print -quit | grep -q . || continue
        printf '%s\n' "$candidate"
        return 0
    done
    return 1
}
choose_morrowind_data() {
    local picked=''
    if [[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]] && command -v zenity >/dev/null 2>&1; then
        picked="$(env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH \
            zenity --file-selection --directory --title='Select Morrowind Data Files' 2>/dev/null || true)"
    elif [[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]] && command -v kdialog >/dev/null 2>&1; then
        picked="$(env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH \
            kdialog --getexistingdirectory "$HOME" 2>/dev/null || true)"
    fi
    [[ -n "$picked" ]] || return 1
    find "$picked" -maxdepth 1 -type f -iname 'Morrowind.esm' -print -quit | grep -q . || return 1
    printf '%s\n' "$picked"
}
write_morrowind_cfg() {
    local data_dir="$1" esm actual
    mkdir -p "$userdata"
    # Remove only a previous ArenaMP auto-generated block; preserve user edits.
    if [[ -f "$user_openmw_cfg" ]]; then
        sed '/^# ArenaMP auto Morrowind data$/,/^# End ArenaMP auto Morrowind data$/d' \
            "$user_openmw_cfg" > "$user_openmw_cfg.tmp"
        mv "$user_openmw_cfg.tmp" "$user_openmw_cfg"
    fi
    {
        printf '# ArenaMP auto Morrowind data\n'
        printf 'data="%s"\n' "${data_dir//\"/\\\"}"
        for esm in Morrowind.esm Tribunal.esm Bloodmoon.esm; do
            actual="$(find "$data_dir" -maxdepth 1 -type f -iname "$esm" -printf '%f\n' -quit 2>/dev/null || true)"
            [[ -n "$actual" ]] && printf 'content=%s\n' "$actual"
        done
        printf '# End ArenaMP auto Morrowind data\n'
    } >> "$user_openmw_cfg"
}

if [[ "$wrapper" != tes3mp-server ]] && ! has_content_cfg; then
    data_dir="$(find_morrowind_data || true)"
    if [[ -z "$data_dir" && "$wrapper" == tes3mp ]]; then
        data_dir="$(choose_morrowind_data || true)"
    fi
    if [[ -n "$data_dir" ]]; then
        write_morrowind_cfg "$data_dir"
        printf 'ArenaMP: configured Morrowind data: %s\n' "$data_dir" >&2
    elif [[ "$wrapper" == tes3mp ]]; then
        printf '%s\n' \
            'ArenaMP: Morrowind Data Files are not configured.' \
            'Start ./arenamp-launcher and select the game data directory, or set:' \
            '  ARENAMP_MORROWIND_DATA="/path/to/Morrowind/Data Files" ./tes3mp' >&2
        exit 2
    fi
fi
PRELAUNCH
chmod +x "$package_dir/tes3mp-prelaunch"

wrap_binary() {
    local bin="$1"
    local path="$package_dir/$bin"
    [[ -f "$path" && -x "$path" ]] || return 0
    file "$path" | grep -q 'ELF .* executable' || return 0
    mv "$path" "$path.x86_64"
    cat > "$path" <<'WRAPPER'
#!/usr/bin/env bash
set -euo pipefail
wrapper="$(basename "$0")"
gamedir="$(cd "$(dirname "$0")" && pwd -P)"
cd "$gamedir"
if [[ -f ./tes3mp-prelaunch ]]; then
    bash ./tes3mp-prelaunch "$wrapper"
fi
# The ELF has a relative RUNPATH and qt.conf selects the bundled plugin tree.
# Strip inherited overrides so host desktop utilities spawned by ArenaMP do not
# inherit a private Qt/OpenSSL runtime accidentally.
# Y079: default the portable build to XCB/Xwayland even inside a Wayland
# desktop session.  This avoids Qt selecting a half-present Wayland backend.
# Advanced testers can opt into another backend explicitly with
# ARENAMP_QT_PLATFORM=wayland after supplying a complete compatible plugin set.
exec env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH \
    -u QT_QPA_PLATFORMTHEME -u QT_STYLE_OVERRIDE \
    QT_QPA_PLATFORM="${ARENAMP_QT_PLATFORM:-xcb}" \
    ARENAMP_PORTABLE_ROOT="$gamedir" "$gamedir/$wrapper.x86_64" "$@"
WRAPPER
    chmod +x "$path"
}

for bin in tes3mp tes3mp-server arenamp-launcher arena-updater arena-server-agent arena-relay-agent openmw-iniimporter openmw-essimporter bsatool esmtool; do
    wrap_binary "$bin"
done

# User-facing launcher and desktop integration are part of every Linux release.
cp -- "$script_dir/arena_mp_start.sh" "$package_dir/arena_mp_start.sh"
cp -- "$script_dir/arena_mp_osg_fix.sh" "$package_dir/arena_mp_osg_fix.sh"
chmod +x "$package_dir/arena_mp_osg_fix.sh"
cp -- "$script_dir/../../files/icons/arenamp-emblem.png" "$package_dir/arena_mp_icon.png"
chmod +x "$package_dir/arena_mp_start.sh"

cat > "$package_dir/STEAM_DECK_README.txt" <<'README'
ArenaMP Linux x86_64 portable build for Steam Deck Desktop Mode / SteamOS.
Run bash ./arena_mp_start.sh to launch and install the desktop/menu shortcut.
Use --install-shortcut to only install it, --no-shortcut to skip installation.
On Steam Deck, use Desktop Mode; add arena_mp_start.sh as a Non-Steam Game.
Your desktop may require confirming that the shortcut is trusted.
ArenaMP bundles its application runtime in ./lib, while the host supplies the
graphics driver stack (Mesa/GLVND), libstdc++ and related driver-facing libraries.
The executables use relative RUNPATH entries; LD_LIBRARY_PATH is not exported.
Qt uses the XCB backend by default, including on KDE/GNOME Wayland sessions via
Xwayland. The target system supplies its own fontconfig so its /etc/fonts rules
are parsed by the matching distro runtime. Set ARENAMP_QT_PLATFORM only for
advanced testing with a complete compatible alternative Qt platform backend.

Morrowind data files are NOT included. ArenaMP first tries common Steam paths
and ARENAMP_MORROWIND_DATA. If tes3mp is started directly and data is still
unknown, a Zenity/KDialog folder picker is used when available. You can also
choose the Data Files directory in the launcher. If automatic detection is unavailable,
create/edit userdata/openmw.cfg and add for example:

  data="/home/deck/Games/Morrowind/Data Files"
  content=Morrowind.esm
  content=Tribunal.esm
  content=Bloodmoon.esm

The engine resource path is portable and resolves to ./resources.
README

# Y111: use the actual client and its matching OSG library to load the packaged
# .so files and encode/decode real images. No display, GPU or Morrowind required.
# Explicit absolute preloads in this command prevent build-host plugins masking
# missing runtime files. Also covers direct ELF launch without the shell wrapper.
portable_root="$(cd -- "$package_dir" && pwd -P)"
env -u LD_LIBRARY_PATH -u OSG_LIBRARY_PATH -u OSG_LD_LIBRARY_PATH \
    ARENAMP_PORTABLE_ROOT="$portable_root" \
    "$portable_root/tes3mp.x86_64" --check-image-codecs

# Preserve stable permissions inside the tarball.
find "$package_dir" -type d -exec chmod 755 {} \;
find "$package_dir" -type f -name '*.sh' -exec chmod 755 {} \; 2>/dev/null || true

# The existing updater intentionally rejects external TAR links. Ensure future
# packages contain library bytes rather than distro-specific absolute aliases.
python3 "$script_dir/portable-archive.py" check "$package_dir"
rm -f "$archive_path"
tar --numeric-owner -C "$package_dir" -czf "$archive_path" .
printf 'Created %s\n' "$archive_path"
