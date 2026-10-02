#pragma once

#include "core/config/ExplorerMenuContract.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <optional>

namespace ztermy::config
{
[[nodiscard]] inline bool validExplorerShellId(const QString &id)
{
    for (const auto token : explorer::shellIds)
        if (id == QLatin1StringView(token.data(), static_cast<qsizetype>(token.size())))
            return true;
    return false;
}

struct WindowsIntegrationSettings final
{
    QString menuMode = QStringLiteral("single");
    QString singleShell = QStringLiteral("automatic");
    QStringList submenuShells{QStringLiteral("automatic"),
                              QStringLiteral("powerShellCore"),
                              QStringLiteral("windowsPowerShell"),
                              QStringLiteral("commandPrompt"),
                              QStringLiteral("gitBash"),
                              QStringLiteral("nushell"),
                              QStringLiteral("wsl")};

    [[nodiscard]] bool valid() const
    {
        if ((menuMode != QStringLiteral("single") && menuMode != QStringLiteral("submenu"))
            || !validExplorerShellId(singleShell) || submenuShells.isEmpty() || submenuShells.size() > 7)
            return false;
        QSet<QString> seen;
        for (const auto &shell : submenuShells)
        {
            if (!validExplorerShellId(shell) || seen.contains(shell))
                return false;
            seen.insert(shell);
        }
        return true;
    }
    [[nodiscard]] QJsonObject toJson() const
    {
        return {{QStringLiteral("menuMode"), menuMode},
                {QStringLiteral("singleShell"), singleShell},
                {QStringLiteral("submenuShells"), QJsonArray::fromStringList(submenuShells)}};
    }
    [[nodiscard]] static std::optional<WindowsIntegrationSettings> fromJson(const QJsonObject &json)
    {
        if (!json.value(QStringLiteral("menuMode")).isString() || !json.value(QStringLiteral("singleShell")).isString()
            || !json.value(QStringLiteral("submenuShells")).isArray())
            return std::nullopt;
        WindowsIntegrationSettings value{.menuMode = json.value(QStringLiteral("menuMode")).toString(),
                                         .singleShell = json.value(QStringLiteral("singleShell")).toString(),
                                         .submenuShells = {}};
        for (const auto &shell : json.value(QStringLiteral("submenuShells")).toArray())
        {
            if (!shell.isString())
                return std::nullopt;
            value.submenuShells.append(shell.toString());
        }
        return value.valid() ? std::optional(value) : std::nullopt;
    }
    [[nodiscard]] friend bool operator==(const WindowsIntegrationSettings &,
                                         const WindowsIntegrationSettings &) = default;
};
} // namespace ztermy::config
