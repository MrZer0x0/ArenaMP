// ArenaMP U025 — скелет коммутатора.
//
// Реализован конечный автомат и сетевая часть; звук помечен TODO,
// потому что тянет новую зависимость (libopus) и работу с OpenAL-захватом.
//
// Сборка: в apps/launcher/CMakeLists.txt добавить
//   voice/voicecommutator.cpp в LAUNCHER,
//   voice/voicecommutator.hpp в LAUNCHER_HEADER и LAUNCHER_HEADER_MOC,
//   find_package(Opus) / pkg_check_modules(OPUS opus) и target_link_libraries.
// Серверу libopus не нужен: в режиме mode = client он байты не декодирует.

#include "voicecommutator.hpp"

#include <cstring>

#include <QDateTime>
#include <QHostAddress>
#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>

using namespace Launcher;
using namespace ArenaVoice;

namespace
{
    constexpr int sKeepAliveMs = 2000;
    constexpr quint8 BRIDGE_HELLO = 0xA0;
    constexpr quint8 BRIDGE_STATE = 0xA1;
    constexpr quint8 BRIDGE_PEERS = 0xA2;
}

struct VoiceCommutator::Peer
{
    quint32 ssrc = 0;
    quint32 source = 0;         // alSource
    quint16 lastSeq = 0;
    qint64 lastFrameMs = 0;
    float smoothedGain = 0.f;   // сглаживание за 60..80 мс, иначе щелчки
    float smoothedAzimuth = 0.f;
    bool anonymous = false;
    // TODO: OpusDecoder* + кольцевой джиттер-буфер кадров по 20 мс.
};

VoiceCommutator::VoiceCommutator(QObject* parent)
    : QObject(parent)
    , mVoiceSocket(new QUdpSocket(this))
    , mBridgeSocket(new QUdpSocket(this))
    , mCaptureTimer(new QTimer(this))
    , mKeepAliveTimer(new QTimer(this))
{
    connect(mVoiceSocket, &QUdpSocket::readyRead, this, &VoiceCommutator::slotVoiceDatagram);
    connect(mBridgeSocket, &QUdpSocket::readyRead, this, &VoiceCommutator::slotBridgeDatagram);

    mCaptureTimer->setInterval(sFrameMs);
    connect(mCaptureTimer, &QTimer::timeout, this, &VoiceCommutator::slotCaptureTick);

    mKeepAliveTimer->setInterval(sKeepAliveMs);
    connect(mKeepAliveTimer, &QTimer::timeout, this, &VoiceCommutator::slotKeepAlive);
}

VoiceCommutator::~VoiceCommutator()
{
    closeCapture();
    closePlayback();
    qDeleteAll(mPeers);
}

void VoiceCommutator::applySettings(const VoiceSettings& settings)
{
    const bool wasEnabled = mSettings.enabled;
    mSettings = settings;

    if (!settings.enabled)
    {
        stopLobbyVoice();
        closeCapture();
        closePlayback();
        setState(VoiceState::Off);
        return;
    }
    if (!wasEnabled)
    {
        openPlayback();
        if (settings.lobbyAutoJoin)
        {
            // Намеренно НЕ входим в общую комнату молча: включение голоса
            // не должно внезапно открывать эфир. Автовход — только если
            // игрок сам поставил галочку.
        }
        setState(VoiceState::Lobby);
    }
}

int VoiceCommutator::startBridge(const QString& token)
{
    mBridgeToken = token;
    if (!mBridgeSocket->bind(QHostAddress::LocalHost, 0))
    {
        emit errorOccurred(tr("Could not open the local voice bridge"));
        return 0;
    }
    return mBridgeSocket->localPort();
}

void VoiceCommutator::stopBridge()
{
    mBridgeSocket->close();
    mBridgeToken.clear();
    mTicket.clear();
}

void VoiceCommutator::beginHandoff(const QString& serverHost, quint16 serverPort)
{
    mServerHost = serverHost;
    // Голос слушает на игровом порту + 1, если сервер не переопределил.
    mServerPort = static_cast<quint16>(serverPort + 1);

    // Общую комнату покидаем сразу: игрок не должен оказаться слышен
    // одновременно в лобби и в мире.
    stopLobbyVoice();
    setState(VoiceState::Handoff);
    // Микрофон не закрываем: ждём тикет от движка (до 60 с).
}

void VoiceCommutator::startLobbyVoice(const QString& host, quint16 voicePort, const QByteArray& ticket)
{
    // Лобби и игра — одна и та же сессия ArenaVoice, отличается только
    // scope тикета: сервер не применяет к лобби ни дистанцию, ни панораму.
    // Отдельного стека для лобби нет: один код, один порт, один кодек.
    mServerHost = host;
    mServerPort = voicePort;
    setTicket(ticket);
    openPlayback();
    sendHello();
    setState(VoiceState::Lobby);
}

