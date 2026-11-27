#pragma once
#include <QCache>
#include <QFileIconProvider>
#include <QIcon>
#include <QIdentityProxyModel>
#include <QSet>

class QFileSystemModel;

// Proxy sobre o QFileSystemModel que troca o ícone de imagens pela miniatura.
// O QFileSystemModel busca e guarda ícones numa thread própria, por isso a
// miniatura é entregue aqui (thread da UI): as imagens são decodificadas em
// threads de fundo e a UI nunca trava, mesmo em pastas com milhares de fotos.
class ThumbnailProxy : public QIdentityProxyModel {
    Q_OBJECT
public:
    explicit ThumbnailProxy(QFileSystemModel *source, QObject *parent = nullptr);
    ~ThumbnailProxy() override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

private:
    void schedule(const QString &path, const QString &key);

    QFileSystemModel *m_fs;
    QSet<QString> m_formats;
    mutable QCache<QString, QIcon> m_cache;
    mutable QSet<QString> m_pending;
    mutable QSet<QString> m_failed;
};
