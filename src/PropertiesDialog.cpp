#include "PropertiesDialog.h"

#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDirIterator>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLocale>
#include <QMimeDatabase>
#include <QPushButton>
#include <QStyle>
#include <QTabWidget>
#include <QThread>
#include <QToolBox>
#include <QUrl>
#include <QVBoxLayout>

#include <mutex>

namespace {

std::atomic_int activeScans{ 0 };
std::mutex scanHookMutex;
ScanItemHook scanHook;

ScanItemHook currentScanHook()
{
    const std::lock_guard<std::mutex> lock(scanHookMutex);
    return scanHook;
}

class DirectoryScanWorker : public QObject
{
    Q_OBJECT
public:
    DirectoryScanWorker(QString path, std::shared_ptr<std::atomic_bool> cancel)
        : m_path(std::move(path)), m_cancel(std::move(cancel))
    {
        activeScans.fetch_add(1);
    }

    void run()
    {
        qint64 bytes = 0;
        int folders = 0;
        QStringList warnings;
        const QFileInfo info(m_path);
        if (info.isDir() && !info.isSymLink()) {
            scanDirectory(m_path, bytes, folders, warnings);
        } else if (!m_cancel->load()) {
            bytes = info.size();
        }
        emit scanDone(m_path, bytes, folders, warnings);
        activeScans.fetch_sub(1);
        thread()->quit();
    }

signals:
    void scanDone(const QString &path, qint64 bytes, int folders, const QStringList &warnings);

private:
    void scanDirectory(const QString &path, qint64 &bytes, int &folders, QStringList &warnings)
    {
        QDir dir(path);
        if (!dir.isReadable()) {
            warnings << PropertiesDialog::tr("Pasta ilegível: %1").arg(path);
            return;
        }
        QDirIterator it(path, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot,
                        QDirIterator::NoIteratorFlags);
        while (it.hasNext()) {
            if (m_cancel->load())
                return;
            if (const ScanItemHook hook = currentScanHook())
                hook();
            it.next();
            const QFileInfo entry = it.fileInfo();
            if (entry.isSymLink()) {
                continue;
            } else if (entry.isDir()) {
                ++folders;
                scanDirectory(entry.absoluteFilePath(), bytes, folders, warnings);
            } else {
                bytes += entry.size();
            }
        }
    }

    QString m_path;
    std::shared_ptr<std::atomic_bool> m_cancel;
};

QWidget *generalSection(const QFileInfo &fi, QLabel **size, QLabel **folders)
{
    auto *w = new QWidget;
    w->setObjectName(QStringLiteral("propertiesGeneral"));
    auto *form = new QFormLayout(w);

    const auto addRow = [form](const QString &label, const QString &value) {
        auto *out = new QLabel(value);
        out->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(label, out);
        return out;
    };
    addRow(PropertiesDialog::tr("Nome:"), fi.fileName());
    addRow(PropertiesDialog::tr("Local:"), fi.absolutePath());
    addRow(PropertiesDialog::tr("Tipo:"), QMimeDatabase().mimeTypeForFile(fi).comment());

    *size = addRow(PropertiesDialog::tr("Tamanho:"),
                   fi.isFile() ? QLocale().formattedDataSize(fi.size())
                               : PropertiesDialog::tr("Calculando…"));
    (*size)->setObjectName(QStringLiteral("propertiesSize"));
    *folders = addRow(PropertiesDialog::tr("Pastas:"), fi.isDir() ? QStringLiteral("…") : QStringLiteral("—"));
    (*folders)->setObjectName(QStringLiteral("propertiesFolderCount"));
    return w;
}

QWidget *datesSection(const QFileInfo &fi)
{
    auto *w = new QWidget;
    auto *form = new QFormLayout(w);
    const auto fmt = [](const QDateTime &dt) {
        return QLocale().toString(dt, QLocale::LongFormat);
    };
    form->addRow(PropertiesDialog::tr("Criado:"), new QLabel(fmt(fi.birthTime())));
    form->addRow(PropertiesDialog::tr("Modificado:"), new QLabel(fmt(fi.lastModified())));
    form->addRow(PropertiesDialog::tr("Lido:"), new QLabel(fmt(fi.lastRead())));
    return w;
}

QWidget *permissionsSection(const QFileInfo &fi)
{
    auto *w = new QWidget;
    auto *form = new QFormLayout(w);
    form->addRow(PropertiesDialog::tr("Dono:"), new QLabel(fi.owner()));
    form->addRow(PropertiesDialog::tr("Grupo:"), new QLabel(fi.group()));
    form->addRow(PropertiesDialog::tr("Permissões:"),
                 new QLabel(QString::number(static_cast<uint>(fi.permissions()) & 0xFFF, 16)));
    return w;
}

} // namespace

void setScanItemHookForTests(ScanItemHook hook)
{
    const std::lock_guard<std::mutex> lock(scanHookMutex);
    scanHook = std::move(hook);
}

