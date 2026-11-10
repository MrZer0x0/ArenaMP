#ifndef OPENMW_COMPONENTS_MISC_ARENATHEME_H
#define OPENMW_COMPONENTS_MISC_ARENATHEME_H

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>
#include <QString>

namespace ArenaUi
{
    // Compiled-in visual language shared by Launcher, Wizard, Updater and Qt
    // utilities. The controls deliberately stay native Qt widgets so the UI
    // remains cheap to draw on integrated GPUs and Android-derived systems.
    inline void applyMorrowindGlassPalette(QApplication& app)
    {
#ifdef Q_OS_WIN
        // Windows exposes a separate accessibility "Text size" scale in
        // addition to monitor DPI. Qt maps that setting into the default
        // application point font, which can make a fixed launcher layout grow
        // beyond its cards and buttons. Keep the launcher typography in logical
        // pixels instead: monitor DPI/high-DPI rendering still works, but the
        // Windows text-only multiplier no longer changes widget geometry.
        QFont stableFont = app.font();
        stableFont.setPixelSize(13);
        app.setFont(stableFont);
#endif
        if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion")))
            app.setStyle(fusion);

        const QColor obsidian(25, 26, 29);
        const QColor surface(31, 32, 35);
        const QColor surfaceRaised(43, 44, 48);
        const QColor brass(194, 157, 91);
        const QColor gold(226, 193, 126);
        const QColor text(235, 230, 220);
        const QColor disabled(105, 102, 96);

        QPalette palette;
        palette.setColor(QPalette::Window, obsidian);
        palette.setColor(QPalette::WindowText, text);
        palette.setColor(QPalette::Base, surface);
        palette.setColor(QPalette::AlternateBase, surfaceRaised);
        palette.setColor(QPalette::ToolTipBase, surfaceRaised);
        palette.setColor(QPalette::ToolTipText, text);
        palette.setColor(QPalette::Text, text);
        palette.setColor(QPalette::Button, surfaceRaised);
        palette.setColor(QPalette::ButtonText, text);
        palette.setColor(QPalette::BrightText, QColor(255, 241, 211));
        palette.setColor(QPalette::Link, gold);
        palette.setColor(QPalette::Highlight, brass);
        palette.setColor(QPalette::HighlightedText, QColor(28, 24, 18));
        palette.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
        palette.setColor(QPalette::Disabled, QPalette::Text, disabled);
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
        app.setPalette(palette);

        // MSVC has a relatively small limit for a single string literal.
        // Keep the shared QSS in several chunks so the launcher, wizard and
        // updater can all include this header without C2026.
        QString arenaStyleSheet;
        arenaStyleSheet.reserve(45056);
        arenaStyleSheet += QStringLiteral(R"ARENA(
            QMainWindow, QDialog, QWizard, QWidget#centralWidget, QWidget#centralwidget {
                background-color: #191a1d;
                color: #ebe6dc;
            }
            QWidget { selection-background-color: #c19b5b; selection-color: #1d1812; }
            QLabel { color: #ebe6dc; background: transparent; }
            QLabel:disabled { color: #706c64; }

            QStackedWidget#pagesWidget {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                    stop:0 rgba(36,37,40,205), stop:1 rgba(19,20,23,215));
                border: 1px solid rgba(233, 216, 182, 34);
                border-radius: 15px;
            }
            QFrame[arenaCard="true"] {
                background-color: rgba(255, 255, 255, 12);
                border: 1px solid rgba(255, 255, 255, 24);
                border-radius: 12px;
            }
            QFrame[arenaFooter="true"] {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                    stop:0 rgba(255,255,255,10), stop:1 rgba(255,255,255,6));
                border: 1px solid rgba(255,255,255,20);
                border-radius: 12px;
            }
            QLabel#versionLabel {
                padding: 5px 9px;
                border-radius: 10px;
                font-size: 11px;
                font-weight: 600;
            }
            QLabel#versionLabel[arenaStatus="online"] {
                color: #62d987;
                background-color: rgba(56,180,92,18);
                border: 1px solid rgba(83,202,113,36);
            }
            QLabel#versionLabel[arenaStatus="offline"] {
                color: #aaa69e;
                background-color: rgba(255,255,255,6);
                border: 1px solid rgba(255,255,255,15);
            }

            QGroupBox {
                color: #d7bd89;
                background-color: rgba(255, 255, 255, 7);
                border: 1px solid rgba(230, 206, 158, 33);
                border-radius: 12px;
                margin-top: 12px;
                padding-top: 8px;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 10px;
                padding: 0 5px;
            }

            QPushButton {
                color: #eee8dd;
                background-color: rgba(255, 255, 255, 14);
                border: 1px solid rgba(226, 193, 126, 68);
                border-radius: 11px;
                padding: 6px 12px;
                min-height: 22px;
                font-weight: 500;
            }
            QPushButton:hover {
                background-color: rgba(226, 193, 126, 28);
                border-color: rgba(226, 193, 126, 145);
            }
            QPushButton:pressed {
                background-color: rgba(173, 132, 74, 70);
                border-color: #9f7e4b;
            }
            QPushButton:default { border-color: #d6ae68; }
            QPushButton:disabled {
                color: #716e68;
                background-color: rgba(255, 255, 255, 6);
                border-color: rgba(255, 255, 255, 14);
            }
            QPushButton[arenaPrimary="true"] {
                color: #241d13;
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 #e7c887, stop:1 #b98e50);
                border: 1px solid #f0d79e;
                font-weight: 600;
                padding-left: 15px;
                padding-right: 15px;
            }
            QPushButton[arenaPrimary="true"]:hover { background: #ecd093; }
            QPushButton[arenaPrimary="true"]:pressed { background: #ae8248; }
            QPushButton[arenaPrimary="true"]:disabled {
                background: #3c3933; color: #8d887e; border-color: #555048;
            }
            QPushButton[arenaQuiet="true"] {
                background-color: rgba(255, 255, 255, 7);
                border-color: rgba(255, 255, 255, 20);
            }
            QPushButton[arenaQuiet="true"]:hover {
                background-color: rgba(255, 255, 255, 16);
                border-color: rgba(226, 193, 126, 82);
            }
            QDialogButtonBox QPushButton { min-width: 82px; min-height: 24px; }

            QToolButton {
                color: #ece5d8;
                background-color: rgba(255, 255, 255, 12);
                border: 1px solid rgba(226, 193, 126, 55);
                border-radius: 8px;
                padding: 4px 6px;
            }
            QToolButton:hover {
                background-color: rgba(226, 193, 126, 24);
                border-color: rgba(226, 193, 126, 125);
            }
            QToolButton:pressed { background-color: rgba(151, 112, 61, 65); }
            QToolButton:disabled { color: #716e68; border-color: rgba(255,255,255,12); }

            QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox, QComboBox,
            QListView, QTreeView, QTableView {
                color: #ebe6dc;
                background-color: rgba(18, 19, 21, 205);
                border: 1px solid rgba(231, 218, 194, 46);
                border-radius: 8px;
            }
            QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
                min-height: 20px;
                padding: 4px 8px;
            }
            QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover, QComboBox:hover {
                border-color: rgba(226, 193, 126, 92);
            }
            QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus,
            QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus,
            QListView:focus, QTreeView:focus, QTableView:focus {
                border-color: #c9a562;
            }
            QComboBox { padding-right: 28px; }
            QComboBox::drop-down {
                subcontrol-origin: padding;
                subcontrol-position: top right;
                width: 24px;
                border: 0;
                background-color: transparent;
                border-top-right-radius: 8px;
                border-bottom-right-radius: 8px;
            }
            QComboBox::drop-down:hover { background-color: rgba(226,193,126,18); }
            QComboBox::down-arrow {
                image: url(:/arena/arenaicons/chevron-down.png);
                width: 10px;
                height: 10px;
            }
            QComboBox::down-arrow:on { top: 1px; }
            QComboBox QAbstractItemView {
                background-color: #25262a;
                border: 1px solid #5b5245;
                color: #ebe6dc;
                padding: 4px;
                outline: 0;
            }
            QAbstractItemView::item { border-radius: 5px; padding: 4px; }
            QAbstractItemView::item:hover { background-color: rgba(226, 193, 126, 30); }
            QAbstractItemView::item:selected { background-color: #b99254; color: #201a12; }

            QCheckBox, QRadioButton { color: #e1dcd2; spacing: 8px; }
            QCheckBox::indicator {
)ARENA");
        arenaStyleSheet += QStringLiteral(R"ARENA(                width: 18px; height: 18px;
                border: 1px solid #6b685f;
                border-radius: 5px;
                background-color: rgba(24, 25, 28, 235);
            }
            QCheckBox::indicator:hover {
                border-color: #c9a562;
                background-color: rgba(226, 193, 126, 18);
            }
            QCheckBox::indicator:checked {
                image: url(:/arena/arenaicons/check.png);
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 #efd18c, stop:1 #bd8c4a);
                border: 1px solid #f1d79d;
            }
            QCheckBox::indicator:checked:hover {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 #f5d99a, stop:1 #ca9855);
                border-color: #ffe5af;
            }
            QCheckBox::indicator:disabled {
                background-color: #222326;
                border-color: #44443f;
            }
            QCheckBox::indicator:checked:disabled {
                image: url(:/arena/arenaicons/check.png);
                background-color: #6f624d;
                border-color: #81735b;
            }
            QRadioButton::indicator {
                width: 18px; height: 18px;
                border: 1px solid #6b685f;
                border-radius: 10px;
                background-color: rgba(24, 25, 28, 235);
            }
            QRadioButton::indicator:hover { border-color: #c9a562; }
            QRadioButton::indicator:checked {
                image: url(:/arena/arenaicons/radio-dot.png);
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 #efd18c, stop:1 #bd8c4a);
                border: 1px solid #f1d79d;
            }
            QCheckBox:focus, QRadioButton:focus { color: #f0d7a4; }

            QTabWidget::pane {
                top: -1px;
                background-color: rgba(255, 255, 255, 5);
                border: 1px solid rgba(232, 215, 181, 30);
                border-radius: 11px;
            }
            QTabBar { background: transparent; }
            QTabBar::tab {
                color: #aaa69e;
                background-color: transparent;
                border: 1px solid transparent;
                border-radius: 8px;
                padding: 6px 12px;
                margin: 2px 2px 3px 0;
                min-width: 72px;
            }
            QTabBar::tab:hover {
                color: #e6dfd3;
                background-color: rgba(255,255,255,9);
            }
            QTabBar::tab:selected {
                color: #f0d9ac;
                background-color: rgba(198, 158, 91, 22);
                border-color: rgba(216, 178, 111, 70);
            }
            QTabWidget[arenaSegmented="true"] QTabBar::tab { padding: 5px 11px; }

            /* U016 horizontal controls: equal-width, no scrolling arrows, readable RU labels. */
            QTabWidget#pageTabs::pane {
                top: -1px;
                background-color: rgba(15,16,18,105);
                border: 1px solid rgba(233,216,182,25);
                border-radius: 13px;
            }
            QTabWidget#pageTabs QTabBar::tab {
                min-width: 0px;
                min-height: 22px;
                padding: 5px 8px;
                margin: 2px 2px 4px 0;
                color: #b7b2aa;
                background: transparent;
                border: 1px solid transparent;
                border-radius: 9px;
                font-size: 12px;
                font-weight: 600;
                min-width: 96px;
            }
            QTabWidget#pageTabs QTabBar::tab:hover {
                color: #eee7da;
                background-color: rgba(255,255,255,9);
            }
            QTabWidget#pageTabs QTabBar::tab:selected {
                color: #271f14;
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 #e7c887, stop:1 #ad8047);
                border-color: #efd498;
            }
            QTabWidget#playModeTabs::pane, QTabWidget#serverSettingsModeTabs::pane {
                top: -1px;
                background-color: rgba(255,255,255,5);
                border: 1px solid rgba(233,216,182,22);
                border-radius: 12px;
            }
            QTabWidget#playModeTabs QTabBar::tab,
            QTabWidget#serverSettingsModeTabs QTabBar::tab {
                min-width: 104px;
                min-height: 22px;
                padding: 5px 10px;
                margin: 2px 2px 4px 0;
                border: 1px solid transparent;
                border-radius: 10px;
                color: #b0aca4;
                font-size: 12px;
                font-weight: 600;
                background: transparent;
            }
            QTabWidget#playModeTabs QTabBar::tab:hover,
            QTabWidget#serverSettingsModeTabs QTabBar::tab:hover {
                color: #eee7da;
                background-color: rgba(255,255,255,9);
            }
            QTabWidget#playModeTabs QTabBar::tab:selected,
            QTabWidget#serverSettingsModeTabs QTabBar::tab:selected {
                color: #f0d7a4;
                background-color: rgba(198,158,91,27);
                border-color: rgba(226,193,126,84);
            }
            QFrame[arenaHeroCard="true"] {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                    stop:0 rgba(201,159,88,20), stop:0.45 rgba(255,255,255,9), stop:1 rgba(91,113,130,9));
                border: 1px solid rgba(226,193,126,37);
                border-radius: 12px;
            }
            QLineEdit[arenaBuildName="true"] {
                font-weight: 600;
                color: #f1e7d5;
                background-color: rgba(13,14,16,176);
                border-color: rgba(226,193,126,42);
            }
            QFrame[arenaSectionHeader="true"] {
                background: transparent;
                border: 0;
                padding: 1px 2px 4px 2px;
            }
            QLabel[arenaSectionTitle="true"] {
                color: #f0e9dc;
                font-size: 14px;
                font-weight: 650;
            }

            /* Advanced follows the same compact horizontal switch language. */
            QTabWidget#AdvancedTabWidget::pane {
                top: -1px;
                background-color: rgba(16,17,19,120);
                border: 1px solid rgba(233,216,182,24);
                border-radius: 12px;
            }
            QTabWidget#AdvancedTabWidget QTabBar::tab {
                min-width: 92px;
                padding: 5px 10px;
                margin: 2px 2px 4px 0;
                border-radius: 10px;
                border: 1px solid transparent;
                color: #b0aca4;
                font-size: 12px;
                font-weight: 600;
                background: transparent;
            }
            QTabWidget#AdvancedTabWidget QTabBar::tab:hover {
                color: #eee7da;
                background-color: rgba(255,255,255,9);
            }
            QTabWidget#AdvancedTabWidget QTabBar::tab:selected {
                color: #f0d7a4;
)ARENA");
        arenaStyleSheet += QStringLiteral(R"ARENA(                background-color: rgba(198,158,91,25);
                border-color: rgba(226,193,126,78);
            }

            /* U014 compact graphics page: segmented macOS-like inner navigation. */
            QTabWidget#DisplayTabWidget::pane {
                top: -1px;
                background-color: rgba(16, 17, 19, 130);
                border: 1px solid rgba(233, 216, 182, 27);
                border-radius: 13px;
            }
            QTabWidget#DisplayTabWidget QTabBar::tab {
                min-width: 92px;
                padding: 5px 12px;
                margin: 2px 2px 4px 0;
                color: #b0aca4;
                border: 1px solid transparent;
                border-radius: 10px;
                font-size: 12px;
                font-weight: 600;
                background: transparent;
            }
            QTabWidget#DisplayTabWidget QTabBar::tab:hover {
                color: #eee7da;
                background-color: rgba(255,255,255,9);
            }
            QTabWidget#DisplayTabWidget QTabBar::tab:selected {
                color: #f0d7a4;
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 rgba(205, 164, 94, 46), stop:1 rgba(144, 104, 55, 28));
                border-color: rgba(226, 193, 126, 93);
            }

            QFrame[arenaCard="true"] {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                    stop:0 rgba(255,255,255,13), stop:1 rgba(255,255,255,7));
                border: 1px solid rgba(237, 220, 187, 34);
                border-radius: 12px;
            }
            QLabel[arenaTitle="true"] {
                color: #f2ecdf;
                font-size: 15px;
                font-weight: 650;
            }
            QLabel[arenaMuted="true"] {
                color: #b3afa8;
                font-size: 11px;
            }
            QLabel[arenaAccent="true"] {
                color: #e3bd70;
                font-weight: 600;
            }
            QLabel[arenaSettingLabel="true"] { color: #d8d2c8; }
            QFrame[arenaDivider="true"] {
                border: 0;
                background-color: rgba(255,255,255,20);
                min-height: 1px;
                max-height: 1px;
            }
            QPushButton[arenaPresetButton="true"] {
                min-height: 28px;
                padding: 4px 8px;
                border-radius: 10px;
                color: #ddd8cf;
                background-color: rgba(255,255,255,8);
                border: 1px solid rgba(255,255,255,18);
                font-size: 11px;
                font-weight: 600;
            }
            QPushButton[arenaPresetButton="true"]:hover {
                color: #f1e9da;
                background-color: rgba(226,193,126,18);
                border-color: rgba(226,193,126,75);
            }
            QPushButton[arenaPresetButton="true"]:checked {
                color: #261d12;
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 #efd18c, stop:1 #b98949);
                border: 1px solid #f2d79b;
                font-weight: 650;
            }
            QWidget#qualityPresetButtonsHost { background: transparent; }

            QFrame#qualityQuickCard, QFrame#buildHeaderCard {
                border-radius: 14px;
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                    stop:0 rgba(205,164,94,18), stop:0.5 rgba(255,255,255,10), stop:1 rgba(96,114,132,10));
                border: 1px solid rgba(226,193,126,38);
            }
            QLabel[arenaSectionTitle="true"] {
                color: #f0e7d7;
                font-size: 15px;
                font-weight: 650;
            }

            QListWidget#iconWidget {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                    stop:0 rgba(255,255,255,9), stop:1 rgba(255,255,255,5));
                border: 1px solid rgba(255,255,255,20);
                border-radius: 14px;
                padding: 5px;
                outline: 0;
            }
            QListWidget#iconWidget::item {
                color: #d8d2c7;
                background: transparent;
                border: 1px solid transparent;
                border-radius: 10px;
                padding: 3px 7px;
            }
            QListWidget#iconWidget::item:hover {
                color: #f2eadc;
                background-color: rgba(255,255,255,10);
                border-color: rgba(255,255,255,18);
            }
            QListWidget#iconWidget::item:selected {
                color: #f6dda8;
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 rgba(205,164,94,52), stop:1 rgba(128,91,46,34));
                border-color: rgba(233,194,120,105);
            }

            QHeaderView::section {
                color: #d7c8aa;
                background-color: #28292d;
                border: 0;
                border-right: 1px solid #3d3e42;
                border-bottom: 1px solid #3d3e42;
                padding: 5px;
            }
            QProgressBar {
                min-height: 17px;
                color: #ebe6dc;
                background-color: #222327;
                border: 1px solid #4b4842;
                border-radius: 8px;
                text-align: center;
            }
            QProgressBar::chunk {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                    stop:0 #a67e48, stop:1 #e2c27e);
                border-radius: 6px;
            }

            QMenu {
                color: #ebe6dc;
                background-color: #25262a;
                border: 1px solid #504a40;
                border-radius: 8px;
                padding: 4px;
            }
            QMenu::item { padding: 6px 20px 6px 10px; border-radius: 5px; }
            QMenu::item:selected { background-color: rgba(193,154,88,65); color: #f2e4c9; }
            QToolTip {
                color: #f0e8dc;
                background-color: #2a2928;
                border: 1px solid #765f40;
                border-radius: 6px;
                padding: 5px;
            }

            /* U015 server console and server-settings polish. */
            QWidget#serverConsolePanel { background: transparent; }
            QFrame[arenaToolbar="true"] {
                background-color: rgba(255,255,255,7);
                border: 1px solid rgba(255,255,255,19);
                border-radius: 11px;
            }
            QFrame[arenaConsoleCard="true"] {
                background-color: rgba(11,12,14,225);
                border: 1px solid rgba(226,193,126,38);
                border-radius: 12px;
            }
            QLabel[arenaConsoleAddress="true"] {
                color: #77cf92;
                font-weight: 600;
                padding: 4px 8px;
                background-color: rgba(62,167,91,17);
                border: 1px solid rgba(91,190,116,38);
                border-radius: 7px;
            }
            QLabel[arenaConsolePort="true"] {
                color: #dfc187;
                padding: 4px 8px;
                background-color: rgba(226,193,126,10);
                border: 1px solid rgba(226,193,126,30);
                border-radius: 7px;
            }
            QPlainTextEdit[arenaConsole="true"] {
                color: #cec9bf;
                background-color: #101114;
                border: 0;
                border-radius: 11px;
                padding: 8px;
)ARENA");
        arenaStyleSheet += QStringLiteral(R"ARENA(                selection-background-color: #725b36;
                selection-color: #fff4df;
            }
            QPlainTextEdit[arenaCodeEditor="true"] {
                color: #d8d2c7;
                background-color: #111216;
                border: 1px solid rgba(226,193,126,38);
                border-radius: 10px;
                padding: 8px;
                selection-background-color: #725b36;
            }
            QPushButton[arenaDanger="true"] {
                color: #ffd7d2;
                background-color: rgba(180,65,58,34);
                border-color: rgba(238,111,100,90);
                font-weight: 600;
            }
            QPushButton[arenaDanger="true"]:hover {
                background-color: rgba(210,75,66,55);
                border-color: rgba(255,130,118,145);
            }
            QPushButton[arenaDanger="true"]:disabled {
                color: #776967;
                background-color: rgba(255,255,255,5);
                border-color: rgba(255,255,255,13);
            }
            QTabWidget#serverSettingsCategoryTabs::pane {
                top: -1px;
                background-color: rgba(14,15,17,112);
                border: 1px solid rgba(233,216,182,24);
                border-radius: 11px;
            }
            QTabWidget#serverSettingsCategoryTabs QTabBar::tab {
                min-width: 92px;
                padding: 5px 10px;
                margin: 2px 2px 4px 0;
                color: #b0aca4;
                border: 1px solid transparent;
                border-radius: 10px;
                font-size: 12px;
                font-weight: 600;
                background: transparent;
            }
            QTabWidget#serverSettingsCategoryTabs QTabBar::tab:hover {
                color: #ebe4d8;
                background-color: rgba(255,255,255,9);
            }
            QTabWidget#serverSettingsCategoryTabs QTabBar::tab:selected {
                color: #f0d7a4;
                background-color: rgba(198,158,91,24);
                border-color: rgba(216,178,111,72);
            }
            QGroupBox[arenaSettingsGroup="true"] {
                background-color: rgba(255,255,255,6);
                border-color: rgba(230,206,158,27);
                margin-top: 13px;
                padding-top: 9px;
            }
            QScrollArea[arenaSettingsScroll="true"] { border: 0; background: transparent; }

            QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }
            QScrollBar:vertical { background: transparent; border: none; width: 9px; margin: 2px; }
            QScrollBar:horizontal { background: transparent; border: none; height: 9px; margin: 2px; }
            QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
                background: #57534c;
                border-radius: 4px;
                min-height: 24px;
                min-width: 24px;
            }
            QScrollBar::handle:hover { background: #777066; }
            QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
            QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
            QStatusBar { color: #aaa49a; background: transparent; }

            QWidget[arenaGlassWindow="true"] { background: transparent; }
            QWidget#centralwidget, QWidget#centralWidget, QWizardPage { background: transparent; }
            QLabel#arenaWindowTitle { color: #d8d2c8; font-weight: 600; }
            QToolButton#arenaClose, QToolButton#arenaMinimize, QToolButton#arenaMaximize {
                padding: 0;
                border-radius: 7px;
                border: 1px solid rgba(0,0,0,70);
                color: transparent;
            }
            QToolButton#arenaClose { background-color: #ff5f57; }
            QToolButton#arenaMinimize { background-color: #febc2e; }
            QToolButton#arenaMaximize { background-color: #28c840; }
            QToolButton#arenaMaximize:disabled { background-color: #1f9f37; border-color: rgba(0,0,0,50); }
            QToolButton#arenaClose:hover { background-color: #ff756e; }
            QToolButton#arenaMinimize:hover { background-color: #ffca55; }
            QToolButton#arenaMaximize:hover { background-color: #4bd361; }
        )ARENA");
        arenaStyleSheet += QStringLiteral(R"ARENA(
            /* U020 showcase layout: panels, insets, hero action, status column, footer. */
            QStackedWidget#pagesWidget[arenaBare="true"] { background: transparent; border: 0; }
            QFrame[arenaPanel="true"], QTabWidget#pageTabs {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 rgba(40,41,45,218), stop:1 rgba(21,22,25,228));
                border: 1px solid rgba(233,216,182,36);
                border-radius: 15px;
            }
            QTabWidget#pageTabs::pane { border: 0; background: transparent; top: 0px; }
            QTabWidget#pageTabs::tab-bar { left: 10px; }
            QTabWidget#pageTabs QTabBar::tab {
                min-width: 88px;
                min-height: 22px;
                padding: 5px 9px;
                margin: 10px 6px 0 0;
                color: #cfc9be;
                background-color: rgba(255,255,255,8);
                border: 1px solid rgba(255,255,255,20);
                border-radius: 11px;
                font-size: 13px;
                font-weight: 600;
            }
            QTabWidget#pageTabs QTabBar::tab:hover {
                color: #f1eadc;
                background-color: rgba(255,255,255,14);
                border-color: rgba(226,193,126,72);
            }
            QTabWidget#pageTabs QTabBar::tab:selected {
                color: #fff1d2;
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 rgba(224,182,106,158), stop:1 rgba(146,105,52,128));
                border: 1px solid #e6c07a;
            }
            QFrame[arenaInset="true"] {
                background-color: rgba(9,10,12,112);
                border: 1px solid rgba(255,255,255,17);
                border-radius: 11px;
            }
            QLabel[arenaFieldLabel="true"] { color: #e4dfd5; }
            QLabel[arenaHint="true"] { background: transparent; }
            QFrame[arenaVDivider="true"] { border: 0; background-color: rgba(255,255,255,28); }

            QPushButton[arenaHero="true"] {
                padding: 0;
                border-radius: 13px;
                border: 1px solid #f6dfaa;
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 #f1d598, stop:0.55 #d7ae67, stop:1 #b3843f);
            }
            QPushButton[arenaHero="true"]:hover {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 #f7dea8, stop:0.55 #e0b973, stop:1 #c08f4b);
            }
            QPushButton[arenaHero="true"]:pressed { background: #a97b3f; }
            QPushButton[arenaHero="true"]:disabled { background: #3c3933; border-color: #555048; }
            QLabel[arenaHeroTitle="true"] { color: #211910; font-size: 16px; font-weight: 700; }
            QLabel[arenaHeroSubtitle="true"] { color: rgba(33,25,16,190); font-size: 11px; }
            QLabel[arenaHeroTitle="true"]:disabled, QLabel[arenaHeroSubtitle="true"]:disabled { color: #8d887e; }

            QLabel[arenaStatusDot="true"] {
                border-radius: 6px;
                background-color: #3fbf6e;
                border: 2px solid rgba(63,191,110,70);
            }
            QLabel#statusDotLabel { border-radius: 7px; }
            QLabel[arenaStatusDot="true"][arenaStatus="online"] { background-color: #5fe08f; border-color: rgba(95,224,143,95); }
            QLabel[arenaStatusDot="true"][arenaStatus="warn"] { background-color: #e2b75c; border-color: rgba(226,183,92,90); }
            QLabel[arenaStatusDot="true"][arenaStatus="busy"] { background-color: #78aee0; border-color: rgba(120,174,224,90); }
            QLabel[arenaStatusHeadline="true"] { color: #5fd88a; font-size: 15px; font-weight: 700; }
            QLabel[arenaStatusHeadline="true"][arenaStatus="online"] { color: #74e59d; }
            QLabel[arenaStatusHeadline="true"][arenaStatus="warn"] { color: #ebc476; }
            QLabel[arenaStatusHeadline="true"][arenaStatus="busy"] { color: #93c1ec; }
            QLabel[arenaStatusKey="true"] { color: #aaa59c; font-size: 12px; }
            QLabel[arenaStatusValue="true"] { color: #ece6da; font-size: 12px; }
            QLabel[arenaStatusValue="true"][arenaAccent="true"] { color: #e9c272; font-weight: 700; }
            QLabel[arenaTipTitle="true"] { color: #f0e9dc; font-size: 13px; font-weight: 650; }

            QFrame[arenaFooter="true"] { border-radius: 14px; }
            QLabel#versionLabel,
            QLabel#versionLabel[arenaStatus="online"],
            QLabel#versionLabel[arenaStatus="offline"] {
                padding: 0;
                border: 0;
                background: transparent;
                font-size: 13px;
                font-weight: 650;
            }
            QLabel#versionLabel[arenaStatus="offline"] { color: #5cc985; }
            QLabel#versionLabel[arenaStatus="online"] { color: #7be3a0; }
            QPushButton[arenaFooterButton="true"] {
                min-height: 24px;
                padding: 6px 14px;
                border-radius: 11px;
                font-size: 13px;
            }

            QListWidget#iconWidget::item:selected {
                color: #fff0cf;
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 rgba(218,176,102,125), stop:1 rgba(132,94,46,98));
                border: 1px solid rgba(236,197,124,170);
            }
            QListWidget#iconWidget::item { font-size: 13px; }
        )ARENA");
        arenaStyleSheet += QStringLiteral(R"ARENA(
            /* U021: Windows-style window controls, vertical settings navigation,
               quick panels and list surfaces. */
            QToolButton#arenaClose, QToolButton#arenaMinimize {
                color: #d8d2c8;
                background-color: transparent;
                border: 0;
                border-radius: 7px;
                font-size: 13px;
                padding: 0;
            }
            QToolButton#arenaMinimize:hover { background-color: rgba(255,255,255,22); color: #fff3da; }
            QToolButton#arenaMinimize:pressed { background-color: rgba(255,255,255,34); }
            QToolButton#arenaClose:hover { background-color: #c4392c; color: #fff5f2; }
            QToolButton#arenaClose:pressed { background-color: #a12d22; color: #fff5f2; }
            QLabel#arenaWindowTitle { color: #ded7cb; font-weight: 600; font-size: 13px; }

            QPushButton[arenaHero="true"] {
                min-height: 0px;
                padding: 0;
                border: 0;
                background: transparent;
            }

            QListWidget#serverSettingsNav, QListWidget#graphicsSettingsNav {
                background-color: rgba(9,10,12,110);
                border: 1px solid rgba(255,255,255,17);
                border-radius: 11px;
                padding: 6px;
                outline: 0;
                font-size: 13px;
            }
            QListWidget#serverSettingsNav::item, QListWidget#graphicsSettingsNav::item {
                color: #cfc9be;
                border: 1px solid transparent;
                border-radius: 9px;
                padding: 5px 8px;
                margin: 1px 0;
            }
            QListWidget#serverSettingsNav::item:hover, QListWidget#graphicsSettingsNav::item:hover {
                color: #f1eadc;
                background-color: rgba(255,255,255,12);
            }
            QListWidget#serverSettingsNav::item:selected, QListWidget#graphicsSettingsNav::item:selected {
                color: #fff1d2;
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                    stop:0 rgba(224,182,106,140), stop:1 rgba(146,105,52,96));
                border-color: rgba(236,197,124,150);
            }

            QListWidget#buildContentList {
                background-color: rgba(9,10,12,150);
                border: 1px solid rgba(255,255,255,20);
                border-radius: 10px;
                padding: 4px;
                outline: 0;
            }
            QListWidget#buildContentList::item {
                color: #e3ded4;
                border-radius: 7px;
                padding: 4px 6px;
            }
            QListWidget#buildContentList::item:selected {
                background-color: rgba(226,193,126,42);
                color: #f7ecd7;
            }
            QListWidget#buildContentList::indicator,
            QTreeView::indicator, QListView::indicator, QTableView::indicator {
                width: 16px;
                height: 16px;
                border: 1px solid #6b685f;
                border-radius: 4px;
                background-color: rgba(24,25,28,235);
            }
            QListWidget#buildContentList::indicator:checked,
            QTreeView::indicator:checked, QListView::indicator:checked, QTableView::indicator:checked {
                image: url(:/arena/arenaicons/check.png);
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 #efd18c, stop:1 #bd8c4a);
                border: 1px solid #f1d79d;
            }

            QDialog#BuildSetupDialog { background: transparent; }
            QWidget#advancedQuickPanel { background: transparent; }
            QWidget#advancedQuickPanel QCheckBox { color: #e1dcd2; }
        )ARENA");
        arenaStyleSheet += QStringLiteral(R"ARENA(
            /* U022: readable spin-box steppers instead of the tiny native arrows. */
            QSpinBox, QDoubleSpinBox {
                padding-right: 28px;
            }
            QSpinBox::up-button, QDoubleSpinBox::up-button,
            QSpinBox::down-button, QDoubleSpinBox::down-button {
                subcontrol-origin: border;
                width: 22px;
                height: 11px;
                margin: 2px 3px;
                border: 1px solid rgba(255,255,255,20);
                border-radius: 6px;
                background-color: rgba(255,255,255,12);
            }
            QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-position: top right; }
            QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-position: bottom right; }
            QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,
            QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover {
                background-color: rgba(226,193,126,46);
                border-color: rgba(226,193,126,120);
            }
            QSpinBox::up-button:pressed, QDoubleSpinBox::up-button:pressed,
            QSpinBox::down-button:pressed, QDoubleSpinBox::down-button:pressed {
                background-color: rgba(226,193,126,92);
            }
            QSpinBox::up-button:disabled, QDoubleSpinBox::up-button:disabled,
            QSpinBox::down-button:disabled, QDoubleSpinBox::down-button:disabled,
            QSpinBox::up-button:off, QDoubleSpinBox::up-button:off,
            QSpinBox::down-button:off, QDoubleSpinBox::down-button:off {
                background-color: rgba(255,255,255,5);
                border-color: rgba(255,255,255,10);
            }
            QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {
                image: url(:/arena/arenaicons/chevron-up.png);
                width: 9px;
                height: 9px;
            }
            QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {
                image: url(:/arena/arenaicons/chevron-down.png);
                width: 9px;
                height: 9px;
            }
            QSpinBox::up-arrow:disabled, QDoubleSpinBox::up-arrow:disabled,
            QSpinBox::up-arrow:off, QDoubleSpinBox::up-arrow:off {
                image: url(:/arena/arenaicons/chevron-up-muted.png);
            }
            QSpinBox::down-arrow:disabled, QDoubleSpinBox::down-arrow:disabled,
            QSpinBox::down-arrow:off, QDoubleSpinBox::down-arrow:off {
                image: url(:/arena/arenaicons/chevron-down-muted.png);
            }
        )ARENA");
        app.setStyleSheet(arenaStyleSheet);
    }
}

#endif
