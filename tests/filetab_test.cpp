#include <QtTest>

#include <functional>
#include <memory>

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileSystemModel>
#include <QHash>
#include <QHeaderView>
#include <QImage>
#include <QItemSelectionModel>
#include <QElapsedTimer>
#include <QInputDialog>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QScopeGuard>
#include <QMenu>
#include <QPointer>
#include <QSignalSpy>
#include <QStyleOptionViewItem>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeView>

#include "FileTab.h"
#include "ThumbnailProxy.h"
#include "UiPreferences.h"

namespace {

// Restaura a variável ao estado exato anterior, inclusive quando não existia.
auto setTestEnv(const char *name, const QByteArray &value)
{
    const QByteArray old = qgetenv(name);
    const bool wasSet = !old.isNull();
    qputenv(name, value);
    return qScopeGuard([name, old, wasSet] {
        if (wasSet)
            qputenv(name, old);
        else
            qunsetenv(name);
    });
}

QAction *actionNamed(QMenu *menu, const QString &text)
{
    if (!menu)
        return nullptr;
    for (QAction *a : menu->actions())
        if (a->text() == text)
            return a;
    return nullptr;
}

void acceptName(const QString &name)
{
    QInputDialog *dlg = nullptr;
    for (QWidget *w : qApp->topLevelWidgets())
        if ((dlg = qobject_cast<QInputDialog *>(w)))
            break;
    if (!dlg) {
        qWarning("QInputDialog de nome não encontrado");
        return;
    }
    dlg->setTextValue(name);
    dlg->accept();
}

void acceptNewFileName()
{
    acceptName(QStringLiteral("manual-test.txt"));
}

// Rótulos visíveis do menu, com "---" para separadores e caminho "sub > ação"
// para ações dentro de submenus.
QStringList collectMenuLabels(QMenu *menu, const QString &prefix = QString())
{
    QStringList out;
    if (!menu)
        return out;
    for (QAction *a : menu->actions()) {
        if (a->isSeparator()) {
            out << QStringLiteral("---");
            continue;
        }
        const QString label = prefix.isEmpty() ? a->text() : prefix + QStringLiteral(" > ") + a->text();
        out << label;
        if (a->menu())
            out << collectMenuLabels(a->menu(), label);
    }
    return out;
}

// Rótulos diretos do menu, sem separadores e sem descer em submenus.
QStringList directMenuLabels(QMenu *menu)
{
    QStringList out;
    if (!menu)
        return out;
    for (QAction *a : menu->actions())
        if (!a->isSeparator())
            out << a->text();
    return out;
}

QByteArray fileContents(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.readAll();
}

void closePopups()
{
    while (QWidget *p = qApp->activePopupWidget())
        p->close();
}

// Percorre o mesmo QMenu que o usuário vê, incluindo submenus.
void runMenuActionIn(QMenu *menu, const QStringList &labels,
                     const std::function<void()> &before, const std::function<void()> &after)
{
    if (!menu || labels.isEmpty()) {
        qWarning("popup ou ação do menu ausente: %s", qPrintable(labels.join(QLatin1String(" > "))));
        closePopups();
        return;
    }
    QAction *a = actionNamed(menu, labels.first());
    if (!a) {
        qWarning("ação do menu não encontrada: %s", qPrintable(labels.join(QLatin1String(" > "))));
        closePopups();
        return;
    }
    if (labels.size() > 1) {
        QMenu *sub = a->menu();
        QTimer::singleShot(0, [sub, labels, before, after] {
            runMenuActionIn(sub, labels.mid(1), before, after);
        });
        a->hover();
        return;
    }
    if (before)
        before();
    if (after)
        QTimer::singleShot(0, after);
    a->trigger();
    closePopups();
}

void runMenuAction(const QStringList &labels, const std::function<void()> &before = {},
                   const std::function<void()> &after = {})
{
    QTimer::singleShot(0, [labels, before, after] {
        runMenuActionIn(qobject_cast<QMenu *>(qApp->activePopupWidget()), labels, before, after);
    });
}

void openContextMenu(QAbstractItemView *view, const QModelIndex &index = QModelIndex(), int column = 0)
{
    // QWidget de viewport recebe o QContextMenuEvent; o QTreeView emite customContextMenuRequested.
    const QPoint vp = index.isValid() ? view->visualRect(index.siblingAtColumn(column)).center() : QPoint(5, 400);
    QContextMenuEvent ev(QContextMenuEvent::Mouse, vp, view->viewport()->mapToGlobal(vp));
    QApplication::sendEvent(view->viewport(), &ev);
}


void installFakeBsdtar(const QString &dir, const QString &mode)
{
    QVERIFY(QDir().mkpath(dir));
    QFile f(QDir(dir).filePath(QStringLiteral("bsdtar")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    const QString script = QStringLiteral(
        "#!/bin/sh\n"
        "out=.\n"
        "while [ $# -gt 0 ]; do\n"
        "  if [ \"$1\" = \"-C\" ]; then shift; out=\"$1\"; fi\n"
        "  shift\n"
        "done\n"
        "printf 'parcial\\n' > \"$out/parcial.txt\"\n"
        "if [ \"%1\" = \"two\" ]; then\n"
        "  printf 'a\\n' > \"$out/a.txt\"; printf 'b\\n' > \"$out/b.txt\"\n"
        "fi\n"
        "case \"%1\" in\n"
        "  ok) exit 0 ;;\n"
        "  fail) echo erro-simulado >&2; exit 1 ;;\n"
        "  slow) exec sleep 30 ;;\n"
        "  later) sleep 1; exit 0 ;;\n"
        "  shortslow) sleep 3; exit 0 ;;\n"
        "  two) exit 0 ;;\n"
        "esac\n").arg(mode);
    f.write(script.toUtf8());
    f.close();
    QVERIFY(QFile::setPermissions(f.fileName(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                  | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther));
}

QStringList scratchDirs(const QString &parent)
{
    QStringList out;
    for (const QString &name : QDir(parent).entryList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot))
        if (name.startsWith(QStringLiteral(".fm-extract-")))
            out << name;
    return out;
}

QModelIndex findRowNamed(QTreeView *view, const QString &name)
{
    for (int r = 0; r < view->model()->rowCount(view->rootIndex()); ++r) {
        const QModelIndex i = view->model()->index(r, 0, view->rootIndex());
        if (view->model()->data(i).toString() == name)
            return i;
    }
    return QModelIndex();
}

void triggerOnRow(QTreeView *view, const QModelIndex &row, const QString &label)
{
    runMenuAction(QStringList{ label });
    openContextMenu(view, row);
}

QStringList captureContextTargets(FileTab &tab, QAbstractItemView *view, const QModelIndex &index,
                                 int column = 0)
{
    QStringList targets;
    setContextMenuHookForTests([&targets](QMenu *, const QStringList &paths) {
        targets = paths;
        QTimer::singleShot(0, [] { closePopups(); });
    });
    const auto cleanup = qScopeGuard([] { setContextMenuHookForTests({}); });
    openContextMenu(view, index, column);
    return targets;
}

struct MenuSnapshot {
    QStringList labels;
    QString pasteData;
};

MenuSnapshot captureMenuSnapshot(FileTab &tab, QTreeView *view, const QModelIndex &index)
{
    MenuSnapshot snap;
    setContextMenuHookForTests([&snap](QMenu *menu, const QStringList &) {
        QTimer::singleShot(0, [] { closePopups(); });
        for (QAction *a : menu->actions()) {
            snap.labels << a->text();
            if (a->objectName() == QStringLiteral("pasteAction"))
                snap.pasteData = a->data().toString();
        }
    });
    const auto reset = qScopeGuard([] { setContextMenuHookForTests({}); });
    openContextMenu(view, index);
    return snap;
}

QStringList stripShortcuts(const QStringList &labels)
{
    QStringList out;
    for (const QString &label : labels)
        out << label.section(QLatin1Char('\t'), 0, 0);
    return out;
}

QString contextTestRoot(QTemporaryDir &dir)
{
    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    QDir().mkpath(root);
    for (const QString &name : { QStringLiteral("alpha.txt"), QStringLiteral("beta.txt"),
                                 QStringLiteral("gamma.txt") }) {
        QFile f(QDir(root).filePath(name));
        if (!f.open(QIODevice::WriteOnly))
            return QString();
        f.write(name.toUtf8());
    }
    return root;
}

struct LaunchCall {
    QString program;
    QStringList args;
    QString cwd;
};

// Executável falso para tornar a escolha do terminal determinística nos testes.
QString fakeTerminal(const QString &binDir, const QString &name)
{
    if (!QDir().mkpath(binDir))
        return QString();
    const QString exe = QDir(binDir).filePath(name);
    QFile f(exe);
    if (!f.open(QIODevice::WriteOnly))
        return QString();
    f.write("#!/bin/sh\nexit 0\n");
    f.close();
    if (!QFile::setPermissions(exe, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner))
        return QString();
    return exe;
}

QString writeDesktopFile(const QString &path, const QString &name, const QString &exec)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return QString();
    const QString content = QStringLiteral("[Desktop Entry]\nType=Application\nName=%1\nExec=%2\nTerminal=false\n")
                                .arg(name, exec);
    f.write(content.toUtf8());
    f.close();
    return QFileInfo(path).absoluteFilePath();
}

} // namespace

class FileTabTest : public QObject
{
    Q_OBJECT

private slots:
    void navigationBackForwardUp();
    void thumbnailIsDeliveredForPng();
    void contextMenuCreateCopyPasteRename();
    void newSubmenuGroupsAndContents();
    void newFromPersonalTemplate();
    void contextMenuTargetOnFirstRow();
    void contextMenuTargetOnDetailColumn();
    void contextMenuTargetOnIcons();
    void contextMenuPreservesMultiSelection();
    void contextMenuOutsideSelectionReplacesTarget();
    void f2UsesCurrentIndexWithMultiSelection();
    void menuActionsUseFrozenTarget();
    void copyPathActionsWriteTextToClipboard();
    void contextMenuCompositionByTarget();
    void pasteOverSubfolderUsesCurrentDirectory();
    void openWithUsesDesktopPathAndSeparateArgs();
    void desktopFileForIdPrefersXdgDataDirsOverDefaultHome();
    void desktopFileForIdPrefersFirstXdgDataDir();
    void desktopFileForIdResolvesSubdirectoryId();
    void desktopFileForIdPrefersXdgDataHomeOverDataDirs();
    void desktopFileForIdIgnoresRelativeXdgPaths();
    void openWithFindsNestedDesktopUsingGioId();
    void gioLaunchPassesAccentedPathToDesktopApp();
    void openWithLaunchFailureReportsStatus();
    void openWithCancelStartsNothing();
    void executableChooserLaunchSurvivesTabClose();
    void openWithWithoutAssociationsOffersChooser();
    void orphanDesktopApplicationReportsStatus();
    void terminalUsesTargetDirectory();
    void emptyAreaTerminalUsesCurrentDirectory();
    void missingTerminalReportsStatus();
    void propertiesOfCurrentFolder();
    void refreshRereadsWithoutWatcherAndKeepsHistory();
    void mimeHookResetRestoresAsyncQueries();
    void extractionFailureDoesNotPublishFolder();
    void extractionCancelRemovesTemporaryFolder();
    void extractionSuccessPublishesFolder();
    void extractHereFailureLeavesCurrentFolderUntouched();
    void extractHerePublishesFiles();
    void extractHereRollsBackWhenPublishFails();
    void extractHereUsesOriginalDestination();
    void extractHereCancelRemovesTemporaryFolder();
    void extractHereKeepsBackupWhenRestoreFails();
    void closingTabStopsExtractionBeforeCleanup();
    void closingTabKeepsScratchWhenProcessDoesNotStop();
    void zoomChangesIconAndGridSize();
    void zoomClampsAndSurvivesModeSwitch();
    void viewRefinementsAreApplied();
    void zoomResetRestoresInitialSizes();
    void sizeColumnCellsAreRightAligned();
    void presetChangesIconAndColumns();
    void densityChangesRowHeight();
    void previewFollowsPreferences();
    void sectionHeadersAreNotPlaces();
    void menuShortcutsByPreset();
    void menuTargetsUnchanged();
};

void FileTabTest::navigationBackForwardUp()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    const QString sub = QDir(root).filePath(QStringLiteral("sub"));
    QVERIFY(QDir().mkpath(sub));

    FileTab tab(root);
    QCOMPARE(tab.currentPath(), QDir::cleanPath(root));

    tab.goTo(sub);
    QCOMPARE(tab.currentPath(), QDir::cleanPath(sub));
    QVERIFY(tab.canGoBack());

    tab.back();
    QCOMPARE(tab.currentPath(), QDir::cleanPath(root));
    QVERIFY(tab.canGoForward());

    tab.forward();
    QCOMPARE(tab.currentPath(), QDir::cleanPath(sub));

    tab.up();
    QCOMPARE(tab.currentPath(), QDir::cleanPath(root));
}

