#include <QSaveFile>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <components/misc/arenaglassicons.hpp>
#include <components/misc/arenaglasswindow.hpp>
#include "updatecontroller.hpp"
#include "maindialog.hpp"

#include <components/version/version.hpp>
#include <components/misc/helpviewer.hpp>
#include <components/config/buildmanifest.hpp>
#include <components/config/contentorder.hpp>

#include <QDate>
#include <QDebug>
#include <QCoreApplication>
#include <QMessageBox>
#include <QProcess>
#include <QFontDatabase>
#include <QInputDialog>
#include <QFileDialog>
#include <QCloseEvent>
#include <QTextCodec>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QAbstractItemView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <boost/crc.hpp>
#include <QDesktopServices>
#include <QUrl>
#include <QTimer>
#include <QElapsedTimer>
#include <QFrame>
#include <QHBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QRegularExpression>
#include <QStyle>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QLinearGradient>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif




#include "buildsetupdialog.hpp"
#include "desktopshortcut.hpp"
#include "playpage.hpp"
#include "chatpage.hpp"
#include "graphicspage.hpp"
#include <QTextStream>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include "datafilespage.hpp"
#include "settingspage.hpp"
#include "advancedpage.hpp"
#include "serverdialog.hpp"

using namespace Process;

void cfgError(const QString& title, const QString& msg) {
    QMessageBox msgBox;
    msgBox.setWindowTitle(title);
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setText(msg);
    msgBox.exec();
}

namespace
{
    // U020 showcase layout: two cards plus a status column need a little more
    // room than the former 960x660 window. 1080x720 still fits 1366x768
    // laptop screens together with the Windows taskbar.
    constexpr int sLauncherWidth = 1080;
    constexpr int sLauncherHeight = 720;
    constexpr int sNavigationItems = 5;

#ifdef Q_OS_WIN
    bool isTes3mpProcessRunningSystemWide()
    {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return false;

        PROCESSENTRY32W entry;
        ZeroMemory(&entry, sizeof(entry));
        entry.dwSize = sizeof(entry);
        bool found = false;
        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                if (QString::fromWCharArray(entry.szExeFile).compare(
                        QStringLiteral("tes3mp.exe"), Qt::CaseInsensitive) == 0)
                {
                    found = true;
                    break;
                }
            }
            while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return found;
    }
#endif

    void repolishWidget(QWidget* widget)
    {
        if (widget == nullptr)
            return;
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }

    QString formatSessionTime(qint64 milliseconds)
    {
        const qint64 totalSeconds = qMax<qint64>(0, milliseconds / 1000);
        const qint64 hours = totalSeconds / 3600;
        const qint64 minutes = (totalSeconds / 60) % 60;
        const qint64 seconds = totalSeconds % 60;
        if (hours > 0)
            return QStringLiteral("%1:%2:%3").arg(hours)
                .arg(minutes, 2, 10, QLatin1Char('0')).arg(seconds, 2, 10, QLatin1Char('0'));
        return QStringLiteral("%1:%2").arg(minutes, 2, 10, QLatin1Char('0')).arg(seconds, 2, 10, QLatin1Char('0'));
    }

    bool containsGameContent(const QDir& dir)
    {
        if (!dir.exists())
            return false;

        const QStringList files = dir.entryList(
            QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
        for (const QString& fileName : files)
        {
            if (fileName.endsWith(QLatin1String(".esm"), Qt::CaseInsensitive)
                || fileName.endsWith(QLatin1String(".esp"), Qt::CaseInsensitive)
                || fileName.endsWith(QLatin1String(".omwgame"), Qt::CaseInsensitive)
                || fileName.endsWith(QLatin1String(".omwaddon"), Qt::CaseInsensitive))
                return true;
        }
        return false;
    }

    QString resolveDataFilesDirectory(const QString& selectedPath)
    {
        if (selectedPath.trimmed().isEmpty())
            return QString();

        const QString cleanPath = QDir::cleanPath(selectedPath);
        const QDir selectedDir(cleanPath);
        if (!selectedDir.exists())
            return QString();
        if (containsGameContent(selectedDir))
            return cleanPath;

        const QStringList childDirectories = selectedDir.entryList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
        for (const QString& childName : childDirectories)
        {
            if (childName.compare(QLatin1String("Data Files"), Qt::CaseInsensitive) != 0)
                continue;

            const QString childPath = QDir::cleanPath(selectedDir.filePath(childName));
            if (containsGameContent(QDir(childPath)))
                return childPath;
        }
        return QString();
    }

    QString formatChangelogInline(QString text)
    {
        text = text.toHtmlEscaped();
        text.replace(QRegularExpression(QStringLiteral("\\*\\*(.+?)\\*\\*")),
            QStringLiteral("<b>\\1</b>"));
        text.replace(QRegularExpression(QStringLiteral("`([^`]+)`")),
            QStringLiteral("<code>\\1</code>"));
        return text;
    }

    QIcon buildChatLauncherIcon()
    {
        const int size = 64;
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        auto makeBubblePath = [](const QRectF& rect, const QPointF& tailTip,
                                 const QPointF& tailBase1, const QPointF& tailBase2)
        {
            QPainterPath path;
            path.addRoundedRect(rect, 12.0, 12.0);
            QPainterPath tail;
            tail.moveTo(tailBase1);
            tail.lineTo(tailTip);
            tail.lineTo(tailBase2);
            tail.closeSubpath();
            path.addPath(tail);
            return path;
        };

        const QRectF backRect(12.0, 12.0, 34.0, 24.0);
        const QPainterPath backPath = makeBubblePath(backRect, QPointF(20.0, 42.0),
            QPointF(22.0, 35.0), QPointF(28.0, 35.0));
        QLinearGradient backGradient(backRect.topLeft(), backRect.bottomRight());
        backGradient.setColorAt(0.0, QColor(255, 232, 176, 96));
        backGradient.setColorAt(1.0, QColor(120, 82, 20, 150));
        painter.setPen(QPen(QColor(255, 223, 154, 200), 1.6));
        painter.setBrush(backGradient);
        painter.drawPath(backPath);

        const QRectF frontRect(18.0, 20.0, 34.0, 24.0);
        const QPainterPath frontPath = makeBubblePath(frontRect, QPointF(42.0, 50.0),
            QPointF(36.0, 43.0), QPointF(44.0, 43.0));
        QLinearGradient frontGradient(frontRect.topLeft(), frontRect.bottomLeft());
        frontGradient.setColorAt(0.0, QColor(255, 249, 229, 230));
        frontGradient.setColorAt(0.45, QColor(244, 212, 127, 224));
        frontGradient.setColorAt(1.0, QColor(173, 116, 38, 236));
        painter.setPen(QPen(QColor(255, 245, 214, 235), 1.8));
        painter.setBrush(frontGradient);
        painter.drawPath(frontPath);

        painter.setPen(QPen(QColor(90, 46, 8, 230), 2.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(26.0, 29.0), QPointF(44.0, 29.0));
        painter.drawLine(QPointF(26.0, 35.0), QPointF(40.0, 35.0));

        painter.setPen(QPen(QColor(255, 255, 255, 92), 1.2));
        painter.drawArc(QRectF(20.0, 22.0, 22.0, 12.0), 25 * 16, 130 * 16);

        return QIcon(pixmap);
    }

    QString changelogMarkdownToHtml(const QString& markdown)
    {
        QString html = QStringLiteral(
            "<html><head><style>"
            "body{font-family:'Segoe UI',sans-serif;font-size:14px;line-height:1.45;margin:18px;color:#e8e8e8;background:#252525;}"
            "h1,h2,h3{color:#ffffff;margin-top:18px;margin-bottom:8px;}"
            "h2{font-size:23px;border-bottom:1px solid #555;padding-bottom:5px;}"
            "h3{font-size:17px;color:#f0d58a;}"
            "ul{margin-top:4px;margin-bottom:10px;} li{margin-bottom:5px;}"
            "code{font-family:Consolas,monospace;background:#333;padding:1px 4px;border-radius:2px;color:#f4f4f4;}"
            "a{color:#7fb7ff;} .paragraph{margin:6px 0 10px 0;}"
            "</style></head><body>");

        bool listOpen = false;
        const QStringList lines = markdown.split(QLatin1Char('\n'));
        for (QString line : lines)
        {
            if (line.endsWith(QLatin1Char('\r')))
                line.chop(1);
            const QString trimmed = line.trimmed();

            if (trimmed.startsWith(QStringLiteral("- ")))
            {
                if (!listOpen)
                {
                    html += QStringLiteral("<ul>");
                    listOpen = true;
                }
                html += QStringLiteral("<li>") + formatChangelogInline(trimmed.mid(2)) + QStringLiteral("</li>");
                continue;
            }

            if (listOpen)
            {
                html += QStringLiteral("</ul>");
                listOpen = false;
            }

            if (trimmed.startsWith(QStringLiteral("### ")))
                html += QStringLiteral("<h3>") + formatChangelogInline(trimmed.mid(4)) + QStringLiteral("</h3>");
            else if (trimmed.startsWith(QStringLiteral("## ")))
                html += QStringLiteral("<h2>") + formatChangelogInline(trimmed.mid(3)) + QStringLiteral("</h2>");
            else if (trimmed.startsWith(QStringLiteral("# ")))
                html += QStringLiteral("<h1>") + formatChangelogInline(trimmed.mid(2)) + QStringLiteral("</h1>");
            else if (trimmed == QStringLiteral("---"))
                html += QStringLiteral("<hr>");
            else if (!trimmed.isEmpty())
                html += QStringLiteral("<div class='paragraph'>") + formatChangelogInline(trimmed) + QStringLiteral("</div>");
        }

        if (listOpen)
            html += QStringLiteral("</ul>");
        html += QStringLiteral("</body></html>");
        return html;
    }
}

Launcher::MainDialog::MainDialog(QWidget *parent)
    : QMainWindow(parent)
    , mPlayPage(nullptr)
    , mGraphicsPage(nullptr)
    , mChatPage(nullptr)
    , mDataFilesPage(nullptr)
    , mSettingsPage(nullptr)
    , mAdvancedPage(nullptr)
    , mGameInvoker(nullptr)
    , mServerDialog(nullptr)
    , mPlayButton(nullptr)
    , mFooterStatusDot(nullptr)
    , mFooterStatusDetail(nullptr)
    , mSessionLabel(nullptr)
    , mSessionTimer(nullptr)
    , mGameProcessTimer(nullptr)
    , mBuildManifestLoaded(false)
    , mBuildName(QStringLiteral("ArenaMP"))
    , mBuildServerAddress(QStringLiteral("127.0.0.1"))
    , mBuildServerPort(QStringLiteral("25565"))
    , mBuildServerAddressSpecified(false)
    , mBuildServerPortSpecified(false)
    , mBuildComplete(false)
    , mUpdateAvailable(false)
    , mUpdateCheckRunning(false)
    , mGameLaunchLocked(false)
    , mGameSettings (mCfgMgr)
{
    setupUi(this);
    setFixedSize(sLauncherWidth, sLauncherHeight);

    mGameInvoker = new ProcessInvoker();
    // U034: keep the launcher (and ArenaLink chat) alive while tes3mp runs.
    // A tracked QProcess lets us hand lobby voice back after the game exits,
    // while forwarded channels avoid a hidden launcher filling a pipe buffer.
    mGameInvoker->getProcess()->setProcessChannelMode(QProcess::ForwardedChannels);
    connect(mGameInvoker->getProcess(), QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        this, [this](int, QProcess::ExitStatus)
        {
            if (mChatPage != nullptr)
                mChatPage->setGameRunning(false);
            refreshGameClientState();
            showNormal();
            raise();
            activateWindow();
        });
    mGameProcessTimer = new QTimer(this);
    mGameProcessTimer->setInterval(750);
    connect(mGameProcessTimer, &QTimer::timeout, this, &MainDialog::refreshGameClientState);
    mGameProcessTimer->start();
    mServerDialog = new ServerDialog(this);
    // The server console is embedded directly inside the Play page.
    // Do not wrap it in a second glass window/title bar, otherwise the
    // inner traffic-light controls and extra top margin waste space.

    iconWidget->setViewMode(QListView::IconMode);
    iconWidget->setWrapping(false);
    iconWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // Just to be sure
    iconWidget->setIconSize(QSize(27, 27));
    // Five equal toolbar cells across the full window width.
    const int navigationCell = (sLauncherWidth - 20 - 18 - 12 - 30) / sNavigationItems;
    iconWidget->setGridSize(QSize(navigationCell, 60));
    iconWidget->setWordWrap(false);
    iconWidget->setTextElideMode(Qt::ElideNone);
    iconWidget->setMovement(QListView::Static);

    iconWidget->setSpacing(3);
    iconWidget->setUniformItemSizes(true);
    iconWidget->setCurrentRow(0);
    iconWidget->setFlow(QListView::LeftToRight);

    // U020 footer: status dot + two-line server state, session time and the
    // global actions in a fixed, predictable order (the platform-dependent
    // QDialogButtonBox ordering is no longer used).
    buttonBox->hide();
    ArenaUi::HeroButton *playButton = new ArenaUi::HeroButton(footerBar);
    playButton->setText(tr("Play"));
    playButton->setCompact(true);
    playButton->setPulse(true);
    QPushButton *changelogButton = new QPushButton(QStringLiteral("Changelog"), footerBar);
    QPushButton *serverButton = new QPushButton(tr("Run Server"), footerBar);
    QPushButton *helpButton = new QPushButton(tr("Help"), footerBar);
    changelogButton->setToolTip(tr("Open the ArenaMP changelog"));
    mPlayButton = playButton;
    helpButton->setProperty("arenaQuiet", true);
    changelogButton->setProperty("arenaQuiet", true);
    serverButton->setProperty("arenaQuiet", true);
    playButton->setIcon(ArenaUi::glassIcon(QStringLiteral("play-dark")));
    helpButton->setIcon(ArenaUi::glassIcon(QStringLiteral("help")));
    changelogButton->setIcon(ArenaUi::glassIcon(QStringLiteral("changelog")));
    serverButton->setIcon(ArenaUi::glassIcon(QStringLiteral("server")));
    playButton->setMinimumWidth(168);
    changelogButton->setMinimumWidth(112);
    serverButton->setMinimumWidth(150);
    helpButton->setMinimumWidth(104);
    horizontalLayout->addWidget(playButton);
    for (QPushButton* button : { changelogButton, serverButton, helpButton })
    {
        button->setProperty("arenaFooterButton", true);
        button->setCursor(Qt::PointingHandCursor);
        horizontalLayout->addWidget(button);
    }

    horizontalLayout->removeWidget(versionLabel);
    mFooterStatusDot = new QLabel(footerBar);
    mFooterStatusDot->setObjectName(QStringLiteral("footerStatusDot"));
    mFooterStatusDot->setFixedSize(12, 12);
    mFooterStatusDot->setProperty("arenaStatusDot", true);
    mFooterStatusDot->setProperty("arenaStatus", QStringLiteral("ready"));
    versionLabel->setProperty("arenaStatus", QStringLiteral("offline"));
    mFooterStatusDetail = new QLabel(footerBar);
    mFooterStatusDetail->setObjectName(QStringLiteral("footerStatusDetail"));
    mFooterStatusDetail->setProperty("arenaMuted", true);
    mFooterStatusDetail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QVBoxLayout* footerStatusText = new QVBoxLayout();
    footerStatusText->setContentsMargins(0, 0, 0, 0);
    footerStatusText->setSpacing(0);
    footerStatusText->addStretch(1);
    footerStatusText->addWidget(versionLabel);
    footerStatusText->addWidget(mFooterStatusDetail);
    footerStatusText->addStretch(1);
    QFrame* footerDivider = new QFrame(footerBar);
    footerDivider->setProperty("arenaVDivider", true);
    footerDivider->setFixedSize(1, 28);
    mSessionLabel = new QLabel(footerBar);
    mSessionLabel->setObjectName(QStringLiteral("footerSessionLabel"));
    mSessionLabel->setProperty("arenaMuted", true);
    mSessionLabel->setToolTip(tr("Uptime of the local server started from this launcher"));
    mSessionLabel->setText(tr("Session time: %1").arg(formatSessionTime(0)));
    horizontalLayout->insertWidget(0, mFooterStatusDot, 0, Qt::AlignVCenter);
    horizontalLayout->insertSpacing(1, 2);
    horizontalLayout->insertLayout(2, footerStatusText);
    horizontalLayout->insertSpacing(3, 12);
    horizontalLayout->insertWidget(4, footerDivider, 0, Qt::AlignVCenter);
    horizontalLayout->insertSpacing(5, 12);
    horizontalLayout->insertWidget(6, mSessionLabel, 0, Qt::AlignVCenter);

    mSessionTimer = new QTimer(this);
    mSessionTimer->setInterval(1000);
    connect(mSessionTimer, SIGNAL(timeout()), this, SLOT(updateSessionTime()));

    connect(playButton, SIGNAL(clicked()), this, SLOT(play()));
    connect(serverButton, SIGNAL(clicked()), this, SLOT(runServer()));
    connect(changelogButton, SIGNAL(clicked()), this, SLOT(showChangelog()));
    connect(helpButton, SIGNAL(clicked()), this, SLOT(help()));

    // Remove what's this? button
    setWindowFlags(this->windowFlags() & ~Qt::WindowContextHelpButtonHint);

    createIcons();
}

Launcher::MainDialog::~MainDialog()
{
    delete mGameInvoker;
}

void Launcher::MainDialog::createIcons()
{
    if (!QIcon::hasThemeIcon("document-new"))
        QIcon::setThemeName("tango");

    QListWidgetItem *playButton = new QListWidgetItem(iconWidget);
    playButton->setSizeHint(QSize((sLauncherWidth - 20 - 18 - 12 - 30) / sNavigationItems - 4, 58));
    playButton->setIcon(ArenaUi::glassIcon(QStringLiteral("play")));
    playButton->setText(tr("Play"));
    playButton->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    playButton->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    QListWidgetItem *dataFilesButton = new QListWidgetItem(iconWidget);
    dataFilesButton->setSizeHint(QSize((sLauncherWidth - 20 - 18 - 12 - 30) / sNavigationItems - 4, 58));
    dataFilesButton->setIcon(ArenaUi::glassIcon(QStringLiteral("browse")));
    dataFilesButton->setText(tr("Data Files"));
    dataFilesButton->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    dataFilesButton->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    QListWidgetItem *graphicsButton = new QListWidgetItem(iconWidget);
    graphicsButton->setSizeHint(QSize((sLauncherWidth - 20 - 18 - 12 - 30) / sNavigationItems - 4, 58));
    graphicsButton->setIcon(ArenaUi::glassIcon(QStringLiteral("graphics")));
    graphicsButton->setText(tr("Graphics"));
    graphicsButton->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom | Qt::AlignAbsolute);
    graphicsButton->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    QListWidgetItem *chatButton = new QListWidgetItem(iconWidget);
    chatButton->setSizeHint(QSize((sLauncherWidth - 20 - 18 - 12 - 30) / sNavigationItems - 4, 58));
    chatButton->setIcon(buildChatLauncherIcon());
    chatButton->setText(tr("Chat"));
    chatButton->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    chatButton->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    // U022: four sections. The old OpenMW "Settings" page (Morrowind.ini
    // importer and wizard shortcuts) is gone; the compact engine options that
    // used to live under "Advanced" are the Settings page now.
    QListWidgetItem *settingsButton = new QListWidgetItem(iconWidget);
    settingsButton->setSizeHint(QSize((sLauncherWidth - 20 - 18 - 12 - 30) / sNavigationItems - 4, 58));
    settingsButton->setIcon(ArenaUi::glassIcon(QStringLiteral("settings")));
    settingsButton->setText(tr("Settings"));
    settingsButton->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    settingsButton->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    connect(iconWidget,
            SIGNAL(currentItemChanged(QListWidgetItem*,QListWidgetItem*)),
            this, SLOT(changePage(QListWidgetItem*,QListWidgetItem*)));

}

