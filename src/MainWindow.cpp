#include "MainWindow.h"
#include "FileTab.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPaintEvent>
#include <QPoint>
#include <QProcess>
#include <QResizeEvent>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVector>

namespace {

QString elideMiddle(const QString &text, int maxWidth)
{
    return QFontMetrics(QApplication::font()).elidedText(text, Qt::ElideMiddle, maxWidth);
}

struct SortActionSpec {
    const char *objectName;
    const char *label;
    int column;
    Qt::SortOrder order;
};

constexpr SortActionSpec kSortActions[] = {
    {"sortNameAsc", QT_TR_NOOP("Nome (A–Z)"), 0, Qt::AscendingOrder},
    {"sortNameDesc", QT_TR_NOOP("Nome (Z–A)"), 0, Qt::DescendingOrder},
    {"sortDateNewest", QT_TR_NOOP("Modificação (mais recente)"), 3, Qt::DescendingOrder},
    {"sortDateOldest", QT_TR_NOOP("Modificação (mais antiga)"), 3, Qt::AscendingOrder},
    {"sortSizeDesc", QT_TR_NOOP("Tamanho (maior)"), 1, Qt::DescendingOrder},
    {"sortSizeAsc", QT_TR_NOOP("Tamanho (menor)"), 1, Qt::AscendingOrder},
    {"sortTypeAsc", QT_TR_NOOP("Tipo (A–Z)"), 2, Qt::AscendingOrder},
    {"sortTypeDesc", QT_TR_NOOP("Tipo (Z–A)"), 2, Qt::DescendingOrder},
};

} // namespace