void FileTabTest::thumbnailIsDeliveredForPng()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage img(64, 64, QImage::Format_RGB32);
    img.fill(QColor(QStringLiteral("#ff8800")));
    QVERIFY(img.save(QDir(dir.path()).filePath(QStringLiteral("teste.png")), "PNG"));

    QFileSystemModel model;
    model.setRootPath(dir.path());
    ThumbnailProxy proxy(&model);
    const QModelIndex root = proxy.mapFromSource(model.index(dir.path()));
    QTRY_VERIFY_WITH_TIMEOUT(proxy.rowCount(root) > 0, 3000);
    const QModelIndex idx = proxy.index(0, 0, root);
    QVERIFY2(idx.isValid(), "a imagem de teste deve aparecer no modelo");

    QSignalSpy changed(&proxy, &QAbstractItemModel::dataChanged);
    (void)proxy.data(idx, Qt::DecorationRole); // agenda a miniatura
    QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 1, 5000);

    const QIcon icon = proxy.data(idx, Qt::DecorationRole).value<QIcon>();
    QVERIFY(!icon.isNull());
}

void FileTabTest::contextMenuCreateCopyPasteRename()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    const QString sub = QDir(root).filePath(QStringLiteral("sub"));
    QVERIFY(QDir().mkpath(sub));
    const QString original = QDir(root).filePath(QStringLiteral("origem.txt"));
    {
        QFile f(original);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("conteudo original\n");
    }

    FileTab tab(root);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(view->model()->rowCount(view->rootIndex()) > 0, 3000);

    auto findRow = [view](const QString &name) {
        for (int r = 0; r < view->model()->rowCount(view->rootIndex()); ++r) {
            const QModelIndex i = view->model()->index(r, 0, view->rootIndex());
            if (view->model()->data(i).toString() == name)
                return i;
        }
        return QModelIndex();
    };
    auto selectRow = [view, findRow](const QString &name) {
        const QModelIndex i = findRow(name);
        view->selectionModel()->select(i, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        view->setCurrentIndex(i);
        return i.isValid();
    };

    // 1) menu de contexto: Novo > Texto (.txt), nome manual-test.txt
    runMenuAction(QStringList{ QStringLiteral("Novo"), QStringLiteral("Texto (.txt)") },
                  {}, acceptNewFileName);
    openContextMenu(view);
    const QString created = QDir(root).filePath(QStringLiteral("manual-test.txt"));
    QVERIFY2(QFileInfo::exists(created), "menu Novo > Texto (.txt) deve criar o arquivo");
    {
        QFile f(created);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("conteudo de teste\n");
    }

    // 2) Copiar no menu, navegar para a subpasta e Colar no menu
    QTRY_VERIFY_WITH_TIMEOUT(findRow(QStringLiteral("manual-test.txt")).isValid(), 3000);
    const QModelIndex srcRow = findRow(QStringLiteral("manual-test.txt"));
    QVERIFY(selectRow(QStringLiteral("manual-test.txt")));
    runMenuAction(QStringList{ QStringLiteral("Copiar") },
                  [selectRow] { selectRow(QStringLiteral("manual-test.txt")); });
    openContextMenu(view, srcRow);
    const QMimeData *clip = QGuiApplication::clipboard()->mimeData();
    QVERIFY(clip && clip->hasUrls());

    tab.goTo(sub);
    runMenuAction(QStringList{ QStringLiteral("Colar") });
    openContextMenu(view);
    const QString pasted = QDir(sub).filePath(QStringLiteral("manual-test.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(pasted), 3000);

    // 3) Renomear a cópia pelo menu para copia-renomeada.txt
    QTRY_VERIFY_WITH_TIMEOUT(findRow(QStringLiteral("manual-test.txt")).isValid(), 3000);
    const QModelIndex dstRow = findRow(QStringLiteral("manual-test.txt"));
    QVERIFY(selectRow(QStringLiteral("manual-test.txt")));
    runMenuAction(QStringList{ QStringLiteral("Renomear") },
                  [selectRow] { selectRow(QStringLiteral("manual-test.txt")); });
    openContextMenu(view, dstRow);
    QTRY_VERIFY_WITH_TIMEOUT(view->findChild<QLineEdit *>() != nullptr, 2000);
    QLineEdit *editor = view->findChild<QLineEdit *>();
    editor->setText(QStringLiteral("copia-renomeada.txt"));
    QTest::keyClick(editor, Qt::Key_Return);
    const QString renamed = QDir(sub).filePath(QStringLiteral("copia-renomeada.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(renamed), 3000);
    QVERIFY(!QFileInfo::exists(pasted));

    // 4) original intacto; cópia com o mesmo conteúdo
    auto contents = [](const QString &path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return QByteArray();
        return f.readAll();
    };
    QCOMPARE(contents(original), QByteArray("conteudo original\n"));
    QCOMPARE(contents(created), QByteArray("conteudo de teste\n"));
    QCOMPARE(contents(renamed), QByteArray("conteudo de teste\n"));
}

void FileTabTest::newSubmenuGroupsAndContents()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    const QString tplDir = QDir(dir.path()).filePath(QStringLiteral("templates"));
    QVERIFY(QDir().mkpath(root));
    QVERIFY(QDir().mkpath(tplDir));
    setTemplatesDirForTests(tplDir);
    const auto resetTemplates = qScopeGuard([] { setTemplatesDirForTests(QString()); });

    FileTab tab(root);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);

    auto captureNovo = [view]() {
        QStringList labels;
        setContextMenuHookForTests([&labels](QMenu *menu, const QStringList &) {
            QAction *novo = actionNamed(menu, QStringLiteral("Novo"));
            labels = novo ? collectMenuLabels(novo->menu()) : QStringList();
            QTimer::singleShot(0, [] { closePopups(); });
        });
        const auto resetHook = qScopeGuard([] { setContextMenuHookForTests({}); });
        openContextMenu(view);
        return labels;
    };

    const QStringList base{
        QStringLiteral("Pasta…"),
        QStringLiteral("Arquivo vazio…"),
        QStringLiteral("Texto (.txt)"),
        QStringLiteral("Markdown (.md)"),
        QStringLiteral("---"),
        QStringLiteral("Código e dados"),
        QStringLiteral("Código e dados > HTML (.html)"),
        QStringLiteral("Código e dados > JSON (.json)"),
        QStringLiteral("Código e dados > Script shell (.sh)"),
        QStringLiteral("Código e dados > Script Python (.py)"),
    };
    // Pasta de modelos vazia: a entrada Modelos não deve aparecer.
    const QStringList emptyLabels = captureNovo();
    QVERIFY2(emptyLabels == base,
             qPrintable(QStringLiteral("roteiros: %1").arg(emptyLabels.join(QLatin1String(" | ")))));

    QFile tpl(QDir(tplDir).filePath(QStringLiteral("modelo.txt")));
    QVERIFY(tpl.open(QIODevice::WriteOnly));
    tpl.write("modelo\n");
    tpl.close();
    const QStringList withTemplate = base + QStringList{ QStringLiteral("---"),
                                                         QStringLiteral("Modelos"),
                                                         QStringLiteral("Modelos > modelo.txt") };
    const QStringList templateLabels = captureNovo();
    QVERIFY2(templateLabels == withTemplate,
             qPrintable(QStringLiteral("roteiros: %1").arg(templateLabels.join(QLatin1String(" | ")))));

    auto createViaMenu = [&](const QStringList &labels, const QString &name) {
        runMenuAction(QStringList{ QStringLiteral("Novo") } + labels, {},
                      [name] { acceptName(name); });
        openContextMenu(view);
    };

    createViaMenu({ QStringLiteral("Pasta…") }, QStringLiteral("pasta-criada"));
    QVERIFY(QFileInfo(QDir(root).filePath(QStringLiteral("pasta-criada"))).isDir());

    createViaMenu({ QStringLiteral("Arquivo vazio…") }, QStringLiteral("vazio"));
    const QString vazio = QDir(root).filePath(QStringLiteral("vazio"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(vazio), 3000);
    QCOMPARE(fileContents(vazio), QByteArray());

    createViaMenu({ QStringLiteral("Texto (.txt)") }, QStringLiteral("texto.txt"));
    const QString texto = QDir(root).filePath(QStringLiteral("texto.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(texto), 3000);
    QCOMPARE(fileContents(texto), QByteArray());

    createViaMenu({ QStringLiteral("Markdown (.md)") }, QStringLiteral("notas.md"));
    QCOMPARE(fileContents(QDir(root).filePath(QStringLiteral("notas.md"))), QByteArray("# Título\n"));

    createViaMenu({ QStringLiteral("Código e dados"), QStringLiteral("HTML (.html)") },
                  QStringLiteral("pagina.html"));
    QCOMPARE(fileContents(QDir(root).filePath(QStringLiteral("pagina.html"))),
             QByteArray("<!DOCTYPE html>\n<html lang=\"pt-BR\">\n<head>\n  <meta charset=\"utf-8\">\n"
                        "  <title></title>\n</head>\n<body>\n\n</body>\n</html>\n"));

    createViaMenu({ QStringLiteral("Código e dados"), QStringLiteral("JSON (.json)") },
                  QStringLiteral("dados.json"));
    QCOMPARE(fileContents(QDir(root).filePath(QStringLiteral("dados.json"))), QByteArray("{}\n"));

    const QString shell = QDir(root).filePath(QStringLiteral("script.sh"));
    createViaMenu({ QStringLiteral("Código e dados"), QStringLiteral("Script shell (.sh)") },
                  QStringLiteral("script.sh"));
    QCOMPARE(fileContents(shell), QByteArray("#!/usr/bin/env bash\nset -euo pipefail\n\n"));
    QVERIFY2(QFileInfo(shell).permissions() & QFile::ExeOwner, "script.sh precisa ser executável");

    createViaMenu({ QStringLiteral("Código e dados"), QStringLiteral("Script Python (.py)") },
                  QStringLiteral("script.py"));
    QCOMPARE(fileContents(QDir(root).filePath(QStringLiteral("script.py"))),
             QByteArray("#!/usr/bin/env python3\n\n"));
}

void FileTabTest::newFromPersonalTemplate()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    const QString tplDir = QDir(dir.path()).filePath(QStringLiteral("templates"));
    QVERIFY(QDir().mkpath(root));
    QVERIFY(QDir().mkpath(tplDir));
    const QString tplPath = QDir(tplDir).filePath(QStringLiteral("modelo com acento.txt"));
    {
        QFile f(tplPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QVERIFY(f.write("conteúdo do modelo\nlinha 2\n") > 0);
    }
    setTemplatesDirForTests(tplDir);
    const auto resetTemplates = qScopeGuard([] { setTemplatesDirForTests(QString()); });

    FileTab tab(root);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);

    runMenuAction(QStringList{ QStringLiteral("Novo"), QStringLiteral("Modelos"),
                               QStringLiteral("modelo com acento.txt") });
    openContextMenu(view);

    const QString created = QDir(root).filePath(QStringLiteral("modelo com acento.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(created), 3000);
    QCOMPARE(fileContents(created), fileContents(tplPath));
}

void FileTabTest::contextMenuTargetOnFirstRow()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = contextTestRoot(dir);
    FileTab tab(root);
    tab.show();
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(view->model()->rowCount(view->rootIndex()) > 0, 3000);
    const QModelIndex index = findRowNamed(view, QStringLiteral("alpha.txt"));
    QVERIFY(index.isValid());
    QCOMPARE(captureContextTargets(tab, view, index), QStringList{QDir(root).filePath("alpha.txt")});
}

void FileTabTest::contextMenuTargetOnDetailColumn()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = contextTestRoot(dir);
    FileTab tab(root);
    tab.show();
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(view->model()->rowCount(view->rootIndex()) > 0, 3000);
    const QModelIndex index = findRowNamed(view, QStringLiteral("beta.txt"));
    QVERIFY(index.isValid());
    QCOMPARE(captureContextTargets(tab, view, index, 1), QStringList{QDir(root).filePath("beta.txt")});
}

void FileTabTest::contextMenuTargetOnIcons()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = contextTestRoot(dir);
    FileTab tab(root);
    tab.setIconMode(true);
    tab.show();
    QListView *view = nullptr;
    for (QListView *candidate : tab.findChildren<QListView *>())
        if (candidate->viewMode() == QListView::IconMode) { view = candidate; break; }
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(view->model()->rowCount(view->rootIndex()) > 0, 3000);
    const QModelIndex index = view->model()->index(0, 0, view->rootIndex());
    const QString expected = view->model()->data(index).toString();
    QCOMPARE(captureContextTargets(tab, view, index), QStringList{QDir(root).filePath(expected)});
}

void FileTabTest::contextMenuPreservesMultiSelection()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = contextTestRoot(dir);
    FileTab tab(root);
    tab.show();
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(view->model()->rowCount(view->rootIndex()) > 0, 3000);
    const QModelIndex alpha = findRowNamed(view, QStringLiteral("alpha.txt"));
    const QModelIndex beta = findRowNamed(view, QStringLiteral("beta.txt"));
    view->selectionModel()->select(alpha, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->selectionModel()->select(beta, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    QStringList expected{QDir(root).filePath("alpha.txt"), QDir(root).filePath("beta.txt")};
    expected.sort();
    QStringList actual = captureContextTargets(tab, view, beta);
    actual.sort();
    QCOMPARE(actual, expected);
}

void FileTabTest::contextMenuOutsideSelectionReplacesTarget()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = contextTestRoot(dir);
    FileTab tab(root);
    tab.show();
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(view->model()->rowCount(view->rootIndex()) > 0, 3000);
    const QModelIndex alpha = findRowNamed(view, QStringLiteral("alpha.txt"));
    const QModelIndex gamma = findRowNamed(view, QStringLiteral("gamma.txt"));
    view->selectionModel()->select(alpha, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->selectionModel()->select(alpha, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QCOMPARE(captureContextTargets(tab, view, gamma), QStringList{QDir(root).filePath("gamma.txt")});
}

void FileTabTest::f2UsesCurrentIndexWithMultiSelection()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = contextTestRoot(dir);
    FileTab tab(root);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(view->model()->rowCount(view->rootIndex()) >= 3, 3000);

    const QModelIndex alpha = findRowNamed(view, QStringLiteral("alpha.txt"));
    const QModelIndex beta = findRowNamed(view, QStringLiteral("beta.txt"));
    QVERIFY(alpha.isValid() && beta.isValid());
    view->selectionModel()->select(alpha, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->selectionModel()->select(beta, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    view->selectionModel()->setCurrentIndex(beta, QItemSelectionModel::NoUpdate);

    // A primeira linha selecionada é alpha.txt, mas o item corrente é beta.txt.
    QCOMPARE(view->selectionModel()->selectedRows().first().data().toString(), QStringLiteral("alpha.txt"));
    QCOMPARE(view->selectionModel()->currentIndex().data().toString(), QStringLiteral("beta.txt"));

    tab.setFocus();
    QTest::keyClick(&tab, Qt::Key_F2);

    QTRY_VERIFY_WITH_TIMEOUT(view->findChild<QLineEdit *>() != nullptr, 2000);
    QLineEdit *editor = view->findChild<QLineEdit *>();
    QVERIFY2(editor->text() == QStringLiteral("beta.txt"),
             qPrintable(QStringLiteral("editor abriu em: %1").arg(editor->text())));
}

void FileTabTest::menuActionsUseFrozenTarget()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(root));
    for (const QString &name : { QStringLiteral("origem.txt"), QStringLiteral("outro.txt") }) {
        QFile f(QDir(root).filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(name.toUtf8());
    }

    FileTab tab(root);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(view->model()->rowCount(view->rootIndex()) > 0, 3000);

    const QModelIndex origin = findRowNamed(view, QStringLiteral("origem.txt"));
    QVERIFY(origin.isValid());
    auto selectRow = [view](const QString &name) {
        const QModelIndex i = findRowNamed(view, name);
        if (!i.isValid())
            return false;
        view->selectionModel()->select(i, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        view->setCurrentIndex(i);
        return true;
    };
    QVERIFY(selectRow(QStringLiteral("origem.txt")));
    QGuiApplication::clipboard()->clear();

    runMenuAction(QStringList{ QStringLiteral("Copiar") },
                  [=] { selectRow(QStringLiteral("outro.txt")); });
    openContextMenu(view, origin);

    // Confirma primeiro que a seleção visual mudou enquanto o menu estava aberto.
    const QModelIndex current = view->selectionModel()->currentIndex();
    QVERIFY(current.isValid());
    QCOMPARE(current.data().toString(), QStringLiteral("outro.txt"));

    const QMimeData *clip = QGuiApplication::clipboard()->mimeData();
    QVERIFY(clip && clip->hasUrls());
    const QList<QUrl> urls = clip->urls();
    QCOMPARE(urls.size(), 1);
    QCOMPARE(urls.first().toLocalFile(), QDir(root).filePath(QStringLiteral("origem.txt")));
}

void FileTabTest::copyPathActionsWriteTextToClipboard()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = contextTestRoot(dir);
    FileTab tab(root);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(view->model()->rowCount(view->rootIndex()) > 0, 3000);

    const QModelIndex alpha = findRowNamed(view, QStringLiteral("alpha.txt"));
    const QModelIndex beta = findRowNamed(view, QStringLiteral("beta.txt"));
    QVERIFY(alpha.isValid());
    QVERIFY(beta.isValid());

    view->selectionModel()->select(alpha, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->setCurrentIndex(alpha);
    QGuiApplication::clipboard()->clear();
    runMenuAction(QStringList{ QStringLiteral("Copiar caminho") });
    openContextMenu(view, alpha);
    const QMimeData *one = QGuiApplication::clipboard()->mimeData();
    QVERIFY(one && one->hasText());
    QCOMPARE(one->text(), QDir(root).filePath(QStringLiteral("alpha.txt")));

    // O modelo ordena as linhas; refaz a busca para não usar índices obsoletos.
    const QModelIndex alphaNow = findRowNamed(view, QStringLiteral("alpha.txt"));
    const QModelIndex betaNow = findRowNamed(view, QStringLiteral("beta.txt"));
    QVERIFY(alphaNow.isValid());
    QVERIFY(betaNow.isValid());
    view->selectionModel()->select(alphaNow, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->selectionModel()->select(betaNow, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    QGuiApplication::clipboard()->clear();
    runMenuAction(QStringList{ QStringLiteral("Copiar caminhos") });
    openContextMenu(view, betaNow);
    const QMimeData *many = QGuiApplication::clipboard()->mimeData();
    QVERIFY(many && many->hasText());
    const QString expectedPaths = QDir(root).filePath(QStringLiteral("alpha.txt")) + QLatin1Char('\n')
                                + QDir(root).filePath(QStringLiteral("beta.txt"));
    QCOMPARE(many->text(), expectedPaths);
}

void FileTabTest::contextMenuCompositionByTarget()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(QDir(root).filePath(QStringLiteral("sub"))));
    for (const QString &name : { QStringLiteral("alpha.txt"), QStringLiteral("beta.txt"),
                                 QStringLiteral("arquivo.zip") }) {
        QFile f(QDir(root).filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        QVERIFY(f.write("conteudo\n") > 0);
    }
    QGuiApplication::clipboard()->clear();

    FileTab tab(root);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(view->model()->rowCount(view->rootIndex()) >= 4, 3000);

    struct Snapshot {
        QStringList labels;
        bool pasteEnabled = false;
    };
    auto capture = [view](const QModelIndex &index) {
        Snapshot snap;
        setContextMenuHookForTests([&snap](QMenu *menu, const QStringList &) {
            snap.labels = stripShortcuts(directMenuLabels(menu));
            QAction *paste = actionNamed(menu, QStringLiteral("Colar"));
            snap.pasteEnabled = paste && paste->isEnabled();
            QTimer::singleShot(0, [] { closePopups(); });
        });
        const auto reset = qScopeGuard([] { setContextMenuHookForTests({}); });
        openContextMenu(view, index);
        return snap;
    };
    auto verify = [](const Snapshot &snap, const QStringList &expected, const char *target) {
        QVERIFY2(snap.labels == expected,
                 qPrintable(QStringLiteral("%1: %2").arg(QLatin1String(target),
                                                         snap.labels.join(QLatin1String(" | ")))));
        QVERIFY2(!snap.pasteEnabled, "Colar deve ficar desabilitado com clipboard limpo");
    };

    const QStringList emptyExpected{
        QStringLiteral("Nova pasta…"), QStringLiteral("Novo"), QStringLiteral("Colar"),
        QStringLiteral("Atualizar"), QStringLiteral("Exibir"), QStringLiteral("Abrir terminal aqui"),
        QStringLiteral("Propriedades da pasta")
    };
    const QStringList fileExpected{
        QStringLiteral("Abrir"), QStringLiteral("Abrir com…"), QStringLiteral("Copiar caminho"),
        QStringLiteral("Novo"), QStringLiteral("Copiar"), QStringLiteral("Recortar"),
        QStringLiteral("Colar"), QStringLiteral("Renomear"), QStringLiteral("Mover para a lixeira"),
        QStringLiteral("Excluir permanentemente"), QStringLiteral("Compactar…"),
        QStringLiteral("Propriedades")
    };
    const QStringList dirExpected{
        QStringLiteral("Abrir"), QStringLiteral("Abrir em nova aba"),
        QStringLiteral("Abrir terminal nesta pasta"), QStringLiteral("Copiar caminho"),
        QStringLiteral("Novo"), QStringLiteral("Copiar"), QStringLiteral("Recortar"),
        QStringLiteral("Colar"), QStringLiteral("Renomear"), QStringLiteral("Mover para a lixeira"),
        QStringLiteral("Excluir permanentemente"), QStringLiteral("Compactar…"),
        QStringLiteral("Propriedades")
    };
    const QStringList manyExpected{
        QStringLiteral("Copiar"), QStringLiteral("Recortar"), QStringLiteral("Copiar caminhos"),
        QStringLiteral("Colar"), QStringLiteral("Compactar…"), QStringLiteral("Mover para a lixeira"),
        QStringLiteral("Excluir permanentemente"), QStringLiteral("Novo")
    };

    verify(capture(QModelIndex()), emptyExpected, "area vazia");

    const QModelIndex alpha = findRowNamed(view, QStringLiteral("alpha.txt"));
    QVERIFY(alpha.isValid());

    view->selectionModel()->select(alpha, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->setCurrentIndex(alpha);
    verify(capture(alpha), fileExpected, "arquivo");

    const QModelIndex subNow = findRowNamed(view, QStringLiteral("sub"));
    QVERIFY(subNow.isValid());
    view->selectionModel()->select(subNow, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->setCurrentIndex(subNow);
    verify(capture(subNow), dirExpected, "pasta");

    const QModelIndex alphaNow = findRowNamed(view, QStringLiteral("alpha.txt"));
    const QModelIndex betaNow = findRowNamed(view, QStringLiteral("beta.txt"));
    QVERIFY(alphaNow.isValid());
    QVERIFY(betaNow.isValid());
    view->selectionModel()->select(alphaNow, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->setCurrentIndex(alphaNow);
    view->selectionModel()->select(betaNow, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    verify(capture(alphaNow), manyExpected, "selecao multipla");

    const QModelIndex zipNow = findRowNamed(view, QStringLiteral("arquivo.zip"));
    QVERIFY(zipNow.isValid());
    QStringList archiveExpected = fileExpected;
    archiveExpected.insert(archiveExpected.size() - 1, QStringLiteral("Extrair aqui"));
    archiveExpected.insert(archiveExpected.size() - 1, QStringLiteral("Extrair em pasta própria"));
    verify(capture(zipNow), archiveExpected, "arquivo compactado");
}

void FileTabTest::pasteOverSubfolderUsesCurrentDirectory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    const QString sub = QDir(root).filePath(QStringLiteral("sub"));
    QVERIFY(QDir().mkpath(sub));
    const QString source = QDir(root).filePath(QStringLiteral("origem.txt"));
    {
        QFile f(source);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QVERIFY(f.write("conteudo\n") > 0);
    }
    const QString inside = QDir(sub).filePath(QStringLiteral("inside.txt"));
    {
        QFile f(inside);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QVERIFY(f.write("dentro\n") > 0);
    }

    FileTab tab(root);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(view->model()->rowCount(view->rootIndex()) >= 2, 3000);

    const QModelIndex srcRow = findRowNamed(view, QStringLiteral("origem.txt"));
    QVERIFY(srcRow.isValid());
    view->selectionModel()->select(srcRow, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->setCurrentIndex(srcRow);
    runMenuAction(QStringList{QStringLiteral("Copiar")});
    openContextMenu(view, srcRow);
    const QMimeData *clip = QGuiApplication::clipboard()->mimeData();
    QVERIFY(clip && clip->hasUrls());

    const QModelIndex subRow = findRowNamed(view, QStringLiteral("sub"));
    QVERIFY(subRow.isValid());
    runMenuAction(QStringList{QStringLiteral("Colar")});
    openContextMenu(view, subRow);

    const QString copyName = QStringLiteral("origem (cópia).txt");
    const QString inRoot = QDir(root).filePath(copyName);
    const QString inSub = QDir(sub).filePath(copyName);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(inRoot), 3000);
    QVERIFY2(!QFileInfo::exists(inSub), "colar sobre a subpasta nao pode usar o item como destino");

    QVERIFY(tab.goTo(sub));
    QTRY_VERIFY_WITH_TIMEOUT(findRowNamed(view, QStringLiteral("inside.txt")).isValid(), 3000);
    const QModelIndex insideRow = findRowNamed(view, QStringLiteral("inside.txt"));
    QVERIFY(insideRow.isValid());
    runMenuAction(QStringList{QStringLiteral("Colar")});
    openContextMenu(view, insideRow);
    const QString inSubAgain = QDir(sub).filePath(QStringLiteral("origem.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(inSubAgain), 3000);
    QCOMPARE(fileContents(inRoot), QByteArray("conteudo\n"));
    QCOMPARE(fileContents(inSubAgain), QByteArray("conteudo\n"));
}

void FileTabTest::openWithUsesDesktopPathAndSeparateArgs()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString apps = QDir(dir.path()).filePath(QStringLiteral("applications"));
    QVERIFY(QDir().mkpath(apps));
    const QString desktop = writeDesktopFile(QDir(apps).filePath(QStringLiteral("tmp.editor.desktop")),
                                             QStringLiteral("Editor de Teste"),
                                             QStringLiteral("/bin/true %f"));
    QVERIFY(!desktop.isEmpty());

    const QByteArray oldXdg = qgetenv("XDG_DATA_HOME");
    qputenv("XDG_DATA_HOME", QDir(dir.path()).absolutePath().toUtf8());
    const auto restoreXdg = qScopeGuard([oldXdg] { qputenv("XDG_DATA_HOME", oldXdg); });

    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(root));
    const QString file = QDir(root).filePath(QStringLiteral("arquivo com acento.txt"));
    {
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("conteúdo\n");
    }

    LaunchCall call;
    bool called = false;
    setMimeQueryForTests({QStringLiteral("tmp.editor.desktop")});
    setLaunchHookForTests([&](const QString &program, const QStringList &args, const QString &cwd) {
        call = {program, args, cwd};
        called = true;
        return true;
    });
    const auto resetMime = qScopeGuard([] { clearMimeQueryForTestsForTests(); });
    const auto resetLaunch = qScopeGuard([] { setLaunchHookForTests({}); });

    FileTab tab(root);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    const QModelIndex row = findRowNamed(view, QFileInfo(file).fileName());
    QVERIFY(row.isValid());

    runMenuAction(QStringList{QStringLiteral("Abrir com…"), QStringLiteral("Editor de Teste")});
    openContextMenu(view, row);

    QTRY_VERIFY_WITH_TIMEOUT(called, 3000);
    const QStringList expectedArgs{QStringLiteral("launch"), desktop, file};
    QCOMPARE(call.program, QStringLiteral("gio"));
    QCOMPARE(call.args, expectedArgs);
    QVERIFY(call.cwd.isEmpty());
}

void FileTabTest::desktopFileForIdPrefersXdgDataDirsOverDefaultHome()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString fakeHome = QDir(dir.path()).filePath(QStringLiteral("fakehome"));
    const QString xdgHome = QDir(dir.path()).filePath(QStringLiteral("xdghome"));
    const QString dirs1 = QDir(dir.path()).filePath(QStringLiteral("dirs1"));
    const QString dirs2 = QDir(dir.path()).filePath(QStringLiteral("dirs2"));
    QVERIFY(QDir().mkpath(QDir(fakeHome).filePath(QStringLiteral(".local/share/applications"))));
    for (const QString &base : {xdgHome, dirs1, dirs2})
        QVERIFY(QDir().mkpath(QDir(base).filePath(QStringLiteral("applications"))));

    const QString inHome = writeDesktopFile(
        QDir(fakeHome).filePath(QStringLiteral(".local/share/applications/dup.desktop")),
        QStringLiteral("Casa"), QStringLiteral("/bin/true"));
    const QString inDirs = writeDesktopFile(
        QDir(dirs1).filePath(QStringLiteral("applications/dup.desktop")),
        QStringLiteral("Dados"), QStringLiteral("/bin/true"));
    QVERIFY(!inHome.isEmpty());
    QVERIFY(!inDirs.isEmpty());

    const auto restoreHome = setTestEnv("HOME", fakeHome.toUtf8());
    const auto restoreDataHome = setTestEnv("XDG_DATA_HOME", xdgHome.toUtf8());
    const auto restoreDataDirs = setTestEnv("XDG_DATA_DIRS",
                                            (dirs1 + QLatin1Char(':') + dirs2).toUtf8());

    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(root));
    FileTab tab(root);
    QCOMPARE(tab.desktopFileForId(QStringLiteral("dup.desktop")), QFileInfo(inDirs).absoluteFilePath());
}

void FileTabTest::desktopFileForIdPrefersFirstXdgDataDir()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString xdgHome = QDir(dir.path()).filePath(QStringLiteral("xdghome"));
    const QString dirs1 = QDir(dir.path()).filePath(QStringLiteral("dirs1"));
    const QString dirs2 = QDir(dir.path()).filePath(QStringLiteral("dirs2"));
    for (const QString &base : {xdgHome, dirs1, dirs2})
        QVERIFY(QDir().mkpath(QDir(base).filePath(QStringLiteral("applications"))));

    const QString inDirs = writeDesktopFile(
        QDir(dirs1).filePath(QStringLiteral("applications/dup.desktop")),
        QStringLiteral("Primeiro"), QStringLiteral("/bin/true"));
    QVERIFY(!writeDesktopFile(QDir(dirs2).filePath(QStringLiteral("applications/dup.desktop")),
                              QStringLiteral("Segundo"), QStringLiteral("/bin/true")).isEmpty());

    const auto restoreDataHome = setTestEnv("XDG_DATA_HOME", xdgHome.toUtf8());
    const auto restoreDataDirs = setTestEnv("XDG_DATA_DIRS",
                                            (dirs1 + QLatin1Char(':') + dirs2).toUtf8());

    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(root));
    FileTab tab(root);
    QCOMPARE(tab.desktopFileForId(QStringLiteral("dup.desktop")), QFileInfo(inDirs).absoluteFilePath());
}

void FileTabTest::desktopFileForIdResolvesSubdirectoryId()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString home = QDir(dir.path()).filePath(QStringLiteral("home"));
    const QString nested = QDir(home).filePath(QStringLiteral("applications/sub/nested.desktop"));
    QVERIFY(QDir().mkpath(QFileInfo(nested).absolutePath()));
    QVERIFY(!writeDesktopFile(nested, QStringLiteral("Aninhado"), QStringLiteral("/bin/true")).isEmpty());

    const auto restoreDataHome = setTestEnv("XDG_DATA_HOME", home.toUtf8());
    const auto restoreDataDirs = setTestEnv("XDG_DATA_DIRS",
                                            QDir(dir.path()).filePath(QStringLiteral("empty-dirs")).toUtf8());

    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(root));
    FileTab tab(root);
    QCOMPARE(tab.desktopFileForId(QStringLiteral("sub/nested.desktop")),
             QFileInfo(nested).absoluteFilePath());
    QVERIFY(tab.desktopFileForId(QStringLiteral("nested.desktop")).isEmpty());
}

void FileTabTest::desktopFileForIdPrefersXdgDataHomeOverDataDirs()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString xdgHome = QDir(dir.path()).filePath(QStringLiteral("xdghome"));
    const QString dirs1 = QDir(dir.path()).filePath(QStringLiteral("dirs1"));
    for (const QString &base : {xdgHome, dirs1})
        QVERIFY(QDir().mkpath(QDir(base).filePath(QStringLiteral("applications"))));

    const QString inHome = writeDesktopFile(
        QDir(xdgHome).filePath(QStringLiteral("applications/dup.desktop")),
        QStringLiteral("Casa"), QStringLiteral("/bin/true"));
    QVERIFY(!writeDesktopFile(QDir(dirs1).filePath(QStringLiteral("applications/dup.desktop")),
                              QStringLiteral("Dados"), QStringLiteral("/bin/true")).isEmpty());
    QVERIFY(!inHome.isEmpty());

    const auto restoreDataHome = setTestEnv("XDG_DATA_HOME", xdgHome.toUtf8());
    const auto restoreDataDirs = setTestEnv("XDG_DATA_DIRS", dirs1.toUtf8());

    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(root));
    FileTab tab(root);
    QCOMPARE(tab.desktopFileForId(QStringLiteral("dup.desktop")), QFileInfo(inHome).absoluteFilePath());
}

void FileTabTest::desktopFileForIdIgnoresRelativeXdgPaths()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString previousCwd = QDir::currentPath();
    const auto restoreCwd = qScopeGuard([&previousCwd] { QDir::setCurrent(previousCwd); });
    QVERIFY(QDir::setCurrent(dir.path()));

    // Caminhos XDG relativos devem ser ignorados; o CWD os tornaria existentes.
    const QString relHome = QStringLiteral("relhome");
    const QString relDirs = QStringLiteral("reldirs");
    QVERIFY(QDir().mkpath(QDir(relHome).filePath(QStringLiteral("applications"))));
    QVERIFY(QDir().mkpath(QDir(relDirs).filePath(QStringLiteral("applications"))));
    QVERIFY(!writeDesktopFile(QDir(relHome).filePath(QStringLiteral("applications/dup.desktop")),
                              QStringLiteral("RelHome"), QStringLiteral("/bin/true")).isEmpty());
    QVERIFY(!writeDesktopFile(QDir(relDirs).filePath(QStringLiteral("applications/dup.desktop")),
                              QStringLiteral("RelDirs"), QStringLiteral("/bin/true")).isEmpty());

    const QString fakeHome = QDir(dir.path()).filePath(QStringLiteral("fakehome"));
    QVERIFY(QDir().mkpath(QDir(fakeHome).filePath(QStringLiteral(".local/share/applications"))));
    const QString inHome = writeDesktopFile(
        QDir(fakeHome).filePath(QStringLiteral(".local/share/applications/dup.desktop")),
        QStringLiteral("Casa"), QStringLiteral("/bin/true"));
    QVERIFY(!inHome.isEmpty());

    const auto restoreHome = setTestEnv("HOME", fakeHome.toUtf8());
    const auto restoreDataHome = setTestEnv("XDG_DATA_HOME", relHome.toUtf8());
    const auto restoreDataDirs = setTestEnv("XDG_DATA_DIRS", relDirs.toUtf8());

    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(root));
    FileTab tab(root);
    QCOMPARE(tab.desktopFileForId(QStringLiteral("dup.desktop")), QFileInfo(inHome).absoluteFilePath());
}