void Launcher::MainDialog::createPages()
{
    // Avoid creating the widgets twice
    if (pagesWidget->count() != 0)
        return;

    mPlayPage = new PlayPage(this);
    mPlayPage->setBuildName(mBuildName);
    mDataFilesPage = new DataFilesPage(mCfgMgr, mGameSettings, mLauncherSettings, this);
    mGraphicsPage = new GraphicsPage(mLauncherSettings, this);
    mSettingsPage = new SettingsPage(mCfgMgr, mGameSettings, mLauncherSettings, this);
    mAdvancedPage = new AdvancedPage(mGameSettings, this);
    mChatPage = new ChatPage(mLauncherSettings, this);
    mPlayPage->setServerConsoleWidget(mServerDialog);
    connect(mGraphicsPage, &GraphicsPage::hardwareInfoChanged, this, [this]()
    {
        if (mPlayPage != nullptr && mGraphicsPage != nullptr)
            mPlayPage->setHardwareInfo(mGraphicsPage->hardwareGpuName(),
                mGraphicsPage->hardwareGpuDetail(), mGraphicsPage->hardwareLogicalThreads());
    });

    auto readLauncherBool = [this](const QString& key, const QString& defaultValue) -> bool
    {
        if (mLauncherSettings.getSettings().contains(key))
            return mLauncherSettings.value(key).compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
        return defaultValue.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
    };

    const QString localServerDefault = mBuildManifestLoaded && mBuildServerAddressSpecified
        ? QStringLiteral("false") : QStringLiteral("true");
    const bool autoStartServer = readLauncherBool(QStringLiteral("General/Server/autoStart"), localServerDefault);
    const bool autoRestartServer = readLauncherBool(QStringLiteral("General/Server/autoRestart"), localServerDefault);

    mPlayPage->setAutoStartServer(autoStartServer);
    mPlayPage->setAutoRestartServer(autoRestartServer);
    mPlayPage->setHostBindAddress(mLauncherSettings.value(
        QStringLiteral("General/Server/bindAddress"), QStringLiteral("0.0.0.0")));

    mServerDialog->setAutoRestartEnabled(autoRestartServer);

    {
        QString addr = mBuildManifestLoaded ? mBuildServerAddress : QStringLiteral("localhost");
        QString port = mBuildManifestLoaded ? mBuildServerPort : QStringLiteral("25565");

        if (!mBuildManifestLoaded)
        {
            const QString cfgPath = QString::fromUtf8(mCfgMgr.getUserConfigPath().string().c_str())
                + QStringLiteral("/tes3mp-client-default.cfg");
            QFile cfgFile(cfgPath);
            if (cfgFile.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                QTextStream in(&cfgFile);
                while (!in.atEnd())
                {
                    const QString line = in.readLine().trimmed();
                    if (line.startsWith(QLatin1String("destinationAddress")))
                        addr = line.section(QLatin1Char('='), 1).trimmed();
                    else if (line.startsWith(QLatin1String("port")) && !line.startsWith(QLatin1String("password")))
                        port = line.section(QLatin1Char('='), 1).trimmed();
                }
            }
        }

        if (autoStartServer)
        {
            // The editable address is the endpoint advertised/shared with other
            // players. Do not overwrite it with a LAN adapter merely because
            // Host mode is enabled. The local host client uses a separate
            // loopback/bind endpoint when launching.
            port = mServerDialog->configuredPort();
        }

        mPlayPage->setServerAddress(addr);
        mPlayPage->setServerPort(port);
        if (mChatPage != nullptr)
            mChatPage->setServerEndpoint(addr, port.toUShort());

        const bool managedServer = mServerDialog->isRunning();
        const bool reachableServer = managedServer
            || (autoStartServer && mServerDialog->isServerReachable(120));
        mPlayPage->setServerRunning(reachableServer, addr, port, managedServer);
        if (reachableServer)
            updateFooterServerStatus(true, addr, port);
    }

    // Add the pages to the stacked widget, in the same order as the icons.
    // mSettingsPage stays alive (its load/save still owns launcher.cfg values)
    // but is no longer reachable: its content was Morrowind.ini import and
    // wizard shortcuts, both obsolete now that setup lives in the launcher.
    pagesWidget->addWidget(mPlayPage);
    pagesWidget->addWidget(mDataFilesPage);
    pagesWidget->addWidget(mGraphicsPage);
    pagesWidget->addWidget(mChatPage);
    pagesWidget->addWidget(mAdvancedPage);
    mSettingsPage->hide();

    applyBuildManifestRestrictions();

    // Select the first page
    iconWidget->setCurrentItem(iconWidget->item(0), QItemSelectionModel::Select);

    connect(mPlayPage, SIGNAL(playButtonClicked()), this, SLOT(play()));
    connect(mPlayPage, SIGNAL(serverButtonClicked()), this, SLOT(runServer()));
    connect(mPlayPage, SIGNAL(stopServerButtonClicked()), this, SLOT(stopServer()));
    connect(mPlayPage, SIGNAL(autoStartServerChanged(bool)), this, SLOT(autoStartServerChanged(bool)));
    connect(mPlayPage, SIGNAL(autoRestartServerChanged(bool)), this, SLOT(autoRestartServerChanged(bool)));
    connect(mPlayPage, SIGNAL(updateHashesRequested()), this, SLOT(updateServerDataFileHashes()));
    connect(mPlayPage, SIGNAL(clearServerCellsRequested()), this, SLOT(clearServerCells()));
    connect(mPlayPage, SIGNAL(resetServerDataRequested()), this, SLOT(resetServerData()));
    connect(mPlayPage, SIGNAL(changeBuildRequested()), this, SLOT(changeBuild()));

    // U022: the removed Settings page carried two ArenaMP client options.
    // Move them onto the Play page instead of losing them.
    if (mSettingsPage != nullptr && mPlayPage != nullptr)
    {
        const QStringList wanted {
            tr("Connect to vanilla-build server"), QStringLiteral("Connect to vanilla-build server"),
            tr("Hide chat messages"), QStringLiteral("Hide chat messages")
        };
        for (QCheckBox* box : mSettingsPage->findChildren<QCheckBox*>())
        {
            if (!wanted.contains(box->text()))
                continue;
            mPlayPage->addLaunchOption(box);
        }
    }

    // U023: Data Files keeps the actual plug-in manager only. Grass/groundcover
    // plug-ins are recognized by name and connected automatically, while the
    // legacy Content List profile strip is redundant because build.ini already
    // describes exactly one build.
    if (mDataFilesPage != nullptr)
    {
        // U023: keep the actual plug-in manager (contentSelectorWidget) but hide
        // the obsolete Content List profile strip with New/Clone/Delete icons.
        // build.ini already represents one selected build, so this second profile
        // layer only duplicates UI without adding useful control.
        if (QGroupBox* profileGroup = mDataFilesPage->findChild<QGroupBox*>(QStringLiteral("profileGroupBox")))
            profileGroup->hide();

        for (QCheckBox* box : mDataFilesPage->findChildren<QCheckBox*>())
        {
            const bool groundcover = box->objectName().contains(QLatin1String("groundcover"), Qt::CaseInsensitive)
                || box->text().contains(QLatin1String("groundcover"), Qt::CaseInsensitive)
                || box->text().contains(QLatin1String("grass"), Qt::CaseInsensitive);
            if (!groundcover)
                continue;
            if (!box->isChecked())
                box->setChecked(true);
            box->hide();
        }

        for (QComboBox* combo : mDataFilesPage->findChildren<QComboBox*>())
        {
            if (!combo->objectName().contains(QLatin1String("profile"), Qt::CaseInsensitive))
                continue;
            QWidget* container = combo->parentWidget();
            while (container != nullptr && container != mDataFilesPage
                && qobject_cast<QGroupBox*>(container) == nullptr)
                container = container->parentWidget();
            // Never hide a container that also holds the content list itself.
            if (container != nullptr && container != mDataFilesPage
                && container->findChildren<QAbstractItemView*>().isEmpty())
                container->hide();
            else
                combo->hide();
        }
    }
    connect(mServerDialog, SIGNAL(runningChanged(bool,QString,QString)),
            this, SLOT(serverRunningChanged(bool,QString,QString)));
    if(auto* password=mPlayPage->findChild<QLineEdit*>("arenaLocalServerPassword")) {
        password->setText(mServerDialog->configuredPassword());
        connect(mServerDialog,&ServerDialog::runningChanged,password,[password](bool running,const QString&,const QString&){password->setEnabled(!running);});
    }
    // Optional publisher follows the managed server; a failed master never blocks gameplay.
    auto* publisher = new QProcess(this);
    publisher->setProcessChannelMode(QProcess::MergedChannels);
    connect(publisher,&QProcess::readyReadStandardOutput,publisher,[this,publisher](){
        const QString status=QString::fromUtf8(publisher->readAllStandardOutput()).trimmed().right(400);
        if(auto* label=mPlayPage->findChild<QLabel*>("arenaMasterPublishStatus")){label->setText(status);label->setToolTip(status);}
    });
    connect(publisher,&QProcess::errorOccurred,this,[this,publisher](QProcess::ProcessError){
        if(auto* label=mPlayPage->findChild<QLabel*>("arenaMasterPublishStatus"))label->setText(tr("Publishing: ")+publisher->errorString());
    });
    auto* publishTimer = new QTimer(this);
    publishTimer->setInterval(3000);
    connect(publishTimer,&QTimer::timeout,this,[this,publisher](){
        const QString path=QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("arena-master-owner.json");
        QFile input(path);QJsonObject owner;
        if(input.open(QIODevice::ReadOnly))owner=QJsonDocument::fromJson(input.readAll()).object();input.close();

        // Keep fields edited in the launcher synchronized with the file that the
        // standalone publisher rereads every heartbeat. Previously website was
        // saved only by editingFinished(), so a link entered immediately before
        // starting the server could be omitted from the master payload.
        if(auto* mode=mPlayPage->findChild<QLineEdit*>("gameModeEdit"))
            owner["game_mode"]=mode->text().trimmed();
        if(auto* website=mPlayPage->findChild<QLineEdit*>("arenaLocalServerWebsite")) {
            const QString value=website->text().trimmed();
            const QUrl url(value);
            if(value.isEmpty() || (value.size()<=2048 && url.isValid()
                && (url.scheme()==QStringLiteral("https") || url.scheme()==QStringLiteral("http"))
                && !url.host().isEmpty() && url.userInfo().isEmpty()))
                owner["website"]=value;
        }
        if(mServerDialog->isRunning()) {
            owner["native_config"]=mServerDialog->masterNativeConfigPath();
            owner["server_home"]=mServerDialog->masterServerHomePath();
        }

        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile output(path);
        if(!output.open(QIODevice::WriteOnly))return;
        output.setPermissions(QFile::ReadOwner|QFile::WriteOwner);
        const QByteArray ownerData=QJsonDocument(owner).toJson();
        if(output.write(ownerData)!=ownerData.size() || !output.commit())return;

        const bool enabled=owner.value("enabled").toBool(true)&&mServerDialog->isRunning();
        if(!enabled){
            if(publisher->state()!=QProcess::NotRunning)publisher->kill();
            if(auto* label=mPlayPage->findChild<QLabel*>("arenaMasterPublishStatus"))
                label->setText(owner.value("enabled").toBool(true)?tr("Publishing enabled. Start the server."):tr("Publishing disabled"));
            return;
        }
        if(publisher->state()!=QProcess::NotRunning)return;
        QString executable=QDir(QCoreApplication::applicationDirPath()).filePath("arena-server-agent");
#ifdef Q_OS_WIN
        executable+=".exe";
#endif
        publisher->start(executable,{"--config",path,"--server-root",QCoreApplication::applicationDirPath()});
    });
    publishTimer->start();
    connect(QCoreApplication::instance(),&QCoreApplication::aboutToQuit,publisher,[publisher](){if(publisher->state()!=QProcess::NotRunning){publisher->kill();publisher->waitForFinished(1000);}});
    // Central pre-launch hook: Run server, Play+Host and automatic relaunches
    // must all verify the same manifest *before* tes3mp-server is spawned.
    mServerDialog->setPreStartCheck([this](QString* error)
    {
        // H039: on the first managed-server start, ask the owner for a server
        // name instead of silently publishing every fresh install as
        // "ArenaMP server". The confirmation flag lives next to the master
        // publication settings, while the actual hostname is written to the
        // native TES3MP cfg so LAN/direct clients and the master list agree.
        const QString ownerPath = QDir(QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation)).filePath("arena-master-owner.json");
        QFile ownerInput(ownerPath);
        QJsonObject owner;
        if (ownerInput.open(QIODevice::ReadOnly))
            owner = QJsonDocument::fromJson(ownerInput.readAll()).object();
        ownerInput.close();

        const QString configuredName = mServerDialog->configuredHostname().trimmed();
        const bool customConfigName = !configuredName.isEmpty()
            && configuredName.compare(QStringLiteral("ArenaMP server"), Qt::CaseInsensitive) != 0;
        bool nameConfirmed = owner.value("server_name_configured").toBool(false);
        QString finalName = configuredName;

        if (!nameConfirmed && !customConfigName)
        {
            QString suggestedName = mBuildName.trimmed();
            if (suggestedName.isEmpty() || suggestedName.compare(QStringLiteral("ArenaMP"), Qt::CaseInsensitive) == 0)
                suggestedName = tr("My ArenaMP server");

            bool accepted = false;
            finalName = QInputDialog::getText(this, tr("Server name"),
                tr("Enter the name that players will see in the server list:"),
                QLineEdit::Normal, suggestedName, &accepted).trimmed();
            if (!accepted)
            {
                if (error)
                    *error = tr("Server start cancelled. Choose a server name to continue.");
                return false;
            }
            if (!mServerDialog->setConfiguredHostname(finalName, error))
                return false;
            nameConfirmed = true;
        }
        else if (!nameConfirmed && customConfigName)
        {
            // Respect an existing hand-edited hostname and do not ask again.
            finalName = configuredName;
            nameConfirmed = true;
        }

        if (nameConfirmed)
        {
            owner["server_name_configured"] = true;
            owner["name"] = finalName;
            QDir().mkpath(QFileInfo(ownerPath).absolutePath());
            QSaveFile ownerOutput(ownerPath);
            if (!ownerOutput.open(QIODevice::WriteOnly))
            {
                if (error)
                    *error = tr("Could not save the server name.");
                return false;
            }
            ownerOutput.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
            const QByteArray ownerData = QJsonDocument(owner).toJson();
            if (ownerOutput.write(ownerData) != ownerData.size() || !ownerOutput.commit())
            {
                if (error)
                    *error = tr("Could not save the server name.");
                return false;
            }
        }

        if(auto* password=mPlayPage->findChild<QLineEdit*>("arenaLocalServerPassword"))
            if(!mServerDialog->setConfiguredPassword(password->text(),error))return false;
        return ensureServerDataFileHashes(error);
    });
    connect(mServerDialog, SIGNAL(autoRestartChanged(bool)),
            this, SLOT(autoRestartServerChanged(bool)));

    // Using Qt::QueuedConnection because signal is emitted in a subthread and slot is in the main thread
    connect(mDataFilesPage, SIGNAL(signalLoadedCellsChanged(QStringList)), mAdvancedPage, SLOT(slotLoadedCellsChanged(QStringList)), Qt::QueuedConnection);

}

