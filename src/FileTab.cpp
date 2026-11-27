#include "FileTab.h"
#include "ThumbnailProxy.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMimeDatabase>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressDialog>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

#include <memory>

QString FileTab::menuLabel(const QString &label, const QString &shortcut) const
{
    if (m_ui.preset == UiPreset::Compact || shortcut.isEmpty())
        return label;
    return label + QLatin1Char('\t') + shortcut;
}

void FileTab::compactMenuSeparators(QMenu *menu) const
{
    if (!menu)
        return;
    const QList<QAction *> actions = menu->actions();
    bool previousSeparator = true;
    for (QAction *a : actions) {
        if (!a->isSeparator()) {
            previousSeparator = false;
            continue;
        }
        if (previousSeparator) {
            menu->removeAction(a);
            delete a;
        }
        previousSeparator = true;
    }
    const QList<QAction *> tail = menu->actions();
    for (auto it = tail.crbegin(); it != tail.crend() && (*it)->isSeparator(); ++it) {
        menu->removeAction(*it);
        delete *it;
    }
}

class DensityDelegate : public QStyledItemDelegate
{
public:
    explicit DensityDelegate(int height, QObject *parent = nullptr)
        : QStyledItemDelegate(parent), m_height(height) {}

    void setHeight(int height) { m_height = height; }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize hint = QStyledItemDelegate::sizeHint(option, index);
        hint.setHeight(m_height);
        return hint;
    }

private:
    int m_height;
};

namespace {

// ---------- utilidades de arquivo ----------

QString uniquePath(const QString &dir, const QString &name)
{
    const QDir d(dir);
    const QString first = d.filePath(name);
    if (!QFileInfo::exists(first) && !QFileInfo(first).isSymLink())
        return first;

    int dot = name.lastIndexOf(QLatin1Char('.'));
    const int tar = name.lastIndexOf(QStringLiteral(".tar."));
    if (tar > 0)
        dot = tar;
    if (dot <= 0)
        dot = name.size();
    const QString base = name.left(dot);
    const QString suf = name.mid(dot);

    for (int n = 1;; ++n) {
        const QString mid = n == 1 ? QStringLiteral(" (cópia)") : QStringLiteral(" (cópia %1)").arg(n);
        const QString cand = d.filePath(base + mid + suf);
        if (!QFileInfo::exists(cand) && !QFileInfo(cand).isSymLink())
            return cand;
    }
}

bool copyRecursive(const QString &src, const QString &dst)
{
    const QFileInfo fi(src);
    if (fi.isSymLink())
        return QFile::link(fi.symLinkTarget(), dst);
    if (fi.isDir()) {
        if (!QDir().mkpath(dst))
            return false;
        bool ok = true;
        const auto entries = QDir(src).entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        for (const QFileInfo &e : entries)
            ok = copyRecursive(e.absoluteFilePath(), QDir(dst).filePath(e.fileName())) && ok;
        return ok;
    }
    return QFile::copy(src, dst);
}

bool removePath(const QString &p)
{
    const QFileInfo fi(p);
    if (fi.isDir() && !fi.isSymLink())
        return QDir(p).removeRecursively();
    return QFile::remove(p);
}

const QStringList &archiveSuffixes()
{
    // ordem importa: sufixos compostos primeiro
    static const QStringList s{".tar.gz", ".tar.bz2", ".tar.xz", ".tar.zst", ".tgz", ".tbz2",
                               ".txz", ".tar", ".zip", ".7z", ".rar", ".iso"};
    return s;
}

QString archiveSuffix(const QString &name)
{
    const QString low = name.toLower();
    for (const QString &s : archiveSuffixes())
        if (low.endsWith(s))
            return s;
    return QString();
}

QString desktopAppName(const QString &desktop, const QString &id)
{
    QFile f(desktop);
    if (f.open(QIODevice::ReadOnly)) {
        bool inEntry = false;
        while (!f.atEnd()) {
            const QString line = QString::fromUtf8(f.readLine()).trimmed();
            if (line.startsWith(QLatin1Char('['))) {
                inEntry = line.compare(QStringLiteral("[Desktop Entry]"), Qt::CaseInsensitive) == 0;
                continue;
            }
            if (inEntry && line.startsWith(QStringLiteral("Name="), Qt::CaseInsensitive)) {
                const QString name = line.mid(5).trimmed();
                if (!name.isEmpty())
                    return name;
            }
        }
    }
    return id;
}

QStringList gioRegisteredApps(const QByteArray &output)
{
    QStringList ids;
    bool registered = false;
    const QStringList lines = QString::fromUtf8(output).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.startsWith(QStringLiteral("Registered applications:"), Qt::CaseInsensitive)) {
            registered = true;
            continue;
        }
        if (!registered)
            continue;
        if (line.startsWith(QStringLiteral("Recommended applications:"), Qt::CaseInsensitive))
            break;
        const QString id = line.section(QLatin1Char('='), 0, 0).trimmed();
        if (id.endsWith(QStringLiteral(".desktop"), Qt::CaseInsensitive))
            ids << id;
    }
    return ids;
}

RestoreBackupHook restoreBackupHook;
WaitForProcessHook waitForProcessHook;
QStringList placesForTests;
QString templatesDirForTests;
ContextMenuHook contextMenuHook;
QStringList mimeIdsForTests;
bool mimeQueryForTestsSet = false;
ExecutableChooserHook executableChooserHook;
LaunchHook launchHook;

QString terminalProgram()
{
    for (const QString &name : { QStringLiteral("x-terminal-emulator"), QStringLiteral("foot"),
                                 QStringLiteral("kitty"), QStringLiteral("alacritty"),
                                 QStringLiteral("konsole"), QStringLiteral("gnome-terminal"),
                                 QStringLiteral("xfce4-terminal") }) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty())
            return found;
    }
    return QString();
}

bool waitForProcess(QProcess *proc, int msecs)
{
    return waitForProcessHook ? waitForProcessHook(proc, msecs) : proc->waitForFinished(msecs);
}

bool restoreBackup(const QString &backup, const QString &dst)
{
    return restoreBackupHook ? restoreBackupHook(backup, dst) : QFile::rename(backup, dst);
}

} // namespace

void setRestoreBackupHookForTests(RestoreBackupHook hook)
{
    restoreBackupHook = std::move(hook);
}

void setWaitForProcessHookForTests(WaitForProcessHook hook)
{
    waitForProcessHook = std::move(hook);
}

void setPlacesForTests(const QStringList &paths)
{
    placesForTests = paths;
}

void setTemplatesDirForTests(const QString &dir)
{
    templatesDirForTests = dir;
}

void setContextMenuHookForTests(ContextMenuHook hook)
{
    contextMenuHook = std::move(hook);
}

void setMimeQueryForTests(const QStringList &ids)
{
    mimeIdsForTests = ids;
    mimeQueryForTestsSet = true;
}

void clearMimeQueryForTestsForTests()
{
    mimeIdsForTests.clear();
    mimeQueryForTestsSet = false;
}

void setExecutableChooserForTests(ExecutableChooserHook hook)
{
    executableChooserHook = std::move(hook);
}

void setLaunchHookForTests(LaunchHook hook)
{
    launchHook = std::move(hook);
}

