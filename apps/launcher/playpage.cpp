#include <functional>
#include "agent-native/MasterDefaults.h"
#include <QDialog>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QInputDialog>
#include <QJsonArray>
#include <QTableWidget>
#include <QHeaderView>
#include <QSettings>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSharedPointer>
#include <QVector>
#include <QUdpSocket>
#include <QTimer>
#include <QDesktopServices>
#include <QUrl>
#include <QLineEdit>
#include "playpage.hpp"
#include "serverstatusquery.hpp"
#include <QGridLayout>

#include <array>
#include <cmath>
#include <exception>
#include <initializer_list>

#include <components/misc/arenaglassicons.hpp>
#include <components/settings/parser.hpp>
#include <components/settings/settings.hpp>

#include <QApplication>
#include <QAbstractSocket>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QIntValidator>
#include <QLineEdit>
#include <QMessageBox>
#include <QLabel>
#include <QNetworkInterface>
#include <QHostAddress>
#include <QHBoxLayout>
#include <QFontDatabase>
#include <QFrame>
#include <QGroupBox>
#include <QScrollArea>
#include <QTabBar>
#include <QTabWidget>
#include <QListWidget>
#include <QFormLayout>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QStandardPaths>
#include <QSpinBox>
#include <QStyle>
#include <QStringList>
#include <QTextStream>
#include <QThread>
#include <QVBoxLayout>

namespace
{
    bool findConfigAssignment(const QString& text, const QString& key, QString* value)
    {
        const QRegularExpression pattern(QStringLiteral("(^|\\n)\\s*config\\.%1\\s*=\\s*([^\\r\\n]+)").arg(QRegularExpression::escape(key)));
        const QRegularExpressionMatch match = pattern.match(text);
        if (!match.hasMatch())
            return false;

        if (value != nullptr)
            *value = match.captured(2).trimmed();
        return true;
    }

    QString replaceConfigAssignment(const QString& text, const QString& key, const QString& value)
    {
        const QRegularExpression pattern(QStringLiteral("(^|\\n)(\\s*config\\.%1\\s*=\\s*)([^\\r\\n]+)").arg(QRegularExpression::escape(key)));
        const QRegularExpressionMatch match = pattern.match(text);
        if (!match.hasMatch())
            return text;

        // Only update the first, user-editable declaration. Several ArenaMP
        // settings are assigned again later when their values are clamped. A
        // global regular-expression replacement would replace the first line
        // of a multi-line clamp call and leave its arguments behind as invalid
        // Lua syntax.
        QString result = text;
        result.replace(match.capturedStart(3), match.capturedLength(3), value);
        return result;
    }

    void loadLineEdit(QLineEdit* widget, const QString& text, const QString& key)
    {
        QString value;
        if (!findConfigAssignment(text, key, &value))
            return;

        if (value.startsWith('"') && value.endsWith('"') && value.size() >= 2)
            value = value.mid(1, value.size() - 2);

        widget->setText(value);
    }

    void loadSpinBox(QSpinBox* widget, const QString& text, const QString& key)
    {
        QString value;
        if (!findConfigAssignment(text, key, &value))
            return;

        bool ok = false;
        const int parsed = value.toInt(&ok);
        if (ok)
            widget->setValue(parsed);
    }

    void loadDoubleSpinBox(QDoubleSpinBox* widget, const QString& text, const QString& key)
    {
        QString value;
        if (!findConfigAssignment(text, key, &value))
            return;

        bool ok = false;
        const double parsed = value.toDouble(&ok);
        if (ok)
            widget->setValue(parsed);
    }

