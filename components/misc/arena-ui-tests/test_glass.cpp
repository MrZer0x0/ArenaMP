// Include the actual updater window and transaction helpers in the smoke test.
#define main arenaUpdaterMain
#include "../../../apps/launcher/updater/arena_updater.cpp"
#undef main
#include "../arenaglassicons.hpp"
#include "../arenaherobutton.hpp"
#include <QMainWindow>
#include <QGroupBox>
#include <QCheckBox>
#include <QPixmap>
#include <QLineEdit>
#include <QListWidget>
#include <QWizard>
#include <QWizardPage>
#include <QToolButton>
#include <QTabWidget>
#include <QTabBar>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    ArenaUi::applyMorrowindGlassPalette(app);
    if (selfTest() != 0) return 1;
    ProgressWindow updater;
    ArenaUi::installGlassWindow(updater);
    ArenaUi::installGlassWindow(updater); // must not reserve a second title bar
    if (updater.contentsMargins().top() != 44) return 2;
    updater.present();
    updater.itemProgress(63, 100, QStringLiteral("63 / 100 MiB — update.zip"));
    auto* close = updater.findChild<QToolButton*>(QStringLiteral("arenaClose"));
    if (!close || !updater.isVisible()) return 3;
    close->click();
    if (!gCancelRequested || !updater.isVisible()) return 4;
    gCancelRequested = false;
    // The window must remain visible while cancellation is locked for apply.
    updater.setCancelable(false);
    updater.close();
    if (gCancelRequested || !updater.isVisible()) return 5;
    updater.itemProgress(64, 100, QStringLiteral("64 / 100 MiB — update.zip"));
    if (!updater.isVisible()) return 6;
    updater.grab().save(QStringLiteral("updater-preview.png"));
    updater.hide();

    QMainWindow launcher;
    launcher.setWindowTitle(QStringLiteral("ArenaMP • Launcher"));
    launcher.setFixedSize(1080, 720);
    auto* central = new QWidget(&launcher);
    central->setObjectName(QStringLiteral("centralwidget"));
    launcher.setCentralWidget(central);
    auto* layout = new QVBoxLayout(central);
    layout->setSpacing(8);
    auto* tabs = new QListWidget(central);
    tabs->setObjectName(QStringLiteral("iconWidget"));
    tabs->setViewMode(QListView::IconMode);
    tabs->setFlow(QListView::LeftToRight);
    tabs->setIconSize(QSize(27, 27));
    tabs->setWrapping(false);
    tabs->setGridSize(QSize(178, 58));
    tabs->setWordWrap(false);
    tabs->setTextElideMode(Qt::ElideNone);
    tabs->setFixedHeight(70);
    // U014 custom controls must be available from the compiled resource bundle;
    // otherwise checked boxes degrade to an empty gold square.
    if (QPixmap(QStringLiteral(":/arena/arenaicons/check.png")).isNull()) return 7;
    if (QPixmap(QStringLiteral(":/arena/arenaicons/chevron-down.png")).isNull()) return 8;
    // U020 showcase Play page icons (status column, cards, dark-on-gold actions).
    for (const char* icon : {"gamepad", "database", "cpu", "drive", "wifi", "cube", "users", "info",
                             "trash", "refresh", "stop", "stop-muted", "play-dark", "update-dark", "hint"})
        if (ArenaUi::glassIcon(QString::fromLatin1(icon)).pixmap(24, 24).isNull()) return 14;

    const QStringList names{"play", "browse", "graphics", "settings", "advanced"};
    const QStringList labels{"Play", "Data Files", "Graphics", "Settings", "Advanced"};
    for (int i = 0; i < names.size(); ++i)
    {
        const auto icon = ArenaUi::glassIcon(names[i]);
        if (icon.pixmap(32, 32).isNull()) return 9;
        (new QListWidgetItem(icon, labels[i], tabs))->setSizeHint(QSize(176, 56));
    }
    tabs->setCurrentRow(0);
    layout->addWidget(tabs);
    auto* group = new QGroupBox(QStringLiteral("Connection"), central);
    auto* fields = new QVBoxLayout(group);
    fields->addWidget(new QLabel(QStringLiteral("ArenaMP — Morrowind multiplayer"), group));
    fields->addWidget(new QLineEdit(QStringLiteral("play.example.org"), group));
    fields->addWidget(new QLineEdit(QStringLiteral("25565"), group));
    auto* checked = new QCheckBox(QStringLiteral("Use hardware recommendation"), group);
    checked->setChecked(true);
    fields->addWidget(checked);
    layout->addWidget(group);
    layout->addStretch();
    auto* bar = new QProgressBar(central);
    bar->setValue(63);
    layout->addWidget(bar);
    auto* buttons = new QHBoxLayout;
    for (const QString& name : {QStringLiteral("changelog"), QStringLiteral("server"), QStringLiteral("play")})
    {
        auto* button = new QPushButton(ArenaUi::glassIcon(name), name, central);
        if (name == QLatin1String("play")) button->setProperty("arenaPrimary", true);
        buttons->addWidget(button);
    }
    layout->addLayout(buttons);
    ArenaUi::installGlassWindow(launcher);
    launcher.show();
    app.processEvents();
    // U021: Windows-style controls live on the right and the maximize control
    // is gone from the fixed-size launcher chrome.
    if (launcher.findChild<QToolButton*>(QStringLiteral("arenaMaximize")) != nullptr) return 10;
    auto* closeButton = launcher.findChild<QToolButton*>(QStringLiteral("arenaClose"));
    auto* minimizeButton = launcher.findChild<QToolButton*>(QStringLiteral("arenaMinimize"));
    if (!closeButton || !minimizeButton || closeButton->isHidden()) return 12;
    if (closeButton->mapTo(&launcher, QPoint(0, 0)).x() <= launcher.width() / 2) return 15;
    if (closeButton->mapTo(&launcher, QPoint(0, 0)).x() <= minimizeButton->mapTo(&launcher, QPoint(0, 0)).x()) return 16;
    if (central->geometry().top() < 44) return 11;
    auto* modeTabs = new QTabWidget(central);
    modeTabs->setGeometry(20, 180, 720, 120);
    modeTabs->addTab(new QWidget(modeTabs), QStringLiteral("Play"));
    modeTabs->addTab(new QWidget(modeTabs), QStringLiteral("Server Console"));
    modeTabs->addTab(new QWidget(modeTabs), QStringLiteral("Server Settings"));
    modeTabs->tabBar()->setExpanding(true);
    modeTabs->tabBar()->setUsesScrollButtons(false);
    modeTabs->show();
    app.processEvents();
    if (modeTabs->tabBar()->width() > modeTabs->width()) return 13;
    // U021: the hero action must report its own size instead of collapsing to
    // the stylesheet minimum used by ordinary push buttons.
    ArenaUi::HeroButton hero(central);
    hero.setText(QStringLiteral("Start game"));
    hero.setSubtitle(QStringLiteral("Local server + game on port 25565"));
    hero.setPulse(true);
    hero.setGeometry(20, 320, 320, hero.sizeHint().height());
    hero.show();
    app.processEvents();
    if (hero.sizeHint().height() < 64) return 17;
    if (hero.height() < 64) return 18;

    launcher.grab().save(QStringLiteral("launcher-component-preview.png"));
    launcher.hide();
    QWizard wizard;
    wizard.setWindowTitle(QStringLiteral("ArenaMP — Setup"));
    auto* page = new QWizardPage;
    page->setTitle(QStringLiteral("Choose your Morrowind installation"));
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->addWidget(new QLineEdit(QStringLiteral("C:/Games/Morrowind"), page));
    wizard.addPage(page);
    wizard.resize(700, 430);
    ArenaUi::installGlassWindow(wizard);
    wizard.show();
    app.processEvents();
    if (page->mapTo(&wizard, QPoint()).y() < 44) return 12;
    wizard.grab().save(QStringLiteral("wizard-component-preview.png"));
    return 0;
}
