#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QListWidget>
#include <QKeySequence>
#include <QPushButton>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPoint>
#include <QScopeGuard>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>

#include <functional>

#include "FileTab.h"
#include "MainWindow.h"
#include "UiPreferences.h"

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

QAction *menuAction(QMenu *menu, const QString &text)
{
    if (!menu)
        return nullptr;
    for (QAction *a : menu->actions())
        if (a->text() == text)
            return a;
    return nullptr;
}

QStringList viewMenuLabels(QMenu *menu)
{
    QStringList labels;
    if (!menu)
        return labels;
    for (QAction *a : menu->actions())
        labels << (a->isSeparator() ? QStringLiteral("---") : a->text());
    return labels;
}

QModelIndex findRowNamed(QTreeView *view, const QString &name)
{
    if (!view)
        return {};
    for (int row = 0; row < view->model()->rowCount(view->rootIndex()); ++row) {
        const QModelIndex index = view->model()->index(row, 0, view->rootIndex());
        if (view->model()->data(index).toString() == name)
            return index;
    }
    return {};
}

void closePopups()
{
    while (QWidget *p = qApp->activePopupWidget())
        p->close();
}

bool openEmptyContextMenu(FileTab *tab)
{
    auto *view = tab->findChild<QTreeView *>();
    if (!view)
        return false;
    const QPoint vp(5, 400); // área vazia, abaixo dos itens
    QContextMenuEvent ev(QContextMenuEvent::Mouse, vp, view->viewport()->mapToGlobal(vp));
    QApplication::sendEvent(view->viewport(), &ev);
    return true;
}

bool usesIconMode(const FileTab *tab)
{
    auto *stack = tab->findChild<QStackedWidget *>();
    return stack && qobject_cast<QListView *>(stack->currentWidget()) != nullptr;
}

QToolBar *toolbar(QWidget *window)
{
    return window->findChild<QToolBar *>();
}

struct TempConfig {
    QTemporaryDir dir;
    QByteArray previous = qgetenv("XDG_CONFIG_HOME");
    bool wasSet = !previous.isNull();
    QScopeGuard<std::function<void()>> restore;

    TempConfig()
        : restore([this] {
              if (wasSet)
                  qputenv("XDG_CONFIG_HOME", previous);
              else
                  qunsetenv("XDG_CONFIG_HOME");
          })
    {
        qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
    }
};

QAction *choosePreset(QWidget *window, const QString &label)
{
    auto *menu = window->findChild<QMenu *>(QStringLiteral("interfaceMenu"));
    QAction *action = menuAction(menu, label);
    if (action)
        action->trigger();
    return action;
}

int visibleBreadcrumbButtons(QWidget *window)
{
    int count = 0;
    for (QToolButton *b : breadcrumbButtons(window))
        if (b->isVisible())
            ++count;
    return count;
}

bool samePreferences(const UiPreferences &a, const UiPreferences &b)
{
    return a.preset == b.preset && a.density == b.density && a.iconSize == b.iconSize
           && a.columns == b.columns && a.preview == b.preview;
}

QListWidgetItem *findPlace(QListWidget *places, const QString &label)
{
    const auto items = places->findItems(label, Qt::MatchExactly);
    return items.isEmpty() ? nullptr : items.first();
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
    void newTabInheritsCurrentZoom();
    void shortcutActionsHaveReadableLabels();
    void viewModeSwitchesBothWaysFromToolbarAndContextMenu();
    void viewMenuSharesToolbarStateAcrossTabs();
    void sharedActionsSurviveToolbarRemoval();
    void narrowWindowKeepsActionsAndAncestors();
    void breadcrumbLimitPerPreset();
    void switchPresetResetsManualAdjustments();
    void restoreDefaultsPersistsAfterRestart();
    void previewTogglePersistsAfterRestart();
    void customizeActionOpensDialog();
    void customizeDialogPersistsManualAdjustments();
    void keyboardOpensMenusAt320InEveryPreset();
    void tabsMenuUsesFullPathForDuplicates();
    void localDialogsHaveReadableText();
};

void MainWindowTest::shortcutActionsHaveReadableLabels()
{
    MainWindow w(QDir::homePath());
    auto *focus = w.findChild<QAction *>(QStringLiteral("focusPathAction"));
    auto *close = w.findChild<QAction *>(QStringLiteral("closeTabAction"));
    QVERIFY(focus && close);
    QCOMPARE(focus->text(), QStringLiteral("Editar caminho"));
    QCOMPARE(close->text(), QStringLiteral("Fechar aba"));
    QCOMPARE(focus->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_L));
    QCOMPARE(close->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_W));
}