MainWindow::MainWindow(const QString &startPath, QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("fm"));
    resize(1100, 680);
    QSettings s(QStringLiteral("fm"), QStringLiteral("fm"));
    restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());

    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    setCentralWidget(m_tabs);

    auto *tb = addToolBar(tr("Navegação"));
    m_toolbar = tb;
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);

    auto icon = [this](const char *name, QStyle::StandardPixmap fb) {
        return QIcon::fromTheme(QLatin1String(name), style()->standardIcon(fb));
    };

    m_filesMenuButton = new QToolButton(tb);
    m_filesMenuButton->setObjectName(QStringLiteral("filesMenuButton"));
    m_filesMenuButton->setText(tr("Arquivos"));
    m_filesMenuButton->setIcon(icon("document-open", QStyle::SP_FileIcon));
    m_filesMenuButton->setToolTip(tr("Arquivos"));
    m_filesMenuButton->setAccessibleName(tr("Arquivos"));
    m_filesMenuButton->setPopupMode(QToolButton::InstantPopup);
    m_filesMenu = new QMenu(m_filesMenuButton);
    m_filesMenu->setObjectName(QStringLiteral("filesMenu"));
    m_filesMenuButton->setMenu(m_filesMenu);
    m_filesMenuButton->installEventFilter(this);
    tb->addWidget(m_filesMenuButton);

    m_back = tb->addAction(icon("go-previous", QStyle::SP_ArrowBack), tr("Voltar"));
    m_forward = tb->addAction(icon("go-next", QStyle::SP_ArrowForward), tr("Avançar"));
    m_up = tb->addAction(icon("go-up", QStyle::SP_ArrowUp), tr("Subir"));
    // A toolbar não dá foco por padrão aos botões de ação; sem isso Voltar,
    // Avançar e Subir ficam fora da navegação por Tab.
    for (QAction *a : {m_back, m_forward, m_up}) {
        if (auto *button = qobject_cast<QToolButton *>(tb->widgetForAction(a)))
            button->setFocusPolicy(Qt::TabFocus);
    }
    m_back->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Left));
    m_forward->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Right));
    m_up->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));

    m_pathContainer = new QWidget;
    auto *pathContainerLayout = new QHBoxLayout(m_pathContainer);
    pathContainerLayout->setContentsMargins(0, 0, 0, 0);
    pathContainerLayout->setSpacing(0);
    m_pathContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_pathBar = new QWidget(m_pathContainer);
    m_pathLayout = new QHBoxLayout(m_pathBar);
    m_pathLayout->setContentsMargins(4, 0, 4, 0);
    m_pathLayout->setSpacing(2);
    m_pathBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_pathBar->setMinimumWidth(80);
    pathContainerLayout->addWidget(m_pathBar);

    m_overflow = new QToolButton(m_pathBar);
    m_overflow->setObjectName(QStringLiteral("breadcrumbOverflow"));
    m_overflow->setText(QStringLiteral("…"));
    m_overflow->setToolTip(tr("Ancestrais ocultos"));
    m_overflow->setPopupMode(QToolButton::InstantPopup);
    m_overflow->setMenu(new QMenu(m_overflow));
    m_overflow->hide();

    m_pathEdit = new QLineEdit(m_pathContainer);
    m_pathEdit->setObjectName(QStringLiteral("pathEdit"));
    m_pathEdit->setClearButtonEnabled(true);
    m_pathEdit->hide();
    pathContainerLayout->addWidget(m_pathEdit);
    tb->addWidget(m_pathContainer);

    m_viewMenuButton = new QToolButton(tb);
    m_viewMenuButton->setObjectName(QStringLiteral("viewMenuButton"));
    m_viewMenuButton->setText(tr("Visualização"));
    m_viewMenuButton->setIcon(icon("view-list-details", QStyle::SP_FileDialogListView));
    m_viewMenuButton->setToolTip(tr("Visualização"));
    m_viewMenuButton->setAccessibleName(tr("Visualização"));
    m_viewMenuButton->setPopupMode(QToolButton::InstantPopup);
    m_viewMenu = new QMenu(m_viewMenuButton);
    m_viewMenu->setObjectName(QStringLiteral("viewMenu"));
    m_viewMenuButton->setMenu(m_viewMenu);
    m_viewMenuButton->installEventFilter(this);
    tb->addWidget(m_viewMenuButton);

    m_listAct = tb->addAction(QIcon::fromTheme("view-list-details"), tr("Lista detalhada"));
    m_listAct->setObjectName(QStringLiteral("listViewAction"));
    m_listAct->setCheckable(true);
    m_iconsAct = tb->addAction(QIcon::fromTheme("view-list-icons"), tr("Grade de ícones"));
    m_iconsAct->setObjectName(QStringLiteral("gridViewAction"));
    m_iconsAct->setCheckable(true);
    m_iconsAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    m_viewModeGroup = new QActionGroup(this);
    m_viewModeGroup->setExclusive(true);
    m_viewModeGroup->addAction(m_listAct);
    m_viewModeGroup->addAction(m_iconsAct);
    m_listAct->setChecked(true);
    m_previewAct = tb->addAction(QIcon::fromTheme("view-preview"), tr("Painel de pré-visualização (F11)"));
    m_previewAct->setCheckable(true);
    m_previewAct->setShortcut(QKeySequence(Qt::Key_F11));
    m_hiddenAct = tb->addAction(QIcon::fromTheme("view-visible"), tr("Mostrar arquivos ocultos (Ctrl+H)"));
    m_hiddenAct->setCheckable(true);
    m_hiddenAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));
    m_zoomInAct = tb->addAction(QIcon::fromTheme("zoom-in"), tr("Aumentar zoom"));
    m_zoomInAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Plus));
    m_zoomOutAct = tb->addAction(QIcon::fromTheme("zoom-out"), tr("Diminuir zoom"));
    m_zoomOutAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));
    m_zoomResetAct = tb->addAction(QIcon::fromTheme("zoom-original"), tr("Zoom original"));
    m_zoomResetAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    m_gridSizeAct = new QAction(tr("Mostrar tamanho na grade"), this);
    m_gridSizeAct->setObjectName(QStringLiteral("gridSizeAction"));
    m_gridSizeAct->setCheckable(true);
    m_gridDateAct = new QAction(tr("Mostrar data na grade"), this);
    m_gridDateAct->setObjectName(QStringLiteral("gridDateAction"));
    m_gridDateAct->setCheckable(true);
    m_newTabAct = tb->addAction(QIcon::fromTheme("tab-new"), tr("Nova aba (Ctrl+T)"));
    m_newTabAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));

    auto *newWindowAct = new QAction(icon("window-new", QStyle::SP_FileDialogNewFolder),
                                     tr("Nova janela"), this);
    newWindowAct->setObjectName(QStringLiteral("newWindowAction"));
    newWindowAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_N));
    connect(newWindowAct, &QAction::triggered, this, [this] {
        const QString path = currentTab() ? currentTab()->currentPath() : QDir::homePath();
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {path});
    });
    m_filesMenu->addAction(newWindowAct);
    m_filesMenu->addAction(m_newTabAct);
    m_filesMenu->addSeparator();

    auto *showAllTabsAct = new QAction(tr("Mostrar todas as abas"), this);
    showAllTabsAct->setObjectName(QStringLiteral("showAllTabsAction"));
    m_showAllTabsMenu = new QMenu(m_filesMenu);
    m_showAllTabsMenu->setObjectName(QStringLiteral("showAllTabsMenu"));
    showAllTabsAct->setMenu(m_showAllTabsMenu);
    connect(m_showAllTabsMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildTabsMenu);
    m_filesMenu->addAction(showAllTabsAct);
    m_filesMenu->addSeparator();

    auto *preferencesAct = new QAction(tr("Preferências…"), this);
    preferencesAct->setObjectName(QStringLiteral("preferencesAction"));
    connect(preferencesAct, &QAction::triggered, this, &MainWindow::openCustomizeDialog);
    m_filesMenu->addAction(preferencesAct);
    m_filesMenu->addSeparator();

    auto *shortcutsAct = new QAction(tr("Atalhos de teclado"), this);
    shortcutsAct->setObjectName(QStringLiteral("shortcutsAction"));
    connect(shortcutsAct, &QAction::triggered, this, &MainWindow::openShortcutsDialog);
    auto *helpAct = new QAction(tr("Ajuda"), this);
    helpAct->setObjectName(QStringLiteral("helpAction"));
    connect(helpAct, &QAction::triggered, this, &MainWindow::openHelpDialog);
    auto *aboutAct = new QAction(tr("Sobre o fm"), this);
    aboutAct->setObjectName(QStringLiteral("aboutAction"));
    connect(aboutAct, &QAction::triggered, this, &MainWindow::openAboutDialog);
    m_filesMenu->addAction(shortcutsAct);
    m_filesMenu->addAction(helpAct);
    m_filesMenu->addAction(aboutAct);

    m_viewMenu->addAction(m_listAct);
    m_viewMenu->addAction(m_iconsAct);
    m_viewMenu->addSeparator();
    m_sortGroup = new QActionGroup(this);
    m_sortGroup->setExclusive(true);
    for (int i = 0; i < SortActionCount; ++i) {
        const SortActionSpec &spec = kSortActions[i];
        QAction *action = new QAction(tr(spec.label), this);
        action->setObjectName(QString::fromUtf8(spec.objectName));
        action->setCheckable(true);
        m_sortGroup->addAction(action);
        m_sortActions[i] = action;
        m_viewMenu->addAction(action);
        const int column = spec.column;
        const Qt::SortOrder order = spec.order;
        connect(action, &QAction::triggered, this, [this, column, order] {
            if (FileTab *tab = currentTab())
                tab->setSort(column, order);
        });
    }
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_hiddenAct);
    m_viewMenu->addAction(m_previewAct);
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_zoomOutAct);
    m_viewMenu->addAction(m_zoomInAct);
    m_viewMenu->addAction(m_zoomResetAct);
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_gridSizeAct);
    m_viewMenu->addAction(m_gridDateAct);

    m_moreAct = new QAction(style()->standardIcon(QStyle::SP_ToolBarHorizontalExtensionButton),
                            QString(), this);
    m_moreAct->setObjectName(QStringLiteral("moreMenu"));
    m_moreAct->setToolTip(tr("Mais opções"));
    auto *moreMenu = new QMenu(this);
    moreMenu->setObjectName(QStringLiteral("moreMenu"));
    m_moreAct->setMenu(moreMenu);
    tb->addAction(m_moreAct);

    auto *interfaceMenu = moreMenu->addMenu(tr("Interface"));
    interfaceMenu->setObjectName(QStringLiteral("interfaceMenu"));
    m_presetCompactAct = interfaceMenu->addAction(tr("Compacta"));
    m_presetBalancedAct = interfaceMenu->addAction(tr("Equilibrada"));
    m_presetAdvancedAct = interfaceMenu->addAction(tr("Avançada"));
    for (QAction *a : {m_presetCompactAct, m_presetBalancedAct, m_presetAdvancedAct})
        a->setCheckable(true);
    m_restoreDefaultsAct = interfaceMenu->addAction(tr("Restaurar padrões"));
    QAction *customizeAct = interfaceMenu->addAction(tr("Personalizar…"));
    connect(customizeAct, &QAction::triggered, this, &MainWindow::openCustomizeDialog);

    for (QAction *a : {m_back, m_forward, m_up, m_listAct, m_iconsAct, m_previewAct, m_hiddenAct,
                       m_zoomInAct, m_zoomOutAct, m_zoomResetAct, m_newTabAct, newWindowAct})
        addAction(a); // garante que os atalhos funcionem em toda a janela

    auto *focusPath = new QAction(this);
    focusPath->setObjectName(QStringLiteral("focusPathAction"));
    focusPath->setText(tr("Editar caminho"));
    focusPath->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    addAction(focusPath);
    connect(focusPath, &QAction::triggered, this, &MainWindow::beginPathEdit);

    auto *closeTabAct = new QAction(this);
    closeTabAct->setObjectName(QStringLiteral("closeTabAction"));
    closeTabAct->setText(tr("Fechar aba"));
    closeTabAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
    addAction(closeTabAct);
    connect(closeTabAct, &QAction::triggered, this, [this] { closeTab(m_tabs->currentIndex()); });

    connect(m_back, &QAction::triggered, this, [this] { if (auto *t = currentTab()) t->back(); });
    connect(m_forward, &QAction::triggered, this, [this] { if (auto *t = currentTab()) t->forward(); });
    connect(m_up, &QAction::triggered, this, [this] { if (auto *t = currentTab()) t->up(); });
    connect(m_iconsAct, &QAction::toggled, this, [this](bool on) {
        for (int i = 0; i < m_tabs->count(); ++i)
            static_cast<FileTab *>(m_tabs->widget(i))->setIconMode(on);
    });
    connect(m_previewAct, &QAction::toggled, this, [this](bool on) {
        for (int i = 0; i < m_tabs->count(); ++i)
            static_cast<FileTab *>(m_tabs->widget(i))->setPreviewVisible(on);
        m_ui.preview = on;
        QSettings s(QStringLiteral("fm"), QStringLiteral("fm"));
        s.setValue(QStringLiteral("ui/preview"), on);
    });
    auto applyGridLegends = [this] {
        m_ui.gridSize = m_gridSizeAct->isChecked();
        m_ui.gridDate = m_gridDateAct->isChecked();
        for (int i = 0; i < m_tabs->count(); ++i)
            static_cast<FileTab *>(m_tabs->widget(i))->setGridLegends(m_ui.gridSize, m_ui.gridDate);
        QSettings s(QStringLiteral("fm"), QStringLiteral("fm"));
        s.setValue(QStringLiteral("ui/gridSize"), m_ui.gridSize);
        s.setValue(QStringLiteral("ui/gridDate"), m_ui.gridDate);
    };
    connect(m_gridSizeAct, &QAction::toggled, this, applyGridLegends);
    connect(m_gridDateAct, &QAction::toggled, this, applyGridLegends);
    connect(m_hiddenAct, &QAction::toggled, this, [this](bool on) {
        for (int i = 0; i < m_tabs->count(); ++i)
            static_cast<FileTab *>(m_tabs->widget(i))->setShowHidden(on);
    });
    auto applyZoom = [this](int delta) {
        auto *active = currentTab();
        const int target = active ? (delta == 0 ? 100 : active->zoom() + delta) : 100;
        for (int i = 0; i < m_tabs->count(); ++i)
            static_cast<FileTab *>(m_tabs->widget(i))->setZoom(target);
    };
    connect(m_zoomInAct, &QAction::triggered, this, [applyZoom] { applyZoom(25); });
    connect(m_zoomOutAct, &QAction::triggered, this, [applyZoom] { applyZoom(-25); });
    connect(m_zoomResetAct, &QAction::triggered, this, [applyZoom] { applyZoom(0); });

    connect(m_pathEdit, &QLineEdit::returnPressed, this, [this] {
        QString p = m_pathEdit->text().trimmed();
        if (p.startsWith(QLatin1Char('~')))
            p.replace(0, 1, QDir::homePath());
        if (auto *t = currentTab()) {
            if (t->goTo(p))
                endPathEdit();
        }
    });

    auto *escEdit = new QShortcut(QKeySequence(Qt::Key_Escape), m_pathEdit);
    escEdit->setContext(Qt::WidgetShortcut);
    connect(escEdit, &QShortcut::activated, this, &MainWindow::endPathEdit);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] { endPathEdit(); });
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::syncSortActions);

    // "Nova aba" abre na pasta da aba atual
    connect(m_newTabAct, &QAction::triggered, this, [this] {
        const FileTab *t = currentTab();
        addTab(t ? t->currentPath() : QDir::homePath());
    });

    auto choosePreset = [this](UiPreset preset) { applyUiPreferences(defaultUiPreferences(preset)); };
    connect(m_presetCompactAct, &QAction::triggered, this,
            [choosePreset] { choosePreset(UiPreset::Compact); });
    connect(m_presetBalancedAct, &QAction::triggered, this,
            [choosePreset] { choosePreset(UiPreset::Balanced); });
    connect(m_presetAdvancedAct, &QAction::triggered, this,
            [choosePreset] { choosePreset(UiPreset::Advanced); });
    connect(m_restoreDefaultsAct, &QAction::triggered, this, [this, choosePreset] {
        choosePreset(UiPreset::Balanced);
        statusBar()->showMessage(tr("Padrões restaurados"));
    });

    m_ui = loadUiPreferences(s);
    addTab(startPath);
    applyUiPreferences(m_ui);
    statusBar()->showMessage(QString());
}

