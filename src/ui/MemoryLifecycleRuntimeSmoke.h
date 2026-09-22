#pragma once

#include "application/AppController.h"
#include "ui/WindowStateRuntimeSmoke.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <ranges>

namespace ztermy::ui
{
[[nodiscard]] inline bool memoryTerminalTabRunning(const AppController &controller, const QString &tabId)
{
    const QVariantList tabs = controller.terminalTabs();
    const auto position = std::ranges::find(tabs, tabId, [](const QVariant &tab) {
        return tab.toMap().value(QStringLiteral("id")).toString();
    });
    return position != tabs.end() && position->toMap().value(QStringLiteral("running")).toBool();
}

template <typename Stage>
[[nodiscard]] bool runMemoryLifecycleStages(AppController &controller, const QString &terminalId, Stage &&stage)
{
    const auto paneCount = [&controller] {
        return controller.activeTerminalWorkspace().value(QStringLiteral("paneCount")).toInt();
    };
    for (const int target : {2, 4, 8})
    {
        while (paneCount() < target)
        {
            const QString orientation =
                paneCount() % 2 == 0 ? QStringLiteral("vertical") : QStringLiteral("horizontal");
            if (!controller.splitActiveTerminal(orientation, true))
                return false;
            processWindowEventsFor(std::chrono::milliseconds{250});
        }
        if (!stage(QStringLiteral("panes-%1-idle").arg(target), std::chrono::seconds{10}))
            return false;
    }
    while (paneCount() > 1)
        if (!controller.closeActiveTerminalPane())
            return false;
    if (!stage(QStringLiteral("panes-closed"), std::chrono::seconds{30}))
        return false;

    const auto startTabs = [&controller](QStringList tabIds) -> std::optional<QStringList> {
        while (tabIds.size() < 8)
        {
            const QString id = controller.startLocalTerminal();
            if (id.isEmpty()
                || !processWindowEventsUntil(
                    [&controller, &id] {
                        return memoryTerminalTabRunning(controller, id);
                    },
                    std::chrono::seconds{10}))
                return std::nullopt;
            tabIds.push_back(id);
        }
        return tabIds;
    };
    auto tabIds = startTabs(QStringList{terminalId});
    if (!tabIds || !stage(QStringLiteral("tabs-8-idle"), std::chrono::seconds{10}))
        return false;
    for (int cycle = 0; cycle < 20; ++cycle)
        for (const QString &id : *tabIds)
        {
            if (!controller.activateTerminalTab(id))
                return false;
            processWindowEventsFor(std::chrono::milliseconds{25});
        }
    if (!stage(QStringLiteral("tabs-8-rotated"), std::chrono::seconds{10}))
        return false;
    for (const QString &id : *tabIds)
        if (!controller.closeTerminalTab(id))
            return false;

    bool validCycles = false;
    const int requestedCycles = qEnvironmentVariableIntValue("ZTERMY_MEMORY_LIFECYCLE_CYCLES", &validCycles);
    const int cycles = validCycles ? std::clamp(requestedCycles, 1, 5) : 1;
    if (!stage(QStringLiteral("tabs-closed-30s"), std::chrono::seconds{30}))
        return false;
    for (int cycle = 2; cycle <= cycles; ++cycle)
    {
        tabIds = startTabs({});
        if (!tabIds || !stage(QStringLiteral("tabs-8-reopen-%1").arg(cycle), std::chrono::seconds{5}))
            return false;
        for (const QString &id : *tabIds)
            if (!controller.closeTerminalTab(id))
                return false;
        if (!stage(QStringLiteral("tabs-closed-reopen-%1").arg(cycle), std::chrono::seconds{10}))
            return false;
    }
    return stage(QStringLiteral("tabs-closed-60s"), std::chrono::seconds{30});
}
} // namespace ztermy::ui