void MainWindowTest::viewModeSwitchesBothWaysFromToolbarAndContextMenu()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    MainWindow w(root.path());
    w.show();
    QApplication::processEvents();

    QAction *grid = w.findChild<QAction *>(QStringLiteral("gridViewAction"));
    QAction *list = w.findChild<QAction *>(QStringLiteral("listViewAction"));
    auto *tab = w.findChild<FileTab *>();
    QVERIFY(grid && list && tab);
    auto *stack = tab->findChild<QStackedWidget *>();
    QVERIFY(stack);

    auto verifyMode = [&](bool icons) {
        QCOMPARE(grid->isChecked(), icons);
        if (icons)
            QVERIFY(qobject_cast<QListView *>(stack->currentWidget()));
        else
            QVERIFY(qobject_cast<QTreeView *>(stack->currentWidget()));
    };
    grid->trigger();
    verifyMode(true);
    list->trigger();
    verifyMode(false);
    grid->trigger();
    verifyMode(true);

    auto switchFromContextMenu = [&](const QString &label) {
        bool triggered = false;
        setContextMenuHookForTests([&](QMenu *menu, const QStringList &) {
            QTimer::singleShot(0, [] { closePopups(); });
            QMenu *view = nullptr;
            for (QAction *action : menu->actions())
                if (action->text() == QStringLiteral("Exibir"))
                    view = action->menu();
            QAction *action = menuAction(view, label);
            if (action) {
                triggered = true;
                action->trigger();
            }
        });
        const auto restore = qScopeGuard([] { setContextMenuHookForTests({}); });
        QVERIFY(openEmptyContextMenu(tab));
        QApplication::processEvents();
        QVERIFY(triggered);
    };
    switchFromContextMenu(QStringLiteral("Lista detalhada"));
    verifyMode(false);
    switchFromContextMenu(QStringLiteral("Grade de ícones"));
    verifyMode(true);
}

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

void MainWindowTest::newTabInheritsCurrentZoom()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(QDir().mkpath(root.path()));

    MainWindow w(root.path());
    w.show();
    QApplication::processEvents();

    auto *tabs = w.findChild<QTabWidget *>();
    QVERIFY(tabs);
    QAction *zoomIn = findAction(&w, QStringLiteral("Aumentar zoom"));
    QVERIFY(zoomIn);
    QAction *newTab = findAction(&w, QStringLiteral("Nova aba"));
    QVERIFY(newTab);

    zoomIn->trigger();
    QApplication::processEvents();
    QCOMPARE(static_cast<FileTab *>(tabs->widget(0))->zoom(), 125);

    newTab->trigger();
    QApplication::processEvents();
    QCOMPARE(tabs->count(), 2);
    auto *second = static_cast<FileTab *>(tabs->widget(1));
    QCOMPARE(second->zoom(), 125);

    zoomIn->trigger();
    QApplication::processEvents();
    QCOMPARE(static_cast<FileTab *>(tabs->widget(0))->zoom(), 150);
    QCOMPARE(second->zoom(), 150);
}

