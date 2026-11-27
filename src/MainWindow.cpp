#include "MainWindow.h"
#include "FileTab.h"

#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QLineEdit>
#include <QSettings>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>

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
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);

    auto icon = [this](const char *name, QStyle::StandardPixmap fb) {
        return QIcon::fromTheme(QLatin1String(name), style()->standardIcon(fb));
    };

    m_back = tb->addAction(icon("go-previous", QStyle::SP_ArrowBack), tr("Voltar"));
    m_forward = tb->addAction(icon("go-next", QStyle::SP_ArrowForward), tr("Avançar"));
    m_up = tb->addAction(icon("go-up", QStyle::SP_ArrowUp), tr("Subir"));
    m_back->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Left));
    m_forward->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Right));
    m_up->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));

    m_pathEdit = new QLineEdit;
    m_pathEdit->setClearButtonEnabled(true);
    tb->addWidget(m_pathEdit);

    m_iconsAct = tb->addAction(QIcon::fromTheme("view-list-icons"), tr("Visão em ícones"));
    m_iconsAct->setCheckable(true);
    m_iconsAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    m_previewAct = tb->addAction(QIcon::fromTheme("view-preview"), tr("Painel de pré-visualização (F11)"));
    m_previewAct->setCheckable(true);
    m_previewAct->setShortcut(QKeySequence(Qt::Key_F11));
    m_hiddenAct = tb->addAction(QIcon::fromTheme("view-visible"), tr("Mostrar arquivos ocultos (Ctrl+H)"));
    m_hiddenAct->setCheckable(true);
    m_hiddenAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));
    QAction *newTab = tb->addAction(QIcon::fromTheme("tab-new"), tr("Nova aba (Ctrl+T)"));
    newTab->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));

    for (QAction *a : {m_back, m_forward, m_up, m_iconsAct, m_previewAct, m_hiddenAct, newTab})
        addAction(a); // garante que os atalhos funcionem em toda a janela

    auto *focusPath = new QAction(this);
    focusPath->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    addAction(focusPath);
    connect(focusPath, &QAction::triggered, this, [this] { m_pathEdit->setFocus(); m_pathEdit->selectAll(); });

    auto *closeTabAct = new QAction(this);
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
    });
    connect(m_hiddenAct, &QAction::toggled, this, [this](bool on) {
        for (int i = 0; i < m_tabs->count(); ++i)
            static_cast<FileTab *>(m_tabs->widget(i))->setShowHidden(on);
    });

    connect(m_pathEdit, &QLineEdit::returnPressed, this, [this] {
        QString p = m_pathEdit->text().trimmed();
        if (p.startsWith(QLatin1Char('~')))
            p.replace(0, 1, QDir::homePath());
        if (auto *t = currentTab()) {
            t->goTo(p);
            t->setFocus();
        }
    });
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] { syncToolbar(); });

    // "Nova aba" abre na pasta da aba atual
    connect(newTab, &QAction::triggered, this, [this] {
        const FileTab *t = currentTab();
        addTab(t ? t->currentPath() : QDir::homePath());
    });

    addTab(startPath);
    statusBar()->showMessage(QString());
}

FileTab *MainWindow::currentTab() const
{
    return static_cast<FileTab *>(m_tabs->currentWidget());
}

FileTab *MainWindow::addTab(const QString &path)
{
    auto *tab = new FileTab(path);
    tab->setIconMode(m_iconsAct->isChecked());
    tab->setShowHidden(m_hiddenAct->isChecked());
    tab->setPreviewVisible(m_previewAct->isChecked());

    const int idx = m_tabs->addTab(tab, QDir(path).dirName());
    m_tabs->setCurrentIndex(idx);

    connect(tab, &FileTab::titleChanged, this, [this, tab](const QString &t) {
        const int i = m_tabs->indexOf(tab);
        if (i >= 0)
            m_tabs->setTabText(i, t);
    });
    connect(tab, &FileTab::pathChanged, this, [this, tab](const QString &p) {
        if (tab == currentTab()) {
            m_pathEdit->setText(p);
            setWindowTitle(QStringLiteral("%1 — fm").arg(p));
            syncToolbar();
        }
    });
    connect(tab, &FileTab::statusMessage, this, [this, tab](const QString &m) {
        if (tab == currentTab())
            statusBar()->showMessage(m);
    });
    connect(tab, &FileTab::openInNewTab, this, [this](const QString &p) { addTab(p); });

    syncToolbar();
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

void MainWindow::closeEvent(QCloseEvent *e)
{
    QSettings s(QStringLiteral("fm"), QStringLiteral("fm"));
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    QMainWindow::closeEvent(e);
}
