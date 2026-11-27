#pragma once
#include <QMainWindow>

class FileTab;
class QAction;
class QLineEdit;
class QTabWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString &startPath, QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    FileTab *currentTab() const;
    FileTab *addTab(const QString &path);
    void closeTab(int index);
    void syncToolbar();

    QTabWidget *m_tabs;
    QLineEdit *m_pathEdit;
    QAction *m_back, *m_forward, *m_up;
    QAction *m_iconsAct, *m_previewAct, *m_hiddenAct;
};
