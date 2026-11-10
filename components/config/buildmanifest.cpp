#include "buildmanifest.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>
#include <components/misc/buildupdatedefaults.hpp>
#include <QBuffer>

namespace
{
    QString decodeValue(QString value)
    {
        value = value.trimmed();
        if (value.size() < 2 || value.front() != QLatin1Char('"') || value.back() != QLatin1Char('"'))
            return value;

        value = value.mid(1, value.size() - 2);
        QString decoded;
        decoded.reserve(value.size());
        bool escaped = false;
        for (const QChar ch : value)
        {
            if (escaped)
            {
                if (ch == QLatin1Char('n'))
                    decoded += QLatin1Char('\n');
                else if (ch == QLatin1Char('r'))
                    decoded += QLatin1Char('\r');
                else if (ch == QLatin1Char('t'))
                    decoded += QLatin1Char('\t');
                else
                    decoded += ch;
                escaped = false;
            }
            else if (ch == QLatin1Char('\\'))
                escaped = true;
            else
                decoded += ch;
        }
        if (escaped)
            decoded += QLatin1Char('\\');
        return decoded;
    }

    QString encodeValue(const QString& value)
    {
        QString encoded;
        encoded.reserve(value.size() + 2);
        encoded += QLatin1Char('"');
        for (const QChar ch : value)
        {
            if (ch == QLatin1Char('\\') || ch == QLatin1Char('"'))
                encoded += QLatin1Char('\\');
            if (ch == QLatin1Char('\n'))
                encoded += QStringLiteral("\\n");
            else if (ch == QLatin1Char('\r'))
                encoded += QStringLiteral("\\r");
            else if (ch == QLatin1Char('\t'))
                encoded += QStringLiteral("\\t");
            else
                encoded += ch;
        }
        encoded += QLatin1Char('"');
        return encoded;
    }

    bool isContentExtensionKey(const QString& key)
    {
        return key == QLatin1String("content") || key == QLatin1String("plugin")
            || key == QLatin1String("esm") || key == QLatin1String("esp")
            || key == QLatin1String("omwgame") || key == QLatin1String("omwaddon");
    }

    bool isBuildSection(const QString& section)
    {
        return section.isEmpty() || section == QLatin1String("build")
            || section == QLatin1String("general") || section == QLatin1String("manifest");
    }

    bool isServerSection(const QString& section)
    {
        return section.isEmpty() || section == QLatin1String("server")
            || section == QLatin1String("network") || section == QLatin1String("connection");
    }

}

Config::BuildManifest::BuildManifest()
{
    clear();
}

void Config::BuildManifest::clear()
{
    useAlternativeServer = false;
    contentVersion = QStringLiteral("9999999");
    engineBuild = QStringLiteral("1");
    projectUrl = QStringLiteral("");
    checkUrl = QStringLiteral("https://mro1.myarena.site/build/check.ini");
    updateUrl = QStringLiteral("");
    windowsUrl = QStringLiteral("https://mro1.myarena.site/build/ArenaMP-Windows-x64-main.zip");
    linuxUrl = QStringLiteral("https://mro1.myarena.site/build/ArenaMP-Linux-x86_64-SteamDeck-main.tar.gz");
    macosUrl = QStringLiteral("");
    androidUrl = QStringLiteral("https://mro1.myarena.site/build/ArenaMP-arm64-client-server.apk");
    altAddress = QStringLiteral("");
    altPort = QStringLiteral("25565");
    formatVersion = 1;
    buildName = QStringLiteral("ArenaMP");
    dataPath.clear();
    language = QStringLiteral("English");
    languageSpecified = false;
    serverAddress = QStringLiteral("127.0.0.1");
    serverAddressSpecified = false;
    serverPort = QStringLiteral("25565");
    serverPortSpecified = false;
    vanillaServerCompatibility = false;
    complete = false;
    contentFiles.clear();
    groundcoverFiles.clear();
    archives.clear();
}