QString FileTab::desktopFileForId(const QString &id) const
{
    if (id.isEmpty())
        return QString();

    const QFileInfo direct(id);
    if (direct.isAbsolute() && direct.exists())
        return direct.absoluteFilePath();

    QStringList roots;
    const QString home = QString::fromLocal8Bit(qgetenv("XDG_DATA_HOME"));
    if (!home.isEmpty() && QFileInfo(home).isAbsolute())
        roots << QFileInfo(home).absoluteFilePath();
    else
        roots << QDir::homePath() + QStringLiteral("/.local/share");

    const QByteArray dirs = qgetenv("XDG_DATA_DIRS");
    int absoluteDirs = 0;
    if (!dirs.isEmpty()) {
        for (const QByteArray &part : dirs.split(':')) {
            const QString dir = QString::fromLocal8Bit(part);
            if (dir.isEmpty() || !QFileInfo(dir).isAbsolute())
                continue;
            roots << QFileInfo(dir).absoluteFilePath();
            ++absoluteDirs;
        }
    }
    if (absoluteDirs == 0)
        roots << QStringLiteral("/usr/local/share") << QStringLiteral("/usr/share");

    QStringList ordered;
    for (const QString &root : roots)
        if (!ordered.contains(root))
            ordered << root;

    for (const QString &root : ordered) {
        const QString apps = QDir(root).absoluteFilePath(QStringLiteral("applications"));
        const QString candidate = QDir(apps).absoluteFilePath(id);
        if (QFileInfo::exists(candidate))
            return QFileInfo(candidate).absoluteFilePath();

        // O GIO codifica o "/" dos ids em subdiretórios como "-".
        if (id.contains(QLatin1Char('-'))) {
            QDirIterator it(apps, QStringList{QStringLiteral("*.desktop")},
                            QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString path = it.next();
                const QString encoded = QDir(apps).relativeFilePath(path)
                                            .replace(QLatin1Char('/'), QLatin1Char('-'));
                if (encoded == id)
                    return QFileInfo(path).absoluteFilePath();
            }
        }
    }
    return QString();
}

FileTab::FileTab(const QString &startPath, QWidget *parent) : QWidget(parent)
{
    // ----- modelo -----
    m_model = new QFileSystemModel(this);
    m_model->setReadOnly(false);
    m_model->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::AllDirs);
    m_proxy = new ThumbnailProxy(m_model, this);
    connect(m_model, &QFileSystemModel::directoryLoaded, this, [this](const QString &dir) {
        if (dir == m_path && selectedPaths().isEmpty())
            onSelectionChanged();
    });

    // ----- visão em detalhes -----
    m_tree = new QTreeView;
    m_tree->setModel(m_proxy);
    m_tree->setRootIsDecorated(false);
    m_tree->setItemsExpandable(false);
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(0, Qt::AscendingOrder);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setEditTriggers(QAbstractItemView::EditKeyPressed);
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(true);
    m_densityDelegate = new DensityDelegate(28, m_tree);
    m_tree->setItemDelegate(m_densityDelegate);
    m_tree->setDragEnabled(true);
    m_tree->setAcceptDrops(true);
    m_tree->setDragDropMode(QAbstractItemView::DragDrop);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setStretchLastSection(false);
    m_tree->setColumnWidth(1, 90);
    m_tree->setColumnWidth(2, 130);
    m_tree->setColumnWidth(3, 150);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);

    // ----- visão em ícones (miniaturas) -----
    m_icons = new QListView;
    m_icons->setModel(m_proxy);
    m_icons->setSelectionModel(m_tree->selectionModel());
    m_icons->setViewMode(QListView::IconMode);
    m_icons->setResizeMode(QListView::Adjust);
    m_icons->setMovement(QListView::Static);
    m_icons->setIconSize(QSize(72, 72));
    m_icons->setGridSize(QSize(112, 112));
    m_gridIconBase = m_icons->iconSize();
    m_gridBase = m_icons->gridSize();
    m_treeIconBase = m_tree->iconSize();
    if (m_treeIconBase.width() <= 0) {
        const int pm = style()->pixelMetric(QStyle::PM_ListViewIconSize);
        m_treeIconBase = QSize(pm, pm);
    }
    m_tree->setIconSize(m_treeIconBase);
    m_icons->setSpacing(8);
    m_icons->setWordWrap(true);
    m_icons->setUniformItemSizes(false);
    m_icons->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_icons->setEditTriggers(QAbstractItemView::EditKeyPressed);
    m_icons->setDragEnabled(true);
    m_icons->setAcceptDrops(true);
    m_icons->setDragDropMode(QAbstractItemView::DragDrop);
    m_icons->setDefaultDropAction(Qt::MoveAction);
    m_icons->setContextMenuPolicy(Qt::CustomContextMenu);

    m_stack = new QStackedWidget;
    m_stack->addWidget(m_tree);
    m_stack->addWidget(m_icons);

    // ----- painel de lugares -----
    m_places = new QListWidget;
    m_places->setObjectName(QStringLiteral("places"));
    m_places->setMinimumWidth(190);
    m_places->setFrameShape(QFrame::NoFrame);
    m_places->setIconSize(QSize(20, 20));

    struct PlaceItem { QString label; QString path; QString icon; };
    QVector<PlaceItem> places;
    if (!placesForTests.isEmpty()) {
        for (const QString &p : placesForTests) {
            if (p.isEmpty() || p == QStringLiteral("/") || !QDir(p).exists())
                continue;
            QString label = QFileInfo(p).fileName();
            if (p == QDir::homePath())
                label = tr("Pasta pessoal");
            places.append({label, QDir::cleanPath(p), QStringLiteral("folder")});
        }
    } else {
        const struct { QStandardPaths::StandardLocation loc; const char *name; const char *icon; } defs[] = {
            {QStandardPaths::HomeLocation, "Pasta pessoal", "user-home"},
            {QStandardPaths::DesktopLocation, "Área de trabalho", "user-desktop"},
            {QStandardPaths::DocumentsLocation, "Documentos", "folder-documents"},
            {QStandardPaths::DownloadLocation, "Downloads", "folder-download"},
            {QStandardPaths::PicturesLocation, "Imagens", "folder-pictures"},
            {QStandardPaths::MusicLocation, "Músicas", "folder-music"},
            {QStandardPaths::MoviesLocation, "Vídeos", "folder-videos"},
        };
        for (const auto &d : defs) {
            const QString p = QStandardPaths::writableLocation(d.loc);
            if (p.isEmpty() || !QDir(p).exists())
                continue;
            places.append({tr(d.name), QDir::cleanPath(p), QLatin1String(d.icon)});
        }
    }
    for (const auto &entry : places) {
        auto *it = new QListWidgetItem(QIcon::fromTheme(entry.icon, QIcon::fromTheme("folder")),
                                       entry.label, m_places);
        it->setData(Qt::UserRole, entry.path);
    }
    auto *root = new QListWidgetItem(QIcon::fromTheme("drive-harddisk"), tr("Raiz (/)"), m_places);
    root->setData(Qt::UserRole, QStringLiteral("/"));
    connect(m_places, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        goTo(it->data(Qt::UserRole).toString());
    });

    // ----- painel de pré-visualização -----
    m_previewPanel = new QWidget;
    m_previewPanel->setObjectName(QStringLiteral("previewPanel"));
    auto *pl = new QVBoxLayout(m_previewPanel);
    pl->setContentsMargins(10, 10, 10, 10);
    m_previewImage = new QLabel;
    m_previewImage->setAlignment(Qt::AlignCenter);
    m_previewImage->setMinimumSize(200, 200);
    m_previewInfo = new QLabel;
    m_previewInfo->setWordWrap(true);
    m_previewInfo->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_previewInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pl->addWidget(m_previewImage);
    pl->addWidget(m_previewInfo, 1);
    m_previewPanel->setVisible(false);

    // ----- layout -----
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    split->addWidget(m_places);
    split->addWidget(m_stack);
    split->addWidget(m_previewPanel);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setStretchFactor(2, 0);
    split->setSizes({180, 700, 280});
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(split);

    // ----- sinais -----
    auto activate = [this](const QModelIndex &idx) {
        if (idx.isValid())
            openPath(pathOf(idx));
    };
    connect(m_tree, &QTreeView::activated, this, activate);
    connect(m_icons, &QListView::activated, this, activate);
    connect(m_tree, &QWidget::customContextMenuRequested, this, &FileTab::showContextMenu);
    connect(m_icons, &QWidget::customContextMenuRequested, this, &FileTab::showContextMenu);
    connect(m_tree->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this] { onSelectionChanged(); });

    // ----- atalhos (escopo: esta aba) -----
    auto sc = [this](const QKeySequence &ks, auto fn) {
        auto *s = new QShortcut(ks, this, fn);
        s->setContext(Qt::WidgetWithChildrenShortcut);
    };
    sc(QKeySequence::Copy, [this] { copyToClipboard(false, selectedPaths()); });
    sc(QKeySequence::Cut, [this] { copyToClipboard(true, selectedPaths()); });
    sc(QKeySequence::Paste, [this] { paste(); });
    sc(QKeySequence(Qt::Key_Delete), [this] { trashSelected(selectedPaths()); });
    sc(QKeySequence(Qt::SHIFT | Qt::Key_Delete), [this] { deleteSelected(selectedPaths()); });
    sc(QKeySequence(Qt::Key_F2), [this] { renameCurrent(pathOf(currentView()->currentIndex())); });

    goTo(startPath);
}

