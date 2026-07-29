#include "buildmanifest.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

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
}

Config::BuildManifest::BuildManifest()
{
    clear();
}

void Config::BuildManifest::clear()
{
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
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }

    QTextStream stream(&file);
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

        if ((section == QLatin1String("build") || section.isEmpty())
            && (key == QLatin1String("format") || key == QLatin1String("version")))
        {
            bool ok = false;
            const int parsed = value.toInt(&ok);
            if (ok && parsed > 0)
                formatVersion = parsed;
        }
        else if ((section == QLatin1String("build") || section.isEmpty())
            && (key == QLatin1String("name") || key == QLatin1String("build-name")))
            buildName = value;
        else if ((section == QLatin1String("build") || section.isEmpty())
            && (key == QLatin1String("data") || key == QLatin1String("data-path") || key == QLatin1String("datafiles")))
            dataPath = value;
        else if ((section == QLatin1String("build") || section.isEmpty())
            && (key == QLatin1String("language") || key == QLatin1String("locale")))
        {
            language = canonicalLanguage(value);
            languageSpecified = !value.trimmed().isEmpty();
        }
        else if ((section == QLatin1String("build") || section.isEmpty())
            && (key == QLatin1String("complete") || key == QLatin1String("locked")
                || key == QLatin1String("read-only")))
            complete = value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
                || value == QLatin1String("1") || value.compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0;
        else if ((section == QLatin1String("server") || section.isEmpty())
            && (key == QLatin1String("address") || key == QLatin1String("ip") || key == QLatin1String("host")))
        {
            serverAddress = value;
            serverAddressSpecified = !value.trimmed().isEmpty();
        }
        else if ((section == QLatin1String("server") || section.isEmpty()) && key == QLatin1String("port"))
        {
            serverPort = value;
            serverPortSpecified = !value.trimmed().isEmpty();
        }
        else if ((section == QLatin1String("server") || section.isEmpty())
            && (key == QLatin1String("vanilla-build-server") || key == QLatin1String("vanilla")
                || key == QLatin1String("legacy-client")))
            vanillaServerCompatibility = value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
                || value == QLatin1String("1") || value.compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0;
        else if ((section == QLatin1String("content") || section.isEmpty()) && isContentExtensionKey(key))
            contentFiles.append(value);
        else if ((section == QLatin1String("content") || section.isEmpty())
            && (key == QLatin1String("groundcover") || key == QLatin1String("grass")))
            groundcoverFiles.append(value);
        else if ((section == QLatin1String("archives") || section == QLatin1String("content") || section.isEmpty())
            && (key == QLatin1String("archive") || key == QLatin1String("bsa") || key == QLatin1String("fallback-archive")))
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

    stream << "[Server]\n";
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
    static const QStringList supportedLanguages = {
        QStringLiteral("English"),
        QStringLiteral("French"),
        QStringLiteral("German"),
        QStringLiteral("Italian"),
        QStringLiteral("Polish"),
        QStringLiteral("Russian"),
        QStringLiteral("Spanish")
    };

    for (const QString& supported : supportedLanguages)
    {
        if (value.compare(supported, Qt::CaseInsensitive) == 0)
            return supported;
    }

    return value.isEmpty() ? QStringLiteral("English") : value;
}

QString Config::BuildManifest::canonicalPathForDataDir(const QString& dataDir)
{
    QDir dir(QDir::cleanPath(dataDir));
    if (dir.dirName().compare(QStringLiteral("Data Files"), Qt::CaseInsensitive) == 0)
    {
        dir.cdUp();
        return dir.absoluteFilePath(QStringLiteral("build.ini"));
    }
    return dir.absoluteFilePath(QStringLiteral("build.ini"));
}

QString Config::BuildManifest::findForDataDir(const QString& dataDir)
{
    const QString canonical = canonicalPathForDataDir(dataDir);
    if (QFileInfo::exists(canonical))
        return canonical;

    const QString inside = QDir(QDir::cleanPath(dataDir)).absoluteFilePath(QStringLiteral("build.ini"));
    if (QFileInfo::exists(inside))
        return inside;

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