bool Config::BuildManifest::read(const QString& filePath, QString* errorMessage)
{
    clear();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    QByteArray bytes = file.readAll();
    file.close();
    if (QFileInfo(filePath).fileName().compare(QStringLiteral("build.ini"), Qt::CaseInsensitive) == 0)
    {
        const QByteArray updated = withUpdateDefaults(QString::fromUtf8(bytes)).toUtf8();
        if (updated != bytes)
        {
            QSaveFile target(filePath);
            if (target.open(QIODevice::WriteOnly) && target.write(updated) == updated.size())
                target.commit();
            // Read-only distributions still get defaults for this launch.
            bytes = updated;
        }
    }
    QBuffer input(&bytes);
    input.open(QIODevice::ReadOnly);
    QTextStream stream(&input);
    stream.setCodec("UTF-8");
    QString section;

    while (!stream.atEnd())
    {
        QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')))
            continue;

        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']')))
        {
            section = line.mid(1, line.size() - 2).trimmed().toLower();
            continue;
        }

        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;

        const QString key = line.left(equals).trimmed().toLower();
        const QString value = decodeValue(line.mid(equals + 1));

        if (isBuildSection(section)
            && key == QLatin1String("format"))
        {
            bool ok = false;
            const int parsed = value.toInt(&ok);
            if (ok && parsed > 0)
                formatVersion = parsed;
        }
        else if (isBuildSection(section) && key == QLatin1String("version"))
            contentVersion = value;
        else if (isBuildSection(section) && key == QLatin1String("build"))
            engineBuild = value;
        else if (isBuildSection(section) && key == QLatin1String("url"))
            projectUrl = value;
        else if (isBuildSection(section) && key == QLatin1String("url_check"))
            checkUrl = value;
        else if (isBuildSection(section) && (key == QLatin1String("url_update") || key == QLatin1String("update") || key == QLatin1String("update-url") || key == QLatin1String("update_url")))
            updateUrl = value;
        else if (isBuildSection(section) && key == QLatin1String("url_win"))
            windowsUrl = value;
        else if (isBuildSection(section) && key == QLatin1String("url_linux"))
            linuxUrl = value;
        else if (isBuildSection(section) && key == QLatin1String("url_macos"))
            macosUrl = value;
        else if (isBuildSection(section) && key == QLatin1String("url_android"))
            androidUrl = value;
        else if (isServerSection(section) && (key == QLatin1String("alt_adress") || key == QLatin1String("alt_address")))
            altAddress = value;
        else if (isServerSection(section) && key == QLatin1String("alt_port"))
            altPort = value;
        else if (isServerSection(section) && key == QLatin1String("use_alt_server"))
            useAlternativeServer = value == QLatin1String("1") || value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
        else if (isBuildSection(section)
            && (key == QLatin1String("name") || key == QLatin1String("build-name")))
            buildName = value;
        else if (isBuildSection(section)
            && (key == QLatin1String("data") || key == QLatin1String("data-path") || key == QLatin1String("datafiles")))
            dataPath = value;
        else if (key == QLatin1String("language") || key == QLatin1String("locale")
            || ((section == QLatin1String("language") || section == QLatin1String("locale"))
                && (key == QLatin1String("value") || key == QLatin1String("name")
                    || key == QLatin1String("selected") || key == QLatin1String("current"))))
        {
            language = canonicalLanguage(value);
            languageSpecified = !value.trimmed().isEmpty();
        }
        else if (isBuildSection(section)
            && (key == QLatin1String("complete") || key == QLatin1String("locked")
                || key == QLatin1String("read-only")))
            complete = value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
                || value == QLatin1String("1") || value.compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0;
        else if (isServerSection(section)
            && (key == QLatin1String("address") || key == QLatin1String("ip") || key == QLatin1String("host")))
        {
            serverAddress = value;
            serverAddressSpecified = !value.trimmed().isEmpty();
        }
        else if (isServerSection(section) && key == QLatin1String("port"))
        {
            serverPort = value;
            serverPortSpecified = !value.trimmed().isEmpty();
        }
        else if (isServerSection(section)
            && (key == QLatin1String("vanilla-build-server") || key == QLatin1String("vanilla")
                || key == QLatin1String("legacy-client")))
            vanillaServerCompatibility = value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
                || value == QLatin1String("1") || value.compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0;
        else if (isContentExtensionKey(key))
            contentFiles.append(value);
        else if (key == QLatin1String("groundcover") || key == QLatin1String("grass"))
            groundcoverFiles.append(value);
        else if (key == QLatin1String("archive") || key == QLatin1String("bsa")
            || key == QLatin1String("fallback-archive"))
            archives.append(value);
    }

    if (buildName.trimmed().isEmpty())
        buildName = QStringLiteral("ArenaMP");
    language = canonicalLanguage(language);
    if (serverAddress.trimmed().isEmpty())
        serverAddress = QStringLiteral("127.0.0.1");
    if (serverPort.trimmed().isEmpty())
        serverPort = QStringLiteral("25565");

    return true;
}

