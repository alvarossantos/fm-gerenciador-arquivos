#include "ThemeManager.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

QString norm(QString k)
{
    k.remove('_');
    k.remove('-');
    return k.toLower();
}

QHash<QString, QColor> parseColors(const QByteArray &data)
{
    QHash<QString, QColor> map;
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return map;

    QJsonObject o = doc.object();
    if (o.value(QStringLiteral("colors")).isObject())
        o = o.value(QStringLiteral("colors")).toObject();
    const QString mode = qEnvironmentVariable("FM_THEME_MODE", QStringLiteral("dark"));
    if (o.value(mode).isObject())
        o = o.value(mode).toObject();

    for (auto it = o.begin(); it != o.end(); ++it) {
        if (!it.value().isString())
            continue;
        const QColor c(it.value().toString());
        if (c.isValid())
            map.insert(norm(it.key()), c);
    }
    return map;
}

QColor pick(const QHash<QString, QColor> &m, std::initializer_list<const char *> keys,
            const QColor &fallback)
{
    for (const char *k : keys) {
        const auto it = m.constFind(QString::fromLatin1(k));
        if (it != m.constEnd())
            return it.value();
    }
    return fallback;
}

} // namespace

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent), m_basePalette(qApp->palette())
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(150);
    connect(&m_debounce, &QTimer::timeout, this, &ThemeManager::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
}

void ThemeManager::setThemeFile(const QString &path)
{
    m_path = path;
    reload();
}

void ThemeManager::rewatch()
{
    // Muitas shells regravam o arquivo de forma atômica (rename), o que derruba
    // o watch no arquivo; por isso observamos também a pasta e re-adicionamos.
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());

    const QFileInfo fi(m_path);
    if (fi.exists())
        m_watcher.addPath(fi.absoluteFilePath());
    if (fi.absoluteDir().exists())
        m_watcher.addPath(fi.absolutePath());
}

void ThemeManager::reload()
{
    rewatch();

    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly)) {
        applyDefault();
        return;
    }
    const auto colors = parseColors(f.readAll());
    if (!colors.contains(QStringLiteral("surface")) && !colors.contains(QStringLiteral("background"))) {
        // Uma gravação pode deixar o JSON inválido por instantes: manter a última
        // paleta boa evita um flash para o padrão do Qt.
        if (!m_applied)
            applyDefault();
        return;
    }
    applyShellColors(colors);
}

void ThemeManager::applyDefault()
{
    qApp->setPalette(m_basePalette);
    qApp->setStyleSheet(QString());
    m_applied = false;
    emit themeApplied(false);
}

void ThemeManager::applyShellColors(const QHash<QString, QColor> &c)
{
    const QColor fb = m_basePalette.color(QPalette::Window);

    const QColor surface   = pick(c, {"surface", "background"}, fb);
    const QColor onSurface = pick(c, {"onsurface", "onbackground"}, m_basePalette.color(QPalette::WindowText));
    const QColor onSurfVar = pick(c, {"onsurfacevariant"}, onSurface);
    const QColor primary   = pick(c, {"primary"}, m_basePalette.color(QPalette::Highlight));
    const QColor onPrimary = pick(c, {"onprimary"}, m_basePalette.color(QPalette::HighlightedText));
    const QColor priCont   = pick(c, {"primarycontainer", "secondarycontainer"}, primary);
    const QColor onPriCont = pick(c, {"onprimarycontainer", "onsecondarycontainer"}, onPrimary);
    const QColor cLow      = pick(c, {"surfacecontainerlow", "surfacecontainer"}, surface);
    const QColor cMid      = pick(c, {"surfacecontainer"}, surface);
    const QColor cHigh     = pick(c, {"surfacecontainerhigh", "surfacecontainer"}, surface);
    const QColor cHighest  = pick(c, {"surfacecontainerhighest", "surfacecontainerhigh"}, cHigh);
    const QColor outline   = pick(c, {"outlinevariant", "outline"}, onSurfVar);

    QPalette p = m_basePalette;
    p.setColor(QPalette::Window, surface);
    p.setColor(QPalette::WindowText, onSurface);
    p.setColor(QPalette::Base, cLow);
    p.setColor(QPalette::AlternateBase, cMid);
    p.setColor(QPalette::Text, onSurface);
    p.setColor(QPalette::Button, cHigh);
    p.setColor(QPalette::ButtonText, onSurface);
    p.setColor(QPalette::BrightText, onPrimary);
    p.setColor(QPalette::Highlight, primary);
    p.setColor(QPalette::HighlightedText, onPrimary);
    p.setColor(QPalette::Link, primary);
    p.setColor(QPalette::ToolTipBase, cHighest);
    p.setColor(QPalette::ToolTipText, onSurface);
    p.setColor(QPalette::PlaceholderText, onSurfVar);
    p.setColor(QPalette::Mid, outline);
    p.setColor(QPalette::Dark, outline);
    p.setColor(QPalette::Light, cHighest);
    p.setColor(QPalette::Midlight, cHigh);

    QColor dis = onSurface;
    dis.setAlphaF(0.4);
    p.setColor(QPalette::Disabled, QPalette::WindowText, dis);
    p.setColor(QPalette::Disabled, QPalette::Text, dis);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, dis);

    const QString qss = QStringLiteral(
        "QTreeView, QListView, QListWidget { border: none; outline: 0; }"
        "QTreeView::item, QListView::item { border-radius: 6px; padding: 3px; }"
        "QTreeView::item:selected, QListView::item:selected, QListWidget::item:selected"
        "  { background: %1; color: %2; }"
        "QTreeView::item:hover:!selected, QListView::item:hover:!selected, QListWidget::item:hover:!selected"
        "  { background: %3; }"
        "QHeaderView::section { background: %4; color: %5; border: none; padding: 5px 8px; }"
        "QTabBar::tab { padding: 6px 14px; margin: 3px 2px; border-radius: 8px; }"
        "QTabBar::tab:selected { background: %1; color: %2; }"
        "QTabBar::tab:!selected:hover { background: %3; }"
        "QTabWidget::pane { border: none; }"
        "QToolBar { border: none; spacing: 4px; padding: 4px; background: %4; }"
        "QToolButton { border-radius: 8px; padding: 5px; }"
        "QToolButton:hover { background: %3; }"
        "QToolButton:checked { background: %1; }"
        "QLineEdit { border: 1px solid %6; border-radius: 8px; padding: 4px 10px; background: %7; }"
        "QLineEdit:focus { border-color: %8; }"
        "QMenu { background: %7; border: 1px solid %6; border-radius: 8px; padding: 4px; }"
        "QMenu::item { padding: 6px 22px; border-radius: 6px; }"
        "QMenu::item:selected { background: %1; color: %2; }"
        "QSplitter::handle { background: %6; }"
        "QStatusBar { background: %4; }")
        .arg(priCont.name(), onPriCont.name(), cHigh.name(), cMid.name(), onSurface.name(),
             outline.name(), cLow.name(), primary.name());

    qApp->setPalette(p);
    qApp->setStyleSheet(qss);
    m_applied = true;
    emit themeApplied(true);
}
