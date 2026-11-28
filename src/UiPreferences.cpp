#include "UiPreferences.h"

#include <algorithm>

#include <QLatin1String>
#include <QSettings>

namespace {

UiPreset presetFromKey(const QString &key)
{
    if (key == QLatin1String("compact"))
        return UiPreset::Compact;
    if (key == QLatin1String("advanced"))
        return UiPreset::Advanced;
    return UiPreset::Balanced;
}

bool validColumn(const QString &column)
{
    bool ok = false;
    const int index = column.toInt(&ok);
    return ok && index >= 0 && index <= 3;
}

} // namespace

QString uiPresetKey(UiPreset preset)
{
    switch (preset) {
    case UiPreset::Compact:
        return QStringLiteral("compact");
    case UiPreset::Advanced:
        return QStringLiteral("advanced");
    case UiPreset::Balanced:
        break;
    }
    return QStringLiteral("balanced");
}

UiPreferences defaultUiPreferences(UiPreset preset)
{
    UiPreferences prefs;
    prefs.preset = preset;
    prefs.gridSize = false;
    prefs.gridDate = false;
    switch (preset) {
    case UiPreset::Compact:
        prefs.density = 0;
        prefs.iconSize = 32;
        prefs.columns = QStringList{QStringLiteral("0")};
        prefs.preview = false;
        break;
    case UiPreset::Advanced:
        prefs.density = 2;
        prefs.iconSize = 64;
        prefs.columns = QStringList{QStringLiteral("0"), QStringLiteral("1"),
                                    QStringLiteral("2"), QStringLiteral("3")};
        prefs.preview = true;
        break;
    case UiPreset::Balanced:
        prefs.density = 1;
        prefs.iconSize = 48;
        prefs.columns = QStringList{QStringLiteral("0"), QStringLiteral("1"),
                                    QStringLiteral("2"), QStringLiteral("3")};
        prefs.preview = false;
        break;
    }
    return prefs;
}

QStringList normalizeColumns(const QStringList &input, UiPreset preset)
{
    QList<int> indices;
    for (const QString &column : input) {
        if (!validColumn(column))
            continue;
        const int index = column.toInt();
        if (!indices.contains(index))
            indices.append(index);
    }
    if (indices.isEmpty())
        return defaultUiPreferences(preset).columns;

    std::sort(indices.begin(), indices.end());
    if (!indices.contains(0))
        indices.prepend(0);

    QStringList result;
    for (int index : indices)
        result << QString::number(index);
    return result;
}

UiPreferences loadUiPreferences(QSettings &settings)
{
    const UiPreset preset = presetFromKey(
        settings.value(QLatin1String("ui/preset"), QStringLiteral("balanced")).toString());
    UiPreferences prefs = defaultUiPreferences(preset);

    bool ok = false;
    const int density = settings.value(QLatin1String("ui/density"), prefs.density).toInt(&ok);
    if (ok && density >= 0 && density <= 2)
        prefs.density = density;

    const int iconSize = settings.value(QLatin1String("ui/iconSize"), prefs.iconSize).toInt(&ok);
    if (ok && (iconSize == 32 || iconSize == 48 || iconSize == 64))
        prefs.iconSize = iconSize;

    const QStringList columns =
        settings.value(QLatin1String("ui/columns"), prefs.columns).toStringList();
    prefs.columns = normalizeColumns(columns, preset);

    if (settings.contains(QLatin1String("ui/preview")))
        prefs.preview = settings.value(QLatin1String("ui/preview")).toBool();

    if (settings.contains(QLatin1String("ui/gridSize")))
        prefs.gridSize = settings.value(QLatin1String("ui/gridSize")).toBool();
    if (settings.contains(QLatin1String("ui/gridDate")))
        prefs.gridDate = settings.value(QLatin1String("ui/gridDate")).toBool();

    return prefs;
}

void saveUiPreferences(QSettings &settings, const UiPreferences &prefs)
{
    settings.setValue(QLatin1String("ui/preset"), uiPresetKey(prefs.preset));
    settings.setValue(QLatin1String("ui/density"), prefs.density);
    settings.setValue(QLatin1String("ui/iconSize"), prefs.iconSize);
    settings.setValue(QLatin1String("ui/columns"), prefs.columns);
    settings.setValue(QLatin1String("ui/preview"), prefs.preview);
    settings.setValue(QLatin1String("ui/gridSize"), prefs.gridSize);
    settings.setValue(QLatin1String("ui/gridDate"), prefs.gridDate);
}
