#ifndef BUILDSETUPDIALOG_H
#define BUILDSETUPDIALOG_H

#include <QDialog>
#include <QString>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace Launcher
{
    /// U021: the former standalone ArenaMP Setup Wizard, folded into the
    /// launcher. It selects the build folder, reads an existing build.ini or
    /// lets the player name a new build and order its content files, and hands
    /// the result back to MainDialog, which writes the configuration.
    ///
    /// Grass/groundcover plug-ins are detected by name and enabled as
    /// groundcover automatically - there is no manual switch for them.
    class BuildSetupDialog : public QDialog
    {
        Q_OBJECT

    public:
        struct Result
        {
            QString dataPath;
            QString buildName;
            QString language;
            QStringList content;
            QStringList groundcover;
            QStringList archives;
            bool manifestExists = false;
            QString manifestPath;
            /// True when the launcher must apply the chosen language itself:
            /// a new build, or an old build.ini without a language field.
            bool applyLanguage = false;
        };

        explicit BuildSetupDialog(QWidget* parent = nullptr);

        /// Pre-selects a folder, for "change build" from inside the launcher.
        void setInitialPath(const QString& dataPath);

        Result result() const { return mResult; }

        /// Data Files directory for a folder the player picked: the folder
        /// itself when it holds game files, otherwise its "Data Files" child.
        static QString resolveDataFilesDirectory(const QString& selectedPath);
        static bool isGroundcoverCandidate(const QString& fileName);
        static QStringList archivesForDirectory(const QString& dataPath);
        /// win1251 for Russian, win1250 for Polish, win1252 otherwise.
        static QString encodingForLanguage(const QString& language);
        /// Language preselected from the system locale.
        static QString defaultLanguage();

    private slots:
        void browseForFolder();
        void pathEdited();
        void moveSelectionUp();
        void moveSelectionDown();
        void accept() override;

    private:
        void scanFolder(const QString& dataPath);
        void showManifestBuild(const QString& dataPath, const QString& manifestPath);
        void showNewBuild(const QString& dataPath);
        void setStatus(const QString& text, const QString& state);
        void moveSelection(int delta);
        QStringList checkedContent() const;

        QLineEdit* mPathEdit;
        QPushButton* mBrowseButton;
        QLabel* mStatusIcon;
        QLabel* mStatusLabel;
        QLabel* mStatusDetailLabel;
        QLineEdit* mNameEdit;
        QComboBox* mLanguageCombo;
        QLabel* mLanguageHintLabel;
        QLabel* mContentHintLabel;
        QListWidget* mContentList;
        QLabel* mGroundcoverLabel;
        QPushButton* mUpButton;
        QPushButton* mDownButton;
        QPushButton* mAcceptButton;

        Result mResult;
        bool mValid;
    };
}

#endif
