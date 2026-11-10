#include "ServerPublisher.h"
#include "MasterDefaults.h"
#include "Crypto.h"
#include <QSaveFile>
#include <QScopedValueRollback>
#include "HttpJsonClient.h"
#include "RelayLink.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QTimer>
#include <QUrl>
#include <QDebug>
#include <QSet>
ServerPublisher::ServerPublisher(const QString&c,const QString&r,const QUrl&m,const QString&rh,quint16 rp,bool hr,QObject*p):QObject(p),m_configPath(c),m_root(r),m_relayHost(rh),m_master(m),m_relayPort(rp),m_hostRelay(hr){}
bool ServerPublisher::start(QString*e){QFile f(m_configPath);if(!f.open(QIODevice::ReadOnly)){if(e)*e=f.errorString();return false;}QJsonParseError pe;auto d=QJsonDocument::fromJson(f.readAll(),&pe);if(pe.error!=QJsonParseError::NoError||!d.isObject()){if(e)*e="invalid owner config";return false;}m_cfg=d.object();m_http=new HttpJsonClient(this);auto* pub=new QTimer(this);pub->setInterval(15000);connect(pub,&QTimer::timeout,this,&ServerPublisher::publish);pub->start();QTimer::singleShot(0,this,&ServerPublisher::publish);if(m_hostRelay){auto* poll=new QTimer(this);poll->setInterval(2000);connect(poll,&QTimer::timeout,this,&ServerPublisher::pollSessions);poll->start();QTimer::singleShot(500,this,&ServerPublisher::pollSessions);}return true;}
QHash<QString,QString> ServerPublisher::ini(const QString&p)const{QHash<QString,QString>d;QFile f(p);if(!f.open(QIODevice::ReadOnly|QIODevice::Text))return d;QString sec;for(auto line:QString::fromUtf8(f.readAll()).split('\n')){QString s=line.trimmed();if(s.startsWith('[')&&s.endsWith(']')){sec=s.mid(1,s.size()-2);continue;}if(s.startsWith('#')||s.startsWith(';'))continue;int eq=s.indexOf('=');if(eq>0)d[sec+'.'+s.left(eq).trimmed()]=s.mid(eq+1).trimmed();}return d;}
QJsonArray ServerPublisher::manifest(const QString& path, QString* error) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = path + ": " + file.errorString();
        return {};
    }
    QByteArray clean;
    for (const auto& line : file.readAll().split('\n'))
        if (!line.trimmed().startsWith("//")) clean += line + '\n';
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(clean, &parseError);
    QJsonArray content;
    if (parseError.error == QJsonParseError::NoError && document.isArray())
        content = document.array();
    else if (parseError.error == QJsonParseError::NoError && document.isObject()
        && document.object().value("formatVersion").toInt() == 2
        && document.object().value("content").isArray())
        content = document.object().value("content").toArray();
    else {
        if (error) *error = path + ": invalid manifest (expected array or formatVersion=2 content array)";
        return {};
    }
    QJsonArray out;
    for (const auto& value : content) {
        if (!value.isObject() || value.toObject().isEmpty()) {
            if (error) *error = path + ": invalid content entry";
            return {};
        }
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (it.key().isEmpty() || !it.value().isArray()) {
                if (error) *error = path + ": invalid plugin hashes";
                return {};
            }
            QJsonArray hashes;
            for (const auto& hash : it.value().toArray()) hashes.append(hash.toVariant().toString());
            out.append(QJsonObject{{"name", it.key()},
                {"hash", hashes.isEmpty() ? QString() : hashes.first().toString()},
                {"allowed_hashes", hashes}, {"required", true}});
        }
    }
    return out;
}
QJsonObject ServerPublisher::status(const QString&p)const{QFile f(p);if(!f.open(QIODevice::ReadOnly))return{};auto o=QJsonDocument::fromJson(f.readAll()).object();if(QDateTime::currentSecsSinceEpoch()-qint64(o["timestamp"].toDouble())>45)return{};return o;}
QJsonObject ServerPublisher::payload(QString* e) const
{
    QDir root(m_root);
    auto cfg = ini(m_cfg.value("native_config").toString(root.filePath("userdata/tes3mp-server.cfg")));
    if (cfg.isEmpty())
        cfg = ini(root.filePath("tes3mp-server.cfg"));
    if (cfg.isEmpty())
        cfg = ini(root.filePath("config/tes3mp-server.cfg"));

    const QString visibility = m_cfg.value("visibility").toString("public") == "private" ? "private" : "public";
    const QString serverHome = m_cfg.value("server_home").toString(root.filePath("server"));
    const auto st = status(QDir(serverHome).filePath("data/custom/arenaMasterStatus.json"));
    if (st.isEmpty()) {
        if (e)
            *e = QString::fromUtf8("Нет свежего состояния игроков. Проверьте обновление скриптов и файл: ")
                + QDir(serverHome).filePath("data/custom/arenaMasterStatus.json");
        return {};
    }

    const bool cfgHasPassword = !cfg.value("General.password").trimmed().isEmpty();
    const bool hasPassword = st.contains("password_required")
        ? st.value("password_required").toBool() : cfgHasPassword;
    if (visibility == "private" && !hasPassword) {
        if (e)
            *e = "private server has no password in the running TES3MP server";
        return {};
    }

    QString manifestError;
    const auto files = manifest(QDir(serverHome).filePath("data/requiredDataFiles.json"), &manifestError);
    if (!manifestError.isEmpty()) {
        if (e)
            *e = manifestError;
        return {};
    }

    const int configuredMax = cfg.value("General.maximumPlayers",
        QString::number(m_cfg.value("max_players").toInt(100))).toInt();
    const int maxPlayers = st.value("max_players").toInt(configuredMax > 0 ? configuredMax : 100);
    const int configuredPort = cfg.value("General.port",
        QString::number(m_cfg.value("port").toInt(25565))).toInt();
    const int port = st.value("port").toInt(configuredPort > 0 ? configuredPort : 25565);

    return {{"name", cfg.value("General.hostname", m_cfg.value("name").toString("ArenaMP server"))},
        {"build_name", m_cfg.value("build_name").toString("ArenaMP")},
        {"build_version", m_cfg.value("build_version").toString()},
        {"engine_version", m_cfg.value("engine_version").toString()},
        {"protocol", m_cfg.value("protocol").toVariant().toString()},
        {"game_mode", m_cfg.value("game_mode").toString("RP")},
        {"players", st.value("players").toInt()},
        {"started_at", st.value("started_at")},
        {"max_players", maxPlayers},
        {"port", port},
        {"relay_probe", true},
        {"connection", m_cfg.value("connection").toString("auto")},
        {"public_address", m_cfg.value("public_address").toString()},
        {"password_required", hasPassword},
        {"visibility", visibility},
        {"requirements", m_cfg.value("requirements").toString().left(512)},
        {"website", m_cfg.value("website").toString().trimmed().left(2048)},
        {"files", files}};
}

