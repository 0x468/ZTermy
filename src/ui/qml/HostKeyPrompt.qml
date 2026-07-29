import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: overlay

    property color panelColor: "#141E2B"
    property color borderColor: "#334155"
    property color textColor: "#F8FAFC"
    property color mutedColor: "#CBD5E1"
    property color accentColor: "#22C55E"

    visible: appController.hostKeyPromptVisible
    enabled: visible

    Rectangle {
        anchors.fill: parent
        color: "#99000000"

        MouseArea {
            anchors.fill: parent
        }
    }

    Rectangle {
        width: Math.min(560, overlay.width - 48)
        implicitHeight: promptLayout.implicitHeight + 40
        anchors.centerIn: parent
        radius: 12
        color: overlay.panelColor
        border.color: appController.hostKeyChangedWarning ? "#EF4444" : overlay.borderColor

        ColumnLayout {
            id: promptLayout
            anchors.fill: parent
            anchors.margins: 20
            spacing: 14

            Text {
                Layout.fillWidth: true
                text: appController.hostKeyChangedWarning ? "Host identity changed" : "Verify host identity"
                color: overlay.textColor
                font.family: "Segoe UI Variable"
                font.pixelSize: 20
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                text: appController.hostKeyChangedWarning ? "The saved host key does not match. The connection was blocked. Verify the server outside ztermy before changing trust." : "This host is not trusted yet. Compare the fingerprint with a value obtained from the server administrator."
                color: overlay.mutedColor
                wrapMode: Text.WordWrap
                font.family: "Segoe UI Variable"
                font.pixelSize: 13
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: fingerprintLayout.implicitHeight + 24
                radius: 8
                color: "#0B1017"
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
