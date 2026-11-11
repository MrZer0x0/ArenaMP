#include "serverdialog.hpp"

#include <algorithm>
#include <utility>

#include <QAbstractSocket>
#include <QApplication>
#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QColor>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QHostAddress>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QNetworkInterface>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStringList>
#include <QTextCodec>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextStream>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTimer>
#include <QTcpSocket>
#include <QVBoxLayout>

namespace
{
    class ServerLogHighlighter final : public QSyntaxHighlighter
    {
    public:
        explicit ServerLogHighlighter(QTextDocument* document)
            : QSyntaxHighlighter(document)
        {
        }

    protected:
        void highlightBlock(const QString& text) override
        {
            if (text.isEmpty())
                return;

            QTextCharFormat format;
            format.setForeground(QColor(QStringLiteral("#cfc9bf")));

            const QString lower = text.toLower();
            if (lower.contains(QStringLiteral("[error]"))
                || lower.contains(QStringLiteral("fatal"))
                || lower.contains(QStringLiteral("exception"))
                || lower.contains(QStringLiteral("crash"))
                || lower.contains(QString::fromUtf8(u8"ошибка"))
                || lower.contains(QString::fromUtf8(u8"аварийн")))
            {
                format.setForeground(QColor(QStringLiteral("#ff8078")));
                format.setFontWeight(QFont::DemiBold);
            }
            else if (lower.contains(QStringLiteral("[warn]"))
                || lower.contains(QStringLiteral("warning"))
                || lower.contains(QString::fromUtf8(u8"предупрежд")))
            {
                format.setForeground(QColor(QStringLiteral("#e6b966")));
            }
            else if (lower.contains(QStringLiteral("started"))
                || lower.contains(QStringLiteral("listening"))
                || lower.contains(QStringLiteral("connected"))
                || lower.contains(QStringLiteral("backup created"))
                || lower.contains(QString::fromUtf8(u8"запущ"))
                || lower.contains(QString::fromUtf8(u8"подключ"))
                || lower.contains(QString::fromUtf8(u8"резервная копия создана")))
            {
                format.setForeground(QColor(QStringLiteral("#71c78b")));
            }
            else if (lower.contains(QStringLiteral("[info]")))
            {
                format.setForeground(QColor(QStringLiteral("#aaa69e")));
            }

            setFormat(0, text.size(), format);
        }
    };

    bool copyFileReplacing(const QString& sourcePath, const QString& destinationPath, QString* errorMessage)
    {
        const QFileInfo sourceInfo(sourcePath);
        if (!sourceInfo.exists() || !sourceInfo.isFile())
        {
            if (errorMessage)
                *errorMessage = QObject::tr("Bundled server file is missing: %1")
                    .arg(QDir::toNativeSeparators(sourcePath));
            return false;
        }

        const QFileInfo destinationInfo(destinationPath);
        if (!QDir().mkpath(destinationInfo.absolutePath()))
        {
            if (errorMessage)
                *errorMessage = QObject::tr("Could not create userdata directory: %1")
                    .arg(QDir::toNativeSeparators(destinationInfo.absolutePath()));
            return false;
        }

        if (destinationInfo.exists() && !QFile::remove(destinationPath))
        {
            if (errorMessage)
                *errorMessage = QObject::tr("Could not replace server configuration: %1")
                    .arg(QDir::toNativeSeparators(destinationPath));
            return false;
        }

        if (!QFile::copy(sourcePath, destinationPath))
        {
            if (errorMessage)
                *errorMessage = QObject::tr("Could not copy server configuration from %1 to %2")
                    .arg(QDir::toNativeSeparators(sourcePath), QDir::toNativeSeparators(destinationPath));
            return false;
        }

        QFile::setPermissions(destinationPath, sourceInfo.permissions());
        return true;
    }

    bool writeTextFile(const QString& path, const QString& text, QString* errorMessage)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        {
            if (errorMessage)
                *errorMessage = QObject::tr("Could not write server configuration: %1")
                    .arg(QDir::toNativeSeparators(path));
            return false;
        }