    void loadCheckBox(QCheckBox* widget, const QString& text, const QString& key)
    {
        QString value;
        if (!findConfigAssignment(text, key, &value))
            return;

        widget->setChecked(value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
    }

    void loadComboBox(QComboBox* widget, const QString& text, const QString& key)
    {
        QString value;
        if (!findConfigAssignment(text, key, &value))
            return;

        if (value.startsWith('"') && value.endsWith('"') && value.size() >= 2)
            value = value.mid(1, value.size() - 2);

        const int index = widget->findData(value);
        if (index >= 0)
            widget->setCurrentIndex(index);
    }

    void configureHorizontalTabs(QTabWidget* tabs)
    {
        if (tabs == nullptr || tabs->tabBar() == nullptr)
            return;
        tabs->setDocumentMode(true);
        tabs->tabBar()->setExpanding(true);
        tabs->tabBar()->setUsesScrollButtons(false);
        tabs->tabBar()->setElideMode(Qt::ElideRight);
        tabs->tabBar()->setDrawBase(false);
    }

    // Dynamic properties are read by the shared QSS. Changing one on a widget
    // that is already polished needs an explicit re-polish.
    void repolish(QWidget* widget)
    {
        if (widget == nullptr)
            return;
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }

    void setStatusProperty(QWidget* widget, const QString& state)
    {
        if (widget == nullptr || widget->property("arenaStatus").toString() == state)
            return;
        widget->setProperty("arenaStatus", state);
        repolish(widget);
    }

    void setIconLabel(QLabel* label, const QString& icon, int size)
    {
        if (label == nullptr)
            return;
        label->setPixmap(ArenaUi::glassIcon(icon).pixmap(size, size));
    }

    // Small "(?)" markers next to options. The explanation lives in the
    // tooltip, so the cards stay compact in the fixed-size launcher.
    void configureHintLabel(QLabel* label)
    {
        if (label == nullptr)
            return;
        setIconLabel(label, QStringLiteral("hint"), 16);
        label->setFixedSize(18, 18);
        label->setAlignment(Qt::AlignCenter);
        label->setCursor(Qt::WhatsThisCursor);
    }
}

Launcher::PlayPage::PlayPage(QWidget *parent)
    : QWidget(parent)
    , mAlternativeServer(nullptr)
    , mAlternativeAddress(nullptr)
    , mAlternativePort(nullptr)
    , mProjectLink(nullptr)
    , mEmbeddedServerConsole(nullptr)
    , mSyncingServerSettingsTabs(false)
    , mHostInterfaceLabel(nullptr)
    , mHostInterfaceCombo(nullptr)
    , mRefreshHostInterfacesButton(nullptr)
    , mUpdateHashesButton(nullptr)
    , mEnforceRequiredCheckBox(nullptr)
    , mServerModeLabel(nullptr)
    , mServerModeCombo(nullptr)
    , mClearCellsButton(nullptr)
    , mResetServerButton(nullptr)
    , mSyncingXpControls(false)
    , mServerRunning(false)
    , mUpdateChecking(false)
    , mUpdateAvailable(false)
    , mLogicalThreads(QThread::idealThreadCount())
{
    setObjectName("PlayPage");
    setupUi(this);
    serverPortEdit->setValidator(new QIntValidator(1, 65535, serverPortEdit));

    // U024h: live game-server information immediately below the alternate address.
    auto* remoteCard = new QFrame(launchCard);
    remoteCard->setObjectName(QStringLiteral("remoteServerStatusCard"));
    remoteCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    remoteCard->setStyleSheet(QStringLiteral("QFrame#remoteServerStatusCard { background: rgba(12,14,17,100); border: 1px solid rgba(220,191,137,40); border-radius: 10px; }"));
    auto* remoteLayout = new QGridLayout(remoteCard);
    remoteLayout->setContentsMargins(10, 6, 10, 6);
    remoteLayout->setHorizontalSpacing(9);
    remoteLayout->setVerticalSpacing(2);
    auto* title = new QLabel(tr("Selected server"), remoteCard);
    mRemoteStatus = new QLabel(tr("Checking..."), remoteCard);
    mRemotePlayers = new QLabel(QStringLiteral("—"), remoteCard);
    mRemoteUptime = new QLabel(QStringLiteral("—"), remoteCard);
    mRemoteEndpoint = new QLabel(remoteCard);
    mRemoteEndpoint->setTextFormat(Qt::PlainText);
    mRemoteEndpoint->setWordWrap(true);
    remoteLayout->addWidget(title, 0, 0);
    remoteLayout->addWidget(mRemoteStatus, 0, 1);
    remoteLayout->setColumnStretch(2, 1);
    auto* refresh = new QPushButton(remoteCard);
    refresh->setIcon(ArenaUi::glassIcon(QStringLiteral("refresh")));
    refresh->setToolTip(tr("Refresh server status"));
    refresh->setFixedSize(26, 24);
    remoteLayout->addWidget(refresh, 0, 3);
    remoteLayout->addWidget(mRemoteEndpoint, 1, 0, 1, 4);
    auto* playersLabel = new QLabel(tr("Players online:"), remoteCard);
    auto* uptimeLabel = new QLabel(tr("Server uptime:"), remoteCard);
    remoteLayout->addWidget(playersLabel, 2, 0);
    remoteLayout->addWidget(mRemotePlayers, 2, 1, 1, 3);
    remoteLayout->addWidget(uptimeLabel, 3, 0);
    remoteLayout->addWidget(mRemoteUptime, 3, 1, 1, 3);
    auto* selectedWebsite=new QPushButton(tr("Сайт выбранного сервера"),remoteCard);
    selectedWebsite->setObjectName(QStringLiteral("arenaSelectedWebsite"));selectedWebsite->hide();
    remoteLayout->addWidget(selectedWebsite,4,0,1,4);
    connect(selectedWebsite,&QPushButton::clicked,this,[this](){
        const QUrl url(mSelectedMasterServer.value("website").toString());
        if(url.isValid() && (url.scheme()=="https" || url.scheme()=="http") && !url.host().isEmpty() && url.userInfo().isEmpty())QDesktopServices::openUrl(url);
    });
    launchCardLayout->insertWidget(launchCardLayout->indexOf(launchFormFrame) + 1, remoteCard);

    // H036: lifetime of the asynchronous browser is the directory tab itself.
    struct DirectoryPage final : QWidget {
        QVBoxLayout layout;
        QLineEdit endpoint, inviteId;
        QPushButton inviteButton, refreshButton, filesButton, directButton, relayButton;
        QTableWidget table;
        QLabel status;
        QNetworkAccessManager net;
        QVector<QJsonObject> servers;
        QSettings settings;
        explicit DirectoryPage(QWidget* parent) : QWidget(parent), layout(this),
            endpoint(this), inviteId(this), inviteButton(tr("Найти по ID"),this),
            refreshButton(tr("Обновить"),this), filesButton(tr("Файлы"),this),
            directButton(tr("Напрямую"),this), relayButton(tr("Выбрать"),this),
            table(this), status(tr("Нажмите Обновить"),this), net(this),
            settings(QStringLiteral("ArenaMP"),QStringLiteral("Launcher")) {}
    };
    auto* d = new DirectoryPage(pageTabs);
    const auto previous=QJsonDocument::fromJson(d->settings.value("master/selected").toByteArray()).object();
    if(!previous.isEmpty())QTimer::singleShot(0,this,[this,previous](){
        mSelectedMasterServer=previous;
        mMasterSelectionConnected=false;
        if(auto* button=findChild<QPushButton*>("arenaSelectedWebsite")) {
            const QUrl url(previous.value("website").toString());
            button->setVisible(url.isValid() && (url.scheme()=="https" || url.scheme()=="http") && !url.host().isEmpty() && url.userInfo().isEmpty());
            button->setToolTip(url.toString());
        }
        updateStatusPanel();
    });
    pageTabs->addTab(d,tr("Серверы"));
    {
        d->layout.setContentsMargins(12,10,12,10);
        auto* heading=new QLabel(tr("Серверы ArenaMP"),d);
        heading->setProperty("arenaSectionTitle",true);
        d->layout.addWidget(heading);
        auto* clearSelection=new QPushButton(tr("Сбросить выбор"),d);
        connect(clearSelection,&QPushButton::clicked,this,[this](){
            mSelectedMasterServer=QJsonObject();mMasterPassword.clear();
            QSettings("ArenaMP","Launcher").remove("master/selected");mAlternativeServer->setChecked(false);
            if(auto* b=findChild<QPushButton*>("arenaSelectedWebsite"))b->hide();
            updateStatusPanel();
        });
        d->endpoint.setText(d->settings.value(QStringLiteral("master/url"),ArenaMasterDefaults::url()).toString());
        // Master URL remains configurable through master/url in QSettings only.
        d->endpoint.hide();
        auto* inviteRow=new QHBoxLayout();
        d->inviteId.setPlaceholderText(tr("ID приватного сервера"));
        inviteRow->addWidget(&d->inviteId,1);inviteRow->addWidget(&d->inviteButton);inviteRow->addWidget(clearSelection);
        d->layout.addLayout(inviteRow);
        d->table.setColumnCount(5);
        d->table.setHorizontalHeaderLabels({tr("Название"), tr("Сборка"), tr("Режим"), tr("Игроки"), tr("Соединение")});
        d->table.horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        d->table.verticalHeader()->hide();
        d->table.setSelectionMode(QAbstractItemView::SingleSelection);
        d->table.setAlternatingRowColors(true);
        d->table.setShowGrid(false);
        d->table.verticalHeader()->setDefaultSectionSize(38);
        d->table.setTextElideMode(Qt::ElideRight);
        d->table.setSelectionBehavior(QAbstractItemView::SelectRows);
        d->table.setEditTriggers(QAbstractItemView::NoEditTriggers);
        d->layout.addWidget(&d->table);
        d->status.setWordWrap(true);
        d->status.setTextFormat(Qt::PlainText);
        d->layout.addWidget(&d->status);
        auto* actions = new QHBoxLayout();
        actions->addWidget(&d->refreshButton);
        actions->addWidget(&d->filesButton);
        auto* websiteButton=new QPushButton(tr("Сайт"),d);actions->addWidget(websiteButton);
        connect(websiteButton,&QPushButton::clicked,d,[d](){
            const int row=d->table.currentRow();if(row<0 || row>=d->servers.size())return;
            const QUrl url(d->servers[row].value("website").toString());
            if(url.isValid() && (url.scheme()=="https" || url.scheme()=="http") && !url.host().isEmpty() && url.userInfo().isEmpty())QDesktopServices::openUrl(url);
            else d->status.setText(QObject::tr("Владелец не указал ссылку"));
        });
        actions->addWidget(&d->relayButton);
        actions->addWidget(&d->directButton);
        d->layout.addLayout(actions);
        auto selected = [d]() -> QJsonObject {
            const int idx=d->table.currentRow();
            return idx >= 0 && idx < d->servers.size() ? d->servers[idx] : QJsonObject();
        };
        auto get = [d](const QString& url, const std::function<void(QNetworkReply*)>& done) {
            QNetworkReply* reply=d->net.get(ArenaMasterDefaults::request(QUrl(url)));
            QTimer::singleShot(6000, reply, [reply](){if(reply->isRunning())reply->abort();});
            QObject::connect(reply,&QNetworkReply::finished,d,[reply,done](){done(reply);reply->deleteLater();});
        };
        mPrepareMaster=[this,d,get](){
            mMasterPreparing=true;
            d->setEnabled(false);
            mRemoteStatus->setText(tr("Подключение…"));
            const QString base=mSelectedMasterServer.value("master_url").toString();
            const QString id=mSelectedMasterServer.value("id").toString();
            const bool direct=mSelectedMasterServer.value("launcher_direct").toBool();
            auto fail=[this,d](const QString& message){
                mMasterPreparing=false;d->setEnabled(true);d->setEnabled(true);mRemoteStatus->setText(tr("Нет подключения"));
                QMessageBox::warning(this,tr("Выбранный сервер"),message);
            };
            get(base+QStringLiteral("/v1/servers/")+id,[this,d,get,base,id,direct,fail](QNetworkReply* reply){
                if(reply->error()!=QNetworkReply::NoError){fail(reply->errorString());return;}
                QJsonObject live=QJsonDocument::fromJson(reply->readAll()).object();
                if(live.isEmpty()){fail(tr("Мастер вернул пустой сервер"));return;}live["id"]=id;
                get(base+QStringLiteral("/v1/servers/")+id+QStringLiteral("/manifest"),[this,d,base,direct,fail,live](QNetworkReply* manifestReply) mutable {
                    if(manifestReply->error()!=QNetworkReply::NoError){fail(manifestReply->errorString());return;}
                    const auto manifest=QJsonDocument::fromJson(manifestReply->readAll()).object();
                    if(!manifest.value("files").isArray()){fail(tr("Мастер вернул неправильный список файлов"));return;}
                    live["files"]=manifest.value("files");
                    d->servers={live};d->table.setRowCount(1);
                    d->table.setItem(0,0,new QTableWidgetItem(live.value("name").toString()));
                    d->table.selectRow(0);d->endpoint.setText(base);d->setEnabled(true);
                    if(direct || live.value("connection").toString()=="direct")d->directButton.click();else d->relayButton.click();
                    if(mMasterPreparing)d->setEnabled(false);
                });
            });
        };
        QObject::connect(&d->refreshButton,&QPushButton::clicked,d,[this,d,get,selected]() {
            const QString base=d->endpoint.text().trimmed().remove(QRegularExpression(QStringLiteral("/+$")));
            const QUrl serviceUrl(base);
            const bool localOnly=serviceUrl.host()==QStringLiteral("127.0.0.1") || serviceUrl.host()==QStringLiteral("localhost") || serviceUrl.host()==QStringLiteral("::1");
            if (!serviceUrl.isValid() || !(serviceUrl.scheme()==QStringLiteral("https") ||
                (serviceUrl.scheme()==QStringLiteral("http") && localOnly))) {
                d->status.setText(tr("Внешний мастер-сервер требует HTTPS"));return;
            }
            d->settings.setValue(QStringLiteral("master/url"),base);
            d->refreshButton.setEnabled(false);
            d->status.setText(tr("Получение списка серверов…"));
            get(base+QStringLiteral("/v1/servers"),[this,d,get,selected](QNetworkReply* reply){
                d->refreshButton.setEnabled(true);
                if(reply->error()!=QNetworkReply::NoError){d->servers.clear();d->table.setRowCount(0);d->status.setText(tr("Мастер недоступен: %1. Проверьте HTTPS, TCP 17777 и дату устройства.").arg(reply->errorString()));return;}
                QJsonParseError error;
                const QJsonDocument doc=QJsonDocument::fromJson(reply->readAll(),&error);
                if(error.error!=QJsonParseError::NoError || !doc.isObject()){
                    d->status.setText(tr("Неверный ответ мастер-сервера"));return;
                }
                d->servers.clear();d->table.setRowCount(0);
                const QJsonArray array=doc.object().value(QStringLiteral("servers")).toArray();
                for(const QJsonValue& val : array) {
                    if(!val.isObject())continue;
                    const QJsonObject obj=val.toObject();d->servers.push_back(obj);
                    const int row=d->table.rowCount();d->table.insertRow(row);
                    const QStringList cols={(obj.value(QStringLiteral("password_required")).toBool()?QStringLiteral("🔒 "):QString())+obj.value(QStringLiteral("name")).toString(),
                        obj.value(QStringLiteral("build_name")).toString()+QStringLiteral(" ")+obj.value(QStringLiteral("build_version")).toString(),
                        obj.value(QStringLiteral("game_mode")).toString(),
                        QStringLiteral("%1/%2").arg(obj.value(QStringLiteral("players")).toInt()).arg(obj.value(QStringLiteral("max_players")).toInt()),
                        obj.value(QStringLiteral("connection")).toString()};
                    for(int c=0;c<cols.size();++c)d->table.setItem(row,c,new QTableWidgetItem(cols[c]));
                }
                if(!d->servers.isEmpty())d->table.selectRow(0);
                d->status.setText(d->servers.isEmpty()?tr("Мастер доступен. Пока нет опубликованных серверов. Размещение включается в карточке «Локальный сервер»."):tr("Активных серверов: %1. Выберите сервер и нажмите «Выбрать», затем «Играть».").arg(d->servers.size()));
            });
        });
        QObject::connect(&d->inviteButton,&QPushButton::clicked,d,[this,d,get,selected]() {
            const QString id=d->inviteId.text().trimmed();
            if(!QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]{1,80}$")).match(id).hasMatch()) {
                d->status.setText(tr("Некорректный ID сервера"));return;
            }
            get(d->endpoint.text().trimmed()+QStringLiteral("/v1/servers/")+id,[this,d,get,selected,id](QNetworkReply* reply){
                if(reply->error()!=QNetworkReply::NoError){d->status.setText(tr("Сервер недоступен или не найден"));return;}
                const auto obj=QJsonDocument::fromJson(reply->readAll()).object();
                if(obj.isEmpty())return;
                QJsonObject selectedServer=obj;selectedServer.insert(QStringLiteral("id"),id);
                d->servers.clear();d->servers.push_back(selectedServer);d->table.setRowCount(1);
                d->table.setItem(0,0,new QTableWidgetItem(selectedServer.value(QStringLiteral("name")).toString()));
                d->table.setItem(0,1,new QTableWidgetItem(selectedServer.value(QStringLiteral("build_name")).toString()));
                d->table.setItem(0,2,new QTableWidgetItem(selectedServer.value(QStringLiteral("game_mode")).toString()));
                d->table.setItem(0,3,new QTableWidgetItem(QStringLiteral("%1/%2").arg(selectedServer.value(QStringLiteral("players")).toInt()).arg(selectedServer.value(QStringLiteral("max_players")).toInt())));
                d->table.setItem(0,4,new QTableWidgetItem(selectedServer.value(QStringLiteral("connection")).toString()));
                d->table.selectRow(0);
                d->status.setText(tr("Требования: %1").arg(selectedServer.value(QStringLiteral("requirements")).toString()));
            });
        });
        QObject::connect(&d->filesButton,&QPushButton::clicked,d,[this,d,get,selected]() {
            const QJsonObject obj=selected();if(obj.isEmpty())return;
            get(d->endpoint.text().trimmed()+QStringLiteral("/v1/servers/")+obj.value(QStringLiteral("id")).toString()+QStringLiteral("/manifest"),[this,d,get,selected,obj](QNetworkReply* reply){
                if(reply->error()!=QNetworkReply::NoError){d->status.setText(reply->errorString());return;}
                const QJsonArray arr=QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("files")).toArray();
                QStringList lines;
                for(const QJsonValue& v:arr){const auto f=v.toObject();lines<<f.value(QStringLiteral("name")).toString()+QStringLiteral(" · ")+f.value(QStringLiteral("hash")).toString();}
                QMessageBox::information(d,tr("Требования и файлы"),
                    obj.value(QStringLiteral("requirements")).toString()+QStringLiteral("\n\n")+lines.join(QStringLiteral("\n")));
            });
        });
        QObject::connect(&d->directButton,&QPushButton::clicked,d,[this,d,get,selected]() {
            const auto obj=selected();if(obj.isEmpty()){mMasterPreparing=false;d->setEnabled(true);return;}
            if(!mMasterPreparing) {
                auto chosen=obj;chosen["launcher_direct"]=true;rememberMasterServer(chosen,d->endpoint.text().trimmed());
                pageTabs->setCurrentWidget(playTab);return;
            }
            if(obj.value(QStringLiteral("players")).toInt()>=obj.value(QStringLiteral("max_players")).toInt() && obj.value(QStringLiteral("max_players")).toInt()>0) {
                mMasterPreparing=false;d->setEnabled(true);d->status.setText(tr("Сервер заполнен"));return;
            }
            QString pass;
            if(obj.value(QStringLiteral("password_required")).toBool()) {
                bool accepted=false;
                pass=QInputDialog::getText(d,tr("Пароль игрового сервера"),
                    tr("Пароль (проверяется игровым сервером):"),QLineEdit::Password,QString(),&accepted);
                if(!accepted||pass.isEmpty()){mMasterPreparing=false;d->setEnabled(true);return;}
            }
            auto chosen=obj;chosen["launcher_direct"]=true;rememberMasterServer(chosen,d->endpoint.text().trimmed());
            mMasterPassword=pass;
            setAlternativeServer(obj.value(QStringLiteral("address")).toString(),
                QString::number(obj.value(QStringLiteral("port")).toInt(25565)),true);
            pageTabs->setCurrentWidget(playTab);
            mMasterPreparing=false;d->setEnabled(true);emit playButtonClicked();
        });
        QObject::connect(&d->relayButton,&QPushButton::clicked,d,[this,d,get,selected]() {
            const auto obj=selected();if(obj.isEmpty()){mMasterPreparing=false;d->setEnabled(true);return;}
            if(!mMasterPreparing) {
                auto chosen=obj;chosen["launcher_direct"]=false;rememberMasterServer(chosen,d->endpoint.text().trimmed());
                pageTabs->setCurrentWidget(playTab);return;
            }
            if(obj.value(QStringLiteral("connection")).toString()==QStringLiteral("direct")){d->directButton.click();return;}
            if(obj.value(QStringLiteral("max_players")).toInt()>0 && obj.value(QStringLiteral("players")).toInt()>=obj.value(QStringLiteral("max_players")).toInt()){mMasterPreparing=false;d->setEnabled(true);d->status.setText(tr("Сервер заполнен"));return;}

            QString pass;
            if(obj.value(QStringLiteral("password_required")).toBool()) {
                bool accepted=false;
                pass=QInputDialog::getText(d,tr("Пароль игрового сервера"),
                    tr("Пароль (проверяется игровым сервером):"),QLineEdit::Password,QString(),&accepted);
                if(!accepted||pass.isEmpty()){mMasterPreparing=false;d->setEnabled(true);return;}
            }
            const QString base=d->endpoint.text().trimmed().remove(QRegularExpression(QStringLiteral("/+$")));
            const QUrl url(base);
            if (url.scheme()!=QStringLiteral("https") && url.host()!=QStringLiteral("127.0.0.1") && url.host()!=QStringLiteral("localhost")) {
                mMasterPreparing=false;d->setEnabled(true);d->status.setText(tr("Туннель требует HTTPS (кроме локального теста)."));return;
            }
            QString exe=QDir(QApplication::applicationDirPath()).filePath(QStringLiteral("arena-relay-agent"));
#ifdef Q_OS_WIN
            exe+=QStringLiteral(".exe");
#endif
            if (!QFileInfo::exists(exe)) {
                mMasterPreparing=false;d->setEnabled(true);d->status.setText(tr("Не найден агент туннеля рядом с лаунчером. Переустановите полный пакет."));return;
            }
            const QString id=obj.value(QStringLiteral("id")).toString();
            QNetworkRequest req=ArenaMasterDefaults::request(QUrl(base+QStringLiteral("/v1/sessions/")+id));
            req.setHeader(QNetworkRequest::ContentTypeHeader,QStringLiteral("application/json"));
            QNetworkReply* reply=d->net.post(req,QByteArray("{}"));
            QTimer::singleShot(6000,reply,[reply](){if(reply->isRunning())reply->abort();});
            d->status.setText(tr("Выделение туннеля…"));
            QObject::connect(reply,&QNetworkReply::finished,d,[this,d,get,selected,reply,pass,base,id,url,exe,obj](){
                if(reply->error()!=QNetworkReply::NoError){mMasterPreparing=false;d->setEnabled(true);QMessageBox::warning(this,tr("Подключение"),reply->errorString());d->status.setText(reply->errorString());reply->deleteLater();return;}
                const QJsonDocument session=QJsonDocument::fromJson(reply->readAll());reply->deleteLater();
                if(!session.isObject() || !session.object().contains(QStringLiteral("key"))){mMasterPreparing=false;d->setEnabled(true);d->status.setText(tr("Неправильный ответ ретранслятора"));return;}
                const QString directory=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
                QDir().mkpath(directory);
                const QString sessionId=session.object().value(QStringLiteral("id")).toString();
                if(sessionId.isEmpty()){mMasterPreparing=false;d->setEnabled(true);d->status.setText(tr("Не указан ID туннеля"));return;}
                const QString sessionPath=QDir(directory).filePath(QStringLiteral("arena-master-session-%1.json").arg(sessionId));
                QSaveFile file(sessionPath);
                if(!file.open(QIODevice::WriteOnly) || file.write(session.toJson(QJsonDocument::Compact))<0 || !file.commit()){
                    mMasterPreparing=false;d->setEnabled(true);d->status.setText(tr("Не удалось сохранить временный сеанс"));return;
                }
                QFile::setPermissions(sessionPath,QFile::ReadOwner|QFile::WriteOwner);
                const QString readyPath=QDir(directory).filePath(QStringLiteral("arena-master-ready-%1.txt").arg(sessionId));
                QFile::remove(readyPath);
                // An ephemeral loopback port prevents a stale agent from capturing new sessions.
                QUdpSocket portProbe;
                if(!portProbe.bind(QHostAddress(QHostAddress::LocalHost), quint16(0))){
                    mMasterPreparing=false;d->setEnabled(true);d->status.setText(tr("Невозможно выделить локальный UDP-порт"));return;
                }
                const quint16 selectedPort=portProbe.localPort();
                portProbe.close();
                const QStringList args={QStringLiteral("client"),QStringLiteral("--master"),base,
                    QStringLiteral("--server-id"),id,QStringLiteral("--session-file"),sessionPath,
                    QStringLiteral("--relay-host"),url.host(),
                    QStringLiteral("--relay-port"),QString::number(session.object().value(QStringLiteral("relay_port")).toInt(17778)),
                    QStringLiteral("--local-port"),QString::number(selectedPort),
                    QStringLiteral("--ready-file"),readyPath};
                if(!QProcess::startDetached(exe,args)) {mMasterPreparing=false;d->setEnabled(true);d->status.setText(tr("Не удалось запустить UDP-агент"));return;}
                d->relayButton.setEnabled(false);
                d->status.setText(tr("Ожидание готовности локального UDP-агента…"));
                auto* readyCheck=new QTimer(d);
                readyCheck->setInterval(100);
                QObject::connect(readyCheck,&QTimer::timeout,d,[this,d,get,selected,readyCheck,selectedPort,readyPath,pass,obj,base](){
                    QFile readyFile(readyPath);
                    if(!readyFile.open(QIODevice::ReadOnly))return;
                    const quint16 actual=readyFile.readAll().trimmed().toUShort();
                    readyFile.close();QFile::remove(readyPath);
                    readyCheck->stop();d->relayButton.setEnabled(true);
                    if(actual != selectedPort){mMasterPreparing=false;d->setEnabled(true);d->status.setText(tr("Агент запустился на другом порту"));d->relayButton.setEnabled(true);return;}
                    rememberMasterServer(obj,base);
                    mMasterPassword=pass;
                    setAlternativeServer(QStringLiteral("127.0.0.1"),QString::number(selectedPort),true);
                    pageTabs->setCurrentWidget(playTab);
                    mMasterPreparing=false;d->setEnabled(true);emit playButtonClicked();
                });
                QTimer::singleShot(16000,d,[this,d,get,selected,readyCheck](){
                    if(readyCheck->isActive()){
                        readyCheck->stop();d->relayButton.setEnabled(true);
                        mMasterPreparing=false;d->setEnabled(true);d->status.setText(tr("Агент не подтвердил готовность. Проверьте его запуск."));
                    }
                });
                readyCheck->start();
            });
        });

