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
    property string pendingForgetId: ""
    property string pendingForgetName: ""
    property string pendingConnectId: ""
    property string pendingConnectName: ""
    property string pendingConnectAuthentication: ""
    property bool nameWasAutoFilled: false
    property bool editingCredentialStored: false
    property var pendingQuickTarget: ({})
    property string quickConnectMessage: ""
    property bool quickConnectMessageIsError: false
    property string expandedActionsProfileId: ""
    property bool statusIsError: false
    property bool editorExpanded: false
    readonly property bool compactLayout: width < Theme.narrowWindowWidth
    readonly property int contentInset: compactLayout ? Theme.pageInsetCompact : Theme.pageInset
    readonly property int profileCardColumns: scrollView.availableWidth < 700 ? 1 : (scrollView.availableWidth < 1050 ? 2 : 3)
    readonly property var filteredGroups: buildFilteredGroups(controller.hostProfiles, searchField.text)
    readonly property int filteredProfileCount: {
        let count = 0;
        for (const group of filteredGroups) {
            count += group.profiles.length;
        }
        return count;
    }

    signal connectionStarted
    signal securitySettingsRequested

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

    function formatRecentConnection(timestamp) {
        return Qt.formatDateTime(new Date(Number(timestamp)), "yyyy-MM-dd HH:mm");
    }

    function sectionCollapsed(sectionId) {
        return searchField.text.trim().length === 0 && controller.collapsedHostSections.indexOf(sectionId) >= 0;
    }

    function toggleSection(sectionId) {
        controller.setHostSectionCollapsed(sectionId, !sectionCollapsed(sectionId));
    }

    function openQuickConnect(sourceItem) {
        const parsed = controller.parseQuickConnectTarget(quickConnectTarget.text);
        if (!parsed.valid) {
            quickConnectMessage = parsed.error;
            quickConnectMessageIsError = true;
            quickConnectTarget.forceActiveFocus();
            return;
        }
        pendingQuickTarget = parsed;
        quickConnectMessage = "";
        quickConnectMessageIsError = false;
        quickAuthentication.currentIndex = 1;
        quickKeyPath.text = controller.defaultPrivateKeyPath;
        quickPassphraseRequired.checked = false;
        quickCredential.text = "";
        quickSaveProfile.checked = false;
        quickProfileName.text = parsed.username + "@" + parsed.host;
        quickGroup.text = "";
        quickDialogStatus.text = "";
        quickConnectDialog.focusRestoreItem = sourceItem;
        quickConnectDialog.open();
    }

    function connectQuickTarget() {
        const secret = quickCredential.text;
        quickCredential.text = "";
        const authentication = quickAuthentication.currentIndex === 0 ? "private-key" : "password";
        const started = controller.connectQuick(quickConnectTarget.text, authentication, quickKeyPath.text, quickPassphraseRequired.checked, secret, quickSaveProfile.checked, quickProfileName.text, quickGroup.text);
        if (started) {
            quickConnectDialog.close();
            quickConnectTarget.text = "";
            connectionStarted();
        } else {
            quickDialogStatus.kind = "error";
            quickDialogStatus.text = qsTr("The quick connection could not be started. Check authentication and required fields.");
        }
    }

    function buildFilteredGroups(profiles, searchText) {
        const query = searchText.trim().toLocaleLowerCase();
        const groups = {};
        for (const profile of profiles) {
            const groupName = profile.group.trim().length > 0 ? profile.group.trim() : qsTr("Ungrouped");
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
            showStatus(qsTr("Complete every required field."), true);
            return false;
        }
        const port = portNumber();
        if (port < 1 || port > 65535) {
            showStatus(qsTr("Port must be between 1 and 65535."), true);
            return false;
        }
        if (requireCredential && (!privateKey || passphraseRequiredBox.checked) && credentialField.text.length === 0) {
            showStatus(privateKey ? qsTr("Enter the private-key passphrase.") : qsTr("Enter the SSH password."), true);
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
        usernameField.text = "root";
        authenticationBox.currentIndex = 1;
        keyPathField.text = controller.defaultPrivateKeyPath;
        passphraseRequiredBox.checked = false;
        credentialField.text = "";
        credentialField.passwordVisible = false;
        rememberCredentialSwitch.checked = true;
        nameWasAutoFilled = false;
        editingCredentialStored = false;
    }

    function dismissEditor(announce) {
        clearEditor();
        editorExpanded = false;
        if (announce) {
            showStatus(qsTr("Profile editor closed."), false);
        }
    }

    function refreshEditingCredential() {
        if (!editorExpanded || editingProfileId.length === 0 || !editingCredentialStored) {
            return;
        }
        if (controller.effectiveCredentialStorage === "portable" && controller.portableVaultLocked) {
            return;
        }
        const secret = controller.readHostCredential(editingProfileId);
        if (secret.length > 0) {
            credentialField.text = secret;
            return;
        }
        if (controller.credentialOperationError.length > 0) {
            showStatus(controller.credentialOperationError, true);
        }
    }

    function beginNewProfile() {
        clearEditor();
        editorExpanded = true;
        showStatus(qsTr("Create a reusable SSH profile or connect now."), false);
        Qt.callLater(nameField.forceActiveFocus);
    }

    Component.onCompleted: {
        if (controller.hostProfiles.length === 0) {
            clearEditor();
        }
    }

    Connections {
        target: pane.controller

        function onCredentialVaultChanged() {
            if (pane.editorExpanded && pane.editingCredentialStored && credentialField.text.length === 0) {
                Qt.callLater(pane.refreshEditingCredential);
            }
        }
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
        rememberCredentialSwitch.checked = true;
        nameWasAutoFilled = false;
        editingCredentialStored = profile.credentialStored;
        showStatus(qsTr("Editing \"%1\".").arg(profile.name), false);
        Qt.callLater(pane.refreshEditingCredential);
        Qt.callLater(nameField.forceActiveFocus);
    }

    function saveProfile() {
        statusText.text = "";
        if (!validate(false, false)) {
            return;
        }
        if (controller.saveHostProfileWithCredential(editingProfileId, nameField.text, hostField.text, portNumber(), usernameField.text, authenticationToken(), keyPathField.text, passphraseRequiredBox.checked, groupField.text, credentialField.text, rememberCredentialSwitch.checked)) {
            clearEditor();
            editorExpanded = false;
            showStatus(qsTr("Profile and credential preferences saved."), false);
        } else {
            showStatus(controller.credentialOperationError.length > 0 ? controller.credentialOperationError : qsTr("The profile could not be saved."), true);
        }
    }

    function connectCurrent() {
        statusText.text = "";
        if (!validate(false, true)) {
            return;
        }
        const secret = credentialField.text;
        credentialField.text = "";
        const started = controller.saveAndConnectHostProfile(editingProfileId, nameField.text, hostField.text, portNumber(), usernameField.text, authenticationToken(), keyPathField.text, passphraseRequiredBox.checked, groupField.text, secret, rememberCredentialSwitch.checked);
        if (started) {
            clearEditor();
            editorExpanded = false;
            connectionStarted();
        } else {
            showStatus(controller.credentialOperationError.length > 0 ? controller.credentialOperationError : qsTr("The profile could not be saved or connected."), true);
        }
    }

    function connectSaved(profile, sourceItem) {
        if (profile.credentialStored) {
            if (controller.effectiveCredentialStorage === "portable" && controller.portableVaultLocked) {
                pendingConnectId = profile.id;
                pendingConnectName = profile.name;
                pendingConnectAuthentication = profile.authentication;
                portableUnlockDialog.focusRestoreItem = sourceItem;
                portableUnlockPassword.text = "";
                portableUnlockStatus.text = "";
                portableUnlockDialog.open();
                return;
            }
            if (controller.connectHostProfile(profile.id, "")) {
                connectionStarted();
            } else {
                showStatus(controller.credentialOperationError.length > 0 ? controller.credentialOperationError : qsTr("The saved profile could not be connected."), true);
            }
            return;
        }
        if (profile.authentication === "password" || profile.privateKeyPassphraseRequired) {
            pendingConnectId = profile.id;
            pendingConnectName = profile.name;
            pendingConnectAuthentication = profile.authentication;
            credentialDialog.focusRestoreItem = sourceItem;
            savedCredentialField.text = "";
            savedCredentialRemember.checked = true;
            credentialDialog.open();
            return;
        }
        if (controller.connectHostProfile(profile.id, "")) {
            connectionStarted();
        } else {
            showStatus(qsTr("The saved profile could not be connected."), true);
        }
    }

    function connectPendingSaved() {
        if (savedCredentialField.text.length === 0) {
            return;
        }
        const profileId = pendingConnectId;
        const secret = savedCredentialField.text;
        let started = false;
        if (savedCredentialRemember.checked) {
            started = controller.saveHostCredential(profileId, secret) && controller.connectHostProfile(profileId, "");
        } else {
            started = controller.connectHostProfile(profileId, secret);
        }
        if (started) {
            savedCredentialField.text = "";
            credentialDialog.close();
            connectionStarted();
        } else {
            showStatus(controller.credentialOperationError.length > 0 ? controller.credentialOperationError : qsTr("The saved profile could not be connected."), true);
        }
    }

    ScrollView {
        id: scrollView

        objectName: "hostMasterScroll"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: profileEditor.width + (8 * (1.0 - profileEditor.reveal))
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
                        text: qsTr("Hosts")
                        color: pane.textColor
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textTitle
                        font.weight: Font.DemiBold
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Search, connect, and organize SSH hosts from one workspace.")
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
                    text: qsTr("New host")
                    iconName: "plus"
                    accessibleName: qsTr("Create a new SSH host profile")
                    variant: "primary"
                    onClicked: pane.beginNewProfile()
                }
            }

            SectionCard {
                objectName: "quickConnectCard"
                Layout.fillWidth: true
                heading: qsTr("Quick connect")

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingControl

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Connect without creating a saved profile. Use user@host, user@host:port, or user@[IPv6]:port.")
                        color: pane.mutedColor
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingControl

                        AppTextField {
                            id: quickConnectTarget

                            objectName: "quickConnectTarget"
                            Layout.fillWidth: true
                            placeholderText: "user@host[:port]"
                            accessibleName: qsTr("Quick connect SSH target")
                            selectByMouse: true
                            onTextEdited: {
                                pane.quickConnectMessage = "";
                                pane.quickConnectMessageIsError = false;
                            }
                            onAccepted: pane.openQuickConnect(quickConnectTarget)
                        }

                        ActionButton {
                            id: quickConnectAction

                            objectName: "quickConnectAction"
                            text: qsTr("Connect")
                            accessibleName: qsTr("Configure quick SSH connection")
                            variant: "primary"
                            onClicked: pane.openQuickConnect(quickConnectAction)
                        }
                    }

                    StatusMessage {
                        Layout.fillWidth: true
                        text: pane.quickConnectMessage
                        kind: pane.quickConnectMessageIsError ? "error" : "info"
                    }
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
                    placeholderText: qsTr("Search hosts, groups, users, or addresses")
                    accessibleName: qsTr("Search saved SSH hosts")
                }

                Text {
                    visible: !pane.compactLayout
                    text: qsTr("%n profile(s)", "", pane.filteredProfileCount)
                    color: pane.mutedColor
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textLabel
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: pane.controller.recentHostProfiles.length > 0
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingControl

                    ToolButton {
                        id: recentSectionToggle

                        Layout.fillWidth: true
                        implicitHeight: 28
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        Accessible.name: pane.sectionCollapsed("recent") ? qsTr("Expand recent connections") : qsTr("Collapse recent connections")
                        onClicked: pane.toggleSection("recent")

                        contentItem: RowLayout {
                            spacing: 6

                            AppIcon {
                                Layout.preferredWidth: 14
                                Layout.preferredHeight: 14
                                name: "chevron-down"
                                color: pane.mutedColor
                                rotation: pane.sectionCollapsed("recent") ? -90 : 0
                                Behavior on rotation {
                                    NumberAnimation {
                                        duration: Theme.motionFast
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("RECENT CONNECTIONS")
                                color: pane.mutedColor
                                font.family: Theme.uiFont
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.6
                            }
                            Text {
                                text: pane.controller.recentHostProfiles.length
                                color: pane.mutedColor
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textLabel
                            }
                        }
                        background: Rectangle {
                            radius: Theme.radiusSmall
                            color: recentSectionToggle.hovered ? Theme.controlHover : "transparent"
                            border.color: recentSectionToggle.activeFocus ? Theme.focus : "transparent"
                            border.width: recentSectionToggle.activeFocus ? 2 : 0
                        }
                    }

                    ToolButton {
                        id: clearRecentButton

                        implicitWidth: 28
                        implicitHeight: 28
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        Accessible.name: qsTr("Clear recent connections")
                        onClicked: pane.controller.clearRecentHostProfiles()
                        contentItem: AppIcon {
                            name: "trash"
                            color: clearRecentButton.hovered || clearRecentButton.activeFocus ? Theme.text : pane.mutedColor
                        }
                        background: Rectangle {
                            radius: Theme.radiusSmall
                            color: clearRecentButton.hovered ? Theme.controlHover : "transparent"
                            border.color: clearRecentButton.activeFocus ? Theme.focus : "transparent"
                            border.width: clearRecentButton.activeFocus ? 2 : 0
                        }
                        AppToolTip {
                            text: qsTr("Clear recent connections")
                        }
                    }
                }

                Flow {
                    id: recentProfileFlow

                    readonly property int columns: pane.profileCardColumns
                    readonly property real cardWidth: Math.max(0, (width - (spacing * (columns - 1))) / columns)

                    objectName: "recentConnectionsFlow"
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? childrenRect.height : 0
                    visible: !pane.sectionCollapsed("recent")
                    spacing: Theme.spacingRelated

                    Repeater {
                        model: pane.controller.recentHostProfiles

                        delegate: Rectangle {
                            id: recentProfileCard

                            required property var modelData

                            width: recentProfileFlow.cardWidth
                            height: 104
                            radius: Theme.radiusControl
                            color: recentCardHover.hovered ? Theme.controlHover : pane.raisedColor
                            border.color: recentCardHover.hovered ? Theme.borderStrong : pane.borderColor

                            Behavior on color {
                                ColorAnimation {
                                    duration: Theme.motionFast
                                }
                            }

                            HoverHandler {
                                id: recentCardHover
                            }

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 3

                                RowLayout {
                                    Layout.fillWidth: true

                                    Text {
                                        Layout.fillWidth: true
                                        text: recentProfileCard.modelData.name
                                        color: pane.textColor
                                        elide: Text.ElideRight
                                        font.family: Theme.uiFont
                                        font.pixelSize: Theme.textBody
                                        font.weight: Font.DemiBold
                                    }

                                    ActionButton {
                                        id: recentConnectButton

                                        objectName: "recentHostConnectAction"
                                        text: qsTr("Connect")
                                        accessibleName: qsTr("Reconnect to %1").arg(recentProfileCard.modelData.name)
                                        variant: "primary"
                                        onClicked: pane.connectSaved(recentProfileCard.modelData, recentConnectButton)
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: recentProfileCard.modelData.username + "@" + recentProfileCard.modelData.host + ":" + recentProfileCard.modelData.port
                                    color: pane.mutedColor
                                    elide: Text.ElideMiddle
                                    font.family: Theme.terminalFont
                                    font.pixelSize: Theme.textLabel
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("Last connected %1").arg(pane.formatRecentConnection(recentProfileCard.modelData.lastConnectedUtcMs))
                                    color: Theme.textSubtle
                                    elide: Text.ElideRight
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textCompact
                                }
                            }
                        }
                    }
                }
            }

            StatePanel {
                Layout.fillWidth: true
                visible: pane.controller.hostProfiles.length === 0
                heading: qsTr("No saved hosts yet")
                description: qsTr("Select New host to make future connections one click away.")
                centered: true
            }

            StatePanel {
                Layout.fillWidth: true
                visible: pane.controller.hostProfiles.length > 0 && pane.filteredProfileCount === 0
                heading: qsTr("No matching hosts")
                description: qsTr("Try another host name, group, user, or address.")
                centered: true
            }

            Repeater {
                model: pane.filteredGroups

                delegate: ColumnLayout {
                    id: profileGroup

                    required property var modelData

                    Layout.fillWidth: true
                    spacing: 8

                    ToolButton {
                        id: groupSectionToggle

                        Layout.fillWidth: true
                        implicitHeight: 28
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        Accessible.name: pane.sectionCollapsed("group:" + profileGroup.modelData.name) ? qsTr("Expand %1").arg(profileGroup.modelData.name) : qsTr("Collapse %1").arg(profileGroup.modelData.name)
                        onClicked: pane.toggleSection("group:" + profileGroup.modelData.name)

                        contentItem: RowLayout {
                            spacing: 6
                            AppIcon {
                                Layout.preferredWidth: 14
                                Layout.preferredHeight: 14
                                name: "chevron-down"
                                color: pane.mutedColor
                                rotation: pane.sectionCollapsed("group:" + profileGroup.modelData.name) ? -90 : 0
                                Behavior on rotation {
                                    NumberAnimation {
                                        duration: Theme.motionFast
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: profileGroup.modelData.name
                                color: pane.mutedColor
                                font.family: Theme.uiFont
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.6
                            }
                            Text {
                                text: profileGroup.modelData.profiles.length
                                color: pane.mutedColor
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textLabel
                            }
                        }
                        background: Rectangle {
                            radius: Theme.radiusSmall
                            color: groupSectionToggle.hovered ? Theme.controlHover : "transparent"
                            border.color: groupSectionToggle.activeFocus ? Theme.focus : "transparent"
                            border.width: groupSectionToggle.activeFocus ? 2 : 0
                        }
                    }

                    Flow {
                        id: profileFlow

                        readonly property int columns: pane.profileCardColumns
                        readonly property real cardWidth: Math.max(0, (width - (spacing * (columns - 1))) / columns)

                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? childrenRect.height : 0
                        visible: !pane.sectionCollapsed("group:" + profileGroup.modelData.name)
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
                                                text: profileCard.modelData.authentication === "password" ? qsTr("Password") : qsTr("Key")
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
                                            text: qsTr("Connect")
                                            accessibleName: qsTr("Connect to %1").arg(profileCard.modelData.name)
                                            variant: "primary"
                                            onClicked: pane.connectSaved(profileCard.modelData, connectProfileButton)
                                        }

                                        ActionButton {
                                            objectName: "savedHostMoreAction"
                                            text: profileCard.actionsExpanded ? qsTr("Less") : qsTr("More")
                                            accessibleName: profileCard.actionsExpanded ? qsTr("Hide actions for %1").arg(profileCard.modelData.name) : qsTr("Show actions for %1").arg(profileCard.modelData.name)
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
                                                text: qsTr("Edit")
                                                accessibleName: qsTr("Edit %1").arg(profileCard.modelData.name)
                                                onClicked: pane.editProfile(profileCard.modelData)
                                            }

                                            ActionButton {
                                                Layout.fillWidth: true
                                                text: qsTr("Copy")
                                                accessibleName: qsTr("Copy %1").arg(profileCard.modelData.name)
                                                onClicked: {
                                                    if (pane.controller.duplicateHostProfile(profileCard.modelData.id)) {
                                                        pane.showStatus(qsTr("Profile copied without its saved credential."), false);
                                                    } else {
                                                        pane.showStatus(qsTr("The profile could not be copied."), true);
                                                    }
                                                }
                                            }

                                            ActionButton {
                                                id: deleteProfileButton

                                                Layout.fillWidth: true
                                                text: qsTr("Delete")
                                                accessibleName: qsTr("Delete %1").arg(profileCard.modelData.name)
                                                onClicked: {
                                                    pane.pendingDeleteId = profileCard.modelData.id;
                                                    pane.pendingDeleteName = profileCard.modelData.name;
                                                    deleteDialog.openFrom(deleteProfileButton);
                                                }
                                            }

                                            ActionButton {
                                                id: forgetCredentialButton

                                                Layout.fillWidth: true
                                                visible: profileCard.modelData.credentialStored
                                                text: qsTr("Forget secret")
                                                accessibleName: qsTr("Forget saved credential for %1").arg(profileCard.modelData.name)
                                                onClicked: {
                                                    pane.pendingForgetId = profileCard.modelData.id;
                                                    pane.pendingForgetName = profileCard.modelData.name;
                                                    forgetCredentialDialog.openFrom(forgetCredentialButton);
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

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 0

                Rectangle {
                    id: profileEditor

                    property real reveal: pane.editorExpanded ? 1.0 : 0.0
                    readonly property real targetWidth: pane.compactLayout ? pane.width : Math.min(560, Math.max(440, pane.width * 0.52))

                    objectName: "hostDetailPane"
                    parent: pane
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    width: targetWidth * reveal
                    z: 20
                    visible: reveal > 0.001
                    enabled: pane.editorExpanded
                    opacity: reveal
                    clip: true
                    color: pane.raisedColor
                    border.color: pane.borderColor

                    Behavior on reveal {
                        NumberAnimation {
                            duration: Theme.motionMedium
                            easing.type: Easing.OutCubic
                        }
                    }

                    ScrollView {
                        id: editorScroll

                        anchors.fill: parent
                        anchors.rightMargin: 6
                        clip: true
                        contentWidth: availableWidth
                        contentHeight: editorColumn.implicitHeight + 40

                        ColumnLayout {
                            id: editorColumn

                            x: 20
                            y: 20
                            width: Math.max(0, editorScroll.availableWidth - 40)
                            spacing: 14

                            RowLayout {
                                Layout.fillWidth: true

                                Text {
                                    text: pane.editingProfileId.length > 0 ? qsTr("Edit profile") : qsTr("New connection")
                                    color: pane.textColor
                                    font.family: Theme.uiFont
                                    font.pixelSize: 16
                                    font.weight: Font.DemiBold
                                }

                                Item {
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: authenticationBox.currentIndex === 0 ? qsTr("Private-key authentication") : qsTr("Password authentication")
                                    color: pane.mutedColor
                                    font.family: Theme.uiFont
                                    font.pixelSize: 11
                                }

                                ActionButton {
                                    text: qsTr("Close")
                                    accessibleName: qsTr("Close host profile editor")
                                    onClicked: pane.dismissEditor(false)
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingControl
                                visible: pane.controller.effectiveCredentialStorage === "portable" && (!pane.controller.portableVaultInitialized || pane.controller.portableVaultLocked)

                                StatusMessage {
                                    Layout.fillWidth: true
                                    text: !pane.controller.portableVaultInitialized ? qsTr("Create the portable vault before saving credentials.") : qsTr("The portable vault is locked. Unlock it before saving a new credential.")
                                }

                                ActionButton {
                                    objectName: "hostOpenCredentialSecurity"
                                    text: qsTr("Open Security")
                                    accessibleName: qsTr("Open credential security settings")
                                    onClicked: pane.securitySettingsRequested()
                                }
                            }

                            GridLayout {
                                objectName: "hostEditorGrid"
                                Layout.fillWidth: true
                                columns: pane.compactLayout ? 1 : 2
                                columnSpacing: 14
                                rowSpacing: 10

                                Label {
                                    text: qsTr("Profile name")
                                    color: pane.textColor
                                }
                                AppTextField {
                                    id: nameField
                                    objectName: "hostName"
                                    Layout.fillWidth: true
                                    placeholderText: hostField.text.trim().length > 0 ? hostField.text.trim() : qsTr("Defaults to the host name")
                                    accessibleName: qsTr("Profile name")
                                    selectByMouse: true
                                    onActiveFocusChanged: {
                                        if (activeFocus && pane.nameWasAutoFilled) {
                                            selectAll();
                                        }
                                    }
                                    onTextEdited: pane.nameWasAutoFilled = false

                                    MouseArea {
                                        anchors.fill: parent
                                        enabled: pane.nameWasAutoFilled
                                        acceptedButtons: Qt.LeftButton
                                        cursorShape: Qt.IBeamCursor
                                        preventStealing: true
                                        onPressed: nameField.forceActiveFocus(Qt.MouseFocusReason)
                                        onReleased: {
                                            nameField.selectAll();
                                        }
                                    }
                                }

                                Label {
                                    text: qsTr("Group")
                                    color: pane.textColor
                                }
                                EditableSuggestionField {
                                    id: groupField
                                    objectName: "hostGroup"
                                    Layout.fillWidth: true
                                    model: pane.controller.hostProfileGroups
                                    placeholderText: qsTr("Personal, Work, Lab…")
                                    accessibleName: qsTr("SSH profile group")
                                }

                                Label {
                                    text: qsTr("Host")
                                    color: pane.textColor
                                }
                                AppTextField {
                                    id: hostField
                                    objectName: "hostAddress"
                                    Layout.fillWidth: true
                                    placeholderText: "server.example.com or 192.0.2.10"
                                    accessibleName: qsTr("SSH host")
                                    selectByMouse: true
                                    onTextEdited: {
                                        if (nameField.text.trim().length === 0 || pane.nameWasAutoFilled) {
                                            nameField.text = text.trim();
                                            pane.nameWasAutoFilled = text.trim().length > 0;
                                        }
                                    }
                                }

                                Label {
                                    text: qsTr("Port")
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
                                    accessibleName: qsTr("SSH port")
                                    selectByMouse: true
                                }

                                Label {
                                    text: qsTr("Username")
                                    color: pane.textColor
                                }
                                AppTextField {
                                    id: usernameField
                                    objectName: "hostUsername"
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("username")
                                    accessibleName: qsTr("SSH username")
                                    selectByMouse: true
                                }

                                Label {
                                    text: qsTr("Authentication")
                                    color: pane.textColor
                                }
                                AppComboBox {
                                    id: authenticationBox
                                    objectName: "hostAuthentication"
                                    Layout.fillWidth: true
                                    model: [qsTr("Private key"), qsTr("Password")]
                                    accessibleName: qsTr("SSH authentication method")
                                    onCurrentIndexChanged: {
                                        credentialField.text = "";
                                        if (currentIndex === 1) {
                                            passphraseRequiredBox.checked = false;
                                        }
                                    }
                                }

                                Label {
                                    text: qsTr("Private key")
                                    color: pane.textColor
                                    visible: authenticationBox.currentIndex === 0
                                }
                                AppTextField {
                                    id: keyPathField
                                    objectName: "hostKeyPath"
                                    Layout.fillWidth: true
                                    visible: authenticationBox.currentIndex === 0
                                    text: pane.controller.defaultPrivateKeyPath
                                    accessibleName: qsTr("Private-key file path")
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
                                    text: qsTr("This private key requires a passphrase")
                                    accessibleName: qsTr("Private key requires a passphrase")
                                    onCheckedChanged: credentialField.text = ""
                                }

                                Label {
                                    text: authenticationBox.currentIndex === 0 ? qsTr("Passphrase") : qsTr("Password")
                                    color: pane.textColor
                                    visible: authenticationBox.currentIndex === 1 || passphraseRequiredBox.checked
                                }
                                AppTextField {
                                    id: credentialField
                                    objectName: "hostCredential"
                                    Layout.fillWidth: true
                                    visible: authenticationBox.currentIndex === 1 || passphraseRequiredBox.checked
                                    placeholderText: authenticationBox.currentIndex === 0 ? qsTr("Private-key passphrase") : qsTr("SSH password")
                                    passwordRevealable: true
                                    accessibleName: authenticationBox.currentIndex === 0 ? qsTr("Private-key passphrase") : qsTr("SSH password")
                                    selectByMouse: true
                                }

                                Item {
                                    visible: !pane.compactLayout && credentialField.visible
                                    implicitHeight: rememberCredentialSwitch.implicitHeight
                                }
                                AppSwitch {
                                    id: rememberCredentialSwitch

                                    objectName: "hostRememberCredential"
                                    Layout.fillWidth: true
                                    visible: credentialField.visible
                                    checked: true
                                    text: pane.editingCredentialStored && credentialField.text.length === 0 ? qsTr("Keep saved credential") : qsTr("Save credential securely")
                                    accessibleName: text
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
                                    text: qsTr("Cancel")
                                    accessibleName: qsTr("Close host profile editor")
                                    onClicked: pane.dismissEditor(true)
                                }

                                Item {
                                    Layout.fillWidth: true
                                    visible: !pane.compactLayout
                                }

                                ActionButton {
                                    objectName: "hostSave"
                                    Layout.fillWidth: pane.compactLayout
                                    text: qsTr("Save profile")
                                    accessibleName: qsTr("Save SSH profile")
                                    onClicked: pane.saveProfile()
                                }

                                ActionButton {
                                    objectName: "hostConnect"
                                    Layout.fillWidth: pane.compactLayout
                                    text: qsTr("Connect")
                                    accessibleName: qsTr("Connect to SSH host")
                                    variant: "primary"
                                    onClicked: pane.connectCurrent()
                                }
                            }
                        }
                    }
                }

                Item {
                    id: editorDismissRegion

                    objectName: "hostDetailDismissRegion"
                    parent: pane
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    width: Math.max(0, pane.width - profileEditor.width)
                    visible: pane.editorExpanded && width > 0
                    enabled: visible
                    z: 19

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.ArrowCursor
                        onClicked: pane.dismissEditor(false)
                    }
                }
            }
        }
    }

    Dialog {
        id: quickConnectDialog

        objectName: "quickConnectDialog"
        property Item focusRestoreItem: null

        anchors.centerIn: parent
        width: Math.min(520, Math.max(0, parent ? parent.width - 48 : 520))
        height: Math.min(560, Math.max(0, parent ? parent.height - 48 : 560))
        modal: true
        dim: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        padding: 20
        onAboutToShow: Qt.callLater(quickAuthentication.forceActiveFocus)
        onClosed: {
            const restoreItem = focusRestoreItem;
            focusRestoreItem = null;
            quickCredential.text = "";
            quickDialogStatus.text = "";
            pane.pendingQuickTarget = ({});
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
                y: quickConnectDialog.visible ? 0 : Theme.motionDistanceSmall

                Behavior on y {
                    NumberAnimation {
                        duration: Theme.motionMedium
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }

        contentItem: ScrollView {
            id: quickConnectScroll

            clip: true
            contentWidth: availableWidth
            contentHeight: quickConnectContent.implicitHeight
            Accessible.role: Accessible.Dialog
            Accessible.name: qsTr("Quick SSH connection")

            ColumnLayout {
                id: quickConnectContent

                width: quickConnectScroll.availableWidth
                spacing: 12

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Quick connect")
                    color: pane.textColor
                    font.family: Theme.uiFont
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }

                Text {
                    Layout.fillWidth: true
                    text: pane.pendingQuickTarget.valid ? pane.pendingQuickTarget.username + "@" + pane.pendingQuickTarget.host + ":" + pane.pendingQuickTarget.port : ""
                    color: pane.mutedColor
                    elide: Text.ElideMiddle
                    font.family: Theme.terminalFont
                    font.pixelSize: Theme.textLabel
                }

                Label {
                    text: qsTr("Authentication")
                    color: pane.textColor
                }

                AppComboBox {
                    id: quickAuthentication

                    objectName: "quickAuthentication"
                    Layout.fillWidth: true
                    model: [qsTr("Private key"), qsTr("Password")]
                    accessibleName: qsTr("Quick connect authentication method")
                    onCurrentIndexChanged: {
                        quickCredential.text = "";
                        if (currentIndex === 1) {
                            quickPassphraseRequired.checked = false;
                        }
                    }
                }

                AppTextField {
                    id: quickKeyPath

                    objectName: "quickKeyPath"
                    Layout.fillWidth: true
                    visible: quickAuthentication.currentIndex === 0
                    placeholderText: qsTr("Private-key file")
                    accessibleName: qsTr("Quick connect private-key file")
                    selectByMouse: true
                }

                AppCheckBox {
                    id: quickPassphraseRequired

                    objectName: "quickPassphraseRequired"
                    Layout.fillWidth: true
                    visible: quickAuthentication.currentIndex === 0
                    text: qsTr("This private key requires a passphrase")
                    accessibleName: qsTr("Quick connect private key requires a passphrase")
                    onCheckedChanged: quickCredential.text = ""
                }

                AppTextField {
                    id: quickCredential

                    objectName: "quickCredential"
                    Layout.fillWidth: true
                    visible: quickAuthentication.currentIndex === 1 || quickPassphraseRequired.checked
                    placeholderText: quickAuthentication.currentIndex === 1 ? qsTr("SSH password") : qsTr("Private-key passphrase")
                    echoMode: TextInput.Password
                    accessibleName: placeholderText
                    selectByMouse: true
                    onAccepted: {
                        if (quickConnectConfirm.enabled) {
                            pane.connectQuickTarget();
                        }
                    }
                }

                AppCheckBox {
                    id: quickSaveProfile

                    objectName: "quickSaveProfile"
                    Layout.fillWidth: true
                    text: qsTr("Save as a reusable host profile")
                    accessibleName: qsTr("Save quick connection as host profile")
                }

                AppTextField {
                    id: quickProfileName

                    objectName: "quickProfileName"
                    Layout.fillWidth: true
                    visible: quickSaveProfile.checked
                    placeholderText: qsTr("Profile name")
                    accessibleName: qsTr("Quick connection profile name")
                    selectByMouse: true
                }

                AppTextField {
                    id: quickGroup

                    objectName: "quickGroup"
                    Layout.fillWidth: true
                    visible: quickSaveProfile.checked
                    placeholderText: qsTr("Group (optional)")
                    accessibleName: qsTr("Quick connection profile group")
                    selectByMouse: true
                }

                StatusMessage {
                    id: quickDialogStatus

                    Layout.fillWidth: true
                }

                RowLayout {
                    Layout.fillWidth: true

                    Item {
                        Layout.fillWidth: true
                    }

                    ActionButton {
                        id: quickConnectCancel

                        objectName: "quickConnectCancel"
                        text: qsTr("Cancel")
                        accessibleName: qsTr("Cancel quick SSH connection")
                        KeyNavigation.right: quickConnectConfirm
                        onClicked: quickConnectDialog.close()
                    }

                    ActionButton {
                        id: quickConnectConfirm

                        objectName: "quickConnectConfirm"
                        text: qsTr("Connect")
                        accessibleName: qsTr("Start quick SSH connection")
                        enabled: (quickAuthentication.currentIndex === 1 || quickKeyPath.text.trim().length > 0) && (quickAuthentication.currentIndex === 0 || quickCredential.text.length > 0) && (!quickPassphraseRequired.checked || quickCredential.text.length > 0) && (!quickSaveProfile.checked || quickProfileName.text.trim().length > 0)
                        variant: "primary"
                        KeyNavigation.left: quickConnectCancel
                        onClicked: pane.connectQuickTarget()
                    }
                }
            }
        }
    }

    Dialog {
        id: portableUnlockDialog

        property Item focusRestoreItem: null

        anchors.centerIn: parent
        modal: true
        dim: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        padding: 20
        onAboutToShow: Qt.callLater(portableUnlockPassword.forceActiveFocus)
        onClosed: {
            const restoreItem = focusRestoreItem;
            focusRestoreItem = null;
            portableUnlockPassword.text = "";
            portableUnlockStatus.text = "";
            pane.pendingConnectId = "";
            pane.pendingConnectName = "";
            pane.pendingConnectAuthentication = "";
            if (restoreItem && restoreItem.visible && restoreItem.enabled) {
                Qt.callLater(() => restoreItem.forceActiveFocus());
            }
        }

        Overlay.modal: Rectangle {
            color: Theme.modalScrim
        }

        background: Rectangle {
            radius: Theme.radiusPanel
            color: Theme.floatingBackground
            border.color: Theme.borderStrong
        }

        contentItem: ColumnLayout {
            spacing: 14
            Accessible.role: Accessible.Dialog
            Accessible.name: qsTr("Unlock portable credential vault")

            Text {
                text: qsTr("Unlock portable vault")
                color: pane.textColor
                font.family: Theme.uiFont
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }

            Text {
                Layout.preferredWidth: 380
                text: qsTr("Enter the portable-vault master password to connect to \"%1\".").arg(pane.pendingConnectName)
                color: pane.mutedColor
                wrapMode: Text.WordWrap
                font.family: Theme.uiFont
                font.pixelSize: 12
            }

            AppTextField {
                id: portableUnlockPassword

                objectName: "portableUnlockPassword"
                Layout.fillWidth: true
                placeholderText: qsTr("Master password (minimum 8 characters)")
                echoMode: TextInput.Password
                accessibleName: qsTr("Portable vault master password")
                selectByMouse: true
                onAccepted: portableUnlockAction.clicked()
            }

            StatusMessage {
                id: portableUnlockStatus

                Layout.fillWidth: true
                kind: "error"
            }

            RowLayout {
                Layout.fillWidth: true

                Item {
                    Layout.fillWidth: true
                }

                ActionButton {
                    text: qsTr("Cancel")
                    accessibleName: qsTr("Cancel portable vault unlock")
                    onClicked: portableUnlockDialog.close()
                }

                ActionButton {
                    id: portableUnlockAction

                    text: qsTr("Unlock and connect")
                    accessibleName: qsTr("Unlock portable vault and connect")
                    enabled: portableUnlockPassword.text.length >= 8
                    variant: "primary"
                    onClicked: {
                        if (!pane.controller.unlockPortableCredentialVault(portableUnlockPassword.text)) {
                            portableUnlockStatus.text = pane.controller.credentialOperationError;
                            portableUnlockPassword.selectAll();
                            return;
                        }
                        portableUnlockPassword.text = "";
                        if (pane.controller.connectHostProfile(pane.pendingConnectId, "")) {
                            portableUnlockDialog.close();
                            pane.connectionStarted();
                        } else {
                            portableUnlockStatus.text = pane.controller.credentialOperationError.length > 0 ? pane.controller.credentialOperationError : qsTr("The saved profile could not be connected.");
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
            Accessible.name: pane.pendingConnectAuthentication === "password" ? qsTr("Enter SSH password") : qsTr("Enter key passphrase")

            Text {
                text: pane.pendingConnectAuthentication === "password" ? qsTr("Enter SSH password") : qsTr("Enter key passphrase")
                color: pane.textColor
                font.family: Theme.uiFont
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }

            Text {
                Layout.preferredWidth: 360
                text: qsTr("Authenticate to \"%1\". You can save this credential in the active secure store.").arg(pane.pendingConnectName)
                color: pane.mutedColor
                wrapMode: Text.WordWrap
                font.family: Theme.uiFont
                font.pixelSize: 12
            }

            AppTextField {
                id: savedCredentialField

                objectName: "savedCredentialField"
                Layout.fillWidth: true
                placeholderText: pane.pendingConnectAuthentication === "password" ? qsTr("SSH password") : qsTr("Private-key passphrase")
                echoMode: TextInput.Password
                accessibleName: placeholderText
                selectByMouse: true
                onAccepted: pane.connectPendingSaved()
            }

            AppSwitch {
                id: savedCredentialRemember

                objectName: "savedCredentialRemember"
                Layout.fillWidth: true
                checked: true
                text: qsTr("Save this credential securely")
                accessibleName: qsTr("Save this credential in the active secure store")
            }

            RowLayout {
                Layout.fillWidth: true

                Item {
                    Layout.fillWidth: true
                }

                ActionButton {
                    id: savedCredentialCancel

                    objectName: "savedCredentialCancel"
                    text: qsTr("Cancel")
                    accessibleName: qsTr("Cancel saved host authentication")
                    KeyNavigation.right: connectSavedButton
                    onClicked: credentialDialog.close()
                }

                ActionButton {
                    id: connectSavedButton

                    objectName: "savedCredentialConnect"
                    text: qsTr("Connect")
                    accessibleName: qsTr("Connect to saved SSH host")
                    enabled: savedCredentialField.text.length > 0
                    variant: "primary"
                    KeyNavigation.left: savedCredentialCancel
                    onClicked: pane.connectPendingSaved()
                }
            }
        }
    }

    ConfirmationDialog {
        id: forgetCredentialDialog

        heading: qsTr("Forget saved credential?")
        description: qsTr("Remove the password or key passphrase for \"%1\" from the active secure store? The host profile remains.").arg(pane.pendingForgetName)
        acceptText: qsTr("Forget credential")
        destructive: true
        onAccepted: {
            if (pane.controller.forgetHostCredential(pane.pendingForgetId)) {
                pane.showStatus(qsTr("Saved credential removed."), false);
            } else {
                pane.showStatus(pane.controller.credentialOperationError.length > 0 ? pane.controller.credentialOperationError : qsTr("The saved credential could not be removed."), true);
            }
            focusRestoreItem = newHostButton;
            pane.pendingForgetId = "";
            pane.pendingForgetName = "";
        }
        onRejected: {
            pane.pendingForgetId = "";
            pane.pendingForgetName = "";
        }
    }

    ConfirmationDialog {
        id: deleteDialog

        heading: qsTr("Delete saved host?")
        description: qsTr("Remove \"%1\" and its credential from the active store? Copies deliberately retained in another store can be cleared in Settings > Security. This does not change the remote server or trusted host keys.").arg(pane.pendingDeleteName)
        acceptText: qsTr("Delete")
        destructive: true
        onAccepted: {
            if (pane.controller.deleteHostProfile(pane.pendingDeleteId)) {
                pane.showStatus(qsTr("Profile deleted."), false);
            } else {
                pane.showStatus(qsTr("The profile could not be deleted."), true);
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
