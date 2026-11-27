#pragma once

#include <QDialog>

#include <atomic>
#include <memory>

#include "UiPreferences.h"

class QLabel;

class PropertiesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PropertiesDialog(const QString &path, const UiPreferences &prefs,
                              QWidget *parent = nullptr);

    QString path() const;
    void requestCancel();

public slots:
    void setScanResult(qint64 bytes, int folders, const QStringList &warnings);
    void setScanStatus(const QString &status);

private:
    QString m_path;
    UiPreferences m_prefs;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QLabel *m_size = nullptr;
    QLabel *m_folderCount = nullptr;
    QLabel *m_status = nullptr;
};