void MainWindowTest::viewMenuSharesToolbarStateAcrossTabs()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString home = QDir(root.path()).filePath(QStringLiteral("home"));
    QVERIFY(QDir().mkpath(home));

    MainWindow w(home);
    w.show();
    QApplication::processEvents();

    auto *tabs = w.findChild<QTabWidget *>();
    QVERIFY(tabs);
    QCOMPARE(tabs->count(), 1);
    auto *iconsAct = w.findChild<QAction *>(QStringLiteral("gridViewAction"));
    auto *listAct = w.findChild<QAction *>(QStringLiteral("listViewAction"));
    auto *previewAct = findAction(&w, QStringLiteral("Painel de pré-visualização"));
    auto *hiddenAct = findAction(&w, QStringLiteral("Mostrar arquivos ocultos"));
    auto *zoomInAct = findAction(&w, QStringLiteral("Aumentar zoom"));
    auto *zoomOutAct = findAction(&w, QStringLiteral("Diminuir zoom"));
    auto *zoomResetAct = findAction(&w, QStringLiteral("Zoom original"));
    QVERIFY(iconsAct && listAct && previewAct && hiddenAct && zoomInAct && zoomOutAct && zoomResetAct);
    const int windowActions = w.actions().size();

    QAction *newTabAct = findAction(&w, QStringLiteral("Nova aba"));
    QVERIFY(newTabAct);
    newTabAct->trigger();
    QApplication::processEvents();
    QCOMPARE(tabs->count(), 2);
    QCOMPARE(w.actions().size(), windowActions);
    auto *firstTab = static_cast<FileTab *>(tabs->widget(0));
    auto *secondTab = static_cast<FileTab *>(tabs->widget(1));
    QVERIFY(firstTab && secondTab);
    QVERIFY(!iconsAct->isChecked());
    QVERIFY(!usesIconMode(firstTab));
    QVERIFY(!usesIconMode(secondTab));

    const QStringList expectedLabels{
        QStringLiteral("Lista detalhada"), QStringLiteral("Grade de ícones"), QStringLiteral("---"),
        previewAct->text(), hiddenAct->text(), QStringLiteral("---"),
        zoomInAct->text(), zoomOutAct->text(), zoomResetAct->text()
    };

    auto captureViewMenu = [](FileTab *tab, const std::function<void(QMenu *, QMenu *)> &inspect) {
        setContextMenuHookForTests([&inspect](QMenu *menu, const QStringList &) {
            QTimer::singleShot(0, [] { closePopups(); });
            QAction *viewAction = menuAction(menu, QStringLiteral("Exibir"));
            inspect(menu, viewAction ? viewAction->menu() : nullptr);
        });
        const auto resetHook = qScopeGuard([] { setContextMenuHookForTests({}); });
        QVERIFY(openEmptyContextMenu(tab));
        QApplication::processEvents();
    };

    iconsAct->trigger(); // estado vindo da barra
    QVERIFY(iconsAct->isChecked());
    QVERIFY(usesIconMode(firstTab));
    QVERIFY(usesIconMode(secondTab));

    QStringList labels;
    bool localModes = false;
    bool gridFollowsToolbar = false;
    bool injectedAreToolbarActions = false;
    bool noNewWindowActions = false;
    captureViewMenu(firstTab, [&](QMenu *, QMenu *view) {
        labels = viewMenuLabels(view);
        noNewWindowActions = w.actions().size() == windowActions;
        QAction *details = menuAction(view, QStringLiteral("Lista detalhada"));
        QAction *grid = menuAction(view, QStringLiteral("Grade de ícones"));
        localModes = details == listAct && grid == iconsAct
                     && details->isCheckable() && grid->isCheckable()
                     && details->shortcut().isEmpty()
                     && grid->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_1);
        gridFollowsToolbar = grid && grid->isChecked() == iconsAct->isChecked();
        injectedAreToolbarActions =
            menuAction(view, previewAct->text()) == previewAct
            && menuAction(view, hiddenAct->text()) == hiddenAct
            && menuAction(view, zoomInAct->text()) == zoomInAct
            && menuAction(view, zoomOutAct->text()) == zoomOutAct
            && menuAction(view, zoomResetAct->text()) == zoomResetAct;
    });
    QCOMPARE(labels, expectedLabels);
    QVERIFY(localModes);
    QVERIFY(gridFollowsToolbar);
    QVERIFY(injectedAreToolbarActions);
    QVERIFY(noNewWindowActions);

    // Ação do submenu aplica setChecked na ação compartilhada.
    captureViewMenu(firstTab, [&](QMenu *, QMenu *view) {
        QAction *details = menuAction(view, QStringLiteral("Lista detalhada"));
        if (details)
            details->trigger();
    });
    QVERIFY(!iconsAct->isChecked());
    QVERIFY(!usesIconMode(firstTab));
    QVERIFY(!usesIconMode(secondTab));

    // Reabrir em outra aba reflete o estado corrente.
    QStringList labelsSecond;
    bool detailsChecked = false;
    bool gridChecked = false;
    captureViewMenu(secondTab, [&](QMenu *, QMenu *view) {
        labelsSecond = viewMenuLabels(view);
        QAction *details = menuAction(view, QStringLiteral("Lista detalhada"));
        QAction *grid = menuAction(view, QStringLiteral("Grade de ícones"));
        detailsChecked = details && details->isChecked();
        gridChecked = grid && grid->isChecked();
    });
    QCOMPARE(labelsSecond, expectedLabels);
    QVERIFY(detailsChecked);
    QVERIFY(!gridChecked);

    captureViewMenu(secondTab, [&](QMenu *, QMenu *view) {
        QAction *grid = menuAction(view, QStringLiteral("Grade de ícones"));
        if (grid)
            grid->trigger();
    });
    QVERIFY(iconsAct->isChecked());
    QVERIFY(usesIconMode(firstTab));
    QVERIFY(usesIconMode(secondTab));

    listAct->trigger(); // alternância pela barra
    QVERIFY(!iconsAct->isChecked());
    QVERIFY(!usesIconMode(firstTab));
    QVERIFY(!usesIconMode(secondTab));
    QCOMPARE(w.actions().size(), windowActions);

    bool detailsCheckedAgain = false;
    captureViewMenu(firstTab, [&](QMenu *, QMenu *view) {
        QAction *details = menuAction(view, QStringLiteral("Lista detalhada"));
        detailsCheckedAgain = details && details->isChecked();
    });
    QVERIFY(detailsCheckedAgain);
}

