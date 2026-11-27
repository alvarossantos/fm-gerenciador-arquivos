#pragma once
#include <QModelIndex>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <memory>

class QAbstractItemView;
class QFileSystemModel;
class QLabel;
class QListView;
class QListWidget;
class QProcess;
class QProgressDialog;
class QStackedWidget;
class QTreeView;
class ThumbnailProxy;

using RestoreBackupHook = std::function<bool(const QString &, const QString &)>;
void setRestoreBackupHookForTests(RestoreBackupHook hook);

class FileTab : public QWidget {
    Q_OBJECT
public:
    explicit FileTab(const QString &startPath, QWidget *parent = nullptr);

    QString currentPath() const { return m_path; }
    bool canGoBack() const { return m_histIdx > 0; }
    bool canGoForward() const { return m_histIdx < m_history.size() - 1; }

    void goTo(const QString &path, bool addHistory = true);
    void back();
    void forward();
    void up();
    void setIconMode(bool icons);
    void setPreviewVisible(bool visible);
    void setShowHidden(bool show);

signals:
    void pathChanged(const QString &path);
    void titleChanged(const QString &title);
    void statusMessage(const QString &text);
    void openInNewTab(const QString &path);

private:
    QAbstractItemView *currentView() const;
    QStringList selectedPaths() const;
    QString pathOf(const QModelIndex &proxyIndex) const;
    void openPath(const QString &path);
    void showContextMenu(const QPoint &pos);
    void onSelectionChanged();
    void updatePreview();

    void copyToClipboard(bool cut);
    void paste();
    void trashSelected();
    void deleteSelected();
    void renameCurrent();
    void showProperties(const QString &path);

    void newFolder();
    void newFile(const QString &suggested, const QString &content = QString(), bool executable = false);
    void newFromTemplate(const QString &templatePath);

    void compress(const QStringList &paths);
    void extract(const QString &archive, bool intoFolder);
    QProcess *runBsdtar(const QStringList &args, const QString &okMessage,
                        QProgressDialog *progress = nullptr, const QString &scratch = QString(),
                        std::function<bool()> publish = {},
                        const std::shared_ptr<bool> &keepScratch = {});

    QFileSystemModel *m_model;
    ThumbnailProxy *m_proxy;
    QTreeView *m_tree;
    QListView *m_icons;
    QStackedWidget *m_stack;
    QListWidget *m_places;
    QWidget *m_previewPanel;
    QLabel *m_previewImage;
    QLabel *m_previewInfo;

    QString m_path;
    QStringList m_history;
    int m_histIdx = -1;
    bool m_iconMode = false;
};