        const QByteArray data = text.toUtf8();
        if (file.write(data) != data.size())
        {
            if (errorMessage)
                *errorMessage = QObject::tr("Could not finish writing server configuration: %1")
                    .arg(QDir::toNativeSeparators(path));
            return false;
        }
        return true;
    }

    bool setIniValue(const QString& configPath, const QString& sectionName,
        const QString& keyName, const QString& value, QString* errorMessage)
    {
        QFile configFile(configPath);
        if (!configFile.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            if (errorMessage)
                *errorMessage = QObject::tr("Could not read server configuration: %1")
                    .arg(QDir::toNativeSeparators(configPath));
            return false;
        }

        const QByteArray originalData = configFile.readAll();
        configFile.close();
        const QString lineEnding = originalData.contains("\r\n")
            ? QStringLiteral("\r\n") : QStringLiteral("\n");
        QStringList lines = QString::fromUtf8(originalData)
            .split(QRegularExpression(QStringLiteral("\r?\n")));

        int sectionLine = -1;
        int insertionLine = lines.size();
        int valueLine = -1;
        QString currentSection;

        for (int i = 0; i < lines.size(); ++i)
        {
            const QString trimmed = lines.at(i).trimmed();
            if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']')))
            {
                const QString section = trimmed.mid(1, trimmed.size() - 2).trimmed();
                if (sectionLine >= 0 && currentSection == sectionName)
                {
                    insertionLine = i;
                    break;
                }
                currentSection = section;
                if (section == sectionName)
                    sectionLine = i;
                continue;
            }

            if (currentSection != sectionName || trimmed.startsWith(QLatin1Char('#'))
                || trimmed.startsWith(QLatin1Char(';')))
                continue;

            const int equalsPosition = trimmed.indexOf(QLatin1Char('='));
            if (equalsPosition >= 0 && trimmed.left(equalsPosition).trimmed() == keyName)
            {
                valueLine = i;
                break;
            }
        }

        if (valueLine >= 0)
        {
            const QString originalLine = lines.at(valueLine);
            const int equalsPosition = originalLine.indexOf(QLatin1Char('='));
            lines[valueLine] = equalsPosition >= 0
                ? originalLine.left(equalsPosition + 1) + QLatin1Char(' ') + value
                : keyName + QStringLiteral(" = ") + value;
        }
        else if (sectionLine >= 0)
        {
            lines.insert(insertionLine, keyName + QStringLiteral(" = ") + value);
        }
        else
        {
            if (!lines.isEmpty() && !lines.last().isEmpty())
                lines.append(QString());
            lines.append(QLatin1Char('[') + sectionName + QLatin1Char(']'));
            lines.append(keyName + QStringLiteral(" = ") + value);
        }

        return writeTextFile(configPath, lines.join(lineEnding), errorMessage);
    }
    bool clearDirectoryContents(const QString& directoryPath, QString* errorMessage,
        const QString& preservedFile = QString())
    {
        QDir dir(directoryPath);
        if (!dir.exists())
            return QDir().mkpath(directoryPath);

        const QFileInfoList entries = dir.entryInfoList(
            QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);
        for (const QFileInfo& info : entries)
        {
            if (info.fileName() == QLatin1String(".gitkeep"))
                continue;
            if (!preservedFile.isEmpty() && info.fileName() == preservedFile)
                continue;

            bool ok = true;
            if (info.isDir())
                ok = QDir(info.absoluteFilePath()).removeRecursively();
            else
                ok = QFile::remove(info.absoluteFilePath());

            if (!ok)
            {
                if (errorMessage)
                    *errorMessage = QObject::tr("Could not remove server data: %1")
                        .arg(QDir::toNativeSeparators(info.absoluteFilePath()));
                return false;
            }
        }
        return true;
    }

}

