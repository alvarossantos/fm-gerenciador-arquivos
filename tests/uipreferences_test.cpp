#include <QtTest>

#include <functional>

#include <QScopeGuard>
#include <QSettings>
#include <QTemporaryDir>

#include "UiPreferences.h"

class UiPreferencesTest : public QObject {
    Q_OBJECT
private slots:
    void defaultsMatchMockups();
    void missingKeyUsesSelectedPreset();
    void invalidValuesFallBackToPreset();
    void normalizeColumnsSanitizes();
    void roundTrip();
};

namespace {

struct TempConfig {
    QTemporaryDir dir;
    QByteArray previous = qgetenv("XDG_CONFIG_HOME");
    QScopeGuard<std::function<void()>> restore;

    TempConfig()
        : restore([this] { qputenv("XDG_CONFIG_HOME", previous); })
    {
        QVERIFY2(dir.isValid(), "diretório temporário de config inválido");
        qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
    }
};

} // namespace

void UiPreferencesTest::defaultsMatchMockups()
{
    const UiPreferences compact = defaultUiPreferences(UiPreset::Compact);
    QCOMPARE(compact.density, 0);
    QCOMPARE(compact.iconSize, 32);
    QCOMPARE(compact.columns, QStringList{QStringLiteral("0")});
    QCOMPARE(compact.preview, false);

    const QStringList allColumns{QStringLiteral("0"), QStringLiteral("1"),
                                 QStringLiteral("2"), QStringLiteral("3")};
    const UiPreferences balanced = defaultUiPreferences(UiPreset::Balanced);
    QCOMPARE(balanced.density, 1);
    QCOMPARE(balanced.iconSize, 48);
    QCOMPARE(balanced.columns, allColumns);
    QCOMPARE(balanced.preview, false);

    const UiPreferences advanced = defaultUiPreferences(UiPreset::Advanced);
    QCOMPARE(advanced.density, 2);
    QCOMPARE(advanced.iconSize, 64);
    QCOMPARE(advanced.columns, allColumns);
    QCOMPARE(advanced.preview, true);
}

void UiPreferencesTest::missingKeyUsesSelectedPreset()
{
    TempConfig config;
    QSettings settings(QStringLiteral("fm"), QStringLiteral("fm"));

    settings.setValue(QStringLiteral("ui/preset"), QStringLiteral("compact"));
    QVERIFY(!settings.contains(QStringLiteral("ui/iconSize")));
    QCOMPARE(loadUiPreferences(settings).iconSize, 32);

    settings.setValue(QStringLiteral("ui/preset"), QStringLiteral("advanced"));
    QVERIFY(!settings.contains(QStringLiteral("ui/preview")));
    QVERIFY(loadUiPreferences(settings).preview);
}

void UiPreferencesTest::invalidValuesFallBackToPreset()
{
    TempConfig config;
    QSettings settings(QStringLiteral("fm"), QStringLiteral("fm"));
    settings.setValue(QStringLiteral("ui/preset"), QStringLiteral("balanced"));
    settings.setValue(QStringLiteral("ui/density"), 99);
    settings.setValue(QStringLiteral("ui/columns"),
                      QStringList{QStringLiteral("0"), QStringLiteral("9")});

    const UiPreferences loaded = loadUiPreferences(settings);
    QCOMPARE(loaded.density, 1);
    QCOMPARE(loaded.columns, QStringList{QStringLiteral("0")});
}

void UiPreferencesTest::normalizeColumnsSanitizes()
{
    const QStringList onlyName{QStringLiteral("0")};
    const QStringList nameSizeType{QStringLiteral("0"), QStringLiteral("1"),
                                   QStringLiteral("2")};

    QCOMPARE(normalizeColumns({QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("9")},
                              UiPreset::Balanced),
             onlyName);
    QCOMPARE(normalizeColumns({QStringLiteral("1"), QStringLiteral("2")}, UiPreset::Balanced),
             nameSizeType);
    QCOMPARE(normalizeColumns({}, UiPreset::Compact),
             defaultUiPreferences(UiPreset::Compact).columns);
}

void UiPreferencesTest::roundTrip()
{
    TempConfig config;
    UiPreferences prefs = defaultUiPreferences(UiPreset::Advanced);
    prefs.density = 0;
    prefs.iconSize = 32;
    prefs.columns = QStringList{QStringLiteral("0"), QStringLiteral("2")};
    prefs.preview = false;

    QSettings settings(QStringLiteral("fm"), QStringLiteral("fm"));
    saveUiPreferences(settings, prefs);
    const UiPreferences loaded = loadUiPreferences(settings);

    QCOMPARE(loaded.preset, UiPreset::Advanced);
    QCOMPARE(loaded.density, 0);
    QCOMPARE(loaded.iconSize, 32);
    QCOMPARE(loaded.columns, prefs.columns);
    QCOMPARE(loaded.preview, false);
}

QTEST_MAIN(UiPreferencesTest)
#include "uipreferences_test.moc"