QAbstractItemView *FileTab::currentView() const
{
    return m_iconMode ? static_cast<QAbstractItemView *>(m_icons)
                      : static_cast<QAbstractItemView *>(m_tree);
}

QString FileTab::pathOf(const QModelIndex &proxyIndex) const
{
    return m_model->filePath(m_proxy->mapToSource(proxyIndex));
}

QStringList FileTab::selectedPaths() const
{
    QStringList out;
    const auto sel = m_tree->selectionModel()->selectedIndexes();
    for (const QModelIndex &i : sel)
        if (i.column() == 0)
            out << pathOf(i);
    return out;
}

// ---------- navegação ----------

void FileTab::updatePlacesSelection()
{
    const QString current = QDir::cleanPath(m_path);
    const QStringList currentParts = current.split(QLatin1Char('/'), Qt::SkipEmptyParts);

    QListWidgetItem *best = nullptr;
    int bestLen = -1;
    for (int i = 0; i < m_places->count(); ++i) {
        QListWidgetItem *it = m_places->item(i);
        if (it->data(PlaceSectionRole).isValid())
            continue;
        const QString place = QDir::cleanPath(it->data(Qt::UserRole).toString());
        if (place == current) {
            m_places->setCurrentItem(it);
            return;
        }
        if (place == QStringLiteral("/"))
            continue;
        const QStringList parts = place.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (parts.size() > currentParts.size() || parts.size() <= bestLen)
            continue;
        bool matches = true;
        for (int s = 0; s < parts.size(); ++s) {
            if (parts.at(s) != currentParts.at(s)) {
                matches = false;
                break;
            }
        }
        if (matches) {
            best = it;
            bestLen = parts.size();
        }
    }

    if (best) {
        m_places->setCurrentItem(best);
        return;
    }
    m_places->setCurrentItem(nullptr);
    m_places->clearSelection();
}

bool FileTab::goTo(const QString &path, bool addHistory)
{
    const QFileInfo fi(path);
    if (!fi.isDir()) {
        emit statusMessage(tr("Pasta inválida: %1").arg(path));
        return false;
    }
    if (!fi.isReadable()) {
        emit statusMessage(tr("Sem permissão para abrir: %1").arg(path));
        return false;
    }
    const QString abs = QDir::cleanPath(fi.absoluteFilePath());

    if (addHistory) {
        if (m_histIdx >= 0 && m_history.value(m_histIdx) == abs) {
            updatePlacesSelection();
            return true;
        }
        m_history = m_history.mid(0, m_histIdx + 1);
        m_history << abs;
        m_histIdx = m_history.size() - 1;
    }

    m_path = abs;
    m_model->setRootPath(abs);
    const QModelIndex idx = m_proxy->mapFromSource(m_model->index(abs));
    m_tree->setRootIndex(idx);
    m_icons->setRootIndex(idx);
    m_tree->clearSelection();
    updatePreview();
    updatePlacesSelection();

    emit pathChanged(abs);
    emit titleChanged(abs == QLatin1String("/") ? QStringLiteral("/") : fi.fileName());
    return true;
}

void FileTab::refreshModel()
{
    auto *oldModel = m_model;
    auto *oldProxy = m_proxy;
    auto *oldTreeSelection = m_tree->selectionModel();
    auto *oldIconsSelection = m_icons->selectionModel();

    auto *model = new QFileSystemModel(this);
    model->setReadOnly(false);
    model->setFilter(oldModel->filter());
    auto *proxy = new ThumbnailProxy(model, this);
    model->setRootPath(m_path);

    m_model = model;
    m_proxy = proxy;

    const QModelIndex rootIndex = m_proxy->mapFromSource(m_model->index(m_path));
    m_tree->setModel(m_proxy);
    m_icons->setModel(m_proxy);
    m_icons->setSelectionModel(m_tree->selectionModel());
    m_tree->setRootIndex(rootIndex);
    m_icons->setRootIndex(rootIndex);

    connect(m_tree->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this] { onSelectionChanged(); });
    connect(m_model, &QFileSystemModel::directoryLoaded, this, [this](const QString &dir) {
        if (dir == m_path && selectedPaths().isEmpty())
            onSelectionChanged();
    });

    oldTreeSelection->deleteLater();
    if (oldIconsSelection != oldTreeSelection)
        oldIconsSelection->deleteLater();
    oldProxy->deleteLater();
    oldModel->deleteLater();
}

void FileTab::back()
{
    if (canGoBack())
        goTo(m_history.at(--m_histIdx), false);
}

void FileTab::forward()
{
    if (canGoForward())
        goTo(m_history.at(++m_histIdx), false);
}

void FileTab::up()
{
    QDir d(m_path);
    if (d.cdUp())
        goTo(d.absolutePath());
}

