#include "MainWindow.h"
#include "FileTab.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVector>

namespace {

QString elideMiddle(const QString &text, int maxWidth)
{
    return QFontMetrics(QApplication::font()).elidedText(text, Qt::ElideMiddle, maxWidth);
}

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
    connect(focusPath, &QAction::triggered, this, &MainWindow::beginPathEdit);

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
            if (t->goTo(p))
                endPathEdit();
        }
    });

    auto *escEdit = new QShortcut(QKeySequence(Qt::Key_Escape), m_pathEdit);
    escEdit->setContext(Qt::WidgetShortcut);
    connect(escEdit, &QShortcut::activated, this, &MainWindow::endPathEdit);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] { endPathEdit(); });

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
            rebuildBreadcrumb();
            syncToolbar();
        }
    });
    connect(tab, &FileTab::statusMessage, this, [this, tab](const QString &m) {
        if (tab == currentTab())
            statusBar()->showMessage(m);
    });
    connect(tab, &FileTab::openInNewTab, this, [this](const QString &p) { addTab(p); });

    syncToolbar();
    rebuildBreadcrumb();
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

void MainWindow::resizeEvent(QResizeEvent *e)
{
    QMainWindow::resizeEvent(e);
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
