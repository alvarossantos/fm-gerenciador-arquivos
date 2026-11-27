#pragma once
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QPalette>
#include <QTimer>

// Lê o arquivo de cores gerado pela shell (JSON no estilo matugen/Material You)
// e aplica como QPalette + stylesheet no app inteiro. Recarrega sozinho quando
// o arquivo muda (ex.: ao trocar de wallpaper/tema na shell).
class ThemeManager : public QObject {
    Q_OBJECT
public:
    explicit ThemeManager(QObject *parent = nullptr);
    void setThemeFile(const QString &path);
    QString themeFile() const { return m_path; }

signals:
    void themeApplied(bool fromShell);

private slots:
    void reload();

private:
    void rewatch();
    void applyShellColors(const QHash<QString, QColor> &c);
    void applyDefault();

    QString m_path;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QPalette m_basePalette;
};
