#include <QtTest>

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QTabWidget>
#include <QToolBox>

#include "PropertiesDialog.h"
#include "UiPreferences.h"

class PropertiesDialogTest : public QObject
{
    Q_OBJECT

private slots:
    void layoutByPreset();
    void fileFields();
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

QTEST_MAIN(PropertiesDialogTest)
#include "properties_dialog_test.moc"
