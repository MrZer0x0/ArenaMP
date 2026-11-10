#include "buildsetupdialog.hpp"

#include <algorithm>

#include <components/config/buildmanifest.hpp>
#include <components/config/contentorder.hpp>
#include <components/misc/arenaglassicons.hpp>
#include <components/misc/arenaglasswindow.hpp>
#include <components/misc/arenaherobutton.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace
{
    const QStringList& gameFileFilters()
    {
        static const QStringList filters = {
            QStringLiteral("*.esm"), QStringLiteral("*.esp"),
            QStringLiteral("*.omwgame"), QStringLiteral("*.omwaddon")
        };
        return filters;
    }

    bool containsGameFiles(const QDir& dir)
    {
        return dir.exists() && !dir.entryList(gameFileFilters(), QDir::Files | QDir::Readable).isEmpty();
    }

    QLabel* makeMuted(QWidget* parent, const QString& text)
    {
        QLabel* label = new QLabel(text, parent);
        label->setProperty("arenaMuted", true);
        label->setWordWrap(true);
        return label;
    }

    QLabel* makeFieldLabel(QWidget* parent, const QString& text)
    {
        QLabel* label = new QLabel(text, parent);
        label->setProperty("arenaFieldLabel", true);
        return label;
    }

    QFrame* makeCard(QWidget* parent, QVBoxLayout** bodyOut)
    {
        QFrame* card = new QFrame(parent);
        card->setFrameShape(QFrame::NoFrame);
        card->setProperty("arenaCard", true);
        QVBoxLayout* body = new QVBoxLayout(card);
        body->setContentsMargins(12, 12, 12, 12);
        body->setSpacing(8);
        if (bodyOut != nullptr)
            *bodyOut = body;
        return card;
    }
}

QString Launcher::BuildSetupDialog::resolveDataFilesDirectory(const QString& selectedPath)
{
    if (selectedPath.trimmed().isEmpty())
        return QString();

    const QString cleanPath = QDir::cleanPath(selectedPath.trimmed());
    const QDir selectedDir(cleanPath);
    if (!selectedDir.exists())
        return QString();
    if (containsGameFiles(selectedDir))
        return cleanPath;

    // A build folder usually holds the launcher plus a "Data Files" child.
    const QStringList children = selectedDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QString& child : children)
    {
        if (child.compare(QLatin1String("Data Files"), Qt::CaseInsensitive) != 0)
            continue;
        const QString childPath = QDir::cleanPath(selectedDir.filePath(child));
        if (containsGameFiles(QDir(childPath)))
            return childPath;
    }
    return QString();
}