void MainWindowTest::sharedActionsSurviveToolbarRemoval()
{
    TempConfig config;
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString home = QDir(root.path()).filePath(QStringLiteral("home"));
    QVERIFY(QDir().mkpath(home));

    MainWindow w(home);
    w.show();
    QApplication::processEvents();
    auto *tabs = w.findChild<QTabWidget *>();
    QVERIFY(tabs);
    findAction(&w, QStringLiteral("Nova aba"))->trigger();
    QApplication::processEvents();
    QCOMPARE(tabs->count(), 2);

    auto *interfaceMenu = w.findChild<QMenu *>(QStringLiteral("interfaceMenu"));
    QVERIFY2(interfaceMenu, "submenu Interface ausente");
    QVERIFY2(choosePreset(&w, QStringLiteral("Compacta")), "ação Compacta ausente");
    QApplication::processEvents();

    auto *more = w.findChild<QMenu *>(QStringLiteral("moreMenu"));
    auto *tb = toolbar(&w);
    QVERIFY(more && tb);
    QAction *icons = w.findChild<QAction *>(QStringLiteral("gridViewAction"));
    QAction *list = w.findChild<QAction *>(QStringLiteral("listViewAction"));
    QAction *zoomIn = findAction(&w, QStringLiteral("Aumentar zoom"));
    QVERIFY(icons && list && zoomIn);
    const QKeySequence zoomShortcut = zoomIn->shortcut();

    QVERIFY2(tb->widgetForAction(icons) == nullptr, "Grade de ícones ainda na toolbar");
    QVERIFY2(tb->widgetForAction(list) == nullptr, "Lista detalhada ainda na toolbar");
    QVERIFY2(tb->widgetForAction(zoomIn) == nullptr, "zoom ainda na toolbar");
    QCOMPARE(menuAction(more, icons->text()), icons);
    QCOMPARE(menuAction(more, list->text()), list);
    QCOMPARE(menuAction(more, zoomIn->text()), zoomIn);
    QCOMPARE(zoomIn->shortcut(), zoomShortcut);
    QCOMPARE(w.actions().count(icons), 1);
    QCOMPARE(w.actions().count(list), 1);
    QCOMPARE(w.actions().count(zoomIn), 1);

    icons->trigger();
    zoomIn->trigger();
    QApplication::processEvents();
    for (int i = 0; i < tabs->count(); ++i) {
        auto *tab = static_cast<FileTab *>(tabs->widget(i));
        QVERIFY2(usesIconMode(tab), "ação recolhida não trocou a visão em todas as abas");
        QCOMPARE(tab->zoom(), 125);
    }

    list->trigger();
    QApplication::processEvents();
    for (int i = 0; i < tabs->count(); ++i) {
        auto *tab = static_cast<FileTab *>(tabs->widget(i));
        QVERIFY2(!usesIconMode(tab), "ação Lista detalhada recolhida não trocou a visão em todas as abas");
        QCOMPARE(tab->zoom(), 125);
    }
}

void MainWindowTest::narrowWindowKeepsActionsAndAncestors()
{
    TempConfig config;
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString deep = QDir(root.path()).filePath(QStringLiteral("a/b/c/d/e"));
    QVERIFY(QDir().mkpath(deep));

    MainWindow w(deep);
    w.resize(320, 680);
    w.show();
    QApplication::processEvents();
    QVERIFY(choosePreset(&w, QStringLiteral("Compacta")));
    QApplication::processEvents();

    auto *more = w.findChild<QMenu *>(QStringLiteral("moreMenu"));
    auto *tb = toolbar(&w);
    QVERIFY(more && tb);
    QAction *list = w.findChild<QAction *>(QStringLiteral("listViewAction"));
    QAction *preview = findAction(&w, QStringLiteral("Painel de pré-visualização"));
    QAction *zoomIn = findAction(&w, QStringLiteral("Aumentar zoom"));
    QVERIFY(list && preview && zoomIn);
    QVERIFY2(tb->widgetForAction(list) == nullptr, "Lista detalhada ainda na toolbar estreita");
    QCOMPARE(menuAction(more, list->text()), list);
    QCOMPARE(menuAction(more, preview->text()), preview);
    QCOMPARE(menuAction(more, zoomIn->text()), zoomIn);

    w.activateWindow();
    QTest::keyClick(&w, Qt::Key_L, Qt::ControlModifier);
    auto *edit = w.findChild<QLineEdit *>(QStringLiteral("pathEdit"));
    QVERIFY(edit);
    QVERIFY2(edit->hasFocus(), "Ctrl+L não focou o campo de caminho");

    QTest::keyClick(edit, Qt::Key_Escape);
    QApplication::processEvents();
    QVERIFY2(!edit->isVisible(), "Esc não fechou a edição do caminho");
    auto *overflow = w.findChild<QToolButton *>(QStringLiteral("breadcrumbOverflow"));
    QVERIFY(overflow && overflow->isVisible());
    bool hasAncestor = false;
    for (QAction *a : overflow->menu()->actions())
        if (a->data().toString().startsWith(QDir::cleanPath(root.path())))
            hasAncestor = true;
    QVERIFY2(hasAncestor, "ancestral oculto ausente no menu do breadcrumb");
}

void MainWindowTest::breadcrumbLimitPerPreset()
{
    TempConfig config;
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString deep = QDir(root.path()).filePath(QStringLiteral("a/b/c/d/e"));
    QVERIFY(QDir().mkpath(deep));

    MainWindow w(deep);
    w.show();
    QApplication::processEvents();
    QVERIFY(choosePreset(&w, QStringLiteral("Compacta")));
    QApplication::processEvents();
    const int compact = visibleBreadcrumbButtons(&w);
    QVERIFY2(compact > 0 && compact <= 2, "Compacta deve limitar o breadcrumb a 2 segmentos");
    auto *overflow = w.findChild<QToolButton *>(QStringLiteral("breadcrumbOverflow"));
    QVERIFY(overflow && overflow->isVisible());

    QVERIFY(choosePreset(&w, QStringLiteral("Avançada")));
    QApplication::processEvents();
    QVERIFY2(visibleBreadcrumbButtons(&w) > compact, "Avançada não ampliou o breadcrumb");
}