        for(auto* button : d->findChildren<QPushButton*>()) {
            button->setMinimumWidth(0);
            button->setStyleSheet(QStringLiteral("QPushButton { padding: 5px 10px; min-height: 22px; }"));
        }
        connect(pageTabs,&QTabWidget::currentChanged,d,[this,d](int index){
            if(pageTabs->widget(index)==d && d->refreshButton.isEnabled())d->refreshButton.click();
        });
    }


    // H039: keep the connection card compact at the top. The previous scroll
    // wrapper stretched several Preferred-height layout items, so the header
    // could float far above the fields (the large empty gap visible on PC).
    // All surplus height now belongs to the dedicated spacer immediately
    // before the hero button; the header, form and selected-server card keep
    // their natural height. The scroll area remains only as a high-DPI safety
    // net when translations genuinely need more vertical room.
    const int launchIndex = playTabLayout->indexOf(launchCard);
    playTabLayout->removeWidget(launchCard);
    auto* launchScroll = new QScrollArea(playTab);
    launchScroll->setObjectName(QStringLiteral("launchCardScroll"));
    launchScroll->setFrameShape(QFrame::NoFrame);
    launchScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    launchScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    launchScroll->setWidgetResizable(true);
    launchScroll->setAlignment(Qt::AlignTop);
    launchScroll->setStyleSheet(QStringLiteral("QScrollArea#launchCardScroll { background: transparent; border: 0; } QScrollArea#launchCardScroll > QWidget > QWidget { background: transparent; }"));

    launchCard->setMinimumWidth(0);
    launchCard->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    launchCardLayout->setContentsMargins(10, 10, 10, 10);
    launchCardLayout->setSpacing(6);

    launchTitleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    launchSubtitleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    launchFormFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    remoteCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    launchTitleLayout->setAlignment(Qt::AlignTop);
    launchHeaderLayout->setAlignment(Qt::AlignTop);
    launchCardLayout->setAlignment(launchHeaderLayout, Qt::AlignTop);
    launchCardLayout->setAlignment(launchFormFrame, Qt::AlignTop);
    launchCardLayout->setAlignment(remoteCard, Qt::AlignTop);

    // The .ui contains one spacer between the status card and Play. Make it
    // the only vertically expanding item so extra card height can never split
    // the title from the form again.
    for (int i = 0; i < launchCardLayout->count(); ++i)
    {
        if (QSpacerItem* spacer = launchCardLayout->itemAt(i)->spacerItem())
        {
            spacer->changeSize(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
            launchCardLayout->setStretch(i, 1);
        }
        else
            launchCardLayout->setStretch(i, 0);
    }
    launchCardLayout->invalidate();

    launchScroll->setWidget(launchCard);
    playTabLayout->insertWidget(launchIndex, launchScroll, 1);

    mStatusQuery = new ServerStatusQuery(this);
    mStatusQuery->result = [this](ServerStatusQuery::State state, const ArenaStatus::Status& status)
    {
        if(!mSelectedMasterServer.isEmpty())return;
        mRemoteStatus->setText(state == ServerStatusQuery::Online ? tr("Online")
            : state == ServerStatusQuery::Checking ? tr("Checking...")
            : state == ServerStatusQuery::NoAddress ? tr("Enter the server address") : tr("No response"));
        mRemoteStatus->setStyleSheet(state == ServerStatusQuery::Online
            ? QStringLiteral("color: #97c88b;") : QStringLiteral("color: #c5af8a;"));
        mRemotePlayers->setText(status.details ? QStringLiteral("%1 / %2").arg(status.players).arg(status.capacity) : QStringLiteral("—"));
        mRemoteUptime->setText(status.details ? tr("%1d %2h %3m")
            .arg(static_cast<qulonglong>(status.uptime / 86400))
            .arg(static_cast<qulonglong>((status.uptime / 3600) % 24))
            .arg(static_cast<qulonglong>((status.uptime / 60) % 60)) : QStringLiteral("—"));
        const QString hint = state == ServerStatusQuery::Online && !status.details
            ? tr("This server does not publish player count or uptime yet.") : QString();
        mRemotePlayers->setToolTip(hint);
        mRemoteUptime->setToolTip(hint);
    };
    connect(refresh, &QPushButton::clicked, this, [this]() { mStatusQuery->refresh(); });

    // U020 showcase layout. All controls now live in playpage.ui: a launch
    // card, a local-server card and a permanent system-status column. The
    // member pointers below are kept so the existing config/load/save code
    // continues to operate on exactly the same widgets.
    mAlternativeServer = alternativeServerCheckBox;
    mAlternativeAddress = alternativeAddressEdit;
    mAlternativePort = alternativePortEdit;
    mProjectLink = projectLinkButton;
    mHostInterfaceLabel = hostInterfaceLabel;
    mHostInterfaceCombo = hostInterfaceCombo;
    mRefreshHostInterfacesButton = refreshHostInterfacesButton;
    mUpdateHashesButton = updateHashesButton;
    mEnforceRequiredCheckBox = enforceRequiredCheckBox;
    mServerModeLabel = serverModeLabel;
    mServerModeCombo = serverModeCombo;
    mClearCellsButton = clearCellsButton;
    mResetServerButton = resetServerButton;

    // Page switch: compact pills in the top-left corner of the left panel.
    pageTabs->setProperty("arenaPrimaryTabs", true);
    pageTabs->setProperty("arenaSegmented", true);
    serverSettingsModeTabs->setProperty("arenaModeTabs", true);
    configureHorizontalTabs(pageTabs);
    configureHorizontalTabs(serverSettingsModeTabs);
    pageTabs->tabBar()->setExpanding(false);
    // Paint the left panel (rounded glass) behind the pills and the cards.
    pageTabs->setAttribute(Qt::WA_StyledBackground, true);
    pageTabs->setTabText(pageTabs->indexOf(playTab), tr("Connection"));
    pageTabs->setTabText(pageTabs->indexOf(serverConsoleTab), tr("Server Console"));
    pageTabs->setTabText(pageTabs->indexOf(serverSettingsTab), tr("Server Settings"));
    // H037: reserve room for the complete Russian captions including bold text.
    // A local tab-bar rule overrides both legacy and showcase theme rules.
    pageTabs->tabBar()->setElideMode(Qt::ElideNone);
    pageTabs->tabBar()->setUsesScrollButtons(true);
    pageTabs->tabBar()->setStyleSheet(QStringLiteral(
        "QTabBar::tab { min-width: 138px; padding: 5px 10px; font-size: 12px; }"));

    // Publication controls belong to the local-server card.
    auto* publication = new QWidget(serverCard);
    auto* publicationLayout = new QVBoxLayout(publication);
    publicationLayout->setContentsMargins(0,0,0,0);
    publicationLayout->setSpacing(4);
    auto* publishCheck = new QCheckBox(tr("Показывать в списке серверов"),publication);
    auto* passwordEdit = new QLineEdit(publication);
    passwordEdit->setObjectName(QStringLiteral("arenaLocalServerPassword"));
    passwordEdit->setMaximumHeight(30);
    passwordEdit->setEchoMode(QLineEdit::Password);
    passwordEdit->setPlaceholderText(tr("Пароль сервера (пусто — без пароля)"));
    passwordEdit->setToolTip(tr("Сохраняется перед запуском. Для смены пароля остановите сервер."));
    auto* publishInfo = new QLabel(tr("Размещение выключено"),publication);
    publishInfo->setWordWrap(true);
    publishInfo->setTextFormat(Qt::PlainText);
    publishInfo->setObjectName(QStringLiteral("arenaMasterPublishStatus"));
    publishInfo->setProperty("arenaMuted",true);
    publishInfo->setMaximumHeight(52);
    publicationLayout->addWidget(publishCheck);
    publicationLayout->addWidget(passwordEdit);
    publicationLayout->addWidget(publishInfo);
    serverCardLayout->insertWidget(3,publication);
    serverCardLayout->setSpacing(6);
    const QString ownerPath=QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("arena-master-owner.json");
    QFile ownerFile(ownerPath);QJsonObject owner;
    if(ownerFile.open(QIODevice::ReadOnly))owner=QJsonDocument::fromJson(ownerFile.readAll()).object();
    ownerFile.close();
    auto* websiteEdit=new QLineEdit(publication);
    websiteEdit->setObjectName(QStringLiteral("arenaLocalServerWebsite"));
    websiteEdit->setMaximumHeight(30);
    websiteEdit->setPlaceholderText(tr("Ссылка на сайт или сборку (https://…)"));
    websiteEdit->setText(owner.value("website").toString());
    publicationLayout->insertWidget(2,websiteEdit);
    connect(websiteEdit,&QLineEdit::editingFinished,this,[ownerPath,websiteEdit,publishInfo](){
        const QString value=websiteEdit->text().trimmed();const QUrl url(value);
        if(!value.isEmpty() && (value.size()>2048 || !url.isValid() || (url.scheme()!=QStringLiteral("https") && url.scheme()!=QStringLiteral("http")) || url.host().isEmpty() || !url.userInfo().isEmpty())) {
            publishInfo->setText(QObject::tr("Ссылка не сохранена: укажите полный http:// или https:// адрес."));return;
        }
        QFile file(ownerPath);QJsonObject config;
        if(file.open(QIODevice::ReadOnly))config=QJsonDocument::fromJson(file.readAll()).object();
        file.close();config["website"]=value;
        QDir().mkpath(QFileInfo(ownerPath).absolutePath());QSaveFile output(ownerPath);
        const QByteArray data=QJsonDocument(config).toJson();
        if(!output.open(QIODevice::WriteOnly) || output.write(data)!=data.size() || !output.commit())publishInfo->setText(QObject::tr("Не удалось сохранить ссылку"));
    });
    publishCheck->setChecked(owner.value("enabled").toBool(true));
    if(publishCheck->isChecked())publishInfo->setText(tr("Публикация включена. Запустите сервер."));
    connect(publishCheck,&QCheckBox::toggled,this,[ownerPath,publishCheck,publishInfo](bool enabled){
        QFile file(ownerPath);QJsonObject config;
        if(file.open(QIODevice::ReadOnly))config=QJsonDocument::fromJson(file.readAll()).object();
        file.close();config["enabled"]=enabled;
        config["visibility"]="public";config["connection"]="auto";
        if(!config.contains("master_url"))config["master_url"]=ArenaMasterDefaults::url();
        QDir().mkpath(QFileInfo(ownerPath).absolutePath());QSaveFile output(ownerPath);
        bool saved=output.open(QIODevice::WriteOnly);
        if(saved){output.setPermissions(QFile::ReadOwner|QFile::WriteOwner);const auto bytes=QJsonDocument(config).toJson();saved=output.write(bytes)==bytes.size()&&output.commit();}
        if(!saved){QSignalBlocker blocker(publishCheck);publishCheck->setChecked(!enabled);}
        publishInfo->setText(!saved?QObject::tr("Не удалось сохранить публикацию"):enabled?QObject::tr("Публикация включена. Запустите сервер."):QObject::tr("Размещение выключено. Запись исчезнет в течение 50 с."));
    });

    // The status column belongs to the overview only. Console and server
    // settings need the full width of the fixed window.
    connect(pageTabs, &QTabWidget::currentChanged, this, [this](int index) {
        statusPanel->setVisible(pageTabs->widget(index) == playTab);
    });

    // Card headers, status icons and "(?)" markers.
    setIconLabel(launchIconLabel, QStringLiteral("gamepad"), 30);
    setIconLabel(serverIconLabel, QStringLiteral("database"), 28);
    setIconLabel(statusPanelIconLabel, QStringLiteral("cpu"), 28);
    setIconLabel(gpuIconLabel, QStringLiteral("graphics"), 18);
    setIconLabel(cpuIconLabel, QStringLiteral("cpu"), 18);
    setIconLabel(modeIconLabel, QStringLiteral("drive"), 18);
    setIconLabel(endpointIconLabel, QStringLiteral("wifi"), 18);
    setIconLabel(buildIconLabel, QStringLiteral("cube"), 18);
    setIconLabel(presetIconLabel, QStringLiteral("users"), 18);
    setIconLabel(tipIconLabel, QStringLiteral("info"), 20);
    for (QLabel* hint : { hostModeHintLabel, alternativeServerHintLabel, autoRestartHintLabel, enforceRequiredHintLabel })
        configureHintLabel(hint);

    buildNameLabel->setProperty("arenaFieldLabel", true);
    serverLabel->setProperty("arenaFieldLabel", true);
    hostInterfaceLabel->setProperty("arenaFieldLabel", true);
    serverModeLabel->setProperty("arenaFieldLabel", true);
    buildNameEdit->setProperty("arenaBuildName", true);

    // Hero "Start game" action. ArenaUi::HeroButton paints itself, so the
    // caption, the connection target and the soft pulse are set through its
    // own API instead of child labels.
    playButton->setText(tr("Start game"));
    playButton->setIcon(ArenaUi::glassIcon(QStringLiteral("play-dark")));
    playButton->setToolTip(tr("Start ArenaMP with the current connection settings"));
    playButton->setPulse(true);

    changeBuildButton->setIcon(ArenaUi::glassIcon(QStringLiteral("browse")));
    connect(changeBuildButton, &QPushButton::clicked, this, &PlayPage::changeBuildRequested);

    // Connection card.
    mAlternativePort->setValidator(new QIntValidator(1, 65535, mAlternativePort));
    mAlternativeAddress->setVisible(false);
    mAlternativePort->setVisible(false);
    mRefreshHostInterfacesButton->setIcon(ArenaUi::glassIcon(QStringLiteral("refresh")));
    mRefreshHostInterfacesButton->setText(QString());
    mRefreshHostInterfacesButton->setAccessibleName(tr("Refresh the list of network interfaces"));
    mRefreshHostInterfacesButton->setFixedWidth(34);
    mRefreshHostInterfacesButton->setStyleSheet(QStringLiteral("QPushButton { min-width: 0; padding: 4px; }"));
    mHostInterfaceCombo->setMinimumContentsLength(0);
    mHostInterfaceCombo->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
    connect(mAlternativeServer, &QCheckBox::toggled, this, [this](bool checked) {
        mAlternativeAddress->setVisible(checked);
        mAlternativePort->setVisible(checked);
        if (checked) autoStartServerCheckBox->setChecked(false);
        updateStatusPanel();
    });
    connect(autoStartServerCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) mAlternativeServer->setChecked(false);
    });
    connect(mAlternativeAddress, &QLineEdit::textChanged, this, [this]() { updateStatusPanel(); });
    connect(mAlternativeAddress, &QLineEdit::textEdited, this, [this]() { mMasterPassword.clear(); mSelectedMasterServer=QJsonObject(); if(auto* b=findChild<QPushButton*>("arenaSelectedWebsite"))b->hide(); QSettings("ArenaMP","Launcher").remove("master/selected"); });
    connect(mAlternativePort, &QLineEdit::textEdited, this, [this]() { mMasterPassword.clear(); mSelectedMasterServer=QJsonObject(); if(auto* b=findChild<QPushButton*>("arenaSelectedWebsite"))b->hide(); QSettings("ArenaMP","Launcher").remove("master/selected"); });
    connect(mAlternativePort, &QLineEdit::textChanged, this, [this]() { updateStatusPanel(); });
    connect(serverAddressEdit, &QLineEdit::textChanged, this, [this]() { updateStatusPanel(); });
    connect(serverPortEdit, &QLineEdit::textChanged, this, [this]() { updateStatusPanel(); });
    connect(buildNameEdit, &QLineEdit::textChanged, this, [this]() { updateStatusPanel(); });
    connect(mHostInterfaceCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
        this, [this](int) { updateStatusPanel(); });

    // Local server card.
    mProjectUrl = QStringLiteral("https://t.me/arena_mp");
    mProjectLink->setIcon(ArenaUi::glassIcon(QStringLiteral("globe")));
    connect(mProjectLink, &QPushButton::clicked, this, [this]() {
        QString link = mProjectUrl.trimmed();
        if (!link.contains(QStringLiteral("://"))) link.prepend(QStringLiteral("https://"));
        const QUrl url(link);
        if (url.isValid() && (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http")))
            QDesktopServices::openUrl(url);
    });

    // Host-only helper: regenerate server/data/requiredDataFiles.json from the
    // exact content=/groundcover= selection currently active in the launcher.
    mUpdateHashesButton->setIcon(ArenaUi::glassIcon(QStringLiteral("refresh")));
    mUpdateHashesButton->setToolTip(tr("Generate the server data-file manifest from the current Content Files order and CRC32 hashes."));

    mServerModeCombo->addItem(tr("MMO (default)"), QStringLiteral("MMO"));
    mServerModeCombo->addItem(tr("CO-OP"), QStringLiteral("CO-OP"));
    mServerModeCombo->addItem(tr("Custom / mixed"), QStringLiteral("CUSTOM"));
    mServerModeCombo->setToolTip(tr("MMO keeps journal, factions, topics and reputation personal. CO-OP shares story progression between players. This preset changes server gameplay only and never touches graphics settings."));

    mClearCellsButton->setIcon(ArenaUi::glassIcon(QStringLiteral("trash")));
    mResetServerButton->setIcon(ArenaUi::glassIcon(QStringLiteral("refresh")));
    mClearCellsButton->setToolTip(tr("Delete saved cell state while keeping player accounts and world data."));
    mResetServerButton->setToolTip(tr("Delete all persistent gameplay data. The data-file manifest, ban list and custom/arenaFurnitureReference.json are preserved."));

    serverButton->setIcon(ArenaUi::glassIcon(QStringLiteral("play-dark")));
    stopServerButton->setIcon(ArenaUi::glassIcon(QStringLiteral("stop-muted")));
    serverButton->setCursor(Qt::PointingHandCursor);

    refreshHostInterfaces(QStringLiteral("0.0.0.0"));
    updateHostModeUi(autoStartServerCheckBox->isChecked());

    connect(playButton, SIGNAL(clicked()), this, SLOT(slotPlayClicked()));
    connect(serverButton, SIGNAL(clicked()), this, SLOT(slotServerClicked()));
    connect(stopServerButton, SIGNAL(clicked()), this, SLOT(slotStopServerClicked()));
    connect(autoStartServerCheckBox, SIGNAL(toggled(bool)), this, SLOT(slotAutoStartServerToggled(bool)));
    connect(mRefreshHostInterfacesButton, SIGNAL(clicked()), this, SLOT(slotRefreshHostInterfaces()));
    connect(mUpdateHashesButton, SIGNAL(clicked()), this, SLOT(slotUpdateHashesClicked()));
    connect(mEnforceRequiredCheckBox, SIGNAL(toggled(bool)), this, SLOT(slotEnforceRequiredToggled(bool)));
    connect(enforceDataFilesCheckBox, &QCheckBox::toggled, this, [this](bool enabled)
    {
        if (mEnforceRequiredCheckBox == nullptr || mEnforceRequiredCheckBox->isChecked() == enabled)
            return;
        const QSignalBlocker blocker(mEnforceRequiredCheckBox);
        mEnforceRequiredCheckBox->setChecked(enabled);
    });
    connect(mServerModeCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(slotServerModeChanged(int)));
    connect(mClearCellsButton, SIGNAL(clicked()), this, SIGNAL(clearServerCellsRequested()));
    connect(mResetServerButton, SIGNAL(clicked()), this, SIGNAL(resetServerDataRequested()));
    connect(autoRestartServerCheckBox, SIGNAL(toggled(bool)), this, SIGNAL(autoRestartServerChanged(bool)));
    connect(reloadServerSettingsButton, SIGNAL(clicked()), this, SLOT(slotReloadServerSettings()));
    connect(saveServerSettingsButton, SIGNAL(clicked()), this, SLOT(slotSaveServerSettings()));
    connect(applyServerSettingsFormButton, SIGNAL(clicked()), this, SLOT(slotApplyFormToRawConfig()));
    connect(syncServerSettingsFormButton, SIGNAL(clicked()), this, SLOT(slotSyncFormFromRawConfig()));
    connect(serverSettingsModeTabs, SIGNAL(currentChanged(int)), this, SLOT(slotServerSettingsModeChanged(int)));

    // X041: XP progression presets, moved from Advanced -> Arena Settings.
    // Selecting a named profile updates the overall multiplier; entering
    // another value marks the preset Custom.
    connect(xpRatePresetComboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
        this, &PlayPage::slotXpRatePresetChanged);
    connect(xpGainMultiplierSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
        this, &PlayPage::slotXpGainMultiplierChanged);

    pageTabs->setProperty("arenaSegmented", true);
    serverSettingsModeTabs->setProperty("arenaSegmented", true);

    // U015: keep the fixed-size launcher usable by replacing the former single
    // 980px-tall server-settings form with compact category pages.  Every
    // original widget is reparented rather than recreated, so the existing
    // config.lua load/save code keeps the same object pointers and behavior.
    serverSettingsTitleLabel->setProperty("arenaTitle", true);
    serverSettingsPathLabel->setProperty("arenaMuted", true);
    serverSettingsFormInfoLabel->setProperty("arenaMuted", true);
    serverSettingsStatusLabel->setProperty("arenaMuted", true);
    reloadServerSettingsButton->setProperty("arenaQuiet", true);
    saveServerSettingsButton->setProperty("arenaPrimary", true);
    applyServerSettingsFormButton->setProperty("arenaQuiet", true);
    syncServerSettingsFormButton->setProperty("arenaQuiet", true);
    // U021: the explanation moved into the title tooltip; the page itself now
    // uses the space for the settings.
    serverSettingsFormInfoLabel->hide();
    serverSettingsTitleLabel->setToolTip(tr("Main server settings grouped by category. Changes are saved to the persistent config and synchronized to runtime."));
    serverSettingsEditor->setProperty("arenaCodeEditor", true);
    serverSettingsEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
    serverSettingsEditor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    serverSettingsEditor->setTabStopWidth(32);

    QTabWidget* categoryTabs = new QTabWidget(formServerSettingsTab);
    categoryTabs->setObjectName(QStringLiteral("serverSettingsCategoryTabs"));
    categoryTabs->setProperty("arenaSegmented", true);
    categoryTabs->setProperty("arenaCategoryTabs", true);
    configureHorizontalTabs(categoryTabs);
    categoryTabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto addSettingsCategory = [this, categoryTabs](const QString& title,
        std::initializer_list<QGroupBox*> groups)
    {
        QWidget* page = new QWidget(categoryTabs);
        QVBoxLayout* pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->setSpacing(0);

        QScrollArea* scroll = new QScrollArea(page);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setProperty("arenaSettingsScroll", true);

        QWidget* content = new QWidget(scroll);
        QVBoxLayout* layout = new QVBoxLayout(content);
        layout->setContentsMargins(7, 7, 7, 7);
        layout->setSpacing(8);

        for (QGroupBox* group : groups)
        {
            if (group == nullptr)
                continue;
            serverSettingsFormLayout->removeWidget(group);
            group->setParent(content);
            group->setProperty("arenaSettingsGroup", true);
            layout->addWidget(group);
        }
        layout->addStretch(1);
        scroll->setWidget(content);
        pageLayout->addWidget(scroll);
        categoryTabs->addTab(page, title);
    };

    auto* housingGroup = new QGroupBox(QString::fromUtf8("Дома / Housing"), serverSettingsTab);
    auto* housingForm = new QFormLayout(housingGroup);
    auto* housingEnabled = new QCheckBox(QString::fromUtf8("Включить дома / Enable housing"), housingGroup);
    housingEnabled->setObjectName(QStringLiteral("housingEnabled"));
    housingEnabled->setChecked(true);
    housingForm->addRow(housingEnabled);
    auto* housingGeneration = new QComboBox(housingGroup);
    housingGeneration->setObjectName(QStringLiteral("housingGeneration"));
    housingGeneration->addItem(QString::fromUtf8("Игроки / Players"), QStringLiteral("players"));
    housingGeneration->addItem(QString::fromUtf8("Администраторы / Admins"), QStringLiteral("admins"));
    housingGeneration->addItem(QString::fromUtf8("Выключено / Manual only"), QStringLiteral("off"));
    housingForm->addRow(QString::fromUtf8("Автогенерация / Generation"), housingGeneration);
    auto* housingHouses = new QCheckBox(QString::fromUtf8("Дома / Houses"), housingGroup);
    housingHouses->setObjectName(QStringLiteral("housingHouses"));
    housingHouses->setChecked(true);
    housingForm->addRow(housingHouses);
    auto* housingTaverns = new QCheckBox(QString::fromUtf8("Таверны / Taverns"), housingGroup);
    housingTaverns->setObjectName(QStringLiteral("housingTaverns"));
    housingTaverns->setChecked(false);
    housingForm->addRow(housingTaverns);
    auto* housingGuilds = new QCheckBox(QString::fromUtf8("Гильдии / Guilds"), housingGroup);
    housingGuilds->setObjectName(QStringLiteral("housingGuilds"));
    housingGuilds->setChecked(false);
    housingForm->addRow(housingGuilds);
    auto* housingShops = new QCheckBox(QString::fromUtf8("Магазины / Shops"), housingGroup);
    housingShops->setObjectName(QStringLiteral("housingShops"));
    housingShops->setChecked(false);
    housingForm->addRow(housingShops);
    addSettingsCategory(tr("Overview"), { generalSettingsGroupBox, timeSettingsGroupBox, housingGroup });
    addSettingsCategory(tr("Players"), { playerPermissionsGroupBox, spawnAndRespawnGroupBox });
    addSettingsCategory(tr("Combat / NPC"), { arenaMechanicsGroupBox, collisionGroupBox });
    addSettingsCategory(tr("Progress"), { sharingGroupBox, xpLevelingGroupBox });
    addSettingsCategory(tr("Network"), { networkGroupBox });

    // The old scroll-area becomes only a compatibility container.  The widgets
    // above have been moved out of it and it is no longer shown.
    serverSettingsScrollArea->hide();
    formServerSettingsTabLayout->insertWidget(1, categoryTabs, 1);

    // U021: one vertical category list on the left drives both the form
    // categories and the raw editor. The two horizontal tab bars are kept as
    // the state machine (existing load/save code depends on them) but are no
    // longer drawn, and the manual form<->raw buttons disappear because the
    // switch already synchronizes both directions.
    applyServerSettingsFormButton->hide();
    syncServerSettingsFormButton->hide();
    if (serverSettingsModeTabs->tabBar() != nullptr)
        serverSettingsModeTabs->tabBar()->hide();
    if (categoryTabs->tabBar() != nullptr)
        categoryTabs->tabBar()->hide();

    QListWidget* settingsNav = new QListWidget(serverSettingsTab);
    settingsNav->setObjectName(QStringLiteral("serverSettingsNav"));
    settingsNav->setFocusPolicy(Qt::NoFocus);
    settingsNav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    settingsNav->setFixedWidth(184);
    settingsNav->setIconSize(QSize(18, 18));
    settingsNav->setUniformItemSizes(true);
    const struct { const char* icon; QString title; } navEntries[] = {
        { "settings", tr("Overview") },
        { "users", tr("Players") },
        { "advanced", tr("Combat / NPC") },
        { "update", tr("Progress") },
        { "wifi", tr("Network") },
        { "changelog", tr("Raw config.lua") },
    };
    for (const auto& entry : navEntries)
    {
        QListWidgetItem* item = new QListWidgetItem(ArenaUi::glassIcon(QString::fromLatin1(entry.icon)),
            entry.title, settingsNav);
        item->setSizeHint(QSize(176, 34));
    }
    connect(settingsNav, &QListWidget::currentRowChanged, this, [this, categoryTabs](int row)
    {
        if (row < 0)
            return;
        if (row >= categoryTabs->count())
        {
            serverSettingsModeTabs->setCurrentWidget(rawServerSettingsTab);
            return;
        }
        serverSettingsModeTabs->setCurrentWidget(formServerSettingsTab);
        categoryTabs->setCurrentIndex(row);
    });

    serverSettingsTabLayout->removeWidget(serverSettingsModeTabs);
    QHBoxLayout* settingsBody = new QHBoxLayout();
    settingsBody->setSpacing(10);
    settingsBody->addWidget(settingsNav);
    settingsBody->addWidget(serverSettingsModeTabs, 1);
    serverSettingsTabLayout->insertLayout(2, settingsBody, 1);
    settingsNav->setCurrentRow(0);

    // Value fields no longer stretch across the whole page: a checkbox or a
    // number stays next to its label, which is what makes the long lists
    // readable at a glance.
    for (QFormLayout* form : serverSettingsTab->findChildren<QFormLayout*>())
    {
        form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
        form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
        form->setHorizontalSpacing(14);
        form->setVerticalSpacing(7);
    }
    for (QSpinBox* box : serverSettingsTab->findChildren<QSpinBox*>())
        box->setMaximumWidth(140);
    for (QDoubleSpinBox* box : serverSettingsTab->findChildren<QDoubleSpinBox*>())
        box->setMaximumWidth(140);
    for (QComboBox* box : serverSettingsTab->findChildren<QComboBox*>())
        box->setMaximumWidth(240);
    for (QLineEdit* edit : serverSettingsTab->findChildren<QLineEdit*>())
        edit->setMaximumWidth(320);

    serverSettingsModeTabs->setTabText(serverSettingsModeTabs->indexOf(formServerSettingsTab), tr("Visual editor"));
    serverSettingsModeTabs->setTabText(serverSettingsModeTabs->indexOf(rawServerSettingsTab), tr("Raw config.lua"));

    connect(mServerModeCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
        this, [this](int) { updateStatusPanel(); });

    pageTabs->setCurrentIndex(0);
    serverSettingsModeTabs->setCurrentIndex(0);
    categoryTabs->setCurrentIndex(0);
    loadServerSettings();
    updateStatusPanel();
}

void Launcher::PlayPage::setPlayButtonState(const QString& text, bool enabled)
{
    playButton->setText(text);
    playButton->setEnabled(enabled);
    updateStatusPanel();
}

void Launcher::PlayPage::addLaunchOption(QWidget* widget)
{
    if (widget == nullptr)
        return;
    widget->setParent(launchFormFrame);
    widget->setVisible(true);
    launchFormLayout->addWidget(widget, launchFormLayout->rowCount(), 0, 1, 3);
}

void Launcher::PlayPage::setUpdateState(bool checking, bool updateAvailable)
{
    mUpdateChecking = checking;
    mUpdateAvailable = updateAvailable;
    updateStatusPanel();
}

void Launcher::PlayPage::setHardwareInfo(const QString& gpuName, const QString& gpuDetail, int logicalThreads)
{
    mGpuName = gpuName.trimmed();
    mGpuDetail = gpuDetail.trimmed();
    mLogicalThreads = logicalThreads;
    updateStatusPanel();
}

void Launcher::PlayPage::updateStatusPanel()
{
    if (statusHeadlineLabel == nullptr)
        return;

    const bool host = autoStartServerCheckBox->isChecked();
    const bool alternate = mAlternativeServer != nullptr && mAlternativeServer->isChecked();
    const bool fromDirectory=!mSelectedMasterServer.isEmpty();
    if(mHostInterfaceCombo) {
        mHostInterfaceLabel->setVisible(host);
        mHostInterfaceCombo->setVisible(host);
        mRefreshHostInterfacesButton->setVisible(host);
    }
    if(mAlternativeServer) {
        mAlternativeServer->setVisible(!fromDirectory);
        alternativeServerHintLabel->setVisible(!fromDirectory);
        mAlternativeAddress->setVisible(alternate && !fromDirectory);
        mAlternativePort->setVisible(alternate && !fromDirectory);
    }
    if (mStatusQuery)
    {
        const QString address = host ? QStringLiteral("127.0.0.1")
            : alternate ? alternativeAddress() : serverAddress();
        const QString portText = host && !mRunningPort.isEmpty() ? mRunningPort
            : alternate ? alternativePort() : serverPort();
        bool valid = false;
        const int port = portText.toInt(&valid);
        mRemoteEndpoint->setText(address.isEmpty() ? QStringLiteral("—") : QStringLiteral("%1:%2").arg(address, portText));
        if(!mSelectedMasterServer.isEmpty()) {
            mRemoteEndpoint->setText(mSelectedMasterServer.value("name").toString()+QStringLiteral("\n")+
                mSelectedMasterServer.value("build_name").toString()+QStringLiteral(" · ")+mSelectedMasterServer.value("game_mode").toString());
            mRemoteEndpoint->setToolTip(mSelectedMasterServer.value("id").toString());
        }
        if(mSelectedMasterServer.isEmpty())mStatusQuery->setEndpoint(address, valid && port > 0 && port <= 65535 ? static_cast<quint16>(port) : 0);
        else {
            mStatusQuery->setEndpoint(QString(),0);
            mRemoteStatus->setText(mMasterPreparing?tr("Подключение…"):tr("Выбран"));
            mRemotePlayers->setText(QStringLiteral("%1 / %2").arg(mSelectedMasterServer.value("players").toInt()).arg(mSelectedMasterServer.value("max_players").toInt()));
            mRemotePlayers->setToolTip(tr("Данные последнего запроса к мастеру; обновляются перед запуском."));
            mRemoteUptime->setText(QStringLiteral("—"));
        }
    }

    // Headline: the single most important launcher state.
    QString state;
    QString headline;
    QString detail;
    if (mUpdateChecking)
    {
        state = QStringLiteral("busy");
        headline = tr("Checking for updates...");
        detail = tr("Comparing the installed build with the update server");
    }
    else if (mUpdateAvailable)
    {
        state = QStringLiteral("warn");
        headline = tr("Update available");
        detail = tr("Press Update at the bottom of the window to install it");
    }
    else if (mServerRunning)
    {
        state = QStringLiteral("online");
        headline = tr("Server is running");
        detail = mRunningAddress.isEmpty()
            ? tr("The local server accepts connections")
            : tr("Players connect to %1:%2").arg(mRunningAddress, mRunningPort);
    }
    else
    {
        state = QStringLiteral("ready");
        headline = tr("Ready to launch");
        detail = tr("All systems are working normally");
    }
    statusHeadlineLabel->setText(headline);
    statusDetailLabel->setText(detail);
    setStatusProperty(statusHeadlineLabel, state);
    setStatusProperty(statusDotLabel, state);

    // Hardware (filled by MainDialog from the Graphics page detection).
    if (mGpuName.isEmpty())
        gpuValueLabel->setText(tr("Detecting..."));
    else if (mGpuDetail.isEmpty())
        gpuValueLabel->setText(mGpuName.toHtmlEscaped());
    else
        gpuValueLabel->setText(QStringLiteral("%1<br><span style=\"color:#8f8a82; font-size:10px;\">%2</span>")
            .arg(mGpuName.toHtmlEscaped(), mGpuDetail.toHtmlEscaped()));
    gpuValueLabel->setToolTip(mGpuDetail.isEmpty() ? mGpuName : mGpuName + QLatin1Char('\n') + mGpuDetail);
    cpuValueLabel->setText(mLogicalThreads > 0
        ? tr("Threads: %1").arg(mLogicalThreads) : tr("Detecting..."));

    // Mode and the endpoint that the next launch will use.
    const QString bind = hostBindAddress();
    if (host)
    {
        modeValueLabel->setText(tr("Host"));
        endpointKeyLabel->setText(tr("Interface:"));
        QString bindHint;
        if (bind == QLatin1String("0.0.0.0"))
            bindHint = tr("all interfaces");
        else if (bind == QLatin1String("127.0.0.1"))
            bindHint = tr("this PC only");
        endpointValueLabel->setText(bindHint.isEmpty() ? bind
            : QStringLiteral("%1<br><span style=\"color:#8f8a82; font-size:10px;\">(%2)</span>").arg(bind, bindHint.toHtmlEscaped()));
    }
    else
    {
        modeValueLabel->setText(alternate ? tr("Another server") : tr("Client"));
        endpointKeyLabel->setText(tr("Server:"));
        const QString address = alternate ? alternativeAddress() : serverAddress();
        const QString port = alternate ? alternativePort() : serverPort();
        endpointValueLabel->setText(address.isEmpty() ? QStringLiteral("\u2014")
            : QStringLiteral("%1:%2").arg(address, port.isEmpty() ? QStringLiteral("25565") : port).toHtmlEscaped());
    }
    buildValueLabel->setText(buildName());

    const QString mode = mServerModeCombo != nullptr ? mServerModeCombo->currentData().toString() : QString();
    if (mode == QLatin1String("CUSTOM"))
        presetValueLabel->setText(tr("Custom"));
    else
        presetValueLabel->setText(mode.isEmpty() ? QStringLiteral("\u2014") : mode);

    // Hero button subtitle: where "Start game" is going to connect.
    playButton->setIcon(ArenaUi::glassIcon(mUpdateAvailable
        ? QStringLiteral("update-dark") : QStringLiteral("play-dark")));
    if (mUpdateChecking)
        playButton->setSubtitle(tr("Please wait a moment"));
    else if (mUpdateAvailable)
        playButton->setSubtitle(tr("Install the new build version first"));
    else if (fromDirectory)
        playButton->setSubtitle(tr("Connect to the selected server"));
    else if (host)
        playButton->setSubtitle(tr("Local server + game on port %1").arg(serverPort()));
    else if (alternate)
        playButton->setSubtitle(alternativeAddress().isEmpty()
            ? tr("Enter the server address")
            : tr("Connect to %1:%2").arg(alternativeAddress(), alternativePort()));
    else
        playButton->setSubtitle(tr("Connect to the selected server"));
    // Keep the glow calm while checking, brighter when an update is waiting.
    playButton->setPulse(!mUpdateChecking, mUpdateAvailable);

    // Contextual tip instead of static marketing text.
    if (mUpdateAvailable)
    {
        tipTitleLabel->setText(tr("A new version is ready"));
        tipTextLabel->setText(tr("Saves, server data and settings are kept. The launcher restarts by itself."));
    }
    else if (mServerRunning)
    {
        tipTitleLabel->setText(tr("Your server is online"));
        tipTextLabel->setText(tr("Friends connect to your Radmin VPN or public IP, port %1. Live log: Server Console tab.")
            .arg(mRunningPort.isEmpty() ? serverPort() : mRunningPort));
    }
    else if (host)
    {
        tipTitleLabel->setText(tr("Playing with friends?"));
        tipTextLabel->setText(tr("The local server starts first. For Internet play forward UDP port %1 or use Radmin VPN.")
            .arg(serverPort()));
    }
    else if (alternate)
    {
        tipTitleLabel->setText(tr("Another server"));
        tipTextLabel->setText(tr("The server from build.ini is ignored. Content files must match the chosen server."));
    }
    else
    {
        tipTitleLabel->setText(tr("Ready for adventure?"));
        tipTextLabel->setText(tr("Adjust the settings, start the game or host your own server. Have a good game on ArenaMP!"));
    }
}

namespace
{
    const std::array<double, 5> sXpRatePresets = { 0.50, 0.75, 1.00, 1.50, 2.00 };
}

void Launcher::PlayPage::slotXpRatePresetChanged(int index)
{
    if (mSyncingXpControls)
        return;
    if (index < 0 || index >= static_cast<int>(sXpRatePresets.size()))
        return; // "Custom"

    mSyncingXpControls = true;
    xpGainMultiplierSpinBox->setValue(sXpRatePresets[index]);
    mSyncingXpControls = false;
}

void Launcher::PlayPage::slotXpGainMultiplierChanged(double value)
{
    if (mSyncingXpControls)
        return;

    int preset = static_cast<int>(sXpRatePresets.size()); // Custom
    for (int i = 0; i < static_cast<int>(sXpRatePresets.size()); ++i)
    {
        if (std::abs(value - sXpRatePresets[i]) < 0.001)
        {
            preset = i;
            break;
        }
    }

    if (xpRatePresetComboBox->currentIndex() != preset)
    {
        mSyncingXpControls = true;
        xpRatePresetComboBox->setCurrentIndex(preset);
        mSyncingXpControls = false;
    }
}

void Launcher::PlayPage::loadXpProgressionSettings()
{
    mSyncingXpControls = true;
    xpLevelingEnabledCheckBox->setChecked(Settings::Manager::getBool("enabled", "XP Leveling"));
    xpGainMultiplierSpinBox->setValue(Settings::Manager::getFloat("xp gain multiplier", "XP Leveling"));
    skillXpMultiplierSpinBox->setValue(Settings::Manager::getFloat("global XP gain multiplier", "Game"));
    baseXpToLevelSpinBox->setValue(Settings::Manager::getInt("base xp to level", "XP Leveling"));
    skillPointsPerLevelSpinBox->setValue(Settings::Manager::getInt("skill points per level", "XP Leveling"));
    deathXpLossSpinBox->setValue(100.0 * Settings::Manager::getFloat("death xp loss fraction", "XP Leveling"));
    difficultyXpScalingCheckBox->setChecked(Settings::Manager::getBool("difficulty xp scaling", "XP Leveling"));
    progressiveXpCurveCheckBox->setChecked(Settings::Manager::getBool("progressive xp curve", "XP Leveling"));
    showXpNotificationsCheckBox->setChecked(Settings::Manager::getBool("show xp notifications", "XP Leveling"));
    mSyncingXpControls = false;

    // Reflect the loaded multiplier in the preset combo without echoing back.
    slotXpGainMultiplierChanged(xpGainMultiplierSpinBox->value());
}

bool Launcher::PlayPage::saveXpProgressionSettings()
{
    // The game may have rewritten settings.cfg while the Launcher stayed open,
    // so re-read it and touch only the keys this group owns.
    const std::string path = Settings::Manager::mUserSettingsPath;
    if (path.empty())
        return false;

    Settings::Manager::mUserSettings.clear();
    Settings::Manager::mChangedSettings.clear();

    if (QFileInfo::exists(QString::fromUtf8(path.c_str())))
    {
        try
        {
            Settings::SettingsFileParser parser;
            parser.loadSettingsFile(path, Settings::Manager::mUserSettings);
        }
        catch (const std::exception& e)
        {
            setServerSettingsStatus(tr("Could not read settings.cfg: %1").arg(QString::fromUtf8(e.what())), true);
            return false;
        }
    }

    Settings::Manager::setBool("enabled", "XP Leveling", xpLevelingEnabledCheckBox->isChecked());
    Settings::Manager::setFloat("xp gain multiplier", "XP Leveling",
        static_cast<float>(xpGainMultiplierSpinBox->value()));
    Settings::Manager::setFloat("global XP gain multiplier", "Game",
        static_cast<float>(skillXpMultiplierSpinBox->value()));
    Settings::Manager::setInt("base xp to level", "XP Leveling", baseXpToLevelSpinBox->value());
    Settings::Manager::setInt("skill points per level", "XP Leveling", skillPointsPerLevelSpinBox->value());
    Settings::Manager::setFloat("death xp loss fraction", "XP Leveling",
        static_cast<float>(deathXpLossSpinBox->value() / 100.0));
    Settings::Manager::setBool("difficulty xp scaling", "XP Leveling", difficultyXpScalingCheckBox->isChecked());
    Settings::Manager::setBool("progressive xp curve", "XP Leveling", progressiveXpCurveCheckBox->isChecked());
    Settings::Manager::setBool("show xp notifications", "XP Leveling", showXpNotificationsCheckBox->isChecked());

    try
    {
        Settings::Manager::saveUser();
    }
    catch (const std::exception& e)
    {
        setServerSettingsStatus(tr("Could not write settings.cfg: %1").arg(QString::fromUtf8(e.what())), true);
        return false;
    }

    return true;
}


void Launcher::PlayPage::setBuildName(const QString& name)
{
    buildNameEdit->setText(name.trimmed().isEmpty() ? QStringLiteral("ArenaMP") : name.trimmed());
}

QString Launcher::PlayPage::buildName() const
{
    const QString name = buildNameEdit->text().trimmed();
    return name.isEmpty() ? QStringLiteral("ArenaMP") : name;
}

void Launcher::PlayPage::setServerAddress(const QString& addr)
{
    serverAddressEdit->setText(addr);
}

void Launcher::PlayPage::setServerPort(const QString& port)
{
    serverPortEdit->setText(port);
}

void Launcher::PlayPage::setBuildManifestComplete(bool complete)
{
    buildNameEdit->setReadOnly(complete);
    buildNameEdit->setFocusPolicy(complete ? Qt::NoFocus : Qt::StrongFocus);
    buildNameEdit->setToolTip(complete
        ? tr("The build name is locked by build.ini (complete=true).") : QString());

    serverLabel->setVisible(!complete);
    serverAddressEdit->setVisible(!complete);
    serverPortEdit->setVisible(!complete);

}

void Launcher::PlayPage::setAutoStartServer(bool enabled)
{
    autoStartServerCheckBox->setChecked(enabled);
}

void Launcher::PlayPage::setAutoRestartServer(bool enabled)
{
    autoRestartServerCheckBox->setChecked(enabled);
}

void Launcher::PlayPage::setEnforceDataFiles(bool enabled)
{
    if (mEnforceRequiredCheckBox != nullptr)
    {
        const QSignalBlocker blocker(mEnforceRequiredCheckBox);
        mEnforceRequiredCheckBox->setChecked(enabled);
    }
    if (enforceDataFilesCheckBox != nullptr)
        enforceDataFilesCheckBox->setChecked(enabled);
}

bool Launcher::PlayPage::enforceDataFiles() const
{
    return mEnforceRequiredCheckBox != nullptr
        ? mEnforceRequiredCheckBox->isChecked()
        : enforceDataFilesCheckBox->isChecked();
}



void Launcher::PlayPage::setServerRunning(bool running, const QString& address, const QString& port, bool managed)
{
    stopServerButton->setEnabled(running && managed);
    serverButton->setEnabled(!running);
    stopServerButton->setIcon(ArenaUi::glassIcon(running && managed ? QStringLiteral("stop") : QStringLiteral("stop-muted")));
    mServerRunning = running;
    mRunningAddress = running ? address : QString();
    mRunningPort = running ? port : QString();
    updateStatusPanel();

    // The running server endpoint is status information only. Do not write it
    // back into the editable fields: doing so used to replace the user's
    // address/port with the launcher's LAN/default endpoint as soon as a
    // local server was started.
}

void Launcher::PlayPage::setServerConsoleWidget(QWidget* widget)
{
    if (widget == nullptr)
        return;

    if (mEmbeddedServerConsole == widget)
        return;

    if (mEmbeddedServerConsole != nullptr)
    {
        serverConsoleHostLayout->removeWidget(mEmbeddedServerConsole);
        mEmbeddedServerConsole->setParent(nullptr);
    }

    mEmbeddedServerConsole = widget;
    widget->setParent(serverConsoleHost);
    widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    serverConsoleHostLayout->addWidget(widget);
}

QString Launcher::PlayPage::serverAddress() const
{
    QString addr = serverAddressEdit->text().trimmed();
    return addr.isEmpty() ? QString("localhost") : addr;
}

bool Launcher::PlayPage::autoStartServer() const
{
    return autoStartServerCheckBox->isChecked();
}

bool Launcher::PlayPage::autoRestartServer() const
{
    return autoRestartServerCheckBox->isChecked();
}



QString Launcher::PlayPage::serverPort() const
{
    QString p = serverPortEdit->text().trimmed();
    return p.isEmpty() ? QString("25565") : p;
}

QString Launcher::PlayPage::hostBindAddress() const
{
    if (mHostInterfaceCombo == nullptr)
        return QStringLiteral("0.0.0.0");

    const QString address = mHostInterfaceCombo->currentData().toString().trimmed();
    return address.isEmpty() ? QStringLiteral("0.0.0.0") : address;
}

void Launcher::PlayPage::setHostBindAddress(const QString& address)
{
    const QString wanted = address.trimmed().isEmpty() ? QStringLiteral("0.0.0.0") : address.trimmed();
    refreshHostInterfaces(wanted);
}

void Launcher::PlayPage::refreshHostInterfaces(const QString& preferredAddress)
{
    if (mHostInterfaceCombo == nullptr)
        return;

    const QString previous = preferredAddress.isEmpty() ? hostBindAddress() : preferredAddress;
    mHostInterfaceCombo->clear();
    mHostInterfaceCombo->addItem(tr("All interfaces"), QStringLiteral("0.0.0.0"));
    mHostInterfaceCombo->setItemData(0,tr("All interfaces (recommended) - 0.0.0.0"),Qt::ToolTipRole);
    mHostInterfaceCombo->addItem(tr("Local only - 127.0.0.1"), QStringLiteral("127.0.0.1"));

    QStringList seenAddresses;
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : interfaces)
    {
        if (!(iface.flags() & QNetworkInterface::IsUp) || !(iface.flags() & QNetworkInterface::IsRunning))
            continue;

        for (const QNetworkAddressEntry& entry : iface.addressEntries())
        {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;

            const QString address = entry.ip().toString();
            if (address.isEmpty() || address == QLatin1String("0.0.0.0")
                || address == QLatin1String("127.0.0.1") || address.startsWith(QLatin1String("169.254."))
                || seenAddresses.contains(address))
                continue;

            seenAddresses.append(address);
            QString name = iface.humanReadableName().trimmed();
            if (name.isEmpty())
                name = iface.name();
            mHostInterfaceCombo->addItem(QStringLiteral("%1 - %2").arg(name, address), address);
        }
    }

    int index = mHostInterfaceCombo->findData(previous);
    if (index < 0)
    {
        // Keep a previously selected adapter address visible if the VPN is
        // temporarily down. The user can refresh when it comes back.
        if (!previous.isEmpty() && previous != QLatin1String("0.0.0.0"))
        {
            mHostInterfaceCombo->addItem(tr("Unavailable interface - %1").arg(previous), previous);
            index = mHostInterfaceCombo->count() - 1;
        }
        else
            index = 0;
    }
    mHostInterfaceCombo->setCurrentIndex(index);
}

