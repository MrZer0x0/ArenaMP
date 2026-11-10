"""Source-level regression: desktop source manifest is prepared on both CRC paths.
Not a substitute for a compiled Qt launcher test on Windows.
"""
from pathlib import Path
root = Path(__file__).resolve().parents[2]
text = (root / 'apps/launcher/maindialog.cpp').read_text(encoding='utf-8')
start = text.index('bool Launcher::MainDialog::ensureServerDataFileHashes(')
end = text.index('void Launcher::MainDialog::updateServerDataFileHashes()', start)
body = text[start:end]
assert 'resolveSelectedDataFilePath(fileName, selectedPaths)' in body
assert 'sources.insert(QStringLiteral("files"), resolvedFiles)' in body
assert 'sources.insert(QStringLiteral("dataDirs"), directories)' in body
assert 'custom/arenaFurnitureSources.json' in body
assert 'QSaveFile output(sourcePath);' in body
assert 'return saveFurnitureSources();' in body, 'Unchanged CRC manifest must also create/update source paths'
assert 'if (!saveFurnitureSources()) return false;' in body, 'Changed CRC manifest must update source paths'
assert body.index('verify.readAll()') < body.index('if (!saveFurnitureSources()) return false;')
assert 'mServerDialog->setPreStartCheck(' in text
print('PASS H011 PC: active mod paths saved and verified before starting the server (unchanged/changed CRC)')
