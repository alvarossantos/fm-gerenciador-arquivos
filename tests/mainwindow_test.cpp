#include <QtTest>

#include <QDir>
#include <QListWidget>
#include <QScopeGuard>
#include <QTemporaryDir>

#include "FileTab.h"

namespace {

QString selectedPlacePath(QWidget *tab)
{
    auto *places = tab->findChild<QListWidget *>(QStringLiteral("places"));
    return places && places->currentItem()
               ? places->currentItem()->data(Qt::UserRole).toString()
               : QString();
}

} // namespace

class MainWindowTest : public QObject
{
    Q_OBJECT

private slots:
    void placesHighlightFollowsGoToBackForward();
    void placesHighlightUsesAncestorRules();
    void placesHighlightRejectsPrefixCollision();
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

QTEST_MAIN(MainWindowTest)
#include "mainwindow_test.moc"