void Launcher::PlayPage::updateHostModeUi(bool enabled)
{
    // U020: the local-server card is always visible, because "Run Server"
    // works with and without Host mode. Host mode only changes what the
    // "Start game" button does and how the address field is interpreted.

    serverLabel->setText(enabled ? tr("Address for players:") : tr("Server Address:"));
    serverAddressEdit->setPlaceholderText(enabled
        ? tr("Radmin/VPN IP or public IP (optional)")
        : tr("192.168.x.x"));
    serverAddressEdit->setToolTip(enabled
        ? tr("Address you give to other players. It is not used as the server bind address. "
             "For a public Internet IP, forward the UDP server port on your router to this PC. "
             "For Radmin VPN, enter the Radmin IPv4 address or leave this as your preferred share address.")
        : QString());
}

void Launcher::PlayPage::switchToServerConsoleTab()
{
    pageTabs->setCurrentWidget(serverConsoleTab);
}

QString Launcher::PlayPage::serverConfigPath() const
{
    const QDir baseDir(QApplication::applicationDirPath());
#ifdef Q_OS_MAC
    return QDir::cleanPath(
        baseDir.filePath(QStringLiteral("../Resources/server/scripts/config.lua")));
#else
    return QDir::cleanPath(
        baseDir.filePath(QStringLiteral("server/scripts/config.lua")));
#endif
}

