#ifndef ARENAMP_SERVERSTATUSQUERY_HPP
#define ARENAMP_SERVERSTATUSQUERY_HPP
#include <QObject>
#include <QUdpSocket>
#include <QHostInfo>
#include <QTimer>
#include <QUuid>
#include <QWidget>
#include <functional>
#include <components/openmw-mp/serverstatus.hpp>

namespace Launcher
{
    class ServerStatusQuery : public QObject
    {
    public:
        enum State { NoAddress, Checking, Online, NoResponse };
        std::function<void(State, const ArenaStatus::Status&)> result;
        explicit ServerStatusQuery(QWidget* parent) : QObject(parent), mPage(parent)
        {
            mInterval.setInterval(15000);
            mDebounce.setSingleShot(true);
            mTimeout.setSingleShot(true);
            mResolve.setSingleShot(true);
            connect(&mInterval, &QTimer::timeout, this, [this]() { if (mPage->isVisible()) refresh(); });
            connect(&mDebounce, &QTimer::timeout, this, [this]() { refresh(); });
            connect(&mTimeout, &QTimer::timeout, this, [this]() { cancel(); publish(NoResponse); });
            connect(&mResolve, &QTimer::timeout, this, [this]() { cancel(); publish(NoResponse); });
            connect(&mSocket, &QUdpSocket::readyRead, this, [this]() { receive(); });
            mInterval.start();
        }
        ~ServerStatusQuery() override { cancel(); }
        void setEndpoint(QString host, quint16 port)
        {
            host = host.trimmed();
            if (host.startsWith('[') && host.endsWith(']')) host = host.mid(1, host.size() - 2);
            if (host == mHost && port == mPort) return;
            cancel();
            mHost = host; mPort = port;
            // Адрес сменился — прежние показания больше не про этот сервер.
            mLast = ArenaStatus::Status();
            publish(host.isEmpty() || port == 0 ? NoAddress : Checking);
            if (!host.isEmpty() && port != 0) mDebounce.start(600);
        }
        void refresh()
        {
            cancel();
            if (mHost.isEmpty() || !mPort) { publish(NoAddress); return; }
            // Пока идёт повторная проверка, показываем последние известные
            // значения, а не сбрасываем карточку в прочерки каждые 15 секунд.
            publish(Checking, mLast);
            const unsigned generation = mGeneration;
            mToken = QUuid::createUuid().toRfc4122().left(8).toStdString();
            // У разрешения имени свой бюджет: раньше DNS и ожидание ответа
            // делили одни 3 секунды, и на холодном резолве домена таймаут
            // срабатывал ещё до того, как пакет уходил.
            mResolve.start(3000);
            mLookup = QHostInfo::lookupHost(mHost, this, [this, generation](const QHostInfo& info)
            {
                if (generation != mGeneration) return;
                mLookup = -1;
                mResolve.stop();
                mAddresses = info.addresses();
                if (mAddresses.isEmpty() || !bindSocket()) { cancel(); publish(NoResponse); return; }
                // Support RakNet Time32 and Time64 builds, using one ephemeral socket.
                int sent = 0;
                for (const auto& address : mAddresses)
                {
                    if (address.isMulticast() || address == QHostAddress(QHostAddress::Broadcast)) continue;
                    for (unsigned size : {8u, 4u})
                    {
                        const std::string packet = ArenaStatus::ping(mToken, size);
                        if (mSocket.writeDatagram(packet.data(), static_cast<qint64>(packet.size()), address, mPort) >= 0) ++sent;
                    }
                    if (sent >= 8) break;
                }
                if (sent == 0) { cancel(); publish(NoResponse); return; }
                mTimeout.start(3000);
            });
        }
    private:
        void publish(State state, const ArenaStatus::Status& status = {}) { if (result) result(state, status); }
        void cancel()
        {
            ++mGeneration;
            if (mLookup != -1) QHostInfo::abortHostLookup(mLookup);
            mLookup = -1;
            mTimeout.stop(); mDebounce.stop(); mResolve.stop(); mSocket.abort();
        }

        bool bindSocket()
        {
            // Двухстековый Any нужен, чтобы дотянуться и до IPv6-серверов.
            // Если система его не даёт, откатываемся на чистый IPv4.
            if (mSocket.bind(QHostAddress(QHostAddress::Any), 0)) return true;
            return mSocket.bind(QHostAddress(QHostAddress::AnyIPv4), 0);
        }

        void receive()
        {
            while (mSocket.hasPendingDatagrams())
            {
                const qint64 length = mSocket.pendingDatagramSize();
                if (length < 0) break;
                if (length > 512) { mSocket.readDatagram(nullptr, 0); continue; }
                QByteArray bytes(static_cast<int>(length), '\0');
                quint16 port = 0;
                const qint64 received = mSocket.readDatagram(bytes.data(), bytes.size(), nullptr, &port);
                if (received != length || port != mPort) continue;

                // ПРИЧИНА БАГА U024h. Здесь стояло !mAddresses.contains(sender).
                // Сокет, привязанный к QHostAddress::Any, — двухстековый
                // (AF_INET6 с выключенным V6ONLY), поэтому ответ IPv4-сервера
                // приходит с адресом отправителя в виде IPv4-mapped
                // ("::ffff:1.2.3.4"), а QHostAddress::operator== работает
                // в StrictConversion и НЕ считает его равным "1.2.3.4".
                // QList::contains() отправителя не находил, и каждый
                // корректный pong отбрасывался — включая ответ 127.0.0.1
                // в режиме локального сервера. На Android этого не было:
                // там сравниваются InetAddress, которые Java нормализует сама.
                //
                // Проверка адреса здесь не нужна вовсе: подлинность
                // подтверждает 8-байтовый одноразовый токен внутри пакета —
                // подделать ответ может только тот, кто этот токен видел.
                // Заодно это чинит ответы серверов за NAT, отвечающих
                // с другого исходящего адреса.
                const auto status = ArenaStatus::pong(bytes.toStdString(), mToken);
                if (!status.reachable) continue;
                cancel();
                mLast = status;
                publish(Online, status);
                return;
            }
        }
        QWidget* mPage;
        QUdpSocket mSocket;
        QTimer mInterval, mDebounce, mTimeout, mResolve;
        QString mHost;
        quint16 mPort = 0;
        QList<QHostAddress> mAddresses;
        std::string mToken;
        ArenaStatus::Status mLast;
        unsigned mGeneration = 0;
        int mLookup = -1;
    };
}
#endif