FileTab *MainWindow::currentTab() const
{
    return static_cast<FileTab *>(m_tabs->currentWidget());
}

FileTab *MainWindow::addTab(const QString &path)
{
    auto *tab = new FileTab(path);
    tab->applyUiPreferences(m_ui);
    tab->setViewActions(m_listAct, m_iconsAct, m_previewAct, m_hiddenAct,
                        m_zoomInAct, m_zoomOutAct, m_zoomResetAct);
    tab->setIconMode(m_iconsAct->isChecked());
    tab->setZoom(currentTab() ? currentTab()->zoom() : 100);
    tab->setShowHidden(m_hiddenAct->isChecked());
    tab->setPreviewVisible(m_previewAct->isChecked());

    const int idx = m_tabs->addTab(tab, QDir(path).dirName());
    m_tabs->setCurrentIndex(idx);

    connect(tab, &FileTab::customizeRequested, this, &MainWindow::openCustomizeDialog);
    connect(tab, &FileTab::titleChanged, this, [this, tab](const QString &t) {
        const int i = m_tabs->indexOf(tab);
        if (i >= 0)
            m_tabs->setTabText(i, t);
    });
    connect(tab, &FileTab::pathChanged, this, [this, tab](const QString &p) {
        if (tab == currentTab()) {
            m_pathEdit->setText(p);
            setWindowTitle(QStringLiteral("%1 — fm").arg(p));
            rebuildBreadcrumb();
            syncToolbar();
        }
    });
    connect(tab, &FileTab::statusMessage, this, [this, tab](const QString &m) {
        if (tab == currentTab())
            statusBar()->showMessage(m);
    });
    connect(tab, &FileTab::openInNewTab, this, [this](const QString &p) { addTab(p); });
    connect(tab, &FileTab::sortChanged, this, &MainWindow::syncSortActions);

    syncToolbar();
    syncSortActions();
    rebuildBreadcrumb();
    syncTabBarVisibility();
    return tab;
}

