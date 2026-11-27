#include <QtTest>

#include <functional>

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileSystemModel>
#include <QImage>
#include <QInputDialog>
#include <QLineEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QScopeGuard>
#include <QMenu>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeView>

#include "FileTab.h"
#include "ThumbnailProxy.h"

namespace {

QAction *actionNamed(QMenu *menu, const QString &text)
{
    if (!menu)
        return nullptr;
    for (QAction *a : menu->actions())
        if (a->text() == text)
            return a;
    return nullptr;
}

void acceptNewFileName()
{
    QInputDialog *dlg = nullptr;
    for (QWidget *w : qApp->topLevelWidgets())
        if ((dlg = qobject_cast<QInputDialog *>(w)))
            break;
    if (!dlg) {
        qWarning("QInputDialog do 'Novo arquivo' não encontrado");
        return;
    }
    dlg->setTextValue(QStringLiteral("manual-test.txt"));
    dlg->accept();
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

void openContextMenu(QTreeView *view, const QModelIndex &index = QModelIndex())
{
    // QWidget de viewport recebe o QContextMenuEvent; o QTreeView emite customContextMenuRequested.
    const QPoint vp = index.isValid() ? view->visualRect(index).center() : QPoint(5, 400);
    // FileTab converte pos de coordenadas da view para o viewport; informar pos já na view.
    const QPoint at = view->viewport()->mapTo(view, vp);
    QContextMenuEvent ev(QContextMenuEvent::Mouse, at, view->viewport()->mapToGlobal(vp));
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
        "case \"%1\" in\n"
        "  ok) exit 0 ;;\n"
        "  fail) echo erro-simulado >&2; exit 1 ;;\n"
        "  slow) exec sleep 30 ;;\n"
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

} // namespace

class FileTabTest : public QObject
{
    Q_OBJECT

private slots:
    void navigationBackForwardUp();
    void thumbnailIsDeliveredForPng();
    void contextMenuCreateCopyPasteRename();
    void extractionFailureDoesNotPublishFolder();
    void extractionCancelRemovesTemporaryFolder();
    void extractionSuccessPublishesFolder();
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

    // 1) menu de contexto: Novo > Arquivo de texto (.txt), nome manual-test.txt
    runMenuAction(QStringList{ QStringLiteral("Novo"), QStringLiteral("Arquivo de texto (.txt)") },
                  {}, acceptNewFileName);
    openContextMenu(view);
    const QString created = QDir(root).filePath(QStringLiteral("manual-test.txt"));
    QVERIFY2(QFileInfo::exists(created), "menu Novo > Arquivo de texto (.txt) deve criar o arquivo");
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
        for (QWidget *w : qApp->topLevelWidgets())
            if ((dlg = qobject_cast<QProgressDialog *>(w)))
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
    QVERIFY2(scratchDirs(root).isEmpty(), "sucesso não deve deixar temporário");
}

QTEST_MAIN(FileTabTest)
#include "filetab_test.moc"
