#ifndef OPENMW_COMPONENTS_MISC_ARENAGLASSICONS_H
#define OPENMW_COMPONENTS_MISC_ARENAGLASSICONS_H
#include <QIcon>
#include <QImageReader>
namespace ArenaUi
{
    inline QIcon glassIcon(const QString& name)
    {
        // SVG is preferred. Embedded 4x PNGs keep the same artwork visible in
        // portable installs that do not ship Qt's optional SVG image plugin.
        const QString suffix = QImageReader::supportedImageFormats().contains("svg")
            ? QStringLiteral(".svg") : QStringLiteral(".png");
        return QIcon(QStringLiteral(":/arena/arenaicons/") + name + suffix);
    }
}
#endif