void MainWindowTest::switchPresetResetsManualAdjustments()
{
    TempConfig config;
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString home = QDir(root.path()).filePath(QStringLiteral("home"));
    QVERIFY(QDir().mkpath(home));

    MainWindow w(home);
    w.show();
    QApplication::processEvents();
    auto *tabs = w.findChild<QTabWidget *>();
    QVERIFY(tabs);
    findAction(&w, QStringLiteral("Nova aba"))->trigger();
    QApplication::processEvents();
    findAction(&w, QStringLiteral("Aumentar zoom"))->trigger();
    w.findChild<QAction *>(QStringLiteral("gridViewAction"))->trigger();
    QCOMPARE(static_cast<FileTab *>(tabs->widget(0))->zoom(), 125);

    QVERIFY(choosePreset(&w, QStringLiteral("Avançada")));
    QApplication::processEvents();

    const UiPreferences prefs = w.uiPreferences();
    QCOMPARE(prefs.preset, UiPreset::Advanced);
    QCOMPARE(prefs.iconSize, 64);
    QCOMPARE(prefs.density, 2);
    QVERIFY(prefs.preview);
    for (int i = 0; i < tabs->count(); ++i)
        QCOMPARE(static_cast<FileTab *>(tabs->widget(i))->zoom(), 100);
}

void MainWindowTest::restoreDefaultsPersistsAfterRestart()
{
    TempConfig config;
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString home = QDir(root.path()).filePath(QStringLiteral("home"));
    QVERIFY(QDir().mkpath(home));

    {
        MainWindow w(home);
        w.show();
        QApplication::processEvents();
        auto *tabs = w.findChild<QTabWidget *>();
        QVERIFY(tabs);
        QVERIFY(choosePreset(&w, QStringLiteral("Avançada")));
        UiPreferences manual = w.uiPreferences();
        manual.density = 0;
        manual.iconSize = 32;
        w.applyUiPreferences(manual);
        findAction(&w, QStringLiteral("Aumentar zoom"))->trigger();
        QCOMPARE(static_cast<FileTab *>(tabs->widget(0))->zoom(), 125);

        auto *interfaceMenu = w.findChild<QMenu *>(QStringLiteral("interfaceMenu"));
        QVERIFY(interfaceMenu);
        QAction *restore = menuAction(interfaceMenu, QStringLiteral("Restaurar padrões"));
        QVERIFY(restore);
        QVERIFY(!restore->isCheckable());
        restore->trigger();

        QVERIFY2(samePreferences(w.uiPreferences(), defaultUiPreferences(UiPreset::Balanced)),
                 "Restaurar padrões não voltou à Equilibrada");
        for (int i = 0; i < tabs->count(); ++i)
            QCOMPARE(static_cast<FileTab *>(tabs->widget(i))->zoom(), 100);
        auto *status = w.findChild<QStatusBar *>();
        QVERIFY(status);
        QCOMPARE(status->currentMessage(), QStringLiteral("Padrões restaurados"));
        w.close();
    }

    MainWindow restarted(home);
    QVERIFY2(samePreferences(restarted.uiPreferences(), defaultUiPreferences(UiPreset::Balanced)),
             "preferências não sobreviveram ao reinício");
    QSettings settings(QStringLiteral("fm"), QStringLiteral("fm"));
    QCOMPARE(settings.value(QStringLiteral("ui/preset")).toString(), QStringLiteral("balanced"));
}

void MainWindowTest::previewTogglePersistsAfterRestart()
{
    TempConfig config;
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString home = QDir(root.path()).filePath(QStringLiteral("home"));
    QVERIFY(QDir().mkpath(home));

    {
        MainWindow w(home);
        w.show();
        QApplication::processEvents();
        QAction *preview = findAction(&w, QStringLiteral("Painel de pré-visualização"));
        QVERIFY(preview);
        QVERIFY(!preview->isChecked());
        preview->trigger();
        QVERIFY(preview->isChecked());
        QVERIFY2(w.uiPreferences().preview, "prévia não entrou nas preferências");
        QSettings settings(QStringLiteral("fm"), QStringLiteral("fm"));
        QVERIFY(settings.value(QStringLiteral("ui/preview")).toBool());
        w.close();
    }

    MainWindow restarted(home);
    QVERIFY(restarted.uiPreferences().preview);
    QAction *preview = findAction(&restarted, QStringLiteral("Painel de pré-visualização"));
    QVERIFY(preview && preview->isChecked());
}

void MainWindowTest::customizeActionOpensDialog()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString home = QDir(root.path()).filePath(QStringLiteral("home"));
    QVERIFY(QDir().mkpath(home));

    MainWindow w(home);
    w.show();
    QApplication::processEvents();
    auto *menu = w.findChild<QMenu *>(QStringLiteral("interfaceMenu"));
    QVERIFY(menu);
    QAction *customize = menuAction(menu, QStringLiteral("Personalizar…"));
    QVERIFY2(customize, "Personalizar… ausente do menu Interface");
    customize->trigger();
    QApplication::processEvents();
    QVERIFY2(w.findChild<QDialog *>(QStringLiteral("customizeDialog")),
             "Personalizar… não abriu o painel");
}

