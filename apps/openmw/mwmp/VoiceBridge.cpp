// ArenaMP U025 — скелет моста движок ↔ коммутатор.
//
// Точки подключения в существующем дереве:
//   Main.cpp (рядом с mVoiceChat):
//     mVoiceBridge = new VoiceBridge();                                  // ctor
//     mVoiceBridge->configure(manager.getInt("bridgePort", "Voice"),
//                             manager.getString("bridgeToken", "Voice")); // init()
//     get().getVoiceBridge()->update(dt);                                 // frame()
//     delete mVoiceBridge;                                                // dtor
//   mwinput/keyboardmanager.cpp: keyPressed/keyReleased для pushToTalkKey
//     -> setPushToTalk(true/false)
//   GUIChat: speakers() -> индикатор «говорит N игроков»
//   обработчик игрового пакета с тикетом -> setVoiceTicket(...)

#include "VoiceBridge.hpp"

#include <cstring>

#include <components/openmw-mp/arenavoice.hpp>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  define ARENA_CLOSESOCKET closesocket
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#  define ARENA_CLOSESOCKET ::close
#endif

using namespace mwmp;
using namespace ArenaVoice;

namespace
{
    // Локальный формат моста — НЕ протокол AVX1. Наружу он не ходит,
    // поэтому достаточно того же Writer/Reader и общего заголовка.
    constexpr std::uint8_t BRIDGE_HELLO = 0xA0;   // токен + тикет + имя
    constexpr std::uint8_t BRIDGE_STATE = 0xA1;   // позиция, поворот, ячейка, PTT
    constexpr std::uint8_t BRIDGE_PEERS = 0xA2;   // ответ коммутатора
}

VoiceBridge::VoiceBridge() = default;

VoiceBridge::~VoiceBridge()
{
    if (mSocket >= 0)
        ARENA_CLOSESOCKET(mSocket);
}

void VoiceBridge::configure(int bridgePort, const std::string& bridgeToken)
{
    mPort = bridgePort;
    mToken = bridgeToken;
    if (mPort <= 0 || mToken.empty())
        return;

    mSocket = static_cast<int>(::socket(AF_INET, SOCK_DGRAM, 0));
    if (mSocket < 0)
        return;
#ifndef _WIN32
    ::fcntl(mSocket, F_SETFL, O_NONBLOCK);
#endif
    // TODO: connect() на 127.0.0.1:mPort, чтобы дальше обходиться send/recv
    // и не принимать датаграммы от посторонних процессов.
}

void VoiceBridge::setVoiceTicket(const std::string& ticket)
{
    mTicket = ticket;
    mTicketSent = false;
}

void VoiceBridge::update(float dt)
{
    if (mSocket < 0)
        return;

    receive();

    mSendAccumulator += dt;
    if (mSendAccumulator < 0.1f)          // 10 Гц
        return;
    mSendAccumulator = 0.f;

    if (!mTicketSent && !mTicket.empty())
    {
        // TODO: собрать BRIDGE_HELLO {token, ticket, playerName, serverAddr, serverPort}
        // и отправить; коммутатор по нему поднимает сессию ArenaVoice.
        mTicketSent = true;
    }

    sendState();

    mSilenceAccumulator += 0.1f;
    if (mSilenceAccumulator > 3.f && mConnected)
    {
        mConnected = false;
        mSpeakers.clear();
        // TODO: сообщить VoiceChat, что fallback-режим снова разрешён
        // (или оставить голос выключенным — по настройке).
    }
}

void VoiceBridge::sendState()
{
    // TODO: взять из MWBase::Environment:
    //   player.getRefData().getPosition().pos[0..2]  -> x, y, z в юнитах
    //   rot[2] -> yaw, rot[0] -> pitch (радианы)
    //   world->getPlayerPtr().getCell()->getCell()->mName -> cellHash(name)
    //   cell->isExterior() -> флаг интерьера
    // Канал берётся из GUIChat::getChatChannel() — голос следует за текстовым
    // каналом (шёпот/обычный/крик), отдельной настройки не заводим.
    Writer writer;
    writeHeader(writer, BRIDGE_STATE, 0);
    State state;
    state.ptt = mPushToTalk ? 1 : 0;
    state.muted = mMuted ? 1 : 0;
    (void)state;
    // TODO: writer + makeState(...) -> ::send(mSocket, ...)
}

void VoiceBridge::receive()
{
    char buffer[sMaxDatagram];
    for (;;)
    {
        const auto received = ::recv(mSocket, buffer, sizeof(buffer), 0);
        if (received <= 0)
            return;

        Reader reader(buffer, static_cast<std::size_t>(received));
        Header header;
        if (!readHeader(reader, header) || header.type != BRIDGE_PEERS)
            continue;

        mConnected = true;
        mSilenceAccumulator = 0.f;
        mSpeakers.clear();

        const std::size_t count = reader.u8();
        for (std::size_t i = 0; i < count && reader.ok(); ++i)
        {
            VoiceSpeaker speaker;
            speaker.ssrc = reader.u32();
            speaker.level = static_cast<float>(reader.u8()) / 255.f;
            speaker.anonymous = (reader.u8() & AUDIO_ANON) != 0;
            speaker.name = reader.text(sMaxNickLength);
            if (speaker.anonymous)
                speaker.name = "???";
            mSpeakers.push_back(speaker);
        }
    }
}
