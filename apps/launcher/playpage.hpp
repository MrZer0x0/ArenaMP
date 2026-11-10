#ifndef PLAYPAGE_H
#define PLAYPAGE_H

#include "ui_playpage.h"

#include <QString>
#include <QJsonObject>
#include <functional>

class QWidget;
class QLineEdit;
class QLabel;
class QComboBox;
class QPushButton;
class QCheckBox;

namespace Launcher
{
    class ServerStatusQuery;
    class PlayPage : public QWidget, private Ui::PlayPage
    {
        Q_OBJECT

    public:
        explicit PlayPage(QWidget *parent = nullptr);

        void setBuildName(const QString& name);
        void setServerAddress(const QString& addr);
        void setServerPort(const QString& port);
        void setServerConsoleWidget(QWidget* widget);
        void setAutoStartServer(bool enabled);
        void setAutoRestartServer(bool enabled);
        void setServerRunning(bool running, const QString& address = QString(), const QString& port = QString(), bool managed = true);
        void setBuildManifestComplete(bool complete);
        void setPlayButtonState(const QString& text, bool enabled);
        // U020 status column: launcher/update state and detected hardware.
        void setUpdateState(bool checking, bool updateAvailable);
        void setHardwareInfo(const QString& gpuName, const QString& gpuDetail, int logicalThreads);
        /// U022: hosts a client option that used to live on the removed
        /// Settings page. The widget keeps its owner, only its place changes.
        void addLaunchOption(QWidget* widget);

        void setAlternativeServer(const QString& address, const QString& port, bool enabled);
        bool alternativeServer() const;
        QString alternativeAddress() const;
        QString alternativePort() const;
        bool validateMasterSelection();
        QString takeMasterPassword() { QString value=mMasterPassword; mMasterPassword.clear(); return value; }
        void setProjectUrl(const QString& url);
        QString buildName() const;
        QString serverAddress() const;
        QString serverPort() const;
        QString hostBindAddress() const;
        void setHostBindAddress(const QString& address);
        bool autoStartServer() const;
        bool autoRestartServer() const;
        void setEnforceDataFiles(bool enabled);
        bool enforceDataFiles() const;

       void switchToServerConsoleTab();
       void loadServerSettings();
       bool saveServerSettings();

        // X041: XP progression, moved here from the removed Advanced -> Arena
        // Settings tab. These are client-side defaults in settings.cfg; a
        // connected ArenaMP server stays authoritative and may override them.
        void loadXpProgressionSettings();
        bool saveXpProgressionSettings();

    signals:
        void playButtonClicked();
        void serverButtonClicked();
        void stopServerButtonClicked();
        void autoStartServerChanged(bool enabled);
        void autoRestartServerChanged(bool enabled);
        void updateHashesRequested();
        void clearServerCellsRequested();
        void resetServerDataRequested();
        void changeBuildRequested();

    private slots:
        void slotPlayClicked();
        void slotServerClicked();
        void slotStopServerClicked();
        void slotAutoStartServerToggled(bool enabled);
        void slotRefreshHostInterfaces();
        void slotUpdateHashesClicked();
        void slotEnforceRequiredToggled(bool enabled);
        void slotServerModeChanged(int index);
        void slotReloadServerSettings();
        void slotSaveServerSettings();
        void slotApplyFormToRawConfig();
        void slotSyncFormFromRawConfig();
        void slotServerSettingsModeChanged(int index);
        void slotXpRatePresetChanged(int index);
        void slotXpGainMultiplierChanged(double value);

    private:
        QCheckBox* mAlternativeServer;
        QLineEdit* mAlternativeAddress;
        QLineEdit* mAlternativePort;
        QPushButton* mProjectLink;
        QString mProjectUrl;
        QString mMasterPassword;
        QJsonObject mSelectedMasterServer;
        bool mMasterSelectionConnected = false;
        bool mMasterPreparing = false;
        std::function<void()> mPrepareMaster;
        void rememberMasterServer(const QJsonObject& server, const QString& base);

        QString serverConfigPath() const;
        QString persistentServerConfigPath() const;
        bool writeServerConfigFile(const QString& path, const QString& text, QString* errorMessage) const;
        QString replaceRawValue(const QString& text, const QString& key, const QString& value) const;
        QString updatedConfigFromForm(const QString& input) const;
        void populateFormFromConfig(const QString& text);
        void setServerSettingsStatus(const QString& text, bool isError = false);
        void refreshHostInterfaces(const QString& preferredAddress = QString());
        void updateHostModeUi(bool enabled);
        void applyServerModePreset(int index);
        void updateStatusPanel();
        QWidget* mEmbeddedServerConsole;
        bool mSyncingServerSettingsTabs;
        QLabel* mHostInterfaceLabel;
        QComboBox* mHostInterfaceCombo;
        QPushButton* mRefreshHostInterfacesButton;
        QPushButton* mUpdateHashesButton;
        QCheckBox* mEnforceRequiredCheckBox;
        QLabel* mServerModeLabel;
        QComboBox* mServerModeCombo;
        QPushButton* mClearCellsButton;
        QPushButton* mResetServerButton;
        bool mSyncingXpControls;

        ServerStatusQuery* mStatusQuery = nullptr;
        QLabel* mRemoteStatus = nullptr;
        QLabel* mRemotePlayers = nullptr;
        QLabel* mRemoteUptime = nullptr;
        QLabel* mRemoteEndpoint = nullptr;
        bool mServerRunning;
        bool mUpdateChecking;
        bool mUpdateAvailable;
        QString mRunningAddress;
        QString mRunningPort;
        QString mGpuName;
        QString mGpuDetail;
        int mLogicalThreads;
    };
}
#endif
