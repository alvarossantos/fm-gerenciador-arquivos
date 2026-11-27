#include "ThumbnailProxy.h"

#include <QDateTime>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QImage>
#include <QImageReader>
#include <QPixmap>
#include <QThreadPool>

namespace {
constexpr int kThumbSize = 128;
constexpr qint64 kMaxFileSize = 40LL * 1024 * 1024;
}

ThumbnailProxy::ThumbnailProxy(QFileSystemModel *source, QObject *parent)
    : QIdentityProxyModel(parent), m_fs(source), m_cache(600)
{
    setSourceModel(source);
    for (const QByteArray &f : QImageReader::supportedImageFormats())
        m_formats.insert(QString::fromLatin1(f).toLower());
}

ThumbnailProxy::~ThumbnailProxy()
{
    QThreadPool::globalInstance()->waitForDone();
}

QVariant ThumbnailProxy::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::TextAlignmentRole && section >= 1 && section <= 3)
        return QVariant(Qt::AlignRight | Qt::AlignVCenter);
    return QIdentityProxyModel::headerData(section, orientation, role);
}

QVariant ThumbnailProxy::data(const QModelIndex &index, int role) const
{
    if (role != Qt::DecorationRole || index.column() != 0)
        return QIdentityProxyModel::data(index, role);

    const QFileInfo info = m_fs->fileInfo(mapToSource(index));
    if (!info.isFile() || info.size() > kMaxFileSize || !m_formats.contains(info.suffix().toLower()))
        return QIdentityProxyModel::data(index, role);

    const QString path = info.absoluteFilePath();
    const QString key = path + QLatin1Char('|') + QString::number(info.lastModified().toMSecsSinceEpoch());

    if (QIcon *ic = m_cache.object(key))
        return *ic;

    if (!m_failed.contains(key) && !m_pending.contains(key)) {
        m_pending.insert(key);
        const_cast<ThumbnailProxy *>(this)->schedule(path, key);
    }
    return QIdentityProxyModel::data(index, role); // ícone genérico até a miniatura ficar pronta
}

void ThumbnailProxy::schedule(const QString &path, const QString &key)
{
    QThreadPool::globalInstance()->start([this, path, key] {
        QImageReader r(path);
        r.setAutoTransform(true);
        QSize s = r.size();
        if (s.isValid()) {
            s.scale(kThumbSize, kThumbSize, Qt::KeepAspectRatio);
            r.setScaledSize(s);
        }
        const QImage img = r.read();
        QMetaObject::invokeMethod(this, [this, path, key, img] {
            m_pending.remove(key);
            if (img.isNull()) {
                m_failed.insert(key);
                return;
            }
            m_cache.insert(key, new QIcon(QPixmap::fromImage(img)));
            const QModelIndex idx = mapFromSource(m_fs->index(path));
            if (idx.isValid())
                emit dataChanged(idx, idx, {Qt::DecorationRole});
        }, Qt::QueuedConnection);
    });
}
