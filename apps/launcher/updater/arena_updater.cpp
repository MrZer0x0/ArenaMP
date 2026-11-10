#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#ifndef Q_OS_WIN
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#endif
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QRegularExpression>
#include <QSet>
#include <QMap>
#include <QPair>
#include <QThread>
#include <QPushButton>
#include <QSaveFile>
#include <QStorageInfo>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QCloseEvent>
#include <QElapsedTimer>

#include <zlib.h>
#include "serverupdatepolicy.hpp"
#include "../../../components/misc/arenatheme.hpp"
#include "../../../components/misc/arenaglasswindow.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <winhttp.h>
#  include <io.h>
#  include <tlhelp32.h>
#else
#  include <fcntl.h>
#  include <signal.h>
#  include <sys/file.h>
#  include <unistd.h>
#endif

namespace
{
constexpr qint64 Chunk = 1024 * 1024;
constexpr qint64 MaxCheck = 64 * 1024;
constexpr quint64 MaxExpanded = quint64(128) * 1024 * 1024 * 1024;
constexpr quint64 MaxEntries = 500000;

const QSet<QString> ProtectedRoots = {
    QStringLiteral("userdata"), QStringLiteral("saves"),
    QStringLiteral("screenshots"), QStringLiteral("data files"), QStringLiteral("datafiles"),
    QStringLiteral(".arena-update")
};
const QSet<QString> ProtectedFiles = {
    QStringLiteral("build.ini"), QStringLiteral("check.ini"), QStringLiteral("openmw.cfg"),
    QStringLiteral("settings.cfg"), QStringLiteral("launcher.cfg"), QStringLiteral("tes3mp-client.cfg"),
    QStringLiteral("tes3mp-server.cfg"), QStringLiteral("update.log"), QStringLiteral("update.log.old")
};
// U020: host-generated server manifests are user state, never engine payload.
// requiredDataFiles.json is produced by "Update Hash" from the host's exact
// content order/CRC32 list, so an engine archive must not replace it even if a
// package layouts change the location of the server tree.
const QSet<QString> EngineProtectedFileNames = {
    QStringLiteral("requireddatafiles.json")
};

QString gLogPath;
bool gCancelRequested = false;
#ifndef Q_OS_WIN
QNetworkReply* gActiveReply = nullptr;
#endif

class ProgressWindow : public QWidget
{
public:
    ProgressWindow()
    {
        setWindowTitle(QStringLiteral("ArenaMP — Обновление"));
        setMinimumSize(580, 210);
        setWindowFlag(Qt::WindowContextHelpButtonHint, false);
        setWindowFlag(Qt::WindowMinimizeButtonHint, true);
        // The updater is a detached supervisor. Keep its installer window above
        // the launcher that is closing so Windows cannot silently bury it behind
        // another application. This flag is used only for update/prepare/apply;
        // the lightweight `check` action remains headless.
        setWindowFlag(Qt::WindowStaysOnTopHint, true);
        setAttribute(Qt::WA_QuitOnClose, false);

        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(14);
        mTitle = new QLabel(QStringLiteral("<b>Обновление ArenaMP</b>"), this);
        QFont titleFont = mTitle->font();
        const int titleBasePx = titleFont.pixelSize() > 0 ? titleFont.pixelSize() : 13;
        titleFont.setPixelSize(titleBasePx + 3);
        mTitle->setFont(titleFont);
        layout->addWidget(mTitle);

        mStatus = new QLabel(QStringLiteral("Подготовка обновления…"), this);
        mStatus->setWordWrap(true);
        layout->addWidget(mStatus);

        mHint = new QLabel(QStringLiteral("Не закрывайте это окно до завершения установки."), this);
        QFont hintFont = mHint->font();
        const int hintBasePx = hintFont.pixelSize() > 0 ? hintFont.pixelSize() : 13;
        hintFont.setPixelSize(std::max(11, hintBasePx - 1));
        mHint->setFont(hintFont);
        layout->addWidget(mHint);

        mProgress = new QProgressBar(this);
        mProgress->setRange(0, 0);
        mProgress->setTextVisible(true);
        layout->addWidget(mProgress);

        mTransfer = new QLabel(this);
        mTransfer->setText(QStringLiteral("Проверка файлов и версии…"));
        mTransfer->setWordWrap(true);
        layout->addWidget(mTransfer);

        mDetails = new QPlainTextEdit(this);
        mDetails->setReadOnly(true);
        mDetails->setMaximumBlockCount(1000);
        mDetails->setVisible(false);
        mDetails->setMinimumHeight(180);
        layout->addWidget(mDetails);

        auto* buttons = new QHBoxLayout();
        mDetailsButton = new QPushButton(QStringLiteral("Подробнее"), this);
        mCancelButton = new QPushButton(QStringLiteral("Отмена"), this);
        buttons->addWidget(mDetailsButton);
        buttons->addStretch(1);
        buttons->addWidget(mCancelButton);
        layout->addLayout(buttons);

        QObject::connect(mDetailsButton, &QPushButton::clicked, this, [this]() {
            const bool show = !mDetails->isVisible();
            mDetails->setVisible(show);
            mDetailsButton->setText(show ? QStringLiteral("Скрыть подробности") : QStringLiteral("Подробнее"));
            adjustSize();
        });
        QObject::connect(mCancelButton, &QPushButton::clicked, this, []() {
            gCancelRequested = true;
#ifndef Q_OS_WIN
            if (gActiveReply)
                gActiveReply->abort();
#endif
        });
    }

    void present()
    {
        if (!mVisibleTimer.isValid())
            mVisibleTimer.start();
        if (isMinimized())
            setWindowState(windowState() & ~Qt::WindowMinimized);
        showNormal();
        raise();
        activateWindow();
        QApplication::alert(this, 0);
#ifdef Q_OS_WIN
        // Qt's raise()/activateWindow() can be ignored by Windows foreground
        // restrictions for a detached child process. Explicitly make the
        // installer visible and topmost, then flash its taskbar button as a
        // fallback if the OS still refuses focus stealing.
        HWND hwnd = reinterpret_cast<HWND>(winId());
        if (hwnd)
        {
            ShowWindow(hwnd, SW_RESTORE);
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
            SetForegroundWindow(hwnd);
            FLASHWINFO flash{};
            flash.cbSize = sizeof(flash);
            flash.hwnd = hwnd;
            flash.dwFlags = FLASHW_TRAY | FLASHW_TIMERNOFG;
            flash.uCount = 3;
            flash.dwTimeout = 0;
            FlashWindowEx(&flash);
        }
#endif
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    void linger(int minimumMs = 1200)
    {
        if (!mVisibleTimer.isValid())
            return;
        while (mVisibleTimer.elapsed() < minimumMs)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(20);
        }
    }

    void phase(const QString& text, bool indeterminate = true)
    {
        if (!isVisible())
            present();
        mStatus->setText(text);
        if (indeterminate)
            mProgress->setRange(0, 0);
        QCoreApplication::processEvents();
    }

    void progress(qint64 done, qint64 total, const QString& text = QString())
    {
        if (!isVisible())
            present();
        if (total > 0)
        {
            mProgress->setRange(0, 1000);
            mProgress->setValue(int(std::min<qint64>(1000, done * 1000 / total)));
        }
        else
            mProgress->setRange(0, 0);
        if (!text.isEmpty())
            mTransfer->setText(text);
        QCoreApplication::processEvents();
    }

    void itemProgress(int done, int total, const QString& text)
    {
        if (!isVisible())
            present();
        mProgress->setRange(0, std::max(1, total));
        mProgress->setValue(done);
        mTransfer->setText(text);
        QCoreApplication::processEvents();
    }

    void append(const QString& line)
    {
        mDetails->appendPlainText(line);
        QCoreApplication::processEvents();
    }

    void setCancelable(bool enabled)
    {
        mCancelButton->setEnabled(enabled);
        mCancelButton->setText(enabled ? QStringLiteral("Отмена") : QStringLiteral("Установка…"));
    }

    void finish(const QString& text, bool ok)
    {
        mFinished = true;
        if (!isVisible())
            present();
        mStatus->setText(text);
        mProgress->setRange(0, 1000);
        mProgress->setValue(ok ? 1000 : 0);
        mTransfer->setText(ok ? QStringLiteral("Готово. Лаунчер будет открыт снова.")
                              : QStringLiteral("Подробности записаны в Update.log"));
        mHint->setText(ok ? QStringLiteral("Обновление завершено. Лаунчер сейчас будет запущен снова.")
                          : QStringLiteral("Обновление не завершено. Подробности сохранены в Update.log."));
        mCancelButton->setEnabled(false);
        mCancelButton->setText(QStringLiteral("Готово"));
        QCoreApplication::processEvents();
    }

protected:
    void closeEvent(QCloseEvent* event) override
    {
        if (mCancelButton->isEnabled())
        {
            gCancelRequested = true;
#ifndef Q_OS_WIN
            if (gActiveReply)
                gActiveReply->abort();
#endif
            event->ignore();
            return;
        }
        if (!mFinished)
        {
            // File application cannot be cancelled: keep its progress visible.
            event->ignore();
            return;
        }
        QWidget::closeEvent(event);
    }

private:
    bool mFinished = false;
    QLabel* mTitle = nullptr;
    QLabel* mStatus = nullptr;
    QLabel* mHint = nullptr;
    QProgressBar* mProgress = nullptr;
    QLabel* mTransfer = nullptr;
    QPlainTextEdit* mDetails = nullptr;
    QPushButton* mDetailsButton = nullptr;
    QPushButton* mCancelButton = nullptr;
    QElapsedTimer mVisibleTimer;
};

ProgressWindow* gWindow = nullptr;

[[noreturn]] void fail(const QString& message)
{
    throw std::runtime_error(message.toUtf8().constData());
}

QString errorText(const std::exception& e)
{
    return QString::fromUtf8(e.what());
}

void atomicJson(const QString& path, const QJsonObject& object)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        fail(QStringLiteral("Не удалось записать %1: %2").arg(path, file.errorString()));
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit())
        fail(QStringLiteral("Не удалось сохранить %1: %2").arg(path, file.errorString()));
}

void configureLog(const QJsonObject& request, const QString& job)
{
    QStringList candidates;
    candidates << request.value(QStringLiteral("log")).toString();
    candidates << QDir(QFileInfo(request.value(QStringLiteral("manifest")).toString()).absolutePath()).filePath(QStringLiteral("Update.log"));
    candidates << QDir(request.value(QStringLiteral("client")).toString()).filePath(QStringLiteral("Update.log"));
    if (!job.isEmpty())
        candidates << QDir(QFileInfo(job).absolutePath()).filePath(QStringLiteral("Update.log"));

    gLogPath.clear();
    for (const QString& candidate : candidates)
    {
        if (candidate.isEmpty() || QFileInfo(candidate).isDir())
            continue;
        QFileInfo info(candidate);
        QDir().mkpath(info.absolutePath());
        if (info.exists() && info.size() > 4 * 1024 * 1024)
        {
            const QString old = candidate + QStringLiteral(".old");
            QFile::remove(old);
            QFile::rename(candidate, old);
        }
        QFile test(candidate);
        if (test.open(QIODevice::WriteOnly | QIODevice::Append))
        {
            test.close();
            gLogPath = QFileInfo(candidate).absoluteFilePath();
            return;
        }
    }
}