bool Config::BuildManifest::write(const QString& filePath, QString* errorMessage) const
{
    const QFileInfo info(filePath);
    QDir dir = info.absoluteDir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("Could not create %1").arg(dir.absolutePath());
        return false;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << "# ArenaMP portable build manifest\n";
    stream << "# Ordered entries are applied exactly as written.\n\n";
    stream << "[Build]\n";
    stream << "format=" << (formatVersion > 0 ? formatVersion : 1) << "\n";
    stream << "name=" << encodeValue(buildName.trimmed().isEmpty() ? QStringLiteral("ArenaMP") : buildName.trimmed()) << "\n";
    stream << "data-path=" << encodeValue(dataPath) << "\n";
    stream << "language=" << encodeValue(canonicalLanguage(language)) << "\n";
    stream << "complete=" << (complete ? "true" : "false") << "\n\n";

    stream << "version=" << encodeValue(contentVersion) << "\n";
    stream << "build=" << encodeValue(engineBuild) << "\n";
    stream << "url=" << encodeValue(projectUrl) << "\n";
    stream << "url_check=" << encodeValue(checkUrl) << "\n";
    stream << "url_update=" << encodeValue(updateUrl) << "\n";
    stream << "url_win=" << encodeValue(windowsUrl) << "\n";
    stream << "url_linux=" << encodeValue(linuxUrl) << "\n";
    stream << "url_macos=" << encodeValue(macosUrl) << "\n";
    stream << "url_android=" << encodeValue(androidUrl) << "\n";

    stream << "\n[Server]\n";
    stream << "use_alt_server=" << (useAlternativeServer ? "true" : "false") << "\n";
    stream << "alt_adress=" << encodeValue(altAddress) << "\n";
    stream << "alt_port=" << encodeValue(altPort) << "\n";
    if (serverAddressSpecified)
        stream << "address=" << encodeValue(serverAddress.trimmed().isEmpty() ? QStringLiteral("127.0.0.1") : serverAddress.trimmed()) << "\n";
    if (serverPortSpecified)
        stream << "port=" << encodeValue(serverPort.trimmed().isEmpty() ? QStringLiteral("25565") : serverPort.trimmed()) << "\n";
    stream << "vanilla-build-server=" << (vanillaServerCompatibility ? "true" : "false") << "\n\n";

    stream << "[Content]\n";
    for (const QString& fileName : contentFiles)
        stream << "content=" << encodeValue(fileName) << "\n";
    for (const QString& fileName : groundcoverFiles)
        stream << "groundcover=" << encodeValue(fileName) << "\n";

    stream << "\n[Archives]\n";
    for (const QString& archive : archives)
        stream << "archive=" << encodeValue(archive) << "\n";

    stream.flush();
    if (stream.status() != QTextStream::Ok || !file.commit())
    {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }
    return true;
}

QString Config::BuildManifest::resolvedDataPath(const QString& manifestPath) const
{
    const QFileInfo manifestInfo(manifestPath);
    const QDir manifestDir = manifestInfo.absoluteDir();

    if (!dataPath.trimmed().isEmpty())
    {
        const QFileInfo pathInfo(dataPath);
        if (pathInfo.isAbsolute())
            return QDir::cleanPath(pathInfo.absoluteFilePath());
        return QDir::cleanPath(manifestDir.absoluteFilePath(dataPath));
    }

    if (manifestDir.dirName().compare(QStringLiteral("Data Files"), Qt::CaseInsensitive) == 0)
        return QDir::cleanPath(manifestDir.absolutePath());

    const QString siblingDataFiles = manifestDir.absoluteFilePath(QStringLiteral("Data Files"));
    if (QFileInfo(siblingDataFiles).isDir())
        return QDir::cleanPath(siblingDataFiles);

    return QDir::cleanPath(manifestDir.absolutePath());
}

QString Config::BuildManifest::canonicalLanguage(const QString& language)
{
    const QString value = language.trimmed();
    if (value.isEmpty())
        return QStringLiteral("English");

    struct LanguageAlias
    {
        const char* canonical;
        const char* aliases[4];
    };

    // Accept both the canonical identifiers written by current ArenaMP builds
    // and translated values produced by older Launcher/Wizard revisions.
    static const LanguageAlias supportedLanguages[] = {
        { "English", { "English", "Английский", "Anglais", nullptr } },
        { "French",  { "French", "Французский", "Français", nullptr } },
        { "German",  { "German", "Немецкий", "Deutsch", nullptr } },
        { "Italian", { "Italian", "Итальянский", "Italiano", nullptr } },
        { "Polish",  { "Polish", "Польский", "Polski", nullptr } },
        { "Russian", { "Russian", "Русский", "Русский язык", nullptr } },
        { "Spanish", { "Spanish", "Испанский", "Español", nullptr } }
    };

    for (const LanguageAlias& languageAlias : supportedLanguages)
    {
        for (const char* alias : languageAlias.aliases)
        {
            if (alias == nullptr)
                break;
            if (value.compare(QString::fromUtf8(alias), Qt::CaseInsensitive) == 0)
                return QString::fromLatin1(languageAlias.canonical);
        }
    }

    return value;
}

