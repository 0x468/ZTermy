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
    readonly property int titleQuickActionsWidth: titleQuickActionWidth * 3
    readonly property int titleSecurityActionWidth: portableVaultNeedsAttention ? 40 : 0
    readonly property int titleNavigationWidth: Math.min(830, Math.max(310, width - (captionButtonWidth * 3) - titleQuickActionsWidth - titleSecurityActionWidth - 96))
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
    property bool settingsTabOpen: false
    property string settingsReturnPage: "hosts"
    property bool startupVaultPromptPresented: false
    property real hostsPageReveal: 1.0
    property bool terminalSearchVisible: false
    property int pendingPasteLineCount: 0
    property bool appearancePreviewActive: false
    property string previewThemePreference: "dark"
    property string previewBackdropPreference: "acrylic"
    property real previewBackdropOpacity: 1.0
    property string previewAccentPreference: "ztermy"
    property color previewCustomAccent: "#22C55E"
    property double sessionClock: Date.now()
    readonly property var activeTerminalTab: {
        for (const tab of controller.terminalTabs) {
            if (tab.id === controller.activeTerminalTabId) {
                return tab;
            }
        }
        return null;
    }
    readonly property bool activeSshFailure: activeTerminalTab !== null && activeTerminalTab.kind === "ssh" && activeTerminalTab.failed
    readonly property bool activeSshConnecting: activeTerminalTab !== null && activeTerminalTab.kind === "ssh" && activeTerminalTab.connecting
    readonly property bool activeSshDisconnected: activeTerminalTab !== null && activeTerminalTab.kind === "ssh" && activeTerminalTab.remoteClosed
    readonly property string activeTerminalWorkbenchSide: activeTerminalTab !== null ? activeTerminalTab.workbenchSide : "left"
    readonly property real activeTerminalWorkbenchWidth: activeTerminalTab !== null && activeTerminalTab.workbenchOpen ? Math.min(activeTerminalTab.workbenchWidth, Math.max(0, terminalBody.width - 240)) : 0
    readonly property real activeTerminalComposerHeight: activeTerminalTab !== null && activeTerminalTab.composerOpen ? Math.min(activeTerminalTab.composerHeight, Math.max(0, terminalBody.height - 120)) : 0
    readonly property bool portableVaultNeedsAttention: controller.effectiveCredentialStorage === "portable" && (!controller.portableVaultInitialized || controller.portableVaultLocked)
    readonly property bool terminalTelemetryVisible: currentPage === "terminal" && visible && root.Window.window !== null && root.Window.window.active

    component TerminalToolbarButton: ToolButton {
        id: control

        property bool selected: false

        hoverEnabled: true
        focusPolicy: Qt.StrongFocus

        background: Item {
            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width, parent.height)
                height: width
                radius: width / 2
                color: control.down ? Theme.controlPressed : control.hovered ? Theme.controlHover : "transparent"
                border.color: control.activeFocus ? Theme.focus : "transparent"
                border.width: control.activeFocus ? 2 : 0

                Behavior on color {
                    ColorAnimation {
                        duration: Theme.motionFast
                    }
                }
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                width: control.selected ? 10 : 0
                height: 2
                radius: 1
                color: Theme.accent

                Behavior on width {
                    NumberAnimation {
                        duration: Theme.motionFast
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }

        HoverHandler {
            cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        }
    }

    color: root.currentPage === "terminal" ? "transparent" : backgroundColor

    function reportTitleBarMetrics() {
        root.windowChrome.setTitleBarMetrics(titleBarHeight, titleNavigation.width + 8, width - (captionButtonWidth * 3) - titleQuickActionsWidth - titleSecurityActionWidth, width - (captionButtonWidth * 2), captionButtonWidth);
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

    function closeSettingsTab() {
        if (!settingsTabOpen) {
            return;
        }
        settingsTabOpen = false;
        currentPage = settingsReturnPage === "settings" ? (controller.terminalTabs.length > 0 ? "terminal" : "hosts") : settingsReturnPage;
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
        root.windowChrome.applyAppearance(controller.backdropPreference, Theme.dark);
    }

    function previewWindowAppearance(theme, opacity, backdrop, accent, customAccent) {
        previewThemePreference = theme;
        previewBackdropPreference = backdrop;
        previewBackdropOpacity = opacity;
        previewAccentPreference = accent;
        previewCustomAccent = customAccent;
        appearancePreviewActive = true;
        const previewDark = theme === "dark" || (theme === "system" && root.windowChrome.systemDarkMode);
        root.windowChrome.applyAppearance(backdrop, previewDark);
    }

    function endWindowAppearancePreview() {
        if (!appearancePreviewActive) {
            return;
        }
        appearancePreviewActive = false;
        Qt.callLater(root.applyWindowAppearance);
    }

    function startLocalTerminalTab() {
        controller.startLocalTerminal();
        currentPage = "terminal";
        terminalViewport.forceActiveFocus();
    }

    function closeActiveTerminalTab() {
        if (controller.activeTerminalTabId.length === 0) {
            return;
        }
        controller.closeTerminalTab(controller.activeTerminalTabId);
    }

    function activateRelativeTerminalTab(offset) {
        const tabs = controller.terminalTabs;
        if (tabs.length < 2) {
            return;
        }
        let currentIndex = 0;
        for (let index = 0; index < tabs.length; ++index) {
            if (tabs[index].id === controller.activeTerminalTabId) {
                currentIndex = index;
                break;
            }
        }
        const nextIndex = (currentIndex + offset + tabs.length) % tabs.length;
        controller.activateTerminalTab(tabs[nextIndex].id);
        currentPage = "terminal";
        terminalViewport.forceActiveFocus();
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
            closeActiveTerminalTab();
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
        value: root.appearancePreviewActive ? root.previewBackdropPreference : root.controller.backdropPreference
    }

    Binding {
        target: Theme
        property: "backdropOpacity"
        value: root.appearancePreviewActive ? root.previewBackdropOpacity : root.controller.backdropOpacity
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
        property: "systemAccent"
        value: root.windowChrome.systemAccentColor
    }

    Binding {
        target: Theme
        property: "uiFont"
        value: root.fontCatalog.effectiveUiFamily(root.controller.uiFontFamily)
    }

    Component.onCompleted: {
        reportTitleBarMetrics();
        applyWindowAppearance();
        controller.setTerminalTelemetryVisible(terminalTelemetryVisible);
        Qt.callLater(root.presentStartupVaultPrompt);
    }
    onTerminalTelemetryVisibleChanged: controller.setTerminalTelemetryVisible(terminalTelemetryVisible)
    onWidthChanged: reportTitleBarMetrics()
    onCurrentPageChanged: {
        if (currentPage === "settings") {
            settingsTabOpen = true;
        }
        if (currentPage === "hosts") {
            hostsPageReveal = Theme.animationsEnabled ? 0.0 : 1.0;
            if (Theme.animationsEnabled) {
                hostsPageEntryAnimation.restart();
            }
        }
        if (currentPage !== "settings") {
            endWindowAppearancePreview();
        }
        if (currentPage === "terminal") {
            terminalViewport.forceActiveFocus();
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
            Qt.callLater(root.applyWindowAppearance);
        }

        function onTerminalTabsChanged() {
            Qt.callLater(titleTerminalTabs.syncCurrentIndex);
            if (root.currentPage === "terminal" && root.controller.terminalTabs.length === 0) {
                root.currentPage = "hosts";
                Qt.callLater(hostsTitleAction.forceActiveFocus);
            }
        }

        function onActiveTerminalTabChanged() {
            Qt.callLater(titleTerminalTabs.syncCurrentIndex);
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
    }

    Timer {
        interval: 1000
        running: root.visible
        repeat: true
        onTriggered: root.sessionClock = Date.now()
    }

    NumberAnimation {
        id: hostsPageEntryAnimation

        target: root
        property: "hostsPageReveal"
        from: 0.0
        to: 1.0
        duration: Theme.motionMedium
        easing.type: Easing.OutCubic
    }

    Connections {
        target: root.windowChrome

        function onSystemDarkModeChanged() {
            if (root.appearancePreviewActive) {
                Qt.callLater(() => root.previewWindowAppearance(root.previewThemePreference, root.previewBackdropOpacity, root.previewBackdropPreference, root.previewAccentPreference, root.previewCustomAccent));
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

            anchors.left: parent.left
            anchors.top: parent.top
            width: childrenRect.width
            height: parent.height
            spacing: 0
            onWidthChanged: root.reportTitleBarMetrics()

            Rectangle {
                width: 44
                height: titleNavigation.height
                color: "transparent"

                BrandIcon {
                    objectName: "titleBrandIcon"
                    anchors.centerIn: parent
                    width: 24
                    height: 24
                    tileColor: root.accentColor
                    ribbonColor: Theme.accentText
                    promptColor: Theme.contrastText(ribbonColor)
                    promptStrokeWidth: 0.86
                }
            }

            Rectangle {
                id: hostsTitleTab

                width: 94
                height: titleNavigation.height
                color: root.currentPage === "hosts" ? Theme.controlBackground : (hostsTitleAction.hovered || hostsTitleAction.activeFocus ? Theme.controlHover : "transparent")
                border.color: hostsTitleAction.activeFocus ? Theme.focus : "transparent"
                border.width: hostsTitleAction.activeFocus ? 1 : 0

                Behavior on color {
                    ColorAnimation {
                        duration: Theme.motionFast
                    }
                }

                Row {
                    anchors.centerIn: parent
                    spacing: 8

                    AppIcon {
                        width: 16
                        height: 16
                        name: "hosts"
                        color: root.currentPage === "hosts" ? root.textColor : root.mutedColor
                    }

                    Text {
                        text: qsTr("Hosts")
                        color: root.currentPage === "hosts" ? root.textColor : root.mutedColor
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textBody
                        font.weight: root.currentPage === "hosts" ? Font.DemiBold : Font.Normal
                    }
                }

                KeyboardAction {
                    id: hostsTitleAction

                    objectName: "hostsTitleAction"
                    anchors.fill: parent
                    anchors.margins: 2
                    accessibleName: qsTr("Hosts")
                    onActivated: root.currentPage = "hosts"
                }
            }

            TerminalTabAction {
                id: settingsTitleTab

                visible: root.settingsTabOpen
                width: visible ? implicitWidth : 0
                height: titleNavigation.height
                title: qsTr("Settings")
                iconName: "settings"
                actionObjectName: "settingsTitleAction"
                closeActionObjectName: "settingsTitleCloseAction"
                selected: root.currentPage === "settings"
                onActivated: root.openSettingsTab()
                onCloseRequested: root.closeSettingsTab()
            }

            ListView {
                id: titleTerminalTabs

                objectName: "titleTerminalTabs"
                currentIndex: -1
                width: count === 0 ? 0 : Math.min(contentWidth, Math.max(140, root.titleNavigationWidth - 174 - settingsTitleTab.width))
                height: titleNavigation.height
                orientation: ListView.Horizontal
                spacing: 2
                clip: true
                model: root.controller.terminalTabs
                onCountChanged: Qt.callLater(ensureCurrentTabVisible)
                onCurrentIndexChanged: Qt.callLater(ensureCurrentTabVisible)
                onWidthChanged: Qt.callLater(ensureCurrentTabVisible)

                addDisplaced: Transition {
                    NumberAnimation {
                        properties: "x"
                        duration: Theme.motionMedium
                        easing.type: Easing.OutCubic
                    }
                }

                remove: Transition {
                    NumberAnimation {
                        property: "opacity"
                        to: 0.0
                        duration: Theme.motionFast
                        easing.type: Easing.InCubic
                    }
                }

                removeDisplaced: Transition {
                    NumberAnimation {
                        properties: "x"
                        duration: Theme.motionMedium
                        easing.type: Easing.OutCubic
                    }
                }

                function syncCurrentIndex() {
                    let activeIndex = -1;
                    for (let index = 0; index < root.controller.terminalTabs.length; ++index) {
                        if (root.controller.terminalTabs[index].id === root.controller.activeTerminalTabId) {
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

                delegate: TerminalTabAction {
                    id: titleTerminalTab

                    required property var modelData

                    title: modelData.title
                    selected: root.currentPage === "terminal" && root.controller.activeTerminalTabId === modelData.id
                    running: modelData.running
                    width: implicitWidth
                    height: titleTerminalTabs.height
                    onActivated: {
                        root.controller.activateTerminalTab(modelData.id);
                        root.currentPage = "terminal";
                        terminalViewport.forceActiveFocus();
                    }
                    onCloseRequested: root.controller.closeTerminalTab(modelData.id)
                }
            }

            Rectangle {
                objectName: "titleNewTabContainer"
                width: 36
                height: titleNavigation.height
                color: titleNewTabAction.hovered || titleNewTabAction.activeFocus ? Theme.controlHover : "transparent"
                border.color: titleNewTabAction.activeFocus ? Theme.focus : "transparent"
                border.width: titleNewTabAction.activeFocus ? 1 : 0

                Behavior on color {
                    ColorAnimation {
                        duration: Theme.motionFast
                    }
                }

                AppIcon {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    name: "plus"
                    color: root.textColor
                }

                KeyboardAction {
                    id: titleNewTabAction

                    objectName: "titleNewTabAction"
                    anchors.fill: parent
                    anchors.margins: 2
                    accessibleName: qsTr("Open new terminal menu")
                    onActivated: newTerminalMenu.open()
                }

                AppToolTip {
                    visible: titleNewTabAction.hovered && !newTerminalMenu.visible
                    text: qsTr("New terminal")
                }

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

                    AppMenuItem {
                        objectName: "browseHostsMenuAction"
                        text: qsTr("Browse hosts")
                        onTriggered: {
                            root.currentPage = "hosts";
                            Qt.callLater(hostsTitleAction.forceActiveFocus);
                        }
                    }
                }
            }
        }

        Row {
            id: titleControls

            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height

            Rectangle {
                width: root.titleSecurityActionWidth
                height: titleBar.height
                visible: root.portableVaultNeedsAttention
                color: portableVaultStatusAction.hovered || portableVaultStatusAction.activeFocus ? Theme.controlHover : "transparent"
                border.color: portableVaultStatusAction.activeFocus ? Theme.focus : "transparent"
                border.width: portableVaultStatusAction.activeFocus ? 1 : 0

                Behavior on color {
                    ColorAnimation {
                        duration: Theme.motionFast
                    }
                }

                AppIcon {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    name: "lock"
                    color: Theme.dangerText
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.rightMargin: 7
                    anchors.top: parent.top
                    anchors.topMargin: 7
                    width: 6
                    height: 6
                    radius: 3
                    color: Theme.danger
                    border.color: Theme.chromeBackground
                    border.width: 1
                }

                KeyboardAction {
                    id: portableVaultStatusAction

                    objectName: "portableVaultStatusAction"
                    anchors.fill: parent
                    anchors.margins: 2
                    accessibleName: root.controller.portableVaultInitialized ? qsTr("Portable vault locked; unlock") : qsTr("Portable vault not configured; open Security settings")
                    onActivated: root.requestPortableVaultAccess(portableVaultStatusAction)
                }
            }

            Rectangle {
                width: root.titleQuickActionWidth
                height: titleBar.height
                color: transferCenterAction.hovered || transferCenterAction.activeFocus ? Theme.controlHover : "transparent"
                border.color: transferCenterAction.activeFocus ? Theme.focus : "transparent"
                border.width: transferCenterAction.activeFocus ? 1 : 0

                AppIcon {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    name: "transfer"
                    color: transferCenter.visible ? root.textColor : root.mutedColor
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.top: parent.top
                    anchors.topMargin: 5
                    visible: root.controller.activeTransferCount > 0
                    width: Math.max(12, transferCountText.implicitWidth + 4)
                    height: 12
                    radius: 6
                    color: Theme.accent

                    Text {
                        id: transferCountText

                        anchors.centerIn: parent
                        text: root.controller.activeTransferCount > 9 ? "9+" : root.controller.activeTransferCount
                        color: Theme.accentText
                        font.family: Theme.uiFont
                        font.pixelSize: 8
                        font.weight: Font.Bold
                    }
                }

                KeyboardAction {
                    id: transferCenterAction

                    objectName: "transferCenterAction"
                    anchors.fill: parent
                    anchors.margins: 2
                    accessibleName: qsTr("Open file transfers")
                    onActivated: transferCenter.visible ? transferCenter.close() : transferCenter.open()
                }

                AppToolTip {
                    visible: transferCenterAction.hovered
                    text: qsTr("File transfers")
                }
            }

            Rectangle {
                width: root.titleQuickActionWidth
                height: titleBar.height
                color: commandPaletteAction.hovered || commandPaletteAction.activeFocus ? Theme.controlHover : "transparent"
                border.color: commandPaletteAction.activeFocus ? Theme.focus : "transparent"
                border.width: commandPaletteAction.activeFocus ? 1 : 0

                Behavior on color {
                    ColorAnimation {
                        duration: Theme.motionFast
                    }
                }

                AppIcon {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    name: "search"
                    color: commandPalette.visible ? root.textColor : root.mutedColor
                }

                KeyboardAction {
                    id: commandPaletteAction

                    objectName: "commandPaletteAction"
                    anchors.fill: parent
                    anchors.margins: 2
                    accessibleName: qsTr("Open command palette")
                    onActivated: commandPalette.open()
                }

                AppToolTip {
                    visible: commandPaletteAction.hovered
                    text: {
                        const shortcut = root.shortcutFor("application.commandPalette");
                        return shortcut.length > 0 ? qsTr("Command palette") + " · " + shortcut : qsTr("Command palette");
                    }
                }
            }

            Rectangle {
                width: root.titleQuickActionWidth
                height: titleBar.height
                color: settingsShortcutAction.hovered || settingsShortcutAction.activeFocus ? Theme.controlHover : "transparent"
                border.color: settingsShortcutAction.activeFocus ? Theme.focus : "transparent"
                border.width: settingsShortcutAction.activeFocus ? 1 : 0

                Behavior on color {
                    ColorAnimation {
                        duration: Theme.motionFast
                    }
                }

                AppIcon {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    name: "settings"
                    color: root.currentPage === "settings" ? root.textColor : root.mutedColor
                }

                KeyboardAction {
                    id: settingsShortcutAction

                    objectName: "settingsShortcutAction"
                    anchors.fill: parent
                    anchors.margins: 2
                    accessibleName: qsTr("Open Settings")
                    onActivated: root.openSettingsTab()
                }
            }

            CaptionButton {
                objectName: "minimizeCaptionButton"
                width: root.captionButtonWidth
                height: titleBar.height
                kind: "minimize"
                chrome: root.windowChrome
                accessibleName: qsTr("Minimize")
                onActivated: root.windowChrome.minimizeWindow()
            }

            CaptionButton {
                objectName: "maximizeCaptionButton"
                width: root.captionButtonWidth
                height: titleBar.height
                kind: "maximize"
                chrome: root.windowChrome
                accessibleName: root.windowChrome.maximized ? qsTr("Restore") : qsTr("Maximize")
                externallyHovered: root.windowChrome.maximizeButtonHovered
                externallyPressed: root.windowChrome.maximizeButtonPressed
                onActivated: root.windowChrome.toggleMaximize()
            }

            CaptionButton {
                objectName: "closeCaptionButton"
                width: root.captionButtonWidth
                height: titleBar.height
                kind: "close"
                chrome: root.windowChrome
                accessibleName: qsTr("Close")
                onActivated: root.windowChrome.closeWindow()
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

    RowLayout {
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: visible ? (root.width < Theme.narrowWindowWidth ? Theme.navigationWidthCompact : Theme.navigationWidth) : 0
            Layout.minimumWidth: Layout.preferredWidth
            Layout.maximumWidth: Layout.preferredWidth
            visible: root.currentPage === "hosts"
            color: root.panelColor

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 4
                    Layout.rightMargin: 4
                    Layout.bottomMargin: 8
                    Layout.preferredHeight: 38
                    spacing: 9

                    BrandIcon {
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        tileColor: Theme.accent
                        ribbonColor: Theme.accentText
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("ztermy")
                        color: Theme.text
                        font.family: Theme.uiFont
                        font.pixelSize: 17
                        font.weight: Font.Bold
                    }
                }

                SideNavigationItem {
                    actionObjectName: "sideHostsAction"
                    Layout.fillWidth: true
                    iconName: "hosts"
                    text: qsTr("Hosts")
                    selected: root.currentPage === "hosts"
                    onActivated: root.currentPage = "hosts"
                }

                SideNavigationItem {
                    actionObjectName: "sideCredentialsAction"
                    Layout.fillWidth: true
                    iconName: "security"
                    text: qsTr("Credentials")
                    onActivated: root.openSecuritySettingsTab()
                }

                SideNavigationItem {
                    actionObjectName: "sideTransfersAction"
                    Layout.fillWidth: true
                    iconName: "transfer"
                    text: qsTr("Transfers")
                    onActivated: transferCenter.visible ? transferCenter.close() : transferCenter.open()
                }

                Item {
                    Layout.fillHeight: true
                }

                SideNavigationItem {
                    actionObjectName: "sideSettingsAction"
                    Layout.fillWidth: true
                    iconName: "settings"
                    text: qsTr("Settings")
                    onActivated: root.openSettingsTab()
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
                                radius: 3
                                color: root.activeTerminalTab && root.activeTerminalTab.running ? root.accentColor : Theme.textSubtle
                            }

                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                Layout.maximumWidth: 250
                                text: root.activeTerminalTab ? root.activeTerminalTab.identity : qsTr("Terminal")
                                color: root.textColor
                                elide: Text.ElideRight
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textCompact
                                font.weight: Font.DemiBold
                            }

                            TerminalToolbarButton {
                                id: copyAddressButton

                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 22
                                visible: root.activeTerminalTab !== null && root.activeTerminalTab.address.length > 0
                                onClicked: root.controller.copyActiveTerminalAddress()
                                Accessible.name: qsTr("Copy host address")
                                contentItem: AppIcon {
                                    name: "copy"
                                    color: root.mutedColor
                                }

                                AppToolTip {
                                    visible: copyAddressButton.hovered
                                    text: qsTr("Copy host address")
                                }
                            }

                            Rectangle {
                                Layout.alignment: Qt.AlignVCenter
                                Layout.preferredWidth: 1
                                Layout.preferredHeight: 12
                                color: root.borderColor
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
                                color: root.mutedColor
                                elide: Text.ElideRight
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textCompact
                            }

                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                visible: root.width >= 900 && root.activeTerminalTab !== null && root.activeTerminalTab.connectedUtcMs > 0
                                text: root.activeTerminalTab !== null ? qsTr("Connected %1").arg(root.formatSessionDuration(root.activeTerminalTab.connectedUtcMs)) : ""
                                color: root.mutedColor
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textCompact
                                Accessible.name: text
                            }

                            Item {
                                Layout.fillWidth: !terminalSessionStatus.visible
                                visible: !terminalSessionStatus.visible
                            }

                            TerminalToolbarButton {
                                id: keywordHighlightButton

                                objectName: "terminalKeywordHighlightAction"
                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 22
                                checkable: true
                                checked: root.activeTerminalTab !== null && root.activeTerminalTab.keywordHighlightEnabled && root.activeTerminalTab.keywordHighlightRules.length > 0
                                selected: checked
                                visible: root.width >= 980 && root.activeTerminalTab !== null && root.activeTerminalTab.kind === "ssh"
                                enabled: root.activeTerminalTab !== null
                                onClicked: keywordHighlightPopover.openFor(keywordHighlightButton)
                                Keys.onReturnPressed: click()
                                Keys.onEnterPressed: click()
                                Accessible.name: qsTr("Host keyword highlighting")
                                contentItem: AppIcon {
                                    name: "highlight"
                                    color: keywordHighlightButton.checked ? Theme.accent : root.mutedColor
                                }
                                AppToolTip {
                                    visible: keywordHighlightButton.hovered && !keywordHighlightPopover.visible
                                    text: qsTr("Host keyword highlighting")
                                }
                            }

                            TerminalToolbarButton {
                                id: sftpToolbarButton

                                objectName: "terminalSftpAction"
                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 22
                                checkable: true
                                checked: root.activeTerminalTab !== null && root.activeTerminalTab.workbenchOpen && root.activeTerminalTab.workbenchPage === "sftp"
                                selected: checked
                                visible: root.width >= 900
                                enabled: root.activeTerminalTab !== null && root.activeTerminalTab.connected
                                onClicked: root.controller.toggleTerminalWorkbench("sftp")
                                Keys.onReturnPressed: click()
                                Keys.onEnterPressed: click()
                                Accessible.name: qsTr("Open SFTP")
                                contentItem: AppIcon {
                                    name: "folder"
                                    color: sftpToolbarButton.checked ? Theme.accent : root.mutedColor
                                }
                                AppToolTip {
                                    visible: sftpToolbarButton.hovered
                                    text: qsTr("Open SFTP")
                                }
                            }

                            TerminalToolbarButton {
                                id: composerToolbarButton

                                objectName: "terminalComposerAction"
                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 22
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
                                Keys.onReturnPressed: click()
                                Keys.onEnterPressed: click()
                                Accessible.name: qsTr("Command composer")
                                contentItem: AppIcon {
                                    name: "compose"
                                    color: composerToolbarButton.checked ? Theme.accent : root.mutedColor
                                }

                                AppToolTip {
                                    visible: composerToolbarButton.hovered
                                    text: qsTr("Command composer")
                                }
                            }

                            TerminalToolbarButton {
                                id: terminalFindButton

                                objectName: "terminalFindAction"
                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 22
                                visible: root.width >= 700
                                enabled: root.activeTerminalTab !== null
                                onClicked: root.toggleTerminalSearch()
                                Keys.onReturnPressed: click()
                                Keys.onEnterPressed: click()
                                Accessible.name: qsTr("Find in terminal")
                                contentItem: AppIcon {
                                    name: "search"
                                    color: root.mutedColor
                                }
                                AppToolTip {
                                    visible: terminalFindButton.hovered
                                    text: qsTr("Find in terminal")
                                }
                            }

                            TerminalToolbarButton {
                                id: sessionLogToolbarButton

                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 22
                                checkable: true
                                checked: root.activeTerminalTab !== null && (root.activeTerminalTab.logState === "active" || root.activeTerminalTab.logState === "starting")
                                selected: checked
                                visible: root.width >= 860
                                enabled: root.activeTerminalTab !== null
                                onClicked: root.toggleSessionLog()
                                Accessible.name: checked ? qsTr("Stop session log") : qsTr("Start session log")
                                contentItem: AppIcon {
                                    name: "save"
                                    color: root.activeTerminalTab !== null && root.activeTerminalTab.logDroppedBytes > 0 ? Theme.warning : sessionLogToolbarButton.checked ? Theme.accent : root.mutedColor
                                }
                                AppToolTip {
                                    visible: sessionLogToolbarButton.hovered
                                    text: root.activeTerminalTab !== null && root.activeTerminalTab.logDroppedBytes > 0 ? qsTr("Session log is incomplete: %1 byte(s) were dropped.").arg(root.activeTerminalTab.logDroppedBytes) : sessionLogToolbarButton.checked ? qsTr("Stop session log") : qsTr("Start session log")
                                }
                            }

                            TerminalToolbarButton {
                                id: scriptsToolbarButton

                                objectName: "terminalScriptsAction"
                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 22
                                checkable: true
                                checked: root.activeTerminalTab !== null && root.activeTerminalTab.workbenchOpen && root.activeTerminalTab.workbenchPage === "scripts"
                                selected: checked
                                visible: root.width >= 940
                                enabled: root.activeTerminalTab !== null
                                onClicked: root.controller.toggleTerminalWorkbench("scripts")
                                Keys.onReturnPressed: click()
                                Keys.onEnterPressed: click()
                                Accessible.name: qsTr("Command snippets")
                                contentItem: AppIcon {
                                    name: "commands"
                                    color: scriptsToolbarButton.checked ? Theme.accent : root.mutedColor
                                }
                                AppToolTip {
                                    visible: scriptsToolbarButton.hovered
                                    text: qsTr("Command snippets")
                                }
                            }

                            TerminalToolbarButton {
                                id: scriptRecordingIndicator

                                objectName: "terminalScriptRecordingIndicator"
                                Layout.preferredWidth: root.activeTerminalTab !== null && root.activeTerminalTab.scriptRecordingState === "review" ? 42 : 50
                                Layout.preferredHeight: 22
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
                                        radius: 4
                                        color: Theme.danger
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: root.activeTerminalTab !== null && root.activeTerminalTab.scriptRecordingState === "review" ? root.activeTerminalTab.scriptRecordingSteps.length : "REC"
                                        color: root.textColor
                                        font.family: Theme.uiFont
                                        font.pixelSize: Theme.textCompact
                                        font.weight: Font.DemiBold
                                    }
                                }
                                AppToolTip {
                                    visible: scriptRecordingIndicator.hovered && !terminalRecordingPopover.visible
                                    text: root.activeTerminalTab !== null && root.activeTerminalTab.scriptRecordingState === "recording" ? qsTr("Pause script recording") : root.activeTerminalTab !== null && root.activeTerminalTab.scriptRecordingState === "paused" ? qsTr("Resume script recording") : qsTr("Review recorded commands")
                                }
                            }

                            TerminalToolbarButton {
                                id: terminalMoreButton
                                objectName: "terminalMoreAction"

                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 22
                                enabled: root.activeTerminalTab !== null
                                onClicked: terminalMoreMenu.open()
                                Keys.onReturnPressed: click()
                                Keys.onEnterPressed: click()
                                Accessible.name: qsTr("More terminal actions")
                                contentItem: AppIcon {
                                    name: "more"
                                    color: root.mutedColor
                                }

                                AppToolTip {
                                    visible: terminalMoreButton.hovered && !terminalMoreMenu.visible
                                    text: qsTr("More terminal actions")
                                }

                                AppMenu {
                                    id: terminalMoreMenu

                                    y: terminalMoreButton.height

                                    AppMenuItem {
                                        objectName: "terminalHistoryMenuAction"
                                        text: qsTr("Command history")
                                        onTriggered: root.controller.toggleTerminalWorkbench("history")
                                    }

                                    AppMenuItem {
                                        text: qsTr("Host keyword highlighting")
                                        visible: !keywordHighlightButton.visible && root.activeTerminalTab !== null && root.activeTerminalTab.kind === "ssh"
                                        onTriggered: keywordHighlightPopover.openFor(terminalMoreButton)
                                    }

                                    AppMenuItem {
                                        text: qsTr("Open SFTP")
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
                                        text: qsTr("Follow terminal directory")
                                        checkable: true
                                        checked: root.controller.activeSftpFollowTerminalDirectory
                                        enabled: root.activeTerminalTab !== null && root.activeTerminalTab.connected
                                        onTriggered: root.controller.setSftpFollowTerminalDirectory(checked)
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

                        TerminalView {
                            id: terminalViewport
                            objectName: "terminalViewport"
                            anchors.fill: parent
                            anchors.leftMargin: root.activeTerminalWorkbenchSide === "left" ? root.activeTerminalWorkbenchWidth : 0
                            anchors.rightMargin: root.activeTerminalWorkbenchSide === "right" ? root.activeTerminalWorkbenchWidth : 0
                            anchors.bottomMargin: root.activeTerminalComposerHeight
                            focus: true
                            fontFamily: root.activeTerminalTab !== null && root.activeTerminalTab.sessionFontFamily.length > 0 ? root.activeTerminalTab.sessionFontFamily : root.controller.terminalFontFamily
                            fontPixelSize: root.activeTerminalTab !== null && root.activeTerminalTab.sessionFontSize > 0 ? root.activeTerminalTab.sessionFontSize : root.controller.terminalFontSize
                            ligaturesEnabled: root.activeTerminalTab !== null && root.activeTerminalTab.sessionFontSize > 0 ? root.activeTerminalTab.sessionLigatures : root.controller.terminalLigatures
                            backgroundOpacity: root.activeTerminalTab !== null && root.activeTerminalTab.sessionBackgroundOpacity >= 0 ? root.activeTerminalTab.sessionBackgroundOpacity : root.controller.terminalBackgroundOpacity
                            cursorPreference: root.activeTerminalTab !== null && root.activeTerminalTab.sessionCursor.length > 0 ? root.activeTerminalTab.sessionCursor : root.controller.cursorPreference
                            foregroundOverride: root.activeTerminalTab !== null ? root.activeTerminalTab.sessionForeground : ""
                            backgroundOverride: root.activeTerminalTab !== null ? root.activeTerminalTab.sessionBackground : ""
                            cursorBlink: root.controller.cursorBlink
                            copyOnSelect: root.controller.copyOnSelect
                            confirmMultilinePaste: root.controller.confirmMultilinePaste

                            Component.onCompleted: forceActiveFocus()
                            onMultilinePasteConfirmationRequested: lineCount => {
                                root.pendingPasteLineCount = lineCount;
                                multilinePasteDialog.openFrom(terminalViewport);
                            }

                            Behavior on anchors.rightMargin {
                                NumberAnimation {
                                    duration: Theme.animationsEnabled ? Theme.motionMedium : 0
                                    easing.type: Easing.OutCubic
                                }
                            }

                            Behavior on anchors.leftMargin {
                                NumberAnimation {
                                    duration: Theme.animationsEnabled ? Theme.motionMedium : 0
                                    easing.type: Easing.OutCubic
                                }
                            }

                            Behavior on anchors.bottomMargin {
                                NumberAnimation {
                                    duration: Theme.animationsEnabled ? Theme.motionMedium : 0
                                    easing.type: Easing.OutCubic
                                }
                            }
                        }

                        TerminalKeywordPopover {
                            id: keywordHighlightPopover
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

                        Item {
                            id: terminalScrollbar

                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.topMargin: 8
                            anchors.rightMargin: (root.activeTerminalWorkbenchSide === "right" ? root.activeTerminalWorkbenchWidth : 0) + 9
                            anchors.bottomMargin: root.activeTerminalComposerHeight + 8
                            width: 16
                            visible: terminalViewport.scrollbarVisible && root.activeTerminalTab !== null
                            enabled: visible
                            z: 12

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 4
                                height: parent.height
                                radius: 2
                                color: Theme.dark ? "#66334155" : "#6694A3B8"
                            }

                            Rectangle {
                                id: terminalScrollbarThumb

                                readonly property real travel: Math.max(0, terminalScrollbar.height - height)

                                x: (terminalScrollbar.width - width) / 2
                                y: travel * terminalViewport.scrollbarPosition
                                width: terminalScrollbarMouse.containsMouse || terminalScrollbarMouse.pressed ? 8 : 6
                                height: Math.min(terminalScrollbar.height, Math.max(28, terminalScrollbar.height * terminalViewport.scrollbarPageRatio))
                                radius: width / 2
                                color: terminalScrollbarMouse.pressed ? Theme.text : terminalScrollbarMouse.containsMouse ? Theme.textSoft : Theme.textMuted
                            }

                            MouseArea {
                                id: terminalScrollbarMouse

                                property real grabOffset: terminalScrollbarThumb.height / 2

                                function applyPointer(pointerY) {
                                    if (terminalScrollbarThumb.travel <= 0) {
                                        return;
                                    }
                                    const thumbTop = Math.max(0, Math.min(terminalScrollbarThumb.travel, pointerY - grabOffset));
                                    terminalViewport.scrollToFraction(thumbTop / terminalScrollbarThumb.travel);
                                }

                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onPressed: mouse => {
                                    if (mouse.y >= terminalScrollbarThumb.y && mouse.y <= terminalScrollbarThumb.y + terminalScrollbarThumb.height) {
                                        grabOffset = mouse.y - terminalScrollbarThumb.y;
                                    } else {
                                        grabOffset = terminalScrollbarThumb.height / 2;
                                        applyPointer(mouse.y);
                                    }
                                }
                                onPositionChanged: mouse => {
                                    if (pressed) {
                                        applyPointer(mouse.y);
                                    }
                                }
                            }
                        }

                        Rectangle {
                            id: searchPanel

                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: 12
                            anchors.rightMargin: (root.activeTerminalWorkbenchSide === "right" ? root.activeTerminalWorkbenchWidth : 0) + 12
                            width: 420
                            height: 42
                            radius: 8
                            color: Theme.floatingBackground
                            border.color: root.borderColor
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
                                        border.color: caseSensitiveButton.activeFocus ? Theme.focus : caseSensitiveButton.checked ? Theme.accentHover : root.borderColor
                                        border.width: caseSensitiveButton.activeFocus ? 2 : 1
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
                                NumberAnimation {
                                    duration: Theme.animationsEnabled ? Theme.motionMedium : 0
                                    easing.type: Easing.OutCubic
                                }
                            }
                        }

                        TerminalWorkbench {
                            id: terminalWorkbench

                            objectName: "terminalWorkbench"
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: root.activeTerminalWorkbenchWidth
                            x: root.activeTerminalWorkbenchSide === "left" ? 0 : parent.width - width
                            visible: root.activeTerminalTab !== null && root.activeTerminalTab.workbenchOpen
                            z: 15
                            controller: root.controller
                            activeTab: root.activeTerminalTab
                            panelSide: root.activeTerminalWorkbenchSide
                            panelWidth: root.activeTerminalTab !== null ? root.activeTerminalTab.workbenchWidth : 520
                            onPanelWidthRequested: width => root.controller.setTerminalWorkbenchWidth(width)
                            onInsertRequested: command => {
                                if (root.controller.insertTerminalCommand(command)) {
                                    terminalViewport.forceActiveFocus();
                                }
                            }
                            onRunRequested: command => root.requestTerminalCommandRun(command)
                            onImportLibraryRequested: scriptImportDialog.open()
                            onExportLibraryRequested: scriptExportDialog.open()
                            onCloseRequested: {
                                root.controller.closeTerminalWorkbench();
                                terminalViewport.forceActiveFocus();
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

                        StatePanel {
                            anchors.centerIn: parent
                            width: Math.min(520, parent.width - 48)
                            visible: root.activeSshConnecting
                            z: 9
                            kind: "loading"
                            heading: qsTr("Connecting to SSH host")
                            description: root.activeTerminalTab ? root.activeTerminalTab.status : ""
                            detail: qsTr("Connection setup runs outside the interface thread. You can close this tab to cancel.")

                            ActionButton {
                                text: qsTr("Cancel connection")
                                Accessible.name: qsTr("Cancel SSH connection and close tab")
                                onClicked: {
                                    if (root.activeTerminalTab) {
                                        root.controller.closeTerminalTab(root.activeTerminalTab.id);
                                    }
                                }
                            }
                        }

                        StatePanel {
                            anchors.centerIn: parent
                            width: Math.min(520, parent.width - 48)
                            visible: root.activeSshDisconnected
                            z: 9
                            kind: "disconnected"
                            heading: qsTr("SSH session ended")
                            description: root.activeTerminalTab ? root.activeTerminalTab.status : ""
                            detail: qsTr("The remote host closed the terminal connection. Credentials are not retained for automatic reconnection.")

                            ActionButton {
                                text: qsTr("Close tab")
                                accessibleName: qsTr("Close ended SSH terminal tab")
                                onClicked: {
                                    if (root.activeTerminalTab) {
                                        root.controller.closeTerminalTab(root.activeTerminalTab.id);
                                    }
                                }
                            }

                            ActionButton {
                                text: qsTr("Review host")
                                accessibleName: qsTr("Return to SSH host profiles")
                                variant: "primary"
                                onClicked: root.currentPage = "hosts"
                            }
                        }

                        StatePanel {
                            anchors.centerIn: parent
                            width: Math.min(520, parent.width - 48)
                            visible: root.activeSshFailure
                            z: 9
                            kind: "error"
                            heading: qsTr("SSH session unavailable")
                            description: root.activeTerminalTab ? root.activeTerminalTab.status : ""
                            detail: qsTr("Review the host and authentication settings before starting a new connection. Credentials are not retained for retry.")

                            ActionButton {
                                text: qsTr("Close tab")
                                Accessible.name: qsTr("Close failed SSH terminal tab")
                                onClicked: {
                                    if (root.activeTerminalTab) {
                                        root.controller.closeTerminalTab(root.activeTerminalTab.id);
                                    }
                                }
                            }

                            ActionButton {
                                text: qsTr("Review host")
                                Accessible.name: qsTr("Return to SSH host profiles")
                                variant: "primary"
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
                anchors.fill: parent
                visible: root.currentPage === "hosts"
                opacity: root.hostsPageReveal
                controller: root.controller
                backgroundColor: Theme.workspaceBackground
                raisedColor: root.raisedColor
                borderColor: root.borderColor
                textColor: root.textColor
                mutedColor: root.mutedColor
                accentColor: root.accentColor
                onConnectionStarted: root.currentPage = "terminal"
                onSecuritySettingsRequested: root.openSecuritySettingsTab()
                onLocalTerminalRequested: root.startLocalTerminalTab()

                transform: Translate {
                    y: Theme.motionDistanceSmall * (1.0 - root.hostsPageReveal)
                }
            }

            SettingsPane {
                id: settingsPane

                anchors.fill: parent
                visible: root.currentPage === "settings"
                controller: root.controller
                diagnostics: root.diagnostics
                fontCatalog: root.fontCatalog
                onAppearancePreviewEnded: root.endWindowAppearancePreview()
                onAppearancePreviewRequested: (theme, opacity, backdrop, accent, customAccent) => {
                    root.previewWindowAppearance(theme, opacity, backdrop, accent, customAccent);
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

        background: Rectangle {
            radius: Theme.radiusPanel
            color: Theme.floatingBackground
            border.color: Theme.borderStrong
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
            terminalViewport.resolveMultilinePaste(true);
            root.pendingPasteLineCount = 0;
        }
        onRejected: {
            terminalViewport.resolveMultilinePaste(false);
            root.pendingPasteLineCount = 0;
        }
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