void MainWindow::closeTab(int index)
{
    if (index < 0)
        return;
    if (m_tabs->count() == 1) {
        close();
        return;
    }
    QWidget *w = m_tabs->widget(index);
    m_tabs->removeTab(index);
    w->deleteLater();
    syncTabBarVisibility();
}

void MainWindow::syncTabBarVisibility()
{
    m_tabs->tabBar()->setVisible(m_tabs->count() > 1);
}

void MainWindow::syncToolbar()
{
    const FileTab *t = currentTab();
    if (!t)
        return;
    m_back->setEnabled(t->canGoBack());
    m_forward->setEnabled(t->canGoForward());
    m_pathEdit->setText(t->currentPath());
    setWindowTitle(QStringLiteral("%1 — fm").arg(t->currentPath()));
}

void MainWindow::syncToolbarActions()
{
    if (!m_toolbar || !m_moreAct)
        return;

    const bool narrow = width() < 720;
    const Qt::ToolButtonStyle menuStyle = narrow ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextOnly;
    m_filesMenuButton->setToolButtonStyle(menuStyle);
    m_viewMenuButton->setToolButtonStyle(menuStyle);

    const bool compact = m_ui.preset == UiPreset::Compact || narrow;
    const bool showView = !compact;
    const bool showNew = m_ui.preset != UiPreset::Compact && !narrow;
    const bool showExtras = m_ui.preset == UiPreset::Advanced && !narrow;

    struct Rule { QAction *action; bool visible; };
    const Rule rules[] = {
        {m_listAct, showView}, {m_iconsAct, showView}, {m_previewAct, showExtras}, {m_hiddenAct, showExtras},
        {m_zoomInAct, showExtras}, {m_zoomOutAct, showExtras}, {m_zoomResetAct, showExtras},
        {m_newTabAct, showNew},
    };
    QMenu *menu = m_moreAct->menu();

    for (const Rule &rule : rules) {
        if (!rule.action)
            continue;
        const bool inToolbar = m_toolbar->actions().contains(rule.action);
        if (rule.visible) {
            menu->removeAction(rule.action);
            if (!inToolbar)
                m_toolbar->insertAction(m_moreAct, rule.action);
        } else {
            if (inToolbar)
                m_toolbar->removeAction(rule.action);
            if (!menu->actions().contains(rule.action))
                menu->addAction(rule.action);
        }
    }
}

