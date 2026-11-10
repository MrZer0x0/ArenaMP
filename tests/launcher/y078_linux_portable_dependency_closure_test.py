from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
package = (root / 'CI/linux/package-portable.sh').read_text()
collector = (root / 'CI/linux/collect-portable-deps.py').read_text()
workflow = (root / '.github/workflows/release-builds.yml').read_text()
linuxpath = (root / 'components/files/linuxpath.cpp').read_text()

# Recursive runtime closure: fixes secondary FFmpeg/codec dependencies such as
# libtheoraenc.so.1, while keeping Mesa/GLVND/libstdc++ host-owned.
assert 'collect-portable-deps.py' in package
assert 'DT_NEEDED' in collector
assert 'readelf' in collector
assert 'ldconfig' in collector
assert 'libtheoraenc.so*' in package
assert 'libboost_filesystem.so*' in package
assert 'libboost_program_options.so*' in package
assert 'libboost_iostreams.so*' in package
assert 'libMyGUIEngine.so*' in package
assert 'libLinearMath' in package
assert 'libBulletCollision' in package
assert 'verify_portable_needed_closure()' in package
assert 'readelf -d "$elf"' in package
assert 'portable ELF dependency missing' in package

# Direct .x86_64 execution must not resurrect the CI install prefix.
assert 'const boost::filesystem::path localRoot = getLocalPath();' in linuxpath
assert 'boost::filesystem::is_regular_file(localRoot / "openmw.cfg")' in linuxpath
assert 'boost::filesystem::is_directory(localRoot / "resources")' in linuxpath
assert linuxpath.count('return localRoot;') >= 2

# CI enforces the concrete libraries observed missing on Arch and has readelf.
assert 'libtheoraenc.so*' in workflow
assert 'libboost_filesystem.so*' in workflow
assert 'libMyGUIEngine.so*' in workflow
assert 'libBulletCollision*.so*' in workflow
assert 'binutils' in workflow

# Extract and exercise the generated first-run helper. This verifies that a
# legitimate external Data Files directory produces a portable userdata cfg.
m = re.search(r"cat > \"\$package_dir/tes3mp-prelaunch\" <<'PRELAUNCH'\n(.*?)\nPRELAUNCH", package, re.S)
assert m, 'tes3mp-prelaunch heredoc not found'
helper = m.group(1)
with tempfile.TemporaryDirectory() as td:
    game = Path(td) / 'ArenaMP'
    home = Path(td) / 'home'
    data = Path(td) / 'Morrowind' / 'Data Files'
    game.mkdir(parents=True)
    home.mkdir(parents=True)
    data.mkdir(parents=True)
    (game / 'openmw.cfg').write_text('resources=resources\n')
    (data / 'Morrowind.esm').write_bytes(b'')
    (data / 'Tribunal.esm').write_bytes(b'')
    helper_path = game / 'tes3mp-prelaunch'
    helper_path.write_text(helper)
    helper_path.chmod(0o755)
    env = os.environ.copy()
    env.update({
        'HOME': str(home),
        'ARENAMP_MORROWIND_DATA': str(data),
        'DISPLAY': '',
        'WAYLAND_DISPLAY': '',
    })
    subprocess.run([str(helper_path), 'tes3mp'], check=True, env=env, cwd=game,
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    cfg = (game / 'userdata' / 'openmw.cfg').read_text()
    assert f'data="{data}"' in cfg
    assert 'content=Morrowind.esm' in cfg
    assert 'content=Tribunal.esm' in cfg
    # Idempotent: second run sees content and does not append another block.
    subprocess.run([str(helper_path), 'tes3mp'], check=True, env=env, cwd=game,
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    assert (game / 'userdata' / 'openmw.cfg').read_text().count('# ArenaMP auto Morrowind data') == 1

print('PASS: Y078 Linux recursive runtime closure + direct portable root + first-run Data Files')