void logEvent(const QString& phase, const QJsonObject& values = QJsonObject())
{
    QJsonObject record = values;
    record.insert(QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    record.insert(QStringLiteral("pid"), double(QCoreApplication::applicationPid()));
    record.insert(QStringLiteral("phase"), phase);

    const QByteArray line = QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n';
    if (!gLogPath.isEmpty())
    {
        QFile log(gLogPath);
        if (log.open(QIODevice::WriteOnly | QIODevice::Append))
            log.write(line);
    }

    if (phase != QLatin1String("download") && gWindow)
    {
        const QString message = values.value(QStringLiteral("message")).toString();
        gWindow->append(message.isEmpty() ? phase : phase + QStringLiteral(": ") + message);
    }

    // check mode is driven by exit code, but JSON stdout is retained for old
    // launcher diagnostics and compatibility with U004/U005.
    if (stdout)
    {
        std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
        std::fflush(stdout);
    }
}

struct IniData
{
    QMap<QPair<QString, QString>, QString> values;
};

IniData readIni(const QString& text)
{
    IniData out;
    QString section;
    const QString clean = text.startsWith(QChar(0xfeff)) ? text.mid(1) : text;
    const QStringList lines = clean.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::KeepEmptyParts);
    for (QString line : lines)
    {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')))
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']')))
        {
            section = line.mid(1, line.size() - 2).trimmed().toLower();
            continue;
        }
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0)
            continue;
        QString key = line.left(equals).trimmed().toLower();
        QString value = line.mid(equals + 1).trimmed();
        if (value.size() >= 2 && value.at(0) == QLatin1Char('"') && value.at(value.size() - 1) == QLatin1Char('"'))
        {
            value = value.mid(1, value.size() - 2);
            value.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
            value.replace(QStringLiteral("\\r"), QStringLiteral("\r"));
            value.replace(QStringLiteral("\\t"), QStringLiteral("\t"));
            value.replace(QStringLiteral("\\\""), QStringLiteral("\""));
            value.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
        }
        out.values.insert(qMakePair(section, key), value);
    }
    return out;
}

QString buildValue(const IniData& ini, const QString& key, const QString& defaultValue = QString())
{
    const QString lower = key.toLower();
    const QStringList sections { QStringLiteral("build"), QString(), QStringLiteral("general"), QStringLiteral("manifest") };
    for (const QString& section : sections)
    {
        const auto it = ini.values.constFind(qMakePair(section, lower));
        if (it != ini.values.cend())
            return it.value();
    }
    return defaultValue;
}

QString normalizedRevision(const QString& value)
{
    if (value.isEmpty() || value.size() > 64
        || !QRegularExpression(QStringLiteral("^[0-9]+$")).match(value).hasMatch())
        fail(QStringLiteral("Некорректный номер версии: %1").arg(value));
    int first = 0;
    while (first + 1 < value.size() && value[first] == QLatin1Char('0'))
        ++first;
    return value.mid(first);
}

bool revisionGreater(const QString& left, const QString& right)
{
    const QString a = normalizedRevision(left);
    const QString b = normalizedRevision(right);
    if (a.size() != b.size())
        return a.size() > b.size();
    return a > b;
}

QString stampManifest(const QString& text, const QMap<QString, QString>& versions)
{
    QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    QString section;
    QSet<QString> found;
    for (QString& line : lines)
    {
        QString stripped = line.trimmed();
        if (stripped.startsWith(QChar(0xfeff)))
            stripped.remove(0, 1);
        if (stripped.startsWith(QLatin1Char('[')) && stripped.endsWith(QLatin1Char(']')))
        {
            section = stripped.mid(1, stripped.size() - 2).trimmed().toLower();
            continue;
        }
        if (section != QLatin1String("build") && !section.isEmpty()
            && section != QLatin1String("general") && section != QLatin1String("manifest"))
            continue;
        const int equals = stripped.indexOf(QLatin1Char('='));
        if (equals < 0)
            continue;
        const QString key = stripped.left(equals).trimmed().toLower();
        const auto it = versions.constFind(key);
        if (it != versions.cend())
        {
            line = key + QLatin1Char('=') + it.value();
            found.insert(key);
        }
    }
    if (found.size() != versions.size())
    {
        lines << QString() << QStringLiteral("[Build]");
        for (auto it = versions.cbegin(); it != versions.cend(); ++it)
            if (!found.contains(it.key()))
                lines << it.key() + QLatin1Char('=') + it.value();
    }
    return lines.join(QLatin1Char('\n'));
}

QUrl normalizedUrl(const QString& raw)
{
    QString value = raw.trimmed();
    if (value.isEmpty())
        fail(QStringLiteral("URL обновления пуст"));
    if (!value.contains(QStringLiteral("://")))
        value.prepend(QStringLiteral("https://"));
    QUrl url(value);
    if (!url.isValid() || url.host().isEmpty()
        || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))
        || !url.userName().isEmpty() || !url.password().isEmpty())
        fail(QStringLiteral("Допускаются только HTTP(S) URL без логина и пароля: %1").arg(value));
    return url;
}

QString humanBytes(qint64 bytes)
{
    const double b = double(std::max<qint64>(0, bytes));
    if (b >= 1024.0 * 1024 * 1024)
        return QString::number(b / (1024.0 * 1024 * 1024), 'f', 2) + QStringLiteral(" ГиБ");
    if (b >= 1024.0 * 1024)
        return QString::number(b / (1024.0 * 1024), 'f', 1) + QStringLiteral(" МиБ");
    if (b >= 1024.0)
        return QString::number(b / 1024.0, 'f', 1) + QStringLiteral(" КиБ");
    return QString::number(bytes) + QStringLiteral(" Б");
}

#ifdef Q_OS_WIN
QString winHttpError(const QString& operation, DWORD code = GetLastError())
{
    wchar_t* message = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageW(flags, nullptr, code, 0, reinterpret_cast<LPWSTR>(&message), 0, nullptr);
    QString detail;
    if (length && message)
        detail = QString::fromWCharArray(message, int(length)).trimmed();
    if (detail.isEmpty())
    {
        if (code == ERROR_WINHTTP_SECURE_FAILURE)
            detail = QStringLiteral("Ошибка проверки TLS-сертификата");
        else if (code == ERROR_WINHTTP_TIMEOUT)
            detail = QStringLiteral("Истекло время ожидания сети");
        else if (code == ERROR_WINHTTP_NAME_NOT_RESOLVED)
            detail = QStringLiteral("Не удалось определить адрес сервера");
        else if (code == ERROR_WINHTTP_CANNOT_CONNECT)
            detail = QStringLiteral("Не удалось установить соединение с сервером");
    }
    if (message)
        LocalFree(message);
    if (detail.isEmpty())
        return QStringLiteral("%1 (WinHTTP error %2)").arg(operation).arg(code);
    return QStringLiteral("%1: %2 (WinHTTP %3)").arg(operation, detail).arg(code);
}

class WinHttpHandle
{
public:
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET handle) : mHandle(handle) {}
    ~WinHttpHandle() { reset(); }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    HINTERNET get() const { return mHandle; }
    explicit operator bool() const { return mHandle != nullptr; }
    void reset(HINTERNET handle = nullptr)
    {
        if (mHandle)
            WinHttpCloseHandle(mHandle);
        mHandle = handle;
    }
private:
    HINTERNET mHandle = nullptr;
};