QString Launcher::PlayPage::persistentServerConfigPath() const
{
#ifdef Q_OS_MAC
    QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (basePath.isEmpty())
        basePath = QApplication::applicationDirPath();
    const QDir baseDir(basePath);
#else
    const QDir baseDir(QApplication::applicationDirPath());
#endif
    return QDir::cleanPath(baseDir.filePath(QStringLiteral("userdata/server-config.lua")));
}

bool Launcher::PlayPage::writeServerConfigFile(const QString& path, const QString& text, QString* errorMessage) const
{
    const QFileInfo info(path);
    QDir dir = info.absoluteDir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
    {
        if (errorMessage != nullptr)
            *errorMessage = tr("Could not create directory: %1").arg(QDir::toNativeSeparators(dir.absolutePath()));
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        if (errorMessage != nullptr)
            *errorMessage = tr("Could not write: %1").arg(QDir::toNativeSeparators(path));
        return false;
    }

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << text;
    file.close();
    return file.error() == QFile::NoError;
}

QString Launcher::PlayPage::replaceRawValue(const QString& text, const QString& key, const QString& value) const
{
    return replaceConfigAssignment(text, key, value);
}

void Launcher::PlayPage::setServerSettingsStatus(const QString& text, bool isError)
{
    serverSettingsStatusLabel->setText(text);
    serverSettingsStatusLabel->setStyleSheet(isError
        ? QStringLiteral("color: #9f1f1f; font-weight: 600;")
        : QStringLiteral("color: #245027; font-weight: 600;"));
}