int PropertiesDialog::activeScansForTests()
{
    return activeScans.load();
}

PropertiesDialog::PropertiesDialog(const QString &path, const UiPreferences &prefs, QWidget *parent)
    : QDialog(parent), m_path(QFileInfo(path).absoluteFilePath()), m_prefs(prefs),
      m_cancel(std::make_shared<std::atomic_bool>(false))
{
    setWindowModality(Qt::NonModal);
    setWindowTitle(tr("Propriedades — %1").arg(QFileInfo(m_path).fileName()));
    resize(520, 440);

    const QFileInfo fi(m_path);
    const QMimeType mt = QMimeDatabase().mimeTypeForFile(fi);
    auto *root = new QVBoxLayout(this);

    auto *header = new QHBoxLayout;
    auto *icon = new QLabel;
    icon->setPixmap(QIcon::fromTheme(mt.iconName(), style()->standardIcon(QStyle::SP_FileIcon)).pixmap(32));
    auto *names = new QVBoxLayout;
    auto *name = new QLabel(fi.fileName());
    name->setStyleSheet(QStringLiteral("font-weight: bold"));
    names->addWidget(name);
    names->addWidget(new QLabel(mt.comment().isEmpty() ? mt.name() : mt.comment()));
    header->addWidget(icon);
    header->addLayout(names);
    root->addLayout(header);

    QWidget *general = generalSection(fi, &m_size, &m_folderCount);
    QWidget *dates = datesSection(fi);
    QWidget *permissions = permissionsSection(fi);

    switch (m_prefs.preset) {
    case UiPreset::Compact: {
        auto *sections = new QToolBox;
        sections->setObjectName(QStringLiteral("propertiesSections"));
        sections->addItem(general, tr("Geral"));
        sections->addItem(dates, tr("Datas"));
        sections->addItem(permissions, tr("Permissões"));
        root->addWidget(sections);
        break;
    }
    case UiPreset::Balanced: {
        auto *sections = new QTabWidget;
        sections->setObjectName(QStringLiteral("propertiesSections"));
        sections->addTab(general, tr("Geral"));
        sections->addTab(dates, tr("Datas"));
        sections->addTab(permissions, tr("Permissões"));
        root->addWidget(sections);
        break;
    }
    case UiPreset::Advanced: {
        auto *sections = new QWidget;
        sections->setObjectName(QStringLiteral("propertiesSections"));
        auto *stack = new QVBoxLayout(sections);
        stack->addWidget(general);
        stack->addWidget(dates);
        stack->addWidget(permissions);
        root->addWidget(sections);
        break;
    }
    }

    m_status = new QLabel;
    m_status->setObjectName(QStringLiteral("propertiesStatus"));
    root->addWidget(m_status);

    auto *buttons = new QHBoxLayout;
    auto *copy = new QPushButton(tr("Copiar caminho"));
    copy->setObjectName(QStringLiteral("copyPathButton"));
    connect(copy, &QPushButton::clicked, this,
            [this] { QGuiApplication::clipboard()->setText(m_path); });
    auto *openParent = new QPushButton(tr("Abrir pasta pai"));
    openParent->setObjectName(QStringLiteral("openParentButton"));
    connect(openParent, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_path).absolutePath()));
    });
    auto *close = new QPushButton(tr("Fechar"));
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    buttons->addStretch(1);
    buttons->addWidget(copy);
    buttons->addWidget(openParent);
    buttons->addWidget(close);
    root->addLayout(buttons);
    startScan();
}

PropertiesDialog::~PropertiesDialog()
{
    requestCancel();
}

QString PropertiesDialog::path() const
{
    return m_path;
}

void PropertiesDialog::requestCancel()
{
    m_cancel->store(true);
}

void PropertiesDialog::closeEvent(QCloseEvent *event)
{
    requestCancel();
    QDialog::closeEvent(event);
}

void PropertiesDialog::startScan()
{
    auto *thread = new QThread;
    auto *worker = new DirectoryScanWorker(m_path, m_cancel);
    worker->moveToThread(thread);
    connect(worker, &DirectoryScanWorker::scanDone, this,
            [this](const QString &path, qint64 bytes, int folders, const QStringList &warnings) {
                setScanResult(bytes, folders, warnings);
                emit scanFinished(path, bytes, folders, warnings);
            });
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::started, worker, &DirectoryScanWorker::run);
    thread->start();
}

void PropertiesDialog::setScanResult(qint64 bytes, int folders, const QStringList &warnings)
{
    m_size->setText(QLocale().formattedDataSize(bytes));
    m_folderCount->setText(QString::number(folders));
    m_status->setText(warnings.isEmpty()
                          ? tr("Varredura concluída")
                          : tr("Varredura concluída com avisos: %1").arg(warnings.join(QStringLiteral("; "))));
}

void PropertiesDialog::setScanStatus(const QString &status)
{
    m_status->setText(status);
}

#include "PropertiesDialog.moc"
