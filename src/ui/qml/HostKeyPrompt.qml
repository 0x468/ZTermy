import QtQuick
import QtQuick.Layouts

Item {
    id: overlay

    required property var controller
    property Item focusRestoreItem: null
    property color panelColor: Theme.elevatedBackground
    property color borderColor: Theme.borderStrong
    property color textColor: Theme.text
    property color mutedColor: Theme.textSoft
    property color accentColor: Theme.accent

    visible: controller.hostKeyPromptVisible || opacity > 0.001
    enabled: controller.hostKeyPromptVisible
    focus: enabled
    opacity: controller.hostKeyPromptVisible ? 1 : 0

    Behavior on opacity {
        NumberAnimation {
            duration: overlay.controller.hostKeyPromptVisible ? Theme.motionMedium : Theme.motionFast
            easing.type: overlay.controller.hostKeyPromptVisible ? Easing.OutCubic : Easing.InCubic
        }
    }

    onEnabledChanged: {
        if (enabled) {
            Qt.callLater(rejectButton.forceActiveFocus);
        } else if (focusRestoreItem && focusRestoreItem.visible && focusRestoreItem.enabled) {
            Qt.callLater(focusRestoreItem.forceActiveFocus);
        }
    }

    Keys.onEscapePressed: event => {
        overlay.controller.rejectHostKey();
        event.accepted = true;
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.modalScrim

        MouseArea {
            anchors.fill: parent
        }
    }

    Text {
        Layout.fillWidth: true
        text: overlay.controller.hostKeyEndpoint
        color: overlay.textColor
        elide: Text.ElideMiddle
        font.family: Theme.uiFont
        font.pixelSize: Theme.textBody
        font.weight: Font.DemiBold
        Accessible.name: qsTr("SSH endpoint being verified")
    }

    Rectangle {
        id: promptPanel

        objectName: "hostKeyPromptPanel"
        width: Math.min(560, overlay.width - 48)
        implicitHeight: promptLayout.implicitHeight + 40
        anchors.centerIn: parent
        radius: Theme.radiusPanel
        color: overlay.panelColor
        border.color: overlay.controller.hostKeyChangedWarning ? Theme.danger : overlay.borderColor
        Accessible.role: Accessible.Dialog
        Accessible.name: overlay.controller.hostKeyChangedWarning ? qsTr("Host identity changed") : qsTr("Verify host identity")

        transform: Translate {
            y: overlay.controller.hostKeyPromptVisible ? 0 : Theme.motionDistanceSmall

            Behavior on y {
                NumberAnimation {
                    duration: Theme.motionMedium
                    easing.type: Easing.OutCubic
                }
            }
        }

        ColumnLayout {
            id: promptLayout
            anchors.fill: parent
            anchors.margins: 20
            spacing: 14

            Text {
                Layout.fillWidth: true
                text: overlay.controller.hostKeyChangedWarning ? qsTr("Host identity changed") : qsTr("Verify host identity")
                color: overlay.textColor
                font.family: Theme.uiFont
                font.pixelSize: Theme.textTitle
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                text: overlay.controller.hostKeyChangedWarning ? qsTr("The saved host key does not match. The connection was blocked. Verify the server outside ztermy before changing trust.") : qsTr("This host is not trusted yet. Compare the fingerprint with a value obtained from the server administrator.")
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
                        text: overlay.controller.hostKeyAlgorithm
                        color: overlay.mutedColor
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                    }

                    TextEdit {
                        Layout.fillWidth: true
                        text: overlay.controller.hostKeyFingerprint
                        color: overlay.textColor
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        font.family: Theme.terminalFont
                        font.pixelSize: 12
                        Accessible.name: qsTr("Observed SSH host fingerprint")
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 9

                Item {
                    Layout.fillWidth: true
                }

                ActionButton {
                    id: rejectButton

                    objectName: "hostKeyReject"
                    text: overlay.controller.hostKeyChangedWarning ? qsTr("Close") : qsTr("Reject")
                    accessibleName: text
                    KeyNavigation.left: rememberButton.visible ? rememberButton : rejectButton
                    KeyNavigation.right: trustOnceButton.visible ? trustOnceButton : rejectButton
                    onClicked: overlay.controller.rejectHostKey()
                }

                ActionButton {
                    id: trustOnceButton

                    objectName: "hostKeyTrustOnce"
                    visible: !overlay.controller.hostKeyChangedWarning
                    text: qsTr("Trust once")
                    accessibleName: qsTr("Trust this host once")
                    KeyNavigation.left: rejectButton
                    KeyNavigation.right: rememberButton
                    onClicked: overlay.controller.acceptHostKey(false)
                }

                ActionButton {
                    id: rememberButton

                    objectName: "hostKeyRemember"
                    visible: !overlay.controller.hostKeyChangedWarning
                    text: qsTr("Trust and remember")
                    accessibleName: qsTr("Trust and remember this host")
                    variant: "primary"
                    KeyNavigation.left: trustOnceButton
                    KeyNavigation.right: rejectButton
                    onClicked: overlay.controller.acceptHostKey(true)
                }
            }
        }
    }
}
