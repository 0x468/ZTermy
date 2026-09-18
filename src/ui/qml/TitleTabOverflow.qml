pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// Overflow entry point for the terminal tab strip: lists every tab with its
// session status and a close affordance.
TitleChromeAction {
    id: control

    required property var controller
    property var tabs: controller.terminalTabs
    signal terminalActivated(string tabId)
    signal terminalCloseRequested(var tab)

    implicitWidth: visible ? 26 : 0
    visible: tabs.length > 1
    iconName: "chevron-down"
    iconSize: 14
    actionObjectName: "titleTabOverflowAction"
    accessibleName: qsTranslate("Main", "Show all terminal tabs")
    toolTip: qsTranslate("Main", "All terminal tabs")
    menuOpen: overflowMenu.visible
    onActivated: overflowMenu.open()

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
                    SessionStatusDot {
                        Layout.preferredWidth: 7
                        Layout.preferredHeight: 7
                        running: !!entry.modelData.running
                        connecting: !!entry.modelData.connecting
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
                    AppIconButton {
                        id: close
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        label: qsTr("Close %1").arg(entry.modelData.title)
                        iconName: "close"
                        iconColor: close.down ? Theme.accent : close.hovered || close.activeFocus ? Theme.text : Theme.textMuted
                        onClicked: {
                            const tab = entry.modelData;
                            overflowMenu.close();
                            Qt.callLater(() => control.terminalCloseRequested(tab));
                        }
                    }
                }
            }
            onObjectAdded: (index, object) => overflowMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => overflowMenu.removeItem(object)
        }
    }
}