QString Launcher::BuildSetupDialog::encodingForLanguage(const QString& language)
{
    const QString canonical = Config::BuildManifest::canonicalLanguage(language);
    if (canonical.compare(QLatin1String("Russian"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("win1251");
    if (canonical.compare(QLatin1String("Polish"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("win1250");
    return QStringLiteral("win1252");
}

QString Launcher::BuildSetupDialog::defaultLanguage()
{
    // A Russian or Polish Morrowind installation is the common case for those
    // systems, so preselect the matching text encoding instead of English.
    switch (QLocale::system().language())
    {
        case QLocale::Russian: return QStringLiteral("Russian");
        case QLocale::Polish: return QStringLiteral("Polish");
        default: return QStringLiteral("English");
    }
}

bool Launcher::BuildSetupDialog::isGroundcoverCandidate(const QString& fileName)
{
    const QString lowered = fileName.toLower();
    return lowered.contains(QLatin1String("groundcover")) || lowered.contains(QLatin1String("grass"));
}

QStringList Launcher::BuildSetupDialog::archivesForDirectory(const QString& dataPath)
{
    // Base BSAs first, in game dependency order, then everything else in
    // case-insensitive alphabetical order. Same rule the Wizard used.
    const QDir dir(dataPath);
    QStringList all;
    const QStringList files = dir.entryList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    for (const QString& fileName : files)
    {
        if (fileName.endsWith(QLatin1String(".bsa"), Qt::CaseInsensitive))
            all.append(fileName);
    }

    const QStringList baseArchives = {
        QStringLiteral("Morrowind.bsa"), QStringLiteral("Tribunal.bsa"), QStringLiteral("Bloodmoon.bsa")
    };
    QStringList ordered;
    for (const QString& wanted : baseArchives)
    {
        for (const QString& actual : all)
        {
            if (actual.compare(wanted, Qt::CaseInsensitive) == 0)
            {
                ordered.append(actual);
                break;
            }
        }
    }

    QStringList additional;
    for (const QString& archive : all)
    {
        bool isBase = false;
        for (const QString& wanted : baseArchives)
            isBase = isBase || archive.compare(wanted, Qt::CaseInsensitive) == 0;
        if (!isBase)
            additional.append(archive);
    }
    std::sort(additional.begin(), additional.end(), [](const QString& left, const QString& right) {
        return QString::localeAwareCompare(left.toLower(), right.toLower()) < 0;
    });
    ordered.append(additional);
    return ordered;
}

Launcher::BuildSetupDialog::BuildSetupDialog(QWidget* parent)
    : QDialog(parent)
    , mPathEdit(nullptr)
    , mBrowseButton(nullptr)
    , mStatusIcon(nullptr)
    , mStatusLabel(nullptr)
    , mStatusDetailLabel(nullptr)
    , mNameEdit(nullptr)
    , mLanguageCombo(nullptr)
    , mLanguageHintLabel(nullptr)
    , mContentHintLabel(nullptr)
    , mContentList(nullptr)
    , mGroundcoverLabel(nullptr)
    , mUpButton(nullptr)
    , mDownButton(nullptr)
    , mAcceptButton(nullptr)
    , mValid(false)
{
    setObjectName(QStringLiteral("BuildSetupDialog"));
    setWindowTitle(tr("ArenaMP build setup"));
    setWindowFlag(Qt::WindowMinimizeButtonHint, false);
    setFixedSize(760, 600);

    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);

    // Header
    QHBoxLayout* header = new QHBoxLayout();
    header->setSpacing(10);
    QLabel* headerIcon = new QLabel(this);
    headerIcon->setFixedSize(34, 34);
    headerIcon->setAlignment(Qt::AlignCenter);
    headerIcon->setPixmap(ArenaUi::glassIcon(QStringLiteral("cube")).pixmap(30, 30));
    header->addWidget(headerIcon);
    QVBoxLayout* headerText = new QVBoxLayout();
    headerText->setSpacing(1);
    QLabel* title = new QLabel(tr("Choose the build"), this);
    title->setProperty("arenaSectionTitle", true);
    headerText->addWidget(title);
    headerText->addWidget(makeMuted(this,
        tr("Point the launcher at a build folder. An existing build.ini is loaded as is; a new build gets one generated from the order below.")));
    header->addLayout(headerText, 1);
    root->addLayout(header);

    // Folder card
    QVBoxLayout* folderBody = nullptr;
    QFrame* folderCard = makeCard(this, &folderBody);
    QHBoxLayout* pathRow = new QHBoxLayout();
    pathRow->setSpacing(8);
    pathRow->addWidget(makeFieldLabel(folderCard, tr("Build folder:")));
    mPathEdit = new QLineEdit(folderCard);
    mPathEdit->setMinimumHeight(30);
    mPathEdit->setPlaceholderText(tr("Folder with the game data files"));
    pathRow->addWidget(mPathEdit, 1);
    mBrowseButton = new QPushButton(tr("Browse..."), folderCard);
    mBrowseButton->setProperty("arenaQuiet", true);
    mBrowseButton->setMinimumHeight(30);
    mBrowseButton->setIcon(ArenaUi::glassIcon(QStringLiteral("browse")));
    pathRow->addWidget(mBrowseButton);
    folderBody->addLayout(pathRow);

    QHBoxLayout* statusRow = new QHBoxLayout();
    statusRow->setSpacing(10);
    mStatusIcon = new QLabel(folderCard);
    mStatusIcon->setFixedSize(14, 14);
    mStatusIcon->setProperty("arenaStatusDot", true);
    mStatusIcon->setProperty("arenaStatus", QStringLiteral("warn"));
    statusRow->addWidget(mStatusIcon, 0, Qt::AlignTop);
    QVBoxLayout* statusText = new QVBoxLayout();
    statusText->setSpacing(1);
    mStatusLabel = new QLabel(tr("No folder selected"), folderCard);
    mStatusLabel->setProperty("arenaStatusHeadline", true);
    mStatusLabel->setProperty("arenaStatus", QStringLiteral("warn"));
    mStatusDetailLabel = makeMuted(folderCard, tr("Select the folder that contains Morrowind.esm and the build plug-ins."));
    statusText->addWidget(mStatusLabel);
    statusText->addWidget(mStatusDetailLabel);
    statusRow->addLayout(statusText, 1);
    folderBody->addLayout(statusRow);
    root->addWidget(folderCard);

    // Build card
    QVBoxLayout* buildBody = nullptr;
    QFrame* buildCard = makeCard(this, &buildBody);
    QHBoxLayout* nameRow = new QHBoxLayout();
    nameRow->setSpacing(8);
    nameRow->addWidget(makeFieldLabel(buildCard, tr("Build name:")));
    mNameEdit = new QLineEdit(buildCard);
    mNameEdit->setMinimumHeight(30);
    mNameEdit->setPlaceholderText(tr("For example, project NIRN 2.0"));
    nameRow->addWidget(mNameEdit, 1);
    nameRow->addWidget(makeFieldLabel(buildCard, tr("Language:")));
    mLanguageCombo = new QComboBox(buildCard);
    mLanguageCombo->setMinimumHeight(30);
    mLanguageCombo->setMinimumWidth(150);
    mLanguageCombo->addItem(tr("English"), QStringLiteral("English"));
    mLanguageCombo->addItem(tr("Russian"), QStringLiteral("Russian"));
    mLanguageCombo->addItem(tr("Polish"), QStringLiteral("Polish"));
    const int defaultLanguageIndex = mLanguageCombo->findData(defaultLanguage());
    if (defaultLanguageIndex >= 0)
        mLanguageCombo->setCurrentIndex(defaultLanguageIndex);
    nameRow->addWidget(mLanguageCombo);
    buildBody->addLayout(nameRow);

    mLanguageHintLabel = makeMuted(buildCard,
        tr("The language sets the text encoding of the build: win1251 for Russian, win1250 for Polish, win1252 otherwise."));
    buildBody->addWidget(mLanguageHintLabel);

    mContentHintLabel = makeMuted(buildCard,
        tr("Tick the plug-ins the build uses and put them in load order. Grass and groundcover plug-ins are recognized by name and connected automatically."));
    buildBody->addWidget(mContentHintLabel);

    QHBoxLayout* listRow = new QHBoxLayout();
    listRow->setSpacing(8);
    mContentList = new QListWidget(buildCard);
    mContentList->setObjectName(QStringLiteral("buildContentList"));
    mContentList->setSelectionMode(QAbstractItemView::SingleSelection);
    mContentList->setDragDropMode(QAbstractItemView::InternalMove);
    mContentList->setDefaultDropAction(Qt::MoveAction);
    mContentList->setAlternatingRowColors(false);
    listRow->addWidget(mContentList, 1);

    QVBoxLayout* orderButtons = new QVBoxLayout();
    orderButtons->setSpacing(6);
    mUpButton = new QPushButton(QStringLiteral("▲"), buildCard);
    mDownButton = new QPushButton(QStringLiteral("▼"), buildCard);
    for (QPushButton* button : { mUpButton, mDownButton })
    {
        button->setProperty("arenaQuiet", true);
        button->setFixedWidth(38);
        button->setMinimumHeight(30);
    }
    mUpButton->setToolTip(tr("Move up"));
    mDownButton->setToolTip(tr("Move down"));
    orderButtons->addWidget(mUpButton);
    orderButtons->addWidget(mDownButton);
    orderButtons->addStretch(1);
    listRow->addLayout(orderButtons);
    buildBody->addLayout(listRow, 1);

    mGroundcoverLabel = makeMuted(buildCard, QString());
    buildBody->addWidget(mGroundcoverLabel);
    root->addWidget(buildCard, 1);

    // Footer
    QHBoxLayout* footer = new QHBoxLayout();
    footer->setSpacing(8);
    QPushButton* cancelButton = new QPushButton(tr("Cancel"), this);
    cancelButton->setProperty("arenaQuiet", true);
    cancelButton->setMinimumHeight(34);
    footer->addWidget(cancelButton);
    footer->addStretch(1);
    ArenaUi::HeroButton* acceptButton = new ArenaUi::HeroButton(this);
    acceptButton->setText(tr("Use this build"));
    acceptButton->setCompact(true);
    acceptButton->setIcon(ArenaUi::glassIcon(QStringLiteral("play-dark")));
    acceptButton->setMinimumWidth(200);
    acceptButton->setEnabled(false);
    mAcceptButton = acceptButton;
    footer->addWidget(acceptButton);
    root->addLayout(footer);

    connect(mBrowseButton, &QPushButton::clicked, this, &BuildSetupDialog::browseForFolder);
    connect(mPathEdit, &QLineEdit::editingFinished, this, &BuildSetupDialog::pathEdited);
    connect(mUpButton, &QPushButton::clicked, this, &BuildSetupDialog::moveSelectionUp);
    connect(mDownButton, &QPushButton::clicked, this, &BuildSetupDialog::moveSelectionDown);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(acceptButton, &QPushButton::clicked, this, &QDialog::accept);

    ArenaUi::installGlassWindow(*this);
}

void Launcher::BuildSetupDialog::setInitialPath(const QString& dataPath)
{
    if (dataPath.trimmed().isEmpty())
        return;
    mPathEdit->setText(QDir::toNativeSeparators(dataPath));
    scanFolder(dataPath);
}

void Launcher::BuildSetupDialog::browseForFolder()
{
    const QString start = mPathEdit->text().trimmed().isEmpty()
        ? QDir::homePath() : mPathEdit->text().trimmed();
    const QString selected = QFileDialog::getExistingDirectory(this,
        tr("Select the build folder"), start, QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (selected.isEmpty())
        return;
    mPathEdit->setText(QDir::toNativeSeparators(selected));
    scanFolder(selected);
}

void Launcher::BuildSetupDialog::pathEdited()
{
    scanFolder(mPathEdit->text());
}

void Launcher::BuildSetupDialog::setStatus(const QString& text, const QString& state)
{
    mStatusLabel->setText(text);
    for (QWidget* widget : { static_cast<QWidget*>(mStatusLabel), static_cast<QWidget*>(mStatusIcon) })
    {
        widget->setProperty("arenaStatus", state);
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }
}

void Launcher::BuildSetupDialog::scanFolder(const QString& selectedPath)
{
    mValid = false;
    mResult = Result();
    mContentList->clear();
    mGroundcoverLabel->clear();
    mAcceptButton->setEnabled(false);

    const QString dataPath = resolveDataFilesDirectory(selectedPath);
    if (dataPath.isEmpty())
    {
        setStatus(tr("No game files found"), QStringLiteral("warn"));
        mStatusDetailLabel->setText(selectedPath.trimmed().isEmpty()
            ? tr("Select the folder that contains Morrowind.esm and the build plug-ins.")
            : tr("Neither this folder nor its Data Files subfolder contains .esm/.esp files."));
        return;
    }

    const QString manifestPath = Config::BuildManifest::findForDataDir(dataPath);
    if (!manifestPath.isEmpty())
        showManifestBuild(dataPath, manifestPath);
    else
        showNewBuild(dataPath);
}

void Launcher::BuildSetupDialog::showManifestBuild(const QString& dataPath, const QString& manifestPath)
{
    Config::BuildManifest manifest;
    QString error;
    if (!manifest.read(manifestPath, &error))
    {
        setStatus(tr("build.ini could not be read"), QStringLiteral("warn"));
        mStatusDetailLabel->setText(error.isEmpty()
            ? QDir::toNativeSeparators(manifestPath) : error);
        return;
    }

    mResult.dataPath = dataPath;
    mResult.manifestExists = true;
    mResult.manifestPath = manifestPath;
    mResult.buildName = manifest.buildName.trimmed().isEmpty()
        ? QStringLiteral("ArenaMP") : manifest.buildName.trimmed();
    mResult.language = Config::BuildManifest::canonicalLanguage(manifest.language);
    mResult.content = manifest.contentFiles;
    mResult.groundcover = manifest.groundcoverFiles;
    mResult.archives = manifest.archives;
    mValid = true;

    setStatus(tr("Build found"), QStringLiteral("ready"));
    mStatusDetailLabel->setText(tr("build.ini in %1").arg(QDir::toNativeSeparators(dataPath)));

    mNameEdit->setText(mResult.buildName);
    mNameEdit->setReadOnly(true);

    // build.ini owns the language when it declares one. Older manifests
    // without the field let the player choose it here instead.
    mResult.applyLanguage = !manifest.languageSpecified;
    mLanguageCombo->setEnabled(mResult.applyLanguage);
    const int languageIndex = mLanguageCombo->findData(
        mResult.applyLanguage ? defaultLanguage() : mResult.language);
    if (languageIndex >= 0)
        mLanguageCombo->setCurrentIndex(languageIndex);
    mLanguageHintLabel->setText(mResult.applyLanguage
        ? tr("build.ini has no language field: choose the encoding the build expects.")
        : tr("The language comes from build.ini: %1.").arg(mResult.language));

    mContentHintLabel->setText(tr("The plug-in list and its order come from build.ini and are used exactly as they are."));
    mContentList->setDragDropMode(QAbstractItemView::NoDragDrop);
    mUpButton->setEnabled(false);
    mDownButton->setEnabled(false);
    for (const QString& fileName : mResult.content)
    {
        QListWidgetItem* item = new QListWidgetItem(fileName, mContentList);
        item->setFlags(Qt::ItemIsEnabled);
    }
    if (!mResult.groundcover.isEmpty())
        mGroundcoverLabel->setText(tr("Groundcover: %1").arg(mResult.groundcover.join(QStringLiteral(", "))));
    mAcceptButton->setEnabled(true);
    mAcceptButton->setText(tr("Use this build"));
}

void Launcher::BuildSetupDialog::showNewBuild(const QString& dataPath)
{
    mResult.dataPath = dataPath;
    mResult.manifestExists = false;
    mResult.manifestPath = Config::BuildManifest::canonicalPathForDataDir(dataPath);
    mValid = true;

    setStatus(tr("New build"), QStringLiteral("busy"));
    mStatusDetailLabel->setText(tr("No build.ini in %1 yet. The launcher will create one from the settings below.")
        .arg(QDir::toNativeSeparators(dataPath)));

    mNameEdit->setReadOnly(false);
    mLanguageCombo->setEnabled(true);
    mResult.applyLanguage = true;
    mLanguageHintLabel->setText(
        tr("The language sets the text encoding of the build: win1251 for Russian, win1250 for Polish, win1252 otherwise."));
    if (mNameEdit->text().trimmed().isEmpty())
    {
        // Suggest the build folder name, not the literal "Data Files".
        QDir folder(dataPath);
        QString suggestion = folder.dirName();
        if (suggestion.compare(QLatin1String("Data Files"), Qt::CaseInsensitive) == 0)
        {
            QDir parent(dataPath);
            if (parent.cdUp())
                suggestion = parent.dirName();
        }
        mNameEdit->setText(suggestion.trimmed().isEmpty() ? QStringLiteral("ArenaMP") : suggestion);
    }

    mContentHintLabel->setText(tr("Tick the plug-ins the build uses and put them in load order. Grass and groundcover plug-ins are recognized by name and connected automatically."));
    mContentList->setDragDropMode(QAbstractItemView::InternalMove);
    mUpButton->setEnabled(true);
    mDownButton->setEnabled(true);

    const QDir dir(dataPath);
    const QStringList found = dir.entryList(gameFileFilters(), QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);

    QStringList groundcover;
    QStringList plugins;
    for (const QString& fileName : found)
    {
        if (isGroundcoverCandidate(fileName))
            groundcover.append(fileName);
        else
            plugins.append(fileName);
    }

    // Known base masters keep their canonical order and start enabled; every
    // other plug-in is listed afterwards, unticked, for the player to decide.
    QStringList recommended = Config::applyCanonicalContentOrder(plugins, dir);
    if (recommended.isEmpty())
        recommended = plugins;

    QStringList baseOrder;
    const QStringList& canonical = Config::canonicalContentOrder();
    for (const QString& wanted : canonical)
    {
        for (const QString& actual : recommended)
        {
            if (actual.compare(wanted, Qt::CaseInsensitive) == 0)
            {
                baseOrder.append(actual);
                break;
            }
        }
    }
    QStringList rest;
    for (const QString& fileName : recommended)
    {
        if (!baseOrder.contains(fileName, Qt::CaseInsensitive))
            rest.append(fileName);
    }

    auto addItem = [this](const QString& fileName, bool checked) {
        QListWidgetItem* item = new QListWidgetItem(fileName, mContentList);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
        item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    };
    for (const QString& fileName : baseOrder)
        addItem(fileName, true);
    for (const QString& fileName : rest)
        addItem(fileName, true);

    mResult.groundcover = groundcover;
    mResult.archives = archivesForDirectory(dataPath);
    mGroundcoverLabel->setText(groundcover.isEmpty()
        ? tr("No grass plug-ins found. BSA archives registered: %1").arg(mResult.archives.size())
        : tr("Grass connected automatically: %1").arg(groundcover.join(QStringLiteral(", "))));
    mAcceptButton->setEnabled(true);
    mAcceptButton->setText(tr("Create the build"));
}

void Launcher::BuildSetupDialog::moveSelection(int delta)
{
    const int row = mContentList->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= mContentList->count())
        return;
    QListWidgetItem* item = mContentList->takeItem(row);
    mContentList->insertItem(target, item);
    mContentList->setCurrentRow(target);
}

void Launcher::BuildSetupDialog::moveSelectionUp()
{
    moveSelection(-1);
}

void Launcher::BuildSetupDialog::moveSelectionDown()
{
    moveSelection(1);
}

QStringList Launcher::BuildSetupDialog::checkedContent() const
{
    QStringList content;
    for (int row = 0; row < mContentList->count(); ++row)
    {
        QListWidgetItem* item = mContentList->item(row);
        if (item == nullptr)
            continue;
        if ((item->flags() & Qt::ItemIsUserCheckable) == 0 || item->checkState() == Qt::Checked)
            content.append(item->text());
    }
    return content;
}

void Launcher::BuildSetupDialog::accept()
{
    if (!mValid)
        return;

    if (mResult.applyLanguage)
        mResult.language = Config::BuildManifest::canonicalLanguage(
            mLanguageCombo->currentData().toString());

    if (!mResult.manifestExists)
    {
        mResult.buildName = mNameEdit->text().trimmed();
        if (mResult.buildName.isEmpty())
        {
            QMessageBox::warning(this, tr("Build name"), tr("Enter a name for the new build."));
            mNameEdit->setFocus();
            return;
        }
        mResult.content = checkedContent();
        if (mResult.content.isEmpty())
        {
            QMessageBox::warning(this, tr("Content files"),
                tr("Tick at least one game file, normally Morrowind.esm."));
            return;
        }
    }

    QDialog::accept();
}