void VoiceCommutator::stopLobbyVoice()
{
    if (mSession != 0)
    {
        Writer writer;
        writeHeader(writer, TYPE_BYE, mSession);
        const std::string packet = writer.data();
        mVoiceSocket->writeDatagram(packet.data(), static_cast<qint64>(packet.size()),
            QHostAddress(mServerHost), mServerPort);
    }
    mSession = 0;
    qDeleteAll(mPeers);
    mPeers.clear();
}

void VoiceCommutator::setTicket(const QByteArray& ticket)
{
    if (ticket.size() != static_cast<int>(sTicketSize))
        return;
    mTicket = ticket;
}

void VoiceCommutator::setMuted(bool muted)
{
    mMuted = muted;
    // Локальный mute — только прекращение отправки. Приём продолжается,
    // иначе игрок «глохнет» вместе с микрофоном и не понимает почему.
}

void VoiceCommutator::setState(VoiceState state)
{
    if (mState == state)
        return;
    mState = state;
    emit stateChanged(state);
}

void VoiceCommutator::sendHello()
{
    if (mTicket.size() != static_cast<int>(sTicketSize) || mServerHost.isEmpty())
        return;

    Hello hello;
    std::memcpy(hello.ticket, mTicket.constData(), sTicketSize);
    hello.caps = CAP_CLIENT_MIX | CAP_HRTF;
    hello.nick.clear();                  // имя сервер знает по тикету

    const std::string packet = makeHello(0, hello);
    mVoiceSocket->writeDatagram(packet.data(), static_cast<qint64>(packet.size()),
        QHostAddress(mServerHost), mServerPort);
}

void VoiceCommutator::slotVoiceDatagram()
{
    while (mVoiceSocket->hasPendingDatagrams())
    {
        const QNetworkDatagram datagram = mVoiceSocket->receiveDatagram(static_cast<qint64>(sMaxDatagram));
        const QByteArray bytes = datagram.data();
        Reader reader(bytes.constData(), static_cast<std::size_t>(bytes.size()));
        Header header;
        if (!readHeader(reader, header))
            continue;

        switch (header.type)
        {
            case TYPE_WELCOME:
            {
                Welcome welcome;
                if (!parseWelcome(reader, welcome))
                    break;
                mSession = header.session;
                mSsrc = welcome.ssrc;
                mServerMix = welcome.mode == 1;
                openCapture();
                mCaptureTimer->start();
                mKeepAliveTimer->start();
                setState(VoiceState::InGame);
                break;
            }
            case TYPE_REJECT:
            {
                const quint8 reason = reader.u8();
                emit errorOccurred(reason == REJECT_BAD_TICKET
                    ? tr("Voice: the ticket expired, reconnecting")
                    : tr("Voice: the server refused the connection (code %1)").arg(reason));
                // TODO: при REJECT_BAD_TICKET запросить новый тикет через мост
                // (CONTROL_TICKET_STALE движку).
                break;
            }
            case TYPE_AUDIO_DOWN:
            {
                AudioDown audio;
                if (!parseAudioDown(reader, audio))
                    break;
                Peer* peer = mPeers.value(audio.ssrc, nullptr);
                if (peer == nullptr)
                {
                    peer = new Peer();
                    peer->ssrc = audio.ssrc;
                    mPeers.insert(audio.ssrc, peer);
                    // TODO: opus_decoder_create(48000, 1), alGenSources(1)
                }
                peer->lastFrameMs = QDateTime::currentMSecsSinceEpoch();
                peer->anonymous = (audio.flags & AUDIO_ANON) != 0;

                const float gain = unpackGain(audio.gain) * mSettings.outputVolume;
                const float azimuth = unpackAngle(audio.azimuth);
                const float distance = unpackDistance(audio.distance);
                placeSource(*peer, azimuth, distance, gain);

                // TODO: положить audio.opus в джиттер-буфер; PLC при разрыве
                // seq; AUDIO_EOS → погасить источник и отпустить его в пул.
                break;
            }
            case TYPE_MIXDOWN:
                // TODO (U025g): один готовый поток, позиционирование уже
                // применено сервером, ставим как AL_SOURCE_RELATIVE в (0,0,0).
                break;
            case TYPE_PEERS:
            {
                std::vector<PeerLevel> peers;
                if (!parsePeers(reader, peers))
                    break;
                QVector<quint32> ssrcs;
                ssrcs.reserve(static_cast<int>(peers.size()));
                for (const PeerLevel& peer : peers)
                    ssrcs.push_back(peer.ssrc);
                emit speakersChanged(ssrcs);
                // TODO: переслать движку BRIDGE_PEERS для индикатора в HUD.
                break;
            }
            case TYPE_CONTROL:
            {
                const quint8 op = reader.u8();
                const quint32 arg = reader.u32();
                if (op == CONTROL_MUTE)
                    setMuted(arg != 0);
                else if (op == CONTROL_KICK)
                    setState(VoiceState::Lobby);
                break;
            }
            default:
                break;
        }
    }
}

