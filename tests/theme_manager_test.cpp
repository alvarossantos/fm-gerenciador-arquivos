#include <QtTest>

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QPalette>
#include <QTemporaryDir>

#include "ThemeManager.h"

class ThemeManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void validJsonAppliesPalette();

private:
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

QTEST_MAIN(ThemeManagerTest)
#include "theme_manager_test.moc"
