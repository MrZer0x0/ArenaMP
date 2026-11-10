#pragma once
#include <QObject>
#include <QJsonObject>
#include <QNetworkAccessManager>
class HttpJsonClient : public QObject {
 Q_OBJECT
public:
 explicit HttpJsonClient(QObject* parent=nullptr);
 QJsonObject request(const QUrl& url, const QByteArray& method="GET", const QString& bearer={},
                     const QJsonObject* body=nullptr, int timeoutMs=5000, QString* error=nullptr);
private: QNetworkAccessManager m_nam;
};