void MainWindow::syncSortActions()
{
    const FileTab *tab = currentTab();
    if (!tab)
        return;

    for (int i = 0; i < SortActionCount; ++i) {
        QAction *action = m_sortActions[i];
        if (!action)
            continue;
        const bool checked = kSortActions[i].column == tab->sortColumn()
                             && kSortActions[i].order == tab->sortOrder();
        if (action->isChecked() == checked)
            continue;
        const QSignalBlocker blocked(action);
        action->setChecked(checked);
    }
}

void MainWindow::applyUiPreferences(const UiPreferences &prefs)
{
    m_ui = prefs;
    switch (prefs.preset) {
    case UiPreset::Compact:
        m_breadcrumbLimit = 2;
        break;
    case UiPreset::Balanced:
        m_breadcrumbLimit = 4;
        break;
    case UiPreset::Advanced:
        m_breadcrumbLimit = -1;
        break;
    }

    m_presetCompactAct->setChecked(prefs.preset == UiPreset::Compact);
    m_presetBalancedAct->setChecked(prefs.preset == UiPreset::Balanced);
    m_presetAdvancedAct->setChecked(prefs.preset == UiPreset::Advanced);
    m_previewAct->setChecked(prefs.preview);
    const QSignalBlocker blockSize(m_gridSizeAct);
    const QSignalBlocker blockDate(m_gridDateAct);
    m_gridSizeAct->setChecked(prefs.gridSize);
    m_gridDateAct->setChecked(prefs.gridDate);

    for (int i = 0; i < m_tabs->count(); ++i)
        static_cast<FileTab *>(m_tabs->widget(i))->applyUiPreferences(prefs);
    m_zoomResetAct->trigger();

    syncToolbarActions();
    rebuildBreadcrumb();

    QSettings s(QStringLiteral("fm"), QStringLiteral("fm"));
    saveUiPreferences(s, prefs);
}

