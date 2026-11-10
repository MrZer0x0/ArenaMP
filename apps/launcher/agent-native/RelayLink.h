#pragma once
#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QTimer>
#include <QHash>
#include <QSet>
#include <QQueue>
class RelayLink: public QObject{
 Q_OBJECT
public:
 enum Role{Client,Host};
 RelayLink(const QHostAddress& relay,quint16 relayPort,const QJsonObject& session,Role role,quint16 localPort=0,quint16 gamePort=25565,QObject* parent=nullptr);
 bool start(QString* error=nullptr); quint16 localPort()const; bool peerReady() const { return m_peerReady; } QString sessionId()const{return m_sessionId;}
private slots:void onLocal();void onRemote();void heartbeat();
private:
 struct Pending{qint64 created=0;int count=0;QHash<int,QByteArray> parts;};
 void sendClear(const QByteArray&); void sendFrame(const QByteArray* clear);
 QUdpSocket m_remote,m_local; QHostAddress m_relay;quint16 m_relayPort=0,m_gamePort=0;Role m_role;
 QByteArray m_sid,m_key,m_token;QString m_sessionId;QHostAddress m_peerAddr;quint16 m_peerPort=0;QTimer* m_timer=nullptr;
 bool m_peerReady=false;
 QSet<QByteArray> m_seen;QQueue<QByteArray> m_seenQ;QHash<QByteArray,Pending> m_pending;QElapsedTimer m_clock;
};
