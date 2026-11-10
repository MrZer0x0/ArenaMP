#pragma once
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslCertificate>
#include <QUrl>
namespace ArenaMasterDefaults {
inline QString url() { return QStringLiteral("https://178.20.47.31:17777"); }
inline QString enrollmentToken() { return QStringLiteral("65050eeaa496d397ebd8ef60813a0814b7a078761d432dcf539cccdd5ad29d10"); }
inline QNetworkRequest request(const QUrl& url) {
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    if (url.scheme()==QStringLiteral("https") && (url.host()==QStringLiteral("178.20.47.31") || url.host()==QStringLiteral("127.0.0.1") || url.host()==QStringLiteral("localhost"))) {
        auto ssl=QSslConfiguration::defaultConfiguration();
        auto authorities=ssl.caCertificates();
        authorities.append(QSslCertificate::fromData(QByteArray(R"PEM(-----BEGIN CERTIFICATE-----
MIIEQzCCAqugAwIBAgIUBAa39/9bY2RMD2hFm2hUFoKj42UwDQYJKoZIhvcNAQEL
BQAwIDEeMBwGA1UEAwwVQXJlbmFNUCBQYXJlbnQgTWFzdGVyMB4XDTI2MDkyNDAw
MjkwM1oXDTMxMDkyMzAwMjkwM1owIDEeMBwGA1UEAwwVQXJlbmFNUCBQYXJlbnQg
TWFzdGVyMIIBojANBgkqhkiG9w0BAQEFAAOCAY8AMIIBigKCAYEAxtzPac5OY9i8
xWlpDlAHNAx0k/2T3WNcY3ZLtJeCjm8YEbFwiCtT1uMPe/73F7WOx5R1CR8yVHrm
usrGAeVpLcOkjIfMXrBP89XHo28oLXhjP2AznH8+ky6mnsGbi4Uwj9pYvOupCaVC
2od2kZ7SowcwThS4sxXPWR8H6J5wj/8haoczfDtdrC/MX844Q76QzdbO09hhXQi3
lxRz35tR+NWVdJymus6agvuSVjN+7tmICRC/KD2yQRVEiut50XPJR+0vVfBDd8zO
mhHI6UGsNJpIEsZoi4pQGkab2HnJX/NuqCfik4783E+++TIA05OStGoahhLCc49g
4PgsUhVDe2hyx5eYYpGBBjz3+z+SVKqhrEU9PelSNZQpKA424XmrPYEnw3EttOYb
OGrZwwNUImpy0UXR1q1UKDXTo/Txnr8IoHXafNeJ5Kv7OWBhvNDG81UyMSrtonhX
X8lhpTojmuhOBrWAIhuOXTTAqeCGx+GcOsIXlnZD0AgGpfJUpy2XAgMBAAGjdTBz
MB0GA1UdDgQWBBT0v7tl2YK0jAbv46pJwoufOOWm0zAfBgNVHSMEGDAWgBT0v7tl
2YK0jAbv46pJwoufOOWm0zAgBgNVHREEGTAXhwSyFC8fhwR/AAABgglsb2NhbGhv
c3QwDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAYEANdFWxm3Sw6QN
ciJDOHKLDDp/s8C3XZCLmISZblZdERAq8f2jYuir/hesuwraP/mWO19fe4LfmXLd
/fZlqPzIQlpC6Fom6S/Ls5Myt3bdU18u+UlfjYSjN6OA7chV2XzCr0NL0PoWrshc
KPTlBy3U62Ev3uTE57EWDnOixDUG0HQdiNN6S4A7EgYeM89WhneO3OtwrH55bPO3
ihPi2+o296tnuva+xUvpES6qdkWXHZq9JRo3rP+bbniC8B0ajuac1XPwgl4/1sl6
LHSg8k0dHpmDh+eAbuQN3l6fDDC6DStFPfBV2+LoCCeh5cBoj/QVhnrjHEvnDbIV
GKZvLX+WeUE2xDeKTWU3sFJeatrEjnLZm0ozFgNaH083n6ZmvJOaUnafE5QbVaNS
cjtj00SjU9btrQUQYnyLl8n0VS/hgZgroGbQ+GZDBnnkCAXY2nXtcO+/eEyjC1WP
7b6Fd/rd6yUzImaxqEozBbRt84plnA/mjMMjrOi0JRgQxmEH6/LO
-----END CERTIFICATE-----
)PEM")));
        ssl.setCaCertificates(authorities); ssl.setProtocol(QSsl::TlsV1_2OrLater);
        request.setSslConfiguration(ssl);
    }
    return request;
}
}
