#include "MainWindow.h"
#include "ThemeManager.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QSettings>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("fm"));
    QApplication::setDesktopFileName(QStringLiteral("fm"));
    // Fusion respeita 100% a QPalette, que é o que o tema da shell altera.
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Gerenciador de arquivos"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("pasta"), QStringLiteral("Pasta inicial"), QStringLiteral("[pasta]"));
    QCommandLineOption themeOpt({QStringLiteral("t"), QStringLiteral("theme-file")},
                                QStringLiteral("JSON com as cores da shell"), QStringLiteral("arquivo"));
    parser.addOption(themeOpt);
    parser.process(app);

    // Ordem: --theme-file > $FM_THEME_FILE > QSettings > caminho padrão do iNiR (confira o seu!)
    QString themeFile = parser.value(themeOpt);
    if (themeFile.isEmpty())
        themeFile = qEnvironmentVariable("FM_THEME_FILE");
    if (themeFile.isEmpty())
        themeFile = QSettings(QStringLiteral("fm"), QStringLiteral("fm")).value(QStringLiteral("theme/file")).toString();
    if (themeFile.isEmpty())
        themeFile = QDir::homePath() + QStringLiteral("/.local/state/quickshell/user/generated/colors.json");

    ThemeManager theme;
    theme.setThemeFile(themeFile);

    const QStringList args = parser.positionalArguments();
    MainWindow w(args.isEmpty() ? QDir::homePath() : args.first());
    w.show();
    return app.exec();
}
