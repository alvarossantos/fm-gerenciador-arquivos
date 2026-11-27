#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QListWidget>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QScopeGuard>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeView>

#include "FileTab.h"
#include "MainWindow.h"

namespace {

QString selectedPlacePath(QWidget *tab)
{
    auto *places = tab->findChild<QListWidget *>(QStringLiteral("places"));
    return places && places->currentItem()
               ? places->currentItem()->data(Qt::UserRole).toString()
               : QString();
}

bool hasSelectedPlace(QWidget *tab)
{
    auto *places = tab->findChild<QListWidget *>(QStringLiteral("places"));
    return places && places->currentItem() != nullptr;
}

QList<QToolButton *> breadcrumbButtons(QWidget *window)
{
    return window->findChildren<QToolButton *>(QStringLiteral("breadcrumbButton"));
}

QString lastBreadcrumbPath(QWidget *window)
{
    const auto buttons = breadcrumbButtons(window);
    return buttons.isEmpty() ? QString() : buttons.last()->property("path").toString();
}

QAction *findAction(QWidget *window, const QString &prefix)
{
    for (QAction *a : window->findChildren<QAction *>())
        if (a->text().startsWith(prefix))
            return a;
    return nullptr;
}

} // namespace

class MainWindowTest : public QObject
{
    Q_OBJECT

private slots:
    void placesHighlightFollowsGoToBackForward();
    void placesHighlightUsesAncestorRules();
    void placesHighlightRejectsPrefixCollision();
    void placesHighlightClearsOutsideRegisteredPlaces();
    void breadcrumbFollowsActiveTab();
    void breadcrumbClickThenBackUsesActiveTabHistory();
    void overflowMenuKeepsCurrentAndNavigates();
    void overflowAndCurrentButtonFitInsideBar();
    void pathEditKeepsInvalidTextAndEscRestores();
    void tabChangeDiscardsPendingEdit();
    void tabBarHiddenWithSingleTab();
    void closingDownToOneTabHidesBarAgain();
    void zoomActionsReachActiveTab();
    void zoomActionsApplyToAllTabs();
};

void MainWindowTest::placesHighlightFollowsGoToBackForward()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString home = QDir(root.path()).filePath(QStringLiteral("home"));
    const QString documents = QDir(root.path()).filePath(QStringLiteral("Documentos"));
    const QString nested = documents + QStringLiteral("/projeto");
    QVERIFY(QDir().mkpath(home));
    QVERIFY(QDir().mkpath(nested));
    setPlacesForTests({home, documents});
    QScopeGuard restore([] { setPlacesForTests({}); });

    FileTab tab(home);
    auto *tree = tab.findChild<QTreeView *>();
    QVERIFY(tree);
    QCOMPARE(selectedPlacePath(&tab), home);

    tab.goTo(nested);
    QCOMPARE(tab.currentPath(), nested);
    QCOMPARE(selectedPlacePath(&tab), documents);
    QVERIFY(!tree->selectionModel()->hasSelection());
    QVERIFY(tab.canGoBack());
    QVERIFY(!tab.canGoForward());

    tab.back();
    QCOMPARE(tab.currentPath(), home);
    QCOMPARE(selectedPlacePath(&tab), home);
    QVERIFY(!tab.canGoBack());
    QVERIFY(tab.canGoForward());

    tab.forward();
    QCOMPARE(tab.currentPath(), nested);
    QCOMPARE(selectedPlacePath(&tab), documents);
    QVERIFY(!tree->selectionModel()->hasSelection());
    QVERIFY(tab.canGoBack());
    QVERIFY(!tab.canGoForward());
}