void MainWindowTest::customizeDialogPersistsManualAdjustments()
{
    TempConfig config;
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString home = QDir(root.path()).filePath(QStringLiteral("home"));
    QVERIFY(QDir().mkpath(home));

    {
        MainWindow w(home);
        w.show();
        QApplication::processEvents();
        QVERIFY(choosePreset(&w, QStringLiteral("Avançada")));
        auto *places = w.findChild<QListWidget *>(QStringLiteral("places"));
        QVERIFY(places);
        QListWidgetItem *entry = findPlace(places, QStringLiteral("Personalizar…"));
        QVERIFY2(entry && !entry->isHidden(), "Personalizar…” ausente na lateral");
        places->scrollToItem(entry);
        QApplication::processEvents();
        QTest::mouseClick(places->viewport(), Qt::LeftButton, Qt::KeyboardModifiers(),
                          places->visualItemRect(entry).center());
        QApplication::processEvents();

        auto *dialog = w.findChild<QDialog *>(QStringLiteral("customizeDialog"));
        QVERIFY2(dialog, "Personalizar… da lateral não abriu o painel");
        auto *density = dialog->findChild<QComboBox *>(QStringLiteral("customDensity"));
        auto *columns = dialog->findChild<QComboBox *>(QStringLiteral("customColumns"));
        auto *iconSize = dialog->findChild<QComboBox *>(QStringLiteral("customIconSize"));
        auto *preview = dialog->findChild<QCheckBox *>(QStringLiteral("customPreview"));
        auto *buttons = dialog->findChild<QDialogButtonBox *>();
        QVERIFY(density && columns && iconSize && preview && buttons);
        density->setCurrentIndex(0);
        columns->setCurrentIndex(2);
        iconSize->setCurrentIndex(0);
        preview->setChecked(true);
        QPushButton *ok = buttons->button(QDialogButtonBox::Ok);
        QVERIFY(ok);
        ok->click();
        QApplication::processEvents();

        const UiPreferences prefs = w.uiPreferences();
        QCOMPARE(prefs.preset, UiPreset::Advanced);
        QCOMPARE(prefs.density, 0);
        QCOMPARE(prefs.iconSize, 32);
        QCOMPARE(prefs.columns.size(), 3);
        QVERIFY(prefs.preview);

        QSettings settings(QStringLiteral("fm"), QStringLiteral("fm"));
        QCOMPARE(settings.value(QStringLiteral("ui/density")).toInt(), 0);
        QCOMPARE(settings.value(QStringLiteral("ui/iconSize")).toInt(), 32);
        QCOMPARE(settings.value(QStringLiteral("ui/columns")).toStringList().size(), 3);
        QVERIFY(settings.value(QStringLiteral("ui/preview")).toBool());
        w.close();
    }

    MainWindow restarted(home);
    UiPreferences expected = defaultUiPreferences(UiPreset::Advanced);
    expected.density = 0;
    expected.iconSize = 32;
    expected.columns = QStringList{QStringLiteral("0"), QStringLiteral("1"),
                                   QStringLiteral("2")};
    expected.preview = true;
    QVERIFY2(samePreferences(restarted.uiPreferences(), expected),
             "ajustes manuais não sobreviveram ao reinício");
}

