#ifndef ARENAMP_VOICECOMMUTATOR_HPP
#define ARENAMP_VOICECOMMUTATOR_HPP

// ArenaMP U025 — коммутатор голоса в лаунчере.
//
// Лаунчер остаётся жить после нажатия «Играть» и работает телефонной
// станцией: он владеет микрофоном и воспроизведением, держит сессию
// ArenaVoice и коммутирует её между лобби и игрой. Движок звук не трогает —
// он только сообщает по мосту, где находится слушатель и нажат ли PTT.
//
// Транспорт один на оба режима: свой UDP-порт (игровой + 1), обычные
// сокеты, RakNet и веб-сигналинг не используются вообще.
//
// Почему в лаунчере, а не в движке:
//   * лаунчер уже линкуется с OpenAL (CMakeLists: ${OPENAL_LIBRARY},
//     utils/openalutil.cpp), то есть 3D-панорама и HRTF достаются бесплатно;
//   * голос переживает перезапуск игры и работает в лобби до входа в мир;
//   * на Android ровно ту же роль играет foreground service, поэтому
//     схема одинаковая на обеих платформах.

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

#include <components/openmw-mp/arenavoice.hpp>

class QTimer;
class QUdpSocket;

namespace Launcher
{
    enum class VoiceState
    {
        Off,         // голос выключен в настройках
        Lobby,       // комнаты веб-API (общая комната)
        Handoff,     // нажата «Играть»: из общей комнаты вышли, ждём тикет
        InGame       // proximity через ArenaVoice
    };

    struct VoiceSettings
    {
        bool enabled = false;
        bool pushToTalk = true;
        bool lobbyAutoJoin = false;
        bool groupRadio = true;
        float micGain = 1.f;
        float outputVolume = 0.8f;
        float vadThresholdDb = -45.f;
        QString inputDevice;         // пусто = устройство по умолчанию
        QString outputDevice;
    };

    class VoiceCommutator : public QObject
    {
        Q_OBJECT

    public:
        explicit VoiceCommutator(QObject* parent = nullptr);
        ~VoiceCommutator() override;

        void applySettings(const VoiceSettings& settings);
        const VoiceSettings& settings() const { return mSettings; }
        VoiceState state() const { return mState; }

        /// Мост для движка. Возвращает выбранный порт: его лаунчер пишет
        /// в [Voice] bridgePort клиентского конфига вместе с одноразовым
        /// bridgeToken перед стартом игры.
        int startBridge(const QString& token);
        void stopBridge();

        /// Лобби: та же сессия ArenaVoice, только без позиционирования.
        /// Тикет приходит от ArenaLink (страница чата), веб-сигналинга нет.
        void startLobbyVoice(const QString& host, quint16 voicePort, const QByteArray& ticket);
        void stopLobbyVoice();
        /// Тикет игровой сессии: его же присылает ArenaLink при запуске игры,
        /// либо движок по мосту, если лаунчер подключился позже.
        void setTicket(const QByteArray& ticket);

        /// «Играть» нажата: общую комнату покидаем немедленно, микрофон
        /// не отпускаем (на Android иначе пришлось бы пересоздавать FGS).
        void beginHandoff(const QString& serverHost, quint16 serverPort);

        void setMuted(bool muted);
        bool muted() const { return mMuted; }

    signals:
        void stateChanged(VoiceState state);
        void speakersChanged(const QVector<quint32>& ssrcs);
        void errorOccurred(const QString& message);

    private slots:
        void slotVoiceDatagram();      // ArenaVoice: AUDIO_DOWN / PEERS / CONTROL
        void slotBridgeDatagram();     // движок: тикет и состояние слушателя
        void slotCaptureTick();        // 20 мс: захват → Opus → AUDIO_UP
        void slotKeepAlive();          // PING + повторный HELLO при смене сети

    private:
        struct Peer;                   // ssrc → OpenAL source + джиттер-буфер

        void setState(VoiceState state);
        void sendHello();
        void openCapture();
        void closeCapture();
        void openPlayback();
        void closePlayback();
        /// Ставит источник относительно слушателя и отдаёт панораму,
        /// дистанцию и HRTF самому OpenAL Soft.
        void placeSource(Peer& peer, float azimuth, float distanceMeters, float gain);

        VoiceSettings mSettings;
        VoiceState mState = VoiceState::Off;

        QUdpSocket* mVoiceSocket = nullptr;
        QUdpSocket* mBridgeSocket = nullptr;
        QTimer* mCaptureTimer = nullptr;
        QTimer* mKeepAliveTimer = nullptr;

        QString mServerHost;
        quint16 mServerPort = 0;
        QString mBridgeToken;
        QByteArray mTicket;
        quint32 mSession = 0;
        quint32 mSsrc = 0;
        bool mServerMix = false;
        bool mMuted = false;
        bool mPushToTalkDown = false;

        // TODO: OpenAL-контекст захвата (alcCaptureOpenDevice, 48 кГц mono s16,
        // окно 960 сэмплов), OpaqueOpusEncoder/Decoder, адаптивный джиттер-буфер
        // 40..120 мс на каждый ssrc.
        QHash<quint32, Peer*> mPeers;
    };
}
#endif
