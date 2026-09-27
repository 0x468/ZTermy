#pragma once

#include "ui/terminal/DetachedPaneWindowRuntimeSmoke.h"
#include <functional>
#include <ranges>
#include <vector>

namespace ztermy::ui
{
inline bool verifyDetachedSmartSplit(AppController &controller, QQuickWindow &source)
{
    using namespace std::chrono_literals;
    const auto originalGeometry = source.geometry();
    const auto owner = source.property("ownerWindowId").toString();
    const auto previous = source.property("workspaceId").toString();
    const auto id = controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt"));
    if (id.isEmpty() || !controller.insertTerminalWorkspace(id, 0, owner))
        return false;
    const auto cleanup = qScopeGuard([&] {
        controller.closeTerminalTab(id, previous);
        source.setGeometry(originalGeometry);
        processWindowEventsFor(300ms);
    });
    source.resize(1000, 800);
    processWindowEventsFor(400ms);
    if (!controller.splitActiveTerminal(QStringLiteral("auto"), true))
        return false;
    processWindowEventsFor(400ms);
    const auto first = controller.terminalWorkspace(id).value(QStringLiteral("root")).toMap();
    if (first.value(QStringLiteral("orientation")).toString() != QStringLiteral("horizontal")
        || !controller.splitActiveTerminal(QStringLiteral("auto"), true))
        return false;
    processWindowEventsFor(400ms);
    const auto second = controller.terminalWorkspace(id).value(QStringLiteral("root")).toMap();
    const bool adaptive = second.value(QStringLiteral("second")).toMap().value(QStringLiteral("orientation")).toString()
                          == QStringLiteral("vertical");
    qInfo() << "Smart split: wide viewport horizontal, resulting tall pane vertical:" << adaptive;
    return adaptive;
}

inline bool verifyDetachedPaneZoom(NativeWindow &mainWindow, AppController &controller, QQuickWindow &source,
                                   const QString &workspaceId, const QString &otherId)
{
    using namespace std::chrono_literals;
    const auto mainLayout = mainWindow.rootObject()->property("terminalLayoutRoot").toMap();
    if (!controller.activateTerminalTab(workspaceId)
        || !controller.splitActiveTerminal(QStringLiteral("horizontal"), true))
        return false;
    processWindowEventsFor(400ms);
    const auto layout = controller.terminalWorkspace(workspaceId).value(QStringLiteral("root")).toMap();
    const auto paneId = controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString();
    auto *viewport = detachedVisualQuickItem(source.contentItem(), QStringLiteral("detachedWorkspaceViewport"));
    if (!viewport || !QMetaObject::invokeMethod(viewport, "zoomPaneRequested", Q_ARG(QString, paneId)))
        return false;
    processWindowEventsFor(300ms);
    const auto node = viewport->property("node").toMap();
    const bool zoomed = node.value(QStringLiteral("kind")).toString() == QStringLiteral("leaf")
                        && node.value(QStringLiteral("id")).toString() == paneId;
    const bool switched = controller.activateTerminalTab(otherId);
    processWindowEventsFor(200ms);
    const bool isolated = source.property("zoomedPaneId").toString().isEmpty();
    const bool returned = controller.activateTerminalTab(workspaceId);
    processWindowEventsFor(200ms);
    const bool retained = source.property("zoomedPaneId").toString() == paneId
                          && viewport->property("node").toMap().value(QStringLiteral("id")).toString() == paneId;
    const bool toggled = QMetaObject::invokeMethod(viewport, "zoomPaneRequested", Q_ARG(QString, paneId));
    processWindowEventsFor(300ms);
    const bool restored = viewport->property("node").toMap() == layout;
    const bool mainUnchanged = mainWindow.rootObject()->property("terminalLayoutRoot").toMap() == mainLayout;
    const bool removed = controller.activateTerminalPane(paneId) && controller.closeActiveTerminalPane();
    processWindowEventsFor(300ms);
    qInfo() << "Detached pane zoom: zoomed, other tab isolated, retained, restored, main unchanged:" << zoomed
            << isolated << retained << restored << mainUnchanged;
    return zoomed && switched && isolated && returned && retained && toggled && restored && mainUnchanged && removed;
}

inline bool verifyDetachedSearch(NativeWindow &mainWindow, AppController &controller, QQuickWindow &source,
                                 const QString &workspaceId, const QString &mainId)
{
    using namespace std::chrono_literals;
    if (!controller.activateTerminalTab(mainId))
        return false;
    controller.searchTerminal(QStringLiteral("main-search-sentinel"), false, false);
    const auto mainSearchVisible = mainWindow.rootObject()->property("terminalSearchVisible");
    if (!controller.activateTerminalTab(workspaceId))
        return false;
    windowing::present(source);
    processWindowEventsFor(300ms);
    const bool opened = QMetaObject::invokeMethod(&source, "openTerminalSearch");
    auto *bar = detachedVisualQuickItem(source.contentItem(), QStringLiteral("detachedTerminalSearch"));
    auto *field = detachedVisualQuickItem(source.contentItem(), QStringLiteral("terminalSearchQuery"));
    if (!bar || !field)
        return false;
    const bool localUi =
        opened && bar->isVisible() && mainWindow.rootObject()->property("terminalSearchVisible") == mainSearchVisible;
    field->setProperty("text", QStringLiteral("pending-detached-query"));
    const bool edited = QMetaObject::invokeMethod(field, "textEdited");
    const bool switched = controller.activateTerminalTab(mainId);
    processWindowEventsFor(400ms);
    const bool protectedMain = controller.terminalSearchQuery() == QStringLiteral("main-search-sentinel");
    const bool returned = controller.activateTerminalTab(workspaceId);
    processWindowEventsFor(200ms);
    field->setProperty("text", QStringLiteral("detached-query"));
    const bool searched = QMetaObject::invokeMethod(bar, "search", Q_ARG(QVariant, false));
    processWindowEventsFor(200ms);
    const bool localQuery = controller.terminalSearchQuery() == QStringLiteral("detached-query");
    const bool closed = QMetaObject::invokeMethod(bar, "closeSearch");
    const bool cleared = !bar->isVisible() && controller.terminalSearchQuery().isEmpty();
    controller.activateTerminalTab(mainId);
    controller.clearTerminalSearch();
    windowing::present(mainWindow);
    processWindowEventsFor(300ms);
    const bool mainOpened = QMetaObject::invokeMethod(mainWindow.rootObject(), "openTerminalSearch");
    auto *mainBar = detachedVisualQuickItem(mainWindow.contentItem(), QStringLiteral("mainTerminalSearch"));
    auto *mainField = detachedVisualQuickItem(mainWindow.contentItem(), QStringLiteral("terminalSearchQuery"));
    bool mainWorks = mainOpened && mainBar && mainField && mainBar->isVisible() && !bar->isVisible();
    if (mainWorks)
    {
        mainField->setProperty("text", QStringLiteral("main-local-query"));
        mainWorks = QMetaObject::invokeMethod(mainBar, "search", Q_ARG(QVariant, false))
                    && controller.terminalSearchQuery() == QStringLiteral("main-local-query");
        QMetaObject::invokeMethod(mainBar, "closeSearch");
        mainWorks = mainWorks && !mainBar->isVisible() && controller.terminalSearchQuery().isEmpty();
    }
    controller.activateTerminalTab(workspaceId);
    windowing::present(source);
    processWindowEventsFor(200ms);
    const auto chord = [&](const std::vector<WORD> &keys) {
        const auto handle = reinterpret_cast<HWND>(source.winId()); // NOLINT(performance-no-int-to-ptr)
        if (GetForegroundWindow() != handle || std::ranges::any_of(keys, [](const WORD key) {
                return (GetAsyncKeyState(key) & 0x8000) != 0;
            }))
            return false;
        std::vector<INPUT> inputs;
        for (const auto key : keys)
        {
            INPUT input{};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = key;
            if (key == VK_LEFT || key == VK_RIGHT || key == VK_UP || key == VK_DOWN)
                input.ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
            inputs.push_back(input);
        }
        for (const auto key : std::views::reverse(keys))
        {
            INPUT input{};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = key;
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            if (key == VK_LEFT || key == VK_RIGHT || key == VK_UP || key == VK_DOWN)
                input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
            inputs.push_back(input);
        }
        const bool sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT)) == inputs.size();
        processWindowEventsFor(200ms);
        return sent;
    };
    const bool defaultOpened =
        mainBar && chord({VK_CONTROL, VK_SHIFT, 'F'}) && bar->isVisible() && !mainBar->isVisible();
    const bool defaultClosed = chord({VK_CONTROL, VK_SHIFT, 'F'}) && !bar->isVisible();
    const auto changed = controller.setActionShortcut(QStringLiteral("terminal.find"), QStringLiteral("F8"));
    processWindowEventsFor(100ms);
    const bool rebound = mainBar && chord({VK_F8}) && bar->isVisible() && !mainBar->isVisible();
    QMetaObject::invokeMethod(bar, "closeSearch");
    controller.resetActionShortcut(QStringLiteral("terminal.find"));
    QString otherId;
    for (const auto &entry : controller.terminalTabs())
    {
        const auto tab = entry.toMap();
        if (tab.value(QStringLiteral("windowId")).toString() == source.property("ownerWindowId").toString()
            && tab.value(QStringLiteral("id")).toString() != workspaceId)
            otherId = tab.value(QStringLiteral("id")).toString();
    }
    const auto mainLayout = controller.terminalWorkspace(mainId).value(QStringLiteral("root"));
    const int paneCount = controller.terminalWorkspace(workspaceId).value(QStringLiteral("paneCount")).toInt();
    const bool next = !otherId.isEmpty() && chord({VK_CONTROL, VK_TAB}) && controller.activeTerminalTabId() == otherId
                      && source.property("workspaceId").toString() == otherId;
    const bool previous = chord({VK_CONTROL, VK_SHIFT, VK_TAB}) && controller.activeTerminalTabId() == workspaceId
                          && source.property("workspaceId").toString() == workspaceId;
    const bool split =
        chord({VK_MENU, VK_SHIFT, 'H'})
        && controller.terminalWorkspace(workspaceId).value(QStringLiteral("paneCount")).toInt() == paneCount + 1;
    const auto activePane = [&] {
        return controller.terminalWorkspace(workspaceId).value(QStringLiteral("activePaneId")).toString();
    };
    const auto splitPane = activePane();
    const bool focusNext = chord({VK_MENU, VK_RIGHT}) && activePane() != splitPane
                           && qobject_cast<TerminalItem *>(source.activeFocusItem()) != nullptr;
    qInfo() << "Detached focus next details: before/after/focus class:" << splitPane << activePane()
            << (source.activeFocusItem() ? source.activeFocusItem()->metaObject()->className() : "none");
    const bool focusPrevious = chord({VK_MENU, VK_LEFT}) && activePane() == splitPane
                               && qobject_cast<TerminalItem *>(source.activeFocusItem()) != nullptr;
    // Session status/title data can change while native key events are pumped.
    // Compare the complete layout topology and ratios, not those live Tab fields.
    const std::function<QVariantMap(const QVariantMap &)> geometry = [&](const QVariantMap &node) {
        QVariantMap result{{QStringLiteral("id"), node.value(QStringLiteral("id"))},
                           {QStringLiteral("kind"), node.value(QStringLiteral("kind"))}};
        if (node.value(QStringLiteral("kind")).toString() == QStringLiteral("split"))
        {
            for (const auto &key : {QStringLiteral("orientation"), QStringLiteral("ratio")})
                result.insert(key, node.value(key));
            for (const auto &key : {QStringLiteral("first"), QStringLiteral("second")})
                result.insert(key, geometry(node.value(key).toMap()));
        }
        return result;
    };
    const auto splitRoot = controller.terminalWorkspace(workspaceId).value(QStringLiteral("root")).toMap();
    const auto splitGeometry = geometry(splitRoot);
    const bool grow =
        chord({VK_MENU, VK_SHIFT, VK_RIGHT})
        && geometry(controller.terminalWorkspace(workspaceId).value(QStringLiteral("root")).toMap()) != splitGeometry;
    qInfo() << "Detached resize details: before/after ratio:" << splitRoot.value(QStringLiteral("ratio"))
            << controller.terminalWorkspace(workspaceId)
                   .value(QStringLiteral("root"))
                   .toMap()
                   .value(QStringLiteral("ratio"));
    const bool shrinkKey = chord({VK_MENU, VK_SHIFT, VK_LEFT});
    const auto shrunkRoot = controller.terminalWorkspace(workspaceId).value(QStringLiteral("root")).toMap();
    const bool shrink = shrinkKey && geometry(shrunkRoot) == splitGeometry;
    qInfo() << "Detached resize restored geometry / complete live model:" << shrink << (shrunkRoot == splitRoot);
    const auto paneOrder = [&](bool sessions = false) {
        QStringList ids;
        const std::function<void(const QVariantMap &)> visit = [&](const QVariantMap &node) {
            if (node.value(QStringLiteral("kind")).toString() == QStringLiteral("leaf"))
                ids.push_back(
                    sessions ? node.value(QStringLiteral("tab")).toMap().value(QStringLiteral("sessionId")).toString()
                             : node.value(QStringLiteral("id")).toString());
            else if (node.value(QStringLiteral("kind")).toString() == QStringLiteral("split"))
            {
                visit(node.value(QStringLiteral("first")).toMap());
                visit(node.value(QStringLiteral("second")).toMap());
            }
        };
        visit(controller.terminalWorkspace(workspaceId).value(QStringLiteral("root")).toMap());
        return ids;
    };
    const auto beforeSwap = paneOrder();
    const auto beforeSessions = paneOrder(true);
    auto expectedSwap = beforeSessions;
    bool swapPrepared = expectedSwap.size() >= 2 && !expectedSwap.front().isEmpty();
    for (qsizetype attempts = 0; swapPrepared && activePane() != beforeSwap.front() && attempts < beforeSwap.size();
         ++attempts)
        swapPrepared = chord({VK_MENU, VK_RIGHT});
    swapPrepared = swapPrepared && activePane() == beforeSwap.front();
    if (swapPrepared)
        expectedSwap.swapItemsAt(0, 1);
    const auto nextBinding =
        controller.setActionShortcut(QStringLiteral("terminal.swapNextPane"), QStringLiteral("F9"));
    const auto previousBinding =
        controller.setActionShortcut(QStringLiteral("terminal.swapPreviousPane"), QStringLiteral("F10"));
    processWindowEventsFor(100ms);
    const bool swapNext = swapPrepared && nextBinding.value(QStringLiteral("valid")).toBool() && chord({VK_F9})
                          && paneOrder(true) == expectedSwap;
    const bool swapPrevious = swapNext && previousBinding.value(QStringLiteral("valid")).toBool() && chord({VK_F10})
                              && paneOrder(true) == beforeSessions;
    controller.resetActionShortcut(QStringLiteral("terminal.swapNextPane"));
    qInfo() << "Detached swap prepared/bindings/orders:" << swapPrepared << nextBinding << previousBinding
            << beforeSessions << expectedSwap << paneOrder(true);
    controller.resetActionShortcut(QStringLiteral("terminal.swapPreviousPane"));
    controller.activateTerminalPane(splitPane);
    processWindowEventsFor(100ms);
    const bool closePane =
        chord({VK_CONTROL, VK_SHIFT, 'W'})
        && controller.terminalWorkspace(workspaceId).value(QStringLiteral("paneCount")).toInt() == paneCount;
    const bool mainIntact = controller.terminalWorkspace(mainId).value(QStringLiteral("root")) == mainLayout
                            && mainWindow.rootObject()->property("mainWorkspaceId").toString() == mainId;
    // Keep a different first main Tab so a fallback-to-first bug cannot pass.
    const auto extraMain = controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt"));
    const bool prepared = !extraMain.isEmpty() && controller.insertTerminalWorkspace(extraMain, 0)
                          && controller.activateTerminalTab(mainId);
    processWindowEventsFor(200ms);
    controller.activateTerminalTab(workspaceId);
    processWindowEventsFor(200ms);
    const bool newKey = prepared && chord({VK_CONTROL, VK_SHIFT, 'T'});
    const auto createdId = controller.activeTerminalTabId();
    const bool createdHere = newKey && createdId != workspaceId && createdId != mainId && createdId != extraMain
                             && controller.terminalWorkspace(createdId).value(QStringLiteral("windowId")).toString()
                                    == source.property("ownerWindowId").toString()
                             && source.property("workspaceId").toString() == createdId
                             && mainWindow.rootObject()->property("mainWorkspaceId").toString() == mainId;
    const bool newClosed = createdHere && chord({VK_CONTROL, VK_SHIFT, 'W'})
                           && controller.terminalWorkspace(createdId).isEmpty()
                           && controller.activeTerminalTabId() != mainId;
    auto *newButton = detachedVisualQuickItem(source.contentItem(), QStringLiteral("detachedNewTerminalButton"));
    const bool plusInvoked = newButton && QMetaObject::invokeMethod(newButton, "activated");
    processWindowEventsFor(250ms);
    const auto plusId = controller.activeTerminalTabId();
    const bool plusIsolated = plusInvoked && plusId != workspaceId && plusId != otherId
                              && controller.terminalWorkspace(plusId).value(QStringLiteral("windowId")).toString()
                                     == source.property("ownerWindowId").toString()
                              && mainWindow.rootObject()->property("mainWorkspaceId").toString() == mainId;
    if (plusInvoked && plusId != workspaceId && plusId != otherId && plusId != mainId && plusId != extraMain)
        controller.closeTerminalTab(plusId);
    controller.closeTerminalTab(extraMain);
    controller.activateTerminalTab(workspaceId);
    processWindowEventsFor(200ms);
    qInfo() << "Detached new/close Tab keys: created in own window, closed without main selection loss:" << createdHere
            << newClosed;
    qInfo() << "Detached plus-button activation preserves main selection:" << plusIsolated;
    qInfo() << "Detached native keys: next, previous, split, close pane, main unchanged:" << next << previous << split
            << closePane << mainIntact;
    qInfo() << "Detached pane keys: focus next/previous, grow/shrink, swap next/previous:" << focusNext << focusPrevious
            << grow << shrink << swapNext << swapPrevious;
    qInfo() << "Detached search native shortcut: default open/close, rebind:" << defaultOpened << defaultClosed
            << rebound << changed;
    qInfo() << "Detached search: local UI, canceled deferred cross-window search, local query, close:" << localUi
            << protectedMain << localQuery << cleared << "main reused bar:" << mainWorks;
    return localUi && edited && switched && protectedMain && returned && searched && localQuery && closed && cleared
           && mainWorks && defaultOpened && defaultClosed && rebound && next && previous && split && closePane
           && mainIntact && createdHere && newClosed && plusIsolated && focusNext && focusPrevious && grow && shrink
           && swapNext && swapPrevious;
}
} // namespace ztermy::ui
