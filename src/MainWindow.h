#pragma once
#include <QMainWindow>

class FileTab;
class QAction;
class QHBoxLayout;
class QLineEdit;
class QPaintEvent;
class QResizeEvent;
class QTabWidget;
class QToolButton;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString &startPath, QWidget *parent = nullptr);
    void setBenchMode(bool on);

signals:
    void firstPaint();

protected:
    void closeEvent(QCloseEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    FileTab *currentTab() const;
    FileTab *addTab(const QString &path);
    void closeTab(int index);
    void syncTabBarVisibility();
    void syncToolbar();
    void navigateBreadcrumb(const QString &path);
    void rebuildBreadcrumb();
    void beginPathEdit();
    void endPathEdit();

    QTabWidget *m_tabs;
    QWidget *m_pathContainer;
    QWidget *m_pathBar;
    QHBoxLayout *m_pathLayout;
    QLineEdit *m_pathEdit;
    QToolButton *m_overflow;
    QAction *m_back, *m_forward, *m_up;
    QAction *m_iconsAct, *m_previewAct, *m_hiddenAct;
    QAction *m_zoomInAct, *m_zoomOutAct, *m_zoomResetAct;
    bool m_bench = false;
    bool m_painted = false;
};
