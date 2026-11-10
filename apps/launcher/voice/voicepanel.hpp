#ifndef ARENAMP_VOICEPANEL_HPP
#define ARENAMP_VOICEPANEL_HPP

// ArenaMP U026 — launcher controls for the existing native in-game VoiceChat.
// The launcher intentionally does not open its own microphone/audio pipeline.

#include <QWidget>
#include <QString>

class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QPushButton;

namespace Config { class LauncherSettings; }

namespace Launcher
{
    class VoicePanel : public QWidget
    {
        Q_OBJECT

    public:
        explicit VoicePanel(QWidget* parent = nullptr);

        void loadSettings(Config::LauncherSettings& settings);
        void saveSettings(Config::LauncherSettings& settings);
        void setVoiceAvailable(bool available);
        void setGameRunning(bool running);
        void setStateText(const QString& text);

        bool voiceEnabled() const;
        QString pushToTalkKey() const;
        QString captureDevice() const;

    signals:
        void enabledChanged(bool enabled);

    private:
        void updateEnabledState();
        void refreshCaptureDevices(const QString& preferred = QString());

        QCheckBox* mEnabled = nullptr;
        QKeySequenceEdit* mPushToTalkKey = nullptr;
        QComboBox* mCaptureDevice = nullptr;
        QPushButton* mRefreshDevices = nullptr;
        QLabel* mStateLabel = nullptr;
        bool mAvailable = true;
        bool mGameRunning = false;
    };
}
#endif
