import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: overlay

    property color panelColor: Theme.elevatedBackground
    property color borderColor: Theme.borderStrong
    property color textColor: Theme.text
    property color mutedColor: Theme.textSoft
    property color accentColor: Theme.accent

    visible: appController.hostKeyPromptVisible
    enabled: visible

    Rectangle {
        anchors.fill: parent
        color: Theme.modalScrim

        MouseArea {
            anchors.fill: parent
        }
    }

    Rectangle {
        width: Math.min(560, overlay.width - 48)
        implicitHeight: promptLayout.implicitHeight + 40
        anchors.centerIn: parent
        radius: Theme.radiusPanel
        color: overlay.panelColor
        border.color: appController.hostKeyChangedWarning ? Theme.danger : overlay.borderColor

        ColumnLayout {
            id: promptLayout
            anchors.fill: parent
            anchors.margins: 20
            spacing: 14

            Text {
                Layout.fillWidth: true
                text: appController.hostKeyChangedWarning ? "Host identity changed" : "Verify host identity"
                color: overlay.textColor
                font.family: Theme.uiFont
                font.pixelSize: Theme.textTitle
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                text: appController.hostKeyChangedWarning ? "The saved host key does not match. The connection was blocked. Verify the server outside ztermy before changing trust." : "This host is not trusted yet. Compare the fingerprint with a value obtained from the server administrator."
                color: overlay.mutedColor
                wrapMode: Text.WordWrap
                font.family: Theme.uiFont
                font.pixelSize: Theme.textBody
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: fingerprintLayout.implicitHeight + 24
                radius: Theme.radiusControl
                color: Theme.workspaceBackground
                border.color: overlay.borderColor

                ColumnLayout {
                    id: fingerprintLayout
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 7

                    Text {
                        text: appController.hostKeyAlgorithm
                        color: overlay.mutedColor
                        font.family: "Segoe UI Variable"
                        font.pixelSize: 11
                    }

                    TextEdit {
                        Layout.fillWidth: true
                        text: appController.hostKeyFingerprint
                        color: overlay.textColor
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        font.family: "Cascadia Mono"
                        font.pixelSize: 12
                        Accessible.name: "Observed SSH host fingerprint"
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 9

                Item {
                    Layout.fillWidth: true
                }

                Button {
                    text: appController.hostKeyChangedWarning ? "Close" : "Reject"
                    Accessible.name: text
                    onClicked: appController.rejectHostKey()
                }

                Button {
                    visible: !appController.hostKeyChangedWarning
                    text: "Trust once"
                    Accessible.name: "Trust this host once"
                    onClicked: appController.acceptHostKey(false)
                }

                Button {
                    visible: !appController.hostKeyChangedWarning
                    text: "Trust and remember"
                    Accessible.name: "Trust and remember this host"
                    highlighted: true
                    onClicked: appController.acceptHostKey(true)
                }
            }
        }
    }
}