void FileTab::openPath(const QString &path)
{
    const QFileInfo fi(path);
    if (fi.isDir())
        goTo(path);
    else
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void FileTab::applyUiPreferences(const UiPreferences &prefs)
{
    m_ui = prefs;

    struct Entry { QIcon icon; QString label; QString path; };
    QVector<Entry> entries;
    for (int i = 0; i < m_places->count(); ++i) {
        QListWidgetItem *it = m_places->item(i);
        if (it->data(PlaceSectionRole).isValid())
            continue;
        const QString path = it->data(Qt::UserRole).toString();
        if (!path.isEmpty())
            entries.append({it->icon(), it->text(), path});
    }

    m_places->clear();
    const auto addSection = [this](const QString &label, const QString &group) {
        auto *it = new QListWidgetItem(label, m_places);
        it->setData(PlaceSectionRole, QStringLiteral("section"));
        it->setData(PlaceGroupRole, group);
        it->setFlags(it->flags() & ~Qt::ItemIsSelectable);
        return it;
    };
    const auto addControl = [this](const QString &label, const QString &group) {
        auto *it = new QListWidgetItem(label, m_places);
        it->setData(PlaceSectionRole, QStringLiteral("control"));
        it->setData(PlaceGroupRole, group);
        it->setFlags(it->flags() & ~Qt::ItemIsSelectable);
        return it;
    };

    addSection(tr("Lugares"), QStringLiteral("places"));
    for (const Entry &entry : entries) {
        auto *it = new QListWidgetItem(entry.icon, entry.label, m_places);
        it->setData(Qt::UserRole, entry.path);
        it->setData(PlaceGroupRole, QStringLiteral("places"));
    }
    addSection(tr("Da aba"), QStringLiteral("tab"));
    addControl(tr("Mais…"), QStringLiteral("control-more"));
    addSection(tr("Dispositivos"), QStringLiteral("devices"));
    addControl(tr("Personalizar…"), QStringLiteral("control-customize"));

    for (int i = 0; i < m_places->count(); ++i) {
        const QString group = m_places->item(i)->data(PlaceGroupRole).toString();
        bool hidden = false;
        switch (prefs.preset) {
        case UiPreset::Compact:
            hidden = group == QStringLiteral("tab") || group == QStringLiteral("devices")
                  || group == QStringLiteral("control-customize");
            break;
        case UiPreset::Balanced:
            hidden = group == QStringLiteral("devices")
                  || group == QStringLiteral("control-more")
                  || group == QStringLiteral("control-customize");
            break;
        case UiPreset::Advanced:
            hidden = group == QStringLiteral("control-more");
            break;
        }
        m_places->item(i)->setHidden(hidden);
    }

    const int treeSide = qMax(8, prefs.iconSize / 2);
    const int gridCell = qMax(96, prefs.iconSize * 2 + 48);
    m_treeIconBase = QSize(treeSide, treeSide);
    m_gridIconBase = QSize(prefs.iconSize, prefs.iconSize);
    m_gridBase = QSize(gridCell, gridCell);
    setZoom(m_zoom);

    for (int col = 0; col <= 3; ++col)
        m_tree->header()->setSectionHidden(col,
            !prefs.columns.contains(QString::number(col)));

    const int rowHeight = prefs.density == 0 ? 22 : (prefs.density == 2 ? 34 : 28);
    m_densityDelegate->setHeight(rowHeight);
    m_places->setSpacing(prefs.density == 0 ? 0 : (prefs.density == 2 ? 4 : 2));
    setPreviewVisible(prefs.preview);
    updatePlacesSelection();
}

void FileTab::setIconMode(bool icons)
{
    m_iconMode = icons;
    m_stack->setCurrentWidget(icons ? static_cast<QWidget *>(m_icons) : static_cast<QWidget *>(m_tree));
}

void FileTab::setZoom(int percent)
{
    m_zoom = qBound(50, percent, 200);
    const int treeSide = qMax(8, m_treeIconBase.width() * m_zoom / 100);
    const int gridSide = qMax(16, m_gridIconBase.width() * m_zoom / 100);
    const int cell = qMax(96, m_gridBase.width() * m_zoom / 100);
    m_icons->setIconSize(QSize(gridSide, gridSide));
    m_icons->setGridSize(QSize(cell, cell));
    m_tree->setIconSize(QSize(treeSide, treeSide));
    emit zoomChanged(m_zoom);
}

void FileTab::setPreviewVisible(bool visible)
{
    m_previewPanel->setVisible(visible);
    updatePreview();
}

void FileTab::setShowHidden(bool show)
{
    QDir::Filters f = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::AllDirs;
    if (show)
        f |= QDir::Hidden;
    m_model->setFilter(f);
}

void FileTab::setViewActions(QAction *icons, QAction *preview, QAction *hidden,
                             QAction *zoomIn, QAction *zoomOut, QAction *zoomReset)
{
    m_iconsAct = icons;
    m_previewAct = preview;
    m_hiddenAct = hidden;
    m_zoomInAct = zoomIn;
    m_zoomOutAct = zoomOut;
    m_zoomResetAct = zoomReset;
}

// ---------- seleção / pré-visualização ----------

void FileTab::onSelectionChanged()
{
    updatePreview();
    const QStringList sel = selectedPaths();
    if (sel.isEmpty()) {
        emit statusMessage(tr("%1 itens").arg(m_proxy->rowCount(m_tree->rootIndex())));
        return;
    }
    qint64 total = 0;
    for (const QString &p : sel) {
        const QFileInfo fi(p);
        if (fi.isFile())
            total += fi.size();
    }
    emit statusMessage(tr("%1 selecionado(s) — %2").arg(sel.size()).arg(QLocale().formattedDataSize(total)));
}

void FileTab::updatePreview()
{
    if (!m_previewPanel->isVisible())
        return;

    const QStringList sel = selectedPaths();
    if (sel.size() != 1) {
        m_previewImage->clear();
        m_previewInfo->setText(sel.isEmpty() ? tr("Nenhum item selecionado")
                                             : tr("%1 itens selecionados").arg(sel.size()));
        return;
    }

    const QFileInfo fi(sel.first());
    const QMimeType mt = QMimeDatabase().mimeTypeForFile(fi);
    QString info = fi.fileName() + QLatin1Char('\n') + mt.comment();
    if (fi.isFile())
        info += QLatin1Char('\n') + QLocale().formattedDataSize(fi.size());
    info += QLatin1Char('\n') + QLocale().toString(fi.lastModified(), QLocale::ShortFormat);

    bool shown = false;
    if (fi.isFile() && mt.name().startsWith(QLatin1String("image/")) && fi.size() < 64LL * 1024 * 1024) {
        QImageReader r(fi.absoluteFilePath());
        r.setAutoTransform(true);
        QSize s = r.size();
        if (s.isValid()) {
            info += QStringLiteral("\n%1 × %2 px").arg(s.width()).arg(s.height());
            const int side = qMax(160, m_previewPanel->width() - 24);
            s.scale(side, side, Qt::KeepAspectRatio);
            r.setScaledSize(s);
        }
        const QImage img = r.read();
        if (!img.isNull()) {
            m_previewImage->setPixmap(QPixmap::fromImage(img));
            shown = true;
        }
    }
    if (!shown) {
        const QIcon ic = fi.isDir() ? QIcon::fromTheme("folder")
                                    : QIcon::fromTheme(mt.iconName(), QIcon::fromTheme(mt.genericIconName()));
        m_previewImage->setPixmap(ic.pixmap(96, 96));
    }
    m_previewInfo->setText(info);
}

// ---------- menu de contexto ----------

void FileTab::showContextMenu(const QPoint &pos)
{
    QAbstractItemView *v = currentView();
    const QModelIndex idx = v->indexAt(pos);
    if (idx.isValid()) {
        if (!v->selectionModel()->isSelected(idx)) {
            v->clearSelection();
            v->setCurrentIndex(idx.siblingAtColumn(0));
        }
    } else {
        v->clearSelection();
    }

    const QStringList sel = selectedPaths();
    const bool one = sel.size() == 1;
    const bool oneDir = one && QFileInfo(sel.first()).isDir();
    const QString first = sel.value(0);

    QMenu menu(this);
    const auto label = [this](const QString &text, const QString &shortcut = QString()) {
        return menuLabel(text, shortcut);
    };
    const auto addPaste = [this, &menu, &label] {
        QAction *act = menu.addAction(QIcon::fromTheme("edit-paste"), label(tr("Colar")), this,
                                      [this] { paste(); });
        act->setObjectName(QStringLiteral("pasteAction"));
        act->setData(m_path);
        const QMimeData *md = QGuiApplication::clipboard()->mimeData();
        act->setEnabled(md && md->hasUrls());
    };

    if (sel.isEmpty()) {
        menu.addAction(QIcon::fromTheme("folder-new"), label(tr("Nova pasta…")), this, [this] { newFolder(); });
        addNewMenu(&menu);
        menu.addSeparator();
        addPaste();
        menu.addSeparator();
        menu.addAction(QIcon::fromTheme("view-refresh"), label(tr("Atualizar")), this, [this] { refreshModel(); });
        QMenu *view = menu.addMenu(label(tr("Exibir")));
        addViewMenu(view);
        menu.addAction(label(tr("Abrir terminal aqui")), this, [this] { openTerminal(m_path); });
        menu.addAction(label(tr("Propriedades da pasta")), this, [this] { showProperties(m_path); });
    } else if (one && oneDir) {
        menu.addAction(label(tr("Abrir"), QStringLiteral("Enter")), this, [this, first] { openPath(first); });
        menu.addAction(label(tr("Abrir em nova aba")), this, [this, first] { emit openInNewTab(first); });
        menu.addAction(label(tr("Abrir terminal nesta pasta")), this, [this, first] { openTerminal(first); });
        menu.addAction(label(tr("Copiar caminho")), this, [this, first] { copyPathsToClipboard(QStringList{ first }); });
        menu.addSeparator();
        addNewMenu(&menu);
        menu.addSeparator();
        menu.addAction(QIcon::fromTheme("edit-copy"), label(tr("Copiar")), this, [this, sel] { copyToClipboard(false, sel); });
        menu.addAction(QIcon::fromTheme("edit-cut"), label(tr("Recortar")), this, [this, sel] { copyToClipboard(true, sel); });
        addPaste();
        menu.addSeparator();
        menu.addAction(label(tr("Renomear")), this, [this, first] { renameCurrent(first); });
        menu.addAction(QIcon::fromTheme("user-trash"), label(tr("Mover para a lixeira")), this, [this, sel] { trashSelected(sel); });
        menu.addAction(QIcon::fromTheme("edit-delete"), label(tr("Excluir permanentemente")), this, [this, sel] { deleteSelected(sel); });
        menu.addSeparator();
        menu.addAction(QIcon::fromTheme("archive-insert"), label(tr("Compactar…")), this, [this, sel] { compress(sel); });
        menu.addSeparator();
        menu.addAction(label(tr("Propriedades"), QStringLiteral("Alt+Enter")), this,
                      [this, first] { showProperties(first); });
    } else if (one) {
        menu.addAction(label(tr("Abrir"), QStringLiteral("Enter")), this, [this, first] { openPath(first); });
        QMenu *openWith = menu.addMenu(QIcon::fromTheme("applications-accessories"), label(tr("Abrir com…")));
        addOpenWithMenu(openWith, first);
        menu.addAction(label(tr("Copiar caminho")), this, [this, first] { copyPathsToClipboard(QStringList{ first }); });
        menu.addSeparator();
        addNewMenu(&menu);
        menu.addSeparator();
        menu.addAction(QIcon::fromTheme("edit-copy"), label(tr("Copiar")), this, [this, sel] { copyToClipboard(false, sel); });
        menu.addAction(QIcon::fromTheme("edit-cut"), label(tr("Recortar")), this, [this, sel] { copyToClipboard(true, sel); });
        addPaste();
        menu.addSeparator();
        menu.addAction(label(tr("Renomear")), this, [this, first] { renameCurrent(first); });
        menu.addAction(QIcon::fromTheme("user-trash"), label(tr("Mover para a lixeira")), this, [this, sel] { trashSelected(sel); });
        menu.addAction(QIcon::fromTheme("edit-delete"), label(tr("Excluir permanentemente")), this, [this, sel] { deleteSelected(sel); });
        menu.addSeparator();
        menu.addAction(QIcon::fromTheme("archive-insert"), label(tr("Compactar…")), this, [this, sel] { compress(sel); });
        if (!archiveSuffix(first).isEmpty()) {
            menu.addAction(QIcon::fromTheme("archive-extract"), label(tr("Extrair aqui")), this, [this, first] { extract(first, false); });
            menu.addAction(label(tr("Extrair em pasta própria")), this, [this, first] { extract(first, true); });
        }
        menu.addSeparator();
        menu.addAction(label(tr("Propriedades"), QStringLiteral("Alt+Enter")), this,
                      [this, first] { showProperties(first); });
    } else {
        menu.addAction(QIcon::fromTheme("edit-copy"), label(tr("Copiar")), this, [this, sel] { copyToClipboard(false, sel); });
        menu.addAction(QIcon::fromTheme("edit-cut"), label(tr("Recortar")), this, [this, sel] { copyToClipboard(true, sel); });
        menu.addAction(label(tr("Copiar caminhos")), this, [this, sel] { copyPathsToClipboard(sel); });
        addPaste();
        menu.addSeparator();
        menu.addAction(QIcon::fromTheme("archive-insert"), label(tr("Compactar…")), this, [this, sel] { compress(sel); });
        menu.addAction(QIcon::fromTheme("user-trash"), label(tr("Mover para a lixeira")), this, [this, sel] { trashSelected(sel); });
        menu.addAction(QIcon::fromTheme("edit-delete"), label(tr("Excluir permanentemente")), this, [this, sel] { deleteSelected(sel); });
        menu.addSeparator();
        addNewMenu(&menu);
    }

    if (m_ui.preset == UiPreset::Compact)
        compactMenuSeparators(&menu);
    if (contextMenuHook)
        contextMenuHook(&menu, sel);
    menu.exec(v->viewport()->mapToGlobal(pos));
}

void FileTab::addNewMenu(QMenu *menu)
{
    QMenu *nw = menu->addMenu(QIcon::fromTheme("document-new"), tr("Novo"));
    nw->addAction(QIcon::fromTheme("folder-new"), tr("Pasta…"), this, [this] { newFolder(); });
    nw->addAction(tr("Arquivo vazio…"), this, [this] { newFile("arquivo"); });
    nw->addAction(tr("Texto (.txt)"), this, [this] { newFile("novo.txt"); });
    nw->addAction(tr("Markdown (.md)"), this, [this] { newFile("novo.md", "# Título\n"); });
    nw->addSeparator();

    QMenu *code = nw->addMenu(tr("Código e dados"));
    code->addAction(tr("HTML (.html)"), this, [this] {
        newFile("index.html", "<!DOCTYPE html>\n<html lang=\"pt-BR\">\n<head>\n  <meta charset=\"utf-8\">\n"
                              "  <title></title>\n</head>\n<body>\n\n</body>\n</html>\n");
    });
    code->addAction(tr("JSON (.json)"), this, [this] { newFile("dados.json", "{}\n"); });
    code->addAction(tr("Script shell (.sh)"), this,
                    [this] { newFile("script.sh", "#!/usr/bin/env bash\nset -euo pipefail\n\n", true); });
    code->addAction(tr("Script Python (.py)"), this,
                    [this] { newFile("script.py", "#!/usr/bin/env python3\n\n"); });

    const QString tplDir = templatesDirForTests.isEmpty()
                               ? QStandardPaths::writableLocation(QStandardPaths::TemplatesLocation)
                               : templatesDirForTests;
    const auto tpls = QDir(tplDir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    if (!tpls.isEmpty()) {
        nw->addSeparator();
        QMenu *tm = nw->addMenu(tr("Modelos"));
        for (const QFileInfo &t : tpls) {
            const QString p = t.absoluteFilePath();
            tm->addAction(t.fileName(), this, [this, p] { newFromTemplate(p); });
        }
    }
}

void FileTab::addViewMenu(QMenu *menu)
{
    QAction *details = menu->addAction(tr("Lista detalhada"));
    QAction *grid = menu->addAction(tr("Grade de ícones"));
    details->setCheckable(true);
    grid->setCheckable(true);
    const bool icons = m_iconsAct && m_iconsAct->isChecked();
    details->setChecked(!icons);
    grid->setChecked(icons);
    connect(details, &QAction::triggered, this, [this] {
        if (m_iconsAct)
            m_iconsAct->setChecked(false);
    });
    connect(grid, &QAction::triggered, this, [this] {
        if (m_iconsAct)
            m_iconsAct->setChecked(true);
    });

    menu->addSeparator();
    if (m_previewAct)
        menu->addAction(m_previewAct);
    if (m_hiddenAct)
        menu->addAction(m_hiddenAct);

    menu->addSeparator();
    if (m_zoomInAct)
        menu->addAction(m_zoomInAct);
    if (m_zoomOutAct)
        menu->addAction(m_zoomOutAct);
    if (m_zoomResetAct)
        menu->addAction(m_zoomResetAct);
}

void FileTab::addOpenWithMenu(QMenu *menu, const QString &path)
{
    if (!menu)
        return;

    const QPointer<QMenu> popup = menu;
    QAction *chooser = menu->addAction(tr("Escolher executável…"), this, [this, path] {
        const QString program = executableChooserHook ? executableChooserHook()
                                                      : QFileDialog::getOpenFileName(
                                                            this, tr("Escolher executável…"), m_path);
        if (!program.isEmpty())
            launchProgram(program, QStringList{ path }, QString(), true);
    });

    const auto fill = [this, popup, chooser, path](const QStringList &ids) {
        if (!popup)
            return;
        QStringList seen;
        bool added = false;
        for (const QString &id : ids) {
            const QString clean = id.trimmed();
            if (clean.isEmpty() || seen.contains(clean))
                continue;
            seen << clean;
            const QString desktop = desktopFileForId(clean);
            auto *entry = new QAction(desktopAppName(desktop, clean), popup.data());
            popup->insertAction(chooser, entry);
            connect(entry, &QAction::triggered, popup.data(), [this, desktop, path] {
                openWithDesktop(desktop, path);
            });
            added = true;
        }
        if (added)
            popup->insertSeparator(chooser);
    };

    if (mimeQueryForTestsSet) {
        fill(mimeIdsForTests);
        return;
    }

    const QString mime = QMimeDatabase().mimeTypeForFile(QFileInfo(path)).name();
    if (mime.isEmpty())
        return;

    struct QueryState {
        int remaining = 2;
        bool done = false;
        QString defaultId;
        QStringList gioIds;
    };
    auto state = std::make_shared<QueryState>();
    auto defaultDone = std::make_shared<bool>(false);
    auto gioDone = std::make_shared<bool>(false);

    const auto oneDone = [state, fill] {
        if (state->done || --state->remaining > 0)
            return;
        state->done = true;
        QStringList ids;
        if (!state->defaultId.isEmpty())
            ids << state->defaultId;
        ids << state->gioIds;
        fill(ids);
    };

    auto *xdg = new QProcess(this);
    const QPointer<QProcess> xdgGuard = xdg;
    connect(xdg, &QProcess::finished, this,
            [xdgGuard, state, defaultDone, oneDone](int, QProcess::ExitStatus) {
                if (!*defaultDone) {
                    *defaultDone = true;
                    const QString out = QString::fromUtf8(xdgGuard ? xdgGuard->readAllStandardOutput()
                                                                   : QByteArray());
                    state->defaultId = out.trimmed().section(QLatin1Char('\n'), 0, 0).trimmed();
                    oneDone();
                }
                if (xdgGuard)
                    xdgGuard->deleteLater();
            });
    connect(xdg, &QProcess::errorOccurred, this,
            [xdgGuard, defaultDone, oneDone](QProcess::ProcessError) {
                if (!*defaultDone) {
                    *defaultDone = true;
                    oneDone();
                }
                if (xdgGuard)
                    xdgGuard->deleteLater();
            });
    xdg->start(QStringLiteral("xdg-mime"),
               {QStringLiteral("query"), QStringLiteral("default"), mime});

    auto *gio = new QProcess(this);
    const QPointer<QProcess> gioGuard = gio;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    gio->setProcessEnvironment(env);
    connect(gio, &QProcess::finished, this,
            [gioGuard, state, gioDone, oneDone](int, QProcess::ExitStatus) {
                if (!*gioDone) {
                    *gioDone = true;
                    const QByteArray out = gioGuard ? gioGuard->readAllStandardOutput() : QByteArray();
                    state->gioIds = gioRegisteredApps(out);
                    oneDone();
                }
                if (gioGuard)
                    gioGuard->deleteLater();
            });
    connect(gio, &QProcess::errorOccurred, this,
            [gioGuard, gioDone, oneDone](QProcess::ProcessError) {
                if (!*gioDone) {
                    *gioDone = true;
                    oneDone();
                }
                if (gioGuard)
                    gioGuard->deleteLater();
            });
    gio->start(QStringLiteral("gio"), {QStringLiteral("mime"), mime});
}

void FileTab::openWithDesktop(const QString &desktop, const QString &path)
{
    if (desktop.isEmpty() || path.isEmpty()) {
        emit statusMessage(tr("Aplicativo não encontrado"));
        return;
    }
    launchProgram(QStringLiteral("gio"), QStringList{QStringLiteral("launch"), desktop, path}, QString());
}

void FileTab::launchProgram(const QString &program, const QStringList &args, const QString &cwd,
                            bool detached)
{
    if (program.isEmpty())
        return;

    if (launchHook) {
        if (!launchHook(program, args, cwd))
            emit statusMessage(tr("Falha ao abrir o arquivo"));
        return;
    }

    if (detached) {
        if (!QProcess::startDetached(program, args, cwd))
            emit statusMessage(tr("Falha ao abrir o arquivo"));
        return;
    }

    auto *proc = new QProcess(this);
    const QPointer<QProcess> guard = proc;
    const auto reported = std::make_shared<bool>(false);
    const auto failure = [this, reported] {
        if (*reported)
            return;
        *reported = true;
        emit statusMessage(tr("Falha ao abrir o arquivo"));
    };
    connect(proc, &QProcess::errorOccurred, this, [guard, failure](QProcess::ProcessError) {
        failure();
        if (guard)
            guard->deleteLater();
    });
    connect(proc, &QProcess::finished, this,
            [guard, failure, reported](int code, QProcess::ExitStatus status) {
                if (status == QProcess::NormalExit && code == 0)
                    *reported = true;
                else
                    failure();
                if (guard)
                    guard->deleteLater();
            });
    if (!cwd.isEmpty())
        proc->setWorkingDirectory(cwd);
    proc->start(program, args);
}

// ---------- operações de arquivo ----------

void FileTab::copyToClipboard(bool cut, const QStringList &paths)
{
    if (paths.isEmpty())
        return;

    auto *md = new QMimeData;
    QList<QUrl> urls;
    QByteArray gnome = cut ? "cut" : "copy";
    for (const QString &p : paths) {
        const QUrl u = QUrl::fromLocalFile(p);
        urls << u;
        gnome += '\n' + u.toEncoded();
    }
    md->setUrls(urls);
    md->setData(QStringLiteral("application/x-kde-cutselection"), cut ? "1" : "0");
    md->setData(QStringLiteral("x-special/gnome-copied-files"), gnome);
    QGuiApplication::clipboard()->setMimeData(md);
    emit statusMessage(cut ? tr("%1 item(ns) recortado(s)").arg(paths.size())
                           : tr("%1 item(ns) copiado(s)").arg(paths.size()));
}

void FileTab::copyPathsToClipboard(const QStringList &paths)
{
    if (paths.isEmpty())
        return;

    auto *md = new QMimeData;
    md->setText(paths.join(QLatin1Char('\n')));
    QGuiApplication::clipboard()->setMimeData(md);
}

void FileTab::paste()
{
    const QMimeData *md = QGuiApplication::clipboard()->mimeData();
    if (!md || !md->hasUrls())
        return;

    const bool cut = md->data(QStringLiteral("application/x-kde-cutselection")).startsWith('1')
                     || md->data(QStringLiteral("x-special/gnome-copied-files")).startsWith("cut");
    int ok = 0, fail = 0;

    for (const QUrl &u : md->urls()) {
        if (!u.isLocalFile())
            continue;
        const QString src = u.toLocalFile();
        const QFileInfo sfi(src);
        if (!sfi.exists() && !sfi.isSymLink()) {
            ++fail;
            continue;
        }
        // não copiar uma pasta para dentro dela mesma
        if (sfi.isDir() && !sfi.isSymLink() && (m_path == src || m_path.startsWith(src + QLatin1Char('/')))) {
            ++fail;
            continue;
        }
        if (cut && sfi.absolutePath() == m_path)
            continue;

        const QString dst = uniquePath(m_path, sfi.fileName());
        bool good;
        if (cut) {
            good = QFile::rename(src, dst);
            if (!good)
                good = copyRecursive(src, dst) && removePath(src);
        } else {
            good = copyRecursive(src, dst);
        }
        good ? ++ok : ++fail;
    }
    if (cut)
        QGuiApplication::clipboard()->clear();

    emit statusMessage(fail ? tr("%1 colado(s), %2 falha(s)").arg(ok).arg(fail)
                            : tr("%1 item(ns) colado(s)").arg(ok));
}

void FileTab::trashSelected(const QStringList &paths)
{
    int ok = 0, fail = 0;
    for (const QString &p : paths)
        QFile::moveToTrash(p) ? ++ok : ++fail;
    if (!paths.isEmpty())
        emit statusMessage(fail ? tr("%1 na lixeira, %2 falha(s)").arg(ok).arg(fail)
                                : tr("%1 item(ns) movido(s) para a lixeira").arg(ok));
}

void FileTab::deleteSelected(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    const auto r = QMessageBox::warning(
        this, tr("Excluir permanentemente"),
        tr("Excluir %1 item(ns) permanentemente? Essa ação não pode ser desfeita.").arg(paths.size()),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (r != QMessageBox::Yes)
        return;
    int fail = 0;
    for (const QString &p : paths)
        if (!removePath(p))
            ++fail;
    emit statusMessage(fail ? tr("%1 item(ns) não puderam ser excluídos").arg(fail) : tr("Excluído"));
}

void FileTab::renameCurrent(const QString &path)
{
    if (path.isEmpty())
        return;
    const QModelIndex src = m_model->index(path);
    if (!src.isValid())
        return;
    const QModelIndex idx = m_proxy->mapFromSource(src);
    if (idx.isValid())
        currentView()->edit(idx.siblingAtColumn(0));
}

void FileTab::showProperties(const QString &path)
{
    const QFileInfo fi(path);
    const QMimeType mt = QMimeDatabase().mimeTypeForFile(fi);
    QString t;
    t += tr("Nome: %1\n").arg(fi.fileName());
    t += tr("Local: %1\n").arg(fi.absolutePath());
    t += tr("Tipo: %1 (%2)\n").arg(mt.comment(), mt.name());
    if (fi.isFile())
        t += tr("Tamanho: %1\n").arg(QLocale().formattedDataSize(fi.size()));
    if (fi.isSymLink())
        t += tr("Aponta para: %1\n").arg(fi.symLinkTarget());
    t += tr("Modificado: %1\n").arg(QLocale().toString(fi.lastModified(), QLocale::LongFormat));
    t += tr("Dono: %1  Grupo: %2\n").arg(fi.owner(), fi.group());
    t += tr("Permissões: %1").arg(QString::number(static_cast<uint>(fi.permissions()) & 0xFFF, 16));
    QMessageBox::information(this, tr("Propriedades"), t);
}

void FileTab::openTerminal(const QString &dir)
{
    const QString program = terminalProgram();
    if (program.isEmpty()) {
        emit statusMessage(tr("Nenhum terminal disponível"));
        return;
    }

    if (launchHook) {
        if (!launchHook(program, QStringList(), dir))
            emit statusMessage(tr("Falha ao abrir o terminal"));
        return;
    }

    if (!QProcess::startDetached(program, QStringList(), dir))
        emit statusMessage(tr("Falha ao abrir o terminal"));
}

// ---------- criação de arquivos ----------

void FileTab::newFolder()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Nova pasta"), tr("Nome:"), QLineEdit::Normal,
                                               tr("Nova pasta"), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    if (name.contains(QLatin1Char('/'))) {
        QMessageBox::warning(this, tr("Nome inválido"), tr("O nome não pode conter “/”."));
        return;
    }
    if (QFileInfo::exists(QDir(m_path).filePath(name))) {
        QMessageBox::warning(this, tr("Já existe"), tr("Já existe um item com esse nome."));
        return;
    }
    if (!QDir(m_path).mkdir(name))
        QMessageBox::warning(this, tr("Erro"), tr("Não foi possível criar a pasta."));
}

void FileTab::newFile(const QString &suggested, const QString &content, bool executable)
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Novo arquivo"), tr("Nome:"), QLineEdit::Normal,
                                               suggested, &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    if (name.contains(QLatin1Char('/'))) {
        QMessageBox::warning(this, tr("Nome inválido"), tr("O nome não pode conter “/”."));
        return;
    }
    const QString target = QDir(m_path).filePath(name);
    if (QFileInfo::exists(target)) {
        QMessageBox::warning(this, tr("Já existe"), tr("Já existe um item com esse nome."));
        return;
    }
    QFile f(target);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Erro"), tr("Não foi possível criar o arquivo."));
        return;
    }
    f.write(content.toUtf8());
    f.close();
    if (executable)
        QFile::setPermissions(target, QFile::permissions(target) | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);
    emit statusMessage(tr("Criado: %1").arg(name));
}