void FileTabTest::openWithFindsNestedDesktopUsingGioId()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString apps = QDir(dir.path()).filePath(QStringLiteral("applications"));
    const QString nested = QDir(apps).filePath(QStringLiteral("sub/nested.desktop"));
    QVERIFY(QDir().mkpath(QFileInfo(nested).absolutePath()));
    {
        QFile f(nested);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[Desktop Entry]\nType=Application\nName=Editor Aninhado\nExec=/bin/true %f\nMimeType=text/plain;\n");
    }

    const int dbExit = QProcess::execute(QStringLiteral("update-desktop-database"), {apps});
    if (dbExit == -2)
        QSKIP("update-desktop-database não encontrado no ambiente");
    if (dbExit != 0)
        QSKIP("update-desktop-database falhou");

    const auto restoreDataHome = setTestEnv("XDG_DATA_HOME", dir.path().toUtf8());

    QProcess gio;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    gio.setProcessEnvironment(env);
    gio.start(QStringLiteral("gio"), {QStringLiteral("mime"), QStringLiteral("text/plain")});
    QVERIFY2(gio.waitForFinished(10000), qPrintable(gio.errorString()));
    QCOMPARE(gio.exitCode(), 0);
    const QString mimeOut = QString::fromLocal8Bit(gio.readAllStandardOutput());
    const QString mimeErr = QString::fromLocal8Bit(gio.readAllStandardError());

    // Captura o primeiro id devolvido pelo GIO, sem derivá-lo da transformação da produção.
    QString capturedId;
    bool inRegistered = false;
    for (const QString &line : mimeOut.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString text = line.trimmed();
        if (text.startsWith(QStringLiteral("Registered applications:"))) {
            inRegistered = true;
            continue;
        }
        if (inRegistered && text.startsWith(QStringLiteral("Recommended applications:")))
            break;
        if (inRegistered && text.endsWith(QStringLiteral(".desktop"), Qt::CaseInsensitive)) {
            capturedId = text;
            break;
        }
    }
    QVERIFY2(!capturedId.isEmpty(),
             qPrintable(QStringLiteral("gio mime não trouxe nenhum ID .desktop em Registered applications:\n%1\n%2")
                            .arg(mimeOut, mimeErr)));
    // Documentação do comportamento observado; a captura acima não depende desta transformação.
    QCOMPARE(capturedId, QStringLiteral("sub-nested.desktop"));

    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(root));
    const QString file = QDir(root).filePath(QStringLiteral("notas.txt"));
    {
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("texto\n");
    }

    LaunchCall call;
    bool called = false;
    setMimeQueryForTests({capturedId});
    setLaunchHookForTests([&](const QString &program, const QStringList &args, const QString &cwd) {
        call = {program, args, cwd};
        called = true;
        return true;
    });
    const auto resetMime = qScopeGuard([] { clearMimeQueryForTestsForTests(); });
    const auto resetLaunch = qScopeGuard([] { setLaunchHookForTests({}); });

    FileTab tab(root);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(view->model()->rowCount(view->rootIndex()) >= 1, 3000);
    const QModelIndex row = findRowNamed(view, QFileInfo(file).fileName());
    QVERIFY(row.isValid());

    QStringList labels;
    setContextMenuHookForTests([&](QMenu *menu, const QStringList &) {
        labels = collectMenuLabels(menu);
        QTimer::singleShot(0, [] { closePopups(); });
    });
    const auto resetMenuHook = qScopeGuard([] { setContextMenuHookForTests({}); });
    openContextMenu(view, row);
    QVERIFY2(labels.contains(QStringLiteral("Abrir com… > Editor Aninhado")),
             qPrintable(labels.join(QStringLiteral(" | "))));

    setContextMenuHookForTests({});
    runMenuAction(QStringList{QStringLiteral("Abrir com…"), QStringLiteral("Editor Aninhado")});
    openContextMenu(view, row);
    QTRY_VERIFY_WITH_TIMEOUT(called, 3000);
    const QStringList expectedArgs{QStringLiteral("launch"), nested, file};
    QCOMPARE(call.program, QStringLiteral("gio"));
    QCOMPARE(call.args, expectedArgs);
    QVERIFY(call.cwd.isEmpty());
}