void MainWindowTest::placesHighlightUsesAncestorRules()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString home = QDir(root.path()).filePath(QStringLiteral("home"));
    const QString documents = QDir(root.path()).filePath(QStringLiteral("Documentos"));
    const QString project = documents + QStringLiteral("/projeto");
    QVERIFY(QDir().mkpath(home));
    QVERIFY(QDir().mkpath(project + QStringLiteral("/sub")));
    QVERIFY(QDir().mkpath(home + QStringLiteral("/outro")));
    setPlacesForTests({home, documents, project});
    QScopeGuard restore([] { setPlacesForTests({}); });

    FileTab tab(home);
    tab.goTo(project + QStringLiteral("/sub"));
    QCOMPARE(selectedPlacePath(&tab), project);

    tab.goTo(home + QStringLiteral("/outro"));
    QCOMPARE(selectedPlacePath(&tab), home);

    tab.goTo(QStringLiteral("/"));
    QCOMPARE(tab.currentPath(), QStringLiteral("/"));
    QCOMPARE(selectedPlacePath(&tab), QStringLiteral("/"));
}

void MainWindowTest::placesHighlightRejectsPrefixCollision()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString home = QDir(root.path()).filePath(QStringLiteral("home"));
    const QString documents = QDir(root.path()).filePath(QStringLiteral("Documentos"));
    const QString documents2 = QDir(root.path()).filePath(QStringLiteral("Documentos2"));
    QVERIFY(QDir().mkpath(home));
    QVERIFY(QDir().mkpath(documents));
    QVERIFY(QDir().mkpath(documents2));
    setPlacesForTests({home, documents});
    QScopeGuard restore([] { setPlacesForTests({}); });

    FileTab tab(home);
    tab.goTo(documents2);
    QCOMPARE(tab.currentPath(), documents2);
    QVERIFY(!hasSelectedPlace(&tab));
    QVERIFY(selectedPlacePath(&tab) != documents);
}

void MainWindowTest::placesHighlightClearsOutsideRegisteredPlaces()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString home = QDir(root.path()).filePath(QStringLiteral("home"));
    const QString documents = QDir(root.path()).filePath(QStringLiteral("Documentos"));
    QVERIFY(QDir().mkpath(home));
    QVERIFY(QDir().mkpath(documents));
    QVERIFY(QDir(QStringLiteral("/etc")).exists());
    setPlacesForTests({home, documents});
    QScopeGuard restore([] { setPlacesForTests({}); });

    FileTab tab(home);
    QVERIFY(hasSelectedPlace(&tab));

    tab.goTo(QStringLiteral("/etc"));
    QCOMPARE(tab.currentPath(), QStringLiteral("/etc"));
    QVERIFY(!hasSelectedPlace(&tab));
    QVERIFY(tab.canGoBack());
}

void MainWindowTest::breadcrumbFollowsActiveTab()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString base = root.path();
    const QString home = QDir(base).filePath(QStringLiteral("home"));
    const QString documents = QDir(base).filePath(QStringLiteral("Documentos"));
    const QString project = documents + QStringLiteral("/projeto");
    const QString downloads = QDir(base).filePath(QStringLiteral("Downloads"));
    const QString downloadTest = downloads + QStringLiteral("/teste");
    QVERIFY(QDir().mkpath(home));
    QVERIFY(QDir().mkpath(project));
    QVERIFY(QDir().mkpath(downloadTest));
    setPlacesForTests({home, documents, downloads});
    QScopeGuard restore([] { setPlacesForTests({}); });

    MainWindow w(project);
    w.show();
    QApplication::processEvents();
    QCOMPARE(lastBreadcrumbPath(&w), project);

    auto *tabs = w.findChild<QTabWidget *>();
    QVERIFY(tabs);
    QAction *newTab = findAction(&w, QStringLiteral("Nova aba"));
    QVERIFY(newTab);
    newTab->trigger();
    QCOMPARE(tabs->count(), 2);
    auto *tab1 = static_cast<FileTab *>(tabs->widget(1));
    tab1->goTo(downloadTest);
    QApplication::processEvents();
    QCOMPARE(lastBreadcrumbPath(&w), downloadTest);

    tabs->setCurrentIndex(0);
    QApplication::processEvents();
    QCOMPARE(lastBreadcrumbPath(&w), project);
    QCOMPARE(selectedPlacePath(tabs->widget(0)), documents);

    tabs->setCurrentIndex(1);
    QApplication::processEvents();
    QCOMPARE(lastBreadcrumbPath(&w), downloadTest);
    QCOMPARE(selectedPlacePath(tabs->widget(1)), downloads);
}

