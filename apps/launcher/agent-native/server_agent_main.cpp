#include "ServerPublisher.h"
#include "HttpJsonClient.h"
#include "MasterDefaults.h"
#include <QJsonDocument>
#include <QSslSocket>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>
int main(int argc,char**argv){QCoreApplication app(argc,argv);app.setApplicationName("arena-server-agent");app.setApplicationVersion("H024-standalone");QCommandLineParser p;p.addHelpOption();p.addOption({"check-tls","Verify HTTPS runtime and exit"});p.addOption({"check-master","Check master HTTPS health and exit without publishing"});p.addOption({"config","Private owner JSON","path","owner.json"});p.addOption({"server-root","ArenaMP root","path","."});p.addOption({"master","Master HTTPS URL","url","https://178.20.47.31:17777"});p.addOption({"relay-host","Relay address","host","178.20.47.31"});p.addOption({"relay-port","Relay UDP port","port","17778"});p.addOption({"no-relay","Publish only; do not run host relay"});p.process(app);if(p.isSet("check-tls")){if(!QSslSocket::supportsSsl()){qCritical()<<"HTTPS runtime unavailable";return 2;}qInfo()<<QSslSocket::sslLibraryVersionString();return 0;}if(p.isSet("check-master")) {
    if(!QSslSocket::supportsSsl()){qCritical()<<"HTTPS runtime unavailable";return 2;}
    HttpJsonClient client;QString error;QUrl health(p.value("master"));health.setPath("/v1/health");
    const auto result=client.request(health,"GET",{},nullptr,6000,&error);
    if(!error.isEmpty()||!result.value("ok").toBool()){qCritical().noquote()<<"Master check failed:"<<error;return 3;}
    qInfo().noquote()<<"Master HTTPS OK; relay UDP port:"<<result.value("relay_port").toInt();return 0;
}ServerPublisher s(p.value("config"),p.value("server-root"),QUrl(p.value("master")),p.value("relay-host"),p.value("relay-port").toUShort(),!p.isSet("no-relay"));QString er;if(!s.start(&er)){qCritical().noquote()<<er;return 2;}return app.exec();}
