pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: pane

    required property var controller
    signal insertRequested(string path)

    color: Theme.workspaceBackground

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            ActionButton {
                text: qsTr("Up")
                iconName: "chevron-up"
                accessibleName: qsTr("Open parent local directory")
                onClicked: pane.controller.localFiles.navigateUp()
            }

            AppTextField {
                Layout.fillWidth: true
                compact: true
                text: pane.controller.localFiles.path
                accessibleName: qsTr("Local directory path")
                selectByMouse: true
                onAccepted: pane.controller.localFiles.navigate(text)
            }

            ActionButton {
                text: qsTr("Refresh")
                iconName: "refresh"
                accessibleName: qsTr("Refresh local directory")
                onClicked: pane.controller.localFiles.refresh()
            }
        }

        StatusMessage {
            Layout.fillWidth: true
            text: pane.controller.localFiles.error
            kind: "error"
        }

        ListView {
            id: localFileList

            objectName: "localFileList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: pane.controller.localFiles.entries

            delegate: Rectangle {
                id: fileRow

                required property var modelData
                width: localFileList.width
                height: 48
                radius: Theme.radiusSmall
                color: rowAction.hovered ? Theme.controlHover : Theme.controlBackground
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 8
                    spacing: 8

                    AppIcon {
                        Layout.preferredWidth: 16
                        Layout.preferredHeight: 16
                        name: fileRow.modelData.directory ? "folder" : "file"
                        color: fileRow.modelData.directory ? Theme.accent : Theme.textMuted
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Text {
                            Layout.fillWidth: true
                            text: fileRow.modelData.name
                            color: Theme.text
                            elide: Text.ElideMiddle
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textBody
                        }

                        Text {
                            Layout.fillWidth: true
                            text: fileRow.modelData.directory ? qsTr("Directory") : qsTr("%1 bytes").arg(fileRow.modelData.size)
                            color: Theme.textMuted
                            elide: Text.ElideRight
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textCompact
                        }
                    }

                    ActionButton {
                        text: qsTr("Insert")
                        visible: !fileRow.modelData.directory
                        accessibleName: qsTr("Insert local file path")
                        onClicked: pane.insertRequested(fileRow.modelData.path)
                    }

                    ActionButton {
                        text: fileRow.modelData.directory ? qsTr("Open") : qsTr("System open")
                        accessibleName: fileRow.modelData.directory ? qsTr("Open local directory") : qsTr("Open local file with the system")
                        onClicked: {
                            if (fileRow.modelData.directory)
                                pane.controller.localFiles.navigate(fileRow.modelData.path);
                            else
                                pane.controller.localFiles.openPath(fileRow.modelData.path);
                        }
                    }
                }

                KeyboardAction {
                    id: rowAction
                    anchors.fill: parent
                    anchors.rightMargin: 170
                    accessibleName: fileRow.modelData.name
                    onActivated: {
                        if (fileRow.modelData.directory)
                            pane.controller.localFiles.navigate(fileRow.modelData.path);
                        else
                            pane.insertRequested(fileRow.modelData.path);
                    }
                }
            }

            ScrollBar.vertical: ScrollBar {}
        }

        StatePanel {
            Layout.alignment: Qt.AlignCenter
            visible: !pane.controller.localFiles.busy && pane.controller.localFiles.entries.length === 0 && pane.controller.localFiles.error.length === 0
            kind: "empty"
            heading: qsTr("This directory is empty")
            description: qsTr("Navigate to another local folder or refresh the listing.")
        }

        BusyIndicator {
            Layout.alignment: Qt.AlignCenter
            visible: pane.controller.localFiles.busy
            running: visible
        }
    }
}