Launcher::ServerDialog::ServerDialog(QWidget* parent)
    : QWidget(parent)
    , mAddressLabel(nullptr)
    , mPortLabel(nullptr)
    , mEncodingLabel(nullptr)
    , mEncodingCombo(nullptr)
    , mRestartCheckBox(nullptr)
    , mLogView(nullptr)
    , mStopButton(nullptr)
    , mCloseButton(nullptr)
    , mProcess(new QProcess(this))
    , mBackupProcess(new QProcess(this))
    , mLogViewRenderedBytes(0)
    , mLogRefreshPending(false)
    , mRestartCounter(0)
    , mStopRequested(false)
    , mBackupInProgress(false)
    , mRestartAfterBackup(false)
    , mCrashAfterStop(false)
    , mRapidCrashCount(0)
    , mLastStartMs(0)
    , mCachedDisplayAddressAtMs(0)
{
    setObjectName(QStringLiteral("serverConsolePanel"));
    setProperty("arenaServerConsole", true);
    setWindowTitle(tr("Server Console"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    // Compact status card.  The live endpoint stays visible while the console
    // remains the dominant element of the page.
    QFrame* statusCard = new QFrame(this);
    statusCard->setObjectName(QStringLiteral("serverConsoleStatusCard"));
    statusCard->setProperty("arenaCard", true);
    QHBoxLayout* statusLayout = new QHBoxLayout(statusCard);
    statusLayout->setContentsMargins(12, 8, 12, 8);
    statusLayout->setSpacing(10);

    QVBoxLayout* titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(1);
    QLabel* title = new QLabel(tr("Server Console"), statusCard);
    title->setProperty("arenaTitle", true);
    QLabel* subtitle = new QLabel(tr("Live server output and process controls"), statusCard);
    subtitle->setProperty("arenaMuted", true);
    titleLayout->addWidget(title);
    titleLayout->addWidget(subtitle);
    statusLayout->addLayout(titleLayout, 1);

    mAddressLabel = new QLabel(statusCard);
    mAddressLabel->setProperty("arenaConsoleAddress", true);
    mAddressLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mPortLabel = new QLabel(statusCard);
    mPortLabel->setProperty("arenaConsolePort", true);
    mPortLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusLayout->addWidget(mAddressLabel, 0, Qt::AlignVCenter);
    statusLayout->addWidget(mPortLabel, 0, Qt::AlignVCenter);
    mainLayout->addWidget(statusCard);

    const ServerConfig initialConfig = readServerConfig();
    mAddressLabel->setText(tr("Connect IP: %1").arg(resolveDisplayAddress(initialConfig.localAddress)));
    mPortLabel->setText(tr("Port: %1").arg(initialConfig.port));

    // Toolbar-style options replace the old loose row of controls.
    QFrame* toolbar = new QFrame(this);
    toolbar->setObjectName(QStringLiteral("serverConsoleToolbar"));
    toolbar->setProperty("arenaToolbar", true);
    QHBoxLayout* optionsLayout = new QHBoxLayout(toolbar);
    optionsLayout->setContentsMargins(10, 6, 10, 6);
    optionsLayout->setSpacing(8);

    mEncodingLabel = new QLabel(tr("Log encoding:"), toolbar);
    mEncodingLabel->setProperty("arenaMuted", true);
    mEncodingCombo = new QComboBox(toolbar);
    mEncodingCombo->setMinimumWidth(118);
    mEncodingCombo->addItem(tr("UTF-8"), QStringLiteral("UTF-8"));
    mEncodingCombo->addItem(tr("System"), QStringLiteral("System"));
    mEncodingCombo->addItem(tr("Windows-1251"), QStringLiteral("Windows-1251"));
    mEncodingCombo->addItem(tr("CP866"), QStringLiteral("CP866"));
    mRestartCheckBox = new QCheckBox(tr("Auto restart and backup"), toolbar);
    mRestartCheckBox->setChecked(false);

    optionsLayout->addWidget(mEncodingLabel);
    optionsLayout->addWidget(mEncodingCombo);
    optionsLayout->addSpacing(6);
    optionsLayout->addWidget(mRestartCheckBox);
    optionsLayout->addStretch(1);

    mCloseButton = new QPushButton(tr("Clear Log"), toolbar);
    mCloseButton->setProperty("arenaQuiet", true);
    mCloseButton->setMinimumWidth(98);
    mStopButton = new QPushButton(tr("Stop Server"), toolbar);
    mStopButton->setProperty("arenaDanger", true);
    mStopButton->setMinimumWidth(110);
    mStopButton->setEnabled(false);
    optionsLayout->addWidget(mCloseButton);
    optionsLayout->addWidget(mStopButton);
    mainLayout->addWidget(toolbar);

    QFrame* consoleCard = new QFrame(this);
    consoleCard->setObjectName(QStringLiteral("serverConsoleCard"));
    consoleCard->setProperty("arenaConsoleCard", true);
    QVBoxLayout* consoleLayout = new QVBoxLayout(consoleCard);
    consoleLayout->setContentsMargins(0, 0, 0, 0);
    consoleLayout->setSpacing(0);

    mLogView = new QPlainTextEdit(consoleCard);
    mLogView->setObjectName(QStringLiteral("serverConsoleLog"));
    mLogView->setProperty("arenaConsole", true);
    mLogView->setReadOnly(true);
    mLogView->setLineWrapMode(QPlainTextEdit::NoWrap);
    mLogView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    mLogView->setFrameShape(QFrame::NoFrame);
    mLogView->setPlaceholderText(tr("Server output will appear here."));
    // Y050/U015: bounded console RAM usage is preserved despite the richer UI.
    mLogView->document()->setMaximumBlockCount(5000);
    new ServerLogHighlighter(mLogView->document());
    consoleLayout->addWidget(mLogView, 1);
    mainLayout->addWidget(consoleCard, 1);

    QLabel* memoryHint = new QLabel(tr("The console keeps only the latest 5000 lines in memory; the server log remains on disk."), this);
    memoryHint->setProperty("arenaMuted", true);
    memoryHint->setWordWrap(true);
    mainLayout->addWidget(memoryHint);

    connect(mStopButton, SIGNAL(clicked()), this, SLOT(stopServer()));
    connect(mCloseButton, &QPushButton::clicked, this, &ServerDialog::clearLogView);
    connect(mEncodingCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(refreshDecodedLog()));
    connect(mRestartCheckBox, SIGNAL(toggled(bool)), this, SIGNAL(autoRestartChanged(bool)));
    connect(mProcess, SIGNAL(readyReadStandardOutput()), this, SLOT(processReadyReadStandardOutput()));
    connect(mProcess, SIGNAL(readyReadStandardError()), this, SLOT(processReadyReadStandardError()));
    connect(mProcess, SIGNAL(finished(int,QProcess::ExitStatus)), this, SLOT(processFinished(int,QProcess::ExitStatus)));
    connect(mProcess, SIGNAL(error(QProcess::ProcessError)), this, SLOT(processError(QProcess::ProcessError)));
    connect(mBackupProcess, SIGNAL(finished(int,QProcess::ExitStatus)), this, SLOT(backupProcessFinished(int,QProcess::ExitStatus)));
    connect(mBackupProcess, SIGNAL(error(QProcess::ProcessError)), this, SLOT(backupProcessError(QProcess::ProcessError)));
}
Launcher::ServerDialog::~ServerDialog()
{
}

void Launcher::ServerDialog::setPreStartCheck(std::function<bool(QString*)> check)
{
    mPreStartCheck = std::move(check);
}

bool Launcher::ServerDialog::startServer()
{
    if (mProcess->state() != QProcess::NotRunning)
        return true;

    mStopRequested = false;
    mCloseButton->setEnabled(true);
    mStopButton->setEnabled(true);
    // A new server process starts a new diagnostic session. Do not keep the
    // previous run duplicated in launcher RAM; the dedicated server also
    // truncates tes3mp-server.log on startup.
    clearLogView();

    QString preparationError;
    if (!preparePortableServer(&preparationError))
    {
        QMessageBox::warning(this, tr("Error preparing ArenaMP server"), preparationError);
        appendStatusLine(tr("Server preparation failed: %1").arg(preparationError));
        mStopButton->setEnabled(false);
        return false;
    }

    if (mPreStartCheck && !mPreStartCheck(&preparationError))
    {
        appendStatusLine(tr("Server content verification failed: %1").arg(preparationError));
        QMessageBox::critical(this, tr("Server cannot start"), preparationError);
        mStopButton->setEnabled(false);
        return false;
    }

    const ServerConfig config = readServerConfig();
    mAddressLabel->setText(tr("Connect IP: %1").arg(resolveDisplayAddress(config.localAddress)));
    mPortLabel->setText(tr("Port: %1").arg(config.port));

    appendStatusLine(tr("Starting tes3mp-server..."));
    appendStatusLine(tr("Server config: %1").arg(QDir::toNativeSeparators(config.configPath)));
    appendStatusLine(tr("Server data: %1").arg(QDir::toNativeSeparators(config.serverHomePath)));
    appendStatusLine(tr("Log encoding: %1").arg(mEncodingCombo->currentText()));

    const QString executable = resolveServerExecutable();
    if (executable.isEmpty())
    {
        QMessageBox::warning(this, tr("Error starting executable"),
            tr("Could not find tes3mp-server executable next to the launcher."));
        mStopButton->setEnabled(false);
        return false;
    }

    mProcess->setProgram(QDir::toNativeSeparators(executable));
    mProcess->setArguments(QStringList());
    mProcess->setProcessChannelMode(QProcess::SeparateChannels);

    // Mark this child as launcher-managed. The 12h schedule still lives in the
    // dedicated-server core, but on restart the server returns the reserved code
    // to this QProcess instead of spawning an untracked orphan. Standalone
    // tes3mp-server launches self-relaunch natively.
    QProcessEnvironment processEnvironment = QProcessEnvironment::systemEnvironment();
    processEnvironment.insert(QStringLiteral("ARENAMP_LAUNCHER_MANAGED"), QStringLiteral("1"));
    mProcess->setProcessEnvironment(processEnvironment);

    const QString workingDirectory = serverRuntimeBasePath();
    mProcess->setWorkingDirectory(workingDirectory);

    mLastStartMs = QDateTime::currentMSecsSinceEpoch();
    appendStatusLine(tr("Working directory: %1").arg(QDir::toNativeSeparators(workingDirectory)));

    mProcess->start();

    if (!mProcess->waitForStarted(3000))
    {
        QMessageBox::critical(this, tr("Error starting executable"), mProcess->errorString());
        mStopButton->setEnabled(false);
        return false;
    }

    emit runningChanged(true, resolveDisplayAddress(config.localAddress), config.port);
    return true;
}


bool Launcher::ServerDialog::isRunning() const
{
    return mProcess != nullptr && mProcess->state() != QProcess::NotRunning;
}


bool Launcher::ServerDialog::isServerReachable(int timeoutMs) const
{
    const ServerConfig config = readServerConfig();
    bool portOk = false;
    const quint16 port = config.port.toUShort(&portOk);
    if (!portOk || port == 0)
        return false;

    QString probeAddress = config.localAddress;
    if (probeAddress.isEmpty() || probeAddress == QLatin1String("0.0.0.0"))
        probeAddress = QStringLiteral("127.0.0.1");

    QTcpSocket socket;
    socket.connectToHost(probeAddress, port);
    const bool connected = socket.waitForConnected(timeoutMs);
    if (connected)
        socket.disconnectFromHost();
    return connected;
}

QString Launcher::ServerDialog::displayAddress() const
{
    const ServerConfig config = readServerConfig();
    return resolveDisplayAddress(config.localAddress);
}

QString Launcher::ServerDialog::configuredPort() const
{
    return readServerConfig().port;
}

bool Launcher::ServerDialog::setConfiguredPassword(const QString& password, QString* errorMessage)
{
    if (password.contains('\n') || password.contains('\r') || password.contains(QChar(0))) {
        if(errorMessage)*errorMessage=tr("The password must be on a single line.");
        return false;
    }
    if(!preparePortableServer(errorMessage))return false;
    return setIniValue(readServerConfig().configPath,QStringLiteral("General"),QStringLiteral("password"),password,errorMessage);
}

bool Launcher::ServerDialog::setConfiguredHostname(const QString& hostname, QString* errorMessage)
{
    const QString value = hostname.trimmed();
    if (value.isEmpty() || value.size() > 80 || value.contains('\n') || value.contains('\r') || value.contains(QChar(0)))
    {
        if (errorMessage)
            *errorMessage = tr("Server name must contain from 1 to 80 characters and fit on one line.");
        return false;
    }

    if (!preparePortableServer(errorMessage))
        return false;

    return setIniValue(readServerConfig().configPath, QStringLiteral("General"),
        QStringLiteral("hostname"), value, errorMessage);
}

bool Launcher::ServerDialog::setConfiguredPort(const QString& port, QString* errorMessage)
{
    bool portOk = false;
    const uint parsedPort = port.trimmed().toUInt(&portOk);
    if (!portOk || parsedPort == 0 || parsedPort > 65535)
    {
        if (errorMessage)
            *errorMessage = tr("Port must be a number from 1 to 65535.");
        return false;
    }

    QString preparationError;
    if (!preparePortableServer(&preparationError))
    {
        if (errorMessage)
            *errorMessage = preparationError;
        return false;
    }

    return setIniValue(readServerConfig().configPath, QStringLiteral("General"),
        QStringLiteral("port"), QString::number(parsedPort), errorMessage);
}

bool Launcher::ServerDialog::setConfiguredLocalAddress(const QString& address, QString* errorMessage)
{
    const QString value = address.trimmed().isEmpty() ? QStringLiteral("0.0.0.0") : address.trimmed();
    QHostAddress parsed;
    if (!parsed.setAddress(value) || parsed.protocol() != QAbstractSocket::IPv4Protocol)
    {
        if (errorMessage)
            *errorMessage = tr("Server bind address must be a valid IPv4 address.");
        return false;
    }

    QString preparationError;
    if (!preparePortableServer(&preparationError))
    {
        if (errorMessage)
            *errorMessage = preparationError;
        return false;
    }

    mCachedDisplayAddress.clear();
    mCachedDisplayAddressAtMs = 0;
    return setIniValue(readServerConfig().configPath, QStringLiteral("General"),
        QStringLiteral("localAddress"), value, errorMessage);
}

QString Launcher::ServerDialog::localConnectAddress() const
{
    const QString bindAddress = readServerConfig().localAddress.trimmed();
    if (bindAddress.isEmpty() || bindAddress == QLatin1String("0.0.0.0"))
        return QStringLiteral("127.0.0.1");
    return bindAddress;
}

QString Launcher::ServerDialog::requiredDataFilesPath() const
{
    return QDir(QDir(serverRuntimeBasePath()).filePath(QStringLiteral("server/data")))
        .filePath(QStringLiteral("requiredDataFiles.json"));
}

bool Launcher::ServerDialog::autoRestartEnabled() const
{
    return mRestartCheckBox != nullptr && mRestartCheckBox->isChecked();
}

void Launcher::ServerDialog::setAutoRestartEnabled(bool enabled)
{
    if (mRestartCheckBox != nullptr)
        mRestartCheckBox->setChecked(enabled);
}

bool Launcher::ServerDialog::clearPersistentCells(QString* errorMessage) const
{
    if (isRunning())
    {
        if (errorMessage)
            *errorMessage = tr("Stop the server before clearing persistent data.");
        return false;
    }

    const QString cellPath = QDir(QDir(serverRuntimeBasePath()).filePath(QStringLiteral("server/data")))
        .filePath(QStringLiteral("cell"));
    return clearDirectoryContents(cellPath, errorMessage);
}

bool Launcher::ServerDialog::resetPersistentServerData(QString* errorMessage) const
{
    if (isRunning())
    {
        if (errorMessage)
            *errorMessage = tr("Stop the server before clearing persistent data.");
        return false;
    }

    const QDir dataDir(QDir(serverRuntimeBasePath()).filePath(QStringLiteral("server/data")));
    const QStringList gameplayDirectories = QStringList()
        << QStringLiteral("player") << QStringLiteral("cell") << QStringLiteral("world")
        << QStringLiteral("map") << QStringLiteral("custom") << QStringLiteral("recordstore");

    for (const QString& directory : gameplayDirectories)
    {
        const QString preservedFile = directory == QLatin1String("custom")
            ? QStringLiteral("arenaFurnitureReference.json") : QString();
        if (!clearDirectoryContents(dataDir.filePath(directory), errorMessage, preservedFile))
            return false;
    }

    // SQLite is optional, but when enabled it is persistent gameplay data too.
    const QString databasePath = dataDir.filePath(QStringLiteral("database.db"));
    if (QFileInfo::exists(databasePath) && !QFile::remove(databasePath))
    {
        if (errorMessage)
            *errorMessage = tr("Could not remove server data: %1")
                .arg(QDir::toNativeSeparators(databasePath));
        return false;
    }

    // Intentionally preserve requiredDataFiles.json and banlist.json.
    return true;
}

void Launcher::ServerDialog::processReadyReadStandardOutput()
{
    appendRawLog(mProcess->readAllStandardOutput());
}

void Launcher::ServerDialog::processReadyReadStandardError()
{
    appendRawLog(mProcess->readAllStandardError());
}

void Launcher::ServerDialog::processFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    appendRawLog(mProcess->readAllStandardOutput());
    appendRawLog(mProcess->readAllStandardError());

    ++mRestartCounter;
    cleanupOldLogsIfNeeded();

    appendStatusLine(tr("Server stopped. Exit code: %1").arg(exitCode));
    emit runningChanged(false, displayAddress(), configuredPort());

    // Y050 reserved graceful-restart code. The schedule/save/countdown lives in
    // the dedicated server core; the launcher only reopens its tracked child.
    // Skip the old backup/restart pipeline so the 12h reset is cheap and cannot
    // collide with the server's direct self-relaunch mode.
    if (!mStopRequested && exitStatus == QProcess::NormalExit && exitCode == 42)
    {
        appendStatusLine(tr("ArenaMP scheduled restart: relaunching dedicated server..."));
        mStopButton->setEnabled(false);
        QTimer::singleShot(750, this, [this]()
        {
            startServer();
        });
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool rapidCrash = !mStopRequested
        && exitStatus == QProcess::CrashExit
        && mLastStartMs > 0
        && (nowMs - mLastStartMs) < 15000;

    if (rapidCrash)
        ++mRapidCrashCount;
    else
        mRapidCrashCount = 0;

    mRestartAfterBackup = false;
    if (!mStopRequested && mRestartCheckBox->isChecked())
    {
        if (mRapidCrashCount >= 3)
            appendStatusLine(tr("Server crashed too many times in a short period. Auto restart disabled."));
        else
            mRestartAfterBackup = true;
    }

    mCrashAfterStop = exitStatus == QProcess::CrashExit && !mStopRequested;
    mStopButton->setEnabled(false);

    if (mRestartCheckBox->isChecked())
    {
        QString backupError;
        if (startBackupArchive(&backupError))
            return;

        if (!backupError.isEmpty())
            appendStatusLine(tr("Backup failed: %1").arg(backupError));
    }
    finishServerStopSequence();
}

void Launcher::ServerDialog::processError(QProcess::ProcessError error)
{
    if (error == QProcess::Crashed)
        return;

    appendStatusLine(tr("Process error: %1").arg(mProcess->errorString()));
}

void Launcher::ServerDialog::stopServer()
{
    mStopRequested = true;
    if (mProcess->state() == QProcess::NotRunning)
    {
        mStopButton->setEnabled(false);
        emit runningChanged(false, displayAddress(), configuredPort());
        return;
    }

    appendStatusLine(tr("Stopping server..."));
    mStopButton->setEnabled(false);
    mProcess->terminate();

    // Never block the GUI thread while the server shuts down. Some server
    // scripts need a moment to flush state; force-kill only after the grace
    // period and only if the same process is still running.
    QTimer::singleShot(2000, this, [this]()
    {
        if (mProcess != nullptr && mProcess->state() != QProcess::NotRunning)
        {
            appendStatusLine(tr("Server did not stop in time; forcing termination..."));
            mProcess->kill();
        }
    });
}

void Launcher::ServerDialog::refreshDecodedLog()
{
    // A different codec invalidates everything already on screen, so this is the
    // one path that still re-renders the whole retained buffer.
    mLogViewRenderedBytes = 0;
    if (mLogView != nullptr)
        mLogView->clear();
    updateLogView();
}

void Launcher::ServerDialog::appendRawLog(const QByteArray& logData)
{
    if (logData.isEmpty())
        return;

    mRawLog.append(logData);

    // Y050: keep only the recent tail. Before this cap a 40+ MB server log was
    // duplicated in mRawLog and QPlainTextEdit, and setPlainText rebuilt all of
    // it on every packet of stdout, causing the launcher CPU/RAM spike reported
    // in the server log session. One MiB is ample for interactive diagnostics.
    static const int maxRawLogBytes = 1024 * 1024;
    if (mRawLog.size() > maxRawLogBytes)
    {
        int removeBytes = mRawLog.size() - maxRawLogBytes;
        mRawLog.remove(0, removeBytes);
        const int newline = mRawLog.indexOf('\n');
        if (newline >= 0)
        {
            mRawLog.remove(0, newline + 1);
            removeBytes += newline + 1;
        }

        // The rendered cursor is an offset into mRawLog, so it has to follow the
        // trim. QPlainTextEdit drops the corresponding old blocks on its own
        // through maximumBlockCount, so no rebuild is needed here.
        mLogViewRenderedBytes = std::max(0, mLogViewRenderedBytes - removeBytes);
    }

    // Coalesce bursts: render at most ~6 times/second instead of rebuilding the
    // QTextDocument for every individual INFO line. WARN default makes this
    // almost idle in normal operation.
    if (!mLogRefreshPending)
    {
        mLogRefreshPending = true;
        QTimer::singleShot(160, this, [this]()
        {
            mLogRefreshPending = false;
            updateLogView();
        });
    }
}

void Launcher::ServerDialog::updateLogView()
{
    QTextCodec* codec = currentCodec();
    // Y052: append only the bytes that have not been rendered yet. The previous
    // setPlainText() re-decoded and rebuilt the whole 1 MiB document up to six
    // times per second, and it also destroyed the scroll position and any
    // selection, which made the log unreadable while the server was running.
    if (mLogViewRenderedBytes > mRawLog.size())
        mLogViewRenderedBytes = 0;

    // Stop at the last complete line: a chunk boundary must never fall inside a
    // line (appendPlainText always starts a new block) or inside a multi-byte
    // character, and '\n' cannot occur inside a UTF-8 or CP1251 sequence.
    const int completeEnd = mRawLog.lastIndexOf('\n') + 1;
    if (completeEnd <= mLogViewRenderedBytes)
        return;

    const QByteArray chunk = mRawLog.mid(mLogViewRenderedBytes, completeEnd - mLogViewRenderedBytes);
    mLogViewRenderedBytes = completeEnd;

    QString text = codec != nullptr ? codec->toUnicode(chunk)
                                    : QString::fromUtf8(chunk.constData(), chunk.size());
    while (text.endsWith(QLatin1Char('\n')) || text.endsWith(QLatin1Char('\r')))
        text.chop(1);
    if (text.isEmpty())
        return;

    QScrollBar* scrollBar = mLogView->verticalScrollBar();
    const int previousValue = scrollBar->value();
    const bool followTail = previousValue >= scrollBar->maximum() - 4;

    mLogView->appendPlainText(text);

    if (followTail)
        scrollBar->setValue(scrollBar->maximum());
    else
        scrollBar->setValue(std::min(previousValue, scrollBar->maximum()));
}

void Launcher::ServerDialog::clearLogView()
{
    mRawLog.clear();
    mLogViewRenderedBytes = 0;
    if (mLogView != nullptr)
        mLogView->clear();
}

QTextCodec* Launcher::ServerDialog::currentCodec() const
{
    const QString codecName = mEncodingCombo->currentData().toString();
    if (codecName == QLatin1String("System"))
        return QTextCodec::codecForLocale();

    QTextCodec* codec = QTextCodec::codecForName(codecName.toUtf8());
    return codec != nullptr ? codec : QTextCodec::codecForLocale();
}

Launcher::ServerDialog::ServerConfig Launcher::ServerDialog::readServerConfig() const
{
    ServerConfig result;
    result.hostname = QStringLiteral("ArenaMP server");
    result.localAddress = QStringLiteral("0.0.0.0");
    result.port = QStringLiteral("25565");

    const QDir serverBaseDir(serverRuntimeBasePath());
    const QDir userDir(QDir(runtimeDataBasePath()).filePath(QStringLiteral("userdata")));
    result.serverHomePath = serverBaseDir.filePath(QStringLiteral("server"));
    result.configPath = userDir.filePath(QStringLiteral("tes3mp-server.cfg"));

    QString configuredPluginHome = QStringLiteral("./server");

    // Load from lowest to highest priority. The server scripts always come
    // from the bundled server directory; userdata contains only the cfg file.
    QStringList configCandidates;
    configCandidates << serverBaseDir.filePath(QStringLiteral("tes3mp-server-default.cfg"))
                     << userDir.filePath(QStringLiteral("tes3mp-server-default.cfg"))
                     << serverBaseDir.filePath(QStringLiteral("tes3mp-server.cfg"))
                     << userDir.filePath(QStringLiteral("tes3mp-server.cfg"));

    for (const QString& configPath : configCandidates)
    {
        QFile file(configPath);
        if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        result.configPath = QFileInfo(configPath).absoluteFilePath();

        QString currentSection;
        QTextStream in(&file);
        while (!in.atEnd())
        {
            const QString rawLine = in.readLine().trimmed();
            if (rawLine.isEmpty() || rawLine.startsWith('#') || rawLine.startsWith(';'))
                continue;
            if (rawLine.startsWith('[') && rawLine.endsWith(']'))
            {
                currentSection = rawLine.mid(1, rawLine.size() - 2).trimmed();
                continue;
            }

            const int equalsPos = rawLine.indexOf('=');
            if (equalsPos < 0)
                continue;

            const QString key = rawLine.left(equalsPos).trimmed();
            const QString value = rawLine.mid(equalsPos + 1).trimmed();

            if (currentSection == QLatin1String("General"))
            {
                if (key == QLatin1String("hostname"))
                    result.hostname = value;
                else if (key == QLatin1String("password"))
                    result.password = value;
                else if (key == QLatin1String("localAddress"))
                    result.localAddress = value;
                else if (key == QLatin1String("port"))
                    result.port = value;
            }
            else if (currentSection == QLatin1String("Plugins") && key == QLatin1String("home"))
            {
                configuredPluginHome = value;
            }
        }
    }

    if (QDir::isRelativePath(configuredPluginHome))
        result.serverHomePath = serverBaseDir.absoluteFilePath(configuredPluginHome);
    else
        result.serverHomePath = QDir(configuredPluginHome).absolutePath();

    // Do not allow a legacy portable config to redirect the launcher back to
    // userdata/server. That directory is no longer an active server tree.
    const QString legacyServerPath = QDir(userDir.absolutePath()).absoluteFilePath(QStringLiteral("server"));
    if (QDir::cleanPath(result.serverHomePath) == QDir::cleanPath(legacyServerPath))
        result.serverHomePath = serverBaseDir.filePath(QStringLiteral("server"));

    result.configPath = QFileInfo(result.configPath).absoluteFilePath();
    return result;
}

bool Launcher::ServerDialog::preparePortableServer(QString* errorMessage) const
{
    const QDir serverBaseDir(serverRuntimeBasePath());
    const QString bundledServerPath = serverBaseDir.filePath(QStringLiteral("server"));
    const QString bundledCore = QDir(bundledServerPath).filePath(QStringLiteral("scripts/serverCore.lua"));
    const QString bundledConfig = QDir(bundledServerPath).filePath(QStringLiteral("scripts/config.lua"));
    const QString bundledVersion = QDir(bundledServerPath).filePath(QStringLiteral("ARENAMP_CORE_VERSION.txt"));

    if (!QFileInfo::exists(bundledCore) || !QFileInfo::exists(bundledConfig)
        || !QFileInfo::exists(bundledVersion))
    {
        if (errorMessage)
            *errorMessage = tr("The bundled ArenaMP server core is incomplete: %1")
                .arg(QDir::toNativeSeparators(bundledServerPath));
        return false;
    }

    const QString userDataPath = QDir(runtimeDataBasePath()).filePath(QStringLiteral("userdata"));
    if (!QDir().mkpath(userDataPath))
    {
        if (errorMessage)
            *errorMessage = tr("Could not create userdata directory: %1")
                .arg(QDir::toNativeSeparators(userDataPath));
        return false;
    }

    const QString userConfigPath = QDir(userDataPath).filePath(QStringLiteral("tes3mp-server.cfg"));
    if (!QFileInfo::exists(userConfigPath))
    {
        const QString defaultConfigPath = serverBaseDir.filePath(QStringLiteral("tes3mp-server-default.cfg"));
        if (!copyFileReplacing(defaultConfigPath, userConfigPath, errorMessage))
            return false;
    }

    // Keep the editable cfg in userdata, but always run the bundled server
    // tree. No files or directories are created under userdata/server.
    return setIniValue(userConfigPath, QStringLiteral("Plugins"), QStringLiteral("home"),
        QStringLiteral("./server"), errorMessage);
}

QString Launcher::ServerDialog::resolveServerExecutable() const
{
    QDir dir(applicationBasePath());
#ifdef Q_OS_WIN
    const QStringList candidates { QStringLiteral("tes3mp-server.exe") };
#else
    const QStringList candidates {
        QStringLiteral("tes3mp-server"),
        QStringLiteral("tes3mp-server.x86_64")
    };
#endif
    for (const QString& candidate : candidates)
    {
        const QString executable = dir.absoluteFilePath(candidate);
        if (QFileInfo(executable).exists() && QFileInfo(executable).isExecutable())
            return executable;
    }
    return QString();
}

QString Launcher::ServerDialog::resolveDisplayAddress(const QString& bindAddress) const
{
    if (!bindAddress.isEmpty() && bindAddress != QLatin1String("0.0.0.0"))
        return bindAddress;

    // QNetworkInterface::allInterfaces() may wake Windows network-location
    // services hosted by svchost.exe. Cache the LAN address instead of
    // enumerating every time the launcher refreshes its main page.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!mCachedDisplayAddress.isEmpty() && now - mCachedDisplayAddressAtMs < 30000)
        return mCachedDisplayAddress;

    QString bestAddress;
    int bestScore = -1;

    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : interfaces)
    {
        if (!(iface.flags() & QNetworkInterface::IsUp) || !(iface.flags() & QNetworkInterface::IsRunning))
            continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack)
            continue;

        const QString interfaceName = (iface.humanReadableName() + QLatin1Char(' ') + iface.name()).toLower();
        const bool looksVirtual = interfaceName.contains(QLatin1String("virtual"))
            || interfaceName.contains(QLatin1String("vmware"))
            || interfaceName.contains(QLatin1String("hyper-v"))
            || interfaceName.contains(QLatin1String("vethernet"))
            || interfaceName.contains(QLatin1String("wsl"))
            || interfaceName.contains(QLatin1String("docker"))
            || interfaceName.contains(QLatin1String("loopback"));

        const QList<QNetworkAddressEntry> addressEntries = iface.addressEntries();
        for (const QNetworkAddressEntry& entry : addressEntries)
        {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;

            const QString address = entry.ip().toString();
            if (address.startsWith(QLatin1String("169.254.")) || address == QLatin1String("0.0.0.0"))
                continue;

            int score = looksVirtual ? 0 : 100;
            if (address.startsWith(QLatin1String("192.168.")))
                score += 30;
            else if (address.startsWith(QLatin1String("10.")))
                score += 20;
            else if (address.startsWith(QLatin1String("172.")))
            {
                const int secondOctet = address.section(QLatin1Char('.'), 1, 1).toInt();
                if (secondOctet >= 16 && secondOctet <= 31)
                    score += 10;
            }

            if (score > bestScore)
            {
                bestScore = score;
                bestAddress = address;
            }
        }
    }

    mCachedDisplayAddress = bestAddress.isEmpty() ? QStringLiteral("127.0.0.1") : bestAddress;
    mCachedDisplayAddressAtMs = now;
    return mCachedDisplayAddress;
}

