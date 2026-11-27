#include <QtTest>

#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QToolBox>

#include <atomic>
#include <condition_variable>
#include <mutex>

#include "PropertiesDialog.h"
#include "UiPreferences.h"

class PropertiesDialogTest : public QObject
{
    Q_OBJECT

private slots:
    void layoutByPreset();
    void fileFields();
    void closeDuringScanDoesNotBlockAndReopenWorks();
    void scanCountsHiddenAndDoesNotFollowSymlinks();
};

void PropertiesDialogTest::layoutByPreset()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString file = QDir(dir.path()).filePath(QStringLiteral("alvo.txt"));
    QFile f(file);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QVERIFY(f.write("conteudo\n") > 0);
    f.close();

    PropertiesDialog compact(file, defaultUiPreferences(UiPreset::Compact));
    auto *toolBox = compact.findChild<QToolBox *>(QStringLiteral("propertiesSections"));
    QVERIFY2(toolBox, "Compacta deve usar QToolBox");
    QVERIFY(toolBox->count() >= 3);
    QVERIFY(!compact.findChild<QTabWidget *>());

    PropertiesDialog balanced(file, defaultUiPreferences(UiPreset::Balanced));
    auto *tabs = balanced.findChild<QTabWidget *>(QStringLiteral("propertiesSections"));
    QVERIFY2(tabs, "Equilibrada deve usar QTabWidget");
    QStringList tabLabels;
    for (int i = 0; i < tabs->count(); ++i)
        tabLabels << tabs->tabText(i);
    QVERIFY(tabLabels.contains(QStringLiteral("Geral")));
    QVERIFY(tabLabels.contains(QStringLiteral("Datas")));
    QVERIFY(tabLabels.contains(QStringLiteral("Permissões")));
    QVERIFY(!balanced.findChild<QToolBox *>());

    PropertiesDialog advanced(file, defaultUiPreferences(UiPreset::Advanced));
    QVERIFY(!advanced.findChild<QToolBox *>());
    QVERIFY(!advanced.findChild<QTabWidget *>());
    auto *sections = advanced.findChild<QWidget *>(QStringLiteral("propertiesSections"));
    QVERIFY(sections);
    advanced.show();
    QVERIFY(QTest::qWaitForWindowExposed(&advanced));
    QVERIFY(advanced.findChild<QWidget *>(QStringLiteral("propertiesGeneral"))->isVisible());
    QVERIFY(advanced.findChild<QLabel *>(QStringLiteral("propertiesSize"))->isVisible());
}

void PropertiesDialogTest::fileFields()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString file = QDir(dir.path()).filePath(QStringLiteral("alvo.txt"));
    QFile f(file);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QVERIFY(f.write("abc") > 0);
    f.close();

    PropertiesDialog dlg(file, defaultUiPreferences(UiPreset::Balanced));
    QCOMPARE(dlg.path(), file);
    auto *size = dlg.findChild<QLabel *>(QStringLiteral("propertiesSize"));
    QVERIFY(size);
    QVERIFY2(size->text().contains(QLocale().formattedDataSize(3)), qPrintable(size->text()));

    auto *parentButton = dlg.findChild<QPushButton *>(QStringLiteral("openParentButton"));
    QVERIFY(parentButton && parentButton->isEnabled());
    auto *copyButton = dlg.findChild<QPushButton *>(QStringLiteral("copyPathButton"));
    QVERIFY(copyButton);
    QGuiApplication::clipboard()->clear();
    copyButton->click();
    QCOMPARE(QGuiApplication::clipboard()->text(), file);
}

void PropertiesDialogTest::closeDuringScanDoesNotBlockAndReopenWorks()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = QDir(dir.path()).filePath(QStringLiteral("scan"));
    QVERIFY(QDir().mkpath(root));
    for (int i = 0; i < 200; ++i) {
        QFile f(QDir(root).filePath(QString::number(i)));
        QVERIFY(f.open(QIODevice::WriteOnly));
        QVERIFY(f.write("abcd") == 4);
    }
    const QString other = QDir(dir.path()).filePath(QStringLiteral("outro.txt"));
    QFile f(other);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QVERIFY(f.write("1234567") == 7);
    f.close();

    std::atomic_int calls{ 0 };
    std::mutex mutex;
    std::condition_variable cv;
    bool release = false;
    QEventLoop hookLoop;
    setScanItemHookForTests([&] {
        if (calls.fetch_add(1) == 0) {
            QMetaObject::invokeMethod(&hookLoop, [&] { hookLoop.quit(); }, Qt::QueuedConnection);
        }
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return release; });
    });
    const auto reset = qScopeGuard([] { setScanItemHookForTests({}); });

    {
        PropertiesDialog dlg(root, defaultUiPreferences(UiPreset::Advanced));
        hookLoop.exec();
        QElapsedTimer timer;
        timer.start();
        dlg.close();
        QVERIFY2(timer.elapsed() < 200, qPrintable(QStringLiteral("close() levou %1 ms").arg(timer.elapsed())));
        {
            std::lock_guard<std::mutex> lock(mutex);
            release = true;
        }
        cv.notify_all();
        QTRY_VERIFY_WITH_TIMEOUT(PropertiesDialog::activeScansForTests() == 0, 5000);
    }

    PropertiesDialog again(other, defaultUiPreferences(UiPreset::Balanced));
    QSignalSpy spy(&again, &PropertiesDialog::scanFinished);
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0, 5000);
    QCOMPARE(spy.at(0).at(0).toString(), QDir::cleanPath(other));
    QCOMPARE(spy.at(0).at(1).toLongLong(), 7LL);
    QCOMPARE(spy.at(0).at(2).toInt(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(PropertiesDialog::activeScansForTests() == 0, 5000);
}

void PropertiesDialogTest::scanCountsHiddenAndDoesNotFollowSymlinks()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = QDir(dir.path()).filePath(QStringLiteral("arvore"));
    QVERIFY(QDir().mkpath(QDir(root).filePath(QStringLiteral("sub"))));
    const QString outside = QDir(dir.path()).filePath(QStringLiteral("fora"));
    QVERIFY(QDir().mkpath(outside));

    const auto write = [](const QString &path, const char *data) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        return f.write(data) == static_cast<qint64>(qstrlen(data));
    };
    QVERIFY(write(QDir(root).filePath(QStringLiteral("visivel.txt")), "abc"));
    QVERIFY(write(QDir(root).filePath(QStringLiteral(".oculto")), "12345"));
    QVERIFY(write(QDir(QDir(root).filePath(QStringLiteral("sub"))).filePath(QStringLiteral("filho.txt")), "xy"));
    QVERIFY(write(QDir(outside).filePath(QStringLiteral("ignore.txt")), "grande"));
    QVERIFY(QFile::link(outside, QDir(root).filePath(QStringLiteral("link"))));

    PropertiesDialog dlg(root, defaultUiPreferences(UiPreset::Balanced));
    QSignalSpy spy(&dlg, &PropertiesDialog::scanFinished);
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0, 5000);
    QCOMPARE(spy.at(0).at(0).toString(), QDir::cleanPath(root));
    QCOMPARE(spy.at(0).at(1).toLongLong(), 10LL);
    QCOMPARE(spy.at(0).at(2).toInt(), 1);
    QVERIFY(spy.at(0).at(3).toStringList().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(PropertiesDialog::activeScansForTests() == 0, 5000);
}

QTEST_MAIN(PropertiesDialogTest)
#include "properties_dialog_test.moc"