Launcher::FirstRunDialogResult Launcher::MainDialog::showFirstRunDialog()
{
    if (!setupLauncherSettings())
        return FirstRunDialogResultFailure;

    // U021: the setup wizard is part of the launcher now. On a fresh install
    // the player picks the build folder in one dialog; an existing build.ini
    // is used as is and a new build gets one generated from the chosen order.
    const bool firstRun = mLauncherSettings.value(
        QStringLiteral("General/firstrun"), QStringLiteral("true")) == QLatin1String("true");
    if (firstRun && !runBuildSetup(QString()))
        return FirstRunDialogResultFailure;

    if (!setup() || !setupGameData())
        return FirstRunDialogResultFailure;

    if ((firstRun || !mPendingDesktopShortcut.isEmpty()) && !writeSettings())
        return FirstRunDialogResultFailure;
    return FirstRunDialogResultContinue;
}

void Launcher::MainDialog::applyPendingBuildPath()
{
    if (mPendingSetupDataPath.isEmpty())
        return;

    const QString path = QDir::cleanPath(mPendingSetupDataPath);
    if (!QFileInfo(path).isDir())
        return;

    // A language chosen in the build setup (new build, or an old build.ini
    // without a language field) decides the text encoding. A manifest that
    // declares its own language still wins: loadBuildManifest() runs after.
    if (!mPendingSetupLanguage.isEmpty())
    {
        mLauncherSettings.remove(QStringLiteral("Settings/language"));
        mLauncherSettings.setValue(QStringLiteral("Settings/language"), mPendingSetupLanguage);
        mGameSettings.setValue(QStringLiteral("encoding"),
            BuildSetupDialog::encodingForLanguage(mPendingSetupLanguage));
    }

    // One launcher owns exactly one build: replace the user-level data= entry
    // instead of stacking several Data Files folders on top of each other.
    if (!mBuildDataPath.isEmpty() && mBuildDataPath != path)
        mGameSettings.removeDataDir(mBuildDataPath);
    mGameSettings.remove(QStringLiteral("data"));
    mGameSettings.removeDataDir(path);
    mGameSettings.addDataDir(path);
    mGameSettings.setMultiValue(QStringLiteral("data"), path);
}

bool Launcher::MainDialog::runBuildSetup(const QString& initialPath)
{
    BuildSetupDialog dialog(this);
    if (!initialPath.isEmpty())
        dialog.setInitialPath(initialPath);
    if (dialog.exec() != QDialog::Accepted)
        return false;

    const BuildSetupDialog::Result result = dialog.result();
    if (result.dataPath.isEmpty())
        return false;

    if (!result.manifestExists)
    {
        // A new build description. Existing manifests are never rewritten
        // here: they are the authoritative, user-owned build definition.
        Config::BuildManifest manifest;
        manifest.buildName = result.buildName;
        manifest.dataPath = Config::BuildManifest::portableDataPath(result.manifestPath, result.dataPath);
        manifest.language = result.language;
        manifest.languageSpecified = true;
        manifest.contentFiles = result.content;
        manifest.groundcoverFiles = result.groundcover;
        manifest.archives = result.archives;

        QString error;
        if (!manifest.write(result.manifestPath, &error))
        {
            QMessageBox::warning(this, tr("Could not create build.ini"), error);
            return false;
        }
    }

    mPendingSetupDataPath = result.dataPath;
    mPendingSetupLanguage = result.applyLanguage ? result.language : QString();
    mLauncherSettings.remove(QStringLiteral("General/Build/name"));
    mLauncherSettings.setValue(QStringLiteral("General/Build/name"),
        result.buildName.trimmed().isEmpty() ? QStringLiteral("ArenaMP") : result.buildName.trimmed());
    mLauncherSettings.remove(QStringLiteral("General/firstrun"));
    mLauncherSettings.setValue(QStringLiteral("General/firstrun"), QStringLiteral("false"));
    // U023: selecting any build changes the active settings.cfg target. Re-apply
    // the currently selected Auto/manual graphics profile after the switch so an
    // existing build cannot inherit stale values from a different setup.
    const QString pendingKey = QStringLiteral("General/Graphics/initialQualityPresetPending");
    mLauncherSettings.remove(pendingKey);
    mLauncherSettings.setValue(pendingKey, QStringLiteral("true"));
    mPendingDesktopShortcut = result.buildName.trimmed().isEmpty() ? QStringLiteral("ArenaMP") : result.buildName.trimmed();
    return true;
}