void MainWindowTest::keyboardOpensMenusAt320InEveryPreset()
{
    TempConfig config;
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString home = QDir(root.path()).filePath(QStringLiteral("home"));
    QVERIFY(QDir().mkpath(home));

    MainWindow w(home);
    w.resize(320, 600);
    w.show();
    QApplication::processEvents();

    auto *tb = toolbar(&w);
    QVERIFY(tb);
    const QString nested = QDir(home).filePath(QStringLiteral("sub"));
    QVERIFY(QDir().mkpath(nested));
    auto *initialTab = w.findChild<FileTab *>();
    QVERIFY(initialTab);
    QVERIFY(initialTab->goTo(nested));
    QVERIFY(initialTab->goTo(home));
    initialTab->back(); // deixa Voltar e Avançar habilitados para a cadeia de foco
    QVERIFY(initialTab->canGoBack());
    QVERIFY(initialTab->canGoForward());
    QApplication::processEvents();
    auto *files = w.findChild<QToolButton *>(QStringLiteral("filesMenuButton"));
    auto *view = w.findChild<QToolButton *>(QStringLiteral("viewMenuButton"));
    QVERIFY(files && view);
    QCOMPARE(files->popupMode(), QToolButton::InstantPopup);
    QCOMPARE(view->popupMode(), QToolButton::InstantPopup);
    QVERIFY(files->menu() && view->menu());
    QCOMPARE(files->menu()->objectName(), QStringLiteral("filesMenu"));
    QCOMPARE(view->menu()->objectName(), QStringLiteral("viewMenu"));

    auto focusName = [](QWidget *w) -> QString {
        if (!w)
            return QStringLiteral("(none)");
        if (auto *button = qobject_cast<QToolButton *>(w)) {
            const QString name = button->objectName();
            if (name == QLatin1String("filesMenuButton"))
                return QStringLiteral("files");
            if (name == QLatin1String("viewMenuButton"))
                return QStringLiteral("view");
            if (name == QLatin1String("breadcrumbOverflow"))
                return QStringLiteral("overflow");
            if (name == QLatin1String("breadcrumbButton"))
                return QStringLiteral("crumb");
            return button->text();
        }
        if (qobject_cast<QListWidget *>(w))
            return QStringLiteral("places");
        if (qobject_cast<QTreeView *>(w))
            return QStringLiteral("tree");
        return QString::fromLatin1(w->metaObject()->className());
    };

    // Cadeia natural da toolbar/central widget: Arquivos entra uma vez, Voltar,
    // Avançar e Subir vêm antes do breadcrumb e Visualização antes da lista.
    const QStringList expected{"files", "Voltar", "Avançar", "Subir", "overflow",
                               "view",  "places", "tree",   "crumb"};

    for (const QString &preset : { QStringLiteral("Compacta"), QStringLiteral("Equilibrada"),
                                   QStringLiteral("Avançada") }) {
        QVERIFY(choosePreset(&w, preset));
        w.resize(320, 600);
        w.activateWindow();
        QApplication::processEvents();

        // Âncora única da asserção: o restante da cadeia é percorrado só com Tab.
        files->setFocus();
        QCOMPARE(focusName(QApplication::focusWidget()), QStringLiteral("files"));
        QStringList seen{focusName(QApplication::focusWidget())};
        for (int i = 0; i < expected.size() - 1; ++i) {
            QWidget *focus = QApplication::focusWidget();
            QTest::keyClick(focus ? focus : static_cast<QWidget *>(&w), Qt::Key_Tab);
            seen << focusName(QApplication::focusWidget());
        }
        QCOMPARE(seen, expected);
        QCOMPARE(seen.filter(QStringLiteral("files")).size(), 1);
        // O último breadcrumb é o fim da cadeia: Tab não reinicia em Arquivos.
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
        QCOMPARE(focusName(QApplication::focusWidget()), QStringLiteral("crumb"));

        auto reachButton = [&](QToolButton *button) {
            for (int i = 0; i < 12; ++i) {
                if (QApplication::focusWidget() == button)
                    return true;
                QWidget *focus = QApplication::focusWidget();
                QTest::keyClick(focus ? focus : static_cast<QWidget *>(&w), Qt::Key_Tab,
                                Qt::ShiftModifier);
            }
            return QApplication::focusWidget() == button;
        };

        for (QToolButton *button : {files, view}) {
            QVERIFY2(button->isVisible(), "Botão de menu desapareceu a 320 px");
            QVERIFY2(reachButton(button),
                     qPrintable(QStringLiteral("%1: Tab não alcançou %2")
                                    .arg(preset, button->objectName())));
            QVERIFY(button->hasFocus());

            for (Qt::Key key : {Qt::Key_Space, Qt::Key_Return, Qt::Key_Enter}) {
                QVERIFY2(reachButton(button), "Foco saiu do botão antes da tecla");
                QTest::keyClick(button, key);
                QVERIFY2(button->menu()->isVisible(),
                         qPrintable(QStringLiteral("%1: %2 não abriu com a tecla %3")
                                        .arg(preset, button->objectName(), QString::number(int(key)))));
                QWidget *popup = qApp->activePopupWidget();
                QTest::keyClick(popup ? popup : static_cast<QWidget *>(button), Qt::Key_Escape);
                QVERIFY2(!button->menu()->isVisible(), "Esc não fechou o menu");
                closePopups();
            }
        }
    }
}

