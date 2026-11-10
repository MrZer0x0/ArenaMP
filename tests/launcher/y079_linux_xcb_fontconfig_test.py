from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
package = (root / 'CI/linux/package-portable.sh').read_text()
collector = (root / 'CI/linux/collect-portable-deps.py').read_text()
start = (root / 'CI/linux/arena_mp_start.sh').read_text()
workflow = (root / '.github/workflows/release-builds.yml').read_text()

# Normal portable launch must be deterministic on Wayland desktops: use the
# complete bundled XCB backend through Xwayland instead of a partial Qt Wayland
# plugin stack.
assert 'QT_QPA_PLATFORM="${ARENAMP_QT_PLATFORM:-xcb}"' in start
assert 'QT_QPA_PLATFORM="${ARENAMP_QT_PLATFORM:-xcb}"' in package
assert '-u QT_QPA_PLATFORMTHEME -u QT_STYLE_OVERRIDE' in package
assert 'libqwayland*.so' in package
assert "-name 'libqwayland*.so' -delete" in package
assert 'Partial Qt Wayland platform plugin leaked into portable package' in workflow

# qtwayland5 must not be installed by the release job anymore. The word may be
# present in explanatory comments, so check only the apt install command.
apt_lines = [line for line in workflow.splitlines() if 'apt-get install' in line]
assert apt_lines
assert all('qtwayland5' not in line for line in apt_lines)

# Target distro owns fontconfig: a Jammy libfontconfig cannot parse current
# Arch/SteamOS /etc/fonts syntax reliably.
assert '"libfontconfig.so.*"' in collector
assert "'libfontconfig.so*'" in package
assert 'libfontconfig.so.*' in package
assert 'Bundled fontconfig would parse target distro /etc/fonts with the wrong version' in workflow

# User config is not rewritten to silence QFont warnings. The fix is runtime
# isolation only, not destructive launcher.cfg editing.
assert 'launcher.cfg' not in start

print('PASS: Y079 Linux portable XCB/Xwayland default + host fontconfig isolation')
