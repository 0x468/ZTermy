pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var controller
    property var activeTab: null
    signal openTerminalRequested(string tabId)
    signal toggleActiveLogRequested

    color: Theme.workspaceBackground

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Text {
                    text: qsTr("Session logs")
                    color: Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: 22
                    font.weight: Font.Bold
                }
                Text {
                    text: qsTr("Start or stop raw terminal logging for an open session. Log contents are only written after you choose a destination file.")
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textLabel
                    wrapMode: Text.Wrap
                }
            }
            ActionButton {
                visible: root.controller.activeTerminalTabId.length > 0
                text: root.activeTab !== null && (root.activeTab.logState === "active" || root.activeTab.logState === "starting") ? qsTr("Stop active log") : qsTr("Start active log")
                iconName: "save"
                onClicked: root.toggleActiveLogRequested()
            }
        }

        StatePanel {
            Layout.fillWidth: true
            visible: root.controller.terminalTabs.length === 0
            centered: true
            heading: qsTr("No open terminal sessions")
            description: qsTr("Open a local or SSH terminal before starting a session log.")
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 8
                Repeater {
                    model: root.controller.terminalTabs
                    delegate: Rectangle {
                        id: card
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 70
                        radius: Theme.radiusControl
                        color: cardHover.hovered ? Theme.controlHover : Theme.raisedBackground
                        border.color: card.modelData.logState === "active" ? Theme.accent : Theme.border
                        HoverHandler {
                            id: cardHover
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 12
                            AppIcon {
                                Layout.preferredWidth: 20
                                Layout.preferredHeight: 20
                                name: "terminal"
                                color: card.modelData.logState === "active" ? Theme.accent : Theme.textMuted
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    text: card.modelData.title
                                    color: Theme.text
                                    elide: Text.ElideRight
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textBody
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: card.modelData.logState === "active" ? qsTr("Recording · %1").arg(card.modelData.logPath || "") : qsTr("Not recording")
                                    color: card.modelData.logState === "active" ? Theme.accent : Theme.textMuted
                                    elide: Text.ElideMiddle
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textLabel
                                }
                            }
                            ActionButton {
                                text: qsTr("Open terminal")
                                onClicked: root.openTerminalRequested(card.modelData.id)
                            }
                        }
                    }
                }
            }
        }
    }
}
