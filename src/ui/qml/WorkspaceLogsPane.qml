pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    objectName: "workspaceLogsPane"
    required property var controller
    property var activeTab: null
    property var selectedEntry: null
    readonly property var history: controller.connectionHistory
    readonly property bool compactLayout: width < 760
    signal openTerminalRequested(string tabId)
    signal toggleActiveLogRequested

    color: Theme.workspaceBackground

    function statusLabel(status) {
        if (status === "connected")
            return qsTr("Connected");
        if (status === "connecting")
            return qsTr("Connecting");
        if (status === "failed")
            return qsTr("Failed");
        if (status === "interrupted")
            return qsTr("Interrupted");
        return qsTr("Disconnected");
    }

    function statusColor(status) {
        if (status === "connected")
            return Theme.success;
        if (status === "connecting")
            return Theme.warning;
        if (status === "failed")
            return Theme.danger;
        return Theme.textMuted;
    }

    function dateText(timestamp) {
        return Qt.formatDateTime(new Date(Number(timestamp)), "yyyy-MM-dd");
    }

    function timeRange(entry) {
        const start = Qt.formatTime(new Date(Number(entry.startedUtcMs)), "HH:mm");
        return entry.endedUtcMs > 0 ? start + " – " + Qt.formatTime(new Date(Number(entry.endedUtcMs)), "HH:mm") : start + " – " + qsTr("Ongoing");
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3

                Text {
                    text: qsTr("Connection history")
                    color: Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: 22
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Review connection sessions and diagnostics. Raw terminal content is recorded only when you explicitly start logging.")
                    color: Theme.textMuted
                    wrapMode: Text.Wrap
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textLabel
                }
            }
            ActionButton {
                visible: root.controller.activeTerminalTabId.length > 0
                text: root.activeTab !== null && (root.activeTab.logState === "active" || root.activeTab.logState === "starting") ? qsTr("Stop raw log") : qsTr("Start raw log")
                iconName: "save"
                onClicked: root.toggleActiveLogRequested()
            }
            ActionButton {
                text: qsTr("Clear finished")
                iconName: "trash"
                onClicked: clearDialog.openFrom(this)
            }
        }

        Flow {
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            spacing: 8

            AppTextField {
                id: searchField
                width: root.compactLayout ? root.width - 40 : 280
                accessibleName: qsTr("Search connection history")
                placeholderText: qsTr("Search host, user, device, or status")
                onTextEdited: root.history.setFilter(text, dateField.text, hostField.text, savedOnly.checked)
            }
            AppTextField {
                id: dateField
                width: root.compactLayout ? Math.max(120, (root.width - 48) / 2) : 145
                accessibleName: qsTr("Connection date filter")
                placeholderText: qsTr("YYYY-MM-DD")
                onTextEdited: root.history.setFilter(searchField.text, text, hostField.text, savedOnly.checked)
            }
            AppTextField {
                id: hostField
                width: root.compactLayout ? Math.max(120, (root.width - 48) / 2) : 210
                accessibleName: qsTr("Connection host filter")
                placeholderText: qsTr("Exact host")
                onTextEdited: root.history.setFilter(searchField.text, dateField.text, text, savedOnly.checked)
            }
            AppSwitch {
                id: savedOnly
                text: qsTr("Saved only")
                accessibleName: text
                onToggled: root.history.setFilter(searchField.text, dateField.text, hostField.text, checked)
            }
        }

        StatusMessage {
            Layout.fillWidth: true
            kind: "error"
            text: root.history.error
        }
        StatePanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.history.entries.length === 0
            centered: true
            heading: qsTr("No matching connection sessions")
            description: root.height < 440 ? "" : qsTr("New local and SSH sessions appear here automatically. Use the filters above to narrow the list.")
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            visible: !root.compactLayout && root.history.entries.length > 0
            color: Theme.elevatedBackground
            radius: Theme.radiusControl

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 14
                Text {
                    Layout.preferredWidth: 125
                    text: qsTr("Date")
                    color: Theme.textMuted
                }
                Text {
                    Layout.preferredWidth: 210
                    text: qsTr("Local user")
                    color: Theme.textMuted
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Target")
                    color: Theme.textMuted
                }
                Text {
                    Layout.preferredWidth: 95
                    text: qsTr("Status")
                    color: Theme.textMuted
                }
                Item {
                    Layout.preferredWidth: 76
                }
            }
        }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.history.entries.length > 0
            clip: true
            spacing: 2
            model: root.history.entries
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            delegate: Rectangle {
                id: row
                required property var modelData
                width: ListView.view.width
                height: root.compactLayout ? 104 : 66
                radius: Theme.radiusControl
                color: hover.hovered ? Theme.controlHover : Theme.raisedBackground
                border.color: Theme.border
                HoverHandler {
                    id: hover
                }
                TapHandler {
                    onTapped: root.selectedEntry = row.modelData
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 12
                    ColumnLayout {
                        Layout.preferredWidth: root.compactLayout ? 92 : 115
                        Text {
                            text: root.dateText(row.modelData.startedUtcMs)
                            color: Theme.text
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: root.timeRange(row.modelData)
                            color: Theme.textMuted
                            font.pixelSize: Theme.textLabel
                        }
                    }
                    ColumnLayout {
                        visible: !root.compactLayout
                        Layout.preferredWidth: 198
                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.localUsername
                            color: Theme.text
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.localHostname
                            color: Theme.textMuted
                            elide: Text.ElideRight
                            font.pixelSize: Theme.textLabel
                        }
                    }
                    Rectangle {
                        Layout.preferredWidth: 38
                        Layout.preferredHeight: 38
                        radius: Theme.radiusControl
                        color: Theme.selectedBackground
                        AppIcon {
                            anchors.centerIn: parent
                            width: 18
                            height: 18
                            name: row.modelData.protocol === "local" ? "terminal" : "hosts"
                            color: Theme.accent
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.hostLabel
                            color: Theme.text
                            elide: Text.ElideRight
                            font.weight: Font.DemiBold
                        }
                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.protocol === "local" ? "local" : row.modelData.protocol + " · " + row.modelData.username + "@" + row.modelData.hostname
                            color: Theme.textMuted
                            elide: Text.ElideMiddle
                            font.pixelSize: Theme.textLabel
                        }
                    }
                    Text {
                        visible: !root.compactLayout
                        Layout.preferredWidth: 88
                        text: root.statusLabel(row.modelData.status)
                        color: root.statusColor(row.modelData.status)
                        font.weight: Font.DemiBold
                    }
                    ActionButton {
                        iconName: "bookmark"
                        text: ""
                        implicitWidth: 36
                        accessibleName: row.modelData.saved ? qsTr("Remove bookmark") : qsTr("Save connection session")
                        variant: row.modelData.saved ? "primary" : "default"
                        onClicked: root.history.toggleSaved(row.modelData.id)
                    }
                    ActionButton {
                        iconName: "trash"
                        text: ""
                        implicitWidth: 36
                        accessibleName: qsTr("Delete connection session")
                        variant: "destructive"
                        onClicked: root.history.remove(row.modelData.id)
                    }
                }
            }
            footer: ActionButton {
                width: ListView.view ? ListView.view.width : 0
                visible: root.history.hasMore
                text: qsTr("Load 30 more")
                onClicked: root.history.loadMore()
            }
        }
    }

    Rectangle {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: root.selectedEntry ? Math.min(420, root.width) : 0
        visible: width > 0
        clip: true
        z: 20
        color: Theme.elevatedBackground
        border.color: Theme.border
        Behavior on width {
            NumberAnimation {
                duration: Theme.motionMedium
                easing.type: Easing.OutCubic
            }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Session details")
                    color: Theme.text
                    font.pixelSize: 18
                    font.weight: Font.Bold
                }
                ActionButton {
                    iconName: "close"
                    text: ""
                    implicitWidth: 36
                    accessibleName: qsTr("Close session details")
                    onClicked: root.selectedEntry = null
                }
            }
            Text {
                Layout.fillWidth: true
                text: root.selectedEntry ? root.selectedEntry.hostLabel : ""
                color: Theme.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }
            Text {
                Layout.fillWidth: true
                text: root.selectedEntry ? (root.selectedEntry.username ? root.selectedEntry.username + "@" : "") + root.selectedEntry.hostname : ""
                color: Theme.textMuted
                wrapMode: Text.WrapAnywhere
            }
            SectionCard {
                Layout.fillWidth: true
                heading: qsTr("Connection")
                ColumnLayout {
                    Layout.fillWidth: true
                    Text {
                        text: root.selectedEntry ? qsTr("Status: %1").arg(root.statusLabel(root.selectedEntry.status)) : ""
                        color: Theme.text
                    }
                    Text {
                        text: root.selectedEntry ? qsTr("Phase: %1").arg(root.selectedEntry.phase || qsTr("Unknown")) : ""
                        color: Theme.text
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: root.selectedEntry && root.selectedEntry.failure.length > 0
                        text: root.selectedEntry ? qsTr("Failure: %1").arg(root.selectedEntry.failure) : ""
                        color: Theme.danger
                        wrapMode: Text.Wrap
                    }
                }
            }
            SectionCard {
                Layout.fillWidth: true
                heading: qsTr("Local origin")
                Text {
                    Layout.fillWidth: true
                    text: root.selectedEntry ? root.selectedEntry.localUsername + " · " + root.selectedEntry.localHostname : ""
                    color: Theme.text
                    wrapMode: Text.Wrap
                }
            }
            SectionCard {
                Layout.fillWidth: true
                visible: root.selectedEntry && root.selectedEntry.rawLogPath.length > 0
                heading: qsTr("Raw terminal log")
                Text {
                    Layout.fillWidth: true
                    text: root.selectedEntry ? root.selectedEntry.rawLogPath : ""
                    color: Theme.textMuted
                    wrapMode: Text.WrapAnywhere
                }
            }
            Item {
                Layout.fillHeight: true
            }
        }
    }

    ConfirmationDialog {
        id: clearDialog
        heading: qsTr("Clear finished sessions?")
        description: qsTr("Saved sessions and active connections are kept.")
        acceptText: qsTr("Clear")
        destructive: true
        onAccepted: root.history.clearUnsaved()
    }
}