void ServerPublisher::publish(){
 if(m_busy)return;QScopedValueRollback<bool> busy(m_busy,true);
 QFile config(m_configPath);if(!config.open(QIODevice::ReadOnly))return;
 auto updated=QJsonDocument::fromJson(config.readAll()).object();config.close();
 if(!updated.value("enabled").toBool(true)){m_registered=false;m_links.clear();return;}
 m_cfg=updated;
 QFile credentials(m_configPath+".credentials");
 if(credentials.open(QIODevice::ReadOnly)) {
   const auto saved=QJsonDocument::fromJson(credentials.readAll()).object();
   for(const QString& key:{QStringLiteral("installation_secret"),QStringLiteral("server_id"),QStringLiteral("owner_token")})
     if(saved.contains(key))m_cfg[key]=saved[key];
 }
 credentials.close();
 m_master=QUrl(m_cfg.value("master_url").toString(m_master.toString()));
 if(m_master.scheme()!="https" && !(m_master.scheme()=="http"&&(m_master.host()=="127.0.0.1"||m_master.host()=="localhost"))) {qWarning()<<"HTTPS required";return;}
 if(m_cfg["server_id"].toString().isEmpty()||m_cfg["owner_token"].toString().isEmpty()) {
   auto persist=[&](){QSaveFile out(m_configPath+".credentials");if(!out.open(QIODevice::WriteOnly))return false;out.setPermissions(QFile::ReadOwner|QFile::WriteOwner);const auto data=QJsonDocument(QJsonObject{{"installation_secret",m_cfg["installation_secret"]},{"server_id",m_cfg["server_id"]},{"owner_token",m_cfg["owner_token"]}}).toJson();return out.write(data)==data.size()&&out.commit();};
   if(m_cfg["installation_secret"].toString().isEmpty()) {m_cfg["installation_secret"]=QString::fromLatin1(ArenaCrypto::randomBytes(32).toHex());if(!persist())return;}
   QUrl enrollment=m_master;enrollment.setPath("/v1/enroll");QString error;
   QJsonObject body{{"installation_secret",m_cfg["installation_secret"]}};
   auto result=m_http->request(enrollment,"POST",ArenaMasterDefaults::enrollmentToken(),&body,5000,&error);
   if(!error.isEmpty()){qWarning()<<"Master registration failed:"<<error;return;}
   if(result["server_id"].toString().isEmpty()||result["owner_token"].toString().size()<32)return;
   m_cfg["server_id"]=result["server_id"];m_cfg["owner_token"]=result["owner_token"];
   if(!persist())return;
 }
 QString er;auto p=payload(&er);if(p.isEmpty()){qWarning().noquote()<<"Master publish:"<<er;return;}QUrl u=m_master;u.setPath("/v1/servers/"+m_cfg["server_id"].toString());auto r=m_http->request(u,"POST",m_cfg["owner_token"].toString(),&p,5000,&er);if(!er.isEmpty()) {
    m_registered=false;
    qWarning().noquote()<<"Master publish:"<<er;
    if((r["error"].toString()=="forbidden"||r["error"].toString()=="not found") && !m_cfg["installation_secret"].toString().isEmpty()) {
        QSaveFile out(m_configPath+".credentials");
        if(out.open(QIODevice::WriteOnly)) {
            out.setPermissions(QFile::ReadOwner|QFile::WriteOwner);
            const auto bytes=QJsonDocument(QJsonObject{{"installation_secret",m_cfg["installation_secret"]},{"server_id",""},{"owner_token",""}}).toJson();
            if(out.write(bytes)==bytes.size())out.commit();
        }
    }
 }else {m_registered=true;qInfo().noquote()<<QString::fromUtf8("Опубликован в списке. ID:")<<r["id"].toString();}}
void ServerPublisher::pollSessions(){if(m_busy||!m_registered)return;QScopedValueRollback<bool> busy(m_busy,true);QUrl u=m_master;u.setPath("/v1/servers/"+m_cfg["server_id"].toString()+"/sessions");QString er;auto r=m_http->request(u,"GET",m_cfg["owner_token"].toString(),nullptr,5000,&er);if(!er.isEmpty()){qWarning().noquote()<<"Host relay poll:"<<er;return;}QSet<QString> current;for(auto v:r["sessions"].toArray()){auto s=v.toObject();QString id=s["id"].toString();current.insert(id);if(m_links.contains(id))continue;QHostAddress relay(m_relayHost);QString perr;auto pp=payload(&perr);quint16 gamePort=quint16(pp.value("port").toInt(25565));auto link=QSharedPointer<RelayLink>::create(relay,quint16(s.value("relay_port").toInt(m_relayPort)),s,RelayLink::Host,0,gamePort);if(!link->start(&er)){qWarning().noquote()<<"Relay session:"<<er;continue;}m_links[id]=link;qInfo().noquote()<<"HOST session"<<id;}for(auto it=m_links.begin();it!=m_links.end();)if(!current.contains(it.key()))it=m_links.erase(it);else++it;}