QString Launcher::ServerDialog::applicationBasePath() const
{
    return QCoreApplication::applicationDirPath();
}

QString Launcher::ServerDialog::serverRuntimeBasePath() const
{
#ifdef Q_OS_MAC
    return QDir(applicationBasePath()).absoluteFilePath(QStringLiteral("../Resources"));
#else
    return applicationBasePath();
#endif
}

QString Launcher::ServerDialog::runtimeDataBasePath() const
{
#ifdef Q_OS_MAC
    const QString writablePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!writablePath.isEmpty())
        return writablePath;
#endif
    return applicationBasePath();
}

QString Launcher::ServerDialog::backupDirectoryPath() const
{
    QDir dir(runtimeDataBasePath());
    const QString backupPath = dir.absoluteFilePath(QStringLiteral("Backup"));
    QDir().mkpath(backupPath);
    return backupPath;
}

QString Launcher::ServerDialog::makeBackupArchivePath() const
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy_MM_dd_HH_mm_ss"));
    return QDir(backupDirectoryPath()).absoluteFilePath(QStringLiteral("archive_") + timestamp + QStringLiteral(".zip"));
}

bool Launcher::ServerDialog::startBackupArchive(QString* errorMessage)
{
    if (mBackupInProgress || mBackupProcess->state() != QProcess::NotRunning)
    {
        if (errorMessage)
            *errorMessage = tr("A previous backup is still running.");
        return false;
    }

    const ServerConfig config = readServerConfig();
    const QFileInfo sourceInfo(config.serverHomePath);
    if (!sourceInfo.exists() || !sourceInfo.isDir())
    {
        if (errorMessage)
            *errorMessage = tr("Server directory was not found: %1").arg(QDir::toNativeSeparators(config.serverHomePath));
        return false;
    }

    mPendingBackupPath = makeBackupArchivePath();
    mBackupProcess->setWorkingDirectory(applicationBasePath());
    mBackupProcess->setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_WIN
    QStringList arguments;
    arguments << QStringLiteral("-NoProfile")
              << QStringLiteral("-ExecutionPolicy") << QStringLiteral("Bypass")
              << QStringLiteral("-Command")
              << QStringLiteral("Compress-Archive -Path '%1\\*' -DestinationPath '%2' -Force")
                    .arg(QDir::toNativeSeparators(config.serverHomePath).replace("'", "''"),
                         QDir::toNativeSeparators(mPendingBackupPath).replace("'", "''"));
    mBackupProcess->setProgram(QStringLiteral("powershell.exe"));
    mBackupProcess->setArguments(arguments);
#else
    QStringList arguments;
    arguments << QStringLiteral("-r") << mPendingBackupPath << QDir(config.serverHomePath).dirName();
    mBackupProcess->setWorkingDirectory(QFileInfo(config.serverHomePath).absolutePath());
    mBackupProcess->setProgram(QStringLiteral("zip"));
    mBackupProcess->setArguments(arguments);
#endif

    mBackupInProgress = true;
    appendStatusLine(tr("Creating backup in background: %1")
        .arg(QDir::toNativeSeparators(mPendingBackupPath)));
    mBackupProcess->start();
    return true;
}