QByteArray downloadWinHttp(const QString& rawUrl, const QString& target = QString(), qint64 limit = -1,
    const QString& expectedHash = QString())
{
    if (gCancelRequested)
        fail(QStringLiteral("Обновление отменено пользователем"));

    const QUrl url = normalizedUrl(rawUrl);
    const bool secure = url.scheme() == QLatin1String("https");
    const QString host = url.host();
    QString objectName = url.path(QUrl::FullyEncoded);
    if (objectName.isEmpty())
        objectName = QStringLiteral("/");
    if (url.hasQuery())
        objectName += QLatin1Char('?') + url.query(QUrl::FullyEncoded);
    const INTERNET_PORT port = INTERNET_PORT(url.port(secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT));

    WinHttpHandle session;
#ifdef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
    session.reset(WinHttpOpen(L"ArenaMP-Native-Updater/3", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
#endif
    if (!session)
        session.reset(WinHttpOpen(L"ArenaMP-Native-Updater/3", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session)
        fail(winHttpError(QStringLiteral("Не удалось инициализировать WinHTTP")));

    // These are per-operation timeouts. Large package downloads can run for hours,
    // but a stalled DNS/connect/read operation must not freeze the updater forever.
    WinHttpSetTimeouts(session.get(), 10000, 10000, 30000, limit >= 0 ? 8000 : 30000);

    WinHttpHandle connection(WinHttpConnect(session.get(), reinterpret_cast<LPCWSTR>(host.utf16()), port, 0));
    if (!connection)
        fail(winHttpError(QStringLiteral("Не удалось подключиться к серверу обновлений")));

    const DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;
    WinHttpHandle request(WinHttpOpenRequest(connection.get(), L"GET", reinterpret_cast<LPCWSTR>(objectName.utf16()),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request)
        fail(winHttpError(QStringLiteral("Не удалось создать HTTP-запрос")));

    const wchar_t headers[] = L"Cache-Control: no-cache\r\nAccept-Encoding: identity\r\n";
    logEvent(QStringLiteral("download_start"), QJsonObject{{QStringLiteral("url"), url.toString()},
        {QStringLiteral("target"), target.isEmpty() ? QStringLiteral("check.ini") : target},
        {QStringLiteral("sha256"), expectedHash.isEmpty() ? QStringLiteral("not supplied") : expectedHash},
        {QStringLiteral("transport"), QStringLiteral("winhttp")}});

    if (!WinHttpSendRequest(request.get(), headers, DWORD(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        fail(winHttpError(QStringLiteral("Не удалось отправить запрос обновления")));
    if (!WinHttpReceiveResponse(request.get(), nullptr))
        fail(winHttpError(QStringLiteral("Не удалось получить ответ сервера обновлений")));

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX))
        fail(winHttpError(QStringLiteral("Не удалось получить HTTP-статус")));
    if (status != 200)
        fail(QStringLiteral("HTTP %1 при загрузке %2").arg(status).arg(url.toString()));

    qint64 total = -1;
    DWORD lengthSize = 0;
    WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
        WINHTTP_NO_OUTPUT_BUFFER, &lengthSize, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && lengthSize >= sizeof(wchar_t))
    {
        std::vector<wchar_t> lengthBuffer(size_t(lengthSize / sizeof(wchar_t)) + 1, L'\0');
        if (WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                lengthBuffer.data(), &lengthSize, WINHTTP_NO_HEADER_INDEX))
        {
            bool ok = false;
            const qint64 parsed = QString::fromWCharArray(lengthBuffer.data()).trimmed().toLongLong(&ok);
            if (ok && parsed >= 0)
                total = parsed;
        }
    }

    QCryptographicHash digest(QCryptographicHash::Sha256);
    QByteArray memory;
    QFile file;
    if (!target.isEmpty())
    {
        file.setFileName(target);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            fail(QStringLiteral("Не удалось создать файл загрузки %1: %2").arg(target, file.errorString()));
    }

    qint64 done = 0;
    QElapsedTimer report;
    report.start();
    try
    {
        for (;;)
        {
            if (gCancelRequested)
                fail(QStringLiteral("Обновление отменено пользователем"));
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request.get(), &available))
                fail(winHttpError(QStringLiteral("Ошибка чтения ответа сервера")));
            if (available == 0)
                break;

            while (available > 0)
            {
                if (gCancelRequested)
                    fail(QStringLiteral("Обновление отменено пользователем"));
                const DWORD wanted = std::min<DWORD>(available, DWORD(Chunk));
                QByteArray block(int(wanted), '\0');
                DWORD received = 0;
                if (!WinHttpReadData(request.get(), block.data(), wanted, &received))
                    fail(winHttpError(QStringLiteral("Ошибка загрузки данных")));
                if (received == 0)
                    break;
                block.resize(int(received));
                done += qint64(received);
                if (limit >= 0 && done > limit)
                    fail(QStringLiteral("check.ini превышает допустимый размер"));
                digest.addData(block);
                if (file.isOpen())
                {
                    if (file.write(block) != block.size())
                        fail(QStringLiteral("Ошибка записи загруженного файла: %1").arg(file.errorString()));
                }
                else
                    memory.append(block);

                if (gWindow && !target.isEmpty())
                    gWindow->progress(done, total, total > 0
                        ? QStringLiteral("Загружено %1 из %2").arg(humanBytes(done), humanBytes(total))
                        : QStringLiteral("Загружено %1").arg(humanBytes(done)));
                if (report.elapsed() >= 1000)
                {
                    logEvent(QStringLiteral("download"), QJsonObject{{QStringLiteral("done"), double(done)},
                        {QStringLiteral("total"), double(total)}, {QStringLiteral("transport"), QStringLiteral("winhttp")}});
                    report.restart();
                }
                available -= received;
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            }
        }
        if (file.isOpen())
            file.close();
    }
    catch (...)
    {
        if (file.isOpen())
            file.close();
        if (!target.isEmpty())
            QFile::remove(target);
        throw;
    }

    if (limit >= 0 && done > limit)
        fail(QStringLiteral("check.ini превышает допустимый размер"));
    if (total >= 0 && done != total)
    {
        if (!target.isEmpty())
            QFile::remove(target);
        fail(QStringLiteral("Загрузка неполная: получено %1 из %2 байт").arg(done).arg(total));
    }

    const QString actualHash = QString::fromLatin1(digest.result().toHex());
    if (!expectedHash.isEmpty())
    {
        if (!QRegularExpression(QStringLiteral("^[0-9A-Fa-f]{64}$")).match(expectedHash).hasMatch()
            || actualHash.compare(expectedHash, Qt::CaseInsensitive) != 0)
        {
            if (!target.isEmpty())
                QFile::remove(target);
            fail(QStringLiteral("SHA-256 загруженного пакета не совпадает"));
        }
    }
    logEvent(QStringLiteral("download_complete"), QJsonObject{{QStringLiteral("bytes"), double(done)},
        {QStringLiteral("total"), double(total)}, {QStringLiteral("sha256"), actualHash},
        {QStringLiteral("transport"), QStringLiteral("winhttp")}});
    return memory;
}
#endif

#ifndef Q_OS_WIN
QByteArray downloadQt(const QString& rawUrl, const QString& target = QString(), qint64 limit = -1,
    const QString& expectedHash = QString())
{
    if (gCancelRequested)
        fail(QStringLiteral("Обновление отменено пользователем"));

    QNetworkAccessManager manager;
    QNetworkRequest request(normalizedUrl(rawUrl));
    request.setRawHeader("User-Agent", "ArenaMP-Native-Updater/2");
    request.setRawHeader("Cache-Control", "no-cache");
    request.setRawHeader("Accept-Encoding", "identity");
#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
#else
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
#endif

    QNetworkReply* reply = manager.get(request);
    gActiveReply = reply;
    QCryptographicHash digest(QCryptographicHash::Sha256);
    QByteArray memory;
    QFile file;
    if (!target.isEmpty())
    {
        file.setFileName(target);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            reply->abort();
            gActiveReply = nullptr;
            fail(QStringLiteral("Не удалось создать файл загрузки %1: %2").arg(target, file.errorString()));
        }
    }

    logEvent(QStringLiteral("download_start"), QJsonObject{{QStringLiteral("url"), request.url().toString()},
        {QStringLiteral("target"), target.isEmpty() ? QStringLiteral("check.ini") : target},
        {QStringLiteral("sha256"), expectedHash.isEmpty() ? QStringLiteral("not supplied") : expectedHash}});

    QElapsedTimer report;
    report.start();
    qint64 done = 0;
    qint64 total = -1;
    QString streamError;
    QTimer inactivity;
    inactivity.setSingleShot(true);
    inactivity.setInterval(limit >= 0 ? 5000 : 30000);
    QTimer hardDeadline;
    hardDeadline.setSingleShot(true);
    hardDeadline.setInterval(limit >= 0 ? 8000 : 6 * 60 * 60 * 1000);

    auto resetInactivity = [&]() { inactivity.start(); };
    QObject::connect(&inactivity, &QTimer::timeout, reply, [&]() {
        streamError = QStringLiteral("Истекло время ожидания данных от сервера");
        reply->abort();
    });
    QObject::connect(&hardDeadline, &QTimer::timeout, reply, [&]() {
        streamError = QStringLiteral("Истекло время загрузки");
        reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [&]() {
        const QByteArray block = reply->readAll();
        if (block.isEmpty())
            return;
        resetInactivity();
        done += block.size();
        if (limit >= 0 && done > limit)
        {
            streamError = QStringLiteral("check.ini превышает допустимый размер");
            reply->abort();
            return;
        }
        digest.addData(block);
        if (file.isOpen())
        {
            if (file.write(block) != block.size())
            {
                streamError = QStringLiteral("Ошибка записи загруженного файла: %1").arg(file.errorString());
                reply->abort();
                return;
            }
        }
        else
            memory.append(block);
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [&](qint64 received, qint64 contentLength) {
        total = contentLength;
        resetInactivity();
        if (gWindow && !target.isEmpty())
            gWindow->progress(received, contentLength,
                contentLength > 0 ? QStringLiteral("Загружено %1 из %2").arg(humanBytes(received), humanBytes(contentLength))
                                  : QStringLiteral("Загружено %1").arg(humanBytes(received)));
        if (report.elapsed() >= 1000)
        {
            logEvent(QStringLiteral("download"), QJsonObject{{QStringLiteral("done"), double(received)},
                {QStringLiteral("total"), double(contentLength)}});
            report.restart();
        }
        if (gCancelRequested)
            reply->abort();
    });

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    inactivity.start();
    hardDeadline.start();
    loop.exec();
    inactivity.stop();
    hardDeadline.stop();

    // Drain a final block delivered immediately before finished().
    const QByteArray tail = reply->readAll();
    if (!tail.isEmpty())
    {
        done += tail.size();
        digest.addData(tail);
        if (file.isOpen())
        {
            if (file.write(tail) != tail.size())
                streamError = QStringLiteral("Ошибка записи загруженного файла: %1").arg(file.errorString());
        }
        else
            memory.append(tail);
    }
    if (file.isOpen())
        file.close();

    const QVariant statusValue = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int status = statusValue.isValid() ? statusValue.toInt() : 0;
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkText = reply->errorString();
    const QUrl finalUrl = reply->url();
    reply->deleteLater();
    gActiveReply = nullptr;

    if (gCancelRequested)
        fail(QStringLiteral("Обновление отменено пользователем"));
    if (!streamError.isEmpty())
        fail(streamError);
    if (networkError != QNetworkReply::NoError)
        fail(QStringLiteral("Ошибка сети: %1").arg(networkText));
    if (status != 200)
        fail(QStringLiteral("HTTP %1 при загрузке %2").arg(status).arg(finalUrl.toString()));
    if (limit >= 0 && done > limit)
        fail(QStringLiteral("check.ini превышает допустимый размер"));
    if (total >= 0 && done != total)
        fail(QStringLiteral("Загрузка неполная: получено %1 из %2 байт").arg(done).arg(total));

    const QString actualHash = QString::fromLatin1(digest.result().toHex());
    if (!expectedHash.isEmpty())
    {
        if (!QRegularExpression(QStringLiteral("^[0-9A-Fa-f]{64}$")).match(expectedHash).hasMatch()
            || actualHash.compare(expectedHash, Qt::CaseInsensitive) != 0)
            fail(QStringLiteral("SHA-256 загруженного пакета не совпадает"));
    }
    logEvent(QStringLiteral("download_complete"), QJsonObject{{QStringLiteral("bytes"), double(done)},
        {QStringLiteral("total"), double(total)}, {QStringLiteral("sha256"), actualHash},
        {QStringLiteral("transport"), QStringLiteral("qtnetwork")}});
    return memory;
}

#endif

QByteArray download(const QString& rawUrl, const QString& target = QString(), qint64 limit = -1,
    const QString& expectedHash = QString())
{
#ifdef Q_OS_WIN
    // Qt 5 binary distributions can require external OpenSSL DLLs at runtime.
    // The updater must work in a freshly unpacked client without those optional
    // DLLs, so Windows uses the OS HTTPS stack and certificate store directly.
    return downloadWinHttp(rawUrl, target, limit, expectedHash);
#else
    return downloadQt(rawUrl, target, limit, expectedHash);
#endif
}

QString safeArchiveName(QString name)
{
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (name.startsWith(QStringLiteral("./")))
        name.remove(0, 2);
    while (name.endsWith(QLatin1Char('/')))
        name.chop(1);
    if (name.isEmpty() || name == QLatin1String("."))
        return QString();
    if (name.startsWith(QLatin1Char('/')))
        fail(QStringLiteral("Опасный путь в архиве: %1").arg(name));
    const QStringList parts = name.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    const QString invalidChars = QStringLiteral(":<>\"|?*");
    for (const QString& part : parts)
    {
        bool invalidCharacter = false;
        for (const QChar c : part)
            if (c.unicode() < 32 || invalidChars.contains(c))
            {
                invalidCharacter = true;
                break;
            }
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String("..")
            || part.contains(QStringLiteral(".arena-"), Qt::CaseInsensitive)
            || invalidCharacter || part.endsWith(QLatin1Char(' ')) || part.endsWith(QLatin1Char('.')))
            fail(QStringLiteral("Недопустимый путь в архиве: %1").arg(name));
        const QString stem = part.section(QLatin1Char('.'), 0, 0).toUpper();
        if (stem == QLatin1String("CON") || stem == QLatin1String("PRN") || stem == QLatin1String("AUX")
            || stem == QLatin1String("NUL") || QRegularExpression(QStringLiteral("^(COM|LPT)[1-9]$")).match(stem).hasMatch())
            fail(QStringLiteral("Зарезервированное имя файла в архиве: %1").arg(name));
    }
    return parts.join(QLatin1Char('/'));
}

quint16 le16(const QByteArray& b, int p)
{
    return quint16(quint8(b[p])) | (quint16(quint8(b[p + 1])) << 8);
}
quint32 le32(const QByteArray& b, int p)
{
    return quint32(le16(b, p)) | (quint32(le16(b, p + 2)) << 16);
}
quint64 le64(const QByteArray& b, int p)
{
    return quint64(le32(b, p)) | (quint64(le32(b, p + 4)) << 32);
}

struct ZipEntry
{
    QString name;
    quint16 flags = 0;
    quint16 method = 0;
    quint32 crc = 0;
    quint64 compressed = 0;
    quint64 expanded = 0;
    quint64 localOffset = 0;
    quint32 mode = 0;
    bool directory = false;
};

QByteArray readExact(QFile& file, qint64 count)
{
    QByteArray data = file.read(count);
    if (data.size() != count)
        fail(QStringLiteral("Архив неожиданно закончился"));
    return data;
}

void parseZip64Extra(const QByteArray& extra, quint64& expanded, quint64& compressed, quint64& offset)
{
    int p = 0;
    while (p + 4 <= extra.size())
    {
        const quint16 id = le16(extra, p);
        const quint16 size = le16(extra, p + 2);
        p += 4;
        if (p + size > extra.size())
            fail(QStringLiteral("Повреждён ZIP extra field"));
        if (id == 0x0001)
        {
            int q = p;
            if (expanded == 0xffffffffULL)
            {
                if (q + 8 > p + size) fail(QStringLiteral("Повреждён ZIP64"));
                expanded = le64(extra, q); q += 8;
            }
            if (compressed == 0xffffffffULL)
            {
                if (q + 8 > p + size) fail(QStringLiteral("Повреждён ZIP64"));
                compressed = le64(extra, q); q += 8;
            }
            if (offset == 0xffffffffULL)
            {
                if (q + 8 > p + size) fail(QStringLiteral("Повреждён ZIP64"));
                offset = le64(extra, q);
            }
            return;
        }
        p += size;
    }
}

void ensureParentPathsAreDirectories(const QSet<QString>& fileNames)
{
    QSet<QString> foldedFiles;
    for (const QString& name : fileNames)
        foldedFiles.insert(name.toCaseFolded());
    for (const QString& name : fileNames)
    {
        int slash = name.lastIndexOf(QLatin1Char('/'));
        QString parent = slash >= 0 ? name.left(slash) : QString();
        while (!parent.isEmpty())
        {
            if (foldedFiles.contains(parent.toCaseFolded()))
                fail(QStringLiteral("Файл используется как каталог внутри архива: %1").arg(parent));
            slash = parent.lastIndexOf(QLatin1Char('/'));
            parent = slash >= 0 ? parent.left(slash) : QString();
        }
    }
}

void writeInflatedZipEntry(QFile& archive, const ZipEntry& entry, QFile& output)
{
    if (!archive.seek(qint64(entry.localOffset)))
        fail(QStringLiteral("Не удалось перейти к ZIP entry"));
    const QByteArray header = readExact(archive, 30);
    if (le32(header, 0) != 0x04034b50)
        fail(QStringLiteral("Повреждён локальный ZIP header"));
    const quint16 nameLength = le16(header, 26);
    const quint16 extraLength = le16(header, 28);
    if (!archive.seek(qint64(entry.localOffset) + 30 + nameLength + extraLength))
        fail(QStringLiteral("Повреждён ZIP offset"));

    quint64 remaining = entry.compressed;
    quint64 produced = 0;
    uLong crc = crc32(0L, Z_NULL, 0);

    if (entry.method == 0)
    {
        while (remaining > 0)
        {
            if (gCancelRequested)
                fail(QStringLiteral("Обновление отменено пользователем"));
            const qint64 wanted = qint64(std::min<quint64>(remaining, Chunk));
            const QByteArray block = readExact(archive, wanted);
            if (output.write(block) != block.size())
                fail(QStringLiteral("Ошибка записи распакованного файла"));
            crc = crc32(crc, reinterpret_cast<const Bytef*>(block.constData()), uInt(block.size()));
            produced += quint64(block.size());
            remaining -= quint64(block.size());
        }
    }
    else if (entry.method == 8)
    {
        z_stream stream;
        std::memset(&stream, 0, sizeof(stream));
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
            fail(QStringLiteral("Не удалось инициализировать DEFLATE"));
        QByteArray input(int(std::min<qint64>(Chunk, 4 * 1024 * 1024)), '\0');
        QByteArray out(int(std::min<qint64>(Chunk, 4 * 1024 * 1024)), '\0');
        int result = Z_OK;
        try
        {
            while (result != Z_STREAM_END)
            {
                if (gCancelRequested)
                    fail(QStringLiteral("Обновление отменено пользователем"));
                if (stream.avail_in == 0 && remaining > 0)
                {
                    const qint64 wanted = qint64(std::min<quint64>(remaining, quint64(input.size())));
                    const QByteArray block = readExact(archive, wanted);
                    std::memcpy(input.data(), block.constData(), size_t(block.size()));
                    stream.next_in = reinterpret_cast<Bytef*>(input.data());
                    stream.avail_in = uInt(block.size());
                    remaining -= quint64(block.size());
                }
                stream.next_out = reinterpret_cast<Bytef*>(out.data());
                stream.avail_out = uInt(out.size());
                result = inflate(&stream, Z_NO_FLUSH);
                if (result != Z_OK && result != Z_STREAM_END)
                    fail(QStringLiteral("Ошибка распаковки DEFLATE (%1)").arg(result));
                const int have = out.size() - int(stream.avail_out);
                if (have > 0)
                {
                    if (output.write(out.constData(), have) != have)
                        fail(QStringLiteral("Ошибка записи распакованного файла"));
                    crc = crc32(crc, reinterpret_cast<const Bytef*>(out.constData()), uInt(have));
                    produced += quint64(have);
                }
                if (stream.avail_in == 0 && remaining == 0 && result != Z_STREAM_END && have == 0)
                    fail(QStringLiteral("Оборванный DEFLATE поток"));
            }
        }
        catch (...)
        {
            inflateEnd(&stream);
            throw;
        }
        inflateEnd(&stream);
    }
    else
        fail(QStringLiteral("ZIP использует неподдерживаемый метод сжатия %1").arg(entry.method));

    if (produced != entry.expanded)
        fail(QStringLiteral("Размер распакованного ZIP файла не совпадает"));
    if (quint32(crc) != entry.crc)
        fail(QStringLiteral("CRC ZIP файла не совпадает"));
}

void extractZip(const QString& archivePath, const QString& targetPath)
{
    QFile archive(archivePath);
    if (!archive.open(QIODevice::ReadOnly))
        fail(QStringLiteral("Не удалось открыть ZIP: %1").arg(archive.errorString()));
    const qint64 fileSize = archive.size();
    const qint64 tailSize = std::min<qint64>(fileSize, 131072);
    archive.seek(fileSize - tailSize);
    const QByteArray tail = archive.read(tailSize);
    const QByteArray eocdSig("PK\x05\x06", 4);
    const int eocd = tail.lastIndexOf(eocdSig);
    if (eocd < 0 || eocd + 22 > tail.size())
        fail(QStringLiteral("ZIP End Of Central Directory не найден"));

    quint64 entries = le16(tail, eocd + 10);
    quint64 centralSize = le32(tail, eocd + 12);
    quint64 centralOffset = le32(tail, eocd + 16);
    if (le16(tail, eocd + 4) != 0 || le16(tail, eocd + 6) != 0)
        fail(QStringLiteral("Многотомный ZIP не поддерживается"));

    if (entries == 0xffffULL || centralSize == 0xffffffffULL || centralOffset == 0xffffffffULL)
    {
        const QByteArray locatorSig("PK\x06\x07", 4);
        const int locator = tail.lastIndexOf(locatorSig, eocd - 1);
        if (locator < 0 || locator + 20 > tail.size())
            fail(QStringLiteral("ZIP64 locator не найден"));
        const quint64 zip64Offset = le64(tail, locator + 8);
        if (!archive.seek(qint64(zip64Offset)))
            fail(QStringLiteral("Повреждён ZIP64 offset"));
        const QByteArray zip64 = readExact(archive, 56);
        if (le32(zip64, 0) != 0x06064b50)
            fail(QStringLiteral("ZIP64 EOCD не найден"));
        entries = le64(zip64, 32);
        centralSize = le64(zip64, 40);
        centralOffset = le64(zip64, 48);
    }
    if (entries > MaxEntries)
        fail(QStringLiteral("Слишком много файлов в архиве"));
    if (centralOffset > quint64(fileSize) || centralSize > quint64(fileSize)
        || centralOffset + centralSize > quint64(fileSize))
        fail(QStringLiteral("Повреждён размер ZIP central directory"));
    if (!archive.seek(qint64(centralOffset)))
        fail(QStringLiteral("Повреждён ZIP central directory offset"));

    std::vector<ZipEntry> list;
    list.reserve(size_t(entries));
    QSet<QString> folded;
    QSet<QString> fileNames;
    quint64 expandedTotal = 0;
    for (quint64 i = 0; i < entries; ++i)
    {
        const QByteArray h = readExact(archive, 46);
        if (le32(h, 0) != 0x02014b50)
            fail(QStringLiteral("Повреждён ZIP central directory"));
        ZipEntry e;
        e.flags = le16(h, 8);
        e.method = le16(h, 10);
        e.crc = le32(h, 16);
        e.compressed = le32(h, 20);
        e.expanded = le32(h, 24);
        const quint16 nameLength = le16(h, 28);
        const quint16 extraLength = le16(h, 30);
        const quint16 commentLength = le16(h, 32);
        e.mode = le32(h, 38) >> 16;
        e.localOffset = le32(h, 42);
        const QByteArray nameBytes = readExact(archive, nameLength);
        const QByteArray extra = readExact(archive, extraLength);
        if (commentLength)
            readExact(archive, commentLength);
        if (e.flags & 1)
            fail(QStringLiteral("Зашифрованный ZIP не поддерживается"));
        parseZip64Extra(extra, e.expanded, e.compressed, e.localOffset);
        QString rawName = (e.flags & 0x0800) ? QString::fromUtf8(nameBytes) : QString::fromLatin1(nameBytes);
        const bool slashDirectory = rawName.endsWith(QLatin1Char('/')) || rawName.endsWith(QLatin1Char('\\'));
        e.name = safeArchiveName(rawName);
        if (e.name.isEmpty())
            continue;
        const quint32 type = e.mode & 0170000;
        e.directory = slashDirectory || type == 0040000;
        if (type != 0 && type != 0100000 && type != 0040000)
            fail(QStringLiteral("Ссылка или device в ZIP запрещены: %1").arg(e.name));
        const QString foldedName = e.name.toCaseFolded();
        if (folded.contains(foldedName))
            fail(QStringLiteral("Дублирующийся путь без учёта регистра: %1").arg(e.name));
        folded.insert(foldedName);
        if (!e.directory)
        {
            fileNames.insert(e.name);
            expandedTotal += e.expanded;
            if (expandedTotal > MaxExpanded)
                fail(QStringLiteral("Архив превышает 128 ГиБ после распаковки"));
        }
        list.push_back(e);
    }
    ensureParentPathsAreDirectories(fileNames);

    QDir().mkpath(targetPath);
    QStorageInfo storage(targetPath);
    if (storage.isValid() && storage.bytesAvailable() < qint64(expandedTotal + 16 * Chunk))
        fail(QStringLiteral("Недостаточно места для распаковки обновления"));

    int fileCount = 0;
    for (const ZipEntry& e : list)
        if (!e.directory) ++fileCount;
    int current = 0;
    for (const ZipEntry& e : list)
    {
        if (gCancelRequested)
            fail(QStringLiteral("Обновление отменено пользователем"));
        const QString dest = QDir(targetPath).filePath(e.name);
        if (e.directory)
        {
            if (!QDir().mkpath(dest))
                fail(QStringLiteral("Не удалось создать каталог %1").arg(dest));
            continue;
        }
        if (!QDir().mkpath(QFileInfo(dest).absolutePath()))
            fail(QStringLiteral("Не удалось создать каталог для %1").arg(dest));
        QFile output(dest);
        if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
            fail(QStringLiteral("Не удалось создать %1: %2").arg(dest, output.errorString()));
        writeInflatedZipEntry(archive, e, output);
        output.flush();
        output.close();
#ifndef Q_OS_WIN
        QFile::Permissions permissions = QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther;
        if (e.mode & 0111)
            permissions |= QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther;
        QFile::setPermissions(dest, permissions);
#endif
        ++current;
        if (gWindow)
            gWindow->itemProgress(current, fileCount, QStringLiteral("Распаковка: %1 / %2 — %3").arg(current).arg(fileCount).arg(e.name));
    }
    if (fileCount == 0)
        fail(QStringLiteral("Архив не содержит файлов"));
}

quint64 parseTarNumber(const QByteArray& field)
{
    if (field.isEmpty()) return 0;
    if (quint8(field[0]) & 0x80)
    {
        // GNU/POSIX base-256 positive number.
        quint64 value = quint8(field[0]) & 0x7f;
        for (int i = 1; i < field.size(); ++i)
            value = (value << 8) | quint8(field[i]);
        return value;
    }
    QByteArray trimmed = field;
    const int nul = trimmed.indexOf('\0');
    if (nul >= 0) trimmed.truncate(nul);
    trimmed = trimmed.trimmed();
    if (trimmed.isEmpty()) return 0;
    bool ok = false;
    const quint64 value = trimmed.toULongLong(&ok, 8);
    if (!ok) fail(QStringLiteral("Повреждён TAR numeric field"));
    return value;
}

QString tarString(const QByteArray& header, int offset, int size)
{
    QByteArray data = header.mid(offset, size);
    const int nul = data.indexOf('\0');
    if (nul >= 0) data.truncate(nul);
    return QString::fromUtf8(data);
}

struct TarEntry
{
    QString name;
    char type = '0';
    QString link;
    quint64 size = 0;
    quint64 dataOffset = 0;
    quint32 mode = 0644;
    bool directory = false;
};

quint64 padded512(quint64 size)
{
    return (size + 511) & ~quint64(511);
}

QMap<QString, QString> parsePax(const QByteArray& data)
{
    QMap<QString, QString> result;
    int p = 0;
    while (p < data.size())
    {
        const int space = data.indexOf(' ', p);
        if (space < 0) break;
        bool ok = false;
        const int len = data.mid(p, space - p).toInt(&ok);
        if (!ok || len <= 0 || p + len > data.size())
            fail(QStringLiteral("Повреждён PAX header"));
        QByteArray record = data.mid(space + 1, p + len - space - 1);
        if (record.endsWith('\n')) record.chop(1);
        const int equals = record.indexOf('=');
        if (equals > 0)
            result.insert(QString::fromUtf8(record.left(equals)), QString::fromUtf8(record.mid(equals + 1)));
        p += len;
    }
    return result;
}

QString normalizeTarLink(const QString& entryName, QString link, bool hardLink)
{
    link.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (link.startsWith(QLatin1Char('/')) || QRegularExpression(QStringLiteral("^[A-Za-z]:")).match(link).hasMatch())
        fail(QStringLiteral("Внешняя TAR ссылка запрещена: %1").arg(link));
    QStringList parts;
    if (!hardLink)
    {
        const QString parent = entryName.section(QLatin1Char('/'), 0, -2);
        if (!parent.isEmpty())
            parts = parent.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    }
    for (const QString& part : link.split(QLatin1Char('/'), Qt::SkipEmptyParts))
    {
        if (part == QLatin1String(".")) continue;
        if (part == QLatin1String(".."))
        {
            if (parts.isEmpty())
                fail(QStringLiteral("TAR ссылка выходит за пределы архива: %1").arg(link));
            parts.removeLast();
        }
        else
            parts << part;
    }
    return safeArchiveName(parts.join(QLatin1Char('/')));
}

void gunzipToFile(const QString& archive, const QString& output)
{
    gzFile in = gzopen(QFile::encodeName(archive).constData(), "rb");
    if (!in)
        fail(QStringLiteral("Не удалось открыть GZip архив"));
    QFile out(output);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        gzclose(in);
        fail(QStringLiteral("Не удалось создать временный TAR: %1").arg(out.errorString()));
    }
    QByteArray buffer(int(Chunk), '\0');
    for (;;)
    {
        if (gCancelRequested)
        {
            gzclose(in);
            fail(QStringLiteral("Обновление отменено пользователем"));
        }
        const int got = gzread(in, buffer.data(), unsigned(buffer.size()));
        if (got < 0)
        {
            int err = 0;
            const char* msg = gzerror(in, &err);
            gzclose(in);
            fail(QStringLiteral("Ошибка GZip: %1").arg(QString::fromLocal8Bit(msg ? msg : "unknown")));
        }
        if (got == 0) break;
        if (out.write(buffer.constData(), got) != got)
        {
            gzclose(in);
            fail(QStringLiteral("Ошибка записи временного TAR"));
        }
    }
    const int closeResult = gzclose(in);
    out.close();
    if (closeResult != Z_OK)
        fail(QStringLiteral("GZip архив повреждён"));
}

