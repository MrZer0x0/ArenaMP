#include "HttpJsonClient.h"
#include "RelayLink.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QJsonDocument>
#include <QTimer>
#include <QDebug>
int main(int argc,char**argv){QCoreApplication app(argc,argv);app.setApplicationName("arena-relay-agent");app.setApplicationVersion("H022-native");QCommandLineParser p;p.addHelpOption();p.addOption({"master","Master URL","url","https://178.20.47.31:17777"});p.addOption({"server-id","ArenaMP server id","id"});p.addOption({"relay-host","Relay host","host","178.20.47.31"});p.addOption({"relay-port","Relay UDP port","port","17778"});p.addOption({"local-port","Loopback port, 0=auto","port","0"});p.addOption({"session-file","Launcher-provided session JSON","path"});p.addOption({"ready-file","Write selected local port here","path"});p.process(app);QJsonObject session;QString er;if(!p.value("session-file").isEmpty()){QFile f(p.value("session-file"));if(!f.open(QIODevice::ReadOnly)){qCritical()<<f.errorString();return 2;}session=QJsonDocument::fromJson(f.readAll()).object();f.close();QFile::remove(p.value("session-file"));}else{if(p.value("server-id").isEmpty()){qCritical()<<"--server-id required";return 2;}HttpJsonClient h;QUrl u(p.value("master"));u.setPath("/v1/sessions/"+p.value("server-id"));QJsonObject empty;session=h.request(u,"POST",{},&empty,5000,&er);if(!er.isEmpty()){qCritical().noquote()<<er;return 2;}}RelayLink link(QHostAddress(p.value("relay-host")),p.value("relay-port").toUShort(),session,RelayLink::Client,p.value("local-port").toUShort(),0);if(!link.start(&er)){qCritical().noquote()<<er;return 2;}auto writeReady=[&](){
    if(!p.value("ready-file").isEmpty()){
        QFile f(p.value("ready-file"));
        const QByteArray port=QByteArray::number(link.localPort());
        if(!f.open(QIODevice::WriteOnly|QIODevice::Truncate)||f.write(port)!=port.size()) {
            qCritical()<<"Cannot write relay readiness file";return false;
        }
    }
    qInfo()<<"Connect ArenaMP to 127.0.0.1:"<<link.localPort();return true;
};
// Older hosts have no probe capability; preserve their previous behavior.
QTimer ready;
if(session.value("relay_probe").toBool()) {
    ready.setInterval(50);
    QObject::connect(&ready,&QTimer::timeout,&app,[&](){
        if(link.peerReady()){ready.stop();if(!writeReady())app.exit(2);}
    });
    ready.start();
    QTimer::singleShot(12000,&app,[&](){if(!link.peerReady()){
        qCritical()<<"Relay handshake timed out: host or UDP route unavailable";app.exit(3);
    }});
} else if(!writeReady()) return 2;
return app.exec();}