void MainWindowTest::breadcrumbClickThenBackUsesActiveTabHistory()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString base = root.path();
    const QString home = QDir(base).filePath(QStringLiteral("home"));
    const QString documents = QDir(base).filePath(QStringLiteral("Documentos"));
    const QString nested = documents + QStringLiteral("/projeto/sub");
    const QString downloadTest = QDir(base).filePath(QStringLiteral("Downloads/teste"));
    QVERIFY(QDir().mkpath(home));
    QVERIFY(QDir().mkpath(nested));
    QVERIFY(QDir().mkpath(downloadTest));
    setPlacesForTests({home, documents});
    QScopeGuard restore([] { setPlacesForTests({}); });

    MainWindow w(documents);
    w.show();
    QApplication::processEvents();
    auto *tabs = w.findChild<QTabWidget *>();
    QVERIFY(tabs);
    auto *tab0 = static_cast<FileTab *>(tabs->widget(0));

    QAction *newTab = findAction(&w, QStringLiteral("Nova aba"));
    QVERIFY(newTab);
    newTab->trigger();
    auto *tab1 = static_cast<FileTab *>(tabs->widget(1));
    tab1->goTo(downloadTest);
    tabs->setCurrentIndex(0);
    QApplication::processEvents();

    tab0->goTo(nested);
    QToolButton *documentsButton = nullptr;
    for (QToolButton *b : breadcrumbButtons(&w))
        if (b->property("path").toString() == documents)
            documentsButton = b;
    QVERIFY(documentsButton);
    QTest::mouseClick(documentsButton, Qt::LeftButton);
    QCOMPARE(tab0->currentPath(), documents);

    QAction *back = findAction(&w, QStringLiteral("Voltar"));
    QVERIFY(back);
    back->trigger();
    QCOMPARE(tab0->currentPath(), nested);
    QCOMPARE(tab0->canGoForward(), true);
    QCOMPARE(tab1->currentPath(), downloadTest);
}

void MainWindowTest::overflowMenuKeepsCurrentAndNavigates()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString home = QDir(root.path()).filePath(QStringLiteral("home"));
    const QString documents = QDir(root.path()).filePath(QStringLiteral("Documentos"));
    QVERIFY(QDir().mkpath(home));
    QVERIFY(QDir().mkpath(documents));
    setPlacesForTests({home, documents});
    QScopeGuard restore([] { setPlacesForTests({}); });

    MainWindow w(documents);
    w.resize(420, 600);
    w.show();
    QApplication::processEvents();

    const QString deep = documents + QStringLiteral("/a/b/c/d/com espaços");
    QVERIFY(QDir().mkpath(deep));
    auto *tabs = w.findChild<QTabWidget *>();
    QVERIFY(tabs);
    auto *tab = static_cast<FileTab *>(tabs->widget(0));
    tab->goTo(deep);
    QApplication::processEvents();

    auto *overflow = w.findChild<QToolButton *>(QStringLiteral("breadcrumbOverflow"));
    QVERIFY(overflow);
    QMenu *menu = overflow->menu();
    QVERIFY(menu);

    QStringList visiblePaths;
    for (QToolButton *b : breadcrumbButtons(&w))
        if (b->isVisible())
            visiblePaths << b->property("path").toString();
    QStringList menuPaths;
    for (QAction *a : menu->actions())
        menuPaths << a->data().toString();

    const QStringList ancestors = {
        QStringLiteral("/"), documents,
        documents + QStringLiteral("/a"), documents + QStringLiteral("/a/b"),
        documents + QStringLiteral("/a/b/c"), documents + QStringLiteral("/a/b/c/d")
    };
    for (const QString &p : ancestors)
        QVERIFY2(visiblePaths.contains(p) || menuPaths.contains(p), qPrintable("ancestral ausente: " + p));
    QVERIFY2(visiblePaths.contains(deep), "a pasta atual precisa estar visível");
    QVERIFY(menuPaths.contains(documents));

    QAction *documentsAction = nullptr;
    for (QAction *a : menu->actions())
        if (a->data().toString() == documents)
            documentsAction = a;
    QVERIFY(documentsAction);
    documentsAction->trigger();
    QCOMPARE(tab->currentPath(), documents);

    for (QToolButton *b : breadcrumbButtons(&w))
        QVERIFY(!b->property("path").toString().isEmpty());
}