void copyTarPayload(QFile& tar, const TarEntry& source, QFile& out)
{
    if (!tar.seek(qint64(source.dataOffset)))
        fail(QStringLiteral("Повреждён TAR offset"));
    quint64 remaining = source.size;
    while (remaining > 0)
    {
        if (gCancelRequested)
            fail(QStringLiteral("Обновление отменено пользователем"));
        const qint64 wanted = qint64(std::min<quint64>(remaining, Chunk));
        const QByteArray block = readExact(tar, wanted);
        if (out.write(block) != block.size())
            fail(QStringLiteral("Ошибка записи TAR файла"));
        remaining -= quint64(block.size());
    }
}

void extractTar(const QString& archivePath, const QString& targetPath)
{
    std::unique_ptr<QTemporaryFile> uncompressed;
    QString tarPath = archivePath;

    QFile probe(archivePath);
    if (!probe.open(QIODevice::ReadOnly))
        fail(QStringLiteral("Не удалось открыть архив"));
    const QByteArray magic = probe.read(2);
    probe.close();
    if (magic.size() == 2 && quint8(magic[0]) == 0x1f && quint8(magic[1]) == 0x8b)
    {
        uncompressed.reset(new QTemporaryFile(QDir(QFileInfo(archivePath).absolutePath()).filePath(QStringLiteral(".arena-tar-XXXXXX"))));
        if (!uncompressed->open())
            fail(QStringLiteral("Не удалось создать временный TAR"));
        tarPath = uncompressed->fileName();
        uncompressed->close();
        gunzipToFile(archivePath, tarPath);
    }

    QFile tar(tarPath);
    if (!tar.open(QIODevice::ReadOnly))
        fail(QStringLiteral("Не удалось открыть TAR"));

    std::vector<TarEntry> entries;
    QSet<QString> folded;
    QSet<QString> fileNames;
    QMap<QString, QString> globalPax;
    QMap<QString, QString> nextPax;
    QString longName;
    QString longLink;
    quint64 expandedTotal = 0;

    while (tar.pos() + 512 <= tar.size())
    {
        const QByteArray header = readExact(tar, 512);
        bool zero = true;
        for (char c : header) if (c != 0) { zero = false; break; }
        if (zero) break;

        const quint64 storedChecksum = parseTarNumber(header.mid(148, 8));
        quint64 unsignedChecksum = 0;
        qint64 signedChecksum = 0;
        for (int i = 0; i < header.size(); ++i)
        {
            const unsigned char byte = (i >= 148 && i < 156) ? static_cast<unsigned char>(' ')
                                                               : static_cast<unsigned char>(header[i]);
            unsignedChecksum += byte;
            signedChecksum += static_cast<signed char>(byte);
        }
        if (storedChecksum != unsignedChecksum && qint64(storedChecksum) != signedChecksum)
            fail(QStringLiteral("Контрольная сумма TAR header не совпадает"));

        const quint64 size = parseTarNumber(header.mid(124, 12));
        const quint32 mode = quint32(parseTarNumber(header.mid(100, 8)));
        const char type = header[156] == 0 ? '0' : header[156];
        QString name = tarString(header, 0, 100);
        const QString prefix = tarString(header, 345, 155);
        if (!prefix.isEmpty()) name = prefix + QLatin1Char('/') + name;
        QString link = tarString(header, 157, 100);
        const quint64 dataOffset = quint64(tar.pos());

        if (type == 'L' || type == 'K' || type == 'x' || type == 'g')
        {
            if (size > 4 * 1024 * 1024)
                fail(QStringLiteral("Слишком большой TAR metadata header"));
            QByteArray payload = readExact(tar, qint64(size));
            if (!tar.seek(qint64(dataOffset + padded512(size))))
                fail(QStringLiteral("Повреждён TAR"));
            while (payload.endsWith('\0') || payload.endsWith('\n')) payload.chop(1);
            if (type == 'L') longName = QString::fromUtf8(payload);
            else if (type == 'K') longLink = QString::fromUtf8(payload);
            else if (type == 'x') nextPax = parsePax(payload);
            else globalPax = parsePax(payload);
            continue;
        }

        QMap<QString, QString> pax = globalPax;
        for (auto it = nextPax.cbegin(); it != nextPax.cend(); ++it) pax[it.key()] = it.value();
        nextPax.clear();
        if (!longName.isEmpty()) { name = longName; longName.clear(); }
        if (!longLink.isEmpty()) { link = longLink; longLink.clear(); }
        if (pax.contains(QStringLiteral("path"))) name = pax.value(QStringLiteral("path"));
        if (pax.contains(QStringLiteral("linkpath"))) link = pax.value(QStringLiteral("linkpath"));
        quint64 effectiveSize = size;
        if (pax.contains(QStringLiteral("size")))
        {
            bool ok = false;
            effectiveSize = pax.value(QStringLiteral("size")).toULongLong(&ok, 10);
            if (!ok)
                fail(QStringLiteral("Повреждён PAX size"));
        }

        const QString safe = safeArchiveName(name);
        if (!safe.isEmpty())
        {
            if (type != '0' && type != '5' && type != '1' && type != '2')
                fail(QStringLiteral("Специальный TAR объект запрещён: %1").arg(safe));
            const QString foldedName = safe.toCaseFolded();
            if (folded.contains(foldedName))
                fail(QStringLiteral("Дублирующийся TAR путь без учёта регистра: %1").arg(safe));
            folded.insert(foldedName);
            TarEntry e;
            e.name = safe;
            e.type = type;
            e.link = link;
            e.size = effectiveSize;
            e.dataOffset = dataOffset;
            e.mode = mode;
            e.directory = type == '5';
            if (!e.directory)
            {
                fileNames.insert(e.name);
                if (type == '0')
                {
                    expandedTotal += effectiveSize;
                    if (expandedTotal > MaxExpanded)
                        fail(QStringLiteral("Архив превышает 128 ГиБ после распаковки"));
                }
            }
            entries.push_back(e);
            if (entries.size() > MaxEntries)
                fail(QStringLiteral("Слишком много файлов в TAR"));
        }
        if (!tar.seek(qint64(dataOffset + padded512(effectiveSize))))
            fail(QStringLiteral("Повреждён TAR размер файла"));
    }
    ensureParentPathsAreDirectories(fileNames);

    std::map<QString, size_t> byName;
    for (size_t i = 0; i < entries.size(); ++i) byName[entries[i].name] = i;
    std::function<size_t(size_t, std::set<size_t>)> resolve = [&](size_t index, std::set<size_t> visited) -> size_t {
        if (visited.count(index)) fail(QStringLiteral("Циклическая TAR ссылка"));
        visited.insert(index);
        const TarEntry& e = entries[index];
        if (e.type == '0') return index;
        if (e.type != '1' && e.type != '2') fail(QStringLiteral("Ссылка TAR ведёт не на файл"));
        const QString target = normalizeTarLink(e.name, e.link, e.type == '1');
        auto it = byName.find(target);
        if (it == byName.end()) fail(QStringLiteral("TAR ссылка не найдена внутри архива: %1").arg(target));
        if (entries[it->second].directory) fail(QStringLiteral("TAR ссылка на каталог запрещена"));
        return resolve(it->second, visited);
    };
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].type == '1' || entries[i].type == '2')
        {
            const size_t source = resolve(i, {});
            expandedTotal += entries[source].size;
            if (expandedTotal > MaxExpanded)
                fail(QStringLiteral("Архив превышает 128 ГиБ после распаковки"));
        }

    QDir().mkpath(targetPath);
    QStorageInfo storage(targetPath);
    if (storage.isValid() && storage.bytesAvailable() < qint64(expandedTotal + 16 * Chunk))
        fail(QStringLiteral("Недостаточно места для распаковки обновления"));

    int fileCount = 0;
    for (const TarEntry& e : entries) if (!e.directory) ++fileCount;
    int current = 0;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const TarEntry& e = entries[i];
        const QString dest = QDir(targetPath).filePath(e.name);
        if (e.directory)
        {
            if (!QDir().mkpath(dest)) fail(QStringLiteral("Не удалось создать каталог %1").arg(dest));
            continue;
        }
        if (!QDir().mkpath(QFileInfo(dest).absolutePath()))
            fail(QStringLiteral("Не удалось создать каталог для %1").arg(dest));
        QFile out(dest);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            fail(QStringLiteral("Не удалось создать %1: %2").arg(dest, out.errorString()));
        const size_t sourceIndex = (e.type == '0') ? i : resolve(i, {});
        copyTarPayload(tar, entries[sourceIndex], out);
        out.flush(); out.close();