void Launcher::MainDialog::changeBuild()
{
    if (mServerDialog != nullptr && mServerDialog->isRunning())
    {
        QMessageBox::warning(this, tr("Local server is running"),
            tr("Stop the local server before switching to another build."));
        return;
    }

    // Persist the current build before replacing it, then reload everything
    // from the newly selected folder.
    if (!writeSettings())
        return;
    if (!runBuildSetup(mBuildDataPath))
        return;
    if (!reloadSettings())
        return;

    // U023: reloadSettings() rebinds Settings::Manager to the selected build's
    // settings.cfg. Re-apply the persisted Auto/manual quality profile now so
    // switching an existing build works even though launcher.cfg was reloaded.
    if (mGraphicsPage != nullptr)
        mGraphicsPage->applyCurrentQualityPreset();

    writeSettings();
    applyBuildManifestRestrictions();
    QTimer::singleShot(0, this, SLOT(checkForUpdates()));
}

void Launcher::MainDialog::setVersionLabel()
{
    updateFooterServerStatus(false);
}

void Launcher::MainDialog::updateFooterServerStatus(bool running, const QString& address, const QString& port)
{
    if (versionLabel == nullptr)
        return;

    versionLabel->setProperty("arenaStatus", running ? QStringLiteral("online") : QStringLiteral("offline"));
    versionLabel->setText(running ? tr("Server online") : tr("Server stopped"));
    if (mFooterStatusDetail != nullptr)
        mFooterStatusDetail->setText(running && !address.isEmpty()
            ? QStringLiteral("%1:%2").arg(address, port) : tr("Ready to launch"));
    if (mFooterStatusDot != nullptr)
    {
        mFooterStatusDot->setProperty("arenaStatus", running ? QStringLiteral("online") : QStringLiteral("ready"));
        repolishWidget(mFooterStatusDot);
    }
    // Dynamic properties do not automatically repolish an existing widget.
    repolishWidget(versionLabel);

    // Session time follows the local server lifetime.
    if (running && (mSessionTimer == nullptr || !mSessionTimer->isActive()))
    {
        mSessionClock.start();
        if (mSessionTimer != nullptr)
            mSessionTimer->start();
    }
    else if (!running)
    {
        if (mSessionTimer != nullptr)
            mSessionTimer->stop();
        mSessionClock.invalidate();
    }
    updateSessionTime();
}

void Launcher::MainDialog::updateSessionTime()
{
    if (mSessionLabel == nullptr)
        return;
    const qint64 elapsed = mSessionClock.isValid() ? mSessionClock.elapsed() : 0;
    mSessionLabel->setText(tr("Session time: %1").arg(formatSessionTime(elapsed)));
}

bool Launcher::MainDialog::setup()
{
    if (!setupGameSettings())
        return false;

    applyPendingBuildPath();
    loadBuildManifest();
    setVersionLabel();

    // An existing build.ini is authoritative. Its exact content list was
    // already copied into the named launcher profile by loadBuildManifest().
    // Re-synchronising from merged openmw.cfg here could select stale plugins
    // that are installed but are not present in build.ini.
    if (!mBuildManifestLoaded)
        mLauncherSettings.setContentList(mGameSettings);

    if (!setupGraphicsSettings())
        return false;

    // Now create the pages as they need the settings
    createPages();

    // Call this so we can exit on SDL errors before mainwindow is shown
    if (!mGraphicsPage->loadSettings())
        return false;

    loadSettings();

    return true;
}

bool Launcher::MainDialog::reloadSettings()
{
    if (!setupLauncherSettings())
        return false;

    if (!setupGameSettings())
        return false;

    applyPendingBuildPath();
    loadBuildManifest();
    applyBuildManifestRestrictions();
    if (!mBuildManifestLoaded)
        mLauncherSettings.setContentList(mGameSettings);

    if (!setupGraphicsSettings())
        return false;

    if (!mSettingsPage->loadSettings())
        return false;

    if (!mDataFilesPage->loadSettings())
        return false;

    if (!mGraphicsPage->loadSettings())
        return false;

    if (!mAdvancedPage->loadSettings())
        return false;

    // Refresh top-level launcher widgets as well, so a build selected in the
    // Wizard (new build.ini, new build name, new endpoint, new host-mode
    // defaults) is immediately reflected without restarting the launcher.
    loadSettings();
    applyBuildManifestRestrictions();

    return true;
}

void Launcher::MainDialog::changePage(QListWidgetItem *current, QListWidgetItem *previous)
{
    if (!current)
        current = previous;

    int currentIndex = iconWidget->row(current);
    pagesWidget->setCurrentIndex(currentIndex);
    if (pagesWidget->currentWidget() == mChatPage && mChatPage != nullptr)
    {
        // Opening chat must use the current Play selection even before a game
        // has been launched. A managed local host uses its local endpoint.
        if (mPlayPage != nullptr)
        {
            const bool alternate = mPlayPage->alternativeServer();
            QString host = alternate ? mPlayPage->alternativeAddress() : mPlayPage->serverAddress();
            QString port = alternate ? mPlayPage->alternativePort() : mPlayPage->serverPort();
            if (mPlayPage->autoStartServer() && mServerDialog != nullptr && mServerDialog->isRunning())
            {
                host = mServerDialog->localConnectAddress();
                port = mServerDialog->configuredPort();
            }
            mChatPage->setServerEndpoint(host, port.toUShort());
        }
        mChatPage->refreshLoginFromGameSettings();
    }
    // The Play page draws its own cards and status column, so the shared
    // glass panel behind the stacked pages is disabled only there.
    const bool bare = pagesWidget->currentWidget() == mPlayPage;
    if (pagesWidget->property("arenaBare").toBool() != bare)
    {
        pagesWidget->setProperty("arenaBare", bare);
        repolishWidget(pagesWidget);
    }
    mSettingsPage->resetProgressBar();
}

bool Launcher::MainDialog::setupLauncherSettings()
{
    mLauncherSettings.clear();

    mLauncherSettings.setMultiValueEnabled(true);

    QString userPath = QString::fromUtf8(mCfgMgr.getUserConfigPath().string().c_str());
    QDir userDir(userPath);

    QStringList paths;
    paths.append(QString(Config::LauncherSettings::sLauncherConfigFileName));
    paths.append(userDir.filePath(QString(Config::LauncherSettings::sLauncherConfigFileName)));

    for (const QString &path : paths)
    {
        qDebug() << "Loading config file:" << path.toUtf8().constData();
        QFile file(path);
        if (file.exists()) {
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                cfgError(tr("Error opening OpenMW configuration file"),
                         tr("<br><b>Could not open %0 for reading</b><br><br> \
                             Please make sure you have the right permissions \
                             and try again.<br>").arg(file.fileName()));
                return false;
            }
            QTextStream stream(&file);
            stream.setCodec(QTextCodec::codecForName("UTF-8"));

            mLauncherSettings.readFile(stream);
        }
        file.close();
    }

    return true;
}

bool Launcher::MainDialog::setupGameSettings()
{
    mGameSettings.clear();

    QString localPath = QString::fromUtf8(mCfgMgr.getLocalPath().string().c_str());
    QString userPath = QString::fromUtf8(mCfgMgr.getUserConfigPath().string().c_str());
    QString globalPath = QString::fromUtf8(mCfgMgr.getGlobalPath().string().c_str());
    QDir localDir(localPath);
    QDir userDir(userPath);
    QDir globalDir(globalPath);

    // Load the user config file first, separately
    // So we can write it properly, uncontaminated
    QString path = userDir.filePath(QLatin1String("openmw.cfg"));
    QFile file(path);

    qDebug() << "Loading config file:" << path.toUtf8().constData();

    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            cfgError(tr("Error opening OpenMW configuration file"),
                     tr("<br><b>Could not open %0 for reading</b><br><br> \
                         Please make sure you have the right permissions \
                         and try again.<br>").arg(file.fileName()));
            return false;
        }
        QTextStream stream(&file);
        stream.setCodec(QTextCodec::codecForName("UTF-8"));

        mGameSettings.readUserFile(stream);
        file.close();
    }

    // Now the rest - priority: user > local > global
    QStringList paths;
    paths.append(globalDir.filePath(QString("openmw.cfg")));
    paths.append(localDir.filePath(QString("openmw.cfg")));
    paths.append(userDir.filePath(QString("openmw.cfg")));

    for (const QString &path2 : paths)
    {
        qDebug() << "Loading config file:" << path2.toUtf8().constData();

        file.setFileName(path2);
        if (file.exists()) {
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                cfgError(tr("Error opening OpenMW configuration file"),
                         tr("<br><b>Could not open %0 for reading</b><br><br> \
                             Please make sure you have the right permissions \
                             and try again.<br>").arg(file.fileName()));
                return false;
            }
            QTextStream stream(&file);
            stream.setCodec(QTextCodec::codecForName("UTF-8"));

            mGameSettings.readFile(stream);
            file.close();
        }
    }

    // Normalize legacy data= entries before loadBuildManifest() runs. Older
    // Wizard builds could store the Morrowind root instead of Data Files.
    const QStringList configuredDataDirs = mGameSettings.getDataDirs();
    for (const QString& configuredPath : configuredDataDirs)
    {
        const QString resolvedPath = resolveDataFilesDirectory(configuredPath);
        if (!resolvedPath.isEmpty()
            && !mGameSettings.getDataDirs().contains(resolvedPath, Qt::CaseInsensitive))
        {
            mGameSettings.setMultiValue(QLatin1String("data"), resolvedPath);
            mGameSettings.addDataDir(resolvedPath);
        }
    }

    return true;
}

bool Launcher::MainDialog::setupGameData()
{
    QStringList candidates = mGameSettings.getDataDirs();
    if (!mBuildDataPath.isEmpty())
        candidates.prepend(mBuildDataPath);

    const QString localPath = QString::fromUtf8(mCfgMgr.getLocalPath().string().c_str());
    if (!localPath.isEmpty())
    {
        candidates.append(QDir(localPath).filePath(QLatin1String("Data Files")));
        candidates.append(localPath);
    }

    QStringList dataDirs;
    for (const QString& candidate : candidates)
    {
        const QString resolvedPath = resolveDataFilesDirectory(candidate);
        if (resolvedPath.isEmpty()
            || dataDirs.contains(resolvedPath, Qt::CaseInsensitive))
            continue;

        dataDirs.append(resolvedPath);
        if (!mGameSettings.getDataDirs().contains(resolvedPath, Qt::CaseInsensitive))
        {
            mGameSettings.setMultiValue(QLatin1String("data"), resolvedPath);
            mGameSettings.addDataDir(resolvedPath);
        }
    }

    if (dataDirs.isEmpty())
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("Error detecting Morrowind installation"));
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setStandardButtons(QMessageBox::NoButton);
        msgBox.setText(tr("<br><b>Could not find the Data Files location</b><br><br> \
                                   The directory containing the data files was not found."));

        QAbstractButton *setupButton =
                msgBox.addButton(tr("Choose the build folder..."), QMessageBox::ActionRole);
        QAbstractButton *skipButton =
                msgBox.addButton(tr("Skip"), QMessageBox::RejectRole);

        Q_UNUSED(skipButton); // Suppress compiler unused warning

        msgBox.exec();

        if (msgBox.clickedButton() == setupButton && runBuildSetup(QString()))
        {
            applyPendingBuildPath();
            loadBuildManifest();
        }
    }

    return true;
}

QString Launcher::MainDialog::primaryDataDirectory() const
{
    const QString dataLocal = mGameSettings.getDataLocal();
    if (!dataLocal.isEmpty() && QFileInfo(dataLocal).isDir())
        return QDir::cleanPath(dataLocal);

    const QStringList dataDirs = mGameSettings.getDataDirs();
    for (auto it = dataDirs.crbegin(); it != dataDirs.crend(); ++it)
    {
        if (!Config::BuildManifest::findForDataDir(*it).isEmpty())
            return QDir::cleanPath(*it);
    }

    const QStringList filters = {
        QStringLiteral("*.esm"), QStringLiteral("*.esp"),
        QStringLiteral("*.omwgame"), QStringLiteral("*.omwaddon")
    };
    for (auto it = dataDirs.crbegin(); it != dataDirs.crend(); ++it)
    {
        QDir dir(*it);
        if (dir.exists() && !dir.entryList(filters, QDir::Files | QDir::Readable).isEmpty())
            return QDir::cleanPath(*it);
    }

    return dataDirs.isEmpty() ? QString() : QDir::cleanPath(dataDirs.last());
}

