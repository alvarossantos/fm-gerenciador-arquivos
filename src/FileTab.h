#pragma once
#include <QModelIndex>
#include <QSize>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <memory>

#include "UiPreferences.h"

class QAbstractItemView;
class QAction;
class QFileSystemModel;
class QLabel;
class QListView;
class QListWidget;
class QMenu;
class QProcess;
class QProgressDialog;
class QStackedWidget;
class QTreeView;
class DensityDelegate;
class ThumbnailProxy;

using RestoreBackupHook = std::function<bool(const QString &, const QString &)>;
void setRestoreBackupHookForTests(RestoreBackupHook hook);
using WaitForProcessHook = std::function<bool(QProcess *, int)>;
void setWaitForProcessHookForTests(WaitForProcessHook hook);
void setPlacesForTests(const QStringList &paths);
void setTemplatesDirForTests(const QString &dir);
using ContextMenuHook = std::function<void(QMenu *, const QStringList &)>;
void setContextMenuHookForTests(ContextMenuHook hook);
void setMimeQueryForTests(const QStringList &ids);
void clearMimeQueryForTestsForTests();
using ExecutableChooserHook = std::function<QString()>;
void setExecutableChooserForTests(ExecutableChooserHook hook);
using LaunchHook = std::function<bool(const QString &, const QStringList &, const QString &)>;
void setLaunchHookForTests(LaunchHook hook);

class FileTab : public QWidget {
    Q_OBJECT
public:
    explicit FileTab(const QString &startPath, QWidget *parent = nullptr);

    static constexpr int PlaceSectionRole = Qt::UserRole + 1;
    static constexpr int PlaceGroupRole = Qt::UserRole + 2;

    void applyUiPreferences(const UiPreferences &prefs);
    QString menuLabel(const QString &label, const QString &shortcut = QString()) const;
    void compactMenuSeparators(QMenu *menu) const;

    QString desktopFileForId(const QString &id) const;
    QString currentPath() const { return m_path; }
    bool canGoBack() const { return m_histIdx > 0; }
    bool canGoForward() const { return m_histIdx < m_history.size() - 1; }

    bool goTo(const QString &path, bool addHistory = true);
    void refreshModel();
    void back();
    void forward();
    void up();
    void setIconMode(bool icons);
    void setZoom(int percent);
    int zoom() const { return m_zoom; }
    void setPreviewVisible(bool visible);
    void setShowHidden(bool show);
    void setViewActions(QAction *icons, QAction *preview, QAction *hidden,
                        QAction *zoomIn, QAction *zoomOut, QAction *zoomReset);

signals:
    void zoomChanged(int percent);
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
    void addNewMenu(QMenu *menu);
    void addViewMenu(QMenu *menu);
    void addOpenWithMenu(QMenu *menu, const QString &path);
    void openWithDesktop(const QString &desktop, const QString &path);
    void launchProgram(const QString &program, const QStringList &args, const QString &cwd,
                       bool detached = false);
    void onSelectionChanged();
    void updatePreview();
    void updatePlacesSelection();

    void copyToClipboard(bool cut, const QStringList &paths);
    void paste();
    void copyPathsToClipboard(const QStringList &paths);
    void trashSelected(const QStringList &paths);
    void deleteSelected(const QStringList &paths);
    void renameCurrent(const QString &path);
    void showProperties(const QString &path);
    void openTerminal(const QString &dir);

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
    int m_zoom = 100;
    QSize m_treeIconBase;
    QSize m_gridIconBase;
    QSize m_gridBase;

    QAction *m_iconsAct = nullptr;
    QAction *m_previewAct = nullptr;
    QAction *m_hiddenAct = nullptr;
    QAction *m_zoomInAct = nullptr;
    QAction *m_zoomOutAct = nullptr;
    QAction *m_zoomResetAct = nullptr;
    DensityDelegate *m_densityDelegate = nullptr;
    UiPreferences m_ui;
};