#ifndef Q_OS_WIN
        QFile::Permissions permissions = QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther;
        if (entries[sourceIndex].mode & 0111)
            permissions |= QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther;
        QFile::setPermissions(dest, permissions);
#endif
        ++current;
        if (gWindow) gWindow->itemProgress(current, fileCount, QStringLiteral("Распаковка: %1 / %2 — %3").arg(current).arg(fileCount).arg(e.name));
    }
    if (fileCount == 0)
        fail(QStringLiteral("Архив не содержит файлов"));
}

void extractArchive(const QString& archive, const QString& target)
{
    QFile file(archive);
    if (!file.open(QIODevice::ReadOnly)) fail(QStringLiteral("Не удалось открыть пакет обновления"));
    const QByteArray magic = file.read(4);
    file.close();
    if (magic.startsWith("PK"))
        extractZip(archive, target);
    else
        extractTar(archive, target);
}

QString payloadRoot(QString root, const QString& kind)
{
    QDir dir(root);
    if (kind == QLatin1String("content"))
    {
        const QFileInfoList children = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
        if (children.size() == 1 && children.first().isDir()
            && children.first().fileName().compare(QStringLiteral("Data Files"), Qt::CaseInsensitive) == 0)
            return children.first().absoluteFilePath();
        for (const QFileInfo& child : children)
            if (child.isDir() && child.fileName().compare(QStringLiteral("Data Files"), Qt::CaseInsensitive) == 0)
                fail(QStringLiteral("Смешанный корень update.zip: поместите в него только содержимое Data Files"));
        return root;
    }

    for (int level = 0; level < 4; ++level)
    {
        QDir current(root);
        const QStringList launchers { QStringLiteral("arenamp-launcher.exe"), QStringLiteral("arenamp-launcher"), QStringLiteral("arenamp-launcher.x86_64") };
        for (const QString& launcher : launchers)
            if (QFileInfo(current.filePath(launcher)).isFile())
                return root;
        const QFileInfoList children = current.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
        if (children.size() != 1 || !children.first().isDir())
            break;
        root = children.first().absoluteFilePath();
    }
    fail(QStringLiteral("Архив движка должен содержать arenamp-launcher в корне"));
}

