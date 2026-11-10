#include <apps/launcher/updatecontroller.hpp>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QProgressDialog>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <stdexcept>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    QByteArray read(const QString& path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setApplicationName(QStringLiteral("ArenaUpdaterUiTest"));
    using namespace Launcher::UpdateController;

    try
    {
        QTemporaryDir root;
        require(root.isValid(), "Cannot create test directory");
        const QString data = QDir(root.path()).filePath(QStringLiteral("Data Files"));
        require(QDir().mkpath(data), "Cannot create test Data Files");
        const QString log = QDir(root.path()).filePath(QStringLiteral("Update.log"));

        const auto scenario = [&](const char* name, CheckResult expected, bool cancel) {
            const QString path = QDir(root.path()).filePath(QString::fromLatin1(name) + QStringLiteral(".ini"));
            QFile manifest(path);
            require(manifest.open(QIODevice::WriteOnly), "Cannot write test manifest");
            manifest.write("[Build]\nformat=1\nversion=00001\nbuild=00001\n[Server]\naddress=178.20.47.31\nport=25565\n");
            manifest.close();
            const int previousLogSize = read(log).size();

            bool userClosedDialog = false;
            QTimer userCancel;
            QObject::connect(&userCancel, &QTimer::timeout, &app, [&]() {
                for (QWidget* widget : QApplication::topLevelWidgets())
                {
                    QProgressDialog* progress = qobject_cast<QProgressDialog*>(widget);
                    if (progress != nullptr && progress->isVisible())
                    {
                        userClosedDialog = true;
                        userCancel.stop();
                        progress->close(); // Real user-close path must still cancel.
                        break;
                    }
                }
            });
            if (cancel) userCancel.start(20);
            const CheckResult actual = checkAvailable(nullptr, path, data);
            userCancel.stop();
            require(actual == expected, name);
            const QByteArray events = read(log).mid(previousLogSize);
            require(events.contains("check_finished"), "Missing final check result in Update.log");
            if (cancel)
            {
                require(userClosedDialog, "Test did not close the progress dialog");
                require(events.contains("check_cancelled"), "Genuine user cancellation was ignored");
                require(events.contains("cancelled=1"), "User cancellation was not retained");
            }
            else
            {
                require(!events.contains("check_cancelled"), "Programmatic close was mistaken for user cancellation");
                require(events.contains("cancelled=0"), "Successful completion was marked cancelled");
                if (expected == CheckResult::UpdateAvailable)
                    require(events.contains("button=Update"), "Available update was reset to Play");
            }
        };

        scenario("both_available", CheckResult::UpdateAvailable, false);
        scenario("exit_only", CheckResult::UpdateAvailable, false);
        scenario("no_update", CheckResult::NoUpdate, false);
        scenario("offline", CheckResult::NoUpdate, false);
        scenario("cancel", CheckResult::NoUpdate, true);
        std::puts("Updater Qt UI: 5 scenarios passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "Updater Qt UI FAILED: %s\n", error.what());
        return 1;
    }
}
