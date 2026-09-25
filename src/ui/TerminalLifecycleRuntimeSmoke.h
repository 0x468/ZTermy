#pragma once

#include "application/AppController.h"
#include "ui/WindowStateRuntimeSmoke.h"
#include "ui/terminal/TerminalItem.h"

#include <QLoggingCategory>

#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(applicationLog)

namespace ztermy::ui
{
[[nodiscard]] inline bool terminalTabRunning(const ztermy::AppController &controller, const QString &tabId)
{
    const QVariantList tabs = controller.terminalTabs();
    const auto position = std::ranges::find(tabs, tabId, [](const QVariant &tab) {
        return tab.toMap().value(QStringLiteral("id")).toString();
    });
    return position != tabs.end() && position->toMap().value(QStringLiteral("running")).toBool();
}

[[nodiscard]] inline bool runLifecycleRuntimeSmoke(ztermy::NativeWindow &window, ztermy::AppController &controller)
{
    window.resize(QSize{1120, 800});
    ztermy::ui::showForRuntimeSmoke(window);
    window.requestActivate();
    processWindowEventsFor(std::chrono::milliseconds{200});
    // The terminal page loads on demand; open it before looking for a viewport.
    const auto showTerminalPage = [&window] {
        if (auto *rootObject = window.rootObject(); rootObject != nullptr)
        {
            rootObject->setProperty("currentPage", QStringLiteral("terminal"));
        }
        processWindowEventsFor(std::chrono::milliseconds{100});
    };

    constexpr int sequentialCycles = 8;
    qint64 maximumCloseMilliseconds = 0;
    for (int cycle = 0; cycle < sequentialCycles; ++cycle)
    {
        const QString tabId = controller.startLocalTerminal();
        if (tabId.isEmpty()
            || !processWindowEventsUntil(
                [&controller, &tabId] {
                    return terminalTabRunning(controller, tabId);
                },
                std::chrono::seconds{5}))
        {
            qCWarning(applicationLog) << "Lifecycle smoke could not start local terminal" << "cycle=" << cycle;
            return false;
        }
        showTerminalPage();
        auto *terminalItem = window.findChild<ztermy::ui::TerminalItem *>();
        if (terminalItem == nullptr)
        {
            qCWarning(applicationLog) << "Lifecycle smoke found no terminal viewport" << "cycle=" << cycle;
            return false;
        }
        terminalItem->inputGenerated(QByteArrayLiteral("Write-Output ('ZTERMY_LIFECYCLE_' + 'READY')\r"));
        processWindowEventsFor(std::chrono::milliseconds{40});
        QElapsedTimer closeTimer;
        closeTimer.start();
        if (!controller.closeTerminalTab(tabId))
        {
            return false;
        }
        maximumCloseMilliseconds = std::max(maximumCloseMilliseconds, closeTimer.elapsed());
        if (!processWindowEventsUntil(
                [&controller] {
                    return controller.terminalTabs().isEmpty();
                },
                std::chrono::seconds{3}))
        {
            qCWarning(applicationLog) << "Lifecycle smoke retained a closed tab" << "cycle=" << cycle;
            return false;
        }
    }

    constexpr int concurrentTabs = 8;
    QStringList finalTabs;
    for (int index = 0; index < concurrentTabs; ++index)
    {
        const QString tabId = controller.startLocalTerminal();
        if (tabId.isEmpty())
        {
            return false;
        }
        finalTabs.push_back(tabId);
        if (!processWindowEventsUntil(
                [&controller, &tabId] {
                    return terminalTabRunning(controller, tabId);
                },
                std::chrono::seconds{5}))
        {
            qCWarning(applicationLog) << "Lifecycle smoke failed to start a concurrent tab" << "index=" << index;
            return false;
        }
    }
    const bool allRunning = processWindowEventsUntil(
        [&controller, &finalTabs] {
            return std::ranges::all_of(finalTabs, [&controller](const QString &tabId) {
                return terminalTabRunning(controller, tabId);
            });
        },
        std::chrono::seconds{8});
    if (allRunning)
    {
        showTerminalPage();
        auto *terminalItem = window.findChild<ztermy::ui::TerminalItem *>();
        if (terminalItem == nullptr)
        {
            qCWarning(applicationLog) << "Lifecycle smoke found no terminal viewport for concurrent tabs";
            return false;
        }
        terminalItem->inputGenerated(QByteArrayLiteral("1..2000 | ForEach-Object { \"ztermy lifecycle line $_\" }\r"));
        processWindowEventsFor(std::chrono::milliseconds{100});
        QElapsedTimer bulkCloseTimer;
        bulkCloseTimer.start();
        const bool sequentialClose = qEnvironmentVariableIntValue("ZTERMY_TEST_SEQUENTIAL_TAB_CLOSE") > 0;
        bool closed = true;
        if (sequentialClose)
        {
            for (qsizetype index = finalTabs.size() - 1; index > 0; --index)
                closed = controller.closeTerminalTab(finalTabs[index]) && closed;
        }
        else
            closed = controller.closeOtherTerminalTabs(finalTabs.front());
        qCInfo(applicationLog) << "Lifecycle bulk close" << "closedTabs=" << concurrentTabs - 1
                               << "sequential=" << sequentialClose << "elapsedMs=" << bulkCloseTimer.elapsed();
        if (!closed || controller.terminalTabs().size() != 1 || controller.activeTerminalTabId() != finalTabs.front())
            return false;
    }
    qCInfo(applicationLog) << "Lifecycle runtime exercise"
                           << "sequentialCycles=" << sequentialCycles << "maximumCloseMs=" << maximumCloseMilliseconds
                           << "concurrentTabs=" << finalTabs.size() << "allRunning=" << allRunning;
    return allRunning && maximumCloseMilliseconds < 3000;
}

} // namespace ztermy::ui
