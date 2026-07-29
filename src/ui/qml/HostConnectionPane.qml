pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: pane

    property color backgroundColor: Theme.workspaceBackground
    property color raisedColor: Theme.elevatedBackground
    property color borderColor: Theme.border
    property color textColor: Theme.text
    property color mutedColor: Theme.textMuted
    property color accentColor: Theme.accent
    required property var controller
    property string editingProfileId: ""
    property string pendingDeleteId: ""
    property string pendingDeleteName: ""
    property string pendingConnectId: ""
    property string pendingConnectName: ""
    property string pendingConnectAuthentication: ""
    property bool statusIsError: false
    property bool editorExpanded: controller.hostProfiles.length === 0
    readonly property var filteredGroups: buildFilteredGroups(controller.hostProfiles, searchField.text)
    readonly property int filteredProfileCount: {
        let count = 0;
        for (const group of filteredGroups) {
            count += group.profiles.length;
        }
        return count;
    }

    signal connectionStarted

    color: backgroundColor
    palette.base: Theme.raisedBackground
    palette.text: textColor
    palette.placeholderText: mutedColor
    palette.button: Theme.controlBackground
    palette.buttonText: textColor
    palette.highlight: accentColor
    palette.highlightedText: Theme.accentText

    function portNumber() {
        return Number(portField.text);
    }

    function authenticationToken() {
        return authenticationBox.currentIndex === 0 ? "private-key" : "password";
    }

    function buildFilteredGroups(profiles, searchText) {
        const query = searchText.trim().toLocaleLowerCase();
        const groups = {};
        for (const profile of profiles) {
            const groupName = profile.group.trim().length > 0 ? profile.group.trim() : "Ungrouped";
            const searchable = [
                profile.name,
                groupName,
                profile.username,
                profile.host,
                String(profile.port),
                profile.authentication
            ].join(" ").toLocaleLowerCase();
            if (query.length > 0 && searchable.indexOf(query) < 0) {
                continue;
            }
            if (!groups[groupName]) {
                groups[groupName] = [];
            }
            groups[groupName].push(profile);
        }

        const result = [];
        const groupNames = Object.keys(groups).sort((left, right) => left.localeCompare(right));
        for (const groupName of groupNames) {
            groups[groupName].sort((left, right) => left.name.localeCompare(right.name));
            result.push({ name: groupName, profiles: groups[groupName] });
        }
        return result;
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
        groupField.text = "";
        hostField.text = "";
        portField.text = "22";
        usernameField.text = "";
        authenticationBox.currentIndex = 0;
        keyPathField.text = controller.defaultPrivateKeyPath;
        passphraseRequiredBox.checked = false;
        credentialField.text = "";
    }

    function beginNewProfile() {
        clearEditor();
        editorExpanded = true;
        showStatus("Create a reusable SSH profile or connect without saving.", false);
        nameField.forceActiveFocus();
    }

    function editProfile(profile) {
        editorExpanded = true;
        editingProfileId = profile.id;
        nameField.text = profile.name;
        groupField.text = profile.group;
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
        if (controller.saveHostProfile(editingProfileId, nameField.text, hostField.text, portNumber(), usernameField.text, authenticationToken(), keyPathField.text, passphraseRequiredBox.checked, groupField.text)) {
            clearEditor();
            editorExpanded = false;
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
        const started = authenticationToken() === "private-key" ? controller.connectPrivateKey(hostField.text, portNumber(), usernameField.text, keyPathField.text, secret) : controller.connectPassword(hostField.text, portNumber(), usernameField.text, secret);
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
        if (controller.connectHostProfile(profile.id, "")) {
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
        if (controller.connectHostProfile(profileId, secret)) {
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
            width: Math.min(1040, scrollView.availableWidth - 56)
            spacing: 16

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: "Host Vault"
                        color: pane.textColor
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textTitle
                        font.weight: Font.DemiBold
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "Keep connection details organized without storing passwords or private-key passphrases."
                        color: pane.mutedColor
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textBody
                    }
                }

                ActionButton {
                    id: newHostButton

                    text: "+  New host"
                    Accessible.name: "Create a new SSH host profile"
                    variant: "primary"
                    onClicked: pane.beginNewProfile()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 10

                AppTextField {
                    id: searchField

                    Layout.fillWidth: true
                    placeholderText: "Search hosts, groups, users, or addresses"
                    accessibleName: "Search saved SSH hosts"
                }

                Text {
                    text: pane.filteredProfileCount + (pane.filteredProfileCount === 1 ? " profile" : " profiles")
                    color: pane.mutedColor
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textLabel
                }
            }

            StatePanel {
                Layout.fillWidth: true
                visible: pane.controller.hostProfiles.length === 0
                heading: "No saved hosts yet"
                description: "Add a host below to make future connections one click away."
                centered: true
            }

            StatePanel {
                Layout.fillWidth: true
                visible: pane.controller.hostProfiles.length > 0 && pane.filteredProfileCount === 0
                heading: "No matching hosts"
                description: "Try another host name, group, user, or address."
                centered: true
            }

            Repeater {
                model: pane.filteredGroups

                delegate: ColumnLayout {
                    id: profileGroup

                    required property var modelData

                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        text: profileGroup.modelData.name
                        color: pane.mutedColor
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.6
                    }

                    Repeater {
                        model: profileGroup.modelData.profiles

                        delegate: Rectangle {
                            id: profileCard

                            required property var modelData

                            Layout.fillWidth: true
                            implicitHeight: profileRow.implicitHeight + 24
                            radius: Theme.radiusControl
                            color: cardHover.hovered ? Theme.controlHover : pane.raisedColor
                            border.color: cardHover.hovered ? Theme.borderStrong : pane.borderColor

                            HoverHandler {
                                id: cardHover
                            }

                            RowLayout {
                                id: profileRow

                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 8

                                Rectangle {
                                    Layout.preferredWidth: 34
                                    Layout.preferredHeight: 34
                                    radius: 8
                                    color: Theme.selectedBackground

                                    Text {
                                        anchors.centerIn: parent
                                        text: ">"
                                        color: pane.accentColor
                                        font.family: Theme.terminalFont
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
                                        font.family: Theme.uiFont
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: profileCard.modelData.username + "@" + profileCard.modelData.host + ":" + profileCard.modelData.port
                                        color: pane.mutedColor
                                        elide: Text.ElideMiddle
                                        font.family: Theme.terminalFont
                                        font.pixelSize: 11
                                    }
                                }

                                Rectangle {
                                    Layout.preferredWidth: authenticationLabel.implicitWidth + 16
                                    Layout.preferredHeight: 24
                                    radius: 12
                                    color: Theme.controlBackground
                                    border.color: Theme.border

                                    Text {
                                        id: authenticationLabel
                                        anchors.centerIn: parent
                                        text: profileCard.modelData.authentication === "password" ? "Password" : "Key"
                                        color: Theme.textSoft
                                        font.family: Theme.uiFont
                                        font.pixelSize: Theme.textLabel
                                    }
                                }

                                ActionButton {
                                    text: "Connect"
                                    Accessible.name: "Connect to " + profileCard.modelData.name
                                    variant: "primary"
                                    onClicked: pane.connectSaved(profileCard.modelData)
                                }

                                ActionButton {
                                    text: "Edit"
                                    Accessible.name: "Edit " + profileCard.modelData.name
                                    onClicked: pane.editProfile(profileCard.modelData)
                                }

                                ActionButton {
                                    text: "Copy"
                                    Accessible.name: "Copy " + profileCard.modelData.name
                                    onClicked: {
                                        if (pane.controller.duplicateHostProfile(profileCard.modelData.id)) {
                                            pane.showStatus("Profile copied. Credentials were not copied because they are never stored.", false);
                                        } else {
                                            pane.showStatus("The profile could not be copied.", true);
                                        }
                                    }
                                }

                                ActionButton {
                                    id: deleteProfileButton

                                    text: "Delete"
                                    Accessible.name: "Delete " + profileCard.modelData.name
                                    onClicked: {
                                        pane.pendingDeleteId = profileCard.modelData.id;
                                        pane.pendingDeleteName = profileCard.modelData.name;
                                        deleteDialog.openFrom(deleteProfileButton);
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 10
                implicitHeight: editorColumn.implicitHeight + 40
                visible: pane.editorExpanded
                radius: Theme.radiusPanel
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
                            font.family: Theme.uiFont
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        Text {
                            text: authenticationBox.currentIndex === 0 ? "Private-key authentication" : "Password authentication"
                            color: pane.mutedColor
                            font.family: Theme.uiFont
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
                        AppTextField {
                            id: nameField
                            Layout.fillWidth: true
                            placeholderText: "Home server"
                            Accessible.name: "Profile name"
                            selectByMouse: true
                        }

                        Label {
                            text: "Group"
                            color: pane.textColor
                        }
                        AppTextField {
                            id: groupField
                            Layout.fillWidth: true
                            placeholderText: "Personal, Work, Lab…"
                            Accessible.name: "SSH profile group"
                            selectByMouse: true
                        }

                        Label {
                            text: "Host"
                            color: pane.textColor
                        }
                        AppTextField {
                            id: hostField
                            Layout.fillWidth: true
                            placeholderText: "server.example.com or 192.0.2.10"
                            Accessible.name: "SSH host"
                            selectByMouse: true
                        }

                        Label {
                            text: "Port"
                            color: pane.textColor
                        }
                        AppTextField {
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
                        AppTextField {
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
                                id: authenticationDelegate

                                required property int index
                                required property var modelData

                                width: authenticationBox.width
                                text: modelData
                                highlighted: authenticationBox.highlightedIndex === index

                                contentItem: Text {
                                    text: authenticationDelegate.text
                                    color: pane.textColor
                                    verticalAlignment: Text.AlignVCenter
                                    font.family: Theme.uiFont
                                    font.pixelSize: 13
                                }

                                background: Rectangle {
                                    color: authenticationDelegate.highlighted ? Theme.selectedHover : Theme.controlBackground
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
                                    color: Theme.floatingBackground
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
                        AppTextField {
                            id: keyPathField
                            Layout.fillWidth: true
                            visible: authenticationBox.currentIndex === 0
                            text: pane.controller.defaultPrivateKeyPath
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
                        AppTextField {
                            id: credentialField
                            Layout.fillWidth: true
                            visible: authenticationBox.currentIndex === 1 || passphraseRequiredBox.checked
                            placeholderText: authenticationBox.currentIndex === 0 ? "Private-key passphrase" : "SSH password"
                            echoMode: TextInput.Password
                            Accessible.name: authenticationBox.currentIndex === 0 ? "Private-key passphrase" : "SSH password"
                            selectByMouse: true
                        }
                    }

                    StatusMessage {
                        id: statusText

                        Layout.fillWidth: true
                        kind: pane.statusIsError ? "error" : "success"
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        ActionButton {
                            text: "Cancel"
                            Accessible.name: "Close host profile editor"
                            onClicked: {
                                pane.clearEditor();
                                pane.editorExpanded = false;
                                pane.showStatus("Profile editor closed.", false);
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        ActionButton {
                            text: "Save profile"
                            Accessible.name: "Save SSH profile"
                            onClicked: pane.saveProfile()
                        }

                        ActionButton {
                            text: "Connect"
                            Accessible.name: "Connect to SSH host"
                            variant: "primary"
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
                font.family: Theme.uiFont
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }

            Text {
                Layout.preferredWidth: 360
                text: "Authenticate to \"" + pane.pendingConnectName + "\". This credential is kept only for this connection attempt."
                color: pane.mutedColor
                wrapMode: Text.WordWrap
                font.family: Theme.uiFont
                font.pixelSize: 12
            }

            AppTextField {
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

                ActionButton {
                    text: "Cancel"
                    onClicked: credentialDialog.close()
                }

                ActionButton {
                    id: connectSavedButton
                    text: "Connect"
                    enabled: savedCredentialField.text.length > 0
                    variant: "primary"
                    onClicked: pane.connectPendingSaved()
                }
            }
        }
    }

    ConfirmationDialog {
        id: deleteDialog

        heading: "Delete saved host?"
        description: "Remove \"" + pane.pendingDeleteName + "\" from this device? This does not change the remote server or trusted host keys."
        acceptText: "Delete"
        destructive: true
        onAccepted: {
            if (pane.controller.deleteHostProfile(pane.pendingDeleteId)) {
                pane.showStatus("Profile deleted.", false);
            } else {
                pane.showStatus("The profile could not be deleted.", true);
            }
            focusRestoreItem = newHostButton;
            pane.pendingDeleteId = "";
            pane.pendingDeleteName = "";
        }
        onRejected: {
            pane.pendingDeleteId = "";
            pane.pendingDeleteName = "";
        }
    }
}
