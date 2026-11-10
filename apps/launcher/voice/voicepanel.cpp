#include "voicepanel.hpp"

#include <algorithm>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <SDL.h>

#include <components/config/launchersettings.hpp>

using namespace Launcher;

VoicePanel::VoicePanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("voicePanel"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);

    mEnabled = new QCheckBox(tr("Microphone"), this);
    mStateLabel = new QLabel(tr("Microphone is enabled by default. V toggles radio transmit in game; incoming player voice stays enabled."), this);
    mStateLabel->setWordWrap(true);
    mStateLabel->setProperty("arenaMuted", true);
    mPushToTalkKey = new QKeySequenceEdit(this);
    mCaptureDevice = new QComboBox(this);
    mCaptureDevice->setMinimumContentsLength(24);
    mCaptureDevice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    mRefreshDevices = new QPushButton(tr("Refresh"), this);
    mRefreshDevices->setToolTip(tr("Refresh the list of recording devices"));

    QHBoxLayout* deviceRow = new QHBoxLayout();
    deviceRow->setContentsMargins(0, 0, 0, 0);
    deviceRow->addWidget(mCaptureDevice, 1);
    deviceRow->addWidget(mRefreshDevices);

    QFormLayout* form = new QFormLayout();
    form->addRow(tr("Key"), mPushToTalkKey);
    form->addRow(tr("Microphone"), deviceRow);

    layout->addWidget(mEnabled);
    layout->addWidget(mStateLabel);
    layout->addLayout(form);

    connect(mEnabled, &QCheckBox::toggled, this, [this](bool on)
    {
        updateEnabledState();
        emit enabledChanged(on);
    });
    connect(mRefreshDevices, &QPushButton::clicked, this, [this]()
    {
        refreshCaptureDevices(captureDevice());
    });

    refreshCaptureDevices();
    updateEnabledState();
}

void VoicePanel::loadSettings(Config::LauncherSettings& settings)
{
    mEnabled->setChecked(settings.value(QStringLiteral("Voice/enabled"),
        QStringLiteral("true")).compare(QLatin1String("true"), Qt::CaseInsensitive) == 0);
    mPushToTalkKey->setKeySequence(QKeySequence(
        settings.value(QStringLiteral("Voice/pushToTalkKey"), QStringLiteral("V"))));
    refreshCaptureDevices(settings.value(QStringLiteral("Voice/captureDevice")));
    updateEnabledState();
}

void VoicePanel::saveSettings(Config::LauncherSettings& settings)
{
    settings.setValue(QStringLiteral("Voice/enabled"),
        mEnabled->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    settings.setValue(QStringLiteral("Voice/pushToTalkKey"), pushToTalkKey());
    settings.setValue(QStringLiteral("Voice/captureDevice"), captureDevice());
}

void VoicePanel::setVoiceAvailable(bool available)
{
    mAvailable = available;
    if (!available)
        setStateText(tr("Microphone transmit is disabled on this server"));
    updateEnabledState();
}

void VoicePanel::setGameRunning(bool running)
{
    mGameRunning = running;
    setStateText(running ? tr("In-game microphone uses the selected recording device; incoming player voice stays enabled.") : tr("Microphone is enabled by default. V toggles radio transmit in game; incoming player voice stays enabled."));
    updateEnabledState();
}

void VoicePanel::setStateText(const QString& text)
{
    mStateLabel->setText(text);
}

bool VoicePanel::voiceEnabled() const
{
    return mEnabled != nullptr && mEnabled->isChecked();
}

QString VoicePanel::pushToTalkKey() const
{
    if (mPushToTalkKey == nullptr)
        return QStringLiteral("V");
    const QString value = mPushToTalkKey->keySequence().toString().trimmed();
    return value.isEmpty() ? QStringLiteral("V") : value;
}

QString VoicePanel::captureDevice() const
{
    if (mCaptureDevice == nullptr || mCaptureDevice->currentIndex() < 0)
        return QString();
    return mCaptureDevice->currentData().toString();
}

void VoicePanel::refreshCaptureDevices(const QString& preferred)
{
    if (mCaptureDevice == nullptr)
        return;

    const QString wanted = preferred.trimmed();
    mCaptureDevice->blockSignals(true);
    mCaptureDevice->clear();
    mCaptureDevice->addItem(tr("System default"), QString());

    const bool audioWasReady = (SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0;
    bool ownAudioInit = false;
    if (!audioWasReady)
    {
        SDL_SetMainReady();
        ownAudioInit = SDL_InitSubSystem(SDL_INIT_AUDIO) == 0;
    }

    if (audioWasReady || ownAudioInit)
    {
        const int count = SDL_GetNumAudioDevices(SDL_TRUE);
        for (int i = 0; i < count; ++i)
        {
            const char* rawName = SDL_GetAudioDeviceName(i, SDL_TRUE);
            if (rawName == nullptr || *rawName == '\0')
                continue;
            const QString name = QString::fromUtf8(rawName);
            if (mCaptureDevice->findData(name) < 0)
                mCaptureDevice->addItem(name, name);
        }
        if (ownAudioInit)
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    int selected = wanted.isEmpty() ? 0 : mCaptureDevice->findData(wanted);
    if (selected < 0 && !wanted.isEmpty())
    {
        // Preserve a disconnected USB/Bluetooth microphone choice. VoiceChat
        // falls back to the system default for this run and will use this
        // device again automatically when it becomes available.
        mCaptureDevice->insertItem(1, tr("Unavailable: %1").arg(wanted), wanted);
        selected = 1;
    }
    mCaptureDevice->setCurrentIndex(std::max(0, selected));
    mCaptureDevice->blockSignals(false);
}

void VoicePanel::updateEnabledState()
{
    // While tes3mp is running it exclusively owns microphone/PTT. Disabling the
    // launcher voice controls prevents a second capture session from being
    // opened accidentally by the launcher at the same time.
    mEnabled->setEnabled(mAvailable && !mGameRunning);
    mPushToTalkKey->setEnabled(mAvailable && mEnabled->isChecked() && !mGameRunning);
    mCaptureDevice->setEnabled(mAvailable && !mGameRunning);
    mRefreshDevices->setEnabled(mAvailable && !mGameRunning);
}