UiPreferences MainWindow::uiPreferences() const
{
    return m_ui;
}

void MainWindow::rebuildTabsMenu()
{
    if (!m_showAllTabsMenu || !m_tabs)
        return;

    m_showAllTabsMenu->clear();
    QHash<QString, int> labels;
    for (int i = 0; i < m_tabs->count(); ++i)
        ++labels[m_tabs->tabText(i)];

    for (int i = 0; i < m_tabs->count(); ++i) {
        QString text = m_tabs->tabText(i);
        auto *tab = qobject_cast<FileTab *>(m_tabs->widget(i));
        if (labels.value(text) > 1 && tab)
            text = tab->currentPath();
        QAction *action = m_showAllTabsMenu->addAction(text);
        action->setCheckable(true);
        action->setChecked(i == m_tabs->currentIndex());
        action->setData(i);
        connect(action, &QAction::triggered, this, [this, i] { m_tabs->setCurrentIndex(i); });
    }
}

void MainWindow::openShortcutsDialog()
{
    QStringList lines;
    for (QAction *action : actions()) {
        if (action->text().isEmpty())
            continue;
        QString line = action->text();
        if (!action->shortcut().isEmpty())
            line += QStringLiteral("\t") + action->shortcut().toString(QKeySequence::NativeText);
        lines << line;
    }
    QMessageBox::information(this, tr("Atalhos de teclado"), lines.join(QLatin1Char('\n')));
}