void FileTabTest::gioLaunchPassesAccentedPathToDesktopApp()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString apps = QDir(dir.path()).filePath(QStringLiteral("applications"));
    QVERIFY(QDir().mkpath(apps));

    const QString received = QDir(dir.path()).filePath(QStringLiteral("recebido.txt"));
    const QString capture = QDir(dir.path()).filePath(QStringLiteral("capture.sh"));
    {
        QFile f(capture);
        QVERIFY(f.open(QIODevice::WriteOnly));
        const QString script = QStringLiteral("#!/bin/sh\nprintf '%s' \"$1\" > \"%1\"\n").arg(received);
        f.write(script.toUtf8());
    }
    QVERIFY(QFile::setPermissions(capture, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                  | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther));

    const QString desktop = writeDesktopFile(QDir(apps).filePath(QStringLiteral("test-editor.desktop")),
                                             QStringLiteral("Editor Descartável"),
                                             QStringLiteral("\"%1\" %f").arg(capture));
    QVERIFY(!desktop.isEmpty());

    const QByteArray oldXdg = qgetenv("XDG_DATA_HOME");
    qputenv("XDG_DATA_HOME", QDir(dir.path()).absolutePath().toUtf8());
    const auto restoreXdg = qScopeGuard([oldXdg] { qputenv("XDG_DATA_HOME", oldXdg); });

    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(root));
    const QString file = QDir(root).filePath(QStringLiteral("arquivo com acento á.txt"));
    {
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("conteúdo\n");
    }

    setLaunchHookForTests({});
    setMimeQueryForTests({QStringLiteral("test-editor.desktop")});
    const auto resetMime = qScopeGuard([] { clearMimeQueryForTestsForTests(); });

    FileTab tab(root);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    QCOMPARE(tab.desktopFileForId(QStringLiteral("test-editor.desktop")), desktop);

    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    const QModelIndex row = findRowNamed(view, QFileInfo(file).fileName());
    QVERIFY(row.isValid());

    runMenuAction(QStringList{QStringLiteral("Abrir com…"), QStringLiteral("Editor Descartável")});
    openContextMenu(view, row);

    QTRY_VERIFY_WITH_TIMEOUT(fileContents(received) == file.toUtf8(), 10000);
    QCOMPARE(fileContents(received), file.toUtf8());
}

