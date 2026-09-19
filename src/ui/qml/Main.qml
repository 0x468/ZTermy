pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window

Rectangle {
    id: root

    required property var controller
    required property var diagnostics
    required property var fontCatalog
    required property var windowChrome
    readonly property int titleBarHeight: Theme.titleBarHeight
    readonly property int captionButtonWidth: 46
    readonly property int titleQuickActionWidth: 40
    readonly property int titleQuickActionsWidth: titleQuickActionWidth * (width < 700 ? 2 : 4)
    readonly property int titleSecurityActionWidth: portableVaultNeedsAttention ? 40 : 0
    readonly property int titleNavigationWidth: Math.max(0, width - (captionButtonWidth * 3) - titleQuickActionsWidth - titleSecurityActionWidth - (width < 700 ? 4 : 24))
    readonly property color backgroundColor: Theme.windowBackground
    readonly property color panelColor: Theme.panelBackground
    readonly property color chromeColor: Theme.chromeBackground
    readonly property color contentColor: Theme.contentBackground
    readonly property color workspaceColor: Theme.workspaceBackground
    readonly property color raisedColor: Theme.raisedBackground
    readonly property color elevatedColor: Theme.elevatedBackground
    readonly property color controlColor: Theme.controlBackground
    readonly property color fieldColor: Theme.fieldBackground
    readonly property color borderColor: Theme.border
    readonly property color textColor: Theme.text
    readonly property color mutedColor: Theme.textMuted
    readonly property color accentColor: Theme.accent
    property string currentPage: "hosts"
    property string workspaceSection: "hosts"
    property bool settingsTabOpen: false
    property string renameTerminalTabId: ""
    property string settingsReturnPage: "hosts"
    property bool startupVaultPromptPresented: false
    property real pageReveal: 1.0
    property bool terminalSearchVisible: false
    property int pendingPasteLineCount: 0
    property var pendingPasteViewport: null
    property bool appearancePreviewActive: false
    property string previewThemePreference: "dark"
    property string previewBackdropPreference: "acrylic"
    property real previewBackdropOpacity: 1.0
    property string previewEffectsTier: "full"
    property string previewAccentPreference: "ztermy"
    property color previewCustomAccent: "#22C55E"
    property double sessionClock: Date.now()
    property bool windowAlwaysOnTopRequested: false
    property string lastPinPrimaryScope: ""
    property bool lastPinPreviousWindowState: false
    property bool lastPinPreviousTabState: false
    property bool titleBarMetricsPending: false
    property real liveWorkbenchWidth: -1
    property bool workbenchResizeInProgress: false
    property string draggedTerminalTabId: ""
    property real draggedTerminalTabSceneX: 0
    property var paneZoomByWorkspace: ({})
    property var paneHeadersByWorkspace: ({})
    readonly property string zoomedTerminalPaneId: paneZoomByWorkspace[root.mainWorkspaceId] || ""
    readonly property bool paneHeadersVisible: !!paneHeadersByWorkspace[root.mainWorkspaceId]
    property string detachedTerminalWorkspaceId: ""
    property var detachedTerminalWorkspace: ({})
    property string detachedTerminalPaneId: ""
    readonly property var mainTerminalTabs: controller.terminalTabs.filter(tab => !tab.windowId || tab.windowId === "main")
    property string requestedMainWorkspaceId: ""
    readonly property string mainWorkspaceId: mainWorkspace.id || ""
    onMainWorkspaceIdChanged: Qt.callLater(titleTerminalTabs.syncCurrentIndex)
    onMainTerminalTabsChanged: syncMainWorkspace()

    function syncMainWorkspace() {
        const active = controller.activeTerminalTabId;
        if (mainTerminalTabs.some(tab => tab.id === active))
            requestedMainWorkspaceId = active;
        else if (!mainTerminalTabs.some(tab => tab.id === requestedMainWorkspaceId))
            requestedMainWorkspaceId = mainTerminalTabs.length > 0 ? mainTerminalTabs[0].id : "";
        Qt.callLater(refreshMainWorkspace);
    }
    property real workspaceNavigationWidth: controller.windowInteractionSettings.navigationWidth
    property real workspaceNavigationExpandedWidth: controller.windowInteractionSettings.navigationExpandedWidth
    readonly property real workspaceNavigationMinimumWidth: 56
    readonly property real workspaceNavigationMaximumWidth: 320
    readonly property real workspaceNavigationLabelThreshold: 132
    readonly property bool workspaceNavigationCompact: workspaceNavigationWidth < workspaceNavigationLabelThreshold
    readonly property bool currentTerminalTabPinned: currentPage === "terminal" && activeTerminalTab !== null && controller.activeTerminalTabPinned
    readonly property var activeTerminalTab: {
        for (const tab of root.mainTerminalTabs) {
            if (tab.id === root.mainWorkspaceId) {
                return tab;
            }
        }
        return null;
    }
    readonly property bool activeSshFailure: activeTerminalTab !== null && activeTerminalTab.kind === "ssh" && activeTerminalTab.failed
    readonly property bool activeSshConnecting: activeTerminalTab !== null && activeTerminalTab.kind === "ssh" && activeTerminalTab.connecting
    readonly property bool activeSshReconnecting: activeTerminalTab !== null && activeTerminalTab.kind === "ssh" && activeTerminalTab.reconnecting
    readonly property bool activeSshDisconnected: activeTerminalTab !== null && activeTerminalTab.kind === "ssh" && activeTerminalTab.remoteClosed
    readonly property string activeTerminalWorkbenchSide: activeTerminalTab !== null ? activeTerminalTab.workbenchSide : "left"
    readonly property real activeTerminalWorkbenchWidth: activeTerminalTab !== null && activeTerminalTab.workbenchOpen ? Math.min(liveWorkbenchWidth >= 0 ? liveWorkbenchWidth : activeTerminalTab.workbenchWidth, Math.max(0, terminalBody.width - 240)) : 0
    readonly property real activeTerminalComposerHeight: activeTerminalTab !== null && activeTerminalTab.composerOpen ? Math.min(activeTerminalTab.composerHeight, Math.max(0, terminalBody.height - 120)) : 0
    readonly property bool portableVaultNeedsAttention: controller.effectiveCredentialStorage === "portable" && (!controller.portableVaultInitialized || controller.portableVaultLocked)
    readonly property bool terminalTelemetryVisible: currentPage === "terminal" && visible && root.Window.window !== null && root.Window.window.active
    readonly property string currentContextTitle: {
        if (currentPage === "terminal" && activeTerminalTab !== null)
            return activeTerminalTab.title.length > 0 ? activeTerminalTab.title : qsTr("Terminal");
        if (currentPage === "settings")
            return qsTr("Settings");
        if (currentPage === "sftp")
            return qsTr("SFTP files");
        return qsTr("Workspace");
    }
    readonly property string applicationWindowTitle: qsTr("%1 — ztermy").arg(currentContextTitle)
    property var mainWorkspace: ({})
    function refreshMainWorkspace() {
        mainWorkspace = controller.terminalWorkspace(requestedMainWorkspaceId);
    }

    function activateMainTerminal(workspaceId) {
        if (!controller.activateTerminalTab(workspaceId))
            return;
        requestedMainWorkspaceId = workspaceId;
        refreshMainWorkspace();
        currentPage = "terminal";
        focusTerminalAfterLayout();
    }
    // Session strip actions share the workspace ink because the strip sits on
    // the terminal palette fill, not on the app skin.
    component SessionStripAction: AppIconButton {
        onWorkspace: true
        Layout.preferredWidth: 28
        Layout.preferredHeight: 22
    }
    readonly property var terminalLayoutRoot: mainWorkspace.root || ({})
    readonly property var visibleTerminalLayoutRoot: {
        if (zoomedTerminalPaneId.length > 0)
            return findTerminalPane(terminalLayoutRoot, zoomedTerminalPaneId) || terminalLayoutRoot;
        return terminalLayoutRoot;
    }

    function findTerminalPane(node, paneId) {
        if (!node || !node.kind)
            return null;
        if (node.kind === "leaf")
            return node.id === paneId ? node : null;
        return findTerminalPane(node.first, paneId) || findTerminalPane(node.second, paneId);
    }

    function toggleTerminalPaneZoom(paneId) {
        const next = Object.assign({}, paneZoomByWorkspace);
        next[root.mainWorkspaceId] = zoomedTerminalPaneId === paneId ? "" : paneId;
        paneZoomByWorkspace = next;
    }

    function requestTerminalTabClose(tab) {
        controller.closeTerminalTab(tab.id);
    }

    function toggleTerminalPaneHeaders() {
        const next = Object.assign({}, paneHeadersByWorkspace);
        next[root.mainWorkspaceId] = !paneHeadersVisible;
        paneHeadersByWorkspace = next;
    }

    function detachTerminalPane(paneId) {
        const id = terminalWindows.detachPane(paneId);
        if (id.length > 0) {
            detachedTerminalWorkspaceId = id;
            detachedTerminalWorkspace = controller.terminalWorkspace(id);
            detachedTerminalPaneId = paneId;
        }
    }

    function reattachTerminalPane() {
        reattachWorkspace(detachedTerminalWorkspaceId);
    }

    function reattachWorkspace(workspaceId) {
        if (!controller.reattachTerminalWorkspace(workspaceId))
            return;
        detachedTerminalPaneId = "";
        detachedTerminalWorkspaceId = "";
        detachedTerminalWorkspace = ({});
        const active = controller.terminalWorkspace(controller.activeTerminalTabId);
        if (active.windowId && active.windowId !== "main") {
            terminalWindows.activateWorkspace(active.id);
            return;
        }
        currentPage = "terminal";
        WindowControl.present(windowChrome);
        Qt.callLater(terminalViewport.forceActiveFocus);
    }

    function resizeWorkspaceNavigation(requestedWidth) {
        const boundedWidth = Math.max(workspaceNavigationMinimumWidth, Math.min(workspaceNavigationMaximumWidth, requestedWidth));
        workspaceNavigationWidth = boundedWidth;
        if (boundedWidth >= workspaceNavigationLabelThreshold)
            workspaceNavigationExpandedWidth = boundedWidth;
        navigationSaveTimer.restart();
    }

    Timer {
        id: navigationSaveTimer
        interval: 350
        onTriggered: {
            if (workspaceNavigationResizeHandle.pressed)
                restart();
            else
                root.controller.saveWindowInteractionSettings({
                    navigationWidth: Math.round(root.workspaceNavigationWidth),
                    navigationExpandedWidth: Math.round(root.workspaceNavigationExpandedWidth)
                });
        }
    }

    function toggleWorkspaceNavigation() {
        if (workspaceNavigationCompact) {
            resizeWorkspaceNavigation(Math.max(workspaceNavigationLabelThreshold, workspaceNavigationExpandedWidth));
            return;
        }
        workspaceNavigationExpandedWidth = workspaceNavigationWidth;
        resizeWorkspaceNavigation(workspaceNavigationMinimumWidth);
    }

    Binding {
        target: root.windowChrome
        property: "title"
        value: root.applicationWindowTitle
    }

    color: root.currentPage === "terminal" ? "transparent" : backgroundColor

    FontMetrics {
        id: titleTabFontMetrics

        font.family: Theme.uiFont
        font.pixelSize: Theme.textLabel
    }

    function terminalTabPreferredWidth(title) {
        return Math.min(184, Math.max(112, titleTabFontMetrics.advanceWidth(String(title || "")) + 66));
    }

    function terminalTabStripDesiredWidth() {
        let width = Math.max(0, root.mainTerminalTabs.length - 1) * 2;
        for (let index = 0; index < root.mainTerminalTabs.length; ++index)
            width += root.currentPage === "terminal" && root.mainTerminalTabs[index].id === root.mainWorkspaceId ? terminalTabPreferredWidth(root.mainTerminalTabs[index].title) : 38;
        return width;
    }

    function reportTitleBarMetrics() {
        root.windowChrome.setTitleBarMetrics(titleBarHeight, titleNavigation.width + 8, width - (captionButtonWidth * 3) - titleQuickActionsWidth - titleSecurityActionWidth, width - (captionButtonWidth * 2), captionButtonWidth);
    }

    function scheduleTitleBarMetrics() {
        if (titleBarMetricsPending)
            return;
        titleBarMetricsPending = true;
        Qt.callLater(function () {
            titleBarMetricsPending = false;
            reportTitleBarMetrics();
        });
    }

    function applyAlwaysOnTopPreference() {
        root.windowChrome.setAlwaysOnTop(root.windowAlwaysOnTopRequested || root.currentTerminalTabPinned);
    }

    function toggleActiveTerminalPin() {
        if (root.currentPage !== "terminal" || root.activeTerminalTab === null) {
            return;
        }
        root.controller.toggleActiveTerminalTabPinned();
        Qt.callLater(root.applyAlwaysOnTopPreference);
    }

    function activatePinPrimary() {
        lastPinPreviousWindowState = windowAlwaysOnTopRequested;
        lastPinPreviousTabState = currentTerminalTabPinned;
        if (windowAlwaysOnTopRequested || currentPage !== "terminal" || activeTerminalTab === null) {
            lastPinPrimaryScope = "window";
            setWindowAlwaysOnTopRequested(!windowAlwaysOnTopRequested);
            return;
        }
        lastPinPrimaryScope = "tab";
        toggleActiveTerminalPin();
    }

    function activatePinDouble() {
        if (lastPinPrimaryScope === "tab" && currentTerminalTabPinned !== lastPinPreviousTabState)
            toggleActiveTerminalPin();
        else if (lastPinPrimaryScope === "window" && windowAlwaysOnTopRequested !== lastPinPreviousWindowState)
            setWindowAlwaysOnTopRequested(lastPinPreviousWindowState);
        setWindowAlwaysOnTopRequested(!lastPinPreviousWindowState);
        lastPinPrimaryScope = "";
    }

    function setWindowAlwaysOnTopRequested(enabled) {
        root.windowAlwaysOnTopRequested = enabled;
        root.applyAlwaysOnTopPreference();
    }

    function clearAlwaysOnTopPreference() {
        root.windowAlwaysOnTopRequested = false;
        if (root.currentTerminalTabPinned) {
            root.controller.toggleActiveTerminalTabPinned();
        }
        Qt.callLater(root.applyAlwaysOnTopPreference);
    }

    function requestTerminalCommandRun(command) {
        if (!command || command.trim().length === 0) {
            return;
        }
        if (controller.runTerminalCommand(command)) {
            terminalViewport.forceActiveFocus();
        }
    }

    function requestPortableVaultAccess(sourceItem) {
        if (!controller.portableVaultInitialized) {
            openSecuritySettingsTab();
            return;
        }
        portableVaultUnlockDialog.focusRestoreItem = sourceItem || null;
        portableVaultUnlockPassword.text = "";
        portableVaultUnlockStatus.text = "";
        portableVaultUnlockDialog.open();
    }

    function toggleSessionLog() {
        if (activeTerminalTab === null) {
            return;
        }
        if (activeTerminalTab.logState === "active" || activeTerminalTab.logState === "starting") {
            controller.stopTerminalLog();
        } else {
            const safeTitle = (activeTerminalTab.title || "session").replace(/[\\/:*?"<>|]/g, "-");
            const timestamp = Qt.formatDateTime(new Date(), "yyyy-MM-ddThh-mm-ss");
            sessionLogDialog.currentFile = safeTitle + "_" + timestamp + ".log";
            sessionLogDialog.open();
        }
    }

    function presentStartupVaultPrompt() {
        if (startupVaultPromptPresented || controller.effectiveCredentialStorage !== "portable" || !controller.portableVaultInitialized || !controller.portableVaultLocked) {
            return;
        }
        startupVaultPromptPresented = true;
        requestPortableVaultAccess(null);
    }

    function openSettingsTab() {
        if (currentPage !== "settings") {
            settingsReturnPage = currentPage;
        }
        settingsTabOpen = true;
        currentPage = "settings";
        settingsPane.revealCurrentCategory();
        Qt.callLater(settingsPane.focusCurrentCategory);
    }

    function openSecuritySettingsTab() {
        settingsPane.currentCategory = "security";
        openSettingsTab();
    }

    function editWorkspaceHost(profileId) {
        workspaceSection = "hosts";
        currentPage = "hosts";
        for (const profile of controller.hostProfiles) {
            if (profile.id === profileId) {
                Qt.callLater(() => hostConnectionPane.editProfile(profile));
                return;
            }
        }
    }

    function createWorkspaceHost(host, port) {
        workspaceSection = "hosts";
        currentPage = "hosts";
        Qt.callLater(() => hostConnectionPane.beginNewProfileForHost(host, port));
    }

    function openAiSettingsTab() {
        settingsPane.currentCategory = "ai";
        openSettingsTab();
    }

    function closeSettingsTab() {
        if (!settingsTabOpen) {
            return;
        }
        settingsTabOpen = false;
        currentPage = settingsReturnPage === "settings" ? (root.mainTerminalTabs.length > 0 ? "terminal" : "hosts") : settingsReturnPage;
    }

    function openTerminalSearch() {
        currentPage = "terminal";
        terminalSearchVisible = true;
        searchField.text = controller.terminalSearchQuery;
        caseSensitiveButton.checked = controller.terminalSearchCaseSensitive;
        searchField.forceActiveFocus();
        searchField.selectAll();
    }

    function toggleTerminalSearch() {
        if (terminalSearchVisible) {
            closeTerminalSearch();
        } else {
            openTerminalSearch();
        }
    }

    function closeTerminalSearch() {
        terminalSearchVisible = false;
        searchDelay.stop();
        controller.clearTerminalSearch();
        terminalViewport.forceActiveFocus();
    }

    function applyWindowAppearance() {
        root.windowChrome.applyAppearance(Theme.effectiveBackdrop, Theme.dark);
        terminalWindows.applyAppearance();
    }

    function previewWindowAppearance(theme, opacity, backdrop, accent, customAccent, effects) {
        previewThemePreference = theme;
        previewBackdropPreference = root.windowChrome.opaqueSurface ? "solid" : backdrop;
        previewBackdropOpacity = opacity;
        previewAccentPreference = accent;
        previewCustomAccent = customAccent;
        previewEffectsTier = effects;
        appearancePreviewActive = true;
        Qt.callLater(() => root.windowChrome.applyAppearance(Theme.effectiveBackdrop, Theme.dark));
    }

    function endWindowAppearancePreview() {
        if (!appearancePreviewActive) {
            return;
        }
        appearancePreviewActive = false;
        Qt.callLater(root.applyWindowAppearance);
    }

    function startLocalTerminalTab(shellId) {
        // Create the tab before switching pages: switching first focused the
        // previous tab's viewport, and that focus change re-activated the
        // previous tab underneath the new one.
        const tabId = shellId && shellId.length > 0 ? controller.startLocalTerminalWithShell(shellId) : controller.startLocalTerminal();
        if (tabId.length > 0)
            activateMainTerminal(tabId);
        else
            currentPage = "terminal";
    }

    function focusTerminalAfterLayout() {
        // A new recursive TerminalSplitNode Loader is materialized after the
        // controller publishes its workspace model. One extra event-loop turn
        // avoids focusing the viewport from the previous tab.
        Qt.callLater(() => Qt.callLater(() => {
                if (root.currentPage === "terminal" && !renameTerminalDialog.visible && root.Window.window.active && !terminalWindows.movingWindow && !terminalWindows.draggedPaneId.length)
                    terminalViewport.forceActiveFocus();
            }));
    }

    function closeActiveTerminalTab() {
        if (root.mainWorkspaceId.length === 0) {
            return;
        }
        controller.closeTerminalTab(root.mainWorkspaceId);
    }

    function openTerminalRename(tabId, title) {
        renameTerminalTabId = tabId;
        renameTerminalTitleField.text = title;
        renameTerminalDialog.open();
    }

    function activateRelativeTerminalTab(offset) {
        const tabs = root.mainTerminalTabs;
        if (tabs.length < 2) {
            return;
        }
        let currentIndex = 0;
        for (let index = 0; index < tabs.length; ++index) {
            if (tabs[index].id === root.mainWorkspaceId) {
                currentIndex = index;
                break;
            }
        }
        const nextIndex = (currentIndex + offset + tabs.length) % tabs.length;
        activateMainTerminal(tabs[nextIndex].id);
    }

    function shortcutFor(actionId) {
        for (let index = 0; index < controller.actions.length; ++index) {
            if (controller.actions[index].id === actionId) {
                return controller.actions[index].shortcut;
            }
        }
        return "";
    }

    function executeAction(actionId) {
        switch (actionId) {
        case "application.commandPalette":
            commandPalette.open();
            break;
        case "application.hosts":
            currentPage = "hosts";
            break;
        case "application.settings":
            openSettingsTab();
            break;
        case "application.transfers":
            transferCenter.open();
            break;
        case "scripts.import":
            scriptImportDialog.open();
            break;
        case "scripts.export":
            scriptExportDialog.open();
            break;
        case "terminal.newLocal":
            startLocalTerminalTab();
            break;
        case "tabs.close":
            controller.closeActiveTerminalPane();
            break;
        case "tabs.duplicate":
            controller.duplicateTerminalTab(root.mainWorkspaceId);
            break;
        case "tabs.reopenClosed":
            controller.reopenLastClosedTerminalTab();
            currentPage = "terminal";
            break;
        case "tabs.next":
            activateRelativeTerminalTab(1);
            break;
        case "tabs.previous":
            activateRelativeTerminalTab(-1);
            break;
        case "terminal.find":
            toggleTerminalSearch();
            break;
        case "terminal.splitHorizontal":
            controller.splitActiveTerminal("horizontal", false);
            break;
        case "terminal.splitVertical":
            controller.splitActiveTerminal("vertical", false);
            break;
        case "terminal.duplicatePane":
            controller.splitActiveTerminal("horizontal", true);
            break;
        case "terminal.focusNextPane":
            controller.focusRelativeTerminalPane(1);
            terminalViewport.forceActiveFocus();
            break;
        case "terminal.focusPreviousPane":
            controller.focusRelativeTerminalPane(-1);
            terminalViewport.forceActiveFocus();
            break;
        case "terminal.growPane":
            controller.resizeActiveTerminalPane(0.05);
            break;
        case "terminal.shrinkPane":
            controller.resizeActiveTerminalPane(-0.05);
            break;
        case "terminal.swapNextPane":
            controller.swapActiveTerminalPane(1);
            break;
        case "terminal.swapPreviousPane":
            controller.swapActiveTerminalPane(-1);
            break;
        case "terminal.history":
            currentPage = "terminal";
            controller.toggleTerminalWorkbench("history");
            break;
        case "terminal.scripts":
            currentPage = "terminal";
            controller.toggleTerminalWorkbench("scripts");
            break;
        case "terminal.sftp":
            currentPage = "terminal";
            controller.toggleTerminalWorkbench("sftp");
            break;
        case "terminal.quickSelect":
            currentPage = "terminal";
            terminalViewport.startQuickSelect();
            break;
        case "terminal.copyMode":
            currentPage = "terminal";
            terminalViewport.startCopyMode();
            break;
        case "terminal.composer":
            currentPage = "terminal";
            controller.toggleTerminalComposer();
            break;
        case "terminal.sessionLog":
            toggleSessionLog();
            break;
        case "terminal.hideWorkbench":
            controller.closeTerminalWorkbench();
            terminalViewport.forceActiveFocus();
            break;
        case "terminal.moveWorkbench":
            controller.moveTerminalWorkbench();
            break;
        case "terminal.copyAddress":
            controller.copyActiveTerminalAddress();
            break;
        case "terminal.copy":
            terminalViewport.copySelection();
            break;
        case "terminal.paste":
            terminalViewport.pasteClipboard();
            break;
        case "terminal.selectVisible":
            terminalViewport.selectVisibleTerminal();
            break;
        case "terminal.selectAll":
            terminalViewport.selectAllTerminal();
            break;
        case "terminal.scrollLineUp":
            terminalViewport.scrollLines(-1);
            break;
        case "terminal.scrollLineDown":
            terminalViewport.scrollLines(1);
            break;
        case "terminal.scrollPageUp":
            terminalViewport.scrollPage(-1);
            break;
        case "terminal.scrollPageDown":
            terminalViewport.scrollPage(1);
            break;
        case "terminal.scrollTop":
            terminalViewport.scrollToFraction(0.0);
            break;
        case "terminal.scrollBottom":
            terminalViewport.scrollToFraction(1.0);
            break;
        }
    }

    function formatSessionDuration(startedUtcMs) {
        if (!startedUtcMs || startedUtcMs <= 0) {
            return "";
        }
        const seconds = Math.max(0, Math.floor((sessionClock - startedUtcMs) / 1000));
        const hours = Math.floor(seconds / 3600);
        const minutes = Math.floor((seconds % 3600) / 60);
        const remainingSeconds = seconds % 60;
        const paddedMinutes = String(minutes).padStart(2, "0");
        const paddedSeconds = String(remainingSeconds).padStart(2, "0");
        return hours > 0 ? hours + ":" + paddedMinutes + ":" + paddedSeconds : paddedMinutes + ":" + paddedSeconds;
    }

    Binding {
        target: Theme
        property: "preference"
        value: root.appearancePreviewActive ? root.previewThemePreference : root.controller.themePreference
    }

    Binding {
        target: Theme
        property: "systemDark"
        value: root.windowChrome.systemDarkMode
    }

    Binding {
        target: Theme
        property: "animationsEnabled"
        value: root.windowChrome.animationsEnabled
    }

    Binding {
        target: Theme
        property: "highContrast"
        value: root.windowChrome.highContrast
    }

    Binding {
        target: Theme
        property: "highContrastBackground"
        value: root.windowChrome.highContrastBackground
    }

    Binding {
        target: Theme
        property: "highContrastText"
        value: root.windowChrome.highContrastText
    }

    Binding {
        target: Theme
        property: "highContrastHighlight"
        value: root.windowChrome.highContrastHighlight
    }

    Binding {
        target: Theme
        property: "highContrastHighlightText"
        value: root.windowChrome.highContrastHighlightText
    }

    Binding {
        target: Theme
        property: "backdropPreference"
        value: root.appearancePreviewActive ? root.previewBackdropPreference : root.windowChrome.opaqueSurface ? "solid" : root.controller.backdropPreference
    }

    Binding {
        target: Theme
        property: "backdropOpacity"
        value: root.appearancePreviewActive ? root.previewBackdropOpacity : root.controller.backdropOpacity
    }

    Binding {
        target: Theme
        property: "effectsTier"
        value: root.appearancePreviewActive ? root.previewEffectsTier : root.controller.effectsTier
    }

    Binding {
        target: Theme
        property: "accentPreference"
        value: root.appearancePreviewActive ? root.previewAccentPreference : root.controller.accentPreference
    }

    Binding {
        target: Theme
        property: "customAccent"
        value: root.appearancePreviewActive ? root.previewCustomAccent : root.controller.customAccent
    }

    Binding {
        target: Theme
        property: "terminalPalette"
        value: root.controller.terminalThemeColors
    }

    Binding {
        target: Theme
        property: "systemAccent"
        value: root.windowChrome.systemAccentColor
    }

    Binding {
        target: Theme
        property: "uiFont"
        value: root.fontCatalog.effectiveUiFamily(root.controller.uiFontFamily)
    }

    Component.onCompleted: {
        Qt.callLater(root.refreshMainWorkspace);
        reportTitleBarMetrics();
        applyWindowAppearance();
        Qt.callLater(root.applyAlwaysOnTopPreference);
        controller.setTerminalTelemetryVisible(terminalTelemetryVisible);
        Qt.callLater(root.presentStartupVaultPrompt);
    }
    onTerminalTelemetryVisibleChanged: {
        controller.setTerminalTelemetryVisible(terminalTelemetryVisible);
        if (terminalTelemetryVisible && requestedMainWorkspaceId.length > 0)
            controller.activateTerminalTab(requestedMainWorkspaceId);
    }
    onWidthChanged: scheduleTitleBarMetrics()
    onCurrentPageChanged: {
        Qt.callLater(root.applyAlwaysOnTopPreference);
        pageReveal = Motion.enabled ? 0.0 : 1.0;
        if (Motion.enabled) {
            pageEntryAnimation.restart();
        }
        if (currentPage === "settings") {
            settingsTabOpen = true;
        }
        if (currentPage !== "settings") {
            endWindowAppearancePreview();
        }
        if (currentPage === "terminal") {
            focusTerminalAfterLayout();
            terminalViewport.requestCurrentSize();
        }
    }

    Repeater {
        model: root.controller.actions

        Item {
            id: registryShortcutDelegate

            required property var modelData
            width: 0
            height: 0

            Shortcut {
                sequence: registryShortcutDelegate.modelData.shortcut
                enabled: registryShortcutDelegate.modelData.shortcut.length > 0 && registryShortcutDelegate.modelData.enabled && !settingsPane.shortcutRecording && !commandPalette.visible
                autoRepeat: registryShortcutDelegate.modelData.autoRepeat
                context: Qt.WindowShortcut
                onActivated: root.controller.triggerAction(registryShortcutDelegate.modelData.id)
            }
        }
    }

    Connections {
        target: root.controller

        function onTerminalSearchChanged() {
            if (!root.terminalSearchVisible) {
                return;
            }
            if (searchField.text !== root.controller.terminalSearchQuery) {
                searchField.text = root.controller.terminalSearchQuery;
            }
            caseSensitiveButton.checked = root.controller.terminalSearchCaseSensitive;
        }

        function onApplicationSettingsChanged() {
            if (!workspaceNavigationResizeHandle.pressed) {
                root.workspaceNavigationWidth = root.controller.windowInteractionSettings.navigationWidth;
                root.workspaceNavigationExpandedWidth = root.controller.windowInteractionSettings.navigationExpandedWidth;
            }
            Qt.callLater(root.applyWindowAppearance);
        }

        function onTerminalTabsChanged() {
            Qt.callLater(titleTerminalTabs.syncCurrentIndex);
            Qt.callLater(root.applyAlwaysOnTopPreference);
            if (root.currentPage === "terminal" && root.mainTerminalTabs.length === 0) {
                root.currentPage = "hosts";
                Qt.callLater(hostsTitleTab.focusAction);
            }
            if (root.detachedTerminalPaneId.length > 0) {
                root.detachedTerminalWorkspace = root.controller.terminalWorkspace(root.detachedTerminalWorkspaceId);
                if (!root.detachedTerminalWorkspace.id || root.detachedTerminalWorkspace.windowId === "main") {
                    root.detachedTerminalPaneId = "";
                    root.detachedTerminalWorkspaceId = "";
                    root.detachedTerminalWorkspace = ({});
                }
            }
        }

        function onActiveTerminalTabChanged() {
            root.syncMainWorkspace();
            root.liveWorkbenchWidth = -1;
            Qt.callLater(titleTerminalTabs.syncCurrentIndex);
            Qt.callLater(root.applyAlwaysOnTopPreference);
        }

        function onTerminalWorkspaceChanged() {
            Qt.callLater(root.refreshMainWorkspace);
        }

        function onActiveTerminalTabPinnedChanged() {
            Qt.callLater(root.applyAlwaysOnTopPreference);
        }

        function onCredentialVaultChanged() {
            Qt.callLater(root.reportTitleBarMetrics);
        }

        function onActionRequested(actionId) {
            root.executeAction(actionId);
        }

        function onTransferNotificationRequested(notification) {
            transferToast.present(notification);
        }

        function onTerminalKeywordHighlightAdded(tabId, ruleId, pattern) {
            terminalActionToast.present({
                title: qsTr("Keyword highlight added"),
                message: pattern,
                actionText: qsTr("Undo"),
                payload: {
                    tabId: tabId,
                    ruleId: ruleId
                }
            });
        }
    }

    Timer {
        interval: 1000
        running: root.visible
        repeat: true
        onTriggered: root.sessionClock = Date.now()
    }

    MotionReveal {
        id: pageEntryAnimation

        target: root
        property: "pageReveal"
    }

    Connections {
        target: root.windowChrome
        function onWindowClosing(quitApplication) {
            if (!quitApplication && root.controller.terminalTabs.some(tab => tab.windowId && tab.windowId !== "main")) {
                const ids = root.mainTerminalTabs.map(tab => tab.id);
                for (const id of ids)
                    root.controller.closeTerminalTab(id);
            }
        }
        function onDetachedWindowMoving(window, globalPosition) {
            terminalWindows.updateWindowDrop(window, globalPosition);
        }
        function onDetachedWindowMoved(window, globalPosition, cancelled) {
            terminalWindows.finishWindowDrop(window, cancelled);
        }

        function onSystemDarkModeChanged() {
            if (root.appearancePreviewActive) {
                Qt.callLater(() => root.previewWindowAppearance(root.previewThemePreference, root.previewBackdropOpacity, root.previewBackdropPreference, root.previewAccentPreference, root.previewCustomAccent, root.previewEffectsTier));
            } else {
                Qt.callLater(root.applyWindowAppearance);
            }
        }

        function onHighContrastChanged() {
            Qt.callLater(root.applyWindowAppearance);
        }
    }

    Rectangle {
        id: titleBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.titleBarHeight
        color: Theme.chromeBackground

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: root.borderColor
        }

        Row {
            id: titleNavigation

            objectName: "titleNavigation"
            anchors.left: parent.left
            anchors.top: parent.top
            width: childrenRect.width
            height: parent.height
            spacing: 0
            onWidthChanged: root.scheduleTitleBarMetrics()

            TitleTab {
                id: hostsTitleTab

                width: root.currentPage === "hosts" && root.width >= 700 ? 124 : 44
                height: titleNavigation.height
                title: qsTr("Workspace")
                titlePixelSize: Theme.textBody
                customIcon: true
                iconSize: 24
                selected: root.currentPage === "hosts"
                compact: root.currentPage !== "hosts" || root.width < 700
                actionObjectName: "hostsTitleAction"
                accessibleName: qsTr("Workspace")
                toolTip: qsTr("Workspace")
                toolTipEnabled: root.currentPage !== "hosts"
                onActivated: root.currentPage = "hosts"

                Behavior on width {
                    MotionRelocate {}
                }

                BrandIcon {
                    objectName: "titleBrandIcon"
                    parent: hostsTitleTab.iconSlot
                    anchors.fill: parent
                    tileColor: root.accentColor
                    ribbonColor: Theme.accentText
                    promptColor: Theme.contrastText(ribbonColor)
                    promptStrokeWidth: 0.86
                }
            }

            TitleTab {
                id: sftpTitleTab

                width: root.currentPage === "sftp" && root.width >= 700 ? 82 : 38
                height: titleNavigation.height
                title: qsTr("SFTP")
                iconName: "folder"
                selected: root.currentPage === "sftp"
                compact: root.currentPage !== "sftp" || root.width < 700
                actionObjectName: "sftpTitleAction"
                accessibleName: qsTr("SFTP files")
                toolTip: qsTr("Open local and remote files")
                onActivated: root.currentPage = "sftp"

                Behavior on width {
                    MotionRelocate {}
                }
            }

            TerminalTabAction {
                id: settingsTitleTab

                visible: root.settingsTabOpen
                width: visible ? (compact ? 38 : 112) : 0
                height: titleNavigation.height
                title: qsTr("Settings")
                iconName: "settings"
                compact: root.currentPage !== "settings"
                actionObjectName: "settingsTitleAction"
                closeActionObjectName: "settingsTitleCloseAction"
                selected: root.currentPage === "settings"
                onActivated: root.openSettingsTab()
                onCloseRequested: root.closeSettingsTab()
            }

            ListView {
                id: titleTerminalTabs

                objectName: "titleTerminalTabs"
                readonly property real availableWidth: Math.max(0, root.titleNavigationWidth - hostsTitleTab.width - sftpTitleTab.width - 36 - settingsTitleTab.width - titleTabOverflow.width)
                readonly property real desiredTabWidth: root.terminalTabStripDesiredWidth()
                currentIndex: -1
                width: count === 0 ? 0 : Math.min(availableWidth, desiredTabWidth)
                height: titleNavigation.height
                orientation: ListView.Horizontal
                spacing: 2
                clip: true
                interactive: false
                model: root.mainTerminalTabs
                onCountChanged: Qt.callLater(ensureCurrentTabVisible)
                onCurrentIndexChanged: Qt.callLater(ensureCurrentTabVisible)
                onWidthChanged: Qt.callLater(ensureCurrentTabVisible)

                WheelHandler {
                    target: null
                    blocking: true
                    onWheel: event => {
                        const delta = event.pixelDelta.x !== 0 ? event.pixelDelta.x : event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.x !== 0 ? event.angleDelta.x / 2 : event.angleDelta.y / 2;
                        titleTerminalTabs.contentX = Math.max(0, Math.min(titleTerminalTabs.contentWidth - titleTerminalTabs.width, titleTerminalTabs.contentX - delta));
                        event.accepted = true;
                    }
                }

                addDisplaced: Transition {
                    MotionRelocate {
                        properties: "x"
                    }
                }

                remove: MotionExit {}

                removeDisplaced: Transition {
                    MotionRelocate {
                        properties: "x"
                    }
                }

                function syncCurrentIndex() {
                    let activeIndex = -1;
                    for (let index = 0; index < root.mainTerminalTabs.length; ++index) {
                        if (root.mainTerminalTabs[index].id === root.mainWorkspaceId) {
                            activeIndex = index;
                            break;
                        }
                    }
                    currentIndex = activeIndex;
                    Qt.callLater(ensureCurrentTabVisible);
                }

                function ensureCurrentTabVisible() {
                    if (currentIndex >= 0 && currentIndex < count) {
                        positionViewAtIndex(currentIndex, ListView.Contain);
                    }
                }

                function updateTabDrag(tabId, sceneX) {
                    root.draggedTerminalTabId = tabId;
                    root.draggedTerminalTabSceneX = sceneX;
                }

                function finishTabDrag(tabId, sceneX) {
                    root.draggedTerminalTabSceneX = sceneX;
                    const localX = mapFromItem(null, sceneX, 0).x;
                    const targetIndex = indexAt(contentX + Math.max(0, Math.min(width - 1, localX)), height / 2);
                    if (targetIndex >= 0)
                        root.controller.moveTerminalTab(tabId, targetIndex);
                    root.draggedTerminalTabId = "";
                }

                Timer {
                    interval: 16
                    repeat: true
                    running: root.draggedTerminalTabId.length > 0
                    onTriggered: {
                        const localX = titleTerminalTabs.mapFromItem(null, root.draggedTerminalTabSceneX, 0).x;
                        if (localX < 28)
                            titleTerminalTabs.contentX = Math.max(0, titleTerminalTabs.contentX - 12);
                        else if (localX > titleTerminalTabs.width - 28)
                            titleTerminalTabs.contentX = Math.min(Math.max(0, titleTerminalTabs.contentWidth - titleTerminalTabs.width), titleTerminalTabs.contentX + 12);
                    }
                }

                delegate: TerminalTabAction {
                    id: titleTerminalTab

                    required property var modelData

                    title: modelData.title
                    workspaceId: modelData.id
                    objectName: "workspaceTitle-" + modelData.id
                    selected: root.currentPage === "terminal" && root.mainWorkspaceId === modelData.id
                    doubleClickAction: root.controller.windowInteractionSettings.tabDoubleClick
                    closeButtonMode: root.controller.windowInteractionSettings.tabCloseButton
                    connecting: modelData.connecting === true
                    running: modelData.running
                    canReconnect: modelData.canReconnect
                    canDuplicate: modelData.canDuplicate
                    canCloseOthers: modelData.canCloseOthers
                    canCloseToRight: modelData.canCloseToRight
                    canMoveLeft: modelData.canMoveLeft
                    canMoveRight: modelData.canMoveRight
                    iconName: "terminal"
                    compact: !selected
                    width: selected ? root.terminalTabPreferredWidth(modelData.title) : 38
                    height: titleTerminalTabs.height
                    onActivated: {
                        root.activateMainTerminal(modelData.id);
                    }
                    onCloseRequested: root.controller.closeTerminalTab(modelData.id)
                    onDuplicateRequested: root.controller.duplicateTerminalTab(modelData.id)
                    onRenameRequested: root.openTerminalRename(modelData.id, modelData.title)
                    onCloseOthersRequested: root.controller.closeOtherTerminalTabs(modelData.id)
                    onCloseToRightRequested: root.controller.closeTerminalTabsToRight(modelData.id)
                    onMoveLeftRequested: root.controller.moveTerminalTab(modelData.id, modelData.tabIndex - 1)
                    onMoveRightRequested: root.controller.moveTerminalTab(modelData.id, modelData.tabIndex + 1)
                    onDragMoved: sceneX => titleTerminalTabs.updateTabDrag(modelData.id, sceneX)
                    onDragFinished: (sceneX, sceneY) => {
                        if (!dropCompleted && sceneY >= 0 && sceneY <= root.titleBarHeight)
                            titleTerminalTabs.finishTabDrag(modelData.id, sceneX);
                        root.draggedTerminalTabId = "";
                    }
                    onReconnectRequested: {
                        root.controller.activateTerminalTab(modelData.id);
                        root.currentPage = "terminal";
                        root.controller.reconnectTerminalTab(modelData.sessionId);
                    }
                }
            }

            TitleTabOverflow {
                id: titleTabOverflow

                width: implicitWidth
                height: titleNavigation.height
                controller: root.controller
                tabs: root.mainTerminalTabs
                iconColor: root.textColor
                onTerminalCloseRequested: tab => root.requestTerminalTabClose(tab)
                onTerminalActivated: tabId => {
                    root.activateMainTerminal(tabId);
                }
            }

            TitleChromeAction {
                id: titleNewTabContainer
                objectName: "titleNewTabContainer"
                width: 36
                height: titleNavigation.height
                iconName: "plus"
                iconColor: root.textColor
                actionObjectName: "titleNewTabAction"
                accessibleName: qsTr("Open new terminal menu")
                toolTip: qsTr("New terminal")
                menuOpen: newTerminalMenu.visible
                onActivated: newTerminalMenu.open()

                AppMenu {
                    id: newTerminalMenu

                    y: parent.height

                    AppMenuItem {
                        objectName: "newLocalTerminalMenuAction"
                        text: qsTr("New local terminal")
                        onTriggered: {
                            root.startLocalTerminalTab();
                            Qt.callLater(terminalViewport.forceActiveFocus);
                        }
                    }

                    AppMenu {
                        id: localShellMenu

                        title: qsTr("New terminal with")

                        Instantiator {
                            model: root.controller.availableLocalShells.filter(shell => shell.id !== "automatic" && shell.available)
                            delegate: AppMenuItem {
                                id: shellMenuItem

                                required property var modelData
                                text: modelData.name
                                onTriggered: root.startLocalTerminalTab(modelData.id)

                                AppToolTip {
                                    text: shellMenuItem.modelData.detail
                                }
                            }
                            onObjectAdded: (index, object) => localShellMenu.insertItem(index, object)
                            onObjectRemoved: (index, object) => localShellMenu.removeItem(object)
                        }
                    }

                    AppMenuItem {
                        objectName: "browseHostsMenuAction"
                        text: qsTr("Browse hosts")
                        onTriggered: {
                            root.currentPage = "hosts";
                            Qt.callLater(hostsTitleTab.focusAction);
                        }
                    }

                    AppMenuSeparator {
                        visible: root.controller.canReopenClosedTerminalTab
                    }

                    AppMenuItem {
                        visible: root.controller.canReopenClosedTerminalTab
                        text: qsTr("Reopen closed terminal")
                        onTriggered: {
                            if (root.controller.reopenLastClosedTerminalTab()) {
                                root.currentPage = "terminal";
                                Qt.callLater(terminalViewport.forceActiveFocus);
                            }
                        }
                    }
                }
            }
        }

        Item {
            id: titleBlankDragRegion

            objectName: "titleBlankDragRegion"
            anchors.left: titleNavigation.right
            anchors.right: titleControls.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom

            DragHandler {
                target: null
                acceptedButtons: Qt.LeftButton
                onActiveChanged: {
                    if (active)
                        root.windowChrome.beginSystemMove();
                }
            }
        }

        TitleWindowActions {
            id: titleControls
            hostRoot: root
            transferPopup: transferCenter
            commandPopup: commandPalette
        }
    }

    Rectangle {
        id: recoveryBanner

        objectName: "startupRecoveryBanner"
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.controller.startupRecoveryNotice.length > 0 ? 42 : 0
        visible: height > 0
        clip: true
        color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, Theme.dark ? 0.14 : 0.1)
        border.color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.38)
        z: 3

        Behavior on height {
            MotionRelocate {}
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 8
            spacing: 10

            AppIcon {
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                name: "warning"
                color: Theme.warning
            }

            Text {
                Layout.fillWidth: true
                text: root.controller.startupRecoveryNotice
                color: Theme.text
                elide: Text.ElideRight
                font.family: Theme.uiFont
                font.pixelSize: Theme.textLabel
                Accessible.role: Accessible.AlertMessage
                Accessible.name: text
            }

            ToolButton {
                id: dismissRecoveryButton

                objectName: "dismissStartupRecoveryButton"
                Layout.preferredWidth: 30
                Layout.preferredHeight: 30
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.name: qsTr("Dismiss recovery notice")
                onClicked: root.controller.dismissStartupRecoveryNotice()

                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: dismissRecoveryButton.down ? Theme.controlPressed : dismissRecoveryButton.hovered ? Theme.controlHover : "transparent"
                    border.color: dismissRecoveryButton.visualFocus ? Theme.focus : "transparent"
                    border.width: dismissRecoveryButton.visualFocus ? 2 : 0
                }

                contentItem: AppIcon {
                    name: "close"
                    color: Theme.textSoft
                }

                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }
            }
        }
    }

    CommandPalette {
        id: commandPalette

        anchors.fill: parent
        controller: root.controller
    }

    TransferCenter {
        id: transferCenter

        x: Math.max(8, root.width - root.captionButtonWidth * 3 - width - 8)
        y: root.titleBarHeight + 6
        controller: root.controller
    }

    TransferToast {
        id: transferToast

        x: root.width - width - 16
        y: root.titleBarHeight + 14
        z: 100
    }

    ActionToast {
        id: terminalActionToast

        x: root.width - width - 16
        y: root.titleBarHeight + 14 + (transferToast.opened ? transferToast.height + 8 : 0)
        z: 100
        onActionTriggered: payload => root.controller.undoTerminalKeywordHighlight(payload.tabId || "", payload.ruleId || "")
    }

    FileDialog {
        id: sessionLogDialog

        title: qsTr("Save session log")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Terminal logs (*.log)"), qsTr("All files (*)")]
        defaultSuffix: "log"
        onAccepted: root.controller.startTerminalLog(selectedFile.toString())
    }

    FileDialog {
        id: scriptImportDialog

        title: qsTr("Import command snippet library")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("ztermy script libraries (*.json)"), qsTr("All files (*)")]
        onAccepted: root.controller.importQuickCommands(selectedFile.toString())
    }

    FileDialog {
        id: scriptExportDialog

        title: qsTr("Export command snippet library")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("ztermy script libraries (*.json)"), qsTr("All files (*)")]
        defaultSuffix: "json"
        onAccepted: root.controller.exportQuickCommands(selectedFile.toString())
    }

    FileDialog {
        id: workspaceImportDialog

        title: qsTr("Import workspace")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("ztermy workspaces (*.ztermy-workspace.json)"), qsTr("JSON files (*.json)"), qsTr("All files (*)")]
        onAccepted: {
            const succeeded = root.controller.importWorkspace(selectedFile.toString());
            terminalActionToast.present({
                "title": succeeded ? qsTr("Workspace imported") : qsTr("Import failed"),
                "message": root.controller.workspaceOperationMessage
            });
        }
    }

    FileDialog {
        id: workspaceExportDialog

        title: qsTr("Export workspace")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("ztermy workspaces (*.ztermy-workspace.json)"), qsTr("JSON files (*.json)"), qsTr("All files (*)")]
        defaultSuffix: "ztermy-workspace.json"
        onAccepted: {
            const succeeded = root.controller.exportWorkspace(selectedFile.toString());
            terminalActionToast.present({
                "title": succeeded ? qsTr("Workspace exported") : qsTr("Export failed"),
                "message": root.controller.workspaceOperationMessage
            });
        }
    }

    FileDialog {
        id: openSshConfigImportDialog

        title: qsTr("Import OpenSSH configuration")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("OpenSSH configuration (config)"), qsTr("All files (*)")]
        onAccepted: root.controller.importOpenSshConfig(selectedFile.toString())
    }

    Dialog {
        id: renameTerminalDialog

        anchors.centerIn: parent
        width: Math.min(420, Math.max(0, root.width - 48))
        modal: true
        dim: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        padding: 20
        onOpened: {
            renameTerminalTitleField.forceActiveFocus(Qt.PopupFocusReason);
            renameTerminalTitleField.selectAll();
        }
        onClosed: root.renameTerminalTabId = ""

        Overlay.modal: Rectangle {
            color: Theme.modalScrim
        }

        background: AppSurface {
            elevation: 3
        }

        contentItem: ColumnLayout {
            spacing: 14

            Text {
                Layout.fillWidth: true
                text: qsTr("Rename terminal tab")
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }

            AppTextField {
                id: renameTerminalTitleField

                objectName: "renameTerminalTitleField"
                Layout.fillWidth: true
                accessibleName: qsTr("Terminal tab title")
                maximumLength: 256
                onAccepted: renameTerminalAccept.clicked()
            }

            RowLayout {
                Layout.fillWidth: true

                Item {
                    Layout.fillWidth: true
                }

                ActionButton {
                    text: qsTr("Cancel")
                    accessibleName: text
                    onClicked: renameTerminalDialog.close()
                }

                ActionButton {
                    id: renameTerminalAccept

                    text: qsTr("Rename")
                    accessibleName: text
                    variant: "primary"
                    enabled: renameTerminalTitleField.text.trim().length > 0
                    onClicked: {
                        if (root.controller.setTerminalTabTitle(root.renameTerminalTabId, renameTerminalTitleField.text)) {
                            renameTerminalDialog.close();
                        }
                    }
                }
            }
        }
    }

    RowLayout {
        anchors.top: recoveryBanner.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: 0

        Rectangle {
            id: workspaceNavigation

            objectName: "workspaceNavigation"
            Layout.fillHeight: true
            Layout.preferredWidth: root.currentPage === "hosts" ? root.workspaceNavigationWidth : 0
            Layout.minimumWidth: Layout.preferredWidth
            Layout.maximumWidth: Layout.preferredWidth
            visible: Layout.preferredWidth > 0
            opacity: root.currentPage === "hosts" ? 1.0 : 0.0
            clip: true
            color: root.panelColor

            Behavior on opacity {
                MotionFeedback {}
            }

            Behavior on Layout.preferredWidth {
                enabled: !workspaceNavigationResizeHandle.pressed

                MotionRelocate {}
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4

                Rectangle {
                    id: workspaceNavigationHeader

                    objectName: "workspaceNavigationHeader"
                    Layout.fillWidth: true
                    Layout.bottomMargin: 8
                    Layout.preferredHeight: 38
                    radius: Theme.radiusControl
                    color: workspaceNavigationHeaderAction.hovered ? Theme.raisedBackground : "transparent"
                    border.color: workspaceNavigationHeaderAction.visualFocus ? Theme.focus : "transparent"
                    border.width: workspaceNavigationHeaderAction.visualFocus ? 1 : 0

                    Row {
                        anchors.centerIn: parent
                        spacing: 9

                        BrandIcon {
                            width: 30
                            height: 30
                            tileColor: Theme.accent
                            ribbonColor: Theme.accentText
                        }

                        Text {
                            visible: !root.workspaceNavigationCompact
                            text: qsTr("ztermy")
                            color: Theme.text
                            font.family: Theme.uiFont
                            font.pixelSize: 17
                            font.weight: Font.Bold
                        }
                    }

                    KeyboardAction {
                        id: workspaceNavigationHeaderAction

                        objectName: "workspaceNavigationToggle"
                        anchors.fill: parent
                        anchors.margins: 2
                        accessibleName: root.workspaceNavigationCompact ? qsTr("Expand sidebar") : qsTr("Collapse sidebar")
                        onActivated: root.toggleWorkspaceNavigation()
                    }

                    AppToolTip {
                        visible: workspaceNavigationHeaderAction.hovered
                        text: workspaceNavigationHeaderAction.accessibleName
                    }
                }

                SideNavigationItem {
                    actionObjectName: "sideHostsAction"
                    Layout.fillWidth: true
                    iconName: "hosts"
                    text: qsTr("Hosts")
                    compact: root.workspaceNavigationCompact
                    selected: root.workspaceSection === "hosts"
                    onActivated: root.workspaceSection = "hosts"
                }

                SideNavigationItem {
                    actionObjectName: "sideCredentialsAction"
                    Layout.fillWidth: true
                    iconName: "security"
                    text: qsTr("Keychain")
                    compact: root.workspaceNavigationCompact
                    selected: root.workspaceSection === "credentials"
                    onActivated: root.workspaceSection = "credentials"
                }

                SideNavigationItem {
                    actionObjectName: "sideProxiesAction"
                    Layout.fillWidth: true
                    iconName: "network"
                    text: qsTr("Proxies")
                    compact: root.workspaceNavigationCompact
                    selected: root.workspaceSection === "proxies"
                    onActivated: root.workspaceSection = "proxies"
                }

                SideNavigationItem {
                    actionObjectName: "sidePortForwardingAction"
                    Layout.fillWidth: true
                    iconName: "transfer"
                    text: qsTr("Port forwarding")
                    compact: root.workspaceNavigationCompact
                    selected: root.workspaceSection === "forwarding"
                    onActivated: root.workspaceSection = "forwarding"
                }

                SideNavigationItem {
                    actionObjectName: "sideScriptsAction"
                    Layout.fillWidth: true
                    iconName: "commands"
                    text: qsTr("Scripts")
                    compact: root.workspaceNavigationCompact
                    selected: root.workspaceSection === "scripts"
                    onActivated: root.workspaceSection = "scripts"
                }

                SideNavigationItem {
                    actionObjectName: "sideKnownHostsAction"
                    Layout.fillWidth: true
                    iconName: "bookmark"
                    text: qsTr("Known hosts")
                    compact: root.workspaceNavigationCompact
                    selected: root.workspaceSection === "known-hosts"
                    onActivated: root.workspaceSection = "known-hosts"
                }

                SideNavigationItem {
                    actionObjectName: "sideLogsAction"
                    Layout.fillWidth: true
                    iconName: "history"
                    text: qsTr("Logs")
                    compact: root.workspaceNavigationCompact
                    selected: root.workspaceSection === "logs"
                    onActivated: root.workspaceSection = "logs"
                }

                SideNavigationItem {
                    actionObjectName: "sideImportWorkspaceAction"
                    Layout.fillWidth: true
                    iconName: "upload"
                    text: qsTr("Import workspace")
                    compact: root.workspaceNavigationCompact
                    onActivated: workspaceImportDialog.open()
                }

                SideNavigationItem {
                    actionObjectName: "sideExportWorkspaceAction"
                    Layout.fillWidth: true
                    iconName: "download"
                    text: qsTr("Export workspace")
                    compact: root.workspaceNavigationCompact
                    onActivated: workspaceExportDialog.open()
                }

                Item {
                    Layout.fillHeight: true
                }

                SideNavigationItem {
                    actionObjectName: "sideSettingsAction"
                    Layout.fillWidth: true
                    iconName: "settings"
                    text: qsTr("Settings")
                    compact: root.workspaceNavigationCompact
                    onActivated: root.openSettingsTab()
                }
            }

            ResizeGrip {
                id: workspaceNavigationResizeHandle

                objectName: "workspaceNavigationResizeHandle"
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                width: 6
                z: 5
                value: root.workspaceNavigationWidth
                minimum: root.workspaceNavigationMinimumWidth
                maximum: root.workspaceNavigationMaximumWidth
                defaultValue: 208
                snapPoints: [56, 208]
                onValueEdited: value => root.resizeWorkspaceNavigation(value)
                Rectangle {
                    anchors.fill: parent
                    color: workspaceNavigationResizeHandle.containsMouse || workspaceNavigationResizeHandle.pressed ? Theme.withAlpha(Theme.accent, 0.24) : "transparent"
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                id: terminalPanel
                anchors.fill: parent
                color: "transparent"
                visible: root.currentPage === "terminal"
                opacity: root.pageReveal

                transform: Translate {
                    x: Motion.distance * (1.0 - root.pageReveal)
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 26
                        color: Theme.workspaceBackground

                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 1
                            color: root.borderColor
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            spacing: 8

                            Rectangle {
                                Layout.alignment: Qt.AlignVCenter
                                Layout.preferredWidth: 6
                                Layout.preferredHeight: 6
                                radius: height / 2
                                color: root.activeTerminalTab && root.activeTerminalTab.running ? Theme.accent : Theme.workspaceTextSubtle
                            }

                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                Layout.maximumWidth: 250
                                text: root.activeTerminalTab ? root.activeTerminalTab.identity : qsTr("Terminal")
                                color: Theme.workspaceText
                                elide: Text.ElideRight
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textCompact
                                font.weight: Font.DemiBold
                            }

                            AppIconButton {
                                id: copyAddressButton
                                onWorkspace: true

                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 22
                                visible: root.activeTerminalTab !== null && root.activeTerminalTab.address.length > 0
                                onClicked: root.controller.copyActiveTerminalAddress()
                                label: qsTr("Copy host address")
                                iconName: "copy"
                                iconColor: Theme.workspaceTextMuted

                                toolTipText: qsTr("Copy host address")
                            }

                            Rectangle {
                                Layout.alignment: Qt.AlignVCenter
                                Layout.preferredWidth: 1
                                Layout.preferredHeight: 12
                                color: Theme.workspaceBorder
                                visible: terminalSessionStatus.visible || remoteTelemetryStrip.visible
                            }

                            RemoteTelemetryStrip {
                                id: remoteTelemetryStrip

                                Layout.alignment: Qt.AlignVCenter
                                Layout.preferredWidth: implicitWidth
                                Layout.maximumWidth: Math.max(0, root.width - 690)
                                visible: root.width >= 760 && root.activeTerminalTab !== null && root.activeTerminalTab.kind === "ssh" && root.activeTerminalTab.connected
                                controller: root.controller
                                availableWidth: root.width
                            }

                            Text {
                                id: terminalSessionStatus

                                Layout.alignment: Qt.AlignVCenter
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                visible: root.width >= 1080 || (root.activeTerminalTab !== null && root.activeTerminalTab.kind !== "ssh")
                                text: terminalViewport.statusText
                                color: Theme.workspaceTextMuted
                                elide: Text.ElideRight
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textCompact
                            }

                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                visible: root.width >= 900 && root.activeTerminalTab !== null && root.activeTerminalTab.connectedUtcMs > 0
                                text: root.activeTerminalTab !== null ? qsTr("Connected %1").arg(root.formatSessionDuration(root.activeTerminalTab.connectedUtcMs)) : ""
                                color: Theme.workspaceTextMuted
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textCompact
                                Accessible.name: text
                            }

                            Item {
                                Layout.fillWidth: !terminalSessionStatus.visible
                                visible: !terminalSessionStatus.visible
                            }

                            SessionStripAction {
                                id: keywordHighlightButton

                                objectName: "terminalKeywordHighlightAction"
                                checkable: true
                                checked: root.activeTerminalTab !== null && root.activeTerminalTab.keywordHighlightEnabled && root.activeTerminalTab.keywordHighlightRules.length > 0
                                selected: checked
                                visible: root.width >= 980 && root.activeTerminalTab !== null && root.activeTerminalTab.kind === "ssh"
                                enabled: root.activeTerminalTab !== null
                                onClicked: {
                                    if (keywordHighlightPopover.visible)
                                        keywordHighlightPopover.close();
                                    else
                                        keywordHighlightPopover.openFor(keywordHighlightButton);
                                }
                                label: qsTr("Host keyword highlighting")
                                iconName: "highlight"
                                iconColor: keywordHighlightButton.checked ? Theme.accent : Theme.workspaceTextMuted
                                toolTipText: qsTr("Host keyword highlighting")
                                toolTipEnabled: !keywordHighlightPopover.visible
                            }

                            SessionStripAction {
                                id: sftpToolbarButton

                                objectName: "terminalSftpAction"
                                checkable: true
                                checked: root.activeTerminalTab !== null && root.activeTerminalTab.workbenchOpen && root.activeTerminalTab.workbenchPage === "sftp"
                                selected: checked
                                visible: root.width >= 900
                                enabled: root.activeTerminalTab !== null && root.activeTerminalTab.connected
                                onClicked: root.controller.toggleTerminalWorkbench("sftp")
                                label: qsTr("Open remote files")
                                iconName: "folder"
                                iconColor: sftpToolbarButton.checked ? Theme.accent : Theme.workspaceTextMuted
                                toolTipText: qsTr("Open remote files")
                            }

                            SessionStripAction {
                                id: composerToolbarButton

                                objectName: "terminalComposerAction"
                                checkable: true
                                checked: root.activeTerminalTab !== null && root.activeTerminalTab.composerOpen
                                selected: checked
                                visible: root.width >= 820
                                enabled: root.activeTerminalTab !== null
                                onClicked: {
                                    const opening = root.activeTerminalTab !== null && !root.activeTerminalTab.composerOpen;
                                    root.controller.toggleTerminalComposer();
                                    if (opening) {
                                        terminalComposer.focusEditor();
                                    } else {
                                        terminalViewport.forceActiveFocus();
                                    }
                                }
                                label: qsTr("Command composer")
                                iconName: "compose"
                                iconColor: composerToolbarButton.checked ? Theme.accent : Theme.workspaceTextMuted

                                toolTipText: qsTr("Command composer")
                            }

                            SessionStripAction {
                                id: terminalFindButton

                                objectName: "terminalFindAction"
                                visible: root.width >= 700
                                enabled: root.activeTerminalTab !== null
                                onClicked: root.toggleTerminalSearch()
                                label: qsTr("Find in terminal")
                                iconName: "search"
                                iconColor: Theme.workspaceTextMuted
                                toolTipText: qsTr("Find in terminal")
                            }

                            SessionStripAction {
                                id: sessionLogToolbarButton

                                checkable: true
                                checked: root.activeTerminalTab !== null && (root.activeTerminalTab.logState === "active" || root.activeTerminalTab.logState === "starting")
                                selected: checked
                                visible: root.width >= 860
                                enabled: root.activeTerminalTab !== null
                                onClicked: root.toggleSessionLog()
                                label: checked ? qsTr("Stop session log") : qsTr("Start session log")
                                iconName: "save"
                                iconColor: root.activeTerminalTab !== null && root.activeTerminalTab.logDroppedBytes > 0 ? Theme.warning : sessionLogToolbarButton.checked ? Theme.accent : Theme.workspaceTextMuted
                                toolTipText: root.activeTerminalTab !== null && root.activeTerminalTab.logDroppedBytes > 0 ? qsTr("Session log is incomplete: %1 byte(s) were dropped.").arg(root.activeTerminalTab.logDroppedBytes) : sessionLogToolbarButton.checked ? qsTr("Stop session log") : qsTr("Start session log")
                            }

                            SessionStripAction {
                                id: scriptsToolbarButton

                                objectName: "terminalScriptsAction"
                                checkable: true
                                checked: root.activeTerminalTab !== null && root.activeTerminalTab.workbenchOpen && root.activeTerminalTab.workbenchPage === "scripts"
                                selected: checked
                                visible: root.width >= 940
                                enabled: root.activeTerminalTab !== null
                                onClicked: root.controller.toggleTerminalWorkbench("scripts")
                                label: qsTr("Command snippets")
                                iconName: "commands"
                                iconColor: scriptsToolbarButton.checked ? Theme.accent : Theme.workspaceTextMuted
                                toolTipText: qsTr("Command snippets")
                            }

                            SessionStripAction {
                                id: scriptRecordingIndicator
                                iconName: "commands"
                                label: toolTipText

                                objectName: "terminalScriptRecordingIndicator"
                                Layout.preferredWidth: root.activeTerminalTab !== null && root.activeTerminalTab.scriptRecordingState === "review" ? 42 : 50
                                visible: root.activeTerminalTab !== null && root.activeTerminalTab.scriptRecordingState !== "idle"
                                selected: root.activeTerminalTab !== null && root.activeTerminalTab.scriptRecordingState !== "review"
                                onClicked: {
                                    if (root.activeTerminalTab.scriptRecordingState === "recording")
                                        root.controller.pauseTerminalScriptRecording();
                                    else if (root.activeTerminalTab.scriptRecordingState === "paused")
                                        root.controller.resumeTerminalScriptRecording();
                                    else
                                        terminalRecordingPopover.openFor(scriptRecordingIndicator);
                                }
                                contentItem: Row {
                                    anchors.centerIn: parent
                                    spacing: 5
                                    Rectangle {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 7
                                        height: 7
                                        radius: height / 2
                                        color: Theme.danger
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: root.activeTerminalTab !== null && root.activeTerminalTab.scriptRecordingState === "review" ? root.activeTerminalTab.scriptRecordingSteps.length : "REC"
                                        color: Theme.workspaceText
                                        font.family: Theme.uiFont
                                        font.pixelSize: Theme.textCompact
                                        font.weight: Font.DemiBold
                                    }
                                }
                                toolTipText: root.activeTerminalTab !== null && root.activeTerminalTab.scriptRecordingState === "recording" ? qsTr("Pause script recording") : root.activeTerminalTab !== null && root.activeTerminalTab.scriptRecordingState === "paused" ? qsTr("Resume script recording") : qsTr("Review recorded commands")
                                toolTipEnabled: !terminalRecordingPopover.visible
                            }

                            AppIconButton {
                                id: aiToolbarButton
                                onWorkspace: true

                                objectName: "terminalAiAction"
                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 22
                                checkable: true
                                checked: root.activeTerminalTab !== null && root.activeTerminalTab.workbenchOpen && root.activeTerminalTab.workbenchPage === "ai"
                                selected: checked
                                enabled: root.activeTerminalTab !== null
                                onClicked: root.controller.toggleTerminalWorkbench("ai")
                                label: qsTr("AI assistant")
                                iconName: "ai"
                                iconColor: aiToolbarButton.checked ? Theme.accent : Theme.workspaceTextMuted

                                toolTipText: qsTr("AI assistant")
                            }

                            AppIconButton {
                                id: terminalMoreButton
                                onWorkspace: true
                                objectName: "terminalMoreAction"

                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 22
                                enabled: root.activeTerminalTab !== null
                                onClicked: terminalMoreMenu.open()
                                label: qsTr("More terminal actions")
                                iconName: "more"
                                iconColor: Theme.workspaceTextMuted

                                toolTipText: qsTr("More terminal actions")
                                toolTipEnabled: !terminalMoreMenu.visible

                                AppMenu {
                                    id: terminalMoreMenu

                                    objectName: "terminalMoreMenu"

                                    y: terminalMoreButton.height

                                    AppMenuItem {
                                        objectName: "terminalHistoryMenuAction"
                                        text: qsTr("Command history")
                                        onTriggered: root.controller.toggleTerminalWorkbench("history")
                                    }

                                    AppMenuItem {
                                        objectName: "terminalKeywordMenuAction"
                                        text: qsTr("Host keyword highlighting")
                                        visible: !keywordHighlightButton.visible && root.activeTerminalTab !== null && root.activeTerminalTab.kind === "ssh"
                                        onTriggered: keywordHighlightPopover.openFor(terminalMoreButton)
                                    }

                                    AppMenuItem {
                                        text: qsTr("Open remote files")
                                        visible: !sftpToolbarButton.visible
                                        enabled: root.activeTerminalTab !== null && root.activeTerminalTab.connected
                                        onTriggered: root.controller.toggleTerminalWorkbench("sftp")
                                    }

                                    AppMenuItem {
                                        text: qsTr("Command composer")
                                        visible: !composerToolbarButton.visible
                                        onTriggered: {
                                            const opening = root.activeTerminalTab !== null && !root.activeTerminalTab.composerOpen;
                                            root.controller.toggleTerminalComposer();
                                            if (opening)
                                                terminalComposer.focusEditor();
                                        }
                                    }

                                    AppMenuItem {
                                        text: qsTr("Find in terminal")
                                        visible: !terminalFindButton.visible
                                        onTriggered: root.toggleTerminalSearch()
                                    }

                                    AppMenuItem {
                                        text: sessionLogToolbarButton.checked ? qsTr("Stop session log") : qsTr("Start session log")
                                        visible: !sessionLogToolbarButton.visible
                                        onTriggered: root.toggleSessionLog()
                                    }

                                    AppMenuItem {
                                        text: qsTr("Command snippets")
                                        visible: !scriptsToolbarButton.visible
                                        onTriggered: root.controller.toggleTerminalWorkbench("scripts")
                                    }

                                    AppMenuItem {
                                        objectName: "terminalFollowDirectoryMenuAction"
                                        text: root.controller.activeSftpFollowTerminalDirectory ? qsTr("Stop following terminal directory") : qsTr("Follow terminal directory")
                                        enabled: root.activeTerminalTab !== null && root.activeTerminalTab.connected
                                        onTriggered: root.controller.setSftpFollowTerminalDirectory(!root.controller.activeSftpFollowTerminalDirectory)
                                    }

                                    AppMenuItem {
                                        text: qsTr("Session terminal settings")
                                        onTriggered: terminalSessionSettingsPopover.openFor(terminalMoreButton)
                                    }

                                    AppMenuSeparator {}

                                    AppMenuItem {
                                        text: qsTr("Start script recording")
                                        visible: root.activeTerminalTab !== null && (root.activeTerminalTab.scriptRecordingState === "idle" || root.activeTerminalTab.scriptRecordingState === "review")
                                        enabled: root.activeTerminalTab !== null && root.activeTerminalTab.running
                                        onTriggered: root.controller.startTerminalScriptRecording()
                                    }
                                    AppMenuItem {
                                        objectName: "terminalPauseRecordingMenuAction"
                                        text: root.activeTerminalTab !== null && root.activeTerminalTab.scriptRecordingState === "paused" ? qsTr("Resume script recording") : qsTr("Pause script recording")
                                        visible: root.activeTerminalTab !== null && (root.activeTerminalTab.scriptRecordingState === "recording" || root.activeTerminalTab.scriptRecordingState === "paused")
                                        onTriggered: {
                                            if (root.activeTerminalTab.scriptRecordingState === "paused")
                                                root.controller.resumeTerminalScriptRecording();
                                            else
                                                root.controller.pauseTerminalScriptRecording();
                                        }
                                    }
                                    AppMenuItem {
                                        text: qsTr("Stop script recording")
                                        visible: root.activeTerminalTab !== null && (root.activeTerminalTab.scriptRecordingState === "recording" || root.activeTerminalTab.scriptRecordingState === "paused")
                                        onTriggered: {
                                            if (root.controller.stopTerminalScriptRecording())
                                                terminalRecordingPopover.openFor(terminalMoreButton);
                                        }
                                    }
                                    AppMenuItem {
                                        objectName: "terminalReviewRecordingMenuAction"
                                        text: qsTr("Review recorded commands")
                                        visible: root.activeTerminalTab !== null && root.activeTerminalTab.scriptRecordingState === "review"
                                        onTriggered: terminalRecordingPopover.openFor(terminalMoreButton)
                                    }

                                    AppMenuSeparator {}

                                    AppMenu {
                                        title: qsTr("Terminal encoding")

                                        AppMenuItem {
                                            text: qsTr("UTF-8")
                                            checkable: true
                                            checked: root.activeTerminalTab !== null && root.activeTerminalTab.terminalEncoding === "utf-8"
                                            onTriggered: root.controller.setActiveTerminalEncoding("utf-8")
                                        }
                                        AppMenuItem {
                                            text: qsTr("GB18030")
                                            checkable: true
                                            checked: root.activeTerminalTab !== null && root.activeTerminalTab.terminalEncoding === "gb18030"
                                            onTriggered: root.controller.setActiveTerminalEncoding("gb18030")
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        id: terminalBody

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "transparent"

                        TerminalSplitNode {
                            id: terminalViewport
                            objectName: "terminalWorkspaceViewport"
                            anchors.fill: parent
                            anchors.leftMargin: root.activeTerminalWorkbenchSide === "left" ? root.activeTerminalWorkbenchWidth : 0
                            anchors.rightMargin: root.activeTerminalWorkbenchSide === "right" ? root.activeTerminalWorkbenchWidth : 0
                            anchors.bottomMargin: root.activeTerminalComposerHeight
                            visible: root.activeTerminalTab !== null && !!root.visibleTerminalLayoutRoot.kind
                            controller: root.controller
                            node: root.visibleTerminalLayoutRoot
                            zoomedPaneId: root.zoomedTerminalPaneId
                            paneCount: root.mainWorkspace.paneCount || 1
                            headersVisible: root.paneHeadersVisible
                            onToggleHeadersRequested: root.toggleTerminalPaneHeaders()
                            defaultFontFamily: root.controller.terminalFontFamily
                            defaultFontSize: root.controller.terminalFontSize
                            defaultLigatures: root.controller.terminalLigatures
                            defaultBackgroundOpacity: root.controller.terminalBackgroundOpacity
                            defaultCursor: root.controller.cursorPreference
                            cursorBlink: root.controller.cursorBlink
                            copyOnSelect: root.controller.copyOnSelect
                            keepSelectionAfterCopy: root.controller.keepSelectionAfterCopy
                            selectionActionPopupEnabled: root.controller.terminalSelectionPopupEnabled
                            selectionActions: root.controller.terminalSelectionActions
                            confirmMultilinePaste: root.controller.confirmMultilinePaste
                            rightClickBehavior: root.controller.terminalRightClickBehavior
                            middleClickBehavior: root.controller.terminalMiddleClickBehavior
                            wordDelimiters: root.controller.terminalWordDelimiters
                            scrollRowsPerWheel: root.controller.terminalScrollRows

                            Component.onCompleted: forceActiveFocus()
                            onMultilinePasteConfirmationRequested: (viewport, lineCount) => {
                                root.pendingPasteLineCount = lineCount;
                                root.pendingPasteViewport = viewport;
                                // A context menu restores focus as it closes. Defer the modal so it
                                // becomes the final focus owner regardless of how paste was invoked.
                                Qt.callLater(() => multilinePasteDialog.openFrom(viewport));
                            }
                            onZoomPaneRequested: paneId => root.toggleTerminalPaneZoom(paneId)
                            onDetachPaneRequested: paneId => root.detachTerminalPane(paneId)
                            onBrowseHostsRequested: root.currentPage = "hosts"
                            onTerminalSearchRequested: root.openTerminalSearch()

                            Behavior on anchors.rightMargin {
                                MotionRelocate {
                                    duration: root.workbenchResizeInProgress ? 0 : Motion.relocate
                                }
                            }

                            Behavior on anchors.leftMargin {
                                MotionRelocate {
                                    duration: root.workbenchResizeInProgress ? 0 : Motion.relocate
                                }
                            }

                            Behavior on anchors.bottomMargin {
                                MotionRelocate {}
                            }
                        }

                        TerminalKeywordPopover {
                            id: keywordHighlightPopover
                            objectName: "keywordHighlightPopover"
                            controller: root.controller
                            terminalTab: root.activeTerminalTab
                        }

                        TerminalSessionSettingsPopover {
                            id: terminalSessionSettingsPopover
                            controller: root.controller
                            terminalTab: root.activeTerminalTab
                        }

                        TerminalRecordingPopover {
                            id: terminalRecordingPopover
                            controller: root.controller
                            terminalTab: root.activeTerminalTab
                        }

                        AppSurface {
                            id: searchPanel

                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: 12
                            anchors.rightMargin: (root.activeTerminalWorkbenchSide === "right" ? root.activeTerminalWorkbenchWidth : 0) + 12
                            width: 420
                            height: 42
                            elevation: 2
                            compact: true
                            visible: root.terminalSearchVisible
                            z: 10

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 6
                                spacing: 4

                                AppTextField {
                                    id: searchField

                                    objectName: "terminalSearchQuery"
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 30
                                    compact: true
                                    placeholderText: qsTr("Find in terminal")
                                    accessibleName: qsTr("Terminal search query")

                                    onTextEdited: searchDelay.restart()
                                    Keys.onPressed: event => {
                                        if (event.key === Qt.Key_Escape) {
                                            root.closeTerminalSearch();
                                            event.accepted = true;
                                        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                            searchDelay.stop();
                                            root.controller.searchTerminal(text, (event.modifiers & Qt.ShiftModifier) !== 0, caseSensitiveButton.checked);
                                            event.accepted = true;
                                        }
                                    }
                                }

                                Text {
                                    Layout.preferredWidth: 46
                                    horizontalAlignment: Text.AlignHCenter
                                    text: root.controller.terminalSearchTotal > 0 ? root.controller.terminalSearchCurrent + "/" + root.controller.terminalSearchTotal : "0/0"
                                    color: root.mutedColor
                                    font.family: Theme.terminalFont
                                    font.pixelSize: 10
                                }

                                ToolButton {
                                    id: caseSensitiveButton

                                    Layout.preferredWidth: 30
                                    Layout.preferredHeight: 30
                                    checkable: true
                                    text: "Aa"
                                    checked: root.controller.terminalSearchCaseSensitive
                                    hoverEnabled: true
                                    onClicked: {
                                        searchDelay.stop();
                                        root.controller.searchTerminal(searchField.text, false, checked);
                                    }
                                    Accessible.name: qsTr("Match case")
                                    Accessible.checked: checked

                                    contentItem: Text {
                                        text: "Aa"
                                        color: caseSensitiveButton.checked ? Theme.accentText : root.textColor
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        font.family: Theme.uiFont
                                        font.pixelSize: Theme.textLabel
                                        font.weight: caseSensitiveButton.checked ? Font.Bold : Font.Medium
                                    }

                                    background: Rectangle {
                                        radius: Theme.radiusSmall
                                        color: caseSensitiveButton.checked ? Theme.accent : caseSensitiveButton.down ? Theme.controlPressed : caseSensitiveButton.hovered ? Theme.controlHover : Theme.controlBackground
                                        border.color: caseSensitiveButton.visualFocus ? Theme.focus : caseSensitiveButton.checked ? Theme.accentHover : root.borderColor
                                        border.width: caseSensitiveButton.visualFocus ? 2 : 1
                                    }
                                }

                                ToolButton {
                                    Layout.preferredWidth: 30
                                    Layout.preferredHeight: 30
                                    contentItem: AppIcon {
                                        name: "chevron-up"
                                        color: root.textColor
                                    }
                                    onClicked: root.controller.searchTerminal(searchField.text, true, caseSensitiveButton.checked)
                                    Accessible.name: qsTr("Previous match")
                                }

                                ToolButton {
                                    Layout.preferredWidth: 30
                                    Layout.preferredHeight: 30
                                    contentItem: AppIcon {
                                        name: "chevron-down"
                                        color: root.textColor
                                    }
                                    onClicked: root.controller.searchTerminal(searchField.text, false, caseSensitiveButton.checked)
                                    Accessible.name: qsTr("Next match")
                                }

                                ToolButton {
                                    Layout.preferredWidth: 30
                                    Layout.preferredHeight: 30
                                    contentItem: AppIcon {
                                        name: "close"
                                        color: root.textColor
                                    }
                                    onClicked: root.closeTerminalSearch()
                                    Accessible.name: qsTr("Close terminal search")
                                }
                            }
                        }

                        TerminalComposer {
                            id: terminalComposer

                            objectName: "terminalComposer"
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.leftMargin: root.activeTerminalWorkbenchSide === "left" ? root.activeTerminalWorkbenchWidth : 0
                            anchors.rightMargin: root.activeTerminalWorkbenchSide === "right" ? root.activeTerminalWorkbenchWidth : 0
                            height: root.activeTerminalComposerHeight
                            visible: root.activeTerminalTab !== null && root.activeTerminalTab.composerOpen
                            z: 14
                            controller: root.controller
                            activeTab: root.activeTerminalTab
                            panelHeight: root.activeTerminalTab !== null ? root.activeTerminalTab.composerHeight : 132
                            onHeightRequested: height => root.controller.setTerminalComposerHeight(height)
                            onCloseRequested: {
                                root.controller.toggleTerminalComposer();
                                terminalViewport.forceActiveFocus();
                            }

                            Behavior on height {
                                MotionRelocate {}
                            }
                        }

                        Loader {
                            id: terminalWorkbenchLoader

                            readonly property bool requested: root.activeTerminalTab !== null && root.activeTerminalTab.workbenchOpen
                            property bool retained: false

                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: root.activeTerminalWorkbenchWidth
                            x: root.activeTerminalWorkbenchSide === "left" ? 0 : parent.width - width
                            active: requested || retained
                            visible: requested
                            z: 15
                            onRequestedChanged: {
                                if (requested) {
                                    retained = true;
                                }
                            }

                            sourceComponent: TerminalWorkbench {
                                objectName: "terminalWorkbench"
                                width: terminalWorkbenchLoader.width
                                height: terminalWorkbenchLoader.height
                                controller: root.controller
                                activeTab: root.activeTerminalTab
                                panelSide: root.activeTerminalWorkbenchSide
                                panelWidth: root.activeTerminalWorkbenchWidth
                                onPanelWidthRequested: width => root.liveWorkbenchWidth = width
                                onPanelResizeStarted: root.workbenchResizeInProgress = true
                                onPanelResizeFinished: {
                                    root.workbenchResizeInProgress = false;
                                    if (root.liveWorkbenchWidth >= 0) {
                                        root.controller.setTerminalWorkbenchWidth(root.liveWorkbenchWidth);
                                        root.liveWorkbenchWidth = -1;
                                    }
                                }
                                onInsertRequested: command => {
                                    if (root.controller.insertTerminalCommand(command)) {
                                        terminalViewport.forceActiveFocus();
                                    }
                                }
                                onRunRequested: command => root.requestTerminalCommandRun(command)
                                onImportLibraryRequested: scriptImportDialog.open()
                                onExportLibraryRequested: scriptExportDialog.open()
                                onAiSettingsRequested: root.openAiSettingsTab()
                                onCloseRequested: {
                                    root.controller.closeTerminalWorkbench();
                                    terminalViewport.forceActiveFocus();
                                }
                            }
                        }

                        StatePanel {
                            id: emptyTerminalState

                            objectName: "emptyTerminalState"
                            anchors.centerIn: parent
                            width: Math.min(440, parent.width - 48)
                            visible: root.activeTerminalTab === null
                            z: 9
                            kind: "empty"
                            centered: true
                            heading: qsTr("No terminal sessions")
                            description: qsTr("Open a local PowerShell session or choose an SSH host from the Hosts workspace.")

                            ActionButton {
                                id: emptyTerminalPrimaryAction

                                text: qsTr("New terminal")
                                accessibleName: qsTr("Open a new local terminal")
                                variant: "primary"
                                onClicked: root.startLocalTerminalTab()
                            }

                            ActionButton {
                                text: qsTr("Browse hosts")
                                accessibleName: qsTr("Browse saved SSH hosts")
                                onClicked: root.currentPage = "hosts"
                            }
                        }

                        Timer {
                            id: searchDelay

                            interval: 250
                            repeat: false
                            onTriggered: root.controller.searchTerminal(searchField.text, false, caseSensitiveButton.checked)
                        }
                    }
                }
            }

            HostConnectionPane {
                id: hostConnectionPane

                anchors.fill: parent
                visible: root.currentPage === "hosts" && root.workspaceSection === "hosts"
                opacity: root.pageReveal
                controller: root.controller
                showPortForwarding: false
                backgroundColor: Theme.contentBackground
                raisedColor: root.raisedColor
                borderColor: root.borderColor
                textColor: root.textColor
                mutedColor: root.mutedColor
                accentColor: root.accentColor
                onConnectionStarted: root.currentPage = "terminal"
                onSecuritySettingsRequested: root.openSecuritySettingsTab()
                onLocalTerminalRequested: root.startLocalTerminalTab()
                onOpenSshImportRequested: openSshConfigImportDialog.open()

                transform: Translate {
                    x: -Motion.distance * (1.0 - root.pageReveal)
                }
            }

            WorkspaceCredentialsPane {
                anchors.fill: parent
                visible: root.currentPage === "hosts" && root.workspaceSection === "credentials"
                controller: root.controller
                onEditHostRequested: profileId => root.editWorkspaceHost(profileId)
                onSecuritySettingsRequested: root.openSecuritySettingsTab()
            }

            WorkspaceProxiesPane {
                anchors.fill: parent
                visible: root.currentPage === "hosts" && root.workspaceSection === "proxies"
                controller: root.controller
                onEditHostRequested: profileId => root.editWorkspaceHost(profileId)
            }

            Item {
                id: forwardingWorkspacePane
                anchors.fill: parent
                visible: root.currentPage === "hosts" && root.workspaceSection === "forwarding"

                ScrollView {
                    id: forwardingWorkspaceScroll
                    anchors.fill: parent
                    clip: true
                    contentWidth: availableWidth

                    PortForwardingPane {
                        x: 20
                        y: 20
                        width: Math.max(0, forwardingWorkspaceScroll.availableWidth - 40)
                        controller: root.controller
                        overlayParent: forwardingWorkspacePane
                        compactLayout: forwardingWorkspacePane.width < Theme.narrowWindowWidth
                    }
                }
            }

            WorkspaceScriptsPane {
                anchors.fill: parent
                visible: root.currentPage === "hosts" && root.workspaceSection === "scripts"
                controller: root.controller
            }

            WorkspaceKnownHostsPane {
                anchors.fill: parent
                visible: root.currentPage === "hosts" && root.workspaceSection === "known-hosts"
                controller: root.controller
                onCreateHostRequested: (host, port) => root.createWorkspaceHost(host, port)
            }

            WorkspaceLogsPane {
                anchors.fill: parent
                visible: root.currentPage === "hosts" && root.workspaceSection === "logs"
                controller: root.controller
                activeTab: root.activeTerminalTab
                onOpenTerminalRequested: tabId => {
                    root.controller.activateTerminalTab(tabId);
                    root.currentPage = "terminal";
                }
                onToggleActiveLogRequested: root.toggleSessionLog()
            }

            SftpWorkspacePane {
                anchors.fill: parent
                visible: root.currentPage === "sftp"
                controller: root.controller
                activeTab: root.activeTerminalTab
                onBrowseHostsRequested: root.currentPage = "hosts"
                onTerminalRequested: root.currentPage = "terminal"
            }

            SettingsPane {
                id: settingsPane

                anchors.fill: parent
                visible: root.currentPage === "settings"
                opacity: root.pageReveal
                controller: root.controller
                diagnostics: root.diagnostics
                fontCatalog: root.fontCatalog
                windowChrome: root.windowChrome
                onAppearancePreviewEnded: root.endWindowAppearancePreview()
                onAppearancePreviewRequested: (theme, opacity, backdrop, accent, customAccent, effects) => {
                    root.previewWindowAppearance(theme, opacity, backdrop, accent, customAccent, effects);
                }

                transform: Translate {
                    x: Motion.distance * (1.0 - root.pageReveal)
                }
            }
        }
    }

    Dialog {
        id: portableVaultUnlockDialog

        property Item focusRestoreItem: null

        objectName: "startupPortableVaultUnlockDialog"
        anchors.centerIn: parent
        modal: true
        dim: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        padding: 20
        onAboutToShow: Qt.callLater(portableVaultUnlockPassword.forceActiveFocus)
        onClosed: {
            const restoreItem = focusRestoreItem;
            focusRestoreItem = null;
            portableVaultUnlockPassword.text = "";
            portableVaultUnlockStatus.text = "";
            if (restoreItem && restoreItem.visible && restoreItem.enabled) {
                Qt.callLater(() => restoreItem.forceActiveFocus());
            }
        }

        Overlay.modal: Rectangle {
            color: Theme.modalScrim
        }

        background: AppSurface {
            elevation: 3
        }

        contentItem: ColumnLayout {
            spacing: 14
            Accessible.role: Accessible.Dialog
            Accessible.name: qsTr("Unlock portable credential vault")

            Text {
                text: qsTr("Unlock portable vault")
                color: root.textColor
                font.family: Theme.uiFont
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }

            Text {
                Layout.preferredWidth: 390
                text: qsTr("Unlock saved SSH passwords and private-key passphrases for this ztermy session. The master password is never stored.")
                color: root.mutedColor
                wrapMode: Text.WordWrap
                font.family: Theme.uiFont
                font.pixelSize: Theme.textLabel
            }

            AppTextField {
                id: portableVaultUnlockPassword

                objectName: "startupPortableVaultPassword"
                Layout.fillWidth: true
                placeholderText: qsTr("Master password (minimum 8 characters)")
                passwordRevealable: true
                accessibleName: qsTr("Portable vault master password")
                selectByMouse: true
                onAccepted: portableVaultUnlockAction.clicked()
            }

            StatusMessage {
                id: portableVaultUnlockStatus

                Layout.fillWidth: true
                kind: "error"
            }

            RowLayout {
                Layout.fillWidth: true

                ActionButton {
                    text: qsTr("Open Security")
                    accessibleName: qsTr("Open credential Security settings")
                    onClicked: {
                        portableVaultUnlockDialog.close();
                        root.openSecuritySettingsTab();
                    }
                }

                Item {
                    Layout.fillWidth: true
                }

                ActionButton {
                    text: qsTr("Not now")
                    accessibleName: qsTr("Keep portable vault locked")
                    onClicked: portableVaultUnlockDialog.close()
                }

                ActionButton {
                    id: portableVaultUnlockAction

                    text: qsTr("Unlock")
                    accessibleName: qsTr("Unlock portable credential vault")
                    enabled: portableVaultUnlockPassword.text.length >= 8
                    variant: "primary"
                    onClicked: {
                        if (!root.controller.unlockPortableCredentialVault(portableVaultUnlockPassword.text)) {
                            portableVaultUnlockStatus.text = root.controller.credentialOperationError;
                            portableVaultUnlockPassword.selectAll();
                            return;
                        }
                        portableVaultUnlockDialog.close();
                    }
                }
            }
        }
    }

    ConfirmationDialog {
        id: multilinePasteDialog

        heading: qsTr("Paste multiple lines?")
        description: qsTr("The clipboard contains %n line(s). Pasting may execute commands immediately in the active terminal.", "", root.pendingPasteLineCount)
        acceptText: qsTr("Paste")
        acceptObjectName: "multilinePasteAccept"
        rejectObjectName: "multilinePasteReject"
        onAccepted: {
            if (root.pendingPasteViewport) {
                root.pendingPasteViewport.resolveMultilinePaste(true);
            }
            root.pendingPasteLineCount = 0;
            root.pendingPasteViewport = null;
        }
        onRejected: {
            if (root.pendingPasteViewport) {
                root.pendingPasteViewport.resolveMultilinePaste(false);
            }
            root.pendingPasteLineCount = 0;
            root.pendingPasteViewport = null;
        }
    }

    TerminalWindowCoordinator {
        id: terminalWindows
        hostRoot: root
        titleTabs: titleTerminalTabs
        newTabButton: titleNewTabContainer
        terminalArea: terminalViewport
    }

    HoverHandler {
        id: paneHeaderHover
        blocking: false
        property bool overHeader: false
        onPointChanged: {
            if (paneDragCapture.pressed)
                return;
            const global = root.mapToGlobal(point.position.x, point.position.y);
            const header = root.currentPage === "terminal" && root.paneHeadersVisible ? terminalWindows.viewportAt(terminalViewport, global, "terminalPaneHeader-") : null;
            overHeader = !!header && header.mapFromGlobal(global.x, global.y).x < header.dragAreaWidth;
        }
        onHoveredChanged: {
            if (!hovered)
                overHeader = false;
        }
    }

    MouseArea {
        id: paneDragCapture
        anchors.fill: parent
        z: 80
        enabled: pressed || (paneHeaderHover.overHeader && root.currentPage === "terminal" && root.paneHeadersVisible)
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        property point pressPoint: Qt.point(0, 0)
        property point pointerPoint: Qt.point(0, 0)
        property string paneId: ""
        property string paneTitle: ""
        property bool dragging: false
        onPressed: mouse => {
            const global = mapToGlobal(mouse.x, mouse.y);
            const header = root.currentPage === "terminal" ? terminalWindows.viewportAt(terminalViewport, global, "terminalPaneHeader-") : null;
            if (!header || header.mapFromGlobal(global.x, global.y).x >= header.dragAreaWidth) {
                mouse.accepted = false;
                return;
            }
            paneId = header.paneId;
            paneTitle = header.paneTitle;
            pressPoint = Qt.point(mouse.x, mouse.y);
            pointerPoint = pressPoint;
            dragging = false;
            terminalWindows.draggedPaneId = paneId;
        }
        onPositionChanged: mouse => {
            if (!pressed || !paneId.length)
                return;
            pointerPoint = Qt.point(mouse.x, mouse.y);
            if (!dragging && Math.hypot(mouse.x - pressPoint.x, mouse.y - pressPoint.y) >= 10)
                dragging = true;
            if (dragging)
                terminalWindows.updateDropTarget(mapToGlobal(mouse.x, mouse.y));
        }
        onReleased: mouse => {
            if (!paneId.length)
                return;
            const id = paneId;
            paneId = "";
            if (dragging) {
                terminalWindows.finishPaneDrop(id, mouse.x < 0 || mouse.y < 0 || mouse.x > width || mouse.y > height);
            } else {
                terminalWindows.draggedPaneId = "";
                if (root.controller.activateTerminalPane(id))
                    root.focusTerminalAfterLayout();
            }
            dragging = false;
        }
        function cancelDrag() {
            paneId = "";
            dragging = false;
            terminalWindows.draggedPaneId = "";
            terminalWindows.dropTarget = ({});
        }
        onCanceled: cancelDrag()
    }

    Shortcut {
        sequence: "Escape"
        enabled: paneDragCapture.dragging
        onActivated: paneDragCapture.cancelDrag()
    }

    DragPreview {
        z: 91
        visible: paneDragCapture.dragging
        x: paneDragCapture.pointerPoint.x + 16
        y: paneDragCapture.pointerPoint.y + 18
        title: paneDragCapture.paneTitle
    }

    DropTargetIndicator {
        z: 90
        target: terminalWindows.dropTarget
    }

    HostKeyPrompt {
        anchors.fill: parent
        z: 100
        controller: root.controller
        focusRestoreItem: terminalViewport
        panelColor: root.raisedColor
        borderColor: root.borderColor
        textColor: root.textColor
        mutedColor: Theme.textSoft
        accentColor: root.accentColor
    }
}
