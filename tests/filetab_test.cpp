#include <QtTest>

#include <functional>

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileSystemModel>
#include <QImage>
#include <QInputDialog>
#include <QLineEdit>
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

} // namespace

class FileTabTest : public QObject
{
    Q_OBJECT

private slots:
    void navigationBackForwardUp();
    void thumbnailIsDeliveredForPng();
    void contextMenuCreateCopyPasteRename();
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

QTEST_MAIN(FileTabTest)
#include "filetab_test.moc"