bool isRequiredDataFilesManifest(const QString& relative)
{
    return QFileInfo(QDir::fromNativeSeparators(relative)).fileName().compare(
        QStringLiteral("requiredDataFiles.json"), Qt::CaseInsensitive) == 0;
}

bool isProtected(const QString& relative, const QString& kind)
{
    const QString normalized = QDir::fromNativeSeparators(relative);
    const QString first = normalized.section(QLatin1Char('/'), 0, 0).toCaseFolded();
    const QString name = QFileInfo(normalized).fileName().toCaseFolded();
    if (kind == QLatin1String("engine"))
    {
        if (ProtectedRoots.contains(first) || ProtectedFiles.contains(name)
            || EngineProtectedFileNames.contains(name) || first.startsWith(QStringLiteral(".arena-")))
            return true;
        const auto policy = ArenaUpdater::serverFilePolicy(normalized.toStdString());
        return policy == ArenaUpdater::ServerFilePolicy::Preserve;
    }
    if (ProtectedFiles.contains(first) || first.startsWith(QStringLiteral(".arena-")))
        fail(QStringLiteral("Контентный архив содержит конфигурацию клиента: %1").arg(relative));
    return false;
}

QString destinationPath(const QString& root, const QString& relative)
{
    QFileInfo rootInfo(root);
    if (rootInfo.isSymLink())
        fail(QStringLiteral("Корень обновления является символической ссылкой"));
    QString current = QFileInfo(root).absoluteFilePath();
    const QStringList parts = QDir::fromNativeSeparators(relative).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (int i = 0; i < parts.size(); ++i)
    {
        current = QDir(current).filePath(parts[i]);
        QFileInfo info(current);
        if (info.isSymLink() && i != parts.size() - 1)
            fail(QStringLiteral("Символическая ссылка в пути назначения запрещена: %1").arg(current));
    }
    return current;
}

#ifdef Q_OS_WIN
bool processAlive(qint64 pid)
{
    if (pid <= 0) return false;
    HANDLE handle = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
    if (!handle) return GetLastError() == ERROR_ACCESS_DENIED;
    const DWORD result = WaitForSingleObject(handle, 0);
    CloseHandle(handle);
    return result == WAIT_TIMEOUT;
}
#else
bool processAlive(qint64 pid)
{
    if (pid <= 0) return false;
    if (::kill(pid_t(pid), 0) == 0)
    {
#ifdef Q_OS_LINUX
        QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
        if (stat.open(QIODevice::ReadOnly))
        {
            const QByteArray text = stat.readAll();
            const int close = text.lastIndexOf(')');
            if (close >= 0 && close + 2 < text.size() && text[close + 2] == 'Z')
                return false;
        }
#endif
        return true;
    }
    return errno == EPERM;
}
#endif

QStringList otherClients(const QJsonObject& request)
{
    const QString root = QFileInfo(request.value(QStringLiteral("client")).toString()).canonicalFilePath();
    const qint64 parentPid = qint64(request.value(QStringLiteral("parent_pid")).toDouble());
    QStringList found;
#ifdef Q_OS_WIN
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        fail(QStringLiteral("Не удалось проверить запущенные процессы"));
    PROCESSENTRY32W entry;
    std::memset(&entry, 0, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            const qint64 pid = qint64(entry.th32ProcessID);
            if (pid == QCoreApplication::applicationPid() || pid == parentPid) continue;
            const QString name = QString::fromWCharArray(entry.szExeFile);
            const QString lower = name.toLower();
            if (!lower.startsWith(QStringLiteral("tes3mp")) && !lower.startsWith(QStringLiteral("openmw"))) continue;
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
            if (!process) continue;
            wchar_t buffer[32768]; DWORD length = DWORD(sizeof(buffer) / sizeof(buffer[0]));
            if (QueryFullProcessImageNameW(process, 0, buffer, &length))
            {
                const QString processRoot = QFileInfo(QString::fromWCharArray(buffer, int(length))).absolutePath();
                if (QDir::cleanPath(processRoot).compare(QDir::cleanPath(root), Qt::CaseInsensitive) == 0)
                    found << name;
            }
            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
#elif defined(Q_OS_LINUX)
    QDir proc(QStringLiteral("/proc"));
    const QFileInfoList processes = proc.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& p : processes)
    {
        bool ok = false; const qint64 pid = p.fileName().toLongLong(&ok);
        if (!ok || pid == QCoreApplication::applicationPid() || pid == parentPid) continue;
        QFileInfo exe(p.absoluteFilePath() + QStringLiteral("/exe"));
        if (!exe.isSymLink()) continue;
        const QString path = exe.symLinkTarget();
        const QFileInfo e(path);
        const QString lower = e.fileName().toLower();
        if (e.absolutePath() == root && (lower.startsWith(QStringLiteral("tes3mp")) || lower.startsWith(QStringLiteral("openmw"))))
            found << e.fileName();
    }
#endif
    found.removeDuplicates();
    return found;
}

class InstallLock
{
public:
    explicit InstallLock(const QString& manifest)
    {
        mPath = QDir(QFileInfo(manifest).absolutePath()).filePath(QStringLiteral(".arena-update.lock"));
#ifdef Q_OS_WIN
        mHandle = CreateFileW(reinterpret_cast<LPCWSTR>(mPath.utf16()), GENERIC_READ | GENERIC_WRITE, 0,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (mHandle == INVALID_HANDLE_VALUE)
            fail(QStringLiteral("Другое обновление уже выполняется"));
#else
        mFd = ::open(QFile::encodeName(mPath).constData(), O_CREAT | O_RDWR, 0600);
        if (mFd < 0 || flock(mFd, LOCK_EX | LOCK_NB) != 0)
        {
            if (mFd >= 0) ::close(mFd);
            mFd = -1;
            fail(QStringLiteral("Другое обновление уже выполняется"));
        }
#endif
    }
    ~InstallLock()
    {
#ifdef Q_OS_WIN
        if (mHandle != INVALID_HANDLE_VALUE) CloseHandle(mHandle);
#else
        if (mFd >= 0) { flock(mFd, LOCK_UN); ::close(mFd); }
#endif
    }
private:
    QString mPath;
#ifdef Q_OS_WIN
    HANDLE mHandle = INVALID_HANDLE_VALUE;
#else
    int mFd = -1;
#endif
};

void rollback(const QString& job)
{
    const QString journalPath = QDir(job).filePath(QStringLiteral("journal.json"));
    QFile file(journalPath);
    if (!file.open(QIODevice::ReadOnly)) return;
    QJsonObject journal = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    const QString state = journal.value(QStringLiteral("state")).toString();
    if (state == QLatin1String("done") || state == QLatin1String("rolled-back")) return;
    const QJsonArray operations = journal.value(QStringLiteral("operations")).toArray();
    for (int i = operations.size() - 1; i >= 0; --i)
    {
        const QJsonObject op = operations[i].toObject();
        const QString dest = op.value(QStringLiteral("dest")).toString();
        const QString backup = op.value(QStringLiteral("backup")).toString();
        const QString temp = op.value(QStringLiteral("temp")).toString();
        if (QFileInfo::exists(backup) || QFileInfo(backup).isSymLink())
        {
            QFile::remove(dest);
            if (!QFile::rename(backup, dest))
                fail(QStringLiteral("Не удалось восстановить %1").arg(dest));
        }
        else if (!op.value(QStringLiteral("original")).toBool() && op.value(QStringLiteral("started")).toBool())
            QFile::remove(dest);
        QFile::remove(temp);
    }
    journal.insert(QStringLiteral("state"), QStringLiteral("rolled-back"));
    atomicJson(journalPath, journal);
}

void flushFile(QFile& file)
{
    file.flush();
#ifdef Q_OS_WIN
    FlushFileBuffers(reinterpret_cast<HANDLE>(_get_osfhandle(file.handle())));
#else
    ::fsync(file.handle());
#endif
}

void copyFileDurable(const QString& sourcePath, const QString& targetPath)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) fail(QStringLiteral("Не удалось прочитать %1").arg(sourcePath));
    QFile target(targetPath);
    if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate)) fail(QStringLiteral("Не удалось создать %1").arg(targetPath));
    QByteArray buffer(int(Chunk), '\0');
    while (!source.atEnd())
    {
        const qint64 got = source.read(buffer.data(), buffer.size());
        if (got < 0) fail(QStringLiteral("Ошибка чтения %1").arg(sourcePath));
        if (got && target.write(buffer.constData(), got) != got) fail(QStringLiteral("Ошибка записи %1").arg(targetPath));
    }
    target.setPermissions(source.permissions());
    flushFile(target);
    target.close(); source.close();
}

void commit(const QString& job, const QJsonObject& plan)
{
    const QJsonArray files = plan.value(QStringLiteral("files")).toArray();
    QJsonArray operations;
    const QString suffix = QStringLiteral(".arena-") + QFileInfo(job).fileName();
    for (const QJsonValue& value : files)
    {
        const QJsonObject entry = value.toObject();
        const QString dest = destinationPath(entry.value(QStringLiteral("root")).toString(), entry.value(QStringLiteral("relative")).toString());
        QDir().mkpath(QFileInfo(dest).absolutePath());
        QFileInfo info(dest);
        if ((info.exists() || info.isSymLink()) && !info.isFile() && !info.isSymLink())
            fail(QStringLiteral("Назначение не является обычным файлом: %1").arg(dest));
        const QString backup = dest + suffix + QStringLiteral(".bak");
        const QString temp = dest + suffix + QStringLiteral(".tmp");
        if (QFileInfo::exists(backup) || QFileInfo::exists(temp) || QFileInfo(backup).isSymLink() || QFileInfo(temp).isSymLink())
            fail(QStringLiteral("Найдены остатки незавершённой транзакции: %1").arg(dest));
        operations.append(QJsonObject{{QStringLiteral("dest"), dest}, {QStringLiteral("backup"), backup},
            {QStringLiteral("temp"), temp}, {QStringLiteral("original"), info.exists() || info.isSymLink()},
            {QStringLiteral("started"), true}});
    }
    QJsonObject journal{{QStringLiteral("state"), QStringLiteral("applying")}, {QStringLiteral("operations"), operations}};
    const QString journalPath = QDir(job).filePath(QStringLiteral("journal.json"));
    atomicJson(journalPath, journal);

    if (gWindow) { gWindow->setCancelable(false); gWindow->phase(QStringLiteral("Установка файлов…"), false); }
    try
    {
        for (int i = 0; i < files.size(); ++i)
        {
            const QJsonObject entry = files[i].toObject();
            const QJsonObject op = operations[i].toObject();
            const QString dest = op.value(QStringLiteral("dest")).toString();
            const QString backup = op.value(QStringLiteral("backup")).toString();
            const QString temp = op.value(QStringLiteral("temp")).toString();
            copyFileDurable(entry.value(QStringLiteral("source")).toString(), temp);
            if (op.value(QStringLiteral("original")).toBool())
            {
                if (!QFile::rename(dest, backup))
                    fail(QStringLiteral("Не удалось создать резервную копию %1").arg(dest));
            }
            if (!QFile::rename(temp, dest))
                fail(QStringLiteral("Не удалось заменить %1").arg(dest));
            if (gWindow) gWindow->itemProgress(i + 1, files.size(), QStringLiteral("Установка: %1 / %2 — %3").arg(i + 1).arg(files.size()).arg(entry.value(QStringLiteral("relative")).toString()));
        }
        journal.insert(QStringLiteral("state"), QStringLiteral("done"));
        atomicJson(journalPath, journal);
    }
    catch (...)
    {
        rollback(job);
        throw;
    }
    for (const QJsonValue& value : operations)
        QFile::remove(value.toObject().value(QStringLiteral("backup")).toString());
}