void MainWindowTest::overflowAndCurrentButtonFitInsideBar()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString home = QDir(root.path()).filePath(QStringLiteral("home"));
    const QString documents = QDir(root.path()).filePath(QStringLiteral("Documentos"));
    QVERIFY(QDir().mkpath(home));
    QVERIFY(QDir().mkpath(documents));
    setPlacesForTests({home, documents});
    QScopeGuard restore([] { setPlacesForTests({}); });

    MainWindow w(documents);
    w.show();
    QApplication::processEvents();

    const QString deep = documents + QStringLiteral("/a/b/c/d/e/f/g/h");
    QVERIFY(QDir().mkpath(deep));
    auto *tabs = w.findChild<QTabWidget *>();
    QVERIFY(tabs);
    static_cast<FileTab *>(tabs->widget(0))->goTo(deep);
    QApplication::processEvents();

    auto *overflow = w.findChild<QToolButton *>(QStringLiteral("breadcrumbOverflow"));
    QVERIFY(overflow);
    QWidget *bar = overflow->parentWidget();
    QVERIFY(bar);

    for (int width : {430, 420, 410, 400, 398, 396, 394, 392, 390, 388, 386, 384, 382, 380, 370}) {
        w.resize(width, 600);
        QApplication::processEvents();
        QVERIFY(overflow->isVisible());
        auto *layout = qobject_cast<QHBoxLayout *>(bar->layout());
        QVERIFY(layout);
        int used = 0;
        int items = 0;
        for (QToolButton *b : breadcrumbButtons(&w)) {
            if (!b->isVisible())
                continue;
            used += b->sizeHint().width();
            ++items;
        }
        used += overflow->sizeHint().width();
        ++items;
        used += layout->spacing() * (items - 1);
        const int margins = layout->contentsMargins().left() + layout->contentsMargins().right();
        constexpr int layoutCompression = 4; // o layout comprime o último QToolButton em até 4 px
        QVERIFY2(used <= bar->width() - margins + layoutCompression,
                 qPrintable("largura " + QString::number(width) + ": breadcrumb excede a barra: "
                            + QString::number(used) + " > "
                            + QString::number(bar->width() - margins + layoutCompression)));

        QToolButton *current = nullptr;
        for (QToolButton *b : breadcrumbButtons(&w))
            if (b->property("path").toString() == deep)
                current = b;
        QVERIFY(current);
        QVERIFY(current->isVisible());

        for (QToolButton *b : {overflow, current}) {
            QVERIFY2(b->geometry().left() >= 0,
                     qPrintable("largura " + QString::number(width) + ": botão começa fora da barra: " + b->text()));
            QVERIFY2(b->geometry().right() <= bar->width(),
                     qPrintable("largura " + QString::number(width) + ": botão termina fora da barra: " + b->text()));
        }
    }
}

