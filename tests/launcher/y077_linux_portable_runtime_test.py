from pathlib import Path

root = Path(__file__).resolve().parents[2]
package = (root / 'CI/linux/package-portable.sh').read_text()
start = (root / 'CI/linux/arena_mp_start.sh').read_text()
configure = (root / 'CI/linux/configure.sh').read_text()
workflow = (root / '.github/workflows/release-builds.yml').read_text()
linuxpath = (root / 'components/files/linuxpath.cpp').read_text()
install_deps = (root / 'CI/linux/install-deps.sh').read_text()

# Complete, same-build Qt private ABI set.
for token in (
    'libQt5Core.so.5', 'libQt5Gui.so.5', 'libQt5Widgets.so.5',
    'libQt5Network.so.5', 'libQt5DBus.so.5', 'libQt5XcbQpa.so.5',
    'lib/qt5/plugins/platforms/libqxcb.so',
):
    assert token in package or token in workflow, token

# Host graphics/C++ stack must not be shipped into portable lib/.
for token in (
    'libstdc++.so.*', 'libGL.so.*', 'libGLX.so.*', 'libGLdispatch.so.*',
    'libdrm.so.*', 'libgbm.so.*', 'libEGL.so.*', 'libvulkan.so.*',
):
    assert token in package, token

# No process-tree-wide library override. RUNPATH owns private dependencies.
assert 'patchelf --set-rpath' in package
assert "runpath='$ORIGIN/lib'" in package
assert 'export LD_LIBRARY_PATH=' not in start
wrapper = package.split("cat > \"$path\" <<'WRAPPER'", 1)[1].split('WRAPPER', 1)[0]
assert 'export LD_LIBRARY_PATH=' not in wrapper
assert 'env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH' in wrapper

# Runtime root replaces the build machine's absolute global path only for portable runs.
assert 'ARENAMP_PORTABLE_ROOT="$gamedir"' in wrapper
assert 'getenv("ARENAMP_PORTABLE_ROOT")' in linuxpath
assert 'return portableRoot;' in linuxpath

# Generated config is relative and CI validates against leaked runner paths.
assert 'resources=resources' in package
assert "-DMORROWIND_DATA_FILES=data" in configure
assert "-DOPENMW_RESOURCE_FILES=resources" in configure
assert "-DMORROWIND_DATA_FILES=data" in workflow
assert "-DOPENMW_RESOURCE_FILES=resources" in workflow
assert '/home/runner/work/' in package
assert 'CI-only path leaked into portable openmw.cfg' in workflow

# Packaging dependency and first-run documentation.
assert 'patchelf' in install_deps
assert 'patchelf' in workflow
assert 'Morrowind data files are NOT included' in package
assert 'userdata/openmw.cfg' in package

print('PASS: Y077 Linux portable Qt/Mesa/RUNPATH/config portability guards')
