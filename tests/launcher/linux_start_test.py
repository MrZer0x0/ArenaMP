"""Run portable entry point against a stub launcher in a temporary user profile."""
from pathlib import Path
import os
import shlex
import shutil
import subprocess
import tempfile

source = Path(__file__).resolve().parents[2] / 'CI/linux/arena_mp_start.sh'
with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    game = root / 'Игра Arena $cash `tick` "quote" %field \\path'
    game.mkdir()
    script = game / 'arena_mp_start.sh'
    shutil.copyfile(source, script)
    launcher = game / 'arenamp-launcher'
    launcher.write_text('#!/bin/bash\nprintf "%s\\n" "$PWD" "$@" > "$TEST_CAPTURE"\n')
    home = root / 'profile'
    desktop = home / 'Рабочий стол'
    desktop.mkdir(parents=True)
    bindir = root / 'tools'
    bindir.mkdir()
    xdg = bindir / 'xdg-user-dir'
    xdg.write_text('#!/bin/bash\nprintf "%s\\n" "$TEST_DESKTOP"\n')
    xdg.chmod(0o755)
    capture = root / 'capture'
    env = dict(os.environ, HOME=str(home), XDG_DATA_HOME=str(home / 'data'),
               TEST_DESKTOP=str(desktop), TEST_CAPTURE=str(capture), PATH=f'{bindir}:/usr/bin:/bin')
    subprocess.run(['bash', str(script), 'a b', '--test'], cwd='/', env=env, check=True)
    assert capture.read_text().splitlines() == [str(game), 'a b', '--test']
    shortcut = desktop / 'arenamp-portable.desktop'
    first = shortcut.read_bytes()
    subprocess.run(['bash', str(script), '--install-shortcut'], env=env, check=True)
    assert shortcut.read_bytes() == first
    assert len(list(desktop.iterdir())) == 1
    line = next(x[5:] for x in shortcut.read_text().splitlines() if x.startswith('Exec='))
    # Desktop Entry unescaping precedes Exec argument parsing.
    decoded = line.replace('\\\\', '\\').replace('\\n', '\n').replace('\\r', '\r')
    args = shlex.split(decoded)
    args = [arg.replace('\\$', '$').replace('\\`', '`').replace('%%', '%') for arg in args]
    subprocess.run(args, cwd='/', env=env, check=True)
    assert capture.read_text().splitlines() == [str(game)]
    capture.unlink()
    subprocess.run(['bash', str(script), '--install-shortcut'], env=env, check=True)
    assert not capture.exists()
print('PASS: portable launch, argument forwarding, Unicode/special paths, shortcut launch, idempotency')