bool Launcher::MainDialog::loadBuildManifest()
{
    mBuildManifestLoaded = false;
    mBuildManifestPath.clear();
    mBuildName = mLauncherSettings.value(QStringLiteral("General/Build/name"), QStringLiteral("ArenaMP"));
    mBuildDataPath = primaryDataDirectory();
    mBuildServerAddress = QStringLiteral("127.0.0.1");
    mBuildServerPort = QStringLiteral("25565");
    mBuildServerAddressSpecified = false;
    mBuildServerPortSpecified = false;
    mBuildComplete = false;

    if (mBuildDataPath.isEmpty())
        return false;

    const QString manifestPath = Config::BuildManifest::findForDataDir(mBuildDataPath);
    if (manifestPath.isEmpty())
    {
        // No saved build order exists yet. Apply the recommended order once,
        // then the first Launcher save creates build.ini and all later starts
        // preserve the user's exact order from that file.
        const QStringList orderedContent = Config::applyCanonicalContentOrder(
            mGameSettings.getContentList(), QDir(mBuildDataPath));
        if (!orderedContent.isEmpty())
        {
            mGameSettings.setContentList(orderedContent);
            QString profileName = mLauncherSettings.getCurrentContentListName();
            if (profileName.isEmpty())
                profileName = QStringLiteral("Default");
            QStringList profileFiles = orderedContent;
            const QStringList groundcover = mGameSettings.getGroundcoverList();
            profileFiles.append(groundcover);
            mLauncherSettings.setContentList(profileName, profileFiles, groundcover,
                !groundcover.isEmpty());
            mLauncherSettings.setCurrentContentListName(profileName);
        }
        return false;
    }

    Config::BuildManifest manifest;
    QString error;
    if (!manifest.read(manifestPath, &error))
    {
        qWarning() << "Could not read build manifest" << manifestPath << error;
        return false;
    }

    const QString resolvedDataPath = manifest.resolvedDataPath(manifestPath);
    if (QFileInfo(resolvedDataPath).isDir())
    {
        mBuildDataPath = QDir::cleanPath(resolvedDataPath);
        if (!mGameSettings.getDataDirs().contains(mBuildDataPath))
        {
            mGameSettings.setMultiValue(QStringLiteral("data"), mBuildDataPath);
            mGameSettings.addDataDir(mBuildDataPath);
        }
    }

    const QString effectiveBuildName = manifest.buildName.trimmed().isEmpty()
        ? QStringLiteral("ArenaMP") : manifest.buildName.trimmed();
    // A present build.ini always owns the enabled plug-in list, including an
    // intentionally empty one. Never merge it with launcher.cfg/openmw.cfg or
    // auto-enable other plug-ins merely because they exist in Data Files.
    const QStringList orderedContent = manifest.contentFiles;
    mGameSettings.setContentList(orderedContent);
    mGameSettings.setGroundcoverList(manifest.groundcoverFiles);

    QStringList profileFiles = orderedContent;
    profileFiles.append(manifest.groundcoverFiles);
    mLauncherSettings.setContentList(effectiveBuildName, profileFiles,
        manifest.groundcoverFiles, !manifest.groundcoverFiles.isEmpty());
    mLauncherSettings.setCurrentContentListName(effectiveBuildName);

    if (!manifest.archives.isEmpty())
    {
        mGameSettings.remove(QStringLiteral("fallback-archive"));
        for (const QString& archive : manifest.archives)
            mGameSettings.setMultiValue(QStringLiteral("fallback-archive"), archive);
    }

    // Do not manufacture English when an older manifest has no language field.
    // When the field is present, build.ini is authoritative and the translated
    // launcher UI must not replace its canonical value.
    if (manifest.languageSpecified)
    {
        const QString manifestLanguage = Config::BuildManifest::canonicalLanguage(manifest.language);
        mLauncherSettings.remove(QStringLiteral("Settings/language"));
        mLauncherSettings.setValue(QStringLiteral("Settings/language"), manifestLanguage);
        if (manifestLanguage == QLatin1String("Polish"))
            mGameSettings.setValue(QStringLiteral("encoding"), QStringLiteral("win1250"));
        else if (manifestLanguage == QLatin1String("Russian"))
            mGameSettings.setValue(QStringLiteral("encoding"), QStringLiteral("win1251"));
        else
            mGameSettings.setValue(QStringLiteral("encoding"), QStringLiteral("win1252"));
    }

    mBuildManifestLoaded = true;
    mBuildManifestPath = manifestPath;
    mBuildName = effectiveBuildName;
    mBuildServerAddress = manifest.serverAddress.trimmed().isEmpty()
        ? QStringLiteral("127.0.0.1") : manifest.serverAddress.trimmed();
    mBuildServerPort = manifest.serverPort.trimmed().isEmpty()
        ? QStringLiteral("25565") : manifest.serverPort.trimmed();
    mBuildServerAddressSpecified = manifest.serverAddressSpecified;
    mBuildServerPortSpecified = manifest.serverPortSpecified;
    mBuildComplete = manifest.complete;
    writeClientEndpoint(mBuildServerAddress, mBuildServerPort);

    if (mPlayPage != nullptr)
    {
        mPlayPage->setBuildName(mBuildName);
        mPlayPage->setServerAddress(mBuildServerAddress);
        mPlayPage->setServerPort(mBuildServerPort);
        mPlayPage->setBuildManifestComplete(mBuildComplete);
        mPlayPage->setAlternativeServer(manifest.altAddress, manifest.altPort, manifest.useAlternativeServer);
        mPlayPage->setProjectUrl(manifest.projectUrl);
        if (mBuildServerAddressSpecified)
        {
            mPlayPage->setAutoStartServer(false);
            mPlayPage->setAutoRestartServer(false);
            if (mServerDialog != nullptr)
                mServerDialog->setAutoRestartEnabled(false);
        }
    }

    QTimer::singleShot(0, this, [this, manifestPath]() { UpdateController::showResult(this, manifestPath); });
    qDebug() << "Loaded ArenaMP build manifest:" << manifestPath;
    return true;
}

bool Launcher::MainDialog::writeBuildManifest()
{
    QString dataDir = mBuildDataPath;
    if (dataDir.isEmpty() || !QFileInfo(dataDir).isDir())
        dataDir = primaryDataDirectory();
    if (dataDir.isEmpty() || !QFileInfo(dataDir).isDir())
        return true;

    // Always save the active manifest beside the selected Data Files. If an
    // older patch loaded a fallback copy beside the executable, read it as the
    // source once and migrate its values to the canonical location.
    const QString sourceManifestPath = mBuildManifestPath;
    const QString manifestPath = Config::BuildManifest::canonicalPathForDataDir(dataDir);

    Config::BuildManifest manifest;
    bool existingManifestRead = false;
    QString storedServerAddress;
    bool storedServerAddressSpecified = false;
    QString storedServerPort;
    bool storedServerPortSpecified = false;
    QString existingManifestPath = manifestPath;
    if (!sourceManifestPath.isEmpty() && QFileInfo::exists(sourceManifestPath))
        existingManifestPath = sourceManifestPath;

    if (QFileInfo::exists(existingManifestPath))
    {
        existingManifestRead = manifest.read(existingManifestPath);
        if (existingManifestRead)
        {
            storedServerAddress = manifest.serverAddress;
            storedServerAddressSpecified = manifest.serverAddressSpecified;
            storedServerPort = manifest.serverPort;
            storedServerPortSpecified = manifest.serverPortSpecified;
        }

    }

    manifest.formatVersion = 1;
    manifest.buildName = mPlayPage != nullptr ? mPlayPage->buildName() : mBuildName;
    manifest.dataPath = Config::BuildManifest::portableDataPath(manifestPath, dataDir);
    manifest.language = mLauncherSettings.value(QStringLiteral("Settings/language"), QStringLiteral("English"));
    const bool localServerModeSelected = mPlayPage != nullptr
        && mPlayPage->autoStartServer();

    if ((localServerModeSelected || mBuildComplete || (mPlayPage && mPlayPage->alternativeServer())) && existingManifestRead)
    {
        // Host mode is a launcher choice. Do not replace the distributed remote
        // endpoint in build.ini with this machine's LAN address and local port.
        manifest.serverAddress = storedServerAddress;
        manifest.serverAddressSpecified = storedServerAddressSpecified;
        manifest.serverPort = storedServerPort;
        manifest.serverPortSpecified = storedServerPortSpecified;
        manifest.vanillaServerCompatibility = false;
    }
    else if (localServerModeSelected)
    {
        // A newly generated local-host profile intentionally has no distributed
        // endpoint. This allows Host mode to stay the default until an address
        // is explicitly added to build.ini.
        manifest.serverAddress = QStringLiteral("127.0.0.1");
        manifest.serverAddressSpecified = false;
        manifest.serverPort = QStringLiteral("25565");
        manifest.serverPortSpecified = false;
        manifest.vanillaServerCompatibility = false;
    }
    else
    {
        manifest.serverAddress = mPlayPage != nullptr ? mPlayPage->serverAddress() : mBuildServerAddress;
        manifest.serverAddressSpecified = !manifest.serverAddress.trimmed().isEmpty();
        manifest.serverPort = mPlayPage != nullptr ? mPlayPage->serverPort() : mBuildServerPort;
        manifest.serverPortSpecified = !manifest.serverPort.trimmed().isEmpty();
        manifest.vanillaServerCompatibility = false;
    }
    if (mPlayPage)
    {
        manifest.useAlternativeServer = mPlayPage->alternativeServer();
        manifest.altAddress = mPlayPage->alternativeAddress();
        manifest.altPort = mPlayPage->alternativePort();
    }
    manifest.complete = mBuildComplete;
    manifest.contentFiles = mGameSettings.getContentList();
    manifest.groundcoverFiles = mGameSettings.getGroundcoverList();
    manifest.archives = Config::LauncherSettings::reverse(
        mGameSettings.values(QStringLiteral("fallback-archive")));

    QString error;
    if (!manifest.write(manifestPath, &error))
    {
        cfgError(tr("Error writing ArenaMP build manifest"),
            tr("<br><b>Could not write %1</b><br><br>%2").arg(manifestPath, error));
        return false;
    }

    mBuildManifestLoaded = true;
    mBuildManifestPath = manifestPath;
    mBuildName = manifest.buildName;
    mBuildDataPath = dataDir;
    mBuildServerAddress = manifest.serverAddress;
    mBuildServerPort = manifest.serverPort;
    mBuildServerAddressSpecified = manifest.serverAddressSpecified;
    mBuildServerPortSpecified = manifest.serverPortSpecified;
    mBuildComplete = manifest.complete;
    writeClientEndpoint(mBuildServerAddress, mBuildServerPort);
    return true;
}

void Launcher::MainDialog::applyBuildManifestRestrictions()
{
    Config::BuildManifest linkManifest;
    if (mPlayPage)
        mPlayPage->setProjectUrl(Config::BuildManifest::websiteForManifest(mBuildManifestPath));
    if (mPlayPage && !mBuildManifestPath.isEmpty() && linkManifest.read(mBuildManifestPath))
    {
        mPlayPage->setAlternativeServer(linkManifest.altAddress, linkManifest.altPort, linkManifest.useAlternativeServer);

    }

    if (mPlayPage != nullptr)
    {
        mPlayPage->setBuildManifestComplete(mBuildComplete);
    }

    // build.ini stores the current user order; keep Data Files available so
    // deliberate Launcher changes can be written back to the manifest.
    if (iconWidget != nullptr && iconWidget->count() > 1)
        iconWidget->item(1)->setHidden(false);

    if (mAdvancedPage != nullptr)
        mAdvancedPage->setGameMechanicsVisible(!mBuildComplete);

}

bool Launcher::MainDialog::isLocalServerAddress(const QString& address) const
{
    const QString normalized = address.trimmed().toLower();
    if (normalized.isEmpty() || normalized == QLatin1String("localhost")
        || normalized == QLatin1String("127.0.0.1") || normalized == QLatin1String("::1")
        || normalized == QLatin1String("0.0.0.0"))
        return true;

    return mServerDialog != nullptr
        && normalized == mServerDialog->displayAddress().trimmed().toLower();
}