void MainWindowTest::pathEditKeepsInvalidTextAndEscRestores()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString home = QDir(root.path()).filePath(QStringLiteral("home"));
    const QString project = QDir(root.path()).filePath(QStringLiteral("Documentos/projeto"));
    QVERIFY(QDir().mkpath(home));
    QVERIFY(QDir().mkpath(project));
    setPlacesForTests({home});
    QScopeGuard restore([] { setPlacesForTests({}); });

    MainWindow w(home);
    w.show();
    QApplication::processEvents();
    auto *tabs = w.findChild<QTabWidget *>();
    QVERIFY(tabs);
    auto *tab = static_cast<FileTab *>(tabs->widget(0));
    auto *edit = w.findChild<QLineEdit *>(QStringLiteral("pathEdit"));
    QVERIFY(edit);
    QVERIFY(edit->isHidden());

    QTest::keyClick(&w, Qt::Key_L, Qt::ControlModifier);
    QVERIFY(edit->isVisible());
    QVERIFY(edit->hasFocus());
    QCOMPARE(edit->selectedText(), edit->text());

    QTest::keyClicks(edit, project);
    QTest::keyClick(edit, Qt::Key_Return);
    QApplication::processEvents();
    QCOMPARE(tab->currentPath(), project);
    QVERIFY(edit->isHidden());
    QCOMPARE(lastBreadcrumbPath(&w), project);

    const bool backBefore = tab->canGoBack();
    QTest::keyClick(&w, Qt::Key_L, Qt::ControlModifier);
    const QString invalid = root.path() + QStringLiteral("/nao-existe-fm");
    QTest::keyClicks(edit, invalid);
    QTest::keyClick(edit, Qt::Key_Return);
    QApplication::processEvents();
    QVERIFY(edit->isVisible());
    QCOMPARE(edit->text(), invalid);
    QCOMPARE(tab->currentPath(), project);
    QCOMPARE(tab->canGoBack(), backBefore);
    QVERIFY(w.statusBar()->currentMessage().contains(QStringLiteral("Pasta inválida")));

    QTest::keyClick(edit, Qt::Key_Escape);
    QApplication::processEvents();
    QVERIFY(edit->isHidden());
    QCOMPARE(lastBreadcrumbPath(&w), project);

    QAction *back = findAction(&w, QStringLiteral("Voltar"));
    QAction *forward = findAction(&w, QStringLiteral("Avançar"));
    QVERIFY(back);
    QVERIFY(forward);
    back->trigger();
    QCOMPARE(tab->currentPath(), home);
    forward->trigger();
    QCOMPARE(tab->currentPath(), project);
}

void MainWindowTest::tabChangeDiscardsPendingEdit()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString home = QDir(root.path()).filePath(QStringLiteral("home"));
    const QString downloadTest = QDir(root.path()).filePath(QStringLiteral("Downloads/teste"));
    QVERIFY(QDir().mkpath(home));
    QVERIFY(QDir().mkpath(downloadTest));
    setPlacesForTests({home});
    QScopeGuard restore([] { setPlacesForTests({}); });

    MainWindow w(home);
    w.show();
    QApplication::processEvents();
    auto *tabs = w.findChild<QTabWidget *>();
    QVERIFY(tabs);
    auto *tab0 = static_cast<FileTab *>(tabs->widget(0));
    auto *edit = w.findChild<QLineEdit *>(QStringLiteral("pathEdit"));
    QVERIFY(edit);

    QAction *newTab = findAction(&w, QStringLiteral("Nova aba"));
    QVERIFY(newTab);
    newTab->trigger();
    auto *tab1 = static_cast<FileTab *>(tabs->widget(1));
    tab1->goTo(downloadTest);
    tabs->setCurrentIndex(0);
    QApplication::processEvents();

    QTest::keyClick(&w, Qt::Key_L, Qt::ControlModifier);
    QTest::keyClicks(edit, downloadTest);
    QCOMPARE(edit->text(), downloadTest);
    tabs->setCurrentIndex(1);
    QApplication::processEvents();

    QVERIFY(edit->isHidden());
    QCOMPARE(lastBreadcrumbPath(&w), downloadTest);
    QCOMPARE(edit->text(), downloadTest);

    QTest::keyClick(&w, Qt::Key_Return);
    QApplication::processEvents();
    QCOMPARE(tab0->currentPath(), home);
    QCOMPARE(tab1->currentPath(), downloadTest);
}

