"""Static integration guards for H004; NOT a substitute for Qt/Android runtime tests."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
main = (root / 'apps/launcher/maindialog.cpp').read_text(encoding='utf-8')
head = (root / 'apps/launcher/maindialog.hpp').read_text(encoding='utf-8')
server = (root / 'apps/launcher/serverdialog.cpp').read_text(encoding='utf-8')
server_head = (root / 'apps/launcher/serverdialog.hpp').read_text(encoding='utf-8')
service = (root / 'android/app/src/main/java/server/ArenaServerService.kt').read_text(encoding='utf-8')
android = (root / 'android/app/src/main/java/server/ServerDataFiles.kt').read_text(encoding='utf-8')

def body(src, marker, following=None):
    assert marker in src, marker
    tail = src.split(marker, 1)[1]
    return tail.split(following, 1)[0] if following else tail

start = body(server, 'bool Launcher::ServerDialog::startServer()', 'bool Launcher::ServerDialog::isRunning() const')
assert start.index('preparePortableServer') < start.index('mPreStartCheck(&preparationError)') < start.index('mProcess->start()')
assert 'return false;' in start.split('mPreStartCheck(&preparationError)', 1)[1].split('const ServerConfig config', 1)[0]
assert 'setPreStartCheck(std::function<bool(QString*)>' in server_head
assert 'mServerDialog->setPreStartCheck(' in main
assert 'ensureServerDataFileHashes(error)' in main
assert 'ensureServerDataFileHashes(QString* errorMessage' in head
check = body(main, 'bool Launcher::MainDialog::ensureServerDataFileHashes(', 'void Launcher::MainDialog::updateServerDataFileHashes()')
for token in ('mDataFilesPage->saveSettings()', 'getContentList()', 'getGroundcoverList()',
              'boost::crc_32_type', 'old.object() == root', 'QSaveFile output(path)',
              'output.commit()', 'verify.readAll()'):
    assert token in check, token
assert 'ensureServerDataFileHashes(&error, &changed)' in main
assert 'if (mPlayPage != nullptr && mPlayPage->autoStartServer() != running)' in main
assert 'mPlayPage->setAutoStartServer(running)' in main
assert 'mPlayPage->setAutoStartServer(true)' in body(main, 'void Launcher::MainDialog::runServer()', 'void Launcher::MainDialog::stopServer()')
assert service.index('ServerDataFiles.verifyAndUpdateIfNeeded(this)') < service.index('nativeRun(globalRoot')
assert 'BuildManifest.read(ctx)?.takeIf { it.content.isNotEmpty() }' in android
assert 'writeAtomic(required, expected.toString(2)' in android
assert 'check(required.isFile && matches(expected,' in android
assert 'server_hashes_verified' in android
print('H004 source integration guards: PASS (PC pre-launch, Host lifecycle, Android manifest).')
