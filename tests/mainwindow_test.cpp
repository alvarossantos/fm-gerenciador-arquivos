#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QListWidget>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QScopeGuard>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>

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
    void breadcrumbFollowsActiveTab();
    void breadcrumbClickThenBackUsesActiveTabHistory();
    void overflowMenuKeepsCurrentAndNavigates();
    void pathEditKeepsInvalidTextAndEscRestores();
    void tabChangeDiscardsPendingEdit();
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
    QCOMPARE(selectedPlacePath(&tab), home);

    tab.goTo(nested);
    QCOMPARE(tab.currentPath(), nested);
    QCOMPARE(selectedPlacePath(&tab), documents);
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
    QCOMPARE(selectedPlacePath(&tab), home);
    QVERIFY(selectedPlacePath(&tab) != documents);
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

QTEST_MAIN(MainWindowTest)
#include "mainwindow_test.moc"