void FileTabTest::openWithLaunchFailureReportsStatus()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString apps = QDir(dir.path()).filePath(QStringLiteral("applications"));
    QVERIFY(QDir().mkpath(apps));
    const QString desktop = writeDesktopFile(QDir(apps).filePath(QStringLiteral("fail.desktop")),
                                             QStringLiteral("Falha Teste"),
                                             QStringLiteral("%1/inexistente %f").arg(dir.path()));
    QVERIFY(!desktop.isEmpty());

    const QByteArray oldXdg = qgetenv("XDG_DATA_HOME");
    qputenv("XDG_DATA_HOME", QDir(dir.path()).absolutePath().toUtf8());
    const auto restoreXdg = qScopeGuard([oldXdg] { qputenv("XDG_DATA_HOME", oldXdg); });

    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(root));
    const QString file = QDir(root).filePath(QStringLiteral("arquivo.txt"));
    { QFile f(file); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("x"); }

    setLaunchHookForTests({});
    setMimeQueryForTests({QStringLiteral("fail.desktop")});
    const auto resetMime = qScopeGuard([] { clearMimeQueryForTestsForTests(); });

    FileTab tab(root);
    QSignalSpy spy(&tab, &FileTab::statusMessage);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    const QModelIndex row = findRowNamed(view, QFileInfo(file).fileName());
    QVERIFY(row.isValid());

    runMenuAction(QStringList{QStringLiteral("Abrir com…"), QStringLiteral("Falha Teste")});
    openContextMenu(view, row);

    const auto hasFailure = [&spy] {
        for (int i = 0; i < spy.count(); ++i)
            if (spy.at(i).value(0).toString().contains(QStringLiteral("Falha")))
                return true;
        return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(hasFailure(), 10000);
}

void FileTabTest::openWithCancelStartsNothing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = contextTestRoot(dir);
    const QString file = QDir(root).filePath(QStringLiteral("alpha.txt"));

    LaunchCall call;
    bool launched = false;
    bool chooserCalled = false;
    setMimeQueryForTests({});
    setExecutableChooserForTests([&] { chooserCalled = true; return QString(); });
    setLaunchHookForTests([&](const QString &program, const QStringList &args, const QString &cwd) {
        call = {program, args, cwd};
        launched = true;
        return true;
    });
    const auto resetMime = qScopeGuard([] { clearMimeQueryForTestsForTests(); });
    const auto resetChooser = qScopeGuard([] { setExecutableChooserForTests({}); });
    const auto resetLaunch = qScopeGuard([] { setLaunchHookForTests({}); });

    FileTab tab(root);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    const QModelIndex row = findRowNamed(view, QFileInfo(file).fileName());
    QVERIFY(row.isValid());

    runMenuAction(QStringList{QStringLiteral("Abrir com…"), QStringLiteral("Escolher executável…")});
    openContextMenu(view, row);

    QVERIFY(chooserCalled);
    QVERIFY2(!launched, "cancelar o escolhedor não deve iniciar launch");
    QVERIFY(call.program.isEmpty());
}

void FileTabTest::executableChooserLaunchSurvivesTabClose()
{
    if (!QFileInfo::exists(QStringLiteral("/proc")))
        QSKIP("sem /proc não é possível observar o processo lançado");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = contextTestRoot(dir);
    const QString file = QDir(root).filePath(QStringLiteral("alpha.txt"));
    const QString script = QDir(dir.path()).filePath(QStringLiteral("app.sh"));
    const QString pidFile = QDir(dir.path()).filePath(QStringLiteral("pid"));
    {
        QFile f(script);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QStringLiteral("#!/bin/sh\nprintf '%s\\n' \"$$\" > \"%1\"\nexec sleep 30\n")
                    .arg(pidFile).toUtf8());
    }
    QVERIFY(QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                   | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther));

    QString pid;
    const auto killApp = qScopeGuard([&pid] {
        if (pid.isEmpty())
            return;
        QProcess::execute(QStringLiteral("kill"), QStringList{QStringLiteral("-TERM"), pid});
    });

    setMimeQueryForTests({});
    setExecutableChooserForTests([script] { return script; });
    setLaunchHookForTests({});
    const auto resetMime = qScopeGuard([] { clearMimeQueryForTestsForTests(); });
    const auto resetChooser = qScopeGuard([] { setExecutableChooserForTests({}); });
    const auto resetLaunch = qScopeGuard([] { setLaunchHookForTests({}); });

    auto tab = std::make_unique<FileTab>(root);
    tab->show();
    QVERIFY(QTest::qWaitForWindowExposed(tab.get()));
    auto *view = tab->findChild<QTreeView *>();
    QVERIFY(view);
    const QModelIndex row = findRowNamed(view, QFileInfo(file).fileName());
    QVERIFY(row.isValid());

    runMenuAction(QStringList{QStringLiteral("Abrir com…"), QStringLiteral("Escolher executável…")});
    openContextMenu(view, row);

    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(pidFile), 3000);
    {
        QFile f(pidFile);
        QVERIFY(f.open(QIODevice::ReadOnly));
        pid = QString::fromUtf8(f.readAll()).trimmed();
    }
    QVERIFY(!pid.isEmpty());
    // /proc/<pid> também existe para zombies; o estado em /proc/<pid>/stat decide.
    const auto alive = [](const QString &p) {
        const QString base = QStringLiteral("/proc/") + p;
        if (!QFileInfo::exists(base))
            return false;
        QFile f(base + QStringLiteral("/stat"));
        if (!f.open(QIODevice::ReadOnly))
            return false;
        const QByteArray stat = f.readAll();
        const int close = stat.lastIndexOf(')');
        if (close < 0 || close + 2 >= stat.size())
            return false;
        return stat.at(close + 2) != 'Z';
    };
    QVERIFY2(alive(pid), qPrintable(QStringLiteral("processo %1 não iniciou").arg(pid)));

    tab.reset();
    qApp->processEvents();
    QTest::qWait(100);

    QVERIFY2(alive(pid),
             qPrintable(QStringLiteral("processo %1 ausente ou zombie após fechar a aba").arg(pid)));
}

void FileTabTest::openWithWithoutAssociationsOffersChooser()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = contextTestRoot(dir);

    setMimeQueryForTests({});
    setExecutableChooserForTests([] { return QString(); });
    const auto resetMime = qScopeGuard([] { clearMimeQueryForTestsForTests(); });
    const auto resetChooser = qScopeGuard([] { setExecutableChooserForTests({}); });

    QStringList labels;
    setContextMenuHookForTests([&labels](QMenu *menu, const QStringList &) {
        QAction *openWith = actionNamed(menu, QStringLiteral("Abrir com…"));
        labels = openWith && openWith->menu() ? collectMenuLabels(openWith->menu()) : QStringList();
        QTimer::singleShot(0, [] { closePopups(); });
    });
    const auto resetContext = qScopeGuard([] { setContextMenuHookForTests({}); });

    FileTab tab(root);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    const QModelIndex row = findRowNamed(view, QStringLiteral("alpha.txt"));
    QVERIFY(row.isValid());

    openContextMenu(view, row);

    QCOMPARE(labels, QStringList{QStringLiteral("Escolher executável…")});
}

void FileTabTest::orphanDesktopApplicationReportsStatus()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = contextTestRoot(dir);

    setMimeQueryForTests({QStringLiteral("fantasma-inexistente.desktop")});
    const auto resetMime = qScopeGuard([] { clearMimeQueryForTestsForTests(); });

    FileTab tab(root);
    QSignalSpy spy(&tab, &FileTab::statusMessage);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    const QModelIndex row = findRowNamed(view, QStringLiteral("alpha.txt"));
    QVERIFY(row.isValid());

    runMenuAction(QStringList{QStringLiteral("Abrir com…"),
                              QStringLiteral("fantasma-inexistente.desktop")});
    openContextMenu(view, row);

    const auto reported = [&spy] {
        for (int i = 0; i < spy.count(); ++i)
            if (spy.at(i).value(0).toString() == QStringLiteral("Aplicativo não encontrado"))
                return true;
        return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(reported(), 3000);
}

void FileTabTest::terminalUsesTargetDirectory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString bin = QDir(dir.path()).filePath(QStringLiteral("bin"));
    const QString terminal = fakeTerminal(bin, QStringLiteral("foot"));
    QVERIFY2(!terminal.isEmpty(), "terminal falso deve estar executável");
    const QByteArray oldPath = qgetenv("PATH");
    qputenv("PATH", QDir(bin).absolutePath().toUtf8());
    const auto restorePath = qScopeGuard([oldPath] { qputenv("PATH", oldPath); });

    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    const QString sub = QDir(root).filePath(QStringLiteral("sub"));
    QVERIFY(QDir().mkpath(sub));

    LaunchCall call;
    bool called = false;
    setLaunchHookForTests([&](const QString &program, const QStringList &args, const QString &cwd) {
        call = {program, args, cwd};
        called = true;
        return true;
    });
    const auto resetLaunch = qScopeGuard([] { setLaunchHookForTests({}); });

    FileTab tab(root);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(findRowNamed(view, QStringLiteral("sub")).isValid(), 3000);
    const QModelIndex row = findRowNamed(view, QStringLiteral("sub"));
    QVERIFY(row.isValid());

    runMenuAction(QStringList{QStringLiteral("Abrir terminal nesta pasta")});
    openContextMenu(view, row);

    QTRY_VERIFY_WITH_TIMEOUT(called, 3000);
    QCOMPARE(call.program, QFileInfo(terminal).absoluteFilePath());
    QVERIFY2(call.args.isEmpty(), qPrintable(QStringLiteral("args: %1").arg(call.args.join(QLatin1Char(' ')))));
    QCOMPARE(call.cwd, sub);
}

void FileTabTest::emptyAreaTerminalUsesCurrentDirectory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString bin = QDir(dir.path()).filePath(QStringLiteral("bin"));
    const QString terminal = fakeTerminal(bin, QStringLiteral("foot"));
    QVERIFY2(!terminal.isEmpty(), "terminal falso deve estar executável");
    const QByteArray oldPath = qgetenv("PATH");
    qputenv("PATH", QDir(bin).absolutePath().toUtf8());
    const auto restorePath = qScopeGuard([oldPath] { qputenv("PATH", oldPath); });

    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(root));

    LaunchCall call;
    bool called = false;
    setLaunchHookForTests([&](const QString &program, const QStringList &args, const QString &cwd) {
        call = {program, args, cwd};
        called = true;
        return true;
    });
    const auto resetLaunch = qScopeGuard([] { setLaunchHookForTests({}); });

    FileTab tab(root);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    QVERIFY2(!view->indexAt(QPoint(5, 400)).isValid(), "o ponto do menu deve estar na área vazia");

    runMenuAction(QStringList{QStringLiteral("Abrir terminal aqui")});
    openContextMenu(view);

    QTRY_VERIFY_WITH_TIMEOUT(called, 3000);
    QCOMPARE(call.program, QFileInfo(terminal).absoluteFilePath());
    QVERIFY2(call.args.isEmpty(), qPrintable(QStringLiteral("args: %1").arg(call.args.join(QLatin1Char(' ')))));
    QCOMPARE(call.cwd, root);
}

void FileTabTest::missingTerminalReportsStatus()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString bin = QDir(dir.path()).filePath(QStringLiteral("bin-empty"));
    QVERIFY(QDir().mkpath(bin));
    const QByteArray oldPath = qgetenv("PATH");
    qputenv("PATH", bin.toUtf8());
    const auto restorePath = qScopeGuard([oldPath] { qputenv("PATH", oldPath); });
    setLaunchHookForTests({});

    const QString root = contextTestRoot(dir);
    FileTab tab(root);
    QSignalSpy spy(&tab, &FileTab::statusMessage);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    QVERIFY2(!view->indexAt(QPoint(5, 400)).isValid(), "o ponto do menu deve estar na área vazia");

    runMenuAction(QStringList{QStringLiteral("Abrir terminal aqui")});
    openContextMenu(view);

    const auto reported = [&spy] {
        for (int i = 0; i < spy.count(); ++i)
            if (spy.at(i).value(0).toString() == QStringLiteral("Nenhum terminal disponível"))
                return true;
        return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(reported(), 3000);
}

