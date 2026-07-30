pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: pane

    objectName: "hostConnectionPane"
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
    property string expandedActionsProfileId: ""
    property bool statusIsError: false
    property bool editorExpanded: controller.hostProfiles.length === 0
    readonly property bool compactLayout: width < Theme.narrowWindowWidth
    readonly property int contentInset: compactLayout ? Theme.pageInsetCompact : Theme.pageInset
    readonly property int profileCardColumns: compactLayout ? 1 : (width < 920 ? 2 : 3)
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
    palette.windowText: textColor
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
            const searchable = [profile.name, groupName, profile.username, profile.host, String(profile.port), profile.authentication].join(" ").toLocaleLowerCase();
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
            result.push({
                name: groupName,
                profiles: groups[groupName]
            });
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
        Qt.callLater(nameField.forceActiveFocus);
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
        Qt.callLater(nameField.forceActiveFocus);
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

    function connectSaved(profile, sourceItem) {
        if (profile.authentication === "password" || profile.privateKeyPassphraseRequired) {
            pendingConnectId = profile.id;
            pendingConnectName = profile.name;
            pendingConnectAuthentication = profile.authentication;
            credentialDialog.focusRestoreItem = sourceItem;
            savedCredentialField.text = "";
            credentialDialog.open();
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
        anchors.rightMargin: 8
        contentWidth: availableWidth
        contentHeight: contentColumn.implicitHeight + 76

        ColumnLayout {
            id: contentColumn

            objectName: "hostContentColumn"
            x: Math.max(pane.contentInset, (scrollView.availableWidth - width) / 2)
            y: pane.compactLayout ? 24 : 38
            width: Math.max(0, Math.min(1040, scrollView.availableWidth - (pane.contentInset * 2)))
            spacing: Theme.spacingSection

            GridLayout {
                Layout.fillWidth: true
                columns: pane.compactLayout ? 1 : 2
                columnSpacing: Theme.spacingRelated
                rowSpacing: Theme.spacingRelated

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingDense

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

                    objectName: "hostNew"
                    Layout.fillWidth: pane.compactLayout
                    Layout.alignment: pane.compactLayout ? Qt.AlignLeft : Qt.AlignRight
                    text: "New host"
                    iconName: "plus"
                    accessibleName: "Create a new SSH host profile"
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

                    objectName: "hostSearch"
                    Layout.fillWidth: true
                    placeholderText: "Search hosts, groups, users, or addresses"
                    accessibleName: "Search saved SSH hosts"
                }

                Text {
                    visible: !pane.compactLayout
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

                    Flow {
                        id: profileFlow

                        readonly property int columns: pane.profileCardColumns
                        readonly property real cardWidth: Math.max(0, (width - (spacing * (columns - 1))) / columns)

                        Layout.fillWidth: true
                        Layout.preferredHeight: childrenRect.height
                        spacing: Theme.spacingRelated

                        Repeater {
                            model: profileGroup.modelData.profiles

                            delegate: Rectangle {
                                id: profileCard

                                objectName: "savedHostCard"
                                required property var modelData
                                readonly property bool actionsExpanded: pane.expandedActionsProfileId === modelData.id

                                width: profileFlow.cardWidth
                                height: 24 + profileHeaderRow.implicitHeight + profilePrimaryActionsRow.implicitHeight + Theme.spacingControl + (profileActionsReveal.reveal * (profileActionsRow.implicitHeight + Theme.spacingControl))
                                radius: Theme.radiusControl
                                color: cardHover.hovered ? Theme.controlHover : pane.raisedColor
                                border.color: cardHover.hovered ? Theme.borderStrong : pane.borderColor

                                Behavior on color {
                                    ColorAnimation {
                                        duration: Theme.motionFast
                                    }
                                }

                                Behavior on height {
                                    NumberAnimation {
                                        duration: Theme.motionMedium
                                        easing.type: Easing.OutCubic
                                    }
                                }

                                HoverHandler {
                                    id: cardHover
                                }

                                ColumnLayout {
                                    id: profileContent

                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: Theme.spacingControl

                                    RowLayout {
                                        id: profileHeaderRow

                                        Layout.fillWidth: true
                                        spacing: Theme.spacingControl

                                        Rectangle {
                                            Layout.preferredWidth: 36
                                            Layout.preferredHeight: 36
                                            radius: Theme.radiusControl
                                            color: Theme.selectedBackground

                                            AppIcon {
                                                anchors.centerIn: parent
                                                width: 18
                                                height: 18
                                                name: "terminal"
                                                color: pane.accentColor
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
                                                font.pixelSize: Theme.textBody
                                                font.weight: Font.DemiBold
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                text: profileCard.modelData.username + "@" + profileCard.modelData.host + ":" + profileCard.modelData.port
                                                color: pane.mutedColor
                                                elide: Text.ElideMiddle
                                                font.family: Theme.terminalFont
                                                font.pixelSize: Theme.textLabel
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
                                    }

                                    RowLayout {
                                        id: profilePrimaryActionsRow

                                        Layout.fillWidth: true
                                        spacing: Theme.spacingControl

                                        Item {
                                            Layout.fillWidth: true
                                        }

                                        ActionButton {
                                            id: connectProfileButton

                                            objectName: "savedHostConnectAction"
                                            Layout.preferredWidth: 104
                                            text: "Connect"
                                            accessibleName: "Connect to " + profileCard.modelData.name
                                            variant: "primary"
                                            onClicked: pane.connectSaved(profileCard.modelData, connectProfileButton)
                                        }

                                        ActionButton {
                                            objectName: "savedHostMoreAction"
                                            text: profileCard.actionsExpanded ? "Less" : "More"
                                            accessibleName: (profileCard.actionsExpanded ? "Hide" : "Show") + " actions for " + profileCard.modelData.name
                                            onClicked: pane.expandedActionsProfileId = profileCard.actionsExpanded ? "" : profileCard.modelData.id
                                        }
                                    }

                                    Item {
                                        id: profileActionsReveal

                                        objectName: "savedHostActionsReveal"
                                        property real reveal: profileCard.actionsExpanded ? 1.0 : 0.0

                                        Layout.fillWidth: true
                                        Layout.preferredHeight: profileActionsRow.implicitHeight * reveal
                                        visible: reveal > 0.001
                                        enabled: profileCard.actionsExpanded
                                        clip: true

                                        Behavior on reveal {
                                            NumberAnimation {
                                                duration: Theme.motionMedium
                                                easing.type: Easing.OutCubic
                                            }
                                        }

                                        RowLayout {
                                            id: profileActionsRow

                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            opacity: profileActionsReveal.reveal
                                            spacing: Theme.spacingControl

                                            ActionButton {
                                                Layout.fillWidth: true
                                                text: "Edit"
                                                accessibleName: "Edit " + profileCard.modelData.name
                                                onClicked: pane.editProfile(profileCard.modelData)
                                            }

                                            ActionButton {
                                                Layout.fillWidth: true
                                                text: "Copy"
                                                accessibleName: "Copy " + profileCard.modelData.name
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

                                                Layout.fillWidth: true
                                                text: "Delete"
                                                accessibleName: "Delete " + profileCard.modelData.name
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
                    }
                }
            }

            Rectangle {
                id: profileEditor

                readonly property real naturalHeight: editorColumn.implicitHeight + 40
                property real reveal: pane.editorExpanded ? 1.0 : 0.0

                Layout.fillWidth: true
                Layout.topMargin: 10
                Layout.preferredHeight: naturalHeight * reveal
                implicitHeight: naturalHeight
                visible: reveal > 0.001
                enabled: pane.editorExpanded
                opacity: reveal
                radius: Theme.radiusPanel
                color: pane.raisedColor
                border.color: pane.borderColor

                Behavior on reveal {
                    NumberAnimation {
                        duration: Theme.motionMedium
                        easing.type: Easing.OutCubic
                    }
                }

                transform: Translate {
                    y: (1.0 - profileEditor.reveal) * Theme.motionDistanceSmall
                }

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
                        objectName: "hostEditorGrid"
                        Layout.fillWidth: true
                        columns: pane.compactLayout ? 1 : 2
                        columnSpacing: 14
                        rowSpacing: 10

                        Label {
                            text: "Profile name"
                            color: pane.textColor
                        }
                        AppTextField {
                            id: nameField
                            objectName: "hostName"
                            Layout.fillWidth: true
                            placeholderText: "Home server"
                            accessibleName: "Profile name"
                            selectByMouse: true
                        }

                        Label {
                            text: "Group"
                            color: pane.textColor
                        }
                        AppTextField {
                            id: groupField
                            objectName: "hostGroup"
                            Layout.fillWidth: true
                            placeholderText: "Personal, Work, Lab…"
                            accessibleName: "SSH profile group"
                            selectByMouse: true
                        }

                        Label {
                            text: "Host"
                            color: pane.textColor
                        }
                        AppTextField {
                            id: hostField
                            objectName: "hostAddress"
                            Layout.fillWidth: true
                            placeholderText: "server.example.com or 192.0.2.10"
                            accessibleName: "SSH host"
                            selectByMouse: true
                        }

                        Label {
                            text: "Port"
                            color: pane.textColor
                        }
                        AppTextField {
                            id: portField
                            objectName: "hostPort"
                            Layout.fillWidth: true
                            text: "22"
                            inputMethodHints: Qt.ImhDigitsOnly
                            validator: IntValidator {
                                bottom: 1
                                top: 65535
                            }
                            accessibleName: "SSH port"
                            selectByMouse: true
                        }

                        Label {
                            text: "Username"
                            color: pane.textColor
                        }
                        AppTextField {
                            id: usernameField
                            objectName: "hostUsername"
                            Layout.fillWidth: true
                            placeholderText: "username"
                            accessibleName: "SSH username"
                            selectByMouse: true
                        }

                        Label {
                            text: "Authentication"
                            color: pane.textColor
                        }
                        AppComboBox {
                            id: authenticationBox
                            objectName: "hostAuthentication"
                            Layout.fillWidth: true
                            model: ["Private key", "Password"]
                            accessibleName: "SSH authentication method"
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
                            objectName: "hostKeyPath"
                            Layout.fillWidth: true
                            visible: authenticationBox.currentIndex === 0
                            text: pane.controller.defaultPrivateKeyPath
                            accessibleName: "Private-key file path"
                            selectByMouse: true
                        }

                        Item {
                            visible: !pane.compactLayout && authenticationBox.currentIndex === 0
                            implicitHeight: passphraseRequiredBox.implicitHeight
                        }
                        AppCheckBox {
                            id: passphraseRequiredBox
                            objectName: "hostPassphraseRequired"
                            Layout.fillWidth: true
                            visible: authenticationBox.currentIndex === 0
                            text: "This private key requires a passphrase"
                            accessibleName: "Private key requires a passphrase"
                            onCheckedChanged: credentialField.text = ""
                        }

                        Label {
                            text: authenticationBox.currentIndex === 0 ? "Passphrase" : "Password"
                            color: pane.textColor
                            visible: authenticationBox.currentIndex === 1 || passphraseRequiredBox.checked
                        }
                        AppTextField {
                            id: credentialField
                            objectName: "hostCredential"
                            Layout.fillWidth: true
                            visible: authenticationBox.currentIndex === 1 || passphraseRequiredBox.checked
                            placeholderText: authenticationBox.currentIndex === 0 ? "Private-key passphrase" : "SSH password"
                            echoMode: TextInput.Password
                            accessibleName: authenticationBox.currentIndex === 0 ? "Private-key passphrase" : "SSH password"
                            selectByMouse: true
                        }
                    }

                    StatusMessage {
                        id: statusText

                        Layout.fillWidth: true
                        kind: pane.statusIsError ? "error" : "success"
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: pane.compactLayout ? 1 : 4
                        columnSpacing: Theme.spacingControl
                        rowSpacing: Theme.spacingControl

                        ActionButton {
                            objectName: "hostCancel"
                            Layout.fillWidth: pane.compactLayout
                            text: "Cancel"
                            accessibleName: "Close host profile editor"
                            onClicked: {
                                pane.clearEditor();
                                pane.editorExpanded = false;
                                pane.showStatus("Profile editor closed.", false);
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            visible: !pane.compactLayout
                        }

                        ActionButton {
                            objectName: "hostSave"
                            Layout.fillWidth: pane.compactLayout
                            text: "Save profile"
                            accessibleName: "Save SSH profile"
                            onClicked: pane.saveProfile()
                        }

                        ActionButton {
                            objectName: "hostConnect"
                            Layout.fillWidth: pane.compactLayout
                            text: "Connect"
                            accessibleName: "Connect to SSH host"
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

        property Item focusRestoreItem: null

        anchors.centerIn: parent
        modal: true
        dim: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        padding: 20
        onAboutToShow: Qt.callLater(savedCredentialField.forceActiveFocus)
        onClosed: {
            const restoreItem = focusRestoreItem;
            focusRestoreItem = null;
            savedCredentialField.text = "";
            pane.pendingConnectId = "";
            pane.pendingConnectName = "";
            pane.pendingConnectAuthentication = "";
            if (restoreItem && restoreItem.visible && restoreItem.enabled) {
                Qt.callLater(() => restoreItem.forceActiveFocus());
            }
        }

        enter: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.motionMedium
                easing.type: Easing.OutCubic
            }
        }

        exit: Transition {
            NumberAnimation {
                property: "opacity"
                from: 1
                to: 0
                duration: Theme.motionFast
                easing.type: Easing.InCubic
            }
        }

        Overlay.modal: Rectangle {
            color: Theme.modalScrim
        }

        background: Rectangle {
            radius: Theme.radiusPanel
            color: pane.raisedColor
            border.color: Theme.borderStrong

            transform: Translate {
                y: credentialDialog.visible ? 0 : Theme.motionDistanceSmall

                Behavior on y {
                    NumberAnimation {
                        duration: Theme.motionMedium
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }

        contentItem: ColumnLayout {
            spacing: 14
            Accessible.role: Accessible.Dialog
            Accessible.name: pane.pendingConnectAuthentication === "password" ? "Enter SSH password" : "Enter key passphrase"

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

                objectName: "savedCredentialField"
                Layout.fillWidth: true
                placeholderText: pane.pendingConnectAuthentication === "password" ? "SSH password" : "Private-key passphrase"
                echoMode: TextInput.Password
                accessibleName: placeholderText
                selectByMouse: true
                onAccepted: pane.connectPendingSaved()
            }

            RowLayout {
                Layout.fillWidth: true

                Item {
                    Layout.fillWidth: true
                }

                ActionButton {
                    id: savedCredentialCancel

                    objectName: "savedCredentialCancel"
                    text: "Cancel"
                    accessibleName: "Cancel saved host authentication"
                    KeyNavigation.right: connectSavedButton
                    onClicked: credentialDialog.close()
                }

                ActionButton {
                    id: connectSavedButton

                    objectName: "savedCredentialConnect"
                    text: "Connect"
                    accessibleName: "Connect to saved SSH host"
                    enabled: savedCredentialField.text.length > 0
                    variant: "primary"
                    KeyNavigation.left: savedCredentialCancel
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