void Launcher::ServerDialog::backupProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (!mBackupInProgress)
        return;

    const QString standardError = QString::fromLocal8Bit(mBackupProcess->readAllStandardError()).trimmed();
    if (exitStatus == QProcess::NormalExit && exitCode == 0)
        appendStatusLine(tr("Backup created: %1").arg(QDir::toNativeSeparators(mPendingBackupPath)));
    else
        appendStatusLine(tr("Backup failed: %1").arg(standardError.isEmpty()
            ? tr("archiver exit code %1").arg(exitCode) : standardError));

    mBackupInProgress = false;
    mPendingBackupPath.clear();
    finishServerStopSequence();
}

void Launcher::ServerDialog::backupProcessError(QProcess::ProcessError error)
{
    if (!mBackupInProgress || error == QProcess::Crashed)
        return;

    appendStatusLine(tr("Backup failed: %1").arg(mBackupProcess->errorString()));
    mBackupInProgress = false;
    mPendingBackupPath.clear();
    finishServerStopSequence();
}

void Launcher::ServerDialog::finishServerStopSequence()
{
    if (mCrashAfterStop)
        appendStatusLine(tr("The server process crashed."));
    mCrashAfterStop = false;

    if (mRestartAfterBackup)
    {
        mRestartAfterBackup = false;
        appendStatusLine(tr("Restarting server..."));
        startServer();
        return;
    }

    mStopButton->setEnabled(false);
}

void Launcher::ServerDialog::cleanupOldLogsIfNeeded()
{
    if (mRestartCounter < 5)
        return;

    mRestartCounter = 0;

    QDir logDir(QDir(runtimeDataBasePath()).absoluteFilePath(QStringLiteral("userdata")));
    if (!logDir.exists())
        return;

    const QStringList logFiles = logDir.entryList(
        QStringList() << QStringLiteral("tes3mp-client-*.log")
                      << QStringLiteral("tes3mp-server-*.log"),
        QDir::Files);
    for (const QString& fileName : logFiles)
        logDir.remove(fileName);

    if (!logFiles.isEmpty())
        appendStatusLine(tr("Old timestamped log files were cleaned up."));
}

void Launcher::ServerDialog::appendStatusLine(const QString& text)
{
    const QString line = QStringLiteral("[")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        + QStringLiteral("] ") + text + QStringLiteral("\n");
    // Encode with the codec the view will decode with, otherwise launcher status
    // lines show up as mojibake whenever a non-UTF-8 log encoding is selected.
    QTextCodec* codec = currentCodec();
    mRawLog.append(codec != nullptr ? codec->fromUnicode(line) : line.toUtf8());
    updateLogView();
}