void FileTabTest::propertiesOfCurrentFolder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    const QString sub = QDir(root).filePath(QStringLiteral("pasta-de-testes"));
    QVERIFY(QDir().mkpath(sub));

    FileTab tab(root);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(findRowNamed(view, QStringLiteral("pasta-de-testes")).isValid(), 3000);
    QVERIFY(tab.goTo(sub));

    bool seen = false;
    QString seenText;
    const auto inspect = [&seen, &seenText] {
        QMessageBox *box = qobject_cast<QMessageBox *>(qApp->activeModalWidget());
        if (!box) {
            for (QWidget *w : qApp->topLevelWidgets())
                if ((box = qobject_cast<QMessageBox *>(w)))
                    break;
        }
        if (!box) {
            qWarning("QMessageBox de propriedades não encontrado");
            return;
        }
        seen = true;
        seenText = box->text();
        QTest::keyClick(box, Qt::Key_Return);
    };

    runMenuAction(QStringList{QStringLiteral("Propriedades da pasta")},
                  [inspect] { QTimer::singleShot(0, inspect); });
    openContextMenu(view);

    QVERIFY2(seen, "a ação Propriedades da pasta deve abrir o diálogo");
    QVERIFY2(seenText.contains(QStringLiteral("pasta-de-testes")),
             qPrintable(QStringLiteral("texto: %1").arg(seenText)));
}

void FileTabTest::refreshRereadsWithoutWatcherAndKeepsHistory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    const QString sub = QDir(root).filePath(QStringLiteral("sub"));
    QVERIFY(QDir().mkpath(sub));

    FileTab tab(root);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    QVERIFY(tab.goTo(root));
    QVERIFY(tab.goTo(sub, true));

    auto *tree = tab.findChild<QTreeView *>();
    QVERIFY(tree);
    QListView *icons = nullptr;
    for (QListView *candidate : tab.findChildren<QListView *>())
        if (candidate->viewMode() == QListView::IconMode) { icons = candidate; break; }
    QVERIFY(icons);
    QTRY_VERIFY_WITH_TIMEOUT(tree->rootIndex().isValid(), 3000);

    const auto iconHas = [icons](const QString &name) {
        QAbstractItemModel *m = icons->model();
        const QModelIndex rootIdx = icons->rootIndex();
        for (int r = 0; r < m->rowCount(rootIdx); ++r)
            if (m->data(m->index(r, 0, rootIdx), Qt::DisplayRole).toString() == name)
                return true;
        return false;
    };

    // setRootPath(QString()) desliga o watcher, mas marca o nó de sub como sujo:
    // a visão faz fetchMore no evento seguinte e reinstala o watcher. Drenamos
    // esse fetch enquanto o diretório ainda está intacto e só então desligamos
    // os watchers de vez. Assim a alteração no FS não chega sozinha à visão.
    const auto freezeWatcher = [&](QFileSystemModel *m) -> bool {
        m->setRootPath(QString());
        const QModelIndex subIdx = m->index(sub);
        if (!subIdx.isValid())
            return false;
        bool listed = false;
        const QMetaObject::Connection conn = connect(m, &QFileSystemModel::directoryLoaded, &tab,
                                                     [&](const QString &dir) { if (dir == sub) listed = true; });
        m->fetchMore(subIdx);
        QElapsedTimer timer;
        timer.start();
        while (!listed && timer.elapsed() < 3000)
            QTest::qWait(10);
        disconnect(conn);
        if (!listed)
            return false;
        m->setOption(QFileSystemModel::DontWatchForChanges, true);
        return true;
    };

    auto *model = tab.findChild<QFileSystemModel *>();
    QVERIFY(model);
    QVERIFY2(freezeWatcher(model), "o watcher da pasta precisa ser desligado antes da alteração no FS");

    const QString created = QDir(sub).filePath(QStringLiteral("novo.txt"));
    {
        QFile f(created);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("conteudo\n");
    }
    QTest::qWait(250);
    QVERIFY2(!findRowNamed(tree, QStringLiteral("novo.txt")).isValid(),
             "sem Atualizar o arquivo novo não deve aparecer na lista detalhada");
    QVERIFY2(!iconHas(QStringLiteral("novo.txt")),
             "sem Atualizar o arquivo novo não deve aparecer na grade");

    runMenuAction(QStringList{QStringLiteral("Atualizar")});
    openContextMenu(tree);

    QTRY_VERIFY_WITH_TIMEOUT(findRowNamed(tree, QStringLiteral("novo.txt")).isValid(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(iconHas(QStringLiteral("novo.txt")), 3000);
    QCOMPARE(tab.currentPath(), QDir::cleanPath(sub));
    QVERIFY(tab.canGoBack());
    tab.back();
    QCOMPARE(tab.currentPath(), QDir::cleanPath(root));

    // Sentido inverso: remove o arquivo com o watcher do modelo atual desligado.
    tab.forward();
    QCOMPARE(tab.currentPath(), QDir::cleanPath(sub));
    auto *proxy = qobject_cast<ThumbnailProxy *>(tree->model());
    QVERIFY(proxy);
    auto *currentModel = qobject_cast<QFileSystemModel *>(proxy->sourceModel());
    QVERIFY(currentModel);
    QVERIFY2(freezeWatcher(currentModel), "o novo modelo também precisa ter o watcher desligado");
    QVERIFY(QFile::remove(created));
    QTest::qWait(250);
    QVERIFY2(findRowNamed(tree, QStringLiteral("novo.txt")).isValid(),
             "sem Atualizar o arquivo removido ainda deve constar na lista detalhada");
    QVERIFY2(iconHas(QStringLiteral("novo.txt")),
             "sem Atualizar o arquivo removido ainda deve constar na grade");

    runMenuAction(QStringList{QStringLiteral("Atualizar")});
    openContextMenu(tree);

    QTRY_VERIFY_WITH_TIMEOUT(!findRowNamed(tree, QStringLiteral("novo.txt")).isValid(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!iconHas(QStringLiteral("novo.txt")), 3000);
    QCOMPARE(tab.currentPath(), QDir::cleanPath(sub));
    QVERIFY(tab.canGoBack());
}

void FileTabTest::mimeHookResetRestoresAsyncQueries()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Sema um id obsoleto e confirma que o reset devolve as consultas reais.
    setMimeQueryForTests({QStringLiteral("stale.desktop")});
    clearMimeQueryForTestsForTests();

    const QString apps = QDir(dir.path()).filePath(QStringLiteral("applications"));
    const QString bin = QDir(dir.path()).filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(apps));
    QVERIFY(QDir().mkpath(bin));

    const QString desktop = writeDesktopFile(QDir(apps).filePath(QStringLiteral("real.editor.desktop")),
                                             QStringLiteral("Editor Real"),
                                             QStringLiteral("/bin/true %f"));
    QVERIFY(!desktop.isEmpty());

    {
        QFile xdg(QDir(bin).filePath(QStringLiteral("xdg-mime")));
        QVERIFY(xdg.open(QIODevice::WriteOnly));
        xdg.write("#!/bin/sh\nprintf 'real.editor.desktop\\n'\n");
    }
    {
        QFile gio(QDir(bin).filePath(QStringLiteral("gio")));
        QVERIFY(gio.open(QIODevice::WriteOnly));
        gio.write("#!/bin/sh\nprintf 'Registered applications:\\n\\treal.editor.desktop\\n"
                  "Recommended applications:\\n'\n");
    }
    for (const QString &name : { QStringLiteral("xdg-mime"), QStringLiteral("gio") }) {
        const QString script = QDir(bin).filePath(name);
        QVERIFY(QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                      | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther));
    }

    const QByteArray oldPath = qgetenv("PATH");
    qputenv("PATH", bin.toUtf8() + QByteArray(":") + oldPath);
    const auto restorePath = qScopeGuard([oldPath] { qputenv("PATH", oldPath); });
    const QByteArray oldXdg = qgetenv("XDG_DATA_HOME");
    qputenv("XDG_DATA_HOME", QDir(dir.path()).absolutePath().toUtf8());
    const auto restoreXdg = qScopeGuard([oldXdg] { qputenv("XDG_DATA_HOME", oldXdg); });

    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(root));
    const QString file = QDir(root).filePath(QStringLiteral("consulta real.txt"));
    { QFile f(file); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("conteúdo\n"); }

    FileTab tab(root);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    const QModelIndex row = findRowNamed(view, QFileInfo(file).fileName());
    QVERIFY(row.isValid());

    QStringList labels;
    bool done = false;
    setContextMenuHookForTests([&labels, &done](QMenu *menu, const QStringList &) {
        const QPointer<QMenu> popup = menu;
        auto checks = std::make_shared<int>(0);
        for (int i = 1; i <= 100; ++i) {
            QTimer::singleShot(i * 20, popup.data(), [popup, &labels, &done, checks] {
                if (done || !popup)
                    return;
                if (++(*checks) >= 100) {
                    done = true;
                    QTimer::singleShot(0, popup.data(), [] { closePopups(); });
                    return;
                }
                QAction *openWith = actionNamed(popup.data(), QStringLiteral("Abrir com…"));
                if (!openWith || !openWith->menu())
                    return;
                if (actionNamed(openWith->menu(), QStringLiteral("Editor Real"))) {
                    labels = collectMenuLabels(openWith->menu());
                    done = true;
                    QTimer::singleShot(0, popup.data(), [] { closePopups(); });
                }
            });
        }
    });
    const auto resetContext = qScopeGuard([] { setContextMenuHookForTests({}); });

    openContextMenu(view, row);

    QVERIFY2(done, "as consultas assíncronas devem terminar enquanto o menu está aberto");
    QVERIFY2(labels.contains(QStringLiteral("Editor Real")),
             qPrintable(QStringLiteral("roteiros: %1").arg(labels.join(QLatin1String(" | ")))));
    QVERIFY(labels.contains(QStringLiteral("Escolher executável…")));
}

void FileTabTest::extractionFailureDoesNotPublishFolder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(root));
    const QString archive = QDir(root).filePath(QStringLiteral("arquivo.zip"));
    { QFile f(archive); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("fake"); }

    const QString bin = QDir(dir.path()).filePath(QStringLiteral("bin"));
    installFakeBsdtar(bin, QStringLiteral("fail"));
    const QByteArray oldPath = qgetenv("PATH");
    qputenv("PATH", bin.toUtf8() + QByteArray(":") + oldPath);
    QScopeGuard restorePath([oldPath] { qputenv("PATH", oldPath); });

    QString status;
    FileTab tab(root);
    QObject::connect(&tab, &FileTab::statusMessage, &tab, [&status](const QString &s) { status = s; });
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(findRowNamed(view, QStringLiteral("arquivo.zip")).isValid(), 3000);

    triggerOnRow(view, findRowNamed(view, QStringLiteral("arquivo.zip")), QStringLiteral("Extrair em pasta própria"));
    QTRY_VERIFY_WITH_TIMEOUT(status.startsWith(QStringLiteral("Falhou:")), 5000);
    QVERIFY2(!QFileInfo::exists(QDir(root).filePath(QStringLiteral("arquivo"))),
             "falha não deve publicar a pasta final");
    QVERIFY2(scratchDirs(root).isEmpty(), "falha deve remover o temporário");
}

void FileTabTest::extractionCancelRemovesTemporaryFolder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(root));
    const QString archive = QDir(root).filePath(QStringLiteral("arquivo.zip"));
    { QFile f(archive); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("fake"); }

    const QString bin = QDir(dir.path()).filePath(QStringLiteral("bin"));
    installFakeBsdtar(bin, QStringLiteral("slow"));
    const QByteArray oldPath = qgetenv("PATH");
    qputenv("PATH", bin.toUtf8() + QByteArray(":") + oldPath);
    QScopeGuard restorePath([oldPath] { qputenv("PATH", oldPath); });

    QString status;
    FileTab tab(root);
    QObject::connect(&tab, &FileTab::statusMessage, &tab, [&status](const QString &s) { status = s; });
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(findRowNamed(view, QStringLiteral("arquivo.zip")).isValid(), 3000);

    triggerOnRow(view, findRowNamed(view, QStringLiteral("arquivo.zip")), QStringLiteral("Extrair em pasta própria"));
    QProgressDialog *dlg = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        for (QObject *o : tab.findChildren<QObject *>())
            if ((dlg = qobject_cast<QProgressDialog *>(o)))
                return true;
        return false;
    }(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!scratchDirs(root).isEmpty(), 3000);
    const QString scratch = QDir(root).filePath(scratchDirs(root).first());
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(QDir(scratch).filePath(QStringLiteral("parcial.txt"))), 3000);
    bool clicked = false;
    for (QPushButton *b : dlg->findChildren<QPushButton *>())
        if (b->text() == QStringLiteral("Cancelar")) {
            b->click();
            clicked = true;
        }
    QVERIFY(clicked);
    QTRY_VERIFY_WITH_TIMEOUT(status == QStringLiteral("Extração cancelada"), 5000);
    QVERIFY2(!QFileInfo::exists(QDir(root).filePath(QStringLiteral("arquivo"))),
             "cancelamento não deve publicar a pasta final");
    QVERIFY2(scratchDirs(root).isEmpty(), "cancelamento deve remover o temporário");
}

