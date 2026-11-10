#include "HttpJsonClient.h"
#include "MasterDefaults.h"
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QJsonDocument>
HttpJsonClient::HttpJsonClient(QObject* p):QObject(p){}
QJsonObject HttpJsonClient::request(const QUrl& url,const QByteArray& method,const QString& bearer,
 const QJsonObject* body,int timeoutMs,QString* error){
 QNetworkRequest req=ArenaMasterDefaults::request(url); req.setRawHeader("Accept","application/json");
 if(!bearer.isEmpty()) req.setRawHeader("Authorization",("Bearer "+bearer).toUtf8());
 QByteArray payload; if(body){req.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");payload=QJsonDocument(*body).toJson(QJsonDocument::Compact);} 
 QNetworkReply* reply = method=="GET" ? m_nam.get(req) : m_nam.sendCustomRequest(req,method,payload);
 QEventLoop loop; QTimer timer; timer.setSingleShot(true); connect(reply,&QNetworkReply::finished,&loop,&QEventLoop::quit); connect(&timer,&QTimer::timeout,&loop,&QEventLoop::quit); timer.start(timeoutMs); loop.exec();
 if(!reply->isFinished()){reply->abort();if(error)*error="timeout";reply->deleteLater();return{};}
 QByteArray raw=reply->readAll(); auto e=reply->error(); QString es=reply->errorString(); reply->deleteLater();
 QJsonParseError pe; auto doc=QJsonDocument::fromJson(raw,&pe); if(e!=QNetworkReply::NoError){if(error)*error=es+": "+QString::fromUtf8(raw);return doc.object();}
 if(pe.error!=QJsonParseError::NoError||!doc.isObject()){if(error)*error="invalid JSON";return{};} return doc.object();
}
