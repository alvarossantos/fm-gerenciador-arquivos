#pragma once
#include <QStringList>

class QSettings;

enum class UiPreset { Compact, Balanced, Advanced };

struct UiPreferences {
    UiPreset preset = UiPreset::Balanced;
    int density = 1;
    int iconSize = 48;
    QStringList columns;
    bool preview = false;
    bool gridSize = false;
    bool gridDate = false;
};

UiPreferences defaultUiPreferences(UiPreset preset);
QStringList normalizeColumns(const QStringList &input, UiPreset preset);
UiPreferences loadUiPreferences(QSettings &settings);
void saveUiPreferences(QSettings &settings, const UiPreferences &prefs);
QString uiPresetKey(UiPreset preset);