void Launcher::MainDialog::writeClientEndpoint(const QString& address, const QString& port) const
{
    QDir userDir(QString::fromUtf8(mCfgMgr.getUserConfigPath().string().c_str()));
    if (!userDir.exists())
        userDir.mkpath(QStringLiteral("."));

    const auto writeEndpointFile = [&](const QString& fileName)
    {
        QFile cfgFile(userDir.filePath(fileName));
        QStringList lines;
        bool foundAddress = false;
        bool foundPort = false;

        if (cfgFile.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QTextStream input(&cfgFile);
            input.setCodec("UTF-8");
            while (!input.atEnd())
                lines.append(input.readLine());
            cfgFile.close();
        }

        for (QString& line : lines)
        {
            const QString trimmed = line.trimmed();
            if (trimmed.startsWith(QLatin1String("destinationAddress")))
            {
                line = QStringLiteral("destinationAddress = ") + address;
                foundAddress = true;
            }
            else if (trimmed.startsWith(QLatin1String("port")) && !trimmed.startsWith(QLatin1String("password")))
            {
                line = QStringLiteral("port = ") + port;
                foundPort = true;
            }
        }

        if (!foundAddress)
            lines.append(QStringLiteral("destinationAddress = ") + address);
        if (!foundPort)
            lines.append(QStringLiteral("port = ") + port);

        if (cfgFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        {
            QTextStream output(&cfgFile);
            output.setCodec("UTF-8");
            for (const QString& line : lines)
                output << line << '\n';
        }
    };

    // Keep the shipped/default client profile synchronized on every platform.
    writeEndpointFile(QStringLiteral("tes3mp-client-default.cfg"));

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    // ArenaMP Y087 Linux portable fix: tes3mp-prelaunch creates userdata/
    // tes3mp-client.cfg once and the game subsequently reads that active file.
    // Updating only *-default.cfg left an old build.ini endpoint active when a
    // wrapper/desktop launch did not preserve --connect as expected.  Mirror
    // the selected endpoint into the active Linux profile as a fallback.
    writeEndpointFile(QStringLiteral("tes3mp-client.cfg"));
#endif
}

void Launcher::MainDialog::writeClientVoiceSettings() const
{
    if (mChatPage == nullptr)
        return;

    QDir userDir(QString::fromUtf8(mCfgMgr.getUserConfigPath().string().c_str()));
    if (!userDir.exists())
        userDir.mkpath(QStringLiteral("."));

    QFile cfgFile(userDir.filePath(QStringLiteral("tes3mp-client.cfg")));
    QStringList lines;
    if (cfgFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QTextStream input(&cfgFile);
        input.setCodec("UTF-8");
        while (!input.atEnd())
            lines.append(input.readLine());
        cfgFile.close();
    }

    const QString enabledValue = mChatPage->voiceEnabled() ? QStringLiteral("true") : QStringLiteral("false");
    const QString keyValue = mChatPage->pushToTalkKey().trimmed().isEmpty()
        ? QStringLiteral("V") : mChatPage->pushToTalkKey().trimmed();
    const QString captureDevice = mChatPage->captureDevice().trimmed();

    int voiceHeader = -1;
    int nextHeader = lines.size();
    for (int i = 0; i < lines.size(); ++i)
    {
        const QString trimmed = lines.at(i).trimmed();
        if (trimmed.compare(QStringLiteral("[Voice]"), Qt::CaseInsensitive) == 0)
        {
            voiceHeader = i;
            for (int j = i + 1; j < lines.size(); ++j)
            {
                const QString candidate = lines.at(j).trimmed();
                if (candidate.startsWith(QLatin1Char('[')) && candidate.endsWith(QLatin1Char(']')))
                {
                    nextHeader = j;
                    break;
                }
            }
            break;
        }
    }

    if (voiceHeader < 0)
    {
        if (!lines.isEmpty() && !lines.last().isEmpty())
            lines.append(QString());
        voiceHeader = lines.size();
        lines.append(QStringLiteral("[Voice]"));
        lines.append(QStringLiteral("enabled = ") + enabledValue);
        lines.append(QStringLiteral("pushToTalkKey = ") + keyValue);
        lines.append(QStringLiteral("captureDevice = ") + captureDevice);
    }
    else
    {
        bool foundEnabled = false;
        bool foundKey = false;
        bool foundCaptureDevice = false;
        for (int i = voiceHeader + 1; i < nextHeader; ++i)
        {
            const QString trimmed = lines.at(i).trimmed();
            if (trimmed.startsWith(QLatin1String("enabled")))
            {
                lines[i] = QStringLiteral("enabled = ") + enabledValue;
                foundEnabled = true;
            }
            else if (trimmed.startsWith(QLatin1String("pushToTalkKey")))
            {
                lines[i] = QStringLiteral("pushToTalkKey = ") + keyValue;
                foundKey = true;
            }
            else if (trimmed.startsWith(QLatin1String("captureDevice")))
            {
                lines[i] = QStringLiteral("captureDevice = ") + captureDevice;
                foundCaptureDevice = true;
            }
        }
        int insertAt = nextHeader;
        if (!foundEnabled)
            lines.insert(insertAt++, QStringLiteral("enabled = ") + enabledValue);
        if (!foundKey)
            lines.insert(insertAt++, QStringLiteral("pushToTalkKey = ") + keyValue);
        if (!foundCaptureDevice)
            lines.insert(insertAt, QStringLiteral("captureDevice = ") + captureDevice);
    }

    if (cfgFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        QTextStream output(&cfgFile);
        output.setCodec("UTF-8");
        for (const QString& line : lines)
            output << line << '\n';
    }
}


bool Launcher::MainDialog::setupGraphicsSettings()
{
    // This method is almost a copy of OMW::Engine::loadSettings().  They should definitely
    // remain consistent, and possibly be merged into a shared component.  At the very least
    // the filenames should be in the CfgMgr component.

    // Ensure to clear previous settings in case we had already loaded settings.
    mEngineSettings.clear();
    // ArenaMP uses settings-default.cfg as the canonical preset. Prefer the
    // text file so newly added fork settings are available immediately; keep
    // defaults.bin only as a compatibility fallback for incomplete packages.
    const std::string localDefault = (mCfgMgr.getLocalPath() / "defaults.bin").string();
    const std::string globalDefault = (mCfgMgr.getGlobalPath() / "defaults.bin").string();
    const std::string localDefaultCfg = (mCfgMgr.getLocalPath() / "settings-default.cfg").string();
    const std::string globalDefaultCfg = (mCfgMgr.getGlobalPath() / "settings-default.cfg").string();
    std::string defaultPath;
    bool defaultIsTextCfg = false;

    if (boost::filesystem::exists(localDefaultCfg))
    {
        defaultPath = localDefaultCfg;
        defaultIsTextCfg = true;
    }
    else if (boost::filesystem::exists(globalDefaultCfg))
    {
        defaultPath = globalDefaultCfg;
        defaultIsTextCfg = true;
    }
    else if (boost::filesystem::exists(localDefault))
        defaultPath = localDefault;
    else if (boost::filesystem::exists(globalDefault))
        defaultPath = globalDefault;
    else {
        cfgError(tr("Error reading OpenMW configuration file"),
                 tr("<br><b>Could not find defaults.bin or settings-default.cfg</b><br><br>                      The problem may be due to an incomplete installation of OpenMW.<br>                      Reinstalling OpenMW may resolve the problem."));
        return false;
    }

    try {
        mEngineSettings.loadDefault(defaultPath, !defaultIsTextCfg);
    }
    catch (std::exception& e) {
        std::string msg = std::string("<br><b>Error reading default settings</b><br><br>") + e.what();
        cfgError(tr("Error reading OpenMW configuration file"), tr(msg.c_str()));
        return false;
    }

    // Always configure the canonical user settings path. The explicit preset
    // Apply button may need to create settings.cfg after a clean Wizard run.
    const std::string userPath = mCfgMgr.getPrimarySettingsPath().string();
    mEngineSettings.setUserSettingsPath(userPath);

    // User settings are not required to exist.
    if (!boost::filesystem::exists(userPath))
        return true;

    try {
        mEngineSettings.loadUser(userPath);
    }
    catch (std::exception& e) {
        std::string msg = std::string("<br><b>Error reading settings.cfg</b><br><br>") + e.what();
        cfgError(tr("Error reading OpenMW configuration file"), tr(msg.c_str()));
        return false;
    }

    return true;
}

void Launcher::MainDialog::loadSettings()
{
    int posX = mLauncherSettings.value(QString("General/MainWindow/posx")).toInt();
    int posY = mLauncherSettings.value(QString("General/MainWindow/posy")).toInt();

    // Keep the page layouts stable and ignore stale width/height values.
    setFixedSize(sLauncherWidth, sLauncherHeight);
    move(posX, posY);

    if (mPlayPage != nullptr && mServerDialog != nullptr)
    {
        auto readLauncherBool = [this](const QString& key, const QString& defaultValue) -> bool
        {
            if (mLauncherSettings.getSettings().contains(key))
                return mLauncherSettings.value(key).compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
            return defaultValue.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
        };

        const QString localServerDefault = mBuildManifestLoaded && mBuildServerAddressSpecified
            ? QStringLiteral("false") : QStringLiteral("true");
        const bool autoStart = readLauncherBool(QStringLiteral("General/Server/autoStart"), localServerDefault);
        const bool autoRestart = readLauncherBool(QStringLiteral("General/Server/autoRestart"), localServerDefault);
        mPlayPage->setAutoStartServer(autoStart);
        mPlayPage->setAutoRestartServer(autoRestart);
        mPlayPage->setBuildName(mBuildName);

        mServerDialog->setAutoRestartEnabled(autoRestart);
    }

    if (mChatPage != nullptr)
    {
        const QString settingsPath = QString::fromUtf8(mCfgMgr.getPrimarySettingsPath().string().c_str());
        mChatPage->loadSettings(settingsPath);
    }
}

void Launcher::MainDialog::saveSettings()
{
    QString width = QString::number(sLauncherWidth);
    QString height = QString::number(sLauncherHeight);

    mLauncherSettings.remove(QString("General/MainWindow/width"));
    mLauncherSettings.remove(QString("General/MainWindow/height"));
    mLauncherSettings.setValue(QString("General/MainWindow/width"), width);
    mLauncherSettings.setValue(QString("General/MainWindow/height"), height);

    QString posX = QString::number(this->pos().x());
    QString posY = QString::number(this->pos().y());

    mLauncherSettings.setValue(QString("General/MainWindow/posx"), posX);
    mLauncherSettings.setValue(QString("General/MainWindow/posy"), posY);

    mLauncherSettings.setValue(QString("General/firstrun"), QString("false"));

    if (mPlayPage != nullptr)
    {
        mLauncherSettings.remove(QStringLiteral("General/Server/autoStart"));
        mLauncherSettings.setValue(QStringLiteral("General/Server/autoStart"),
            mPlayPage->autoStartServer() ? QStringLiteral("true") : QStringLiteral("false"));
        mLauncherSettings.remove(QStringLiteral("General/Server/autoRestart"));
        mLauncherSettings.setValue(QStringLiteral("General/Server/autoRestart"),
            mPlayPage->autoRestartServer() ? QStringLiteral("true") : QStringLiteral("false"));
        mLauncherSettings.remove(QStringLiteral("General/Server/vanillaBuild"));
        mLauncherSettings.remove(QStringLiteral("General/Server/bindAddress"));
        mLauncherSettings.setValue(QStringLiteral("General/Server/bindAddress"),
            mPlayPage->hostBindAddress());
        mLauncherSettings.remove(QStringLiteral("General/Chat/hideHistory"));
        mLauncherSettings.setValue(QStringLiteral("General/Build/name"), mPlayPage->buildName());
    }

    if (mChatPage != nullptr)
    {
        mChatPage->saveSettings();
        writeClientVoiceSettings();
    }
}

bool Launcher::MainDialog::writeSettings()
{
    // Now write all config files
    saveSettings();
    mDataFilesPage->saveSettings();

    // Y001: GraphicsPage performs a merge-safe save: it first captures only
    // controls changed relative to the page snapshot, then reloads settings.cfg
    // and replays just those keys. This makes manual Water/Terrain/PBR/shadow
    // changes apply on Play without overwriting unrelated in-game settings.
    if (!mGraphicsPage->saveSettings())
        return false;

    mSettingsPage->saveSettings();
    if (!mPlayPage->saveServerSettings())
        return false;

    QString userPath = QString::fromUtf8(mCfgMgr.getUserConfigPath().string().c_str());
    QDir dir(userPath);

    if (!dir.exists()) {
        if (!dir.mkpath(userPath)) {
            cfgError(tr("Error creating OpenMW configuration directory"),
                     tr("<br><b>Could not create %0</b><br><br> \
                         Please make sure you have the right permissions \
                         and try again.<br>").arg(userPath));
            return false;
        }
    }

    // Game settings
    QFile file(dir.filePath(QString("openmw.cfg")));

    if (!file.open(QIODevice::ReadWrite | QIODevice::Text)) {
        // File cannot be opened or created
        cfgError(tr("Error writing OpenMW configuration file"),
                 tr("<br><b>Could not open or create %0 for writing</b><br><br> \
                     Please make sure you have the right permissions \
                     and try again.<br>").arg(file.fileName()));
        return false;
    }


    mGameSettings.writeFileWithComments(file);
    file.close();

    // Launcher settings
    file.setFileName(dir.filePath(QString(Config::LauncherSettings::sLauncherConfigFileName)));

    if (!file.open(QIODevice::ReadWrite | QIODevice::Text | QIODevice::Truncate)) {
        // File cannot be opened or created
        cfgError(tr("Error writing Launcher configuration file"),
                 tr("<br><b>Could not open or create %0 for writing</b><br><br> \
                     Please make sure you have the right permissions \
                     and try again.<br>").arg(file.fileName()));
        return false;
    }

    QTextStream stream(&file);
    stream.setDevice(&file);
    stream.setCodec(QTextCodec::codecForName("UTF-8"));

    mLauncherSettings.writeFile(stream);
    file.close();

    if (!writeBuildManifest())
        return false;

    if (!mPendingDesktopShortcut.isEmpty())
    {
        const QString name = mPendingDesktopShortcut;
        mPendingDesktopShortcut.clear();
        if (!createDesktopShortcut(name))
            QMessageBox::warning(this, tr("Desktop shortcut"),
                tr("The build is ready, but the desktop shortcut could not be created."));
    }
    return true;
}

void Launcher::MainDialog::closeEvent(QCloseEvent *event)
{
    writeSettings();
    event->accept();
}

bool Launcher::MainDialog::isGameClientRunning() const
{
    if (mGameInvoker != nullptr && mGameInvoker->getProcess() != nullptr
        && mGameInvoker->getProcess()->state() != QProcess::NotRunning)
        return true;
#ifdef Q_OS_WIN
    return isTes3mpProcessRunningSystemWide();
#else
    return false;
#endif
}

void Launcher::MainDialog::refreshGameClientState()
{
    if (mPlayButton == nullptr || mPlayPage == nullptr)
        return;

    const bool running = isGameClientRunning();
    if (running)
    {
        mGameLaunchLocked = true;
        mPlayButton->setText(tr("Game running"));
        mPlayButton->setEnabled(false);
        mPlayButton->setPulse(false);
        mPlayPage->setPlayButtonState(tr("Game running"), false);
        return;
    }

    // Only restore a button that we disabled because tes3mp was running.
    // This avoids overriding unrelated setup/update disabled states.
    if (!mGameLaunchLocked || mUpdateCheckRunning)
        return;

    mGameLaunchLocked = false;
    mPlayButton->setText(mUpdateAvailable ? tr("Update") : tr("Play"));
    mPlayButton->setIcon(ArenaUi::glassIcon(mUpdateAvailable
        ? QStringLiteral("update-dark") : QStringLiteral("play-dark")));
    mPlayButton->setEnabled(true);
    mPlayButton->setPulse(true, mUpdateAvailable);
    mPlayPage->setPlayButtonState(mUpdateAvailable ? tr("Update") : tr("Start game"), true);
}

void Launcher::MainDialog::checkForUpdates()
{
    // A reopened launcher must compare the actual installed revisions again.
    // The resume argument only identifies the return from the updater; it is
    // not proof that the requested content/engine was installed successfully.
    if (mUpdateCheckRunning)
        return;

    mUpdateAvailable = false;
    if (mPlayButton == nullptr || mPlayPage == nullptr || mBuildManifestPath.isEmpty())
        return;

    mUpdateCheckRunning = true;
    mPlayButton->setEnabled(false);
    mPlayButton->setText(tr("Checking for updates..."));
    mPlayButton->setPulse(false);
    mPlayPage->setPlayButtonState(tr("Checking for updates..."), false);
    mPlayPage->setUpdateState(true, false);

    const UpdateController::CheckResult result = UpdateController::checkAvailable(
        this, mBuildManifestPath, mBuildDataPath);

    mUpdateCheckRunning = false;
    mUpdateAvailable = result == UpdateController::CheckResult::UpdateAvailable;
    mPlayButton->setText(mUpdateAvailable ? tr("Update") : tr("Play"));
    mPlayButton->setIcon(ArenaUi::glassIcon(mUpdateAvailable ? QStringLiteral("update-dark") : QStringLiteral("play-dark")));
    mPlayButton->setEnabled(true);
    mPlayButton->setPulse(true, mUpdateAvailable);
    mPlayPage->setPlayButtonState(mUpdateAvailable ? tr("Update") : tr("Start game"), true);
    mPlayPage->setUpdateState(false, mUpdateAvailable);
    refreshGameClientState();
}

void Launcher::MainDialog::play()
{
    refreshGameClientState();
    if (isGameClientRunning())
        return;
    if (mUpdateCheckRunning)
        return;

    if (mUpdateAvailable)
    {
        if (!UpdateController::startUpdate(this, mBuildManifestPath, mBuildDataPath))
            return;

        mUpdateAvailable = false;
        mPlayButton->setEnabled(false);
        mPlayPage->setPlayButtonState(tr("Update"), false);
        hide();
        close();
        QCoreApplication::exit(0);
        return;
    }

    if(!mPlayPage->validateMasterSelection())return;
    const bool alternate = mPlayPage->alternativeServer();
    bool portOk = false;
    const int port = mPlayPage->alternativePort().toInt(&portOk);
    if (alternate && (mPlayPage->alternativeAddress().isEmpty() || !portOk || port < 1 || port > 65535))
    {
        QMessageBox::warning(this, tr("Invalid server address"), tr("Enter an address and a port from 1 to 65535."));
        return;
    }

    if (!writeSettings())
        return qApp->quit();

    if (!mGameSettings.hasMaster())
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("No game file selected"));
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setText(tr("<br><b>You do not have a game file selected.</b><br><br> "
                          "ArenaMP will not start without a game file selected.<br>"));
        msgBox.exec();
        return;
    }

    mPendingClientAddress = alternate ? mPlayPage->alternativeAddress() : mPlayPage->serverAddress();
    mPendingClientPort = alternate ? mPlayPage->alternativePort() : mPlayPage->serverPort();
    if (mChatPage != nullptr)
        mChatPage->setServerEndpoint(mPendingClientAddress, mPendingClientPort.toUShort());

    bool startedNow = false;
    const bool localServerMode = mPlayPage->autoStartServer();
    if (localServerMode)
    {
        // Host mode overrides the remote endpoint for this launch only.
        // The build.ini endpoint remains unchanged.
        mServerDialog->setAutoRestartEnabled(mPlayPage->autoRestartServer());

        if (!mServerDialog->isRunning())
        {
            QString bindError;
            if (!mServerDialog->setConfiguredLocalAddress(mPlayPage->hostBindAddress(), &bindError))
            {
                QMessageBox::warning(this, tr("Invalid server interface"), bindError);
                mPendingClientAddress.clear();
                mPendingClientPort.clear();
                return;
            }

            QString portError;
            if (!mServerDialog->setConfiguredPort(mPlayPage->serverPort(), &portError))
            {
                QMessageBox::warning(this, tr("Invalid server port"), portError);
                mPendingClientAddress.clear();
                mPendingClientPort.clear();
                return;
            }
        }

        // The host's own client must use a local endpoint. The value in the
        // Server Address field may intentionally be a public WAN address and
        // may not support NAT loopback on the user's router.
        mPendingClientAddress = mServerDialog->localConnectAddress();
        mPendingClientPort = mServerDialog->configuredPort();
        writeClientEndpoint(mPendingClientAddress, mPendingClientPort);

        if (!mServerDialog->isRunning())
        {
            if (mServerDialog->isServerReachable())
            {
                mPlayPage->setServerRunning(true, mPendingClientAddress,
                    mPendingClientPort, false);
            }
            else
            {
                startedNow = mServerDialog->startServer();
                if (!startedNow)
                {
                    mPendingClientAddress.clear();
                    mPendingClientPort.clear();
                    return;
                }
            }
        }
    }

    if (startedNow)
        QTimer::singleShot(900, this, SLOT(launchClient()));
    else
        launchClient();
}

