#ifndef ARENAMP_DESKTOPSHORTCUT_HPP
#define ARENAMP_DESKTOPSHORTCUT_HPP

#include <QCoreApplication>
#include <string>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#endif

namespace Launcher
{
    // No shell or script interpreter: Unicode paths are passed directly to the OS.
    inline bool createDesktopShortcut(const QString& buildName)
    {
        const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        if (desktop.isEmpty() || !QDir().mkpath(desktop))
            return false;
        QString name = buildName.trimmed();
        name.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]")), QStringLiteral("_"));
        name = name.left(120);
        while (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')))
            name.chop(1);
        if (name.isEmpty())
            name = QStringLiteral("ArenaMP");
        if (QRegularExpression(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)"),
                QRegularExpression::CaseInsensitiveOption).match(name).hasMatch())
            name.prepend(QLatin1Char('_'));
        const QString executable = QCoreApplication::applicationFilePath();
        const QString workingDirectory = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
        const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(init) && init != RPC_E_CHANGED_MODE)
            return false;
        IShellLinkW* link = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
            IID_IShellLinkW, reinterpret_cast<void**>(&link));
        if (SUCCEEDED(hr))
        {
            IPersistFile* file = nullptr;
            hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file));
            if (SUCCEEDED(hr))
            {
                QString destination = QDir(desktop).filePath(name + QStringLiteral(".lnk"));
                // Preserve unrelated desktop shortcuts with the same display name.
                for (int suffix = 2; QFileInfo::exists(destination); ++suffix)
                {
                    wchar_t target[32768] = {};
                    const std::wstring oldPath = QDir::toNativeSeparators(destination).toStdWString();
                    if (SUCCEEDED(file->Load(oldPath.c_str(), STGM_READ))
                        && SUCCEEDED(link->GetPath(target, 32768, nullptr, SLGP_RAWPATH))
                        && QDir::cleanPath(QDir::fromNativeSeparators(QString::fromWCharArray(target)))
                            .compare(QDir::cleanPath(executable), Qt::CaseInsensitive) == 0)
                        break;
                    destination = QDir(desktop).filePath(name + QStringLiteral(" - ArenaMP %1.lnk").arg(suffix));
                }
                const std::wstring target = QDir::toNativeSeparators(executable).toStdWString();
                const std::wstring directory = QDir::toNativeSeparators(workingDirectory).toStdWString();
                const std::wstring output = QDir::toNativeSeparators(destination).toStdWString();
                hr = link->SetPath(target.c_str());
                if (SUCCEEDED(hr)) hr = link->SetArguments(L"");
                if (SUCCEEDED(hr)) hr = link->SetWorkingDirectory(directory.c_str());
                if (SUCCEEDED(hr)) hr = link->SetDescription(buildName.toStdWString().c_str());
                if (SUCCEEDED(hr)) hr = link->SetIconLocation(target.c_str(), 0);
                if (SUCCEEDED(hr)) hr = link->SetShowCmd(SW_SHOWNORMAL);
                if (SUCCEEDED(hr)) hr = file->Save(output.c_str(), TRUE);
                file->Release();
            }
            link->Release();
        }
        if (SUCCEEDED(init)) CoUninitialize();
        return SUCCEEDED(hr);
#elif defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
        auto escapeValue = [](QString value) {
            value.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
            value.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
            value.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
            value.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
            return value;
        };
        QString command = executable;
        command.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
        command.replace(QStringLiteral("\""), QStringLiteral("\\\""));
        command.replace(QStringLiteral("`"), QStringLiteral("\\`"));
        command.replace(QStringLiteral("$"), QStringLiteral("\\$"));
        command.replace(QStringLiteral("%"), QStringLiteral("%%"));
        const QByteArray identity = QCryptographicHash::hash(executable.toUtf8(), QCryptographicHash::Sha256).toHex();
        const QByteArray marker = "X-ArenaMP-Launcher=" + identity + '\n';
        QString destination = QDir(desktop).filePath(name + QStringLiteral(".desktop"));
        for (int suffix = 2; QFileInfo::exists(destination); ++suffix)
        {
            QFile previous(destination);
            if (previous.open(QIODevice::ReadOnly) && previous.readAll().contains(marker))
                break;
            destination = QDir(desktop).filePath(name + QStringLiteral(" - ArenaMP %1.desktop").arg(suffix));
        }
        const QByteArray content = QStringLiteral("[Desktop Entry]\nType=Application\nVersion=1.0\nName=%1\nExec=%2\nPath=%3\nIcon=openmw\nTerminal=false\nCategories=Game;RolePlaying;\n")
            .arg(escapeValue(buildName), escapeValue(QLatin1Char('"') + command + QLatin1Char('"')),
                escapeValue(workingDirectory)).toUtf8() + marker;
        QSaveFile file(destination);
        if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size() || !file.commit())
            return false;
        return QFile::setPermissions(destination, QFileDevice::ReadOwner | QFileDevice::WriteOwner
            | QFileDevice::ExeOwner | QFileDevice::ReadGroup | QFileDevice::ExeGroup
            | QFileDevice::ReadOther | QFileDevice::ExeOther);
#else
        return QFile::link(executable, QDir(desktop).filePath(name));
#endif
    }
}
#endif
