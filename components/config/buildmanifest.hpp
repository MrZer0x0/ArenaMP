#ifndef BUILDMANIFEST_HPP
#define BUILDMANIFEST_HPP

#include <QString>
#include <QStringList>

namespace Config
{
    /// Portable ArenaMP build description stored next to a Data Files folder.
    /// The file intentionally keeps ordered, repeated content/archive entries.
    class BuildManifest
    {
    public:
        BuildManifest();

        void clear();

        bool read(const QString& filePath, QString* errorMessage = nullptr);
        bool write(const QString& filePath, QString* errorMessage = nullptr) const;

        QString resolvedDataPath(const QString& manifestPath) const;

        static QString canonicalPathForDataDir(const QString& dataDir);
        static QString findForDataDir(const QString& dataDir);
        static QString websiteForManifest(const QString& manifestPath);
        static QString portableDataPath(const QString& manifestPath, const QString& dataDir);
        static QString withUpdateDefaults(const QString& text);
        static QString canonicalLanguage(const QString& language);

        bool useAlternativeServer;
        QString contentVersion;
        QString engineBuild;
        QString projectUrl;
        QString checkUrl;
        QString updateUrl;
        QString windowsUrl;
        QString linuxUrl;
        QString macosUrl;
        QString androidUrl;
        QString altAddress;
        QString altPort;
        int formatVersion;
        QString buildName;
        QString dataPath;
        QString language;
        bool languageSpecified;
        QString serverAddress;
        bool serverAddressSpecified;
        QString serverPort;
        bool serverPortSpecified;
        bool vanillaServerCompatibility;
        bool complete;
        QStringList contentFiles;
        QStringList groundcoverFiles;
        QStringList archives;
    };
}

#endif
