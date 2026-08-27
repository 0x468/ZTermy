pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var controller
    signal editHostRequested(string profileId)

    readonly property var entries: {
        const result = [];
        for (const profile of controller.hostProfiles) {
            if ((profile.proxyType || "none") !== "none")
                result.push(profile);
        }
        return result;
    }

    color: Theme.workspaceBackground

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3
            Text {
                text: qsTr("Proxies")
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: 22
                font.weight: Font.Bold
            }
            Text {
                text: qsTr("Proxy routes currently attached to saved SSH hosts.")
                color: Theme.textMuted
                font.family: Theme.uiFont
                font.pixelSize: Theme.textLabel
            }
        }

        StatePanel {
            Layout.fillWidth: true
            visible: root.entries.length === 0
            centered: true
            heading: qsTr("No configured proxies")
            description: qsTr("Configure an HTTP or SOCKS proxy in a saved host profile. Reusable global proxy profiles are not enabled yet.")
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
                        Layout.preferredHeight: 76
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
                                    name: "network"
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
                                    text: (card.modelData.proxyType || "proxy").toUpperCase() + " · " + (card.modelData.proxyHost || "") + ":" + (card.modelData.proxyPort || "")
                                    color: Theme.textMuted
                                    elide: Text.ElideMiddle
                                    font.family: Theme.terminalFont
                                    font.pixelSize: Theme.textLabel
                                }
                                Text {
                                    visible: !!card.modelData.proxyCredentialStored
                                    text: qsTr("Proxy credential saved")
                                    color: Theme.textSubtle
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textCompact
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