void FileTab::newFromTemplate(const QString &templatePath)
{
    const QString dst = uniquePath(m_path, QFileInfo(templatePath).fileName());
    if (copyRecursive(templatePath, dst))
        emit statusMessage(tr("Criado: %1").arg(QFileInfo(dst).fileName()));
    else
        QMessageBox::warning(this, tr("Erro"), tr("Não foi possível criar a partir do modelo."));
}

// ---------- compactação / descompactação (libarchive via bsdtar) ----------

QProcess *FileTab::runBsdtar(const QStringList &args, const QString &okMessage,
                             QProgressDialog *progress, const QString &scratch,
                             std::function<bool()> publish, const std::shared_ptr<bool> &keepScratch)
{
    auto *p = new QProcess(this);
    const QPointer<FileTab> self = this;
    const QPointer<QProgressDialog> dlg = progress;
    const auto canceled = std::make_shared<bool>(false);
    const auto cleanup = [scratch, keepScratch] {
        if (!scratch.isEmpty() && !(keepScratch && *keepScratch))
            QDir(scratch).removeRecursively();
    };
    const auto finishUi = [p, dlg] {
        if (dlg) {
            dlg->close();
            dlg->deleteLater();
        }
        p->deleteLater();
    };

    if (self)
        emit self->statusMessage(dlg ? tr("Extraindo…") : tr("Trabalhando…"));
    connect(p, &QProcess::errorOccurred, this, [self, p, canceled, cleanup, finishUi](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            if (self)
                emit self->statusMessage(tr("bsdtar não encontrado — instale o pacote libarchive"));
            Q_UNUSED(canceled)
            cleanup();
            finishUi();
        }
    });
    connect(p, &QProcess::finished, this,
            [self, p, canceled, okMessage, publish, cleanup, finishUi, keepScratch](int code, QProcess::ExitStatus st) {
                if (*canceled) {
                    if (self)
                        emit self->statusMessage(tr("Extração cancelada"));
                } else if (st == QProcess::NormalExit && code == 0) {
                    if (!publish || publish()) {
                        if (self)
                            emit self->statusMessage(okMessage);
                    } else if (self && !(keepScratch && *keepScratch)) {
                        emit self->statusMessage(tr("Não foi possível publicar a extração"));
                    }
                } else if (self) {
                    const QString err = QString::fromLocal8Bit(p->readAllStandardError()).trimmed();
                    emit self->statusMessage(tr("Falhou: %1").arg(err.isEmpty() ? tr("erro desconhecido") : err.left(200)));
                }
                cleanup();
                finishUi();
            });
    if (progress) {
        connect(progress, &QProgressDialog::canceled, this, [p, canceled] {
            *canceled = true;
            if (p->state() != QProcess::NotRunning)
                p->kill();
        });
    }
    p->start(QStringLiteral("bsdtar"), args);
    return p;
}

