pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: control

    required property var controller
    required property color iconColor
    property var tabs: controller.terminalTabs
    signal terminalActivated(string tabId)
    signal terminalCloseRequested(var tab)

    implicitWidth: visible ? 26 : 0
    implicitHeight: Theme.titleBarHeight
    visible: tabs.length > 1
    color: "transparent"

    Row {
        anchors.fill: parent

        Rectangle {
            width: 26
            height: parent.height
            color: overflowAction.hovered || overflowAction.visualFocus ? Theme.controlHover : "transparent"

            AppIcon {
                anchors.centerIn: parent
                width: 14
                height: 14
                name: "chevron-down"
                color: control.iconColor
            }

            KeyboardAction {
                id: overflowAction

                objectName: "titleTabOverflowAction"
                anchors.fill: parent
                anchors.margins: 2
                accessibleName: qsTranslate("Main", "Show all terminal tabs")
                onActivated: overflowMenu.open()
            }

            AppToolTip {
                visible: overflowAction.hovered && !overflowMenu.visible
                text: qsTranslate("Main", "All terminal tabs")
            }
        }
    }

    AppMenu {
        id: overflowMenu

        y: parent.height
        width: 320

        Instantiator {
            model: control.tabs
            delegate: AppMenuItem {
                id: entry
                required property var modelData
                text: modelData.title
                onTriggered: control.terminalActivated(modelData.id)
                contentItem: RowLayout {
                    Rectangle {
                        Layout.preferredWidth: 7
                        Layout.preferredHeight: 7
                        radius: 4
                        color: entry.modelData.connecting ? Theme.warning : entry.modelData.running ? Theme.accent : Theme.textSubtle
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: entry.modelData.title
                            color: Theme.text
                            font.bold: control.controller.activeTerminalTabId === entry.modelData.id
                            elide: Text.ElideRight
                        }
                        Text {
                            text: entry.modelData.connecting ? qsTr("Connecting") : entry.modelData.running ? qsTr("Connected") : qsTr("Disconnected")
                            color: Theme.textSubtle
                            font.pixelSize: Theme.textCompact
                        }
                    }
                    ToolButton {
                        id: close
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        focusPolicy: Qt.TabFocus
                        hoverEnabled: true
                        background: Rectangle {
                            radius: 5
                            color: close.down ? Theme.controlPressed : close.hovered || close.visualFocus ? Theme.borderStrong : "transparent"
                            border.color: close.visualFocus ? Theme.focus : "transparent"
                            border.width: close.visualFocus ? 1 : 0
                        }
                        Accessible.name: qsTr("Close %1").arg(entry.modelData.title)
                        contentItem: AppIcon {
                            name: "close"
                            color: close.down ? Theme.accent : close.hovered || close.activeFocus ? Theme.text : Theme.textMuted
                            scale: close.down ? 0.82 : 1
                        }
                        onClicked: {
                            const tab = entry.modelData;
                            overflowMenu.close();
                            Qt.callLater(() => control.terminalCloseRequested(tab));
                        }
                        AppToolTip {
                            text: close.Accessible.name
                        }
                    }
                }
            }
            onObjectAdded: (index, object) => overflowMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => overflowMenu.removeItem(object)
        }
    }
}