void FileTabTest::extractionSuccessPublishesFolder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(root));
    const QString archive = QDir(root).filePath(QStringLiteral("arquivo.zip"));
    { QFile f(archive); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("fake"); }

    const QString bin = QDir(dir.path()).filePath(QStringLiteral("bin"));
    installFakeBsdtar(bin, QStringLiteral("ok"));
    const QByteArray oldPath = qgetenv("PATH");
    qputenv("PATH", bin.toUtf8() + QByteArray(":") + oldPath);
    QScopeGuard restorePath([oldPath] { qputenv("PATH", oldPath); });

    FileTab tab(root);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(findRowNamed(view, QStringLiteral("arquivo.zip")).isValid(), 3000);

    triggerOnRow(view, findRowNamed(view, QStringLiteral("arquivo.zip")), QStringLiteral("Extrair em pasta própria"));
    const QString published = QDir(root).filePath(QStringLiteral("arquivo/parcial.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(published), 5000);
    QFile f(published);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("parcial\n"));
    QVERIFY2(scratchDirs(root).isEmpty(), "sucesso não deve deixar temporário");
}

void FileTabTest::extractHereFailureLeavesCurrentFolderUntouched()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(root));
    const QString archive = QDir(root).filePath(QStringLiteral("arquivo.zip"));
    { QFile f(archive); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("fake"); }

    const QString bin = QDir(dir.path()).filePath(QStringLiteral("bin"));
    installFakeBsdtar(bin, QStringLiteral("fail"));
    const QByteArray oldPath = qgetenv("PATH");
    qputenv("PATH", bin.toUtf8() + QByteArray(":") + oldPath);
    QScopeGuard restorePath([oldPath] { qputenv("PATH", oldPath); });

    QString status;
    FileTab tab(root);
    QObject::connect(&tab, &FileTab::statusMessage, &tab, [&status](const QString &s) { status = s; });
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(findRowNamed(view, QStringLiteral("arquivo.zip")).isValid(), 3000);

    triggerOnRow(view, findRowNamed(view, QStringLiteral("arquivo.zip")), QStringLiteral("Extrair aqui"));
    QTRY_VERIFY_WITH_TIMEOUT(status.startsWith(QStringLiteral("Falhou:")), 5000);
    QVERIFY2(!QFileInfo::exists(QDir(root).filePath(QStringLiteral("parcial.txt"))),
             "falha em Extrair aqui não deve deixar arquivo parcial na pasta atual");
    QVERIFY2(scratchDirs(root).isEmpty(), "falha em Extrair aqui deve remover o temporário");
}

void FileTabTest::extractHerePublishesFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(root));
    const QString archive = QDir(root).filePath(QStringLiteral("arquivo.zip"));
    { QFile f(archive); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("fake"); }

    const QString bin = QDir(dir.path()).filePath(QStringLiteral("bin"));
    installFakeBsdtar(bin, QStringLiteral("ok"));
    const QByteArray oldPath = qgetenv("PATH");
    qputenv("PATH", bin.toUtf8() + QByteArray(":") + oldPath);
    QScopeGuard restorePath([oldPath] { qputenv("PATH", oldPath); });

    FileTab tab(root);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(findRowNamed(view, QStringLiteral("arquivo.zip")).isValid(), 3000);

    triggerOnRow(view, findRowNamed(view, QStringLiteral("arquivo.zip")), QStringLiteral("Extrair aqui"));
    const QString published = QDir(root).filePath(QStringLiteral("parcial.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(published), 5000);
    QFile f(published);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("parcial\n"));
    QVERIFY2(scratchDirs(root).isEmpty(), "sucesso em Extrair aqui não deve deixar temporário");
}

void FileTabTest::extractHereRollsBackWhenPublishFails()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(root));
    const QString archive = QDir(root).filePath(QStringLiteral("arquivo.zip"));
    { QFile f(archive); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("fake"); }
    const QString blocking = QDir(root).filePath(QStringLiteral("b.txt"));
    QVERIFY(QDir().mkpath(blocking));
    { QFile f(QDir(blocking).filePath(QStringLiteral("filho.txt")));
      QVERIFY(f.open(QIODevice::WriteOnly)); f.write("original\n"); }
    QVERIFY(QFile::setPermissions(blocking, QFile::ReadOwner | QFile::ExeOwner | QFile::ReadGroup
                                  | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther));
    QScopeGuard restorePerms([blocking] {
        QFile::setPermissions(blocking, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                              | QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup
                              | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther);
    });

    const QString bin = QDir(dir.path()).filePath(QStringLiteral("bin"));
    installFakeBsdtar(bin, QStringLiteral("two"));
    const QByteArray oldPath = qgetenv("PATH");
    qputenv("PATH", bin.toUtf8() + QByteArray(":") + oldPath);
    QScopeGuard restorePath([oldPath] { qputenv("PATH", oldPath); });

    QString status;
    FileTab tab(root);
    QObject::connect(&tab, &FileTab::statusMessage, &tab, [&status](const QString &s) { status = s; });
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(findRowNamed(view, QStringLiteral("arquivo.zip")).isValid(), 3000);

    triggerOnRow(view, findRowNamed(view, QStringLiteral("arquivo.zip")), QStringLiteral("Extrair aqui"));
    QTRY_VERIFY_WITH_TIMEOUT(status == QStringLiteral("Não foi possível publicar a extração"), 5000);
    QVERIFY2(!QFileInfo::exists(QDir(root).filePath(QStringLiteral("a.txt"))),
             "publicação parcial deve ser desfeita");
    QFile f(QDir(blocking).filePath(QStringLiteral("filho.txt")));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("original\n"));
    QVERIFY(scratchDirs(root).isEmpty());
}

void FileTabTest::extractHereUsesOriginalDestination()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    const QString sub = QDir(root).filePath(QStringLiteral("sub"));
    QVERIFY(QDir().mkpath(sub));
    const QString archive = QDir(root).filePath(QStringLiteral("arquivo.zip"));
    { QFile f(archive); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("fake"); }

    const QString bin = QDir(dir.path()).filePath(QStringLiteral("bin"));
    installFakeBsdtar(bin, QStringLiteral("later"));
    const QByteArray oldPath = qgetenv("PATH");
    qputenv("PATH", bin.toUtf8() + QByteArray(":") + oldPath);
    QScopeGuard restorePath([oldPath] { qputenv("PATH", oldPath); });

    FileTab tab(root);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(findRowNamed(view, QStringLiteral("arquivo.zip")).isValid(), 3000);

    triggerOnRow(view, findRowNamed(view, QStringLiteral("arquivo.zip")), QStringLiteral("Extrair aqui"));
    tab.goTo(sub);
    const QString rootFile = QDir(root).filePath(QStringLiteral("parcial.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(rootFile), 5000);
    QVERIFY2(!QFileInfo::exists(QDir(sub).filePath(QStringLiteral("parcial.txt"))),
             "a extração deve publicar na pasta de origem, não na pasta atual");
    QVERIFY(scratchDirs(root).isEmpty());
}

void FileTabTest::extractHereCancelRemovesTemporaryFolder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(root));
    const QString archive = QDir(root).filePath(QStringLiteral("arquivo.zip"));
    { QFile f(archive); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("fake"); }

    const QString bin = QDir(dir.path()).filePath(QStringLiteral("bin"));
    installFakeBsdtar(bin, QStringLiteral("slow"));
    const QByteArray oldPath = qgetenv("PATH");
    qputenv("PATH", bin.toUtf8() + QByteArray(":") + oldPath);
    QScopeGuard restorePath([oldPath] { qputenv("PATH", oldPath); });

    QString status;
    FileTab tab(root);
    QObject::connect(&tab, &FileTab::statusMessage, &tab, [&status](const QString &s) { status = s; });
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(findRowNamed(view, QStringLiteral("arquivo.zip")).isValid(), 3000);

    triggerOnRow(view, findRowNamed(view, QStringLiteral("arquivo.zip")), QStringLiteral("Extrair aqui"));
    QProgressDialog *dlg = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        for (QObject *o : tab.findChildren<QObject *>())
            if ((dlg = qobject_cast<QProgressDialog *>(o)))
                return true;
        return false;
    }(), 3000);
    bool clicked = false;
    for (QPushButton *b : dlg->findChildren<QPushButton *>())
        if (b->text() == QStringLiteral("Cancelar")) {
            b->click();
            clicked = true;
        }
    QVERIFY(clicked);
    QTRY_VERIFY_WITH_TIMEOUT(status == QStringLiteral("Extração cancelada"), 5000);
    QVERIFY2(!QFileInfo::exists(QDir(root).filePath(QStringLiteral("parcial.txt"))),
             "cancelamento em Extrair aqui não deve publicar arquivo");
    QVERIFY(scratchDirs(root).isEmpty());
}

void FileTabTest::extractHereKeepsBackupWhenRestoreFails()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(root));
    const QString archive = QDir(root).filePath(QStringLiteral("arquivo.zip"));
    { QFile f(archive); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("fake"); }
    const QString original = QDir(root).filePath(QStringLiteral("a.txt"));
    { QFile f(original); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("original\n"); }
    const QString blocking = QDir(root).filePath(QStringLiteral("b.txt"));
    QVERIFY(QDir().mkpath(blocking));
    { QFile f(QDir(blocking).filePath(QStringLiteral("filho.txt")));
      QVERIFY(f.open(QIODevice::WriteOnly)); f.write("original-b\n"); }
    QVERIFY(QFile::setPermissions(blocking, QFile::ReadOwner | QFile::ExeOwner | QFile::ReadGroup
                                  | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther));
    QScopeGuard restorePerms([blocking] {
        QFile::setPermissions(blocking, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                              | QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup
                              | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther);
    });
    setRestoreBackupHookForTests([](const QString &, const QString &) { return false; });
    QScopeGuard clearHook([] { setRestoreBackupHookForTests({}); });

    const QString bin = QDir(dir.path()).filePath(QStringLiteral("bin"));
    installFakeBsdtar(bin, QStringLiteral("two"));
    const QByteArray oldPath = qgetenv("PATH");
    qputenv("PATH", bin.toUtf8() + QByteArray(":") + oldPath);
    QScopeGuard restorePath([oldPath] { qputenv("PATH", oldPath); });

    QStringList statuses;
    FileTab tab(root);
    QObject::connect(&tab, &FileTab::statusMessage, &tab,
                     [&statuses](const QString &s) { statuses.append(s); });
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));
    auto *view = tab.findChild<QTreeView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(findRowNamed(view, QStringLiteral("arquivo.zip")).isValid(), 3000);
    triggerOnRow(view, findRowNamed(view, QStringLiteral("arquivo.zip")), QStringLiteral("Extrair aqui"));
    QTRY_VERIFY_WITH_TIMEOUT(statuses.contains(
                                 QStringLiteral("Não foi possível restaurar o original; cópia mantida em %1")
                                     .arg(QDir(QDir(root).filePath(
                                                   scratchDirs(root).isEmpty() ? QString() : scratchDirs(root).first()))
                                              .filePath(QStringLiteral(".fm-backup-a.txt")))), 5000);
    QVERIFY2(!QFileInfo::exists(original), "original não restaurado não deve parecer publicado");
    const QStringList scratch = scratchDirs(root);
    QCOMPARE(scratch.size(), 1);
    const QString scratchDir = QDir(root).filePath(scratch.first());
    const QStringList backups = QDir(scratchDir).entryList(QDir::Files | QDir::Hidden, QDir::Name);
    QString backupPath;
    for (const QString &name : backups)
        if (name.startsWith(QStringLiteral(".fm-backup-")))
            backupPath = QDir(scratchDir).filePath(name);
    QVERIFY2(!backupPath.isEmpty(), "backup do original deve ser preservado");
    QFile f(backupPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("original\n"));
}

void FileTabTest::closingTabStopsExtractionBeforeCleanup()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(root));
    const QString archive = QDir(root).filePath(QStringLiteral("arquivo.zip"));
    { QFile f(archive); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("fake"); }

    const QString bin = QDir(dir.path()).filePath(QStringLiteral("bin"));
    installFakeBsdtar(bin, QStringLiteral("shortslow"));
    const QByteArray oldPath = qgetenv("PATH");
    qputenv("PATH", bin.toUtf8() + QByteArray(":") + oldPath);
    QScopeGuard restorePath([oldPath] { qputenv("PATH", oldPath); });

    auto *tab = new FileTab(root);
    tab->show();
    QVERIFY(QTest::qWaitForWindowExposed(tab));
    auto *view = tab->findChild<QTreeView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(findRowNamed(view, QStringLiteral("arquivo.zip")).isValid(), 3000);

    triggerOnRow(view, findRowNamed(view, QStringLiteral("arquivo.zip")), QStringLiteral("Extrair aqui"));
    QTRY_VERIFY_WITH_TIMEOUT(!scratchDirs(root).isEmpty(), 3000);
    QElapsedTimer elapsed;
    elapsed.start();
    delete tab;
    QVERIFY2(elapsed.elapsed() < 2500, "fechar a aba deve interromper o processo, não esperar 3 s");
    QTRY_VERIFY_WITH_TIMEOUT(scratchDirs(root).isEmpty(), 3000);
}