void recoverPending(const QJsonObject& request)
{
    const QString manifest = request.value(QStringLiteral("manifest")).toString();
    const QString pointer = QDir(QFileInfo(manifest).absolutePath()).filePath(QStringLiteral(".arena-update-pending.json"));
    QFile file(pointer);
    if (!file.open(QIODevice::ReadOnly)) return;
    const QJsonObject pending = QJsonDocument::fromJson(file.readAll()).object(); file.close();
    if (processAlive(qint64(pending.value(QStringLiteral("owner")).toDouble())))
        fail(QStringLiteral("Незавершённое обновление принадлежит другому запущенному процессу"));
    rollback(pending.value(QStringLiteral("job")).toString());
    QFile::remove(pointer);
}

struct Inspection
{
    IniData local;
    IniData remote;
    QMap<QString, QString> requested;
    bool readable = false;
};

Inspection inspectUpdates(const QJsonObject& request)
{
    Inspection result;
    try
    {
        const QString manifestPath = request.value(QStringLiteral("manifest")).toString();
        QFile manifest(manifestPath);
        if (!manifest.open(QIODevice::ReadOnly)) fail(QStringLiteral("Не удалось прочитать build.ini"));
        result.local = readIni(QString::fromUtf8(manifest.readAll()));
        manifest.close();
        result.readable = true;
        logEvent(QStringLiteral("local"), QJsonObject{{QStringLiteral("manifest"), manifestPath},
            {QStringLiteral("version"), buildValue(result.local, QStringLiteral("version"), QStringLiteral("00000"))},
            {QStringLiteral("build"), buildValue(result.local, QStringLiteral("build"), QStringLiteral("00000"))},
            {QStringLiteral("engine_key"), request.value(QStringLiteral("engine_key"))}});
        const QString checkUrl = buildValue(result.local, QStringLiteral("url_check"));
        if (checkUrl.isEmpty())
        {
            logEvent(QStringLiteral("no_check_url"), QJsonObject{{QStringLiteral("message"), QStringLiteral("В build.ini отсутствует url_check; запуск игры разрешён")}});
            return result;
        }
        if (gWindow) gWindow->phase(QStringLiteral("Проверка доступной версии…"));
        const QByteArray remoteBytes = download(checkUrl, QString(), MaxCheck);
        result.remote = readIni(QString::fromUtf8(remoteBytes));
        logEvent(QStringLiteral("remote"), QJsonObject{{QStringLiteral("version"), buildValue(result.remote, QStringLiteral("version"))},
            {QStringLiteral("build"), buildValue(result.remote, QStringLiteral("build"))}});
        for (const QString& key : {QStringLiteral("version"), QStringLiteral("build")})
        {
            const QString remote = buildValue(result.remote, key);
            if (revisionGreater(remote, buildValue(result.local, key, QStringLiteral("00000"))))
                result.requested.insert(key, remote);
        }
        if (request.value(QStringLiteral("engine_key")).toString() == QLatin1String("url_macos")
            && buildValue(result.local, QStringLiteral("url_macos")).isEmpty())
            result.requested.remove(QStringLiteral("build"));
        QJsonObject versions; for (auto it = result.requested.cbegin(); it != result.requested.cend(); ++it) versions.insert(it.key(), it.value());
        logEvent(QStringLiteral("comparison"), QJsonObject{{QStringLiteral("requested"), versions},
            {QStringLiteral("result"), result.requested.isEmpty() ? QStringLiteral("current") : QStringLiteral("available")}});
        return result;
    }
    catch (const std::exception& e)
    {
        if (gCancelRequested)
            throw;
        logEvent(QStringLiteral("offline"), QJsonObject{{QStringLiteral("message"), errorText(e)}});
        result.remote.values.clear(); result.requested.clear();
        return result;
    }
}

int checkUpdate(const QJsonObject& request)
{
    const QString pending = QDir(QFileInfo(request.value(QStringLiteral("manifest")).toString()).absolutePath()).filePath(QStringLiteral(".arena-update-pending.json"));
    if (QFileInfo::exists(pending))
    {
        logEvent(QStringLiteral("available"), QJsonObject{{QStringLiteral("recovery"), true}});
        return 10;
    }
    const Inspection inspection = inspectUpdates(request);
    if (!inspection.requested.isEmpty())
    {
        QJsonObject versions; for (auto it = inspection.requested.cbegin(); it != inspection.requested.cend(); ++it) versions.insert(it.key(), it.value());
        logEvent(QStringLiteral("available"), QJsonObject{{QStringLiteral("versions"), versions}});
        return 10;
    }
    return 0;
}

int prepareUpdate(const QJsonObject& request, const QString& job)
{
    const QString manifestPath = request.value(QStringLiteral("manifest")).toString();
    InstallLock lock(manifestPath);
    try { recoverPending(request); }
    catch (const std::exception& e) { logEvent(QStringLiteral("blocked"), QJsonObject{{QStringLiteral("message"), errorText(e)}}); return 20; }

    const Inspection inspection = inspectUpdates(request);
    if (inspection.requested.isEmpty()) return 0;
    const QStringList running = otherClients(request);
    if (!running.isEmpty())
    {
        logEvent(QStringLiteral("blocked"), QJsonObject{{QStringLiteral("message"),
            QStringLiteral("Закройте игру/сервер/мастер перед обновлением: %1").arg(running.join(QStringLiteral(", ")))}});
        return 20;
    }

    QJsonArray planFiles;
    QSet<QString> destinations;
    struct Target { QString kind, root, url, hashKey; };
    std::vector<Target> targets;
    if (inspection.requested.contains(QStringLiteral("version")))
        targets.push_back({QStringLiteral("content"), request.value(QStringLiteral("data")).toString(),
            buildValue(inspection.local, QStringLiteral("url_update"), buildValue(inspection.local, QStringLiteral("update"))), QStringLiteral("sha256_update")});
    if (inspection.requested.contains(QStringLiteral("build")))
    {
        const QString key = request.value(QStringLiteral("engine_key")).toString();
        targets.push_back({QStringLiteral("engine"), request.value(QStringLiteral("client")).toString(),
            buildValue(inspection.local, key), QStringLiteral("sha256_") + key.mid(4)});
    }

    for (const Target& target : targets)
    {
        if (gWindow) gWindow->phase(target.kind == QLatin1String("content") ? QStringLiteral("Загрузка обновления контента…")
                                                                            : QStringLiteral("Загрузка обновления клиента…"), false);
        logEvent(QStringLiteral("package"), QJsonObject{{QStringLiteral("kind"), target.kind}});
        const QString archive = QDir(job).filePath(target.kind + QStringLiteral(".download"));
        download(target.url, archive, -1, buildValue(inspection.remote, target.hashKey));
        const QString stage = QDir(job).filePath(target.kind);
        if (gWindow) gWindow->phase(target.kind == QLatin1String("content") ? QStringLiteral("Распаковка контента…")
                                                                            : QStringLiteral("Распаковка клиента…"), false);
        logEvent(QStringLiteral("extract"), QJsonObject{{QStringLiteral("kind"), target.kind}});
        extractArchive(archive, stage);
        QFile::remove(archive);
        const QString payload = payloadRoot(stage, target.kind);
        if (target.kind == QLatin1String("engine"))
        {
            const QStringList expected = request.value(QStringLiteral("engine_key")).toString() == QLatin1String("url_win")
                ? QStringList{QStringLiteral("tes3mp.exe")} : QStringList{QStringLiteral("tes3mp"), QStringLiteral("tes3mp.x86_64")};
            bool found = false; for (const QString& name : expected) if (QFileInfo(QDir(payload).filePath(name)).isFile()) found = true;
            if (!found) fail(QStringLiteral("Архив движка не содержит клиент для этой платформы"));
        }
        QDirIterator it(payload, QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks, QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            const QString source = it.next();
            const QString relative = QDir(payload).relativeFilePath(source).replace(QLatin1Char('\\'), QLatin1Char('/'));
            // U024g: requiredDataFiles.json is generated from the host's selected
            // content/CRC list. Never let a stock client package replace an
            // existing host manifest, independently of server code updates.
            if (target.kind == QLatin1String("engine") && isRequiredDataFilesManifest(relative))
            {
                logEvent(QStringLiteral("preserve"), QJsonObject{
                    {QStringLiteral("kind"), target.kind},
                    {QStringLiteral("relative"), relative},
                    {QStringLiteral("reason"), QStringLiteral("requiredDataFiles.json is host-generated")}
                });
                continue;
            }
            if (isProtected(relative, target.kind)) continue;
            if (target.kind == QLatin1String("engine") &&
                ArenaUpdater::serverFilePolicy(relative.toStdString()) == ArenaUpdater::ServerFilePolicy::SeedOnly)
            {
                const QFileInfo existing(destinationPath(target.root, relative));
                if (existing.exists() || existing.isSymLink())
                {
                    logEvent(QStringLiteral("preserve"), QJsonObject{
                        {QStringLiteral("relative"), relative},
                        {QStringLiteral("reason"), QStringLiteral("server configuration or custom script")}});
                    continue;
                }
            }
            const QString dest = destinationPath(target.root, relative).toCaseFolded();
            if (destinations.contains(dest)) fail(QStringLiteral("Пакеты обновления пытаются заменить один и тот же файл"));
            destinations.insert(dest);
            planFiles.append(QJsonObject{{QStringLiteral("source"), QFileInfo(source).absoluteFilePath()},
                {QStringLiteral("root"), QFileInfo(target.root).absoluteFilePath()}, {QStringLiteral("relative"), relative}});
        }
    }
    if (planFiles.isEmpty()) fail(QStringLiteral("В обновлении нет применимых файлов"));

    QFile manifest(manifestPath);
    if (!manifest.open(QIODevice::ReadOnly)) fail(QStringLiteral("Не удалось перечитать build.ini"));
    const QString stamped = stampManifest(QString::fromUtf8(manifest.readAll()), inspection.requested);
    manifest.close();
    const QString stagedManifest = QDir(job).filePath(QStringLiteral("build.ini"));
    QFile out(stagedManifest);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(stamped.toUtf8()) < 0)
        fail(QStringLiteral("Не удалось подготовить новый build.ini"));
    out.close();
    planFiles.append(QJsonObject{{QStringLiteral("source"), stagedManifest},
        {QStringLiteral("root"), QFileInfo(manifestPath).absolutePath()}, {QStringLiteral("relative"), QFileInfo(manifestPath).fileName()}});

    QJsonObject versions; for (auto it = inspection.requested.cbegin(); it != inspection.requested.cend(); ++it) versions.insert(it.key(), it.value());
    atomicJson(QDir(job).filePath(QStringLiteral("plan.json")), QJsonObject{{QStringLiteral("files"), planFiles}, {QStringLiteral("versions"), versions}});
    atomicJson(QDir(QFileInfo(manifestPath).absolutePath()).filePath(QStringLiteral(".arena-update-pending.json")),
        QJsonObject{{QStringLiteral("job"), QFileInfo(job).absoluteFilePath()}, {QStringLiteral("owner"), double(request.value(QStringLiteral("parent_pid")).toDouble())}});
    logEvent(QStringLiteral("ready"), QJsonObject{{QStringLiteral("files"), planFiles.size()}});
    return 10;
}

