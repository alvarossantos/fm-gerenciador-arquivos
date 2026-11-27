#include "MainWindow.h"
#include "ThemeManager.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QSettings>
#include <QStyleFactory>

#include <cstdio>

int main(int argc, char *argv[])
{
    QElapsedTimer elapsed;
    elapsed.start();

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
    QCommandLineOption benchOpt(QStringLiteral("bench"),
                                QStringLiteral("Mede até a primeira pintura e sai"));
    parser.addOption(benchOpt);
    parser.process(app);

    // Ordem: --theme-file > $FM_THEME_FILE > QSettings > paleta padrão de apps Qt no iNiR
    QString themeFile = parser.value(themeOpt);
    if (themeFile.isEmpty())
        themeFile = qEnvironmentVariable("FM_THEME_FILE");
    if (themeFile.isEmpty())
        themeFile = QSettings(QStringLiteral("fm"), QStringLiteral("fm")).value(QStringLiteral("theme/file")).toString();
    if (themeFile.isEmpty())
        themeFile = QDir::homePath() + QStringLiteral("/.local/state/quickshell/user/generated/app-palette.json");

    ThemeManager theme;
    theme.setThemeFile(themeFile);

    const QStringList args = parser.positionalArguments();
    MainWindow w(args.isEmpty() ? QDir::homePath() : args.first());
    w.setBenchMode(parser.isSet(benchOpt));
    QObject::connect(&w, &MainWindow::firstPaint, &w, [&elapsed] {
        std::fprintf(stderr, "tempo até a primeira pintura: %lld ms\n",
                     static_cast<long long>(elapsed.elapsed()));
    });
    w.show();
    return app.exec();
}
