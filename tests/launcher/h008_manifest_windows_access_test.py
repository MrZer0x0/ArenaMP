"""H008 source-level regression: Windows manifest replacement (no Qt toolchain here)."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
src = (root / "apps/launcher/maindialog.cpp").read_text(encoding="utf-8")
server = (root / "apps/launcher/serverdialog.cpp").read_text(encoding="utf-8")
start = src.index("bool Launcher::MainDialog::ensureServerDataFileHashes(")
end = src.index("void Launcher::MainDialog::updateServerDataFileHashes()", start)
fn = src[start:end]

# The old QFile handle MUST be destroyed BEFORE QSaveFile::commit on Windows.
block_start = fn.index("    {\n        QFile existing(path);")
block_end = fn.index("    } // No read handle to the target survives beyond this point.", block_start)
read_block = fn[block_start:block_end]
assert "existing.readAll()" in read_block
assert "existing.close();" in read_block
assert read_block.index("existing.readAll()") < read_block.index("existing.close();")
assert block_end < fn.index("QSaveFile output(path);") < fn.index("output.commit()")
assert fn.index("QFile::setPermissions(path,") < fn.index("QSaveFile output(path);")
assert "output.cancelWriting();" in fn
assert "verify.readAll()" in fn

# Preserve the pre-flight guarantee and ordered filename/CRC comparison.
assert "mServerDialog->setPreStartCheck(" in src
assert "old.object() == root" in fn
assert "boost::crc_32_type" in fn
server_start = server.index("bool Launcher::ServerDialog::startServer()")
server_end = server.index("bool Launcher::ServerDialog::isRunning()", server_start)
start_fn = server[server_start:server_end]
assert start_fn.index("mPreStartCheck(&preparationError)") < start_fn.index("mProcess->start()")
print("PASS: comparison handle is closed before atomic Windows manifest replacement")
print("PASS: read-only handling, failure cancellation, disk verification and pre-launch check remain")
