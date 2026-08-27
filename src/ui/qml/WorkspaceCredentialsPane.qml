pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var controller
    signal editHostRequested(string profileId)
    signal securitySettingsRequested

    readonly property var entries: {
        const result = [];
        for (const profile of controller.hostProfiles) {
            if (profile.authentication === "agent" || profile.credentialStored || (profile.privateKeyPath || "").length > 0)
                result.push(profile);
        }
        return result;
    }

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
                    text: qsTr("Credentials")
                    color: Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: 22
                    font.weight: Font.Bold
                }
                Text {
                    text: qsTr("Passwords, private keys, and SSH agent identities currently attached to saved hosts.")
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textLabel
                    wrapMode: Text.Wrap
                }
            }

            ActionButton {
                text: qsTr("Storage settings")
                iconName: "settings"
                onClicked: root.securitySettingsRequested()
            }
        }

        StatePanel {
            Layout.fillWidth: true
            visible: root.entries.length === 0
            heading: qsTr("No saved credentials")
            description: qsTr("Save a password, private-key reference, or SSH agent host from Hosts. Credentials remain attached to their host profile in this version.")
            centered: true
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
                    model: root.entries

                    delegate: Rectangle {
                        id: card
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 72
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
                                    name: "security"
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
                                    text: card.modelData.authentication === "agent" ? qsTr("SSH agent") : card.modelData.authentication === "private-key" ? qsTr("Private key · %1").arg(card.modelData.privateKeyPath || qsTr("path not set")) : card.modelData.credentialStored ? qsTr("Saved password") : qsTr("Credential required when connecting")
                                    color: Theme.textMuted
                                    elide: Text.ElideMiddle
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textLabel
                                }
                            }
                            ActionButton {
                                text: qsTr("Edit host")
                                onClicked: root.editHostRequested(card.modelData.id)
                            }
                        }
                    }
                }
            }
        }
    }
}
