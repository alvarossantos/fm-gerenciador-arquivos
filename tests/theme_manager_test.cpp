#include <QtTest>

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QPalette>
#include <QSaveFile>
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
    void atomicReplaceAppliesNewPalette();

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

QTEST_MAIN(ThemeManagerTest)
#include "theme_manager_test.moc"
