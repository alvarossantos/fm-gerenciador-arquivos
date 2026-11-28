#pragma once
#include <QMainWindow>

#include "UiPreferences.h"

class FileTab;
class QAction;
class QActionGroup;
class QHBoxLayout;
class QLineEdit;
class QMenu;
class QPaintEvent;
class QResizeEvent;
class QTabWidget;
class QToolBar;
class QToolButton;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString &startPath, QWidget *parent = nullptr);
    void setBenchMode(bool on);
    void applyUiPreferences(const UiPreferences &prefs);
    UiPreferences uiPreferences() const;

signals:
    void firstPaint();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    FileTab *currentTab() const;
    FileTab *addTab(const QString &path);
    void closeTab(int index);
    void syncTabBarVisibility();
    void syncToolbar();
    void syncToolbarActions();
    void navigateBreadcrumb(const QString &path);
    void rebuildBreadcrumb();
    void beginPathEdit();
    void endPathEdit();
    void openCustomizeDialog();
    void rebuildTabsMenu();
    void openShortcutsDialog();
    void openHelpDialog();
    void openAboutDialog();

    QTabWidget *m_tabs;
    QWidget *m_pathContainer;
    QWidget *m_pathBar;
    QHBoxLayout *m_pathLayout;
    QLineEdit *m_pathEdit;
    QToolButton *m_overflow;
    QToolBar *m_toolbar = nullptr;
    QAction *m_back, *m_forward, *m_up;
    QAction *m_listAct, *m_iconsAct, *m_previewAct, *m_hiddenAct;
    QActionGroup *m_viewModeGroup;
    QAction *m_zoomInAct, *m_zoomOutAct, *m_zoomResetAct;
    QAction *m_newTabAct = nullptr;
    QAction *m_moreAct = nullptr;
    QAction *m_presetCompactAct = nullptr;
    QAction *m_presetBalancedAct = nullptr;
    QAction *m_presetAdvancedAct = nullptr;
    QAction *m_restoreDefaultsAct = nullptr;
    QToolButton *m_filesMenuButton = nullptr;
    QToolButton *m_viewMenuButton = nullptr;
    QMenu *m_filesMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QMenu *m_showAllTabsMenu = nullptr;
    UiPreferences m_ui;
    int m_breadcrumbLimit = 4;
    bool m_bench = false;
    bool m_painted = false;
};
