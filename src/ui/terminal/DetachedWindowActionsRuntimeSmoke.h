#pragma once

#include "ui/terminal/DetachedPaneWindowRuntimeSmoke.h"
#include <ranges>
#include <vector>

namespace ztermy::ui
{
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
            inputs.push_back(input);
        }
        for (const auto key : std::views::reverse(keys))
        {
            INPUT input{};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = key;
            input.ki.dwFlags = KEYEVENTF_KEYUP;
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
    qInfo() << "Detached search native shortcut: default open/close, rebind:" << defaultOpened << defaultClosed
            << rebound << changed;
    qInfo() << "Detached search: local UI, canceled deferred cross-window search, local query, close:" << localUi
            << protectedMain << localQuery << cleared << "main reused bar:" << mainWorks;
    return localUi && edited && switched && protectedMain && returned && searched && localQuery && closed && cleared
           && mainWorks && defaultOpened && defaultClosed && rebound;
}
} // namespace ztermy::ui
