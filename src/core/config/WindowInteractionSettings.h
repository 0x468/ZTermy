#pragma once

#include <QJsonObject>
#include <QStringList>
#include <optional>

namespace ztermy::config
{
struct WindowInteractionSettings final
{
    bool singleInstance = true;
    int navigationWidth = 208;
    int navigationExpandedWidth = 208;
    QString tabDoubleClick = QStringLiteral("rename");
    QString tabCloseButton = QStringLiteral("hover");
    bool autoHideTitleBar = false;
    QString tabWidthMode = QStringLiteral("equal");
    QString globalShortcut;
    bool summonToCursorScreen = false;
    bool operator==(const WindowInteractionSettings &) const = default;

    [[nodiscard]] bool valid() const
    {
        return navigationWidth >= 56 && navigationWidth <= 320 && navigationExpandedWidth >= 132
               && navigationExpandedWidth <= 320
               && QStringList{QStringLiteral("rename"), QStringLiteral("close"), QStringLiteral("none")}.contains(
                   tabDoubleClick)
               && QStringList{QStringLiteral("always"), QStringLiteral("hover"), QStringLiteral("hidden")}.contains(
                   tabCloseButton)
               && QStringList{QStringLiteral("equal"), QStringLiteral("active")}.contains(tabWidthMode)
               && globalShortcut.size() <= 128 && !globalShortcut.contains(QChar::Null)
               && !globalShortcut.contains(u'\n') && !globalShortcut.contains(u'\r');
    }
    [[nodiscard]] QJsonObject toJson() const
    {
        return {{QStringLiteral("singleInstance"), singleInstance},
                {QStringLiteral("navigationWidth"), navigationWidth},
                {QStringLiteral("navigationExpandedWidth"), navigationExpandedWidth},
                {QStringLiteral("tabDoubleClick"), tabDoubleClick},
                {QStringLiteral("tabCloseButton"), tabCloseButton},
                {QStringLiteral("autoHideTitleBar"), autoHideTitleBar},
                {QStringLiteral("tabWidthMode"), tabWidthMode},
                {QStringLiteral("globalShortcut"), globalShortcut},
                {QStringLiteral("summonToCursorScreen"), summonToCursorScreen}};
    }
    [[nodiscard]] static std::optional<WindowInteractionSettings> fromJson(const QJsonObject &json)
    {
        if (!json.value(QStringLiteral("singleInstance")).isBool()
            || !json.value(QStringLiteral("navigationWidth")).isDouble()
            || !json.value(QStringLiteral("navigationExpandedWidth")).isDouble()
            || !json.value(QStringLiteral("tabDoubleClick")).isString()
            || !json.value(QStringLiteral("tabCloseButton")).isString()
            || !json.value(QStringLiteral("autoHideTitleBar")).isBool()
            || !json.value(QStringLiteral("tabWidthMode")).isString()
            || !json.value(QStringLiteral("globalShortcut")).isString()
            || !json.value(QStringLiteral("summonToCursorScreen")).isBool())
            return std::nullopt;
        WindowInteractionSettings result{
            .singleInstance = json.value(QStringLiteral("singleInstance")).toBool(),
            .navigationWidth = json.value(QStringLiteral("navigationWidth")).toInt(-1),
            .navigationExpandedWidth = json.value(QStringLiteral("navigationExpandedWidth")).toInt(-1),
            .tabDoubleClick = json.value(QStringLiteral("tabDoubleClick")).toString(),
            .tabCloseButton = json.value(QStringLiteral("tabCloseButton")).toString(),
            .autoHideTitleBar = json.value(QStringLiteral("autoHideTitleBar")).toBool(),
            .tabWidthMode = json.value(QStringLiteral("tabWidthMode")).toString(),
            .globalShortcut = json.value(QStringLiteral("globalShortcut")).toString(),
            .summonToCursorScreen = json.value(QStringLiteral("summonToCursorScreen")).toBool(),
        };
        return result.valid() ? std::optional(result) : std::nullopt;
    }
};
} // namespace ztermy::config