void MainWindowTest::tabBarHiddenWithSingleTab()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(QDir().mkpath(root.path()));

    MainWindow w(root.path());
    w.show();
    QApplication::processEvents();

    auto *tabs = w.findChild<QTabWidget *>();
    QVERIFY(tabs);
    auto *bar = tabs->tabBar();
    QVERIFY(bar);
    QCOMPARE(tabs->count(), 1);
    QVERIFY(!bar->isVisible());

    QAction *newTab = findAction(&w, QStringLiteral("Nova aba"));
    QVERIFY(newTab);
    QVERIFY(newTab->isVisible());
    newTab->trigger();
    QApplication::processEvents();
    QCOMPARE(tabs->count(), 2);
    QVERIFY(bar->isVisible());
}

void MainWindowTest::closingDownToOneTabHidesBarAgain()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(QDir().mkpath(root.path()));

    MainWindow w(root.path());
    w.show();
    QApplication::processEvents();

    auto *tabs = w.findChild<QTabWidget *>();
    QVERIFY(tabs);
    auto *bar = tabs->tabBar();
    QVERIFY(bar);
    QAction *newTab = findAction(&w, QStringLiteral("Nova aba"));
    QVERIFY(newTab);
    newTab->trigger();
    newTab->trigger();
    QApplication::processEvents();
    QCOMPARE(tabs->count(), 3);
    QVERIFY(bar->isVisible());

    tabs->setCurrentIndex(2);
    tabs->tabBar()->tabCloseRequested(2);
    QApplication::processEvents();
    QCOMPARE(tabs->count(), 2);
    QVERIFY(bar->isVisible());

    tabs->setCurrentIndex(1);
    tabs->tabBar()->tabCloseRequested(1);
    QApplication::processEvents();
    QCOMPARE(tabs->count(), 1);
    QVERIFY(!bar->isVisible());
    QVERIFY(newTab->isVisible());
}

void MainWindowTest::zoomActionsReachActiveTab()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(QDir().mkpath(root.path()));

    MainWindow w(root.path());
    w.show();
    QApplication::processEvents();

    auto *tabs = w.findChild<QTabWidget *>();
    QVERIFY(tabs);
    auto *tab = static_cast<FileTab *>(tabs->widget(0));
    QVERIFY(tab);
    QCOMPARE(tab->zoom(), 100);

    QAction *zoomIn = findAction(&w, QStringLiteral("Aumentar zoom"));
    QVERIFY(zoomIn);
    zoomIn->trigger();
    QApplication::processEvents();
    QVERIFY(tab->zoom() > 100);

    QAction *zoomOut = findAction(&w, QStringLiteral("Diminuir zoom"));
    QVERIFY(zoomOut);
    zoomOut->trigger();
    QCOMPARE(tab->zoom(), 100);

    QAction *zoomReset = findAction(&w, QStringLiteral("Zoom original"));
    QVERIFY(zoomReset);
    tab->setZoom(200);
    zoomReset->trigger();
    QCOMPARE(tab->zoom(), 100);
}

void MainWindowTest::zoomActionsApplyToAllTabs()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(QDir().mkpath(root.path()));

    MainWindow w(root.path());
    w.show();
    QApplication::processEvents();

    auto *tabs = w.findChild<QTabWidget *>();
    QVERIFY(tabs);
    QAction *newTab = findAction(&w, QStringLiteral("Nova aba"));
    QVERIFY(newTab);
    newTab->trigger();
    QApplication::processEvents();
    QCOMPARE(tabs->count(), 2);

    QAction *zoomIn = findAction(&w, QStringLiteral("Aumentar zoom"));
    QVERIFY(zoomIn);
    zoomIn->trigger();
    QApplication::processEvents();

    for (int i = 0; i < tabs->count(); ++i) {
        auto *tab = static_cast<FileTab *>(tabs->widget(i));
        QVERIFY2(tab->zoom() > 100, qPrintable(QString("aba %1 ficou com zoom %2").arg(i).arg(tab->zoom())));
    }
}

QTEST_MAIN(MainWindowTest)
#include "mainwindow_test.moc"
