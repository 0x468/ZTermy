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
    property string editingProfileId: ""
    property string pendingDeleteId: ""
    property string pendingDeleteName: ""
    property string pendingConnectId: ""
    property string pendingConnectName: ""
    property string pendingConnectAuthentication: ""
    property bool statusIsError: false

    signal connectionStarted

    color: backgroundColor
    palette.base: "#0F172A"
    palette.text: textColor
    palette.placeholderText: mutedColor
    palette.button: "#172033"
    palette.buttonText: textColor
    palette.highlight: accentColor
    palette.highlightedText: "#07130B"

    function portNumber() {
        return Number(portField.text);
    }

    function authenticationToken() {
        return authenticationBox.currentIndex === 0 ? "private-key" : "password";
    }

    function validate(requireName, requireCredential) {
        const privateKey = authenticationToken() === "private-key";
        if ((requireName && nameField.text.trim().length === 0) || hostField.text.trim().length === 0 || usernameField.text.trim().length === 0 || (privateKey && keyPathField.text.trim().length === 0) || portField.text.length === 0) {
            showStatus("Complete every required field.", true);
            return false;
        }
        const port = portNumber();
        if (port < 1 || port > 65535) {
            showStatus("Port must be between 1 and 65535.", true);
            return false;
        }
        if (requireCredential && (!privateKey || passphraseRequiredBox.checked) && credentialField.text.length === 0) {
            showStatus(privateKey ? "Enter the private-key passphrase." : "Enter the SSH password.", true);
            return false;
        }
        return true;
    }

    function showStatus(message, isError) {
        statusText.text = message;
        statusIsError = isError;
    }

    function clearEditor() {
        editingProfileId = "";
        nameField.text = "";
        hostField.text = "";
        portField.text = "22";
        usernameField.text = "";
        authenticationBox.currentIndex = 0;
        keyPathField.text = appController.defaultPrivateKeyPath;
        passphraseRequiredBox.checked = false;
        credentialField.text = "";
    }

    function editProfile(profile) {
        editingProfileId = profile.id;
        nameField.text = profile.name;
        hostField.text = profile.host;
        portField.text = String(profile.port);
        usernameField.text = profile.username;
        authenticationBox.currentIndex = profile.authentication === "password" ? 1 : 0;
        keyPathField.text = profile.privateKeyPath;
        passphraseRequiredBox.checked = profile.privateKeyPassphraseRequired;
        credentialField.text = "";
        showStatus("Editing \"" + profile.name + "\".", false);
        nameField.forceActiveFocus();
    }

    function saveProfile() {
        statusText.text = "";
        if (!validate(true, false)) {
            return;
        }
        if (appController.saveHostProfile(editingProfileId, nameField.text, hostField.text, portNumber(), usernameField.text, authenticationToken(), keyPathField.text, passphraseRequiredBox.checked)) {
            clearEditor();
            showStatus("Profile saved. Passwords and key passphrases are never stored.", false);
        } else {
            showStatus("The profile could not be saved.", true);
        }
    }

    function connectCurrent() {
        statusText.text = "";
        if (!validate(false, true)) {
            return;
        }
        const secret = credentialField.text;
        credentialField.text = "";
        const started = authenticationToken() === "private-key" ? appController.connectPrivateKey(hostField.text, portNumber(), usernameField.text, keyPathField.text, secret) : appController.connectPassword(hostField.text, portNumber(), usernameField.text, secret);
        if (started) {
            connectionStarted();
        } else {
            showStatus("The connection settings could not be started.", true);
        }
    }

    function connectSaved(profile) {
        if (profile.authentication === "password" || profile.privateKeyPassphraseRequired) {
            pendingConnectId = profile.id;
            pendingConnectName = profile.name;
            pendingConnectAuthentication = profile.authentication;
            savedCredentialField.text = "";
            credentialDialog.open();
            savedCredentialField.forceActiveFocus();
            return;
        }
        if (appController.connectHostProfile(profile.id, "")) {
            connectionStarted();
        } else {
            showStatus("The saved profile could not be connected.", true);
        }
    }

    function connectPendingSaved() {
        if (savedCredentialField.text.length === 0) {
            return;
        }
        const profileId = pendingConnectId;
        const secret = savedCredentialField.text;
        savedCredentialField.text = "";
        if (appController.connectHostProfile(profileId, secret)) {
            credentialDialog.close();
            connectionStarted();
        } else {
            showStatus("The saved profile could not be connected.", true);
            credentialDialog.close();
        }
    }

    ScrollView {
        id: scrollView

        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: contentColumn.implicitHeight + 76

        ColumnLayout {
            id: contentColumn

            x: Math.max(28, (scrollView.availableWidth - width) / 2)
            y: 38
            width: Math.min(720, scrollView.availableWidth - 56)
            spacing: 18

            Text {
                text: "SSH hosts"
                color: pane.textColor
                font.family: "Segoe UI Variable"
                font.pixelSize: 24
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                text: "Save connection details for quick access. ztermy stores only non-secret profile fields; passwords and private-key passphrases stay in memory for the active attempt."
                color: pane.mutedColor
                wrapMode: Text.WordWrap
                font.family: "Segoe UI Variable"
                font.pixelSize: 13
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6

                Text {
                    text: "Saved hosts"
                    color: pane.textColor
                    font.family: "Segoe UI Variable"
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }

                Item {
                    Layout.fillWidth: true
                }

                Text {
                    text: appController.hostProfiles.length + (appController.hostProfiles.length === 1 ? " profile" : " profiles")
                    color: pane.mutedColor
                    font.family: "Segoe UI Variable"
                    font.pixelSize: 12
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 72
                visible: appController.hostProfiles.length === 0
                radius: 9
                color: pane.raisedColor
                border.color: pane.borderColor

                Text {
                    anchors.centerIn: parent
                    text: "No saved hosts yet. Complete the form below and choose Save profile."
                    color: pane.mutedColor
                    font.family: "Segoe UI Variable"
                    font.pixelSize: 12
                }
            }

            Repeater {
                model: appController.hostProfiles

                delegate: Rectangle {
                    id: profileCard

                    required property var modelData

                    Layout.fillWidth: true
                    implicitHeight: profileRow.implicitHeight + 24
                    radius: 9
                    color: pane.raisedColor
                    border.color: pane.borderColor

                    RowLayout {
                        id: profileRow

                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 10

                        Rectangle {
                            Layout.preferredWidth: 34
                            Layout.preferredHeight: 34
                            radius: 8
                            color: "#173A2B"

                            Text {
                                anchors.centerIn: parent
                                text: ">"
                                color: pane.accentColor
                                font.family: "Cascadia Mono"
                                font.pixelSize: 16
                                font.weight: Font.Bold
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                Layout.fillWidth: true
                                text: profileCard.modelData.name
                                color: pane.textColor
                                elide: Text.ElideRight
                                font.family: "Segoe UI Variable"
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }

                            Text {
                                Layout.fillWidth: true
                                text: profileCard.modelData.username + "@" + profileCard.modelData.host + ":" + profileCard.modelData.port
                                color: pane.mutedColor
                                elide: Text.ElideMiddle
                                font.family: "Cascadia Mono"
                                font.pixelSize: 11
                            }
                        }

                        Button {
                            text: "Connect"
                            Accessible.name: "Connect to " + profileCard.modelData.name
                            palette.button: pane.accentColor
                            palette.buttonText: "#07130B"
                            font.weight: Font.DemiBold
                            onClicked: pane.connectSaved(profileCard.modelData)
                        }

                        Button {
                            text: "Edit"
                            Accessible.name: "Edit " + profileCard.modelData.name
                            onClicked: pane.editProfile(profileCard.modelData)
                        }

                        Button {
                            text: "Delete"
                            Accessible.name: "Delete " + profileCard.modelData.name
                            onClicked: {
                                pane.pendingDeleteId = profileCard.modelData.id;
                                pane.pendingDeleteName = profileCard.modelData.name;
                                deleteDialog.open();
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 10
                implicitHeight: editorColumn.implicitHeight + 40
                radius: 10
                color: pane.raisedColor
                border.color: pane.borderColor

                ColumnLayout {
                    id: editorColumn

                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 14

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: pane.editingProfileId.length > 0 ? "Edit profile" : "New connection"
                            color: pane.textColor
                            font.family: "Segoe UI Variable"
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        Text {
                            text: authenticationBox.currentIndex === 0 ? "Private-key authentication" : "Password authentication"
                            color: pane.mutedColor
                            font.family: "Segoe UI Variable"
                            font.pixelSize: 11
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 14
                        rowSpacing: 10

                        Label {
                            text: "Profile name"
                            color: pane.textColor
                        }
                        TextField {
                            id: nameField
                            Layout.fillWidth: true
                            placeholderText: "Home server"
                            Accessible.name: "Profile name"
                            selectByMouse: true
                        }

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
                            text: "Authentication"
                            color: pane.textColor
                        }
                        ComboBox {
                            id: authenticationBox
                            Layout.fillWidth: true
                            model: ["Private key", "Password"]
                            Accessible.name: "SSH authentication method"
                            delegate: ItemDelegate {
                                required property int index
                                required property var modelData

                                width: authenticationBox.width
                                text: modelData
                                highlighted: authenticationBox.highlightedIndex === index

                                contentItem: Text {
                                    text: parent.text
                                    color: pane.textColor
                                    verticalAlignment: Text.AlignVCenter
                                    font.family: "Segoe UI Variable"
                                    font.pixelSize: 13
                                }

                                background: Rectangle {
                                    color: parent.highlighted ? "#1F513A" : "#172033"
                                }
                            }
                            popup: Popup {
                                y: authenticationBox.height - 1
                                width: authenticationBox.width
                                implicitHeight: contentItem.implicitHeight + 2
                                padding: 1

                                contentItem: ListView {
                                    clip: true
                                    implicitHeight: contentHeight
                                    model: authenticationBox.popup.visible ? authenticationBox.delegateModel : null
                                    currentIndex: authenticationBox.highlightedIndex
                                }

                                background: Rectangle {
                                    color: "#172033"
                                    border.color: pane.borderColor
                                }
                            }
                            onCurrentIndexChanged: {
                                credentialField.text = "";
                                if (currentIndex === 1) {
                                    passphraseRequiredBox.checked = false;
                                }
                            }
                        }

                        Label {
                            text: "Private key"
                            color: pane.textColor
                            visible: authenticationBox.currentIndex === 0
                        }
                        TextField {
                            id: keyPathField
                            Layout.fillWidth: true
                            visible: authenticationBox.currentIndex === 0
                            text: appController.defaultPrivateKeyPath
                            Accessible.name: "Private-key file path"
                            selectByMouse: true
                        }

                        Item {
                            visible: authenticationBox.currentIndex === 0
                            implicitHeight: passphraseRequiredBox.implicitHeight
                        }
                        CheckBox {
                            id: passphraseRequiredBox
                            Layout.fillWidth: true
                            visible: authenticationBox.currentIndex === 0
                            text: "This private key requires a passphrase"
                            Accessible.name: "Private key requires a passphrase"
                            onCheckedChanged: credentialField.text = ""
                        }

                        Label {
                            text: authenticationBox.currentIndex === 0 ? "Passphrase" : "Password"
                            color: pane.textColor
                            visible: authenticationBox.currentIndex === 1 || passphraseRequiredBox.checked
                        }
                        TextField {
                            id: credentialField
                            Layout.fillWidth: true
                            visible: authenticationBox.currentIndex === 1 || passphraseRequiredBox.checked
                            placeholderText: authenticationBox.currentIndex === 0 ? "Private-key passphrase" : "SSH password"
                            echoMode: TextInput.Password
                            Accessible.name: authenticationBox.currentIndex === 0 ? "Private-key passphrase" : "SSH password"
                            selectByMouse: true
                        }
                    }

                    Text {
                        id: statusText

                        Layout.fillWidth: true
                        visible: text.length > 0
                        color: pane.statusIsError ? "#FCA5A5" : "#86EFAC"
                        font.family: "Segoe UI Variable"
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Button {
                            visible: pane.editingProfileId.length > 0
                            text: "Cancel editing"
                            Accessible.name: "Cancel profile editing"
                            onClicked: {
                                pane.clearEditor();
                                pane.showStatus("Editing cancelled.", false);
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        Button {
                            text: "Save profile"
                            Accessible.name: "Save SSH profile"
                            onClicked: pane.saveProfile()
                        }

                        Button {
                            text: "Connect"
                            Accessible.name: "Connect to SSH host"
                            palette.button: pane.accentColor
                            palette.buttonText: "#07130B"
                            font.weight: Font.DemiBold
                            onClicked: pane.connectCurrent()
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: credentialDialog

        anchors.centerIn: parent
        modal: true
        closePolicy: Popup.CloseOnEscape
        padding: 20
        onClosed: {
            savedCredentialField.text = "";
            pane.pendingConnectId = "";
            pane.pendingConnectName = "";
            pane.pendingConnectAuthentication = "";
        }

        background: Rectangle {
            radius: 10
            color: pane.raisedColor
            border.color: pane.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 14

            Text {
                text: pane.pendingConnectAuthentication === "password" ? "Enter SSH password" : "Enter key passphrase"
                color: pane.textColor
                font.family: "Segoe UI Variable"
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }

            Text {
                Layout.preferredWidth: 360
                text: "Authenticate to \"" + pane.pendingConnectName + "\". This credential is kept only for this connection attempt."
                color: pane.mutedColor
                wrapMode: Text.WordWrap
                font.family: "Segoe UI Variable"
                font.pixelSize: 12
            }

            TextField {
                id: savedCredentialField
                Layout.fillWidth: true
                placeholderText: pane.pendingConnectAuthentication === "password" ? "SSH password" : "Private-key passphrase"
                echoMode: TextInput.Password
                Accessible.name: placeholderText
                selectByMouse: true
                onAccepted: pane.connectPendingSaved()
            }

            RowLayout {
                Layout.fillWidth: true

                Item {
                    Layout.fillWidth: true
                }

                Button {
                    text: "Cancel"
                    onClicked: credentialDialog.close()
                }

                Button {
                    id: connectSavedButton
                    text: "Connect"
                    enabled: savedCredentialField.text.length > 0
                    palette.button: pane.accentColor
                    palette.buttonText: "#07130B"
                    font.weight: Font.DemiBold
                    onClicked: pane.connectPendingSaved()
                }
            }
        }
    }

    Dialog {
        id: deleteDialog

        anchors.centerIn: parent
        modal: true
        closePolicy: Popup.CloseOnEscape
        padding: 20

        background: Rectangle {
            radius: 10
            color: pane.raisedColor
            border.color: "#7F1D1D"
        }

        contentItem: ColumnLayout {
            spacing: 14

            Text {
                text: "Delete saved host?"
                color: pane.textColor
                font.family: "Segoe UI Variable"
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }

            Text {
                Layout.preferredWidth: 360
                text: "Remove \"" + pane.pendingDeleteName + "\" from this device? This does not change the remote server or trusted host keys."
                color: pane.mutedColor
                wrapMode: Text.WordWrap
                font.family: "Segoe UI Variable"
                font.pixelSize: 12
            }

            RowLayout {
                Layout.fillWidth: true

                Item {
                    Layout.fillWidth: true
                }

                Button {
                    text: "Cancel"
                    onClicked: deleteDialog.close()
                }

                Button {
                    text: "Delete"
                    palette.button: "#991B1B"
                    palette.buttonText: "#FFFFFF"
                    onClicked: {
                        if (appController.deleteHostProfile(pane.pendingDeleteId)) {
                            pane.showStatus("Profile deleted.", false);
                        } else {
                            pane.showStatus("The profile could not be deleted.", true);
                        }
                        pane.pendingDeleteId = "";
                        pane.pendingDeleteName = "";
                        deleteDialog.close();
                    }
                }
            }
        }
    }
}