void FileTabTest::closingTabKeepsScratchWhenProcessDoesNotStop()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = QDir(dir.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(root));
    const QString archive = QDir(root).filePath(QStringLiteral("arquivo.zip"));
    { QFile f(archive); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("fake"); }

    const QString bin = QDir(dir.path()).filePath(QStringLiteral("bin"));
    installFakeBsdtar(bin, QStringLiteral("slow"));
    const QByteArray oldPath = qgetenv("PATH");
    qputenv("PATH", bin.toUtf8() + QByteArray(":") + oldPath);
    QScopeGuard restorePath([oldPath] { qputenv("PATH", oldPath); });

    bool hookCalled = false;
    bool hookSawRunning = false;
    setWaitForProcessHookForTests([&](QProcess *proc, int msecs) -> bool {
        hookCalled = true;
        hookSawRunning = msecs == 2000 && proc->state() != QProcess::NotRunning;
        return false;
    });
    QScopeGuard clearHook([] { setWaitForProcessHookForTests({}); });

    auto *tab = new FileTab(root);
    tab->show();
    QVERIFY(QTest::qWaitForWindowExposed(tab));
    auto *view = tab->findChild<QTreeView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(findRowNamed(view, QStringLiteral("arquivo.zip")).isValid(), 3000);
    triggerOnRow(view, findRowNamed(view, QStringLiteral("arquivo.zip")), QStringLiteral("Extrair aqui"));
    QTRY_VERIFY_WITH_TIMEOUT(!scratchDirs(root).isEmpty(), 3000);
    delete tab;
    QVERIFY(hookCalled);
    QVERIFY(hookSawRunning);
    QVERIFY2(!scratchDirs(root).isEmpty(), "processo ativo: o temporário não pode ser removido");
}

void FileTabTest::zoomChangesIconAndGridSize()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FileTab tab(dir.path());
    QListView *icons = nullptr;
    for (QListView *view : tab.findChildren<QListView *>())
        if (view->viewMode() == QListView::IconMode)
            icons = view;
    QVERIFY(icons);

    QCOMPARE(tab.zoom(), 100);
    tab.setZoom(150);
    QCOMPARE(tab.zoom(), 150);
    QVERIFY(icons->iconSize().width() > 72);
    QVERIFY(icons->gridSize().width() > 112);
}

void FileTabTest::zoomClampsAndSurvivesModeSwitch()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FileTab tab(dir.path());

    tab.setZoom(500);
    QCOMPARE(tab.zoom(), 200);
    tab.setZoom(10);
    QCOMPARE(tab.zoom(), 50);

    tab.setZoom(150);
    tab.setIconMode(false);
    tab.setIconMode(true);
    QCOMPARE(tab.zoom(), 150);
}

void FileTabTest::viewRefinementsAreApplied()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FileTab tab(dir.path());

    auto *tree = tab.findChild<QTreeView *>();
    QVERIFY(tree);
    QCOMPARE(tree->header()->sectionResizeMode(0), QHeaderView::Stretch);
    QVERIFY(tree->alternatingRowColors());

    QListView *icons = nullptr;
    for (QListView *view : tab.findChildren<QListView *>())
        if (view->viewMode() == QListView::IconMode)
            icons = view;
    QVERIFY(icons);
    QCOMPARE(icons->spacing(), 8);

    auto *places = tab.findChild<QListWidget *>(QStringLiteral("places"));
    QVERIFY(places);
    QVERIFY(places->minimumWidth() >= 190);

    for (int col : {1, 2, 3})
        QVERIFY2(tree->model()->headerData(col, Qt::Horizontal, Qt::TextAlignmentRole).toInt() & Qt::AlignRight,
                 qPrintable(QString("coluna %1 sem alinhamento a direita").arg(col)));
}

void FileTabTest::zoomResetRestoresInitialSizes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FileTab tab(dir.path());

    auto *tree = tab.findChild<QTreeView *>();
    QVERIFY(tree);
    QListView *icons = nullptr;
    for (QListView *view : tab.findChildren<QListView *>())
        if (view->viewMode() == QListView::IconMode)
            icons = view;
    QVERIFY(icons);

    const QSize initialTreeIcon = tree->iconSize();
    const QSize initialGrid = icons->gridSize();
    QVERIFY(!initialGrid.isEmpty());

    tab.setZoom(150);
    QVERIFY(icons->gridSize() != initialGrid);

    tab.setZoom(100);
    QCOMPARE(tree->iconSize(), initialTreeIcon);
    QCOMPARE(icons->gridSize(), initialGrid);
}

void FileTabTest::sizeColumnCellsAreRightAligned()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString file = QDir(dir.path()).filePath(QStringLiteral("medicao.txt"));
    QFile f(file);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write("1234567890"), 10);
    f.close();

    FileTab tab(dir.path());
    auto *tree = tab.findChild<QTreeView *>();
    QVERIFY(tree);
    auto *model = tree->model();
    QVERIFY(model);

    const QModelIndex root = tree->rootIndex();
    QVERIFY(root.isValid());
    QModelIndex sizeIndex;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        for (int row = 0; row < model->rowCount(root); ++row) {
            if (model->index(row, 0, root).data(Qt::DisplayRole).toString() == QStringLiteral("medicao.txt")) {
                sizeIndex = model->index(row, 1, root);
                return true;
            }
        }
        return false;
    }(), 2000);
    QVERIFY(sizeIndex.isValid());
    QVERIFY2(!sizeIndex.data(Qt::DisplayRole).toString().isEmpty(), "celula de tamanho vazia");
    QVERIFY2(sizeIndex.data(Qt::TextAlignmentRole).toInt() & Qt::AlignRight,
             "valor de tamanho precisa estar alinhado a direita");
}

void FileTabTest::presetChangesIconAndColumns()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (int i = 0; i < 5; ++i) {
        QFile f(QDir(dir.path()).filePath(QStringLiteral("arquivo-%1.txt").arg(i)));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
    }

    FileTab tab(dir.path());
    auto *tree = tab.findChild<QTreeView *>();
    QVERIFY(tree);
    QListView *icons = nullptr;
    for (QListView *view : tab.findChildren<QListView *>())
        if (view->viewMode() == QListView::IconMode)
            icons = view;
    QVERIFY(icons);

    tab.applyUiPreferences(defaultUiPreferences(UiPreset::Compact));
    QCOMPARE(icons->iconSize(), QSize(32, 32));
    QVERIFY(!tree->header()->isSectionHidden(0));
    for (int col = 1; col <= 3; ++col)
        QVERIFY(tree->header()->isSectionHidden(col));
    QVERIFY(icons->gridSize().width() > icons->iconSize().width());

    tab.applyUiPreferences(defaultUiPreferences(UiPreset::Advanced));
    QCOMPARE(icons->iconSize(), QSize(64, 64));
    for (int col = 0; col <= 3; ++col)
        QVERIFY(!tree->header()->isSectionHidden(col));
}

void FileTabTest::densityChangesRowHeight()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile f(QDir(dir.path()).filePath(QStringLiteral("linha.txt")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();

    FileTab tab(dir.path());
    auto *tree = tab.findChild<QTreeView *>();
    QVERIFY(tree);
    QTRY_VERIFY_WITH_TIMEOUT(tree->model()->rowCount(tree->rootIndex()) >= 1, 3000);
    const QModelIndex index = tree->model()->index(0, 0, tree->rootIndex());
    QVERIFY(index.isValid());
    const auto rowHeight = [&] {
        return tree->itemDelegate()->sizeHint(QStyleOptionViewItem(), index).height();
    };

    UiPreferences compact = defaultUiPreferences(UiPreset::Compact);
    tab.applyUiPreferences(compact);
    const int compactHeight = rowHeight();
    UiPreferences advanced = defaultUiPreferences(UiPreset::Advanced);
    tab.applyUiPreferences(advanced);
    QVERIFY2(rowHeight() > compactHeight, "densidade alta e baixa precisam produzir alturas distintas");
}

void FileTabTest::previewFollowsPreferences()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FileTab tab(dir.path());
    auto *preview = tab.findChild<QWidget *>(QStringLiteral("previewPanel"));
    QVERIFY(preview);

    tab.applyUiPreferences(defaultUiPreferences(UiPreset::Advanced));
    QVERIFY(!preview->isHidden());
    tab.applyUiPreferences(defaultUiPreferences(UiPreset::Balanced));
    QVERIFY(preview->isHidden());
}

void FileTabTest::sectionHeadersAreNotPlaces()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString parent = dir.path();
    const QString child = QDir(parent).filePath(QStringLiteral("filho"));
    QVERIFY(QDir(parent).mkdir(QStringLiteral("filho")));
    setPlacesForTests(QStringList{parent, child});
    auto restore = qScopeGuard([] { setPlacesForTests(QStringList{}); });

    FileTab tab(child);
    tab.applyUiPreferences(defaultUiPreferences(UiPreset::Balanced));
    QVERIFY(tab.goTo(child));

    auto *places = tab.findChild<QListWidget *>(QStringLiteral("places"));
    QVERIFY(places);
    QVERIFY(places->currentItem());
    QCOMPARE(places->currentItem()->data(Qt::UserRole).toString(), child);
    QVERIFY(!places->currentItem()->data(FileTab::PlaceSectionRole).isValid());

    int sections = 0;
    for (int i = 0; i < places->count(); ++i)
        if (places->item(i)->data(FileTab::PlaceSectionRole).isValid())
            ++sections;
    QVERIFY2(sections >= 2, "grupos da lateral precisam existir como seções");
}

void FileTabTest::menuShortcutsByPreset()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(QDir(dir.path()).filePath(QStringLiteral("alpha.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    FileTab tab(dir.path());
    auto *tree = tab.findChild<QTreeView *>();
    QVERIFY(tree);
    QTRY_VERIFY_WITH_TIMEOUT(tree->model()->rowCount(tree->rootIndex()) >= 1, 3000);
    const QModelIndex row = findRowNamed(tree, QStringLiteral("alpha.txt"));
    QVERIFY(row.isValid());

    tab.applyUiPreferences(defaultUiPreferences(UiPreset::Compact));
    const MenuSnapshot compact = captureMenuSnapshot(tab, tree, row);
    for (const QString &label : compact.labels)
        QVERIFY2(!label.contains(QLatin1Char('\t')), qPrintable("atalho na Compacta: " + label));

    for (UiPreset preset : {UiPreset::Balanced, UiPreset::Advanced}) {
        tab.applyUiPreferences(defaultUiPreferences(preset));
        const MenuSnapshot snap = captureMenuSnapshot(tab, tree, row);
        bool openHint = false;
        bool propsHint = false;
        for (const QString &label : snap.labels) {
            if (label.startsWith(QStringLiteral("Abrir")))
                openHint = openHint || label.endsWith(QStringLiteral("\tEnter"));
            if (label.startsWith(QStringLiteral("Propriedades")))
                propsHint = propsHint || label.endsWith(QStringLiteral("\tAlt+Enter"));
        }
        QVERIFY2(openHint, "Abrir sem dica de atalho");
        QVERIFY2(propsHint, "Propriedades sem dica de atalho");
    }
}

void FileTabTest::menuTargetsUnchanged()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(QDir(dir.path()).filePath(QStringLiteral("alpha.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("pasta")));

    FileTab tab(dir.path());
    auto *tree = tab.findChild<QTreeView *>();
    QVERIFY(tree);
    QTRY_VERIFY_WITH_TIMEOUT(tree->model()->rowCount(tree->rootIndex()) >= 2, 3000);
    QVERIFY(findRowNamed(tree, QStringLiteral("alpha.txt")).isValid());
    QVERIFY(findRowNamed(tree, QStringLiteral("pasta")).isValid());

    struct Target { QString name; QString fileName; bool emptyArea = false; bool multi = false; };
    const QVector<Target> targets{
        {QStringLiteral("arquivo"), QStringLiteral("alpha.txt")},
        {QStringLiteral("pasta"), QStringLiteral("pasta")},
        {QStringLiteral("seleção múltipla"), QString(), false, true},
        {QStringLiteral("área vazia"), QString(), true},
    };
    QHash<QString, QStringList> baseline;

    for (UiPreset preset : {UiPreset::Balanced, UiPreset::Compact, UiPreset::Advanced}) {
        tab.applyUiPreferences(defaultUiPreferences(preset));
        for (const Target &target : targets) {
            tree->clearSelection();
            if (target.multi) {
                const QItemSelection multi(findRowNamed(tree, QStringLiteral("alpha.txt")),
                                           findRowNamed(tree, QStringLiteral("pasta")));
                tree->selectionModel()->select(multi, QItemSelectionModel::ClearAndSelect);
            } else if (!target.emptyArea) {
                tree->selectionModel()->select(findRowNamed(tree, target.fileName),
                                               QItemSelectionModel::ClearAndSelect);
            }
            const QModelIndex row = target.emptyArea
                                        ? QModelIndex()
                                        : findRowNamed(tree, target.multi ? QStringLiteral("alpha.txt")
                                                                          : target.fileName);
            QVERIFY2(target.emptyArea || row.isValid(), qPrintable("linha ausente: " + target.name));
            const MenuSnapshot snap = captureMenuSnapshot(tab, tree, row);
            const QStringList labels = stripShortcuts(snap.labels);
            const QString key = target.name;
            if (preset == UiPreset::Balanced) {
                baseline[key] = labels;
                QVERIFY2(labels.contains(QStringLiteral("Colar")), "Colar ausente");
            } else {
                QCOMPARE(labels, baseline.value(key));
            }
            QCOMPARE(snap.pasteData, QDir::cleanPath(tab.currentPath()));
        }
    }
}

QTEST_MAIN(FileTabTest)
#include "filetab_test.moc"
