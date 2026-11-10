#ifndef ARENA_UPDATECONTROLLER_HPP
#define ARENA_UPDATECONTROLLER_HPP
#include <QString>
class QWidget;
namespace Launcher
{
    namespace UpdateController
    {
        enum class CheckResult { NoUpdate, UpdateAvailable };
        CheckResult checkAvailable(QWidget* parent, const QString& manifestPath, const QString& dataPath);
        bool startUpdate(QWidget* parent, const QString& manifestPath, const QString& dataPath);
        void showResult(QWidget* parent, const QString& manifestPath);
    }
}
#endif