void VoiceCommutator::slotBridgeDatagram()
{
    while (mBridgeSocket->hasPendingDatagrams())
    {
        const QNetworkDatagram datagram = mBridgeSocket->receiveDatagram(static_cast<qint64>(sMaxDatagram));
        // Мост принимает только с лупбэка и только с верным одноразовым токеном.
        if (!datagram.senderAddress().isLoopback())
            continue;

        const QByteArray bytes = datagram.data();
        Reader reader(bytes.constData(), static_cast<std::size_t>(bytes.size()));
        Header header;
        if (!readHeader(reader, header))
            continue;

        if (header.type == BRIDGE_HELLO)
        {
            // TODO: сверить токен, забрать тикет и адрес сервера, sendHello().
            setState(VoiceState::Handoff);
        }
        else if (header.type == BRIDGE_STATE)
        {
            State state;
            if (!parseState(reader, state))
                continue;
            mPushToTalkDown = state.ptt != 0;
            // Поворот головы нужен нам самим: OpenAL-слушатель смотрит
            // всегда вперёд, а источники ставятся уже в его системе координат,
            // поэтому azimuth с сервера применяется как есть. Локальный yaw
            // используется только для интерполяции между пакетами сервера.
            // TODO: alListener3f(AL_ORIENTATION...) не трогаем.
        }
    }
}

void VoiceCommutator::slotCaptureTick()
{
    if (mState != VoiceState::InGame || mMuted)
        return;
    const bool transmit = mSettings.pushToTalk ? mPushToTalkDown : true /* TODO: VAD */;
    if (!transmit)
        return;

    // TODO:
    //   alcCaptureSamples(device, buffer, 960)
    //   DC-фильтр → шумовой гейт (-45 дБ, атака 5 мс, спад 200 мс) → AGC
    //   opus_encode(...) → makeAudioUp(mSession, audio) → writeDatagram
    //   канал берём из последнего BRIDGE_STATE (шёпот/обычный/крик).
}

void VoiceCommutator::slotKeepAlive()
{
    if (mState != VoiceState::InGame)
        return;
    Writer writer;
    writeHeader(writer, TYPE_PING, mSession);
    writer.u32(static_cast<quint32>(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF));
    const std::string packet = writer.data();
    mVoiceSocket->writeDatagram(packet.data(), static_cast<qint64>(packet.size()),
        QHostAddress(mServerHost), mServerPort);

    // Молчащие источники убираем через 2 с, чтобы не держать alSource зря.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = mPeers.begin(); it != mPeers.end();)
    {
        if (now - it.value()->lastFrameMs > 2000)
        {
            delete it.value();
            it = mPeers.erase(it);
        }
        else
            ++it;
    }
}

void VoiceCommutator::placeSource(Peer& peer, float azimuth, float distanceMeters, float gain)
{
    // Сглаживание обязательно: на быстром повороте камеры скачок усиления
    // или панорамы слышен как щелчок.
    constexpr float smoothing = 0.75f;         // ~60 мс при тике 20 мс
    peer.smoothedGain = peer.smoothedGain * smoothing + gain * (1.f - smoothing);
    const float delta = wrapToPi(azimuth - peer.smoothedAzimuth);
    peer.smoothedAzimuth = wrapToPi(peer.smoothedAzimuth + delta * (1.f - smoothing));

    float position[3];
    openAlRelativePosition(peer.smoothedAzimuth, distanceMeters, position);
    Q_UNUSED(position);

    // TODO:
    //   alSourcei(peer.source, AL_SOURCE_RELATIVE, AL_TRUE);
    //   alSourcefv(peer.source, AL_POSITION, position);
    //   alSourcef(peer.source, AL_ROLLOFF_FACTOR, 0.f);   // затухание уже
    //                                                     // посчитал сервер
    //   alSourcef(peer.source, AL_GAIN, peer.smoothedGain);
    // HRTF включается на устройстве через ALC_HRTF_SOFT, значение берём
    // из settings.cfg [Sound] hrtf enable, чтобы совпадало с игрой.
}

void VoiceCommutator::openCapture() { /* TODO: alcCaptureOpenDevice(...) */ }
void VoiceCommutator::closeCapture() { mCaptureTimer->stop(); /* TODO */ }
void VoiceCommutator::openPlayback() { /* TODO: alcOpenDevice/alcCreateContext */ }
void VoiceCommutator::closePlayback() { /* TODO */ }
