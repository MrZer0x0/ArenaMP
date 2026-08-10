#include "playpage.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QIntValidator>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSpinBox>
#include <QTextStream>
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
}

Launcher::PlayPage::PlayPage(QWidget *parent)
    : QWidget(parent)
    , mEmbeddedServerConsole(nullptr)
    , mSyncingServerSettingsTabs(false)
{
    setObjectName("PlayPage");
    setupUi(this);
    serverPortEdit->setValidator(new QIntValidator(1, 65535, serverPortEdit));

    connect(playButton, SIGNAL(clicked()), this, SLOT(slotPlayClicked()));
    connect(serverButton, SIGNAL(clicked()), this, SLOT(slotServerClicked()));
    connect(stopServerButton, SIGNAL(clicked()), this, SLOT(slotStopServerClicked()));
    connect(autoStartServerCheckBox, SIGNAL(toggled(bool)), this, SLOT(slotAutoStartServerToggled(bool)));
    connect(autoRestartServerCheckBox, SIGNAL(toggled(bool)), this, SIGNAL(autoRestartServerChanged(bool)));
    connect(vanillaServerCheckBox, SIGNAL(toggled(bool)), this, SIGNAL(vanillaServerCompatibilityChanged(bool)));
    connect(hideChatHistoryCheckBox, SIGNAL(toggled(bool)), this, SIGNAL(hideChatHistoryChanged(bool)));
    connect(reloadServerSettingsButton, SIGNAL(clicked()), this, SLOT(slotReloadServerSettings()));
    connect(saveServerSettingsButton, SIGNAL(clicked()), this, SLOT(slotSaveServerSettings()));
    connect(applyServerSettingsFormButton, SIGNAL(clicked()), this, SLOT(slotApplyFormToRawConfig()));
    connect(syncServerSettingsFormButton, SIGNAL(clicked()), this, SLOT(slotSyncFormFromRawConfig()));
    connect(serverSettingsModeTabs, SIGNAL(currentChanged(int)), this, SLOT(slotServerSettingsModeChanged(int)));

    pageTabs->setCurrentIndex(0);
    serverSettingsModeTabs->setCurrentIndex(0);
    loadServerSettings();
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
    portLabel->setVisible(!complete);
    serverAddressEdit->setVisible(!complete);
    serverPortEdit->setVisible(!complete);

    // A complete manifest fixes the network identity together with its endpoint.
    vanillaServerCheckBox->setEnabled(!complete);
    if (complete)
        vanillaServerCheckBox->setToolTip(tr("This value is locked by build.ini (complete=true)."));
}

void Launcher::PlayPage::setAutoStartServer(bool enabled)
{
    autoStartServerCheckBox->setChecked(enabled);
}

void Launcher::PlayPage::setAutoRestartServer(bool enabled)
{
    autoRestartServerCheckBox->setChecked(enabled);
}

void Launcher::PlayPage::setVanillaServerCompatibility(bool enabled)
{
    vanillaServerCheckBox->setChecked(enabled);
}

void Launcher::PlayPage::setHideChatHistory(bool enabled)
{
    hideChatHistoryCheckBox->setChecked(enabled);
}

void Launcher::PlayPage::setServerRunning(bool running, const QString&, const QString&, bool managed)
{
    stopServerButton->setEnabled(running && managed);
    serverButton->setEnabled(!running);

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

bool Launcher::PlayPage::vanillaServerCompatibility() const
{
    return vanillaServerCheckBox->isChecked();
}

bool Launcher::PlayPage::hideChatHistory() const
{
    return hideChatHistoryCheckBox->isChecked();
}

QString Launcher::PlayPage::serverPort() const
{
    QString p = serverPortEdit->text().trimmed();
    return p.isEmpty() ? QString("25565") : p;
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
    if (QComboBox* serverLanguageComboBox = findChild<QComboBox*>(QStringLiteral("serverLanguageComboBox")))
        loadComboBox(serverLanguageComboBox, text, QStringLiteral("serverLanguage"));
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

    const QComboBox* serverLanguageComboBox = findChild<QComboBox*>(QStringLiteral("serverLanguageComboBox"));
    const QString serverLanguage = serverLanguageComboBox != nullptr
        ? serverLanguageComboBox->currentData().toString()
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
    emit autoStartServerChanged(enabled);
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
