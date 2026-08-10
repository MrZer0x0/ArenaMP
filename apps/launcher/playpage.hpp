#ifndef PLAYPAGE_H
#define PLAYPAGE_H

#include "ui_playpage.h"

#include <QString>

class QWidget;

namespace Launcher
{
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
        void setVanillaServerCompatibility(bool enabled);
        void setHideChatHistory(bool enabled);
        void setServerRunning(bool running, const QString& address = QString(), const QString& port = QString(), bool managed = true);
        void setBuildManifestComplete(bool complete);

        QString buildName() const;
        QString serverAddress() const;
        QString serverPort() const;
        bool autoStartServer() const;
        bool autoRestartServer() const;
        bool vanillaServerCompatibility() const;
        bool hideChatHistory() const;

       void switchToServerConsoleTab();
       void loadServerSettings();
       bool saveServerSettings();

    signals:
        void playButtonClicked();
        void serverButtonClicked();
        void stopServerButtonClicked();
        void autoStartServerChanged(bool enabled);
        void autoRestartServerChanged(bool enabled);
        void vanillaServerCompatibilityChanged(bool enabled);
        void hideChatHistoryChanged(bool enabled);

    private slots:
        void slotPlayClicked();
        void slotServerClicked();
        void slotStopServerClicked();
        void slotAutoStartServerToggled(bool enabled);
        void slotReloadServerSettings();
        void slotSaveServerSettings();
        void slotApplyFormToRawConfig();
        void slotSyncFormFromRawConfig();
        void slotServerSettingsModeChanged(int index);

    private:
        QString serverConfigPath() const;
        QString persistentServerConfigPath() const;
        bool writeServerConfigFile(const QString& path, const QString& text, QString* errorMessage) const;
        QString replaceRawValue(const QString& text, const QString& key, const QString& value) const;
        QString updatedConfigFromForm(const QString& input) const;
        void populateFormFromConfig(const QString& text);
        void setServerSettingsStatus(const QString& text, bool isError = false);
        QWidget* mEmbeddedServerConsole;
        bool mSyncingServerSettingsTabs;
    };
}
#endif