void Launcher::MainDialog::launchClient()
{
    refreshGameClientState();
    if (isGameClientRunning())
    {
        mPendingClientAddress.clear();
        mPendingClientPort.clear();
        return;
    }
    QString address = mPendingClientAddress.trimmed().isEmpty()
        ? mPlayPage->serverAddress() : mPendingClientAddress.trimmed();
    QString port = mPendingClientPort.trimmed().isEmpty()
        ? mPlayPage->serverPort() : mPendingClientPort.trimmed();

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    // ArenaMP Y087: Host mode on Linux must never fall back to build.ini.
    // Re-resolve the local server endpoint immediately before spawning tes3mp
    // and also persist it through writeClientEndpoint() below.
    if (mPlayPage->autoStartServer() && mServerDialog != nullptr)
    {
        address = mServerDialog->localConnectAddress();
        port = mServerDialog->configuredPort();
    }
#endif

    writeClientEndpoint(address, port);
    if (mChatPage != nullptr)
    {
        mChatPage->setServerEndpoint(address, port.toUShort());
        mChatPage->setGameRunning(true);
    }

    QStringList arguments;
    arguments.append(QLatin1String("--connect=") + address + QLatin1String(":") + port);
    const QString transientPassword=mPlayPage->alternativeServer() ? mPlayPage->takeMasterPassword() : (mPlayPage->takeMasterPassword(), QString());
    if (!transientPassword.isEmpty()) arguments.append(QLatin1String("--password=") + transientPassword);
    mPendingClientAddress.clear();
    mPendingClientPort.clear();

    if (mGameInvoker->startProcess(QLatin1String("tes3mp"), arguments, false))
    {
        refreshGameClientState();
        // Do not terminate the launcher: chat, mentions and the server-status
        // connection stay live. Hiding completely also makes mentions easy to
        // miss, so keep a normal taskbar entry in minimized state.
        showMinimized();
    }
    else if (mChatPage != nullptr)
        mChatPage->setGameRunning(false);
}

void Launcher::MainDialog::runServer()
{
    if (!writeSettings())
        return;

    mServerDialog->setAutoRestartEnabled(mPlayPage->autoRestartServer());

    if (!mServerDialog->isRunning())
    {
        QString bindError;
        if (!mServerDialog->setConfiguredLocalAddress(mPlayPage->hostBindAddress(), &bindError))
        {
            QMessageBox::warning(this, tr("Invalid server interface"), bindError);
            return;
        }

        QString portError;
        if (!mServerDialog->setConfiguredPort(mPlayPage->serverPort(), &portError))
        {
            QMessageBox::warning(this, tr("Invalid server port"), portError);
            return;
        }
    }

    mPlayPage->switchToServerConsoleTab();
    // Explicit server start is always local Host mode, even if the checkbox
    // was previously off (e.g. after choosing a remote server).
    if (mServerDialog->startServer())
        mPlayPage->setAutoStartServer(true);
    else
        mPlayPage->setAutoStartServer(false);
}

void Launcher::MainDialog::stopServer()
{
    if (mServerDialog != nullptr)
        mServerDialog->stopServer();
}