void Launcher::PlayPage::populateFormFromConfig(const QString& text)
{
    auto* housingEnabled = findChild<QCheckBox*>(QStringLiteral("housingEnabled"));
    auto* housingGeneration = findChild<QComboBox*>(QStringLiteral("housingGeneration"));
    housingEnabled->setChecked(true); housingGeneration->setCurrentIndex(0);
    loadCheckBox(housingEnabled, text, QStringLiteral("houseEnabled"));
    loadComboBox(housingGeneration, text, QStringLiteral("houseAutoGeneration"));
    findChild<QCheckBox*>(QStringLiteral("housingHouses"))->setChecked(true);
    loadCheckBox(findChild<QCheckBox*>(QStringLiteral("housingHouses")), text, QStringLiteral("houseGenerateHouses"));
    findChild<QCheckBox*>(QStringLiteral("housingTaverns"))->setChecked(false);
    loadCheckBox(findChild<QCheckBox*>(QStringLiteral("housingTaverns")), text, QStringLiteral("houseGenerateTaverns"));
    findChild<QCheckBox*>(QStringLiteral("housingGuilds"))->setChecked(false);
    loadCheckBox(findChild<QCheckBox*>(QStringLiteral("housingGuilds")), text, QStringLiteral("houseGenerateGuilds"));
    findChild<QCheckBox*>(QStringLiteral("housingShops"))->setChecked(false);
    loadCheckBox(findChild<QCheckBox*>(QStringLiteral("housingShops")), text, QStringLiteral("houseGenerateShops"));

    if (QComboBox* serverLanguageCombo = findChild<QComboBox*>(QStringLiteral("serverLanguageComboBox")))
        loadComboBox(serverLanguageCombo, text, QStringLiteral("serverLanguage"));
    loadLineEdit(gameModeEdit, text, QStringLiteral("gameMode"));
    loadLineEdit(dataPathEdit, text, QStringLiteral("dataPath"));

    loadSpinBox(loginTimeSpinBox, text, QStringLiteral("loginTime"));
    loadSpinBox(maxClientsPerIPSpinBox, text, QStringLiteral("maxClientsPerIP"));
    loadSpinBox(difficultySpinBox, text, QStringLiteral("difficulty"));
    loadSpinBox(nightStartHourSpinBox, text, QStringLiteral("nightStartHour"));
    loadSpinBox(nightEndHourSpinBox, text, QStringLiteral("nightEndHour"));
    loadSpinBox(deathTimeSpinBox, text, QStringLiteral("deathTime"));
    loadSpinBox(deathPenaltyJailDaysSpinBox, text, QStringLiteral("deathPenaltyJailDays"));
    loadSpinBox(fixmeIntervalSpinBox, text, QStringLiteral("fixmeInterval"));
    loadSpinBox(pingDifferenceSpinBox, text, QStringLiteral("pingDifferenceRequiredForAuthority"));
    loadSpinBox(enforcedLogLevelSpinBox, text, QStringLiteral("enforcedLogLevel"));
    loadSpinBox(physicsFramerateSpinBox, text, QStringLiteral("physicsFramerate"));

    loadCheckBox(arenaTacticalCombatCheckBox, text, QStringLiteral("arenaTacticalCombat"));
    loadDoubleSpinBox(arenaCombatWeaponSheatheDelayDoubleSpinBox, text, QStringLiteral("arenaCombatWeaponSheatheDelay"));
    loadCheckBox(arenaCombatPursuitThroughDoorsCheckBox, text, QStringLiteral("arenaCombatPursuitThroughDoors"));
    loadSpinBox(arenaCombatPursuitGuaranteedDistanceSpinBox, text, QStringLiteral("arenaCombatPursuitGuaranteedDistance"));
    loadSpinBox(arenaCombatPursuitDoorMaxDistanceSpinBox, text, QStringLiteral("arenaCombatPursuitDoorMaxDistance"));
    loadDoubleSpinBox(arenaCombatPursuitMinimumChanceDoubleSpinBox, text, QStringLiteral("arenaCombatPursuitMinimumChance"));
    loadSpinBox(arenaCombatPursuitMaxActorsSpinBox, text, QStringLiteral("arenaCombatPursuitMaxActors"));
    loadSpinBox(arenaCombatPursuitMaxDistanceSpinBox, text, QStringLiteral("arenaCombatPursuitMaxDistance"));
    loadCheckBox(arenaFollowersAttackOnSightCheckBox, text, QStringLiteral("arenaFollowersAttackOnSight"));
    loadCheckBox(arenaNpcAvoidCollisionsCheckBox, text, QStringLiteral("arenaNpcAvoidCollisions"));
    loadCheckBox(arenaNpcGiveWayCheckBox, text, QStringLiteral("arenaNpcGiveWay"));
    loadCheckBox(arenaAllowActorsFollowOverWaterCheckBox, text, QStringLiteral("arenaAllowActorsFollowOverWater"));
    loadSpinBox(arenaActorsProcessingRangeSpinBox, text, QStringLiteral("arenaActorsProcessingRange"));
    loadCheckBox(arenaCanLootDuringDeathAnimationCheckBox, text, QStringLiteral("arenaCanLootDuringDeathAnimation"));
    loadCheckBox(arenaWeaponSheathingCheckBox, text, QStringLiteral("arenaWeaponSheathing"));
    loadCheckBox(arenaShieldSheathingCheckBox, text, QStringLiteral("arenaShieldSheathing"));
    loadCheckBox(arenaGraphicHerbalismCheckBox, text, QStringLiteral("arenaGraphicHerbalism"));
    loadCheckBox(arenaLongBladesUseAgilityCheckBox, text, QStringLiteral("arenaLongBladesUseAgility"));
    loadCheckBox(arenaTwoHandedAccuracyPenaltyCheckBox, text, QStringLiteral("arenaTwoHandedAccuracyPenalty"));
    loadCheckBox(arenaStavesAccuracyBonusCheckBox, text, QStringLiteral("arenaStavesAccuracyBonus"));
    loadCheckBox(arenaSkillBooksLevelLimitCheckBox, text, QStringLiteral("arenaSkillBooksLevelLimit"));
    loadCheckBox(arenaNewConstantEffectDifficultyCheckBox, text, QStringLiteral("arenaNewConstantEffectDifficulty"));
    loadDoubleSpinBox(arenaGlobalXpMultiplierDoubleSpinBox, text, QStringLiteral("arenaGlobalXpMultiplier"));

    loadCheckBox(passTimeWhenEmptyCheckBox, text, QStringLiteral("passTimeWhenEmpty"));
    loadCheckBox(allowConsoleCheckBox, text, QStringLiteral("allowConsole"));
    loadCheckBox(allowBedRestCheckBox, text, QStringLiteral("allowBedRest"));
    loadCheckBox(allowWildernessRestCheckBox, text, QStringLiteral("allowWildernessRest"));
    loadCheckBox(allowWaitCheckBox, text, QStringLiteral("allowWait"));
    loadCheckBox(useInstancedSpawnCheckBox, text, QStringLiteral("useInstancedSpawn"));
    loadCheckBox(respawnAtImperialShrineCheckBox, text, QStringLiteral("respawnAtImperialShrine"));
    loadCheckBox(respawnAtTribunalTempleCheckBox, text, QStringLiteral("respawnAtTribunalTemple"));
    loadCheckBox(playersRespawnCheckBox, text, QStringLiteral("playersRespawn"));
    loadCheckBox(bountyResetOnDeathCheckBox, text, QStringLiteral("bountyResetOnDeath"));
    loadCheckBox(bountyDeathPenaltyCheckBox, text, QStringLiteral("bountyDeathPenalty"));
    loadCheckBox(allowSuicideCommandCheckBox, text, QStringLiteral("allowSuicideCommand"));
    loadCheckBox(allowFixmeCommandCheckBox, text, QStringLiteral("allowFixmeCommand"));
    loadCheckBox(allowOnContainerForUnloadedCellsCheckBox, text, QStringLiteral("allowOnContainerForUnloadedCells"));
    loadCheckBox(enablePlayerCollisionCheckBox, text, QStringLiteral("enablePlayerCollision"));
    loadCheckBox(enableActorCollisionCheckBox, text, QStringLiteral("enableActorCollision"));
    loadCheckBox(enablePlacedObjectCollisionCheckBox, text, QStringLiteral("enablePlacedObjectCollision"));
    loadCheckBox(useActorCollisionForPlacedObjectsCheckBox, text, QStringLiteral("useActorCollisionForPlacedObjects"));
    loadCheckBox(enforceDataFilesCheckBox, text, QStringLiteral("enforceDataFiles"));
    if (mEnforceRequiredCheckBox != nullptr)
    {
        const QSignalBlocker blocker(mEnforceRequiredCheckBox);
        mEnforceRequiredCheckBox->setChecked(enforceDataFilesCheckBox->isChecked());
    }
    loadCheckBox(ignoreScriptErrorsCheckBox, text, QStringLiteral("ignoreScriptErrors"));

    loadCheckBox(shareJournalCheckBox, text, QStringLiteral("shareJournal"));
    loadCheckBox(shareFactionRanksCheckBox, text, QStringLiteral("shareFactionRanks"));
    loadCheckBox(shareFactionExpulsionCheckBox, text, QStringLiteral("shareFactionExpulsion"));
    loadCheckBox(shareFactionReputationCheckBox, text, QStringLiteral("shareFactionReputation"));
    loadCheckBox(shareTopicsCheckBox, text, QStringLiteral("shareTopics"));
    loadCheckBox(shareBountyCheckBox, text, QStringLiteral("shareBounty"));
    loadCheckBox(shareReputationCheckBox, text, QStringLiteral("shareReputation"));
    loadCheckBox(shareMapExplorationCheckBox, text, QStringLiteral("shareMapExploration"));
    loadCheckBox(shareVideosCheckBox, text, QStringLiteral("shareVideos"));
    loadCheckBox(shareKillsCheckBox, text, QStringLiteral("shareKills"));

    if (mServerModeCombo != nullptr)
    {
        // Y032: do not display MMO for a mixed set of server rules. Y014 made
        // journal/topics shared by default while the other progression flags
        // stayed personal, so the old "not fully CO-OP == MMO" test hid the
        // mismatch and selecting the already-visible MMO row emitted no signal.
        const bool coop = gameModeEdit->text() == QLatin1String("ArenaMP CO-OP")
            && shareJournalCheckBox->isChecked()
            && shareFactionRanksCheckBox->isChecked()
            && shareFactionExpulsionCheckBox->isChecked()
            && shareFactionReputationCheckBox->isChecked()
            && shareTopicsCheckBox->isChecked()
            && !shareBountyCheckBox->isChecked()
            && shareReputationCheckBox->isChecked()
            && shareMapExplorationCheckBox->isChecked()
            && shareVideosCheckBox->isChecked();

        const bool mmo = gameModeEdit->text() == QLatin1String("ArenaMP MMO")
            && !shareJournalCheckBox->isChecked()
            && !shareFactionRanksCheckBox->isChecked()
            && !shareFactionExpulsionCheckBox->isChecked()
            && !shareFactionReputationCheckBox->isChecked()
            && !shareTopicsCheckBox->isChecked()
            && !shareBountyCheckBox->isChecked()
            && !shareReputationCheckBox->isChecked()
            && !shareMapExplorationCheckBox->isChecked()
            && !shareVideosCheckBox->isChecked();

        const QString preset = coop ? QStringLiteral("CO-OP")
            : (mmo ? QStringLiteral("MMO") : QStringLiteral("CUSTOM"));
        const QSignalBlocker blocker(mServerModeCombo);
        const int presetIndex = mServerModeCombo->findData(preset);
        if (presetIndex >= 0)
            mServerModeCombo->setCurrentIndex(presetIndex);
    }

    loadComboBox(databaseTypeComboBox, text, QStringLiteral("databaseType"));
}

