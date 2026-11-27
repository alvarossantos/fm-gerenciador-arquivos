#pragma once

#include <QDialog>

#include <atomic>
#include <functional>
#include <memory>

#include "UiPreferences.h"

class QCloseEvent;
class QLabel;

using ScanItemHook = std::function<void()>;
void setScanItemHookForTests(ScanItemHook hook);

class PropertiesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PropertiesDialog(const QString &path, const UiPreferences &prefs,
                              QWidget *parent = nullptr);
    ~PropertiesDialog() override;

    QString path() const;
    void requestCancel();

    static int activeScansForTests();

signals:
    void scanFinished(const QString &path, qint64 bytes, int folders, const QStringList &warnings);

public slots:
    void setScanResult(qint64 bytes, int folders, const QStringList &warnings);
    void setScanStatus(const QString &status);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void startScan();

    QString m_path;
    UiPreferences m_prefs;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QLabel *m_size = nullptr;
    QLabel *m_folderCount = nullptr;
    QLabel *m_status = nullptr;
};
