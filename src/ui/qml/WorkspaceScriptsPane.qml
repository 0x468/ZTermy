pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var controller
    property bool editing: false

    color: Theme.workspaceBackground

    function beginNew() {
        editing = true;
        editor.beginNew("");
    }

    function beginEdit(script) {
        editing = true;
        editor.beginEdit(script);
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        visible: !root.editing

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Text {
                    text: qsTr("Scripts")
                    color: Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: 22
                    font.weight: Font.Bold
                }
                Text {
                    text: qsTr("Reusable command sequences shared by all terminal sessions.")
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textLabel
                }
            }
            Text {
                text: qsTr("%1 scripts").arg(root.controller.quickCommands.length)
                color: Theme.textMuted
                font.family: Theme.uiFont
                font.pixelSize: Theme.textLabel
            }
            ActionButton {
                text: qsTr("New script")
                iconName: "plus"
                variant: "primary"
                onClicked: root.beginNew()
            }
        }

        StatePanel {
            Layout.fillWidth: true
            visible: root.controller.quickCommands.length === 0
            centered: true
            heading: qsTr("No scripts yet")
            description: qsTr("Create a reusable script here, then run it from any terminal's Scripts panel.")
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
                    model: root.controller.quickCommands
                    delegate: Rectangle {
                        id: card
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 82
                        radius: Theme.radiusControl
                        color: cardHover.hovered ? Theme.controlHover : Theme.raisedBackground
                        border.color: Theme.border
                        HoverHandler {
                            id: cardHover
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 12
                            Rectangle {
                                Layout.preferredWidth: 40
                                Layout.preferredHeight: 40
                                radius: Theme.radiusControl
                                color: Theme.selectedBackground
                                AppIcon {
                                    anchors.centerIn: parent
                                    width: 19
                                    height: 19
                                    name: "commands"
                                    color: Theme.accent
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    text: card.modelData.name
                                    color: Theme.text
                                    elide: Text.ElideRight
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textBody
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: card.modelData.description || card.modelData.command
                                    color: Theme.textMuted
                                    elide: Text.ElideRight
                                    font.family: card.modelData.description ? Theme.uiFont : Theme.terminalFont
                                    font.pixelSize: Theme.textLabel
                                }
                                Text {
                                    text: qsTr("%1 · %2 steps").arg(card.modelData.shell).arg(card.modelData.steps.length)
                                    color: Theme.textSubtle
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textCompact
                                }
                            }
                            ActionButton {
                                text: qsTr("Edit")
                                onClicked: root.beginEdit(card.modelData)
                            }
                            ToolButton {
                                id: deleteButton
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                                hoverEnabled: true
                                Accessible.name: qsTr("Delete %1").arg(card.modelData.name)
                                onClicked: root.controller.deleteQuickCommand(card.modelData.id)
                                contentItem: AppIcon {
                                    name: "trash"
                                    color: deleteButton.hovered ? Theme.danger : Theme.textMuted
                                }
                                background: Rectangle {
                                    radius: height / 2
                                    color: deleteButton.down ? Theme.controlPressed : deleteButton.hovered ? Theme.controlHover : "transparent"
                                }
                                AppToolTip {
                                    text: deleteButton.Accessible.name
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    ScriptEditor {
        id: editor
        anchors.fill: parent
        anchors.margins: 20
        visible: root.editing
        controller: root.controller
        onClosed: root.editing = false
        onSaved: root.editing = false
    }
}