void MainWindow::openHelpDialog()
{
    QMessageBox::information(this, tr("Ajuda"),
                             tr("Use Voltar, Avançar e Subir para navegar; Ctrl+L edita o caminho. "
                                "Abas trocam com a faixa ou o menu Arquivos, e os menus "
                                "Arquivos e Visualização também abrem com o teclado."));
}

void MainWindow::openAboutDialog()
{
    QMessageBox::about(this, tr("Sobre o fm"),
                       tr("fm — gerenciador de arquivos\nQt %1").arg(QLatin1String(QT_VERSION_STR)));
}

void MainWindow::openCustomizeDialog()
{
    auto *dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("customizeDialog"));
    dialog->setWindowTitle(tr("Personalizar interface"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    auto *root = new QVBoxLayout(dialog);
    auto *form = new QFormLayout;
    auto *density = new QComboBox(dialog);
    density->setObjectName(QStringLiteral("customDensity"));
    density->addItems({ tr("Compacta"), tr("Equilibrada"), tr("Avançada") });
    density->setCurrentIndex(m_ui.density);
    form->addRow(tr("Densidade da lista"), density);

    auto *columnsLayout = new QHBoxLayout;
    const QStringList names { tr("Nome"), tr("Tamanho"), tr("Tipo"), tr("Modificação") };
    QList<QCheckBox *> columns;
    for (int i = 0; i < names.size(); ++i) {
        auto *check = new QCheckBox(names.at(i), dialog);
        check->setObjectName(QStringLiteral("customColumn%1").arg(i));
        check->setChecked(m_ui.columns.contains(QString::number(i)));
        if (i == 0) {
            check->setEnabled(false);
            check->setChecked(true);
        }
        columnsLayout->addWidget(check);
        columns.append(check);
    }
    form->addRow(tr("Colunas visíveis"), columnsLayout);

    auto *iconSize = new QComboBox(dialog);
    iconSize->setObjectName(QStringLiteral("customIconSize"));
    iconSize->addItems({ tr("32 px"), tr("48 px"), tr("64 px") });
    iconSize->setCurrentIndex(m_ui.iconSize == 32 ? 0 : (m_ui.iconSize == 64 ? 2 : 1));
    form->addRow(tr("Tamanho dos ícones"), iconSize);

    auto *preview = new QCheckBox(dialog);
    preview->setObjectName(QStringLiteral("customPreview"));
    preview->setChecked(m_ui.preview);
    form->addRow(tr("Painel de pré-visualização"), preview);
    root->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dialog, [this, dialog, density, columns,
                                                           iconSize, preview] {
        UiPreferences prefs = m_ui;
        prefs.density = density->currentIndex();
        static const int sizes[] = { 32, 48, 64 };
        prefs.iconSize = sizes[qBound(0, iconSize->currentIndex(), 2)];
        QStringList selected;
        for (int i = 0; i < columns.size(); ++i)
            if (columns.at(i)->isChecked())
                selected << QString::number(i);
        prefs.columns = normalizeColumns(selected, prefs.preset);
        prefs.preview = preview->isChecked();
        applyUiPreferences(prefs);
        dialog->accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    dialog->show();
}

void MainWindow::beginPathEdit()
{
    FileTab *t = currentTab();
    if (!t)
        return;
    m_pathEdit->setText(t->currentPath());
    m_pathBar->hide();
    m_pathEdit->show();
    m_pathEdit->setFocus();
    m_pathEdit->selectAll();
}

void MainWindow::endPathEdit()
{
    m_pathEdit->hide();
    m_pathBar->show();
    rebuildBreadcrumb();
    syncToolbar();
    if (auto *t = currentTab())
        t->setFocus();
}

void MainWindow::navigateBreadcrumb(const QString &path)
{
    if (auto *t = currentTab())
        t->goTo(path);
}

void MainWindow::rebuildBreadcrumb()
{
    while (QLayoutItem *item = m_pathLayout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            if (w != m_overflow) {
                w->hide();
                delete w;
            }
        }
        delete item;
    }
    m_overflow->menu()->clear();
    m_overflow->hide();

    const FileTab *tab = currentTab();
    if (!tab)
        return;

    struct Segment { QString path; QString label; };
    QVector<Segment> segments{{QStringLiteral("/"), QStringLiteral("/")}};
    QString acc;
    for (const QString &part : QDir::cleanPath(tab->currentPath()).split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        acc += QLatin1Char('/') + part;
        segments.append({acc, part});
    }

    QVector<QToolButton *> buttons;
    buttons.reserve(segments.size());
    for (const Segment &s : segments) {
        auto *b = new QToolButton(m_pathBar);
        b->setObjectName(QStringLiteral("breadcrumbButton"));
        b->setProperty("path", s.path);
        b->setToolTip(s.path);
        b->setText(elideMiddle(s.label, 90));
        b->setAutoRaise(true);
        b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        connect(b, &QToolButton::clicked, this,
                [this, b] { navigateBreadcrumb(b->property("path").toString()); });
        m_pathLayout->addWidget(b);
        buttons.append(b);
    }

    QVector<bool> visible(buttons.size(), true);
    bool overflowNeeded = false;
    // Limite do preset: mantém os segmentos mais próximos do atual (o último nunca sai).
    if (m_breadcrumbLimit > 0 && buttons.size() > m_breadcrumbLimit) {
        const int first = buttons.size() - m_breadcrumbLimit;
        for (int i = 0; i < first; ++i)
            visible[i] = false;
        overflowNeeded = true;
    }
    auto applyVisibility = [&] {
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setVisible(visible[i]);
    };
    const int margins = m_pathLayout->contentsMargins().left() + m_pathLayout->contentsMargins().right();
    constexpr int slack = 2; // arredondamento do layout entre resizeEvent e o geométrico final
    const int available = m_pathBar->width() - margins - slack;
    const int spacing = m_pathLayout->spacing();
    auto usedWidth = [&] {
        int w = 0;
        int count = 0;
        for (int i = 0; i < buttons.size(); ++i)
            if (visible[i]) {
                w += buttons[i]->sizeHint().width();
                ++count;
            }
        if (overflowNeeded) {
            w += m_overflow->sizeHint().width();
            ++count;
        }
        if (count > 0)
            w += spacing * (count - 1);
        return w;
    };

    QVector<int> candidates;
    for (int i = 1; i < buttons.size() - 1; ++i)
        candidates.append(i);
    candidates.append(0); // raiz entra no menu por último
    applyVisibility();
    for (int i : candidates) {
        if (usedWidth() <= available)
            break;
        int shown = 0;
        for (bool v : visible)
            if (v)
                ++shown;
        if (shown <= 1)
            break;
        visible[i] = false;
        overflowNeeded = true;
        applyVisibility();
    }

    for (int i = 0; i < buttons.size(); ++i) {
        if (visible[i])
            continue;
        const QString p = buttons[i]->property("path").toString();
        QAction *a = m_overflow->menu()->addAction(elideMiddle(p, 160));
        a->setData(p);
        connect(a, &QAction::triggered, this, [this, p] { navigateBreadcrumb(p); });
    }
    m_overflow->setVisible(overflowNeeded);
    if (overflowNeeded)
        m_pathLayout->addWidget(m_overflow);
    m_pathLayout->addStretch(1);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if ((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
        && (watched == m_filesMenuButton || watched == m_viewMenuButton)) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            if (event->type() == QEvent::KeyPress) {
                auto *button = static_cast<QToolButton *>(watched);
                if (!button->menu()->isVisible())
                    button->menu()->popup(button->mapToGlobal(QPoint(0, button->height())));
            }
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::resizeEvent(QResizeEvent *e)
{
    QMainWindow::resizeEvent(e);
    syncToolbarActions();
    rebuildBreadcrumb();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    QSettings s(QStringLiteral("fm"), QStringLiteral("fm"));
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    QMainWindow::closeEvent(e);
}

void MainWindow::setBenchMode(bool on)
{
    m_bench = on;
}

void MainWindow::paintEvent(QPaintEvent *e)
{
    QMainWindow::paintEvent(e);
    if (m_bench && !m_painted) {
        m_painted = true;
        emit firstPaint();
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    }
}