QString Launcher::PlayPage::updatedConfigFromForm(const QString& input) const
{
    QString text = input;

    const auto replaceString = [&text](const QString& key, const QString& value)
    {
        text = replaceConfigAssignment(text, key, QStringLiteral("\"") + value + QStringLiteral("\""));
    };

    const auto replaceNumber = [&text](const QString& key, int value)
    {
        text = replaceConfigAssignment(text, key, QString::number(value));
    };

    const auto replaceReal = [&text](const QString& key, double value, int precision)
    {
        text = replaceConfigAssignment(text, key, QString::number(value, 'f', precision));
    };

    const auto replaceBool = [&text](const QString& key, bool value)
    {
        text = replaceConfigAssignment(text, key, value ? QStringLiteral("true") : QStringLiteral("false"));
    };

    replaceBool(QStringLiteral("houseEnabled"), findChild<QCheckBox*>(QStringLiteral("housingEnabled"))->isChecked());
    replaceString(QStringLiteral("houseAutoGeneration"), findChild<QComboBox*>(QStringLiteral("housingGeneration"))->currentData().toString());
    replaceBool(QStringLiteral("houseGenerateHouses"), findChild<QCheckBox*>(QStringLiteral("housingHouses"))->isChecked());
    replaceBool(QStringLiteral("houseGenerateTaverns"), findChild<QCheckBox*>(QStringLiteral("housingTaverns"))->isChecked());
    replaceBool(QStringLiteral("houseGenerateGuilds"), findChild<QCheckBox*>(QStringLiteral("housingGuilds"))->isChecked());
    replaceBool(QStringLiteral("houseGenerateShops"), findChild<QCheckBox*>(QStringLiteral("housingShops"))->isChecked());
    const QComboBox* serverLanguageCombo = findChild<QComboBox*>(QStringLiteral("serverLanguageComboBox"));
    const QString serverLanguage = serverLanguageCombo != nullptr
        ? serverLanguageCombo->currentData().toString()
        : QStringLiteral("AUTO");
    replaceString(QStringLiteral("serverLanguage"),
        serverLanguage.isEmpty() ? QStringLiteral("AUTO") : serverLanguage);
    replaceString(QStringLiteral("gameMode"), gameModeEdit->text().trimmed());

    const QString dataPathValue = dataPathEdit->text().trimmed();
    if (dataPathValue == QStringLiteral("tes3mp.GetDataPath()"))
        text = replaceRawValue(text, QStringLiteral("dataPath"), dataPathValue);
    else
        replaceString(QStringLiteral("dataPath"), dataPathValue);

    replaceNumber(QStringLiteral("loginTime"), loginTimeSpinBox->value());
    replaceNumber(QStringLiteral("maxClientsPerIP"), maxClientsPerIPSpinBox->value());
    replaceNumber(QStringLiteral("difficulty"), difficultySpinBox->value());
    replaceNumber(QStringLiteral("nightStartHour"), nightStartHourSpinBox->value());
    replaceNumber(QStringLiteral("nightEndHour"), nightEndHourSpinBox->value());
    replaceNumber(QStringLiteral("deathTime"), deathTimeSpinBox->value());
    replaceNumber(QStringLiteral("deathPenaltyJailDays"), deathPenaltyJailDaysSpinBox->value());
    replaceNumber(QStringLiteral("fixmeInterval"), fixmeIntervalSpinBox->value());
    replaceNumber(QStringLiteral("pingDifferenceRequiredForAuthority"), pingDifferenceSpinBox->value());
    replaceNumber(QStringLiteral("enforcedLogLevel"), enforcedLogLevelSpinBox->value());
    replaceNumber(QStringLiteral("physicsFramerate"), physicsFramerateSpinBox->value());

    replaceBool(QStringLiteral("arenaTacticalCombat"), arenaTacticalCombatCheckBox->isChecked());
    replaceReal(QStringLiteral("arenaCombatWeaponSheatheDelay"), arenaCombatWeaponSheatheDelayDoubleSpinBox->value(), 1);
    replaceBool(QStringLiteral("arenaCombatPursuitThroughDoors"), arenaCombatPursuitThroughDoorsCheckBox->isChecked());
    replaceNumber(QStringLiteral("arenaCombatPursuitGuaranteedDistance"), arenaCombatPursuitGuaranteedDistanceSpinBox->value());
    replaceNumber(QStringLiteral("arenaCombatPursuitDoorMaxDistance"), arenaCombatPursuitDoorMaxDistanceSpinBox->value());
    replaceReal(QStringLiteral("arenaCombatPursuitMinimumChance"), arenaCombatPursuitMinimumChanceDoubleSpinBox->value(), 2);
    replaceNumber(QStringLiteral("arenaCombatPursuitMaxActors"), arenaCombatPursuitMaxActorsSpinBox->value());
    replaceNumber(QStringLiteral("arenaCombatPursuitMaxDistance"), arenaCombatPursuitMaxDistanceSpinBox->value());
    replaceBool(QStringLiteral("arenaFollowersAttackOnSight"), arenaFollowersAttackOnSightCheckBox->isChecked());
    replaceBool(QStringLiteral("arenaNpcAvoidCollisions"), arenaNpcAvoidCollisionsCheckBox->isChecked());
    replaceBool(QStringLiteral("arenaNpcGiveWay"), arenaNpcGiveWayCheckBox->isChecked());
    replaceBool(QStringLiteral("arenaAllowActorsFollowOverWater"), arenaAllowActorsFollowOverWaterCheckBox->isChecked());
    replaceNumber(QStringLiteral("arenaActorsProcessingRange"), arenaActorsProcessingRangeSpinBox->value());
    replaceBool(QStringLiteral("arenaCanLootDuringDeathAnimation"), arenaCanLootDuringDeathAnimationCheckBox->isChecked());
    replaceBool(QStringLiteral("arenaWeaponSheathing"), arenaWeaponSheathingCheckBox->isChecked());
    replaceBool(QStringLiteral("arenaShieldSheathing"), arenaShieldSheathingCheckBox->isChecked());
    replaceBool(QStringLiteral("arenaGraphicHerbalism"), arenaGraphicHerbalismCheckBox->isChecked());
    replaceBool(QStringLiteral("arenaLongBladesUseAgility"), arenaLongBladesUseAgilityCheckBox->isChecked());
    replaceBool(QStringLiteral("arenaTwoHandedAccuracyPenalty"), arenaTwoHandedAccuracyPenaltyCheckBox->isChecked());
    replaceBool(QStringLiteral("arenaStavesAccuracyBonus"), arenaStavesAccuracyBonusCheckBox->isChecked());
    replaceBool(QStringLiteral("arenaSkillBooksLevelLimit"), arenaSkillBooksLevelLimitCheckBox->isChecked());
    replaceBool(QStringLiteral("arenaNewConstantEffectDifficulty"), arenaNewConstantEffectDifficultyCheckBox->isChecked());
    replaceReal(QStringLiteral("arenaGlobalXpMultiplier"), arenaGlobalXpMultiplierDoubleSpinBox->value(), 2);

    replaceBool(QStringLiteral("passTimeWhenEmpty"), passTimeWhenEmptyCheckBox->isChecked());
    replaceBool(QStringLiteral("allowConsole"), allowConsoleCheckBox->isChecked());
    replaceBool(QStringLiteral("allowBedRest"), allowBedRestCheckBox->isChecked());
    replaceBool(QStringLiteral("allowWildernessRest"), allowWildernessRestCheckBox->isChecked());
    replaceBool(QStringLiteral("allowWait"), allowWaitCheckBox->isChecked());
    replaceBool(QStringLiteral("useInstancedSpawn"), useInstancedSpawnCheckBox->isChecked());
    replaceBool(QStringLiteral("respawnAtImperialShrine"), respawnAtImperialShrineCheckBox->isChecked());
    replaceBool(QStringLiteral("respawnAtTribunalTemple"), respawnAtTribunalTempleCheckBox->isChecked());
    replaceBool(QStringLiteral("playersRespawn"), playersRespawnCheckBox->isChecked());
    replaceBool(QStringLiteral("bountyResetOnDeath"), bountyResetOnDeathCheckBox->isChecked());
    replaceBool(QStringLiteral("bountyDeathPenalty"), bountyDeathPenaltyCheckBox->isChecked());
    replaceBool(QStringLiteral("allowSuicideCommand"), allowSuicideCommandCheckBox->isChecked());
    replaceBool(QStringLiteral("allowFixmeCommand"), allowFixmeCommandCheckBox->isChecked());
    replaceBool(QStringLiteral("allowOnContainerForUnloadedCells"), allowOnContainerForUnloadedCellsCheckBox->isChecked());
    replaceBool(QStringLiteral("enablePlayerCollision"), enablePlayerCollisionCheckBox->isChecked());
    replaceBool(QStringLiteral("enableActorCollision"), enableActorCollisionCheckBox->isChecked());
    replaceBool(QStringLiteral("enablePlacedObjectCollision"), enablePlacedObjectCollisionCheckBox->isChecked());
    replaceBool(QStringLiteral("useActorCollisionForPlacedObjects"), useActorCollisionForPlacedObjectsCheckBox->isChecked());
    replaceBool(QStringLiteral("enforceDataFiles"), enforceDataFilesCheckBox->isChecked());
    replaceBool(QStringLiteral("ignoreScriptErrors"), ignoreScriptErrorsCheckBox->isChecked());

    replaceBool(QStringLiteral("shareJournal"), shareJournalCheckBox->isChecked());
    replaceBool(QStringLiteral("shareFactionRanks"), shareFactionRanksCheckBox->isChecked());
    replaceBool(QStringLiteral("shareFactionExpulsion"), shareFactionExpulsionCheckBox->isChecked());
    replaceBool(QStringLiteral("shareFactionReputation"), shareFactionReputationCheckBox->isChecked());
    replaceBool(QStringLiteral("shareTopics"), shareTopicsCheckBox->isChecked());
    replaceBool(QStringLiteral("shareBounty"), shareBountyCheckBox->isChecked());
    replaceBool(QStringLiteral("shareReputation"), shareReputationCheckBox->isChecked());
    replaceBool(QStringLiteral("shareMapExploration"), shareMapExplorationCheckBox->isChecked());
    replaceBool(QStringLiteral("shareVideos"), shareVideosCheckBox->isChecked());
    replaceBool(QStringLiteral("shareKills"), shareKillsCheckBox->isChecked());

    const QString databaseTypeValue = databaseTypeComboBox->currentData().toString();
    if (!databaseTypeValue.isEmpty())
        replaceString(QStringLiteral("databaseType"), databaseTypeValue);

    return text;
}