void MainWindowTest::tabsMenuUsesFullPathForDuplicates()
{
    TempConfig config;
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString firstDir =
        QFileInfo(QDir(root.path()).filePath(QStringLiteral("a/proj"))).absoluteFilePath();
    const QString secondDir =
        QFileInfo(QDir(root.path()).filePath(QStringLiteral("b/proj"))).absoluteFilePath();
    const QString nested = QDir(firstDir).filePath(QStringLiteral("sub"));
    QVERIFY(QDir().mkpath(nested));
    QVERIFY(QDir().mkpath(secondDir));
    QFile file(QDir(firstDir).filePath(QStringLiteral("alvo.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("alvo\n");
    file.close();

    MainWindow w(firstDir);
    w.show();
    QApplication::processEvents();
    auto *tabs = w.findChild<QTabWidget *>();
    QVERIFY(tabs);

    auto *first = static_cast<FileTab *>(tabs->widget(0));
    QVERIFY(first);
    // Histórico primeiro; a seleção de arquivo vem depois, antes de trocar de aba.
    QVERIFY(first->goTo(nested));
    QVERIFY(first->goTo(firstDir)); // volta navegando: histórico fica com Voltar habilitado
    QVERIFY(first->canGoBack());
    QCOMPARE(first->currentPath(), firstDir);
    auto *tree = first->findChild<QTreeView *>();
    QVERIFY(tree);
    const QModelIndex target = findRowNamed(tree, QStringLiteral("alvo.txt"));
    QVERIFY(target.isValid());
    tree->selectionModel()->setCurrentIndex(target, QItemSelectionModel::ClearAndSelect
                                                  | QItemSelectionModel::Rows);
    QCOMPARE(tree->selectionModel()->currentIndex().data().toString(), QStringLiteral("alvo.txt"));

    QAction *newTab = findAction(&w, QStringLiteral("Nova aba"));
    QVERIFY(newTab);
    newTab->trigger();
    QApplication::processEvents();
    QCOMPARE(tabs->count(), 2);
    auto *second = static_cast<FileTab *>(tabs->widget(1));
    QVERIFY(second);
    QVERIFY(second->goTo(secondDir));
    QApplication::processEvents();
    QCOMPARE(tabs->tabText(0), tabs->tabText(1));

    tabs->setCurrentIndex(0);
    QApplication::processEvents();
    auto *menu = w.findChild<QMenu *>(QStringLiteral("showAllTabsMenu"));
    QVERIFY(menu);
    auto openTabsMenu = [&]() {
        menu->popup(QPoint(10, 10));
        QApplication::processEvents();
        return menu->actions();
    };

    QList<QAction *> items = openTabsMenu();
    QCOMPARE(items.size(), 2);
    QCOMPARE(items.at(0)->text(), firstDir);
    QCOMPARE(items.at(1)->text(), secondDir);
    QVERIFY(items.at(0)->isCheckable() && items.at(1)->isCheckable());
    QVERIFY(items.at(0)->isChecked());
    QVERIFY(!items.at(1)->isChecked());

    QTest::keyClick(menu, Qt::Key_Down);
    QCOMPARE(menu->activeAction(), items.at(0));
    QTest::keyClick(menu, Qt::Key_Down);
    QCOMPARE(menu->activeAction(), items.at(1));
    QTest::keyClick(menu, Qt::Key_Up);
    QCOMPARE(menu->activeAction(), items.at(0));
    QTest::keyClick(menu, Qt::Key_Down);
    QTest::keyClick(menu, Qt::Key_Return);
    QApplication::processEvents();
    QCOMPARE(tabs->currentIndex(), 1);
    closePopups();

    // A aba 1 guarda caminho, seleção e histórico enquanto está inativa.
    QCOMPARE(first->currentPath(), firstDir);
    QVERIFY(first->canGoBack());
    QCOMPARE(tree->selectionModel()->currentIndex().data().toString(), QStringLiteral("alvo.txt"));

    items = openTabsMenu();
    QCOMPARE(items.size(), 2);
    QVERIFY(!items.at(0)->isChecked());
    QVERIFY(items.at(1)->isChecked());
    QTest::keyClick(menu, Qt::Key_Down);
    QTest::keyClick(menu, Qt::Key_Escape);
    QApplication::processEvents();
    QVERIFY(!menu->isVisible());
    QCOMPARE(tabs->currentIndex(), 1);
    closePopups();

    items = openTabsMenu();
    QCOMPARE(items.size(), 2);
    QTest::keyClick(menu, Qt::Key_Down);
    QTest::keyClick(menu, Qt::Key_Return);
    QApplication::processEvents();
    closePopups();

    QCOMPARE(tabs->currentIndex(), 0);
    QCOMPARE(first->currentPath(), firstDir);
    QCOMPARE(tree->selectionModel()->currentIndex().data().toString(), QStringLiteral("alvo.txt"));
    QVERIFY(findAction(&w, QStringLiteral("Voltar"))->isEnabled());
}

void MainWindowTest::localDialogsHaveReadableText()
{
    TempConfig config;
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString home = QDir(root.path()).filePath(QStringLiteral("home"));
    QVERIFY(QDir().mkpath(home));

    MainWindow w(home);
    w.show();
    QApplication::processEvents();

    auto *shortcuts = w.findChild<QAction *>(QStringLiteral("shortcutsAction"));
    auto *help = w.findChild<QAction *>(QStringLiteral("helpAction"));
    auto *about = w.findChild<QAction *>(QStringLiteral("aboutAction"));
    QVERIFY(shortcuts && help && about);

    auto dialogText = [](QAction *action) {
        QString text;
        QTimer::singleShot(0, [&] {
            const auto topLevel = qApp->topLevelWidgets();
            for (QWidget *widget : topLevel) {
                if (auto *box = qobject_cast<QMessageBox *>(widget)) {
                    text = box->text();
                    box->done(QMessageBox::Ok);
                    return;
                }
            }
        });
        action->trigger();
        QApplication::processEvents();
        closePopups();
        return text;
    };

    const QString shortcutsText = dialogText(shortcuts);
    QVERIFY2(shortcutsText.contains(QStringLiteral("Editar caminho")), "Ctrl+L ausente nos atalhos");
    QVERIFY2(shortcutsText.contains(QStringLiteral("Ctrl+L")), "Ctrl+L ausente nos atalhos");
    QVERIFY2(shortcutsText.contains(QStringLiteral("Fechar aba")), "Ctrl+W ausente nos atalhos");
    QVERIFY2(shortcutsText.contains(QStringLiteral("Ctrl+W")), "Ctrl+W ausente nos atalhos");
    QVERIFY2(!dialogText(help).isEmpty(), "Ajuda sem texto");
    QVERIFY2(!dialogText(about).isEmpty(), "Sobre o fm sem texto");
}

QTEST_MAIN(MainWindowTest)
#include "mainwindow_test.moc"
