#include <QtTest>

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QPalette>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "ThemeManager.h"

class ThemeManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void validJsonAppliesPalette();
    void invalidJsonKeepsLastPalette();
    void invalidReloadDoesNotEmitDefault();
    void missingFileKeepsLastPalette();
    void atomicReplaceAppliesNewPalette();
    void missingFileAtStartupUsesDefault();
    void camelCasePaletteWorks();

private:
    void writeDirect(const QString &path, const QString &json);
    void writeAtomic(const QString &path, const QString &json);

    QPalette m_basePalette;
    QColor m_defaultWindow;
};

void ThemeManagerTest::initTestCase()
{
    m_basePalette = qApp->palette();
    m_defaultWindow = m_basePalette.color(QPalette::Window);
}

void ThemeManagerTest::init()
{
    qApp->setPalette(m_basePalette);
    qApp->setStyleSheet(QString());
}

void ThemeManagerTest::validJsonAppliesPalette()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("colors.json"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("{\"surface\":\"#112233\",\"primary\":\"#445566\"}");
    f.close();

    ThemeManager theme;
    theme.setThemeFile(path);

    QCOMPARE(qApp->palette().color(QPalette::Window), QColor(QStringLiteral("#112233")));
}

void ThemeManagerTest::writeDirect(const QString &path, const QString &json)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(json.toUtf8());
}

void ThemeManagerTest::writeAtomic(const QString &path, const QString &json)
{
    QSaveFile f(path);
    f.setDirectWriteFallback(false);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(json.toUtf8());
    QVERIFY(f.commit());
}

void ThemeManagerTest::invalidJsonKeepsLastPalette()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("colors.json"));
    writeDirect(path, QStringLiteral("{\"surface\":\"#112233\",\"primary\":\"#445566\"}"));

    ThemeManager theme;
    theme.setThemeFile(path);
    QCOMPARE(qApp->palette().color(QPalette::Window), QColor(QStringLiteral("#112233")));

    writeDirect(path, QStringLiteral("{"));
    QTest::qWait(400);
    QCOMPARE(qApp->palette().color(QPalette::Window), QColor(QStringLiteral("#112233")));
}

void ThemeManagerTest::invalidReloadDoesNotEmitDefault()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("colors.json"));
    writeDirect(path, QStringLiteral("{\"surface\":\"#112233\",\"primary\":\"#445566\"}"));

    ThemeManager theme;
    theme.setThemeFile(path);
    QCOMPARE(qApp->palette().color(QPalette::Window), QColor(QStringLiteral("#112233")));

    QSignalSpy applied(&theme, &ThemeManager::themeApplied);
    writeDirect(path, QStringLiteral("{"));
    // Invoca o reload diretamente: prova que o ramo inválido foi executado,
    // independentemente do debounce do QFileSystemWatcher.
    QVERIFY(QMetaObject::invokeMethod(&theme, "reload"));
    QCOMPARE(applied.count(), 0);
    QCOMPARE(qApp->palette().color(QPalette::Window), QColor(QStringLiteral("#112233")));
}

void ThemeManagerTest::missingFileKeepsLastPalette()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("colors.json"));
    writeDirect(path, QStringLiteral("{\"surface\":\"#112233\",\"primary\":\"#445566\"}"));

    ThemeManager theme;
    theme.setThemeFile(path);
    QCOMPARE(qApp->palette().color(QPalette::Window), QColor(QStringLiteral("#112233")));

    QVERIFY(QFile::remove(path));
    QTest::qWait(400);
    QCOMPARE(qApp->palette().color(QPalette::Window), QColor(QStringLiteral("#112233")));

    // O watcher do diretório deve reaplicar quando o arquivo voltar a existir.
    writeDirect(path, QStringLiteral("{\"surface\":\"#abcdef\",\"primary\":\"#445566\"}"));
    QTRY_COMPARE_WITH_TIMEOUT(qApp->palette().color(QPalette::Window),
                              QColor(QStringLiteral("#abcdef")), 2000);
}

void ThemeManagerTest::atomicReplaceAppliesNewPalette()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("colors.json"));
    writeDirect(path, QStringLiteral("{\"surface\":\"#112233\",\"primary\":\"#445566\"}"));

    ThemeManager theme;
    theme.setThemeFile(path);
    QCOMPARE(qApp->palette().color(QPalette::Window), QColor(QStringLiteral("#112233")));

    writeAtomic(path, QStringLiteral("{\"surface\":\"#abcdef\",\"primary\":\"#445566\"}"));
    QTRY_COMPARE_WITH_TIMEOUT(qApp->palette().color(QPalette::Window),
                              QColor(QStringLiteral("#abcdef")), 2000);

    writeAtomic(path, QStringLiteral("{\"surface\":\"#123456\",\"primary\":\"#445566\"}"));
    QTRY_COMPARE_WITH_TIMEOUT(qApp->palette().color(QPalette::Window),
                              QColor(QStringLiteral("#123456")), 2000);
}

void ThemeManagerTest::missingFileAtStartupUsesDefault()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ThemeManager theme;
    theme.setThemeFile(dir.filePath(QStringLiteral("nao-existe.json")));
    QCOMPARE(qApp->palette().color(QPalette::Window), m_defaultWindow);
}

void ThemeManagerTest::camelCasePaletteWorks()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ryoku-colors.json"));
    writeDirect(path, QStringLiteral("{\"surface\":\"#123456\",\"onSurface\":\"#654321\","
                                     "\"primaryContainer\":\"#abcdef\"}"));

    ThemeManager theme;
    theme.setThemeFile(path);

    QCOMPARE(qApp->palette().color(QPalette::Window), QColor(QStringLiteral("#123456")));
    QVERIFY(qApp->styleSheet().contains(QStringLiteral("#abcdef")));
}

QTEST_MAIN(ThemeManagerTest)
#include "theme_manager_test.moc"