QString Config::BuildManifest::canonicalPathForDataDir(const QString& dataDir)
{
    // build.ini describes one concrete Data Files installation, therefore its
    // canonical location is beside that Data Files directory. This also keeps
    // several independent Morrowind installations from sharing one manifest.
    QDir dataDirectory(QDir::cleanPath(dataDir));
    if (dataDirectory.dirName().compare(QStringLiteral("Data Files"), Qt::CaseInsensitive) == 0)
    {
        QDir parentDirectory(dataDirectory);
        if (parentDirectory.cdUp())
            return parentDirectory.absoluteFilePath(QStringLiteral("build.ini"));
    }

    // Non-standard data directory names keep the manifest directly in that
    // selected directory, since there is no reliable Data Files parent marker.
    if (!dataDirectory.absolutePath().isEmpty())
        return dataDirectory.absoluteFilePath(QStringLiteral("build.ini"));

    // Last-resort fallback used only before a valid data directory is known.
    return QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("build.ini"));
}

QString Config::BuildManifest::findForDataDir(const QString& dataDir)
{
    QStringList candidates;

    // The manifest beside Data Files is authoritative and must be checked
    // before legacy portable locations.
    const QString canonicalManifest = canonicalPathForDataDir(dataDir);
    if (!canonicalManifest.isEmpty())
        candidates.append(canonicalManifest);

    QDir dataDirectory(QDir::cleanPath(dataDir));
    const QString legacyInsideDataFiles
        = dataDirectory.absoluteFilePath(QStringLiteral("build.ini"));
    if (!candidates.contains(legacyInsideDataFiles, Qt::CaseInsensitive))
        candidates.append(legacyInsideDataFiles);

    // Backward-compatible fallbacks for builds created by earlier patches.
    const QString applicationDir = QCoreApplication::applicationDirPath();
    if (!applicationDir.isEmpty())
    {
        const QString besideExecutable
            = QDir(applicationDir).absoluteFilePath(QStringLiteral("build.ini"));
        if (!candidates.contains(besideExecutable, Qt::CaseInsensitive))
            candidates.append(besideExecutable);
    }

    const QString workingDirectoryManifest
        = QDir::current().absoluteFilePath(QStringLiteral("build.ini"));
    if (!candidates.contains(workingDirectoryManifest, Qt::CaseInsensitive))
        candidates.append(workingDirectoryManifest);

    for (const QString& candidate : candidates)
    {
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile() && info.isReadable())
            return QDir::cleanPath(info.absoluteFilePath());
    }

    return QString();
}

QString Config::BuildManifest::portableDataPath(const QString& manifestPath, const QString& dataDir)
{
    const QDir manifestDir = QFileInfo(manifestPath).absoluteDir();
    QString relative = manifestDir.relativeFilePath(QDir::cleanPath(dataDir));
    if (relative.isEmpty())
        relative = QStringLiteral(".");
    return QDir::fromNativeSeparators(relative);
}

QString Config::BuildManifest::websiteForManifest(const QString& manifestPath)
{
    const QDir directory = manifestPath.isEmpty()
        ? QDir(QCoreApplication::applicationDirPath()) : QFileInfo(manifestPath).absoluteDir();
    // build.com is a website-only override; never execute it or use it to
    // replace the update/content manifest and its ordered content lists.
    const auto files = directory.entryInfoList(QDir::Files | QDir::Readable);
    for (const QFileInfo& file : files)
    {
        if (file.fileName().compare(QStringLiteral("build.com"), Qt::CaseInsensitive) != 0) continue;
        BuildManifest site;
        if (site.read(file.absoluteFilePath()) && !site.projectUrl.trimmed().isEmpty())
            return site.projectUrl.trimmed();
    }
    BuildManifest site;
    if (!manifestPath.isEmpty() && site.read(manifestPath) && !site.projectUrl.trimmed().isEmpty())
        return site.projectUrl.trimmed();
    return QStringLiteral("https://t.me/arena_mp");
}

QString Config::BuildManifest::withUpdateDefaults(const QString& text)
{
    const auto updated=Misc::withArenaUpdateDefaults(text.toUtf8().toStdString());
    return QString::fromUtf8(updated.data(),static_cast<int>(updated.size()));
}
