pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var controller
    required property var windowChrome
    readonly property int titleBarHeight: 42
    readonly property int captionButtonWidth: 46
    readonly property int titleNavigationWidth: Math.min(790, Math.max(310, width - (captionButtonWidth * 3) - 96))
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
    property string currentPage: "terminal"
    property bool terminalSearchVisible: false
    property int pendingPasteLineCount: 0
    property bool appearancePreviewActive: false
    property string previewThemePreference: "dark"
    property string previewBackdropPreference: "acrylic"
    property real previewBackdropOpacity: 1.0
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

    color: root.currentPage === "terminal" ? "transparent" : backgroundColor

    function reportTitleBarMetrics() {
        root.windowChrome.setTitleBarMetrics(titleBarHeight, titleNavigation.width + 8, width - (captionButtonWidth * 3), width - (captionButtonWidth * 2), captionButtonWidth);
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

    function previewWindowAppearance(theme, opacity, backdrop) {
        previewThemePreference = theme;
        previewBackdropPreference = backdrop;
        previewBackdropOpacity = opacity;
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

    Component.onCompleted: {
        reportTitleBarMetrics();
        applyWindowAppearance();
    }
    onWidthChanged: reportTitleBarMetrics()
    onCurrentPageChanged: {
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
            if (root.currentPage === "terminal" && root.controller.terminalTabs.length === 0) {
                Qt.callLater(emptyTerminalPrimaryAction.forceActiveFocus);
            }
        }
    }

    Connections {
        target: root.windowChrome

        function onSystemDarkModeChanged() {
            if (root.appearancePreviewActive) {
                Qt.callLater(() => root.previewWindowAppearance(root.previewThemePreference, root.previewBackdropOpacity, root.previewBackdropPreference));
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
                color: root.currentPage === "hosts" || hostsTitleAction.hovered || hostsTitleAction.activeFocus ? Theme.controlHover : "transparent"
                border.color: hostsTitleAction.activeFocus ? Theme.focus : "transparent"
                border.width: hostsTitleAction.activeFocus ? 1 : 0

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
                        text: "Hosts"
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
                    accessibleName: "Hosts"
                    onActivated: root.currentPage = "hosts"
                }
            }

            ListView {
                id: titleTerminalTabs

                objectName: "titleTerminalTabs"
                width: count === 0 ? 0 : Math.min(Math.max(contentWidth, 126), Math.max(140, root.titleNavigationWidth - 174))
                height: titleNavigation.height
                orientation: ListView.Horizontal
                spacing: 2
                clip: true
                model: root.controller.terminalTabs

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
                    accessibleName: "New local terminal"
                    onActivated: root.startLocalTerminalTab()
                }
            }
        }

        Row {
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height

            CaptionButton {
                objectName: "minimizeCaptionButton"
                width: root.captionButtonWidth
                height: titleBar.height
                kind: "minimize"
                chrome: root.windowChrome
                accessibleName: "Minimize"
                onActivated: root.windowChrome.minimizeWindow()
            }

            CaptionButton {
                objectName: "maximizeCaptionButton"
                width: root.captionButtonWidth
                height: titleBar.height
                kind: "maximize"
                chrome: root.windowChrome
                accessibleName: root.windowChrome.maximized ? "Restore" : "Maximize"
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
                accessibleName: "Close"
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
            visible: root.currentPage === "hosts" || root.currentPage === "settings"
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
                    text: "Hosts"
                    selected: root.currentPage === "hosts"
                    onActivated: root.currentPage = "hosts"
                }

                SideNavigationItem {
                    actionObjectName: "sideSettingsAction"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 38
                    text: "Settings"
                    selected: root.currentPage === "settings"
                    onActivated: root.currentPage = "settings"
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
                            text: "Local machine"
                            color: root.textColor
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                        }

                        Text {
                            text: "Windows 11 · ready"
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
                        accessibleName: "Open local terminal"
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
                        Layout.preferredHeight: 27
                        color: Theme.workspaceBackground

                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 1
                            color: root.borderColor
                        }

                        Row {
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 7

                            Rectangle {
                                width: 6
                                height: 6
                                radius: 3
                                color: root.controller.sshActive ? root.accentColor : Theme.textSubtle
                            }

                            Text {
                                text: terminalViewport.statusText
                                color: root.mutedColor
                                font.family: Theme.terminalFont
                                font.pixelSize: Theme.textCompact
                            }
                        }

                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            text: "UTF-8   Ctrl+Shift+F  Find"
                            color: Theme.textSubtle
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textCompact
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
                                    placeholderText: "Find in terminal"
                                    accessibleName: "Terminal search query"

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
                                    Accessible.name: "Match case"
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
                                    Accessible.name: "Previous match"
                                }

                                ToolButton {
                                    Layout.preferredWidth: 30
                                    Layout.preferredHeight: 30
                                    contentItem: AppIcon {
                                        name: "chevron-down"
                                        color: root.textColor
                                    }
                                    onClicked: root.controller.searchTerminal(searchField.text, false, caseSensitiveButton.checked)
                                    Accessible.name: "Next match"
                                }

                                ToolButton {
                                    Layout.preferredWidth: 30
                                    Layout.preferredHeight: 30
                                    contentItem: AppIcon {
                                        name: "close"
                                        color: root.textColor
                                    }
                                    onClicked: root.closeTerminalSearch()
                                    Accessible.name: "Close terminal search"
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
                            heading: "No terminal sessions"
                            description: "Open a local PowerShell session or choose an SSH host from the Hosts workspace."

                            ActionButton {
                                id: emptyTerminalPrimaryAction

                                text: "New terminal"
                                accessibleName: "Open a new local terminal"
                                variant: "primary"
                                onClicked: root.startLocalTerminalTab()
                            }

                            ActionButton {
                                text: "Browse hosts"
                                accessibleName: "Browse saved SSH hosts"
                                onClicked: root.currentPage = "hosts"
                            }
                        }

                        StatePanel {
                            anchors.centerIn: parent
                            width: Math.min(520, parent.width - 48)
                            visible: root.activeSshConnecting
                            z: 9
                            kind: "loading"
                            heading: "Connecting to SSH host"
                            description: root.activeTerminalTab ? root.activeTerminalTab.status : ""
                            detail: "Connection setup runs outside the interface thread. You can close this tab to cancel."

                            ActionButton {
                                text: "Cancel connection"
                                Accessible.name: "Cancel SSH connection and close tab"
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
                            heading: "SSH session ended"
                            description: root.activeTerminalTab ? root.activeTerminalTab.status : ""
                            detail: "The remote host closed the terminal connection. Credentials are not retained for automatic reconnection."

                            ActionButton {
                                text: "Close tab"
                                accessibleName: "Close ended SSH terminal tab"
                                onClicked: {
                                    if (root.activeTerminalTab) {
                                        root.controller.closeTerminalTab(root.activeTerminalTab.id);
                                    }
                                }
                            }

                            ActionButton {
                                text: "Review host"
                                accessibleName: "Return to SSH host profiles"
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
                            heading: "SSH session unavailable"
                            description: root.activeTerminalTab ? root.activeTerminalTab.status : ""
                            detail: "Review the host and authentication settings before starting a new connection. Credentials are not retained for retry."

                            ActionButton {
                                text: "Close tab"
                                Accessible.name: "Close failed SSH terminal tab"
                                onClicked: {
                                    if (root.activeTerminalTab) {
                                        root.controller.closeTerminalTab(root.activeTerminalTab.id);
                                    }
                                }
                            }

                            ActionButton {
                                text: "Review host"
                                Accessible.name: "Return to SSH host profiles"
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
                controller: root.controller
                backgroundColor: Theme.workspaceBackground
                raisedColor: root.raisedColor
                borderColor: root.borderColor
                textColor: root.textColor
                mutedColor: root.mutedColor
                accentColor: root.accentColor
                onConnectionStarted: root.currentPage = "terminal"
            }

            SettingsPane {
                anchors.fill: parent
                visible: root.currentPage === "settings"
                controller: root.controller
                onAppearancePreviewEnded: root.endWindowAppearancePreview()
                onAppearancePreviewRequested: (theme, opacity, backdrop) => {
                    root.previewWindowAppearance(theme, opacity, backdrop);
                }
            }
        }
    }

    ConfirmationDialog {
        id: multilinePasteDialog

        heading: "Paste multiple lines?"
        description: "The clipboard contains " + root.pendingPasteLineCount + " lines. Pasting may execute commands immediately in the active terminal."
        acceptText: "Paste"
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
