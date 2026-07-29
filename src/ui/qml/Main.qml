pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ztermy.Terminal 1.0

Rectangle {
    id: root

    readonly property int titleBarHeight: 42
    readonly property int captionButtonWidth: 46
    readonly property int titleNavigationWidth: Math.min(790, Math.max(310, width - (captionButtonWidth * 3) - 96))
    readonly property color backgroundColor: Theme.windowBackground
    readonly property color panelColor: Theme.panelBackground
    readonly property color raisedColor: Theme.raisedBackground
    readonly property color borderColor: Theme.border
    readonly property color textColor: Theme.text
    readonly property color mutedColor: Theme.textMuted
    readonly property color accentColor: Theme.accent
    readonly property var controller: appController
    property string currentPage: "terminal"
    property bool terminalSearchVisible: false
    property int pendingPasteLineCount: 0
    readonly property var activeTerminalTab: {
        for (const tab of controller.terminalTabs) {
            if (tab.id === controller.activeTerminalTabId) {
                return tab;
            }
        }
        return null;
    }
    readonly property bool activeSshFailure: activeTerminalTab !== null
                                                     && activeTerminalTab.kind === "ssh"
                                                     && !activeTerminalTab.running
                                                     && activeTerminalTab.status.length > 0

    color: backgroundColor

    function reportTitleBarMetrics() {
        windowChrome.setTitleBarMetrics(titleBarHeight, titleNavigation.width + 8,
                                        width - (captionButtonWidth * 3),
                                        width - (captionButtonWidth * 2), captionButtonWidth);
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
        windowChrome.applyAppearance(controller.windowOpacity, controller.backdropPreference, Theme.dark);
    }

    Binding {
        target: Theme
        property: "preference"
        value: root.controller.themePreference
    }

    Binding {
        target: Theme
        property: "systemDark"
        value: windowChrome.systemDarkMode
    }

    Component.onCompleted: {
        reportTitleBarMetrics();
        applyWindowAppearance();
    }
    onWidthChanged: reportTitleBarMetrics()
    onCurrentPageChanged: {
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
        sequence: StandardKey.Find
        onActivated: root.openTerminalSearch()
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
    }

    Connections {
        target: windowChrome

        function onSystemDarkModeChanged() {
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
                color: root.currentPage === "hosts" ? Theme.controlHover : "transparent"

                Row {
                    anchors.centerIn: parent
                    spacing: 8

                    Text {
                        text: "□"
                        color: root.currentPage === "hosts" ? root.textColor : root.mutedColor
                        font.family: Theme.uiFont
                        font.pixelSize: 14
                    }

                    Text {
                        text: "Hosts"
                        color: root.currentPage === "hosts" ? root.textColor : root.mutedColor
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textBody
                        font.weight: root.currentPage === "hosts" ? Font.DemiBold : Font.Normal
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.currentPage = "hosts"
                    Accessible.role: Accessible.Button
                    Accessible.name: "Hosts"
                }
            }

            ListView {
                id: titleTerminalTabs

                width: Math.min(Math.max(contentWidth, 126), Math.max(140, root.titleNavigationWidth - 174))
                height: titleNavigation.height
                orientation: ListView.Horizontal
                spacing: 2
                clip: true
                model: root.controller.terminalTabs

                delegate: Rectangle {
                    id: titleTerminalTab

                    required property var modelData

                    width: Math.min(190, Math.max(126, titleTabText.implicitWidth + 54))
                    height: titleTerminalTabs.height
                    color: root.currentPage === "terminal"
                           && root.controller.activeTerminalTabId === modelData.id
                           ? Theme.controlHover : "transparent"

                    Rectangle {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 6
                        height: 6
                        radius: 3
                        color: titleTerminalTab.modelData.running ? root.accentColor : Theme.textSubtle
                    }

                    Text {
                        id: titleTabText

                        anchors.left: parent.left
                        anchors.leftMargin: 24
                        anchors.right: titleTabClose.left
                        anchors.rightMargin: 3
                        anchors.verticalCenter: parent.verticalCenter
                        text: titleTerminalTab.modelData.title
                        color: root.textColor
                        elide: Text.ElideRight
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }

                    MouseArea {
                        anchors.left: parent.left
                        anchors.right: titleTabClose.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.controller.activateTerminalTab(titleTerminalTab.modelData.id);
                            root.currentPage = "terminal";
                            terminalViewport.forceActiveFocus();
                        }
                        Accessible.role: Accessible.Button
                        Accessible.name: "Activate " + titleTerminalTab.modelData.title
                    }

                    Rectangle {
                        id: titleTabClose

                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        width: 24
                        height: 24
                        radius: 5
                        color: titleTabCloseMouse.containsMouse ? Theme.borderStrong : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: "×"
                            color: root.mutedColor
                            font.pixelSize: 15
                        }

                        MouseArea {
                            id: titleTabCloseMouse

                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.controller.closeTerminalTab(titleTerminalTab.modelData.id)
                            Accessible.role: Accessible.Button
                            Accessible.name: "Close " + titleTerminalTab.modelData.title
                        }
                    }
                }
            }

            Rectangle {
                width: 36
                height: titleNavigation.height
                color: titleNewTabMouse.containsMouse ? Theme.controlHover : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "+"
                    color: root.textColor
                    font.pixelSize: 18
                }

                MouseArea {
                    id: titleNewTabMouse

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.controller.startLocalTerminal();
                        root.currentPage = "terminal";
                        terminalViewport.forceActiveFocus();
                    }
                    Accessible.role: Accessible.Button
                    Accessible.name: "New local terminal"
                }
            }
        }

        Row {
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height

            CaptionButton {
                width: root.captionButtonWidth
                height: titleBar.height
                kind: "minimize"
                accessibleName: "Minimize"
                onActivated: windowChrome.minimizeWindow()
            }

            CaptionButton {
                width: root.captionButtonWidth
                height: titleBar.height
                kind: "maximize"
                accessibleName: windowChrome.maximized ? "Restore" : "Maximize"
                externallyHovered: windowChrome.maximizeButtonHovered
                externallyPressed: windowChrome.maximizeButtonPressed
                onActivated: windowChrome.toggleMaximize()
            }

            CaptionButton {
                width: root.captionButtonWidth
                height: titleBar.height
                kind: "close"
                accessibleName: "Close"
                onActivated: windowChrome.closeWindow()
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
            Layout.preferredWidth: visible ? 210 : 0
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

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 38
                    radius: 7
                    color: root.currentPage === "hosts" ? root.raisedColor : "transparent"

                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10

                        Rectangle {
                            width: 3
                            height: 16
                            radius: 2
                            color: root.currentPage === "hosts" ? root.accentColor : "transparent"
                        }

                        Text {
                            text: "Hosts"
                            color: root.currentPage === "hosts" ? root.textColor : root.mutedColor
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textBody
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.currentPage = "hosts"
                        Accessible.role: Accessible.Button
                        Accessible.name: "Hosts"
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 38
                    radius: 7
                    color: root.currentPage === "settings" ? root.raisedColor : "transparent"

                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10

                        Rectangle {
                            width: 3
                            height: 16
                            radius: 2
                            color: root.currentPage === "settings" ? root.accentColor : "transparent"
                        }

                        Text {
                            text: "Settings"
                            color: root.currentPage === "settings" ? root.textColor : root.mutedColor
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textBody
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.currentPage = "settings"
                        Accessible.role: Accessible.Button
                        Accessible.name: "Settings"
                    }
                }

                Item {
                    Layout.fillHeight: true
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 58
                    radius: 8
                    color: Theme.elevatedBackground
                    border.color: root.borderColor

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

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.controller.startLocalTerminal();
                            root.currentPage = "terminal";
                        }
                        Accessible.role: Accessible.Button
                        Accessible.name: "Open local terminal"
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
                color: Theme.contentBackground
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
                            text: "UTF-8   Ctrl+F  Find"
                            color: Theme.textSubtle
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textCompact
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: Theme.workspaceBackground

                        TerminalView {
                            id: terminalViewport
                            objectName: "terminalViewport"
                            anchors.fill: parent
                            focus: true
                            fontFamily: root.controller.terminalFontFamily
                            fontPixelSize: root.controller.terminalFontSize
                            cursorPreference: root.controller.cursorPreference
                            cursorBlink: root.controller.cursorBlink
                            copyOnSelect: root.controller.copyOnSelect
                            confirmMultilinePaste: root.controller.confirmMultilinePaste

                            Component.onCompleted: forceActiveFocus()
                            onMultilinePasteConfirmationRequested: lineCount => {
                                root.pendingPasteLineCount = lineCount;
                                multilinePasteDialog.open();
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

                                TextField {
                                    id: searchField

                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 30
                                    color: root.textColor
                                    placeholderText: "Find in terminal"
                                    placeholderTextColor: root.mutedColor
                                    selectByMouse: true
                                    font.family: "Segoe UI Variable"
                                    font.pixelSize: 12

                                    background: Rectangle {
                                        radius: 5
                                        color: Theme.fieldBackground
                                        border.color: searchField.activeFocus ? root.accentColor : root.borderColor
                                    }

                                    onTextEdited: searchDelay.restart()
                                    Keys.onPressed: event => {
                                        if (event.key === Qt.Key_Escape) {
                                            root.closeTerminalSearch();
                                            event.accepted = true;
                                        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                            searchDelay.stop();
                                            root.controller.searchTerminal(text, (event.modifiers & Qt.ShiftModifier) !== 0,
                                                                           caseSensitiveButton.checked);
                                            event.accepted = true;
                                        }
                                    }
                                    Accessible.name: "Terminal search query"
                                }

                                Text {
                                    Layout.preferredWidth: 46
                                    horizontalAlignment: Text.AlignHCenter
                                    text: root.controller.terminalSearchTotal > 0
                                          ? root.controller.terminalSearchCurrent + "/" + root.controller.terminalSearchTotal
                                          : "0/0"
                                    color: root.mutedColor
                                    font.family: "Cascadia Mono"
                                    font.pixelSize: 10
                                }

                                ToolButton {
                                    id: caseSensitiveButton

                                    Layout.preferredWidth: 30
                                    Layout.preferredHeight: 30
                                    checkable: true
                                    text: "Aa"
                                    checked: root.controller.terminalSearchCaseSensitive
                                    onClicked: {
                                        searchDelay.stop();
                                        root.controller.searchTerminal(searchField.text, false, checked);
                                    }
                                    Accessible.name: "Match case"
                                }

                                ToolButton {
                                    Layout.preferredWidth: 30
                                    Layout.preferredHeight: 30
                                    text: "↑"
                                    onClicked: root.controller.searchTerminal(searchField.text, true,
                                                                              caseSensitiveButton.checked)
                                    Accessible.name: "Previous match"
                                }

                                ToolButton {
                                    Layout.preferredWidth: 30
                                    Layout.preferredHeight: 30
                                    text: "↓"
                                    onClicked: root.controller.searchTerminal(searchField.text, false,
                                                                              caseSensitiveButton.checked)
                                    Accessible.name: "Next match"
                                }

                                ToolButton {
                                    Layout.preferredWidth: 30
                                    Layout.preferredHeight: 30
                                    text: "×"
                                    onClicked: root.closeTerminalSearch()
                                    Accessible.name: "Close terminal search"
                                }
                            }
                        }

                        Rectangle {
                            anchors.centerIn: parent
                            width: Math.min(520, parent.width - 48)
                            implicitHeight: sshFailureLayout.implicitHeight + 36
                            visible: root.activeSshFailure
                            z: 9
                            radius: Theme.radiusPanel
                            color: Theme.floatingBackground
                            border.color: Theme.danger

                            ColumnLayout {
                                id: sshFailureLayout

                                anchors.fill: parent
                                anchors.margins: 18
                                spacing: 10

                                Text {
                                    text: "SSH session unavailable"
                                    color: Theme.text
                                    font.family: Theme.uiFont
                                    font.pixelSize: 17
                                    font.weight: Font.DemiBold
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: root.activeTerminalTab ? root.activeTerminalTab.status : ""
                                    color: Theme.textSoft
                                    wrapMode: Text.WordWrap
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textBody
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: "Review the host and authentication settings before starting a new connection. Credentials are not retained for retry."
                                    color: Theme.textMuted
                                    wrapMode: Text.WordWrap
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textLabel
                                }

                                RowLayout {
                                    Layout.fillWidth: true

                                    Item {
                                        Layout.fillWidth: true
                                    }

                                    Button {
                                        text: "Close tab"
                                        Accessible.name: "Close failed SSH terminal tab"
                                        onClicked: {
                                            if (root.activeTerminalTab) {
                                                root.controller.closeTerminalTab(root.activeTerminalTab.id);
                                            }
                                        }
                                    }

                                    Button {
                                        text: "Review host"
                                        Accessible.name: "Return to SSH host profiles"
                                        palette.button: Theme.accent
                                        palette.buttonText: Theme.accentText
                                        font.weight: Font.DemiBold
                                        onClicked: root.currentPage = "hosts"
                                    }
                                }
                            }
                        }

                        Timer {
                            id: searchDelay

                            interval: 250
                            repeat: false
                            onTriggered: root.controller.searchTerminal(searchField.text, false,
                                                                        caseSensitiveButton.checked)
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
            }
        }
    }

    Dialog {
        id: multilinePasteDialog

        anchors.centerIn: parent
        modal: true
        closePolicy: Popup.CloseOnEscape
        padding: 20
        onClosed: {
            if (result !== Dialog.Accepted) {
                terminalViewport.resolveMultilinePaste(false);
            }
            root.pendingPasteLineCount = 0;
            terminalViewport.forceActiveFocus();
        }

        background: Rectangle {
            radius: Theme.radiusPanel
            color: Theme.elevatedBackground
            border.color: Theme.borderStrong
        }

        contentItem: ColumnLayout {
            spacing: 14

            Text {
                text: "Paste multiple lines?"
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }

            Text {
                Layout.preferredWidth: 380
                text: "The clipboard contains " + root.pendingPasteLineCount + " lines. Pasting may execute commands immediately in the active terminal."
                color: Theme.textMuted
                wrapMode: Text.WordWrap
                font.family: Theme.uiFont
                font.pixelSize: Theme.textBody
            }

            RowLayout {
                Layout.fillWidth: true

                Item {
                    Layout.fillWidth: true
                }

                Button {
                    text: "Cancel"
                    onClicked: multilinePasteDialog.reject()
                }

                Button {
                    text: "Paste"
                    palette.button: Theme.accent
                    palette.buttonText: Theme.accentText
                    font.weight: Font.DemiBold
                    onClicked: {
                        terminalViewport.resolveMultilinePaste(true);
                        multilinePasteDialog.accept();
                    }
                }
            }
        }
    }

    HostKeyPrompt {
        anchors.fill: parent
        z: 100
        panelColor: root.raisedColor
        borderColor: root.borderColor
        textColor: root.textColor
        mutedColor: Theme.textSoft
        accentColor: root.accentColor
    }
}