void FileTab::compress(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    const QStringList formats{"zip", "tar.gz", "tar.xz", "tar.zst", "tar.bz2", "7z"};
    bool ok = false;
    const QString fmt = QInputDialog::getItem(this, tr("Compactar"), tr("Formato:"), formats, 0, false, &ok);
    if (!ok)
        return;

    const QFileInfo first(paths.first());
    const QString base = paths.size() == 1 ? (first.isDir() ? first.fileName() : first.completeBaseName())
                                           : QFileInfo(m_path).fileName();
    const QString suggested = QFileInfo(uniquePath(m_path, (base.isEmpty() ? QStringLiteral("arquivos") : base)
                                                           + QLatin1Char('.') + fmt)).fileName();
    const QString name = QInputDialog::getText(this, tr("Compactar"), tr("Nome do arquivo:"),
                                               QLineEdit::Normal, suggested, &ok).trimmed();
    if (!ok || name.isEmpty() || name.contains(QLatin1Char('/')))
        return;

    QStringList args{"-a", "-c", "-f", QDir(m_path).filePath(name), "-C", m_path};
    for (const QString &p : paths)
        args << QFileInfo(p).fileName();
    runBsdtar(args, tr("Criado: %1").arg(name));
}

void FileTab::extract(const QString &archive, bool intoFolder)
{
    const QString destDir = m_path;
    QString dest = destDir;
    if (intoFolder) {
        QString base = QFileInfo(archive).fileName();
        base.chop(archiveSuffix(base).size());
        dest = uniquePath(m_path, base.isEmpty() ? QStringLiteral("extraido") : base);
    }

    QTemporaryDir tmp(QDir(m_path).filePath(QStringLiteral(".fm-extract-XXXXXX")));
    if (!tmp.isValid()) {
        emit statusMessage(tr("Não foi possível criar a pasta de destino"));
        return;
    }
    tmp.setAutoRemove(false);
    const QString scratch = tmp.path();
    auto keepScratch = std::make_shared<bool>(false);
    const QPointer<FileTab> self = this;

    auto *progress = new QProgressDialog(this);
    progress->setWindowTitle(tr("Extrair"));
    progress->setLabelText(tr("Extraindo…"));
    progress->setCancelButtonText(tr("Cancelar"));
    progress->setRange(0, 0);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setWindowModality(Qt::NonModal);
    progress->show();

    std::function<bool()> publish;
    if (intoFolder) {
        publish = [scratch, dest] {
            return !QFileInfo::exists(dest) && QDir().rename(scratch, dest);
        };
    } else {
        publish = [self, destDir, scratch, keepScratch] {
            const auto entries = QDir(scratch).entryInfoList(
                QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
            QList<QPair<QString, QString>> moved;
            QList<QPair<QString, QString>> backedUp;
            bool ok = true;
            for (const QFileInfo &e : entries) {
                const QString src = e.absoluteFilePath();
                const QString dst = QDir(destDir).filePath(e.fileName());
                if (QFileInfo::exists(dst) || QFileInfo(dst).isSymLink()) {
                    const QString backup = uniquePath(scratch, QStringLiteral(".fm-backup-") + e.fileName());
                    if (!QFile::rename(dst, backup)) {
                        ok = false;
                        break;
                    }
                    backedUp.append(qMakePair(backup, dst));
                }
                if (!QFile::rename(src, dst) && !(copyRecursive(src, dst) && removePath(src))) {
                    ok = false;
                    break;
                }
                moved.append(qMakePair(src, dst));
            }
            if (!ok) {
                for (int i = moved.size() - 1; i >= 0; --i)
                    QFile::rename(moved.at(i).second, moved.at(i).first);
                bool restoredAll = true;
                QString failedBackup;
                for (int i = backedUp.size() - 1; i >= 0; --i) {
                    const QString backup = backedUp.at(i).first;
                    if (!restoreBackup(backup, backedUp.at(i).second)) {
                        restoredAll = false;
                        if (failedBackup.isEmpty())
                            failedBackup = backup;
                    }
                }
                if (!restoredAll) {
                    *keepScratch = true;
                    if (self)
                        emit self->statusMessage(tr("Não foi possível restaurar o original; cópia mantida em %1")
                                                     .arg(failedBackup));
                }
            }
            return ok;
        };
    }

    // bsdtar, por padrão, recusa caminhos absolutos e “..” (proteção contra zip-slip).
    QProcess *proc = runBsdtar({"-x", "-f", archive, "-C", scratch}, tr("Extraído em %1").arg(dest),
                               progress, scratch, publish, keepScratch);
    const QPointer<QProcess> procGuard = proc;
    auto *lifetimeGuard = new QObject(this);
    connect(this, &QObject::destroyed, lifetimeGuard, [procGuard, scratch, keepScratch] {
        bool stopped = !procGuard || procGuard->state() == QProcess::NotRunning;
        if (!stopped) {
            procGuard->kill();
            stopped = waitForProcess(procGuard.data(), 2000);
        }
        if (!stopped) {
            // finished/errorOccurred também limpam; impedir que apaguem arquivo em uso.
            *keepScratch = true;
        } else if (!(keepScratch && *keepScratch)) {
            QDir(scratch).removeRecursively();
        }
    });
}
