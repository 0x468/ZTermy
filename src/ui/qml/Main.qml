pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var controller
    required property var fontCatalog
    required property var windowChrome
    readonly property int titleBarHeight: Theme.titleBarHeight
    readonly property int captionButtonWidth: 46
    readonly property int titleQuickActionWidth: 40
    readonly property int titleSecurityActionWidth: portableVaultNeedsAttention ? 40 : 0
    readonly property int titleNavigationWidth: Math.min(830, Math.max(310, width - (captionButtonWidth * 3) - titleQuickActionWidth - titleSecurityActionWidth - 96))
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
    readonly property bool portableVaultNeedsAttention: controller.effectiveCredentialStorage === "portable" && (!controller.portableVaultInitialized || controller.portableVaultLocked)

    color: root.currentPage === "terminal" ? "transparent" : backgroundColor

    function reportTitleBarMetrics() {
        root.windowChrome.setTitleBarMetrics(titleBarHeight, titleNavigation.width + 8, width - (captionButtonWidth * 3) - titleQuickActionWidth - titleSecurityActionWidth, width - (captionButtonWidth * 2), captionButtonWidth);
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
        Qt.callLater(root.presentStartupVaultPrompt);
    }
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

    Shortcut {
        sequence: "Ctrl+Shift+F"
        onActivated: root.openTerminalSearch()
    }

    Shortcut {
        sequence: "Ctrl+Shift+T"
        autoRepeat: false
        onActivated: root.startLocalTerminalTab()
    }

    Shortcut {
        sequence: "Ctrl+Shift+W"
        autoRepeat: false
        onActivated: root.closeActiveTerminalTab()
    }

    Shortcut {
        sequence: "Ctrl+Tab"
        onActivated: root.activateRelativeTerminalTab(1)
    }

    Shortcut {
        sequence: "Ctrl+Shift+Tab"
        onActivated: root.activateRelativeTerminalTab(-1)
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
                Qt.callLater(emptyTerminalPrimaryAction.forceActiveFocus);
            }
        }

        function onActiveTerminalTabChanged() {
            Qt.callLater(titleTerminalTabs.syncCurrentIndex);
        }

        function onCredentialVaultChanged() {
            Qt.callLater(root.reportTitleBarMetrics);
        }
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

                Rectangle {
                    anchors.centerIn: parent
                    width: 22
                    height: 22
                    radius: 6
                    color: root.accentColor

                    Text {
                        anchors.centerIn: parent
                        text: ">_"
                        color: Theme.accentText
                        font.family: Theme.terminalFont
                        font.pixelSize: Theme.textCompact
                        font.weight: Font.Bold
                    }
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
                width: count === 0 ? 0 : Math.min(Math.max(contentWidth, 126), Math.max(140, root.titleNavigationWidth - 174 - settingsTitleTab.width))
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
                    accessibleName: qsTr("New local terminal")
                    onActivated: root.startLocalTerminalTab()
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
                anchors.margins: 14
                spacing: 10

                Text {
                    text: "ZTERMY"
                    color: Theme.textSubtle
                    font.family: Theme.uiFont
                    font.pixelSize: 10
                    font.letterSpacing: 1.2
                    font.weight: Font.DemiBold
                }

                SideNavigationItem {
                    actionObjectName: "sideHostsAction"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 38
                    text: qsTr("Hosts")
                    selected: root.currentPage === "hosts"
                    onActivated: root.currentPage = "hosts"
                }

                Item {
                    Layout.fillHeight: true
                }

                Rectangle {
                    id: localMachineActionTile

                    Layout.fillWidth: true
                    Layout.preferredHeight: 58
                    radius: 8
                    color: localMachineAction.hovered ? Theme.controlHover : Theme.elevatedBackground
                    border.color: localMachineAction.activeFocus ? Theme.focus : root.borderColor

                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3

                        Text {
                            text: qsTr("Local machine")
                            color: root.textColor
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                        }

                        Text {
                            text: qsTr("Windows 11 · ready")
                            color: root.mutedColor
                            font.family: Theme.uiFont
                            font.pixelSize: 10
                        }
                    }

                    KeyboardAction {
                        id: localMachineAction

                        objectName: "localMachineAction"
                        anchors.fill: parent
                        anchors.margins: 2
                        accessibleName: qsTr("Open local terminal")
                        onActivated: root.startLocalTerminalTab()
                    }
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
                                Layout.maximumWidth: 220
                                text: root.activeTerminalTab ? root.activeTerminalTab.title : qsTr("Terminal")
                                color: root.textColor
                                elide: Text.ElideRight
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textCompact
                                font.weight: Font.DemiBold
                            }

                            Rectangle {
                                Layout.alignment: Qt.AlignVCenter
                                Layout.preferredWidth: 1
                                Layout.preferredHeight: 12
                                color: root.borderColor
                                visible: terminalSessionStatus.visible
                            }

                            Text {
                                id: terminalSessionStatus

                                Layout.alignment: Qt.AlignVCenter
                                Layout.fillWidth: true
                                visible: root.width >= 720
                                text: terminalViewport.statusText
                                color: root.mutedColor
                                elide: Text.ElideRight
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textCompact
                            }

                            Item {
                                Layout.fillWidth: !terminalSessionStatus.visible
                                visible: !terminalSessionStatus.visible
                            }

                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                text: "UTF-8"
                                color: Theme.textSubtle
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textCompact
                            }

                            Rectangle {
                                Layout.alignment: Qt.AlignVCenter
                                Layout.preferredWidth: 1
                                Layout.preferredHeight: 12
                                color: root.borderColor
                            }

                            Rectangle {
                                Layout.alignment: Qt.AlignVCenter
                                Layout.preferredWidth: 66
                                Layout.preferredHeight: 22
                                radius: Theme.radiusSmall
                                color: terminalFindAction.hovered || terminalFindAction.activeFocus ? Theme.controlHover : "transparent"
                                border.color: terminalFindAction.activeFocus ? Theme.focus : "transparent"
                                border.width: terminalFindAction.activeFocus ? 1 : 0

                                Behavior on color {
                                    ColorAnimation {
                                        duration: Theme.motionFast
                                    }
                                }

                                Row {
                                    anchors.centerIn: parent
                                    spacing: 5

                                    AppIcon {
                                        width: 13
                                        height: 13
                                        name: "search"
                                        color: root.mutedColor
                                    }

                                    Text {
                                        text: qsTr("Find")
                                        color: root.mutedColor
                                        font.family: Theme.uiFont
                                        font.pixelSize: Theme.textCompact
                                    }
                                }

                                KeyboardAction {
                                    id: terminalFindAction

                                    objectName: "terminalFindAction"
                                    anchors.fill: parent
                                    accessibleName: qsTr("Find in terminal")
                                    onActivated: root.openTerminalSearch()
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "transparent"

                        TerminalView {
                            id: terminalViewport
                            objectName: "terminalViewport"
                            anchors.fill: parent
                            focus: true
                            fontFamily: root.controller.terminalFontFamily
                            fontPixelSize: root.controller.terminalFontSize
                            ligaturesEnabled: root.controller.terminalLigatures
                            backgroundOpacity: root.controller.terminalBackgroundOpacity
                            cursorPreference: root.controller.cursorPreference
                            cursorBlink: root.controller.cursorBlink
                            copyOnSelect: root.controller.copyOnSelect
                            confirmMultilinePaste: root.controller.confirmMultilinePaste

                            Component.onCompleted: forceActiveFocus()
                            onMultilinePasteConfirmationRequested: lineCount => {
                                root.pendingPasteLineCount = lineCount;
                                multilinePasteDialog.openFrom(terminalViewport);
                            }
                        }

                        Item {
                            id: terminalScrollbar

                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.topMargin: 8
                            anchors.rightMargin: 9
                            anchors.bottomMargin: 8
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

                transform: Translate {
                    y: Theme.motionDistanceSmall * (1.0 - root.hostsPageReveal)
                }
            }

            SettingsPane {
                id: settingsPane

                anchors.fill: parent
                visible: root.currentPage === "settings"
                controller: root.controller
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