QString Launcher::MainDialog::resolveSelectedDataFilePath(const QString& fileName, const QStringList& selectedPaths) const
{
    for (const QString& path : selectedPaths)
    {
        if (QFileInfo(path).fileName().compare(fileName, Qt::CaseInsensitive) == 0)
            return path;
    }

    QStringList searchDirs;
    if (!mGameSettings.getDataLocal().isEmpty())
        searchDirs << mGameSettings.getDataLocal();
    searchDirs << mGameSettings.getDataDirs();
    for (const QString& dirPath : searchDirs)
    {
        const QString candidate = QDir(dirPath).filePath(fileName);
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return QString();
}

bool Launcher::MainDialog::ensureServerDataFileHashes(QString* errorMessage, bool* changed)
{
    if (changed) *changed = false;
    auto fail = [&](const QString& reason) -> bool
    {
        if (errorMessage) *errorMessage = reason;
        return false;
    };
    if (mDataFilesPage == nullptr || mServerDialog == nullptr)
        return fail(tr("The selected mod list or server is not available."));

    // The Data Files selection, NOT the shipped default JSON and NOT a stale
    // build.ini, is authoritative. Save the live selection before reading it.
    mDataFilesPage->saveSettings();
    const QStringList selectedPaths = mDataFilesPage->selectedFilePaths();
    const QStringList contentFiles = mGameSettings.getContentList();
    const QStringList groundcoverFiles = mGameSettings.getGroundcoverList();

    if (contentFiles.isEmpty())
        return fail(tr("No content files are selected. Choose mods before running the server."));
    if (selectedPaths.size() != contentFiles.size() + groundcoverFiles.size())
        return fail(tr("Some selected mod files are missing. Refresh the Mods list and check the selected files."));

    auto crc32ForPath = [](const QString& path, quint32* value) -> bool
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return false;
        boost::crc_32_type crc;
        while (!file.atEnd())
        {
            const QByteArray chunk = file.read(1024 * 1024);
            if (chunk.isEmpty())
                return false;
            crc.process_bytes(chunk.constData(), static_cast<std::size_t>(chunk.size()));
        }
        *value = crc.checksum();
        return true;
    };

    auto makeEntry = [&](const QString& fileName, QJsonObject* entry, QString* error) -> bool
    {
        const QString path = resolveSelectedDataFilePath(fileName, selectedPaths);
        if (path.isEmpty() || !QFileInfo(path).isFile())
        {
            *error = tr("Selected data file was not found: %1").arg(fileName);
            return false;
        }
        quint32 crc = 0;
        if (!crc32ForPath(path, &crc))
        {
            *error = tr("Could not calculate CRC32 for %1").arg(QDir::toNativeSeparators(path));
            return false;
        }
        const QString hash = QStringLiteral("0x")
            + QString::number(crc, 16).rightJustified(8, QLatin1Char('0')).toUpper();
        QJsonArray hashes;
        hashes.append(hash);
        entry->insert(fileName, hashes);
        return true;
    };

    QJsonArray contentArray;
    QJsonArray groundcoverArray;
    QString error;
    for (const QString& fileName : contentFiles)
    {
        QJsonObject entry;
        if (!makeEntry(fileName, &entry, &error)) return fail(error);
        contentArray.append(entry);
    }
    for (const QString& fileName : groundcoverFiles)
    {
        QJsonObject entry;
        if (!makeEntry(fileName, &entry, &error)) return fail(error);
        groundcoverArray.append(entry);
    }

    QJsonObject root;
    root.insert(QStringLiteral("formatVersion"), 2);
    root.insert(QStringLiteral("content"), contentArray);
    root.insert(QStringLiteral("groundcover"), groundcoverArray);

    // H011: the furniture scanner needs the same *resolved paths* as the hash
    // preflight, not an arbitrary relative "Data Files" directory. This also
    // works when Windows usernames contain non-ASCII characters or the active
    // build spans several data directories. The file stores paths only: item
    // definitions and prices still come from reading the selected ESM/ESP.
    auto saveFurnitureSources = [&]() -> bool
    {
        QJsonObject resolvedFiles;
        QJsonArray directories;
        QStringList seenDirectories;
        for (const QString& fileName : contentFiles)
        {
            const QString resolved = resolveSelectedDataFilePath(fileName, selectedPaths);
            if (resolved.isEmpty() || !QFileInfo(resolved).isFile())
                return fail(tr("Could not resolve the furniture source mod: %1").arg(fileName));
            const QString fullPath = QFileInfo(resolved).absoluteFilePath();
            resolvedFiles.insert(fileName, fullPath);
            const QString directory = QFileInfo(fullPath).absolutePath();
            if (!seenDirectories.contains(directory, Qt::CaseInsensitive))
            {
                seenDirectories.append(directory);
                directories.append(directory);
            }
        }
        const QString sourcePath = QFileInfo(mServerDialog->requiredDataFilesPath())
            .dir().filePath(QStringLiteral("custom/arenaFurnitureSources.json"));
        QJsonObject sources;
        sources.insert(QStringLiteral("dataDirs"), directories);
        sources.insert(QStringLiteral("files"), resolvedFiles);
        {
            QFile existing(sourcePath);
            if (existing.open(QIODevice::ReadOnly))
            {
                const QJsonDocument previous = QJsonDocument::fromJson(existing.readAll());
                existing.close();
                if (previous.isObject() && previous.object() == sources)
                    return true;
            }
        }
        if (!QDir().mkpath(QFileInfo(sourcePath).absolutePath()))
            return fail(tr("Could not create the furniture source directory: %1").arg(sourcePath));
        QSaveFile output(sourcePath);
        if (!output.open(QIODevice::WriteOnly))
            return fail(tr("Could not write furniture source paths to %1: %2")
                .arg(sourcePath, output.errorString()));
        const QByteArray data = QJsonDocument(sources).toJson(QJsonDocument::Indented);
        if (output.write(data) != data.size() || !output.commit())
            return fail(tr("Could not save furniture source paths to %1: %2")
                .arg(sourcePath, output.errorString()));
        QFile verify(sourcePath);
        if (!verify.open(QIODevice::ReadOnly)
            || QJsonDocument::fromJson(verify.readAll()).object() != sources)
            return fail(tr("Could not verify saved furniture source paths: %1").arg(sourcePath));
        return true;
    };

    const QString path = mServerDialog->requiredDataFilesPath();
    // QJsonArray equality is ORDER-SENSITIVE: a different load order, count,
    // filename or CRC32 forces replacement; matching files stay untouched.
    // On Windows an open handle to the OLD requiredDataFiles.json can prevent
    // QSaveFile::commit() from replacing it ("Access denied"). In particular,
    // do NOT keep the QFile used for comparison alive until after commit.
    {
        QFile existing(path);
        if (existing.open(QIODevice::ReadOnly))
        {
            QJsonParseError parseError;
            const QJsonDocument old = QJsonDocument::fromJson(existing.readAll(), &parseError);
            existing.close();
            if (parseError.error == QJsonParseError::NoError && old.isObject()
                && old.object() == root)
            {
                qInfo() << "ArenaMP server mod manifest verified:" << contentFiles.size()
                    << "content," << groundcoverFiles.size() << "groundcover," << path;
                return saveFurnitureSources();
            }
        }
    } // No read handle to the target survives beyond this point.

    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return fail(tr("Could not create the server data directory for %1").arg(path));

    // Some Windows distributions mark the shipped JSON read-only. Only make
    // this specific generated manifest writable, not the whole server tree.
    const QFileInfo targetInfo(path);
    if (targetInfo.exists() && !targetInfo.isWritable())
    {
        if (!QFile::setPermissions(path,
                targetInfo.permissions() | QFileDevice::WriteOwner | QFileDevice::WriteUser))
            return fail(tr("Could not make the server manifest writable: %1. "
                "Check the file's Read-only attribute and your folder permissions.").arg(path));
    }

    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly))
        return fail(tr("Could not write %1: %2. Ensure the server folder is writable "
            "and no other server instance has locked the file.").arg(path, output.errorString()));
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (output.write(json) != json.size())
    {
        const QString reason = output.errorString();
        output.cancelWriting();
        return fail(tr("Could not write the complete server manifest %1: %2").arg(path, reason));
    }
    if (!output.commit())
        return fail(tr("Could not replace the server manifest %1: %2. "
            "Close other tes3mp-server instances and check write permissions for the file and its folder.")
            .arg(path, output.errorString()));

    // Verify what is actually on disk before permitting process creation.
    QFile verify(path);
    if (!verify.open(QIODevice::ReadOnly)
        || QJsonDocument::fromJson(verify.readAll()).object() != root)
        return fail(tr("The saved server manifest could not be verified: %1").arg(path));
    if (!saveFurnitureSources()) return false;
    if (changed) *changed = true;
    qInfo() << "ArenaMP server mod manifest refreshed:" << contentFiles.size()
        << "content," << groundcoverFiles.size() << "groundcover," << path;
    return true;
}

void Launcher::MainDialog::updateServerDataFileHashes()
{
    if (mServerDialog == nullptr || mPlayPage == nullptr) return;
    if (mServerDialog->isRunning())
    {
        QMessageBox::warning(this, tr("Update Hash"),
            tr("Stop the local server before updating its required mod list."));
        return;
    }
    mPlayPage->setEnforceDataFiles(true);
    if (!mPlayPage->saveServerSettings()) return;
    QString error;
    bool changed = false;
    if (!ensureServerDataFileHashes(&error, &changed))
    {
        QMessageBox::critical(this, tr("Update Hash"), error);
        return;
    }

    const QString status = tr("Server manifest %1.\n\n%2")
        .arg(changed ? tr("updated") : tr("already up to date"),
            QDir::toNativeSeparators(mServerDialog->requiredDataFilesPath()));
    QMessageBox::information(this, tr("Update Hash"), status);
}

void Launcher::MainDialog::clearServerCells()
{
    if (mServerDialog == nullptr)
        return;
    if (mServerDialog->isRunning())
    {
        QMessageBox::warning(this, tr("Clear server cells"),
            tr("Stop the server before clearing persistent data."));
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::warning(this,
        tr("Clear server cells?"),
        tr("This will delete all saved cell state. Player accounts and world data will be kept. Continue?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    QString error;
    if (!mServerDialog->clearPersistentCells(&error))
    {
        QMessageBox::critical(this, tr("Clear server cells"),
            tr("Server data reset failed: %1").arg(error));
        return;
    }
    QMessageBox::information(this, tr("Clear server cells"), tr("All saved server cells were cleared."));
}

void Launcher::MainDialog::resetServerData()
{
    if (mServerDialog == nullptr)
        return;
    if (mServerDialog->isRunning())
    {
        QMessageBox::warning(this, tr("Full server reset"),
            tr("Stop the server before clearing persistent data."));
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::critical(this,
        tr("Full server reset?"),
        tr("This will delete player accounts, cells, world state, maps, custom data, record stores and the server database. requiredDataFiles.json, banlist.json and custom/arenaFurnitureReference.json will be kept. Continue?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    QString error;
    if (!mServerDialog->resetPersistentServerData(&error))
    {
        QMessageBox::critical(this, tr("Full server reset"),
            tr("Server data reset failed: %1").arg(error));
        return;
    }
    QMessageBox::information(this, tr("Full server reset"), tr("Server gameplay data was fully reset."));
}

void Launcher::MainDialog::autoStartServerChanged(bool enabled)
{
    mLauncherSettings.remove(QStringLiteral("General/Server/autoStart"));
    mLauncherSettings.setValue(QStringLiteral("General/Server/autoStart"),
        enabled ? QStringLiteral("true") : QStringLiteral("false"));
}

void Launcher::MainDialog::autoRestartServerChanged(bool enabled)
{
    if (mPlayPage != nullptr && mPlayPage->autoRestartServer() != enabled)
        mPlayPage->setAutoRestartServer(enabled);
    if (mServerDialog != nullptr && mServerDialog->autoRestartEnabled() != enabled)
        mServerDialog->setAutoRestartEnabled(enabled);

    mLauncherSettings.remove(QStringLiteral("General/Server/autoRestart"));
    mLauncherSettings.setValue(QStringLiteral("General/Server/autoRestart"),
        enabled ? QStringLiteral("true") : QStringLiteral("false"));
}



void Launcher::MainDialog::serverRunningChanged(bool running, const QString& address, const QString& port)
{
    // The checkbox reflects the actual managed server lifecycle, not merely
    // what the user selected before launching. In particular, turn Host off
    // after a crash or manual stop and back on after a successful restart.
    if (mPlayPage != nullptr && mPlayPage->autoStartServer() != running)
        mPlayPage->setAutoStartServer(running);
    if (mPlayPage != nullptr)
    {
        mPlayPage->setServerRunning(running, address, port);
    }

    if (mChatPage != nullptr && !address.trimmed().isEmpty())
        mChatPage->setServerEndpoint(address, port.toUShort());

    updateFooterServerStatus(running, address, port);
}

void Launcher::MainDialog::showChangelog()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("CHANGELOG.txt")),
        QDir(appDir).filePath(QStringLiteral("CHANGELOG.md")),
        QDir::current().filePath(QStringLiteral("CHANGELOG.txt")),
        QDir::current().filePath(QStringLiteral("CHANGELOG.md"))
    };

    QString changelogPath;
    for (const QString& candidate : candidates)
    {
        if (QFileInfo(candidate).isFile())
        {
            changelogPath = candidate;
            break;
        }
    }

    if (changelogPath.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("Changelog"),
            tr("CHANGELOG.txt was not found in the ArenaMP installation."));
        return;
    }

    QFile file(changelogPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, QStringLiteral("Changelog"),
            tr("Could not open the changelog file: %1").arg(changelogPath));
        return;
    }

    QTextStream stream(&file);
    stream.setCodec(QTextCodec::codecForName("UTF-8"));
    const QString markdown = stream.readAll();

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("arenaChangelogDialog"));
    dialog.setWindowTitle(QStringLiteral("%1 — Changelog").arg(mBuildName));
    dialog.resize(860, 600);
    dialog.setMinimumSize(620, 420);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    QLabel* header = new QLabel(QStringLiteral("<b>%1 — Changelog</b>").arg(mBuildName.toHtmlEscaped()), &dialog);
    QFont headerFont = header->font();
    const int headerBasePx = headerFont.pixelSize() > 0 ? headerFont.pixelSize() : 13;
    headerFont.setPixelSize(headerBasePx + 3);
    header->setFont(headerFont);
    layout->addWidget(header);

    QTextBrowser* browser = new QTextBrowser(&dialog);
    browser->setOpenExternalLinks(true);
    browser->setReadOnly(true);
    browser->setHtml(changelogMarkdownToHtml(markdown));
    layout->addWidget(browser, 1);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Close"));
    connect(buttons, SIGNAL(rejected()), &dialog, SLOT(reject()));
    layout->addWidget(buttons);

    ArenaUi::installGlassWindow(dialog);
    dialog.exec();
}

void Launcher::MainDialog::help()
{
    // U022: there is no bundled manual yet, so Help opens the ArenaMP chat.
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://t.me/arena_mp")));
}
