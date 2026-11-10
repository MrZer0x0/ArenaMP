#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QHash>
#include <QSharedPointer>
class QTimer; class HttpJsonClient; class RelayLink;
class ServerPublisher: public QObject{
 Q_OBJECT
public:
 ServerPublisher(const QString& config,const QString& root,const QUrl& master,const QString& relayHost,quint16 relayPort,bool hostRelay,QObject* parent=nullptr);
 bool start(QString* error=nullptr);
private slots:void publish();void pollSessions();
private:
 QJsonArray manifest(const QString& path,QString* error)const;QHash<QString,QString> ini(const QString& path)const;QJsonObject payload(QString* error)const;QJsonObject status(const QString& path)const;
 QString m_configPath,m_root,m_relayHost;QUrl m_master;quint16 m_relayPort;bool m_hostRelay;bool m_busy=false;bool m_registered=false;QJsonObject m_cfg;HttpJsonClient* m_http=nullptr;QHash<QString,QSharedPointer<RelayLink>> m_links;
};
