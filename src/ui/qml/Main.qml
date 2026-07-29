pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ztermy.Terminal 1.0

Rectangle {
    id: root

    readonly property int titleBarHeight: 42
    readonly property int captionButtonWidth: 46
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

    color: backgroundColor

    function reportTitleBarMetrics() {
        windowChrome.setTitleBarMetrics(titleBarHeight, width - (captionButtonWidth * 3), width - (captionButtonWidth * 2), captionButtonWidth);
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

    Component.onCompleted: reportTitleBarMetrics()
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
    }

    Rectangle {
        id: titleBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.titleBarHeight
        color: "#E60F1722"

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: root.borderColor
        }

        RowLayout {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 9

            Rectangle {
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                radius: 5
                color: root.accentColor

                Text {
                    anchors.centerIn: parent
                    text: ">_"
                    color: "#052E16"
                    font.family: "Cascadia Mono"
                    font.pixelSize: 9
                    font.weight: Font.Bold
                }
            }

            Text {
                text: "ztermy"
                color: root.textColor
                font.family: "Segoe UI Variable"
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }

            Rectangle {
                Layout.leftMargin: 12
                Layout.preferredWidth: 1
                Layout.preferredHeight: 16
                color: root.borderColor
            }

            RowLayout {
                Layout.leftMargin: 4
                spacing: 7

                Rectangle {
                    Layout.preferredWidth: 7
                    Layout.preferredHeight: 7
                    radius: 4
                    color: root.accentColor
                }

                Text {
                    text: root.controller.sshActive ? "SSH terminal" : "Local terminal"
                    color: root.mutedColor
                    font.family: "Segoe UI Variable"
                    font.pixelSize: 12
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
        anchors.bottom: statusBar.top
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 220
            color: root.panelColor

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                Text {
                    text: "WORKSPACE"
                    color: "#64748B"
                    font.family: "Segoe UI Variable"
                    font.pixelSize: 10
                    font.letterSpacing: 1.2
                    font.weight: Font.DemiBold
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 38
                    radius: 7
                    color: root.currentPage === "terminal" ? root.raisedColor : "transparent"

                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10

                        Rectangle {
                            width: 3
                            height: 16
                            radius: 2
                            color: root.accentColor
                        }

                        Text {
                            text: "Terminal"
                            color: root.textColor
                            font.family: "Segoe UI Variable"
                            font.pixelSize: 13
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.currentPage = "terminal"
                        Accessible.role: Accessible.Button
                        Accessible.name: "Terminal"
                    }
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
                            font.family: "Segoe UI Variable"
                            font.pixelSize: 13
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

                Text {
                    Layout.leftMargin: 15
                    text: "Settings"
                    color: root.mutedColor
                    font.family: "Segoe UI Variable"
                    font.pixelSize: 13
                }

                Item {
                    Layout.fillHeight: true
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 58
                    radius: 8
                    color: "#141E2B"
                    border.color: root.borderColor

                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3

                        Text {
                            text: "Local machine"
                            color: root.textColor
                            font.family: "Segoe UI Variable"
                            font.pixelSize: 12
                        }

                        Text {
                            text: "Windows 11 · ready"
                            color: root.mutedColor
                            font.family: "Segoe UI Variable"
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
                color: "#E60A0E14"
                visible: root.currentPage === "terminal"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 8

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        radius: 8
                        color: "#D9141E2B"
                        border.color: root.borderColor

                        ListView {
                            id: terminalTabList

                            anchors.left: parent.left
                            anchors.right: newTabButton.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            orientation: ListView.Horizontal
                            spacing: 4
                            clip: true
                            model: root.controller.terminalTabs

                            delegate: Rectangle {
                                id: terminalTab

                                required property var modelData

                                width: Math.min(210, Math.max(126, tabTitle.implicitWidth + 52))
                                height: terminalTabList.height - 8
                                y: 4
                                radius: 6
                                color: root.controller.activeTerminalTabId === modelData.id ? root.raisedColor : "transparent"

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 6
                                    height: 6
                                    radius: 3
                                    color: terminalTab.modelData.running ? root.accentColor : "#64748B"
                                }

                                Text {
                                    id: tabTitle

                                    anchors.left: parent.left
                                    anchors.leftMargin: 13
                                    anchors.right: closeTabButton.left
                                    anchors.rightMargin: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: terminalTab.modelData.title
                                    color: root.textColor
                                    elide: Text.ElideRight
                                    font.family: "Segoe UI Variable"
                                    font.pixelSize: 11
                                }

                                MouseArea {
                                    anchors.left: parent.left
                                    anchors.right: closeTabButton.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        root.controller.activateTerminalTab(terminalTab.modelData.id);
                                        terminalViewport.forceActiveFocus();
                                    }
                                    Accessible.role: Accessible.Button
                                    Accessible.name: "Activate " + terminalTab.modelData.title
                                }

                                Rectangle {
                                    id: closeTabButton

                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 24
                                    height: 24
                                    radius: 5
                                    color: closeTabMouse.containsMouse ? "#334155" : "transparent"

                                    Text {
                                        anchors.centerIn: parent
                                        text: "×"
                                        color: root.mutedColor
                                        font.pixelSize: 15
                                    }

                                    MouseArea {
                                        id: closeTabMouse

                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.controller.closeTerminalTab(terminalTab.modelData.id)
                                        Accessible.role: Accessible.Button
                                        Accessible.name: "Close " + terminalTab.modelData.title
                                    }
                                }
                            }
                        }

                        Rectangle {
                            id: newTabButton

                            anchors.right: parent.right
                            anchors.rightMargin: 5
                            anchors.verticalCenter: parent.verticalCenter
                            width: 28
                            height: 26
                            radius: 6
                            color: newTabMouse.containsMouse ? root.raisedColor : "transparent"

                            Text {
                                anchors.centerIn: parent
                                text: "+"
                                color: root.textColor
                                font.pixelSize: 18
                            }

                            MouseArea {
                                id: newTabMouse

                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.controller.startLocalTerminal();
                                    terminalViewport.forceActiveFocus();
                                }
                                Accessible.role: Accessible.Button
                                Accessible.name: "New local terminal"
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 9
                        color: "#D90B1017"
                        border.color: root.borderColor

                        TerminalView {
                            id: terminalViewport
                            objectName: "terminalViewport"
                            anchors.fill: parent
                            focus: true

                            Component.onCompleted: forceActiveFocus()
                        }

                        Rectangle {
                            id: searchPanel

                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: 12
                            width: 420
                            height: 42
                            radius: 8
                            color: "#F21E293B"
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
                                        color: "#111827"
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
        }
    }

    Rectangle {
        id: statusBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 27
        color: "#E60F1722"

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: root.borderColor
        }

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 7

            Rectangle {
                width: 6
                height: 6
                radius: 3
                color: root.accentColor
            }

            Text {
                text: terminalViewport.statusText
                color: root.mutedColor
                font.family: "Segoe UI Variable"
                font.pixelSize: 10
            }
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
        text: "UTF-8   C++23   Qt 6.8"
        color: Theme.textSubtle
        font.family: Theme.terminalFont
        font.pixelSize: Theme.textCompact
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