void Launcher::PlayPage::loadServerSettings()
{
    const QString runtimePath = serverConfigPath();
    const QString persistentPath = persistentServerConfigPath();
    serverSettingsPathLabel->setText(tr("Persistent: %1  ->  Runtime: %2")
        .arg(QDir::toNativeSeparators(persistentPath), QDir::toNativeSeparators(runtimePath)));

    // The userdata copy is authoritative once it exists. The packaged
    // server/scripts/config.lua can be replaced by an update or deployment,
    // so using it as the only settings store caused user changes to appear to
    // reset to defaults on the next launch.
    QString sourcePath = QFileInfo::exists(persistentPath) ? persistentPath : runtimePath;
    QFile file(sourcePath);
    if (!file.exists())
    {
        serverSettingsEditor->setPlainText(QString());
        setServerSettingsStatus(tr("config.lua not found"), true);
        return;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        setServerSettingsStatus(tr("Could not open config.lua"), true);
        return;
    }

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    const QString text = stream.readAll();
    file.close();

    serverSettingsEditor->setPlainText(text);
    populateFormFromConfig(text);
    loadXpProgressionSettings();

    QString syncError;
    const bool persistentOk = sourcePath == persistentPath
        || writeServerConfigFile(persistentPath, text, &syncError);
    const bool runtimeOk = sourcePath == runtimePath
        || writeServerConfigFile(runtimePath, text, &syncError);

    if (!persistentOk || !runtimeOk)
        setServerSettingsStatus(tr("Loaded settings, but could not synchronize the persistent/runtime copy: %1").arg(syncError), true);
    else
        setServerSettingsStatus(tr("Loaded persistent server settings"));
}

bool Launcher::PlayPage::saveServerSettings()
{
    // When the form is visible it is the authoritative editor. If the user
    // switched to Raw, slotServerSettingsModeChanged() has already applied the
    // form values before the switch, so neither mode can silently restore the
    // old/default text on Play/Start Server.
    if (serverSettingsModeTabs->currentWidget() == formServerSettingsTab)
        serverSettingsEditor->setPlainText(updatedConfigFromForm(serverSettingsEditor->toPlainText()));

    const QString text = serverSettingsEditor->toPlainText();
    QString error;

    if (!writeServerConfigFile(persistentServerConfigPath(), text, &error))
    {
        QMessageBox::warning(this, tr("Save error"), error);
        setServerSettingsStatus(error, true);
        return false;
    }

    if (!writeServerConfigFile(serverConfigPath(), text, &error))
    {
        QMessageBox::warning(this, tr("Save error"), error);
        setServerSettingsStatus(error, true);
        return false;
    }

    if (!saveXpProgressionSettings())
        return false;

    setServerSettingsStatus(tr("Saved persistent server settings"));
    return true;
}

void Launcher::PlayPage::slotPlayClicked()
{
    emit playButtonClicked();
}

void Launcher::PlayPage::slotServerClicked()
{
    switchToServerConsoleTab();
    emit serverButtonClicked();
}

void Launcher::PlayPage::slotStopServerClicked()
{
    emit stopServerButtonClicked();
}

void Launcher::PlayPage::slotAutoStartServerToggled(bool enabled)
{
    updateHostModeUi(enabled);
    if (enabled)
        refreshHostInterfaces();
    updateStatusPanel();
    emit autoStartServerChanged(enabled);
}

void Launcher::PlayPage::slotRefreshHostInterfaces()
{
    refreshHostInterfaces();
}

void Launcher::PlayPage::slotUpdateHashesClicked()
{
    // A freshly generated manifest is only useful when enforcement is enabled.
    if (mEnforceRequiredCheckBox != nullptr && !mEnforceRequiredCheckBox->isChecked())
        mEnforceRequiredCheckBox->setChecked(true);
    else if (!enforceDataFilesCheckBox->isChecked())
        setEnforceDataFiles(true);

    serverSettingsEditor->setPlainText(updatedConfigFromForm(serverSettingsEditor->toPlainText()));
    saveServerSettings();
    emit updateHashesRequested();
}

void Launcher::PlayPage::slotEnforceRequiredToggled(bool enabled)
{
    if (enforceDataFilesCheckBox->isChecked() != enabled)
        enforceDataFilesCheckBox->setChecked(enabled);
    serverSettingsEditor->setPlainText(updatedConfigFromForm(serverSettingsEditor->toPlainText()));
    saveServerSettings();
}

void Launcher::PlayPage::applyServerModePreset(int index)
{
    if (mServerModeCombo == nullptr)
        return;

    const QString mode = mServerModeCombo->itemData(index).toString();
    if (mode == QLatin1String("CUSTOM"))
        return;

    const bool coop = mode == QLatin1String("CO-OP");
    gameModeEdit->setText(coop ? QStringLiteral("ArenaMP CO-OP") : QStringLiteral("ArenaMP MMO"));
    shareJournalCheckBox->setChecked(coop);
    shareFactionRanksCheckBox->setChecked(coop);
    shareFactionExpulsionCheckBox->setChecked(coop);
    shareFactionReputationCheckBox->setChecked(coop);
    shareTopicsCheckBox->setChecked(coop);
    // Crime bounty stays personal in both presets.
    shareBountyCheckBox->setChecked(false);
    shareReputationCheckBox->setChecked(coop);
    shareMapExplorationCheckBox->setChecked(coop);
    shareVideosCheckBox->setChecked(coop);
    shareKillsCheckBox->setChecked(coop);
}

void Launcher::PlayPage::slotServerModeChanged(int index)
{
    // X032: gameplay/server preset only. Do not call MainDialog graphics pages or
    // Settings::Manager here; graphics quality is an independent launcher state.
    applyServerModePreset(index);
    serverSettingsEditor->setPlainText(updatedConfigFromForm(serverSettingsEditor->toPlainText()));
    saveServerSettings();
}

void Launcher::PlayPage::slotReloadServerSettings()
{
    loadServerSettings();
}

void Launcher::PlayPage::slotSaveServerSettings()
{
    saveServerSettings();
}


void Launcher::PlayPage::slotServerSettingsModeChanged(int index)
{
    if (mSyncingServerSettingsTabs)
        return;

    mSyncingServerSettingsTabs = true;
    QWidget* target = serverSettingsModeTabs->widget(index);
    if (target == rawServerSettingsTab)
    {
        // Never discard unsaved form edits merely because the user opens Raw.
        serverSettingsEditor->setPlainText(updatedConfigFromForm(serverSettingsEditor->toPlainText()));
        setServerSettingsStatus(tr("Form synchronized to raw config"));
    }
    else if (target == formServerSettingsTab)
    {
        // Likewise, manual Raw edits become the values shown by the form.
        populateFormFromConfig(serverSettingsEditor->toPlainText());
        setServerSettingsStatus(tr("Raw config synchronized to form"));
    }
    mSyncingServerSettingsTabs = false;
}

void Launcher::PlayPage::slotApplyFormToRawConfig()
{
    serverSettingsEditor->setPlainText(updatedConfigFromForm(serverSettingsEditor->toPlainText()));
    setServerSettingsStatus(tr("Form applied to raw config"));
    serverSettingsModeTabs->setCurrentWidget(rawServerSettingsTab);
}

void Launcher::PlayPage::slotSyncFormFromRawConfig()
{
    populateFormFromConfig(serverSettingsEditor->toPlainText());
    setServerSettingsStatus(tr("Form updated from raw config"));
    serverSettingsModeTabs->setCurrentWidget(formServerSettingsTab);
}

void Launcher::PlayPage::setAlternativeServer(const QString& address, const QString& port, bool enabled)
{
    mAlternativeAddress->setText(address);
    mAlternativePort->setText(port);
    mAlternativeServer->setChecked(enabled);
    updateStatusPanel();
}
bool Launcher::PlayPage::alternativeServer() const { return mAlternativeServer->isChecked(); }
QString Launcher::PlayPage::alternativeAddress() const { return mAlternativeAddress->text().trimmed(); }
QString Launcher::PlayPage::alternativePort() const { return mAlternativePort->text().trimmed(); }
void Launcher::PlayPage::setProjectUrl(const QString& url)
{
    mProjectUrl = url.trimmed().isEmpty() ? QStringLiteral("https://t.me/arena_mp") : url.trimmed();
    mProjectLink->setToolTip(mProjectUrl);
    mProjectLink->setVisible(true);
}

void Launcher::PlayPage::rememberMasterServer(const QJsonObject& server, const QString& base)
{
    mMasterSelectionConnected=mMasterPreparing;
    mSelectedMasterServer=server;
    mSelectedMasterServer["master_url"]=base;
    QSettings("ArenaMP","Launcher").setValue("master/selected",QJsonDocument(mSelectedMasterServer).toJson(QJsonDocument::Compact));
    // The selected remote endpoint must take precedence over host mode.
    autoStartServerCheckBox->setChecked(false);
    if(auto* button=findChild<QPushButton*>("arenaSelectedWebsite")) {
        const QUrl url(server.value("website").toString());
        button->setVisible(url.isValid() && (url.scheme()=="https" || url.scheme()=="http") && !url.host().isEmpty() && url.userInfo().isEmpty());
        button->setToolTip(url.toString());
    }
    updateStatusPanel();
}

bool Launcher::PlayPage::validateMasterSelection()
{
    if(mSelectedMasterServer.isEmpty())return true;
    if(mMasterSelectionConnected){mMasterSelectionConnected=false;return true;}
    if(!mMasterPreparing && mPrepareMaster)mPrepareMaster();
    return false;
}
