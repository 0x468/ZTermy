import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: pane

    property color backgroundColor: "#0B1017"
    property color raisedColor: "#141E2B"
    property color borderColor: "#263244"
    property color textColor: "#F8FAFC"
    property color mutedColor: "#94A3B8"
    property color accentColor: "#22C55E"

    signal connectionStarted

    color: backgroundColor
    palette.base: "#0F172A"
    palette.text: textColor
    palette.placeholderText: mutedColor
    palette.button: "#172033"
    palette.buttonText: textColor
    palette.highlight: accentColor
    palette.highlightedText: "#07130B"

    function submit() {
        validationText.text = "";
        if (hostField.text.trim().length === 0 || usernameField.text.length === 0 || keyPathField.text.length === 0 || portField.text.length === 0) {
            validationText.text = "Complete every required field.";
            return;
        }

        const port = Number(portField.text);
        if (port < 1 || port > 65535) {
            validationText.text = "Port must be between 1 and 65535.";
            return;
        }
        if (appController.connectPrivateKey(hostField.text, port, usernameField.text, keyPathField.text)) {
            connectionStarted();
        } else {
            validationText.text = "The connection settings could not be started.";
        }
    }

    ScrollView {
        id: scrollView

        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: connectionForm.implicitHeight + 76

        ColumnLayout {
            id: connectionForm

            x: Math.max(28, (scrollView.availableWidth - width) / 2)
            y: 38
            width: Math.min(620, scrollView.availableWidth - 56)
            spacing: 18

            Text {
                text: "Connect to a host"
                color: pane.textColor
                font.family: "Segoe UI Variable"
                font.pixelSize: 24
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                text: "Start with an Ed25519 private key. Password and encrypted-key prompts will be added without storing secrets."
                color: pane.mutedColor
                wrapMode: Text.WordWrap
                font.family: "Segoe UI Variable"
                font.pixelSize: 13
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 6
                implicitHeight: formLayout.implicitHeight + 40
                radius: 10
                color: pane.raisedColor
                border.color: pane.borderColor

                GridLayout {
                    id: formLayout
                    anchors.fill: parent
                    anchors.margins: 20
                    columns: 2
                    columnSpacing: 14
                    rowSpacing: 10

                    Label {
                        text: "Host"
                        color: pane.textColor
                    }
                    TextField {
                        id: hostField
                        Layout.fillWidth: true
                        placeholderText: "server.example.com or 192.168.1.25"
                        Accessible.name: "SSH host"
                        selectByMouse: true
                    }

                    Label {
                        text: "Port"
                        color: pane.textColor
                    }
                    TextField {
                        id: portField
                        Layout.fillWidth: true
                        text: "22"
                        inputMethodHints: Qt.ImhDigitsOnly
                        validator: IntValidator {
                            bottom: 1
                            top: 65535
                        }
                        Accessible.name: "SSH port"
                        selectByMouse: true
                    }

                    Label {
                        text: "Username"
                        color: pane.textColor
                    }
                    TextField {
                        id: usernameField
                        Layout.fillWidth: true
                        placeholderText: "username"
                        Accessible.name: "SSH username"
                        selectByMouse: true
                    }

                    Label {
                        text: "Private key"
                        color: pane.textColor
                    }
                    TextField {
                        id: keyPathField
                        Layout.fillWidth: true
                        text: appController.defaultPrivateKeyPath
                        Accessible.name: "Private-key file path"
                        selectByMouse: true
                    }
                }
            }

            Text {
                id: validationText
                Layout.fillWidth: true
                visible: text.length > 0
                color: "#FCA5A5"
                font.family: "Segoe UI Variable"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true

                Item {
                    Layout.fillWidth: true
                }

                Button {
                    text: "Connect"
                    Accessible.name: "Connect to SSH host"
                    highlighted: true
                    palette.button: pane.accentColor
                    palette.buttonText: "#07130B"
                    font.weight: Font.DemiBold
                    onClicked: pane.submit()
                }
            }
        }
    }
}
