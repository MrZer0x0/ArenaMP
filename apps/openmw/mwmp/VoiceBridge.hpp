#ifndef ARENAMP_VOICEBRIDGE_HPP
#define ARENAMP_VOICEBRIDGE_HPP

// ArenaMP U025 — мост «движок → лаунчер-коммутатор».
//
// Движок больше не занимается звуком: он 10 раз в секунду отдаёт коммутатору
// состояние слушателя и получает обратно список говорящих для индикатора в HUD.
// Транспорт — loopback UDP, одинаковый на ПК и на Android (там на другом конце
// не лаунчер, а VoiceService), поэтому здесь нет ни одной платформенной ветки.
//
// Ставится рядом с существующим mwmp::VoiceChat (Y033). Пока мост жив,
// VoiceChat обязан оставаться в режиме «только индикация» и не открывать
// устройство захвата — иначе два процесса подерутся за микрофон.

#include <cstdint>
#include <string>
#include <vector>

namespace mwmp
{
    struct VoiceSpeaker
    {
        std::uint32_t ssrc = 0;
        std::string name;      // "???" для скрытых (флаг ANON)
        float level = 0.f;     // 0..1, для полоски громкости
        bool anonymous = false;
    };

    class VoiceBridge
    {
    public:
        VoiceBridge();
        ~VoiceBridge();

        /// Читает [Voice] bridgePort / bridgeToken из настроек клиента.
        /// Их пишет лаунчер перед запуском игры, на один запуск.
        void configure(int bridgePort, const std::string& bridgeToken);

        /// Тикет приходит игровым пакетом от сервера при входе в мир и
        /// пересылается коммутатору как есть.
        void setVoiceTicket(const std::string& ticket);

        void update(float dt);

        bool connected() const { return mConnected; }
        const std::vector<VoiceSpeaker>& speakers() const { return mSpeakers; }

        /// PTT прокидывается из mwinput: клавиша из [Voice] pushToTalkKey.
        void setPushToTalk(bool pressed) { mPushToTalk = pressed; }
        void setMuted(bool muted) { mMuted = muted; }

    private:
        void sendState();
        void receive();

        int mSocket = -1;
        int mPort = 0;
        std::string mToken;
        std::string mTicket;
        bool mTicketSent = false;
        bool mConnected = false;
        bool mPushToTalk = false;
        bool mMuted = false;
        float mSendAccumulator = 0.f;      // 100 мс
        float mSilenceAccumulator = 0.f;   // 3 с без ответа = коммутатора нет
        std::vector<VoiceSpeaker> mSpeakers;
    };
}
#endif