bool launchLauncher(const QJsonObject& request, bool resume)
{
    QStringList args;
    for (const QJsonValue& value : request.value(QStringLiteral("launcher_args")).toArray()) args << value.toString();
    if (resume) args << QStringLiteral("--arena-update-resume");
    const QString executable = request.value(QStringLiteral("launcher")).toString();
    const QString cwd = request.value(QStringLiteral("client")).toString();
    logEvent(QStringLiteral("launcher_start"), QJsonObject{{QStringLiteral("executable"), executable},
        {QStringLiteral("resume"), resume}, {QStringLiteral("cwd"), cwd}});
    return QProcess::startDetached(executable, args, cwd);
}

bool waitForLauncherExit(const QJsonObject& request)
{
    const qint64 pid = qint64(request.value(QStringLiteral("parent_pid")).toDouble());
    if (gWindow)
    {
        gWindow->setCancelable(false);
        gWindow->phase(QStringLiteral("Ожидание закрытия лаунчера…"));
    }
    logEvent(QStringLiteral("wait_launcher"), QJsonObject{{QStringLiteral("parent_pid"), double(pid)}});
    QElapsedTimer timer; timer.start();
    while (processAlive(pid))
    {
        if (timer.elapsed() > 120000)
        {
            const QString message = QStringLiteral("Лаунчер не завершился за 120 секунд; файлы не изменены");
            logEvent(QStringLiteral("error"), QJsonObject{{QStringLiteral("message"), message}});
            atomicJson(QDir(QFileInfo(request.value(QStringLiteral("manifest")).toString()).absolutePath()).filePath(QStringLiteral(".arena-update-result.json")),
                QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}});
            return false;
        }
        QThread::msleep(100);
        QCoreApplication::processEvents();
    }
    logEvent(QStringLiteral("launcher_exited"), QJsonObject{{QStringLiteral("parent_pid"), double(pid)}});
    if (gWindow) gWindow->setCancelable(true);
    return true;
}

int applyPrepared(const QJsonObject& request, const QString& job, bool alreadyWaited)
{
    const QString manifestPath = request.value(QStringLiteral("manifest")).toString();
    const QString resultPath = QDir(QFileInfo(manifestPath).absolutePath()).filePath(QStringLiteral(".arena-update-result.json"));
    const QString pointer = QDir(QFileInfo(manifestPath).absolutePath()).filePath(QStringLiteral(".arena-update-pending.json"));
    if (!alreadyWaited && !waitForLauncherExit(request)) return 1;
    bool ok = false;
    try
    {
        InstallLock lock(manifestPath);
        atomicJson(pointer, QJsonObject{{QStringLiteral("job"), QFileInfo(job).absoluteFilePath()},
            {QStringLiteral("owner"), double(QCoreApplication::applicationPid())}});
        const QStringList running = otherClients(request);
        if (!running.isEmpty()) fail(QStringLiteral("Игра/сервер/мастер всё ещё запущены: %1").arg(running.join(QStringLiteral(", "))));
        QFile planFile(QDir(job).filePath(QStringLiteral("plan.json")));
        if (!planFile.open(QIODevice::ReadOnly)) fail(QStringLiteral("План обновления не найден"));
        const QJsonObject plan = QJsonDocument::fromJson(planFile.readAll()).object(); planFile.close();
        logEvent(QStringLiteral("apply"), QJsonObject{{QStringLiteral("files"), plan.value(QStringLiteral("files")).toArray().size()},
            {QStringLiteral("versions"), plan.value(QStringLiteral("versions"))}});
        commit(job, plan);
        QFile::remove(pointer);
        atomicJson(resultPath, QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("versions"), plan.value(QStringLiteral("versions"))}});
        logEvent(QStringLiteral("installed"), QJsonObject{{QStringLiteral("versions"), plan.value(QStringLiteral("versions"))}});
        ok = true;
        if (gWindow) gWindow->finish(QStringLiteral("Обновление установлено успешно"), true);
    }
    catch (const std::exception& e)
    {
        const QString message = errorText(e);
        logEvent(QStringLiteral("error"), QJsonObject{{QStringLiteral("message"), message}});
        try { atomicJson(resultPath, QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}}); } catch (...) {}
        if (gWindow) gWindow->finish(QStringLiteral("Не удалось установить обновление"), false);
    }
    if (!launchLauncher(request, ok))
    {
        try { atomicJson(resultPath, QJsonObject{{QStringLiteral("ok"), false},
            {QStringLiteral("message"), QStringLiteral("Обновление обработано, но лаунчер не удалось открыть автоматически. Запустите его вручную.")}}); } catch (...) {}
        return 1;
    }
    if (ok)
        QDir(job).removeRecursively();
    return ok ? 0 : 1;
}

int updateSupervisor(const QJsonObject& request, const QString& job)
{
    const QString resultPath = QDir(QFileInfo(request.value(QStringLiteral("manifest")).toString()).absolutePath()).filePath(QStringLiteral(".arena-update-result.json"));
    if (!waitForLauncherExit(request))
    {
        if (gCancelRequested)
        {
            try { atomicJson(resultPath, QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("message"), QStringLiteral("Обновление отменено пользователем")}}); } catch (...) {}
            launchLauncher(request, false);
        }
        return 1;
    }
    int code = 1;
    try
    {
        code = prepareUpdate(request, job);
    }
    catch (const std::exception& e)
    {
        const QString message = errorText(e);
        logEvent(QStringLiteral("error"), QJsonObject{{QStringLiteral("message"), message}});
        try { atomicJson(resultPath, QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}}); } catch (...) {}
        if (gWindow) gWindow->finish(QStringLiteral("Не удалось подготовить обновление"), false);
        launchLauncher(request, false);
        return 1;
    }
    if (code == 10)
        return applyPrepared(request, job, true);
    if (code == 0)
    {
        if (gWindow) gWindow->finish(QStringLiteral("Установлена актуальная версия"), true);
        launchLauncher(request, true);
        QDir(job).removeRecursively();
        return 0;
    }
    if (!QFileInfo::exists(resultPath))
    {
        try { atomicJson(resultPath, QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("message"), QStringLiteral("Обновление не удалось подготовить")}}); } catch (...) {}
    }
    if (gWindow) gWindow->finish(QStringLiteral("Обновление заблокировано"), false);
    launchLauncher(request, false);
    QDir(job).removeRecursively();
    return 1;
}

int selfTest()
{
    try
    {
        if (!revisionGreater(QStringLiteral("00010"), QStringLiteral("00009"))) return 2;
        if (safeArchiveName(QStringLiteral("./Textures/test.dds")) != QLatin1String("Textures/test.dds")) return 3;
        bool rejected = false;
        try { safeArchiveName(QStringLiteral("../evil")); } catch (...) { rejected = true; }
        if (!rejected) return 4;
        const QString original = QStringLiteral("#x\n[Build]\nversion=00001\nbuild=00002\n[Server]\naddress=127.0.0.1\n");
        const QString changed = stampManifest(original, {{QStringLiteral("version"), QStringLiteral("00003")}});
        if (!changed.contains(QStringLiteral("version=00003")) || !changed.contains(QStringLiteral("address=127.0.0.1"))) return 5;
        // U020: engine packages never replace the host's data-file manifest.
        if (!isProtected(QStringLiteral("server/data/requiredDataFiles.json"), QStringLiteral("engine"))) return 6;
        if (!isProtected(QStringLiteral("Resources/server/data/RequiredDataFiles.json"), QStringLiteral("engine"))) return 7;
        if (!isRequiredDataFilesManifest(QStringLiteral("server/data/requiredDataFiles.json"))) return 8;
        if (!isRequiredDataFilesManifest(QStringLiteral("Resources/server/data/REQUIREDDATAFILES.JSON"))) return 9;
        if (isRequiredDataFilesManifest(QStringLiteral("server/data/other.json"))) return 10;
        if (isProtected(QStringLiteral("tes3mp.exe"), QStringLiteral("engine"))) return 11;
        if (isProtected(QStringLiteral("server/scripts/houseHelper.lua"), QStringLiteral("engine"))) return 12;
        if (isProtected(QStringLiteral("Resources/server/scripts/cell/base.lua"), QStringLiteral("engine"))) return 13;
        if (!isProtected(QStringLiteral("server/data/cell/house.json"), QStringLiteral("engine"))) return 14;
        if (!isProtected(QStringLiteral("Resources/server/data/player/alice.json"), QStringLiteral("engine"))) return 15;
        if (isProtected(QStringLiteral("server/lib/cjson.dll"), QStringLiteral("engine"))) return 16;
#ifdef Q_OS_WIN
        std::fprintf(stderr, "arena-updater native self-test: OK; transport=winhttp\n");
#else
        std::fprintf(stderr, "arena-updater native self-test: OK; transport=qtnetwork\n");
#endif
        return 0;
    }
    catch (...) { return 99; }
}
}

int main(int argc, char** argv)
{
    // Keep the CI/self-test path independent of a GUI platform plugin. This is
    // useful before qwindows.dll is copied into a freshly installed tree.
    if (argc == 2 && std::strcmp(argv[1], "--self-test") == 0)
    {
        QCoreApplication core(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("ArenaMP Updater"));
        QCoreApplication::setOrganizationName(QStringLiteral("ArenaMP"));
        return selfTest();
    }

#ifdef Q_OS_WIN
    // The updater is a native Windows GUI. Do not inherit an accidental
    // QT_QPA_PLATFORM=offscreen/minimal from a development environment; such
    // a setting would make the update work while its progress window remained
    // completely invisible.
    qputenv("QT_QPA_PLATFORM", QByteArray("windows"));
#endif
    QApplication app(argc, argv);
    ArenaUi::applyMorrowindGlassPalette(app);
    app.setQuitOnLastWindowClosed(false);
    QCoreApplication::setApplicationName(QStringLiteral("ArenaMP Updater"));
    QCoreApplication::setOrganizationName(QStringLiteral("ArenaMP"));

    const QStringList args = QCoreApplication::arguments();
    if (args.size() != 3)
        return 64;

    const QString action = args[1].toLower();
    const QString requestPath = QFileInfo(args[2]).absoluteFilePath();
    QFile requestFile(requestPath);
    if (!requestFile.open(QIODevice::ReadOnly))
        return 65;
    QJsonParseError jsonError;
    const QJsonDocument document = QJsonDocument::fromJson(requestFile.readAll(), &jsonError);
    requestFile.close();
    if (jsonError.error != QJsonParseError::NoError || !document.isObject())
        return 66;
    const QJsonObject request = document.object();
    const QString job = QFileInfo(requestPath).absolutePath();
    configureLog(request, job);

    ProgressWindow window;
    ArenaUi::installGlassWindow(window);
    if (action != QLatin1String("check"))
    {
        gWindow = &window;
        window.present();
    }

    logEvent(QStringLiteral("start"), QJsonObject{{QStringLiteral("action"), action},
        {QStringLiteral("request"), requestPath}, {QStringLiteral("client"), request.value(QStringLiteral("client"))},
        {QStringLiteral("data"), request.value(QStringLiteral("data"))}, {QStringLiteral("log"), gLogPath},
        {QStringLiteral("native_qt"), true}});

    int code = 1;
    try
    {
        if (action == QLatin1String("check")) code = checkUpdate(request);
        else if (action == QLatin1String("prepare")) code = prepareUpdate(request, job);
        else if (action == QLatin1String("apply")) code = applyPrepared(request, job, false);
        else if (action == QLatin1String("update")) code = updateSupervisor(request, job);
        else code = 64;
    }
    catch (const std::exception& e)
    {
        logEvent(QStringLiteral("error"), QJsonObject{{QStringLiteral("message"), errorText(e)}});
        if (gWindow) gWindow->finish(QStringLiteral("Ошибка обновления"), false);
        code = 1;
    }
    logEvent(QStringLiteral("finish"), QJsonObject{{QStringLiteral("action"), action}, {QStringLiteral("exit_code"), code}});
    if (gWindow)
        gWindow->linger();
    gWindow = nullptr;
    return code;
}
