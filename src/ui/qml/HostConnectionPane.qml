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
    property bool pendingConnectNeedsHostCredential: false
    property bool pendingConnectNeedsProxyCredential: false
    property bool nameWasAutoFilled: false
    property bool editingCredentialStored: false
    property bool editingProxyCredentialStored: false
    property var jumpProfileIds: []
    property var pendingQuickTarget: ({})
    property string quickConnectMessage: ""
    property bool quickConnectMessageIsError: false
    property bool statusIsError: false
    property bool editorExpanded: false
    property bool advancedExpanded: false
    readonly property bool compactLayout: width < Theme.narrowWindowWidth
    readonly property int contentInset: compactLayout ? 8 : 12
    readonly property int profileCardColumns: scrollView.availableWidth < 540 ? 1 : (scrollView.availableWidth < 840 ? 2 : (scrollView.availableWidth < 1140 ? 3 : 4))
    readonly property var filteredGroups: buildFilteredGroups(controller.hostProfiles, quickConnectTarget.text)
    readonly property int filteredProfileCount: {
        let count = 0;
        for (const group of filteredGroups) {
            count += group.profiles.length;
        }
        return count;
    }

    signal connectionStarted
    signal securitySettingsRequested
    signal localTerminalRequested

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
        if (authenticationBox.currentIndex === 0) {
            return "private-key";
        }
        return authenticationBox.currentIndex === 1 ? "password" : "agent";
    }

    function authenticationSummary() {
        if (authenticationBox.currentIndex === 0) {
            return qsTr("Private-key authentication");
        }
        return authenticationBox.currentIndex === 1 ? qsTr("Password authentication") : qsTr("SSH agent authentication");
    }

    function pendingCredentialTitle() {
        if (pendingConnectNeedsHostCredential && pendingConnectNeedsProxyCredential) {
            return qsTr("Enter connection credentials");
        }
        if (pendingConnectNeedsProxyCredential) {
            return qsTr("Enter proxy password");
        }
        return pendingConnectAuthentication === "password" ? qsTr("Enter SSH password") : qsTr("Enter key passphrase");
    }

    function proxyTypeToken() {
        if (proxyTypeBox.currentIndex === 1) {
            return "socks5";
        }
        return proxyTypeBox.currentIndex === 2 ? "http-connect" : "none";
    }

    function proxyOptionsMap() {
        const type = proxyTypeToken();
        if (type === "none") {
            return {
                type: type,
                host: "",
                port: 0,
                username: ""
            };
        }
        const host = proxyHostField.text.trim();
        const port = Number(proxyPortField.text);
        if (host.length === 0 || !Number.isInteger(port) || port < 1 || port > 65535) {
            showStatus(qsTr("Complete the proxy host and use a port between 1 and 65535."), true);
            return null;
        }
        if (proxyUsernameField.text.trim().length === 0 && proxyCredentialField.text.length > 0) {
            showStatus(qsTr("Enter a proxy username before entering its password."), true);
            return null;
        }
        return {
            type: type,
            host: host,
            port: port,
            username: proxyUsernameField.text.trim()
        };
    }

    function routeOptionsMap() {
        return {
            jumpProfileIds: jumpProfileIds.slice(0)
        };
    }

    function availableJumpProfiles() {
        const result = [];
        for (const profile of controller.hostProfiles) {
            if (profile.id !== editingProfileId && jumpProfileIds.indexOf(profile.id) < 0) {
                result.push({
                    id: profile.id,
                    label: profile.name + "  ·  " + profile.username + "@" + profile.host + ":" + profile.port
                });
            }
        }
        return result;
    }

    function jumpProfile(profileId) {
        for (const profile of controller.hostProfiles) {
            if (profile.id === profileId) {
                return profile;
            }
        }
        return null;
    }

    function addJumpProfile() {
        const options = availableJumpProfiles();
        if (jumpProfileIds.length >= 3 || jumpProfileBox.currentIndex < 0 || jumpProfileBox.currentIndex >= options.length) {
            return;
        }
        const updated = jumpProfileIds.slice(0);
        updated.push(options[jumpProfileBox.currentIndex].id);
        jumpProfileIds = updated;
        jumpProfileBox.currentIndex = availableJumpProfiles().length > 0 ? 0 : -1;
    }

    function removeJumpProfile(index) {
        const updated = jumpProfileIds.slice(0);
        updated.splice(index, 1);
        jumpProfileIds = updated;
    }

    function moveJumpProfile(index, delta) {
        const target = index + delta;
        if (target < 0 || target >= jumpProfileIds.length) {
            return;
        }
        const updated = jumpProfileIds.slice(0);
        const value = updated[index];
        updated[index] = updated[target];
        updated[target] = value;
        jumpProfileIds = updated;
    }

    function sessionOptionsMap() {
        const connectionTimeout = Number(connectionTimeoutField.text);
        const keepaliveInterval = Number(keepaliveIntervalField.text);
        const keepaliveThreshold = Number(keepaliveThresholdField.text);
        const startupDelay = Number(startupDelayField.text);
        const reconnectAttempts = Number(reconnectAttemptsField.text);
        const reconnectBackoff = Number(reconnectBackoffField.text);
        if (!Number.isInteger(connectionTimeout) || connectionTimeout < 1 || connectionTimeout > 300) {
            showStatus(qsTr("Connection timeout must be between 1 and 300 seconds."), true);
            return null;
        }
        if (!Number.isInteger(keepaliveInterval) || keepaliveInterval < 0 || keepaliveInterval > 3600) {
            showStatus(qsTr("Keepalive interval must be between 0 and 3600 seconds."), true);
            return null;
        }
        if (!Number.isInteger(keepaliveThreshold) || keepaliveThreshold < 1 || keepaliveThreshold > 10) {
            showStatus(qsTr("Keepalive failure limit must be between 1 and 10."), true);
            return null;
        }
        if (!Number.isInteger(startupDelay) || startupDelay < 0 || startupDelay > 5000) {
            showStatus(qsTr("Startup line delay must be between 0 and 5000 milliseconds."), true);
            return null;
        }
        if (!Number.isInteger(reconnectAttempts) || reconnectAttempts < 1 || reconnectAttempts > 10) {
            showStatus(qsTr("Reconnect attempts must be between 1 and 10."), true);
            return null;
        }
        if (!Number.isInteger(reconnectBackoff) || reconnectBackoff < 250 || reconnectBackoff > 30000) {
            showStatus(qsTr("Reconnect delay must be between 250 and 30000 milliseconds."), true);
            return null;
        }
        const environment = [];
        const lines = environmentField.text.split(/\r?\n/);
        for (const sourceLine of lines) {
            const line = sourceLine.trim();
            if (line.length === 0) {
                continue;
            }
            const separator = line.indexOf("=");
            const name = separator < 0 ? "" : line.slice(0, separator).trim();
            if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(name)) {
                showStatus(qsTr("Environment entries must use NAME=value with a valid variable name."), true);
                return null;
            }
            environment.push({
                name: name,
                value: line.slice(separator + 1)
            });
        }
        return {
            terminalType: terminalTypeField.text.trim(),
            connectionTimeoutSeconds: connectionTimeout,
            keepaliveIntervalSeconds: keepaliveInterval,
            keepaliveFailureThreshold: keepaliveThreshold,
            startupCommand: startupCommandField.text,
            startupCommandMode: startupModeBox.currentIndex === 1 ? "line-delay" : "paste",
            startupLineDelayMilliseconds: startupDelay,
            environment: environment,
            reconnectPolicy: reconnectPolicyBox.currentIndex === 1 ? "transport-failure" : "never",
            reconnectMaximumAttempts: reconnectAttempts,
            reconnectInitialBackoffMilliseconds: reconnectBackoff
        };
    }

    function environmentText(options) {
        if (!options || !options.environment) {
            return "";
        }
        return options.environment.map(variable => variable.name + "=" + variable.value).join("\n");
    }

    function formatRecentConnection(timestamp) {
        return Qt.formatDateTime(new Date(Number(timestamp)), "yyyy-MM-dd HH:mm");
    }

    function sectionCollapsed(sectionId) {
        return quickConnectTarget.text.trim().length === 0 && controller.collapsedHostSections.indexOf(sectionId) >= 0;
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
        const agent = quickAuthentication.currentIndex === 2;
        const secret = agent ? "" : quickCredential.text;
        quickCredential.text = "";
        const authentication = quickAuthentication.currentIndex === 0 ? "private-key" : (quickAuthentication.currentIndex === 1 ? "password" : "agent");
        const started = controller.connectQuick(quickConnectTarget.text, authentication, agent ? "" : quickKeyPath.text, agent ? false : quickPassphraseRequired.checked, secret, quickSaveProfile.checked, quickProfileName.text, quickGroup.text);
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
        const authentication = authenticationToken();
        const privateKey = authentication === "private-key";
        if ((requireName && nameField.text.trim().length === 0) || hostField.text.trim().length === 0 || usernameField.text.trim().length === 0 || (privateKey && keyPathField.text.trim().length === 0) || portField.text.length === 0) {
            showStatus(qsTr("Complete every required field."), true);
            return false;
        }
        const port = portNumber();
        if (port < 1 || port > 65535) {
            showStatus(qsTr("Port must be between 1 and 65535."), true);
            return false;
        }
        const needsCredential = authentication === "password" || (privateKey && passphraseRequiredBox.checked);
        if (requireCredential && needsCredential && credentialField.text.length === 0) {
            showStatus(privateKey ? qsTr("Enter the private-key passphrase.") : qsTr("Enter the SSH password."), true);
            return false;
        }
        if (requireCredential && proxyTypeBox.currentIndex > 0 && proxyUsernameField.text.trim().length > 0 && proxyCredentialField.text.length === 0 && !editingProxyCredentialStored) {
            showStatus(qsTr("Enter the proxy password."), true);
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
        proxyTypeBox.currentIndex = 0;
        proxyHostField.text = "";
        proxyPortField.text = "1080";
        proxyUsernameField.text = "";
        proxyCredentialField.text = "";
        proxyCredentialField.passwordVisible = false;
        rememberProxyCredentialSwitch.checked = true;
        editingProxyCredentialStored = false;
        jumpProfileIds = [];
        terminalTypeField.text = "xterm-256color";
        connectionTimeoutField.text = "10";
        keepaliveIntervalField.text = "0";
        keepaliveThresholdField.text = "3";
        startupCommandField.text = "";
        startupModeBox.currentIndex = 0;
        startupDelayField.text = "100";
        environmentField.text = "";
        reconnectPolicyBox.currentIndex = 0;
        reconnectAttemptsField.text = "3";
        reconnectBackoffField.text = "1000";
        advancedExpanded = false;
    }

    function dismissEditor(announce) {
        clearEditor();
        editorExpanded = false;
        if (announce) {
            showStatus(qsTr("Profile editor closed."), false);
        }
    }

    function refreshEditingCredential() {
        if (!editorExpanded || editingProfileId.length === 0) {
            return;
        }
        if (controller.effectiveCredentialStorage === "portable" && controller.portableVaultLocked) {
            return;
        }
        if (editingCredentialStored && credentialField.text.length === 0) {
            const secret = controller.readHostCredential(editingProfileId);
            if (secret.length > 0) {
                credentialField.text = secret;
            } else if (controller.credentialOperationError.length > 0) {
                showStatus(controller.credentialOperationError, true);
                return;
            }
        }
        if (editingProxyCredentialStored && proxyCredentialField.text.length === 0) {
            const proxySecret = controller.readProxyCredential(editingProfileId);
            if (proxySecret.length > 0) {
                proxyCredentialField.text = proxySecret;
            } else if (controller.credentialOperationError.length > 0) {
                showStatus(controller.credentialOperationError, true);
            }
        }
    }

    function beginNewProfile() {
        forwardingPane.closeEditor();
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
            if (pane.editorExpanded && ((pane.editingCredentialStored && credentialField.text.length === 0) || (pane.editingProxyCredentialStored && proxyCredentialField.text.length === 0))) {
                Qt.callLater(pane.refreshEditingCredential);
            }
        }
    }

    function editProfile(profile) {
        forwardingPane.closeEditor();
        editorExpanded = true;
        editingProfileId = profile.id;
        nameField.text = profile.name;
        groupField.text = profile.group;
        hostField.text = profile.host;
        portField.text = String(profile.port);
        usernameField.text = profile.username;
        authenticationBox.currentIndex = profile.authentication === "password" ? 1 : (profile.authentication === "agent" ? 2 : 0);
        keyPathField.text = profile.privateKeyPath;
        passphraseRequiredBox.checked = profile.privateKeyPassphraseRequired;
        credentialField.text = "";
        rememberCredentialSwitch.checked = true;
        nameWasAutoFilled = false;
        editingCredentialStored = profile.credentialStored;
        const proxy = profile.proxy || {};
        proxyTypeBox.currentIndex = proxy.type === "socks5" ? 1 : (proxy.type === "http-connect" ? 2 : 0);
        proxyHostField.text = proxy.host || "";
        proxyPortField.text = String(proxy.port || 1080);
        proxyUsernameField.text = proxy.username || "";
        proxyCredentialField.text = "";
        rememberProxyCredentialSwitch.checked = true;
        editingProxyCredentialStored = proxy.credentialStored === true;
        jumpProfileIds = profile.jumpProfileIds ? profile.jumpProfileIds.slice(0) : [];
        const options = profile.sessionOptions || {};
        terminalTypeField.text = options.terminalType || "xterm-256color";
        connectionTimeoutField.text = String(options.connectionTimeoutSeconds === undefined ? 10 : options.connectionTimeoutSeconds);
        keepaliveIntervalField.text = String(options.keepaliveIntervalSeconds === undefined ? 0 : options.keepaliveIntervalSeconds);
        keepaliveThresholdField.text = String(options.keepaliveFailureThreshold === undefined ? 3 : options.keepaliveFailureThreshold);
        startupCommandField.text = options.startupCommand || "";
        startupModeBox.currentIndex = options.startupCommandMode === "line-delay" ? 1 : 0;
        startupDelayField.text = String(options.startupLineDelayMilliseconds === undefined ? 100 : options.startupLineDelayMilliseconds);
        environmentField.text = environmentText(options);
        reconnectPolicyBox.currentIndex = options.reconnectPolicy === "transport-failure" ? 1 : 0;
        reconnectAttemptsField.text = String(options.reconnectMaximumAttempts === undefined ? 3 : options.reconnectMaximumAttempts);
        reconnectBackoffField.text = String(options.reconnectInitialBackoffMilliseconds === undefined ? 1000 : options.reconnectInitialBackoffMilliseconds);
        advancedExpanded = false;
        showStatus(qsTr("Editing \"%1\".").arg(profile.name), false);
        Qt.callLater(pane.refreshEditingCredential);
        Qt.callLater(nameField.forceActiveFocus);
    }

    function saveProfile() {
        statusText.text = "";
        if (!validate(false, false)) {
            return;
        }
        const sessionOptions = sessionOptionsMap();
        if (!sessionOptions) {
            advancedExpanded = true;
            return;
        }
        const proxyOptions = proxyOptionsMap();
        if (!proxyOptions) {
            advancedExpanded = true;
            return;
        }
        if (controller.saveHostProfileWithCredential(editingProfileId, nameField.text, hostField.text, portNumber(), usernameField.text, authenticationToken(), keyPathField.text, passphraseRequiredBox.checked, groupField.text, credentialField.text, rememberCredentialSwitch.checked, sessionOptions, proxyOptions, proxyCredentialField.text, rememberProxyCredentialSwitch.checked, routeOptionsMap())) {
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
        const sessionOptions = sessionOptionsMap();
        if (!sessionOptions) {
            advancedExpanded = true;
            return;
        }
        const proxyOptions = proxyOptionsMap();
        if (!proxyOptions) {
            advancedExpanded = true;
            return;
        }
        const secret = credentialField.text;
        const proxySecret = proxyCredentialField.text;
        credentialField.text = "";
        proxyCredentialField.text = "";
        const started = controller.saveAndConnectHostProfile(editingProfileId, nameField.text, hostField.text, portNumber(), usernameField.text, authenticationToken(), keyPathField.text, passphraseRequiredBox.checked, groupField.text, secret, rememberCredentialSwitch.checked, sessionOptions, proxyOptions, proxySecret, rememberProxyCredentialSwitch.checked, routeOptionsMap());
        if (started) {
            clearEditor();
            editorExpanded = false;
            connectionStarted();
        } else {
            showStatus(controller.credentialOperationError.length > 0 ? controller.credentialOperationError : qsTr("The profile could not be saved or connected."), true);
        }
    }

    function connectSaved(profile, sourceItem) {
        if (profile.jumpProfilesReady === false) {
            showStatus(qsTr("Save the required credentials in every jump profile before connecting."), true);
            return;
        }
        const needsHostCredential = (profile.authentication === "password" || profile.privateKeyPassphraseRequired) && !profile.credentialStored;
        const proxy = profile.proxy || {};
        const needsProxyCredential = proxy.type !== "none" && (proxy.username || "").length > 0 && !proxy.credentialStored;
        pendingConnectId = profile.id;
        pendingConnectName = profile.name;
        pendingConnectAuthentication = profile.authentication;
        pendingConnectNeedsHostCredential = needsHostCredential;
        pendingConnectNeedsProxyCredential = needsProxyCredential;

        if (profile.connectionCredentialStored === true) {
            if (controller.effectiveCredentialStorage === "portable" && controller.portableVaultLocked) {
                portableUnlockDialog.focusRestoreItem = sourceItem;
                portableUnlockPassword.text = "";
                portableUnlockStatus.text = "";
                portableUnlockDialog.open();
                return;
            }
        }
        if (needsHostCredential || needsProxyCredential) {
            credentialDialog.focusRestoreItem = sourceItem;
            savedCredentialField.text = "";
            savedProxyCredentialField.text = "";
            savedCredentialRemember.checked = true;
            savedProxyCredentialRemember.checked = true;
            credentialDialog.open();
            return;
        }
        if (controller.connectHostProfile(profile.id, "", "")) {
            connectionStarted();
        } else {
            showStatus(controller.credentialOperationError.length > 0 ? controller.credentialOperationError : qsTr("The saved profile could not be connected."), true);
        }
    }

    function connectPendingSaved() {
        if ((pendingConnectNeedsHostCredential && savedCredentialField.text.length === 0) || (pendingConnectNeedsProxyCredential && savedProxyCredentialField.text.length === 0)) {
            return;
        }
        const profileId = pendingConnectId;
        const secret = savedCredentialField.text;
        const proxySecret = savedProxyCredentialField.text;
        let prepared = true;
        if (pendingConnectNeedsHostCredential && savedCredentialRemember.checked) {
            prepared = controller.saveHostCredential(profileId, secret);
        }
        if (prepared && pendingConnectNeedsProxyCredential && savedProxyCredentialRemember.checked) {
            prepared = controller.saveProxyCredential(profileId, proxySecret);
        }
        const connectionSecret = pendingConnectNeedsHostCredential && !savedCredentialRemember.checked ? secret : "";
        const connectionProxySecret = pendingConnectNeedsProxyCredential && !savedProxyCredentialRemember.checked ? proxySecret : "";
        const started = prepared && controller.connectHostProfile(profileId, connectionSecret, connectionProxySecret);
        if (started) {
            savedCredentialField.text = "";
            savedProxyCredentialField.text = "";
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
        anchors.rightMargin: Math.max(profileEditor.width, forwardingPane.editorWidth) + (8 * (1.0 - Math.max(profileEditor.reveal, forwardingPane.editorReveal)))
        contentWidth: availableWidth
        contentHeight: contentColumn.implicitHeight + 76

        ColumnLayout {
            id: contentColumn

            objectName: "hostContentColumn"
            x: Math.max(pane.contentInset, (scrollView.availableWidth - width) / 2)
            y: pane.contentInset
            width: Math.max(0, scrollView.availableWidth - (pane.contentInset * 2))
            spacing: 10

            RowLayout {
                objectName: "hostCommandRow"
                Layout.fillWidth: true
                spacing: 6

                AppTextField {
                    id: quickConnectTarget

                    objectName: "quickConnectTarget"
                    Layout.fillWidth: true
                    compact: true
                    placeholderText: qsTr("Find a host or enter user@host[:port]")
                    accessibleName: qsTr("Find a saved host or enter a quick SSH target")
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

                ActionButton {
                    objectName: "hostLocalTerminal"
                    visible: !pane.compactLayout
                    text: qsTr("Terminal")
                    iconName: "terminal"
                    accessibleName: qsTr("Open local terminal")
                    onClicked: pane.localTerminalRequested()
                }

                ActionButton {
                    id: newHostButton

                    objectName: "hostNew"
                    text: qsTr("New host")
                    iconName: "plus"
                    accessibleName: qsTr("Create a new SSH host profile")
                    onClicked: pane.beginNewProfile()
                }
            }

            StatusMessage {
                Layout.fillWidth: true
                text: pane.quickConnectMessage
                kind: pane.quickConnectMessageIsError ? "error" : "info"
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
                            border.color: recentSectionToggle.visualFocus ? Theme.focus : "transparent"
                            border.width: recentSectionToggle.visualFocus ? 2 : 0
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
                            color: clearRecentButton.hovered || clearRecentButton.visualFocus ? Theme.text : pane.mutedColor
                        }
                        background: Rectangle {
                            radius: Theme.radiusSmall
                            color: clearRecentButton.hovered ? Theme.controlHover : "transparent"
                            border.color: clearRecentButton.visualFocus ? Theme.focus : "transparent"
                            border.width: clearRecentButton.visualFocus ? 2 : 0
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
                            readonly property string accessibleName: qsTr("Reconnect to %1").arg(modelData.name)

                            width: recentProfileFlow.cardWidth
                            height: 68
                            radius: Theme.radiusControl
                            color: recentCardHover.hovered ? Theme.controlHover : pane.raisedColor
                            border.color: recentProfileCard.activeFocus ? Theme.focus : recentCardHover.hovered ? Theme.borderStrong : pane.borderColor
                            activeFocusOnTab: true
                            Accessible.role: Accessible.Button
                            Accessible.name: accessibleName
                            Keys.onReturnPressed: pane.connectSaved(modelData, recentProfileCard)
                            Keys.onEnterPressed: pane.connectSaved(modelData, recentProfileCard)

                            Behavior on color {
                                ColorAnimation {
                                    duration: Theme.motionFast
                                }
                            }

                            HoverHandler {
                                id: recentCardHover
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 9
                                spacing: 9

                                Rectangle {
                                    Layout.preferredWidth: 38
                                    Layout.preferredHeight: 38
                                    radius: Theme.radiusControl
                                    color: Theme.selectedBackground

                                    AppIcon {
                                        anchors.centerIn: parent
                                        width: 17
                                        height: 17
                                        name: "terminal"
                                        color: pane.accentColor
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1

                                    Text {
                                        Layout.fillWidth: true
                                        text: recentProfileCard.modelData.name
                                        color: pane.textColor
                                        elide: Text.ElideRight
                                        font.family: Theme.uiFont
                                        font.pixelSize: Theme.textBody
                                        font.weight: Font.DemiBold
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: recentProfileCard.modelData.username + "@" + recentProfileCard.modelData.host + ":" + recentProfileCard.modelData.port
                                        color: pane.mutedColor
                                        elide: Text.ElideMiddle
                                        font.family: Theme.terminalFont
                                        font.pixelSize: Theme.textCompact
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

                                AppIcon {
                                    Layout.preferredWidth: 14
                                    Layout.preferredHeight: 14
                                    name: "play"
                                    color: recentCardHover.hovered || recentProfileCard.activeFocus ? pane.accentColor : Theme.textSubtle
                                }
                            }

                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onTapped: pane.connectSaved(recentProfileCard.modelData, recentProfileCard)
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
                            border.color: groupSectionToggle.visualFocus ? Theme.focus : "transparent"
                            border.width: groupSectionToggle.visualFocus ? 2 : 0
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
                                readonly property string accessibleName: qsTr("Connect to %1").arg(modelData.name)

                                width: profileFlow.cardWidth
                                height: 68
                                radius: Theme.radiusControl
                                color: cardHover.hovered ? Theme.controlHover : pane.raisedColor
                                border.color: profileCard.activeFocus ? Theme.focus : cardHover.hovered ? Theme.borderStrong : pane.borderColor
                                activeFocusOnTab: true
                                Accessible.role: Accessible.Button
                                Accessible.name: accessibleName
                                Keys.onReturnPressed: pane.connectSaved(modelData, profileCard)
                                Keys.onEnterPressed: pane.connectSaved(modelData, profileCard)

                                Behavior on color {
                                    ColorAnimation {
                                        duration: Theme.motionFast
                                    }
                                }

                                HoverHandler {
                                    id: cardHover
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 5
                                    spacing: 9

                                    Rectangle {
                                        Layout.preferredWidth: 38
                                        Layout.preferredHeight: 38
                                        radius: Theme.radiusControl
                                        color: Theme.selectedBackground

                                        AppIcon {
                                            anchors.centerIn: parent
                                            width: 17
                                            height: 17
                                            name: "terminal"
                                            color: pane.accentColor
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 1

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
                                            font.pixelSize: Theme.textCompact
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: profileCard.modelData.authentication === "password" ? qsTr("Password") : qsTr("Private key")
                                            color: Theme.textSubtle
                                            elide: Text.ElideRight
                                            font.family: Theme.uiFont
                                            font.pixelSize: Theme.textCompact
                                        }
                                    }

                                    ToolButton {
                                        id: editProfileButton

                                        Layout.preferredWidth: 28
                                        Layout.preferredHeight: 28
                                        hoverEnabled: true
                                        focusPolicy: Qt.StrongFocus
                                        Accessible.name: qsTr("Edit %1").arg(profileCard.modelData.name)
                                        onClicked: pane.editProfile(profileCard.modelData)
                                        contentItem: AppIcon {
                                            name: "edit"
                                            color: editProfileButton.hovered || editProfileButton.visualFocus ? pane.textColor : Theme.textSubtle
                                        }
                                        background: Rectangle {
                                            radius: width / 2
                                            color: editProfileButton.down ? Theme.controlPressed : editProfileButton.hovered ? Theme.controlHover : "transparent"
                                            border.color: editProfileButton.visualFocus ? Theme.focus : "transparent"
                                        }
                                    }

                                    ToolButton {
                                        id: profileMoreButton

                                        objectName: "savedHostMoreAction"
                                        readonly property bool menuVisible: profileMoreMenu.visible
                                        Layout.preferredWidth: 28
                                        Layout.preferredHeight: 28
                                        hoverEnabled: true
                                        focusPolicy: Qt.StrongFocus
                                        Accessible.name: qsTr("More actions for %1").arg(profileCard.modelData.name)
                                        onClicked: profileMoreMenu.open()
                                        Keys.onReturnPressed: click()
                                        Keys.onEnterPressed: click()
                                        contentItem: AppIcon {
                                            name: "more"
                                            color: profileMoreButton.hovered || profileMoreButton.visualFocus || profileMoreMenu.visible ? pane.textColor : Theme.textSubtle
                                        }
                                        background: Rectangle {
                                            radius: width / 2
                                            color: profileMoreButton.down ? Theme.controlPressed : profileMoreButton.hovered ? Theme.controlHover : "transparent"
                                            border.color: profileMoreButton.visualFocus ? Theme.focus : "transparent"
                                        }
                                    }
                                }

                                TapHandler {
                                    acceptedButtons: Qt.LeftButton
                                    onTapped: pane.connectSaved(profileCard.modelData, profileCard)
                                }

                                AppMenu {
                                    id: profileMoreMenu

                                    x: Math.max(0, profileCard.width - width)
                                    y: profileMoreButton.y + profileMoreButton.height

                                    AppMenuItem {
                                        text: qsTr("Edit")
                                        onTriggered: pane.editProfile(profileCard.modelData)
                                    }

                                    AppMenuItem {
                                        text: qsTr("Copy")
                                        onTriggered: {
                                            if (pane.controller.duplicateHostProfile(profileCard.modelData.id)) {
                                                pane.showStatus(qsTr("Profile copied without its saved credential."), false);
                                            } else {
                                                pane.showStatus(qsTr("The profile could not be copied."), true);
                                            }
                                        }
                                    }

                                    AppMenuItem {
                                        visible: profileCard.modelData.credentialStored
                                        text: qsTr("Forget secret")
                                        onTriggered: {
                                            pane.pendingForgetId = profileCard.modelData.id;
                                            pane.pendingForgetName = profileCard.modelData.name;
                                            forgetCredentialDialog.openFrom(profileMoreButton);
                                        }
                                    }

                                    AppMenuSeparator {}

                                    AppMenuItem {
                                        text: qsTr("Delete")
                                        onTriggered: {
                                            pane.pendingDeleteId = profileCard.modelData.id;
                                            pane.pendingDeleteName = profileCard.modelData.name;
                                            deleteDialog.openFrom(profileMoreButton);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            PortForwardingPane {
                id: forwardingPane

                Layout.fillWidth: true
                controller: pane.controller
                overlayParent: pane
                compactLayout: pane.compactLayout
                onEditorOpening: pane.dismissEditor(false)
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 0

                Rectangle {
                    id: profileEditor

                    property real reveal: pane.editorExpanded ? 1.0 : 0.0
                    readonly property real targetWidth: pane.compactLayout ? pane.width : Math.min(460, Math.max(400, pane.width * 0.38))

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

                            x: 14
                            y: 12
                            width: Math.max(0, editorScroll.availableWidth - 28)
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true

                                Text {
                                    text: pane.editingProfileId.length > 0 ? qsTr("Edit profile") : qsTr("New connection")
                                    color: pane.textColor
                                    font.family: Theme.uiFont
                                    font.pixelSize: 15
                                    font.weight: Font.DemiBold
                                }

                                Item {
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: pane.authenticationSummary()
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
                                visible: authenticationBox.currentIndex !== 2 && pane.controller.effectiveCredentialStorage === "portable" && (!pane.controller.portableVaultInitialized || pane.controller.portableVaultLocked)

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
                                    model: [qsTr("Private key"), qsTr("Password"), qsTr("SSH agent")]
                                    accessibleName: qsTr("SSH authentication method")
                                    onCurrentIndexChanged: {
                                        credentialField.text = "";
                                        if (currentIndex !== 0) {
                                            passphraseRequiredBox.checked = false;
                                        }
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    Layout.columnSpan: pane.compactLayout ? 1 : 2
                                    visible: authenticationBox.currentIndex === 2
                                    text: qsTr("Uses identities already loaded in your Windows SSH agent. ztermy never reads the private-key material.")
                                    color: pane.mutedColor
                                    wrapMode: Text.WordWrap
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textLabel
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

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 42
                                    radius: Theme.radiusSmall
                                    color: advancedHeaderMouse.containsMouse ? Theme.controlHover : Theme.controlBackground
                                    border.color: pane.advancedExpanded ? pane.accentColor : pane.borderColor

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 12
                                        anchors.rightMargin: 10
                                        spacing: Theme.spacingControl

                                        Text {
                                            text: qsTr("Advanced SSH options")
                                            color: pane.textColor
                                            font.family: Theme.uiFont
                                            font.pixelSize: Theme.textBody
                                            font.weight: Font.DemiBold
                                        }
                                        Item {
                                            Layout.fillWidth: true
                                        }
                                        Text {
                                            text: pane.advancedExpanded ? "−" : "+"
                                            color: pane.mutedColor
                                            font.family: Theme.uiFont
                                            font.pixelSize: 18
                                        }
                                    }

                                    MouseArea {
                                        id: advancedHeaderMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        Accessible.name: qsTr("Toggle advanced SSH options")
                                        onClicked: pane.advancedExpanded = !pane.advancedExpanded
                                    }
                                }

                                Rectangle {
                                    property real revealHeight: pane.advancedExpanded ? advancedOptions.implicitHeight + 24 : 0

                                    Layout.fillWidth: true
                                    Layout.preferredHeight: revealHeight
                                    clip: true
                                    enabled: pane.advancedExpanded
                                    opacity: pane.advancedExpanded ? 1 : 0
                                    color: "transparent"
                                    border.color: pane.advancedExpanded ? pane.borderColor : "transparent"
                                    radius: Theme.radiusSmall

                                    Behavior on revealHeight {
                                        NumberAnimation {
                                            duration: Theme.motionMedium
                                            easing.type: Easing.OutCubic
                                        }
                                    }

                                    ColumnLayout {
                                        id: advancedOptions
                                        x: 12
                                        y: 12
                                        width: Math.max(0, parent.width - 24)
                                        spacing: 10

                                        Text {
                                            Layout.fillWidth: true
                                            text: qsTr("Jump hosts")
                                            color: pane.textColor
                                            font.family: Theme.uiFont
                                            font.pixelSize: Theme.textBody
                                            font.weight: Font.DemiBold
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: qsTr("Route this connection through up to three saved SSH profiles, in order.")
                                            color: pane.mutedColor
                                            font.family: Theme.uiFont
                                            font.pixelSize: Theme.textLabel
                                            wrapMode: Text.WordWrap
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 6

                                            Repeater {
                                                model: pane.jumpProfileIds

                                                delegate: Rectangle {
                                                    id: jumpHostRow

                                                    required property int index
                                                    required property string modelData
                                                    readonly property var profile: pane.jumpProfile(modelData)

                                                    Layout.fillWidth: true
                                                    implicitHeight: 42
                                                    radius: Theme.radiusSmall
                                                    color: Theme.controlBackground
                                                    border.color: pane.borderColor

                                                    RowLayout {
                                                        anchors.fill: parent
                                                        anchors.leftMargin: 10
                                                        anchors.rightMargin: 6
                                                        spacing: 6

                                                        Text {
                                                            text: String(jumpHostRow.index + 1)
                                                            color: pane.accentColor
                                                            font.family: Theme.uiFont
                                                            font.pixelSize: Theme.textLabel
                                                            font.weight: Font.DemiBold
                                                        }
                                                        ColumnLayout {
                                                            Layout.fillWidth: true
                                                            spacing: 0
                                                            Text {
                                                                Layout.fillWidth: true
                                                                text: jumpHostRow.profile ? jumpHostRow.profile.name : qsTr("Missing profile")
                                                                color: pane.textColor
                                                                elide: Text.ElideRight
                                                                font.family: Theme.uiFont
                                                                font.pixelSize: Theme.textBody
                                                                font.weight: Font.Medium
                                                            }
                                                            Text {
                                                                Layout.fillWidth: true
                                                                text: jumpHostRow.profile ? jumpHostRow.profile.username + "@" + jumpHostRow.profile.host + ":" + jumpHostRow.profile.port : jumpHostRow.modelData
                                                                color: pane.mutedColor
                                                                elide: Text.ElideMiddle
                                                                font.family: Theme.uiFont
                                                                font.pixelSize: Theme.textLabel
                                                            }
                                                        }
                                                        ToolButton {
                                                            enabled: jumpHostRow.index > 0
                                                            Accessible.name: qsTr("Move jump host earlier")
                                                            onClicked: pane.moveJumpProfile(jumpHostRow.index, -1)
                                                            contentItem: AppIcon {
                                                                name: "chevron-up"
                                                                color: parent.enabled ? pane.textColor : Theme.textSubtle
                                                            }
                                                        }
                                                        ToolButton {
                                                            enabled: jumpHostRow.index + 1 < pane.jumpProfileIds.length
                                                            Accessible.name: qsTr("Move jump host later")
                                                            onClicked: pane.moveJumpProfile(jumpHostRow.index, 1)
                                                            contentItem: AppIcon {
                                                                name: "chevron-down"
                                                                color: parent.enabled ? pane.textColor : Theme.textSubtle
                                                            }
                                                        }
                                                        ToolButton {
                                                            Accessible.name: qsTr("Remove jump host")
                                                            onClicked: pane.removeJumpProfile(jumpHostRow.index)
                                                            contentItem: AppIcon {
                                                                name: "close"
                                                                color: pane.textColor
                                                            }
                                                        }
                                                    }
                                                }
                                            }

                                            RowLayout {
                                                Layout.fillWidth: true
                                                visible: pane.jumpProfileIds.length < 3 && pane.availableJumpProfiles().length > 0
                                                spacing: 8

                                                AppComboBox {
                                                    id: jumpProfileBox
                                                    objectName: "hostJumpProfile"
                                                    Layout.fillWidth: true
                                                    model: pane.availableJumpProfiles()
                                                    textRole: "label"
                                                    valueRole: "id"
                                                    currentIndex: model.length > 0 ? 0 : -1
                                                    accessibleName: qsTr("Saved SSH profile to use as a jump host")
                                                }
                                                ActionButton {
                                                    text: qsTr("Add")
                                                    accessibleName: qsTr("Add selected jump host")
                                                    onClicked: pane.addJumpProfile()
                                                }
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                visible: pane.jumpProfileIds.length > 0
                                                text: qsTr("Jump profiles that require credentials must store them before this chain can connect.")
                                                color: pane.mutedColor
                                                wrapMode: Text.WordWrap
                                                font.family: Theme.uiFont
                                                font.pixelSize: Theme.textLabel
                                            }
                                        }

                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 1
                                            color: pane.borderColor
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: qsTr("Connection proxy")
                                            color: pane.textColor
                                            font.family: Theme.uiFont
                                            font.pixelSize: Theme.textBody
                                            font.weight: Font.DemiBold
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: qsTr("Connect directly, or establish the SSH transport through a SOCKS5 or HTTP CONNECT proxy.")
                                            color: pane.mutedColor
                                            font.family: Theme.uiFont
                                            font.pixelSize: Theme.textLabel
                                            wrapMode: Text.WordWrap
                                        }

                                        GridLayout {
                                            Layout.fillWidth: true
                                            columns: pane.compactLayout ? 1 : 2
                                            columnSpacing: 14
                                            rowSpacing: 10

                                            Label {
                                                text: qsTr("Proxy type")
                                                color: pane.textColor
                                            }
                                            AppComboBox {
                                                id: proxyTypeBox
                                                objectName: "hostProxyType"
                                                Layout.fillWidth: true
                                                model: [qsTr("Direct"), qsTr("SOCKS5"), qsTr("HTTP CONNECT")]
                                                accessibleName: qsTr("SSH connection proxy type")
                                                onCurrentIndexChanged: {
                                                    if (currentIndex === 0) {
                                                        proxyCredentialField.text = "";
                                                    }
                                                }
                                            }

                                            Label {
                                                text: qsTr("Proxy host")
                                                color: pane.textColor
                                                visible: proxyTypeBox.currentIndex > 0
                                            }
                                            AppTextField {
                                                id: proxyHostField
                                                objectName: "hostProxyAddress"
                                                Layout.fillWidth: true
                                                visible: proxyTypeBox.currentIndex > 0
                                                placeholderText: qsTr("proxy.example.com or 192.0.2.20")
                                                accessibleName: qsTr("Proxy host")
                                                selectByMouse: true
                                            }

                                            Label {
                                                text: qsTr("Proxy port")
                                                color: pane.textColor
                                                visible: proxyTypeBox.currentIndex > 0
                                            }
                                            AppTextField {
                                                id: proxyPortField
                                                objectName: "hostProxyPort"
                                                Layout.fillWidth: true
                                                visible: proxyTypeBox.currentIndex > 0
                                                text: "1080"
                                                inputMethodHints: Qt.ImhDigitsOnly
                                                validator: IntValidator {
                                                    bottom: 1
                                                    top: 65535
                                                }
                                                accessibleName: qsTr("Proxy port")
                                                selectByMouse: true
                                            }

                                            Label {
                                                text: qsTr("Proxy username")
                                                color: pane.textColor
                                                visible: proxyTypeBox.currentIndex > 0
                                            }
                                            AppTextField {
                                                id: proxyUsernameField
                                                objectName: "hostProxyUsername"
                                                Layout.fillWidth: true
                                                visible: proxyTypeBox.currentIndex > 0
                                                placeholderText: qsTr("Optional")
                                                accessibleName: qsTr("Proxy username")
                                                selectByMouse: true
                                                onTextEdited: {
                                                    if (text.trim().length === 0) {
                                                        proxyCredentialField.text = "";
                                                    }
                                                }
                                            }

                                            Label {
                                                text: qsTr("Proxy password")
                                                color: pane.textColor
                                                visible: proxyTypeBox.currentIndex > 0 && proxyUsernameField.text.trim().length > 0
                                            }
                                            AppTextField {
                                                id: proxyCredentialField
                                                objectName: "hostProxyCredential"
                                                Layout.fillWidth: true
                                                visible: proxyTypeBox.currentIndex > 0 && proxyUsernameField.text.trim().length > 0
                                                placeholderText: qsTr("Proxy password")
                                                passwordRevealable: true
                                                accessibleName: qsTr("Proxy password")
                                                selectByMouse: true
                                            }

                                            Item {
                                                visible: !pane.compactLayout && proxyCredentialField.visible
                                                implicitHeight: rememberProxyCredentialSwitch.implicitHeight
                                            }
                                            AppSwitch {
                                                id: rememberProxyCredentialSwitch
                                                objectName: "hostRememberProxyCredential"
                                                Layout.fillWidth: true
                                                visible: proxyCredentialField.visible
                                                checked: true
                                                text: pane.editingProxyCredentialStored && proxyCredentialField.text.length === 0 ? qsTr("Keep saved proxy credential") : qsTr("Save proxy credential securely")
                                                accessibleName: text
                                            }
                                        }

                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 1
                                            color: pane.borderColor
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: qsTr("Terminal session")
                                            color: pane.textColor
                                            font.family: Theme.uiFont
                                            font.pixelSize: Theme.textBody
                                            font.weight: Font.DemiBold
                                        }

                                        GridLayout {
                                            Layout.fillWidth: true
                                            columns: pane.compactLayout ? 1 : 2
                                            columnSpacing: 14
                                            rowSpacing: 10

                                            Label {
                                                text: qsTr("Terminal type")
                                                color: pane.textColor
                                            }
                                            AppTextField {
                                                id: terminalTypeField
                                                objectName: "hostTerminalType"
                                                Layout.fillWidth: true
                                                text: "xterm-256color"
                                                placeholderText: "xterm-256color"
                                                accessibleName: qsTr("SSH terminal type")
                                                selectByMouse: true
                                            }

                                            Label {
                                                text: qsTr("Connection timeout")
                                                color: pane.textColor
                                            }
                                            AppTextField {
                                                id: connectionTimeoutField
                                                objectName: "hostConnectionTimeout"
                                                Layout.fillWidth: true
                                                text: "10"
                                                placeholderText: qsTr("Seconds")
                                                accessibleName: qsTr("SSH connection timeout in seconds")
                                                inputMethodHints: Qt.ImhDigitsOnly
                                                validator: IntValidator {
                                                    bottom: 1
                                                    top: 300
                                                }
                                                selectByMouse: true
                                                AppToolTip {
                                                    text: qsTr("Applies to TCP, proxy, jump-host, and SSH handshake setup. Authentication keeps its own timeout.")
                                                }
                                            }

                                            Label {
                                                text: qsTr("Keepalive interval")
                                                color: pane.textColor
                                            }
                                            AppTextField {
                                                id: keepaliveIntervalField
                                                objectName: "hostKeepaliveInterval"
                                                Layout.fillWidth: true
                                                text: "0"
                                                placeholderText: qsTr("0 disables keepalive")
                                                accessibleName: qsTr("SSH keepalive interval in seconds")
                                                inputMethodHints: Qt.ImhDigitsOnly
                                                validator: IntValidator {
                                                    bottom: 0
                                                    top: 3600
                                                }
                                                selectByMouse: true
                                            }

                                            Label {
                                                text: qsTr("Failure limit")
                                                color: pane.textColor
                                            }
                                            AppTextField {
                                                id: keepaliveThresholdField
                                                objectName: "hostKeepaliveFailureLimit"
                                                Layout.fillWidth: true
                                                text: "3"
                                                accessibleName: qsTr("SSH keepalive consecutive failure limit")
                                                inputMethodHints: Qt.ImhDigitsOnly
                                                validator: IntValidator {
                                                    bottom: 1
                                                    top: 10
                                                }
                                                selectByMouse: true
                                            }

                                            Label {
                                                text: qsTr("Automatic reconnect")
                                                color: pane.textColor
                                            }
                                            AppComboBox {
                                                id: reconnectPolicyBox
                                                objectName: "hostReconnectPolicy"
                                                Layout.fillWidth: true
                                                model: [qsTr("Never"), qsTr("After transport failures")]
                                                accessibleName: qsTr("SSH automatic reconnect policy")
                                            }

                                            Label {
                                                text: qsTr("Reconnect attempts")
                                                color: pane.textColor
                                                visible: reconnectPolicyBox.currentIndex === 1
                                            }
                                            AppTextField {
                                                id: reconnectAttemptsField
                                                objectName: "hostReconnectAttempts"
                                                Layout.fillWidth: true
                                                visible: reconnectPolicyBox.currentIndex === 1
                                                text: "3"
                                                accessibleName: qsTr("Maximum automatic reconnect attempts")
                                                inputMethodHints: Qt.ImhDigitsOnly
                                                validator: IntValidator {
                                                    bottom: 1
                                                    top: 10
                                                }
                                                selectByMouse: true
                                            }

                                            Label {
                                                text: qsTr("Initial reconnect delay")
                                                color: pane.textColor
                                                visible: reconnectPolicyBox.currentIndex === 1
                                            }
                                            AppTextField {
                                                id: reconnectBackoffField
                                                objectName: "hostReconnectDelay"
                                                Layout.fillWidth: true
                                                visible: reconnectPolicyBox.currentIndex === 1
                                                text: "1000"
                                                placeholderText: qsTr("Milliseconds")
                                                accessibleName: qsTr("Initial automatic reconnect delay in milliseconds")
                                                inputMethodHints: Qt.ImhDigitsOnly
                                                validator: IntValidator {
                                                    bottom: 250
                                                    top: 30000
                                                }
                                                selectByMouse: true
                                            }

                                            Label {
                                                text: qsTr("Startup mode")
                                                color: pane.textColor
                                            }
                                            AppComboBox {
                                                id: startupModeBox
                                                objectName: "hostStartupMode"
                                                Layout.fillWidth: true
                                                model: [qsTr("Send at once"), qsTr("Send line by line")]
                                                accessibleName: qsTr("SSH startup command mode")
                                            }

                                            Label {
                                                text: qsTr("Line delay")
                                                color: pane.textColor
                                                visible: startupModeBox.currentIndex === 1
                                            }
                                            AppTextField {
                                                id: startupDelayField
                                                objectName: "hostStartupLineDelay"
                                                Layout.fillWidth: true
                                                visible: startupModeBox.currentIndex === 1
                                                text: "100"
                                                placeholderText: qsTr("Milliseconds")
                                                accessibleName: qsTr("SSH startup command line delay in milliseconds")
                                                inputMethodHints: Qt.ImhDigitsOnly
                                                validator: IntValidator {
                                                    bottom: 0
                                                    top: 5000
                                                }
                                                selectByMouse: true
                                            }
                                        }

                                        Label {
                                            text: qsTr("Startup command")
                                            color: pane.textColor
                                        }
                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 92
                                            radius: Theme.radiusSmall
                                            color: Theme.controlBackground
                                            border.color: startupCommandField.activeFocus ? Theme.focus : pane.borderColor

                                            ScrollView {
                                                anchors.fill: parent
                                                anchors.margins: 6
                                                clip: true

                                                TextArea {
                                                    id: startupCommandField
                                                    objectName: "hostStartupCommand"
                                                    placeholderText: qsTr("Commands to run after the shell opens")
                                                    color: pane.textColor
                                                    placeholderTextColor: pane.mutedColor
                                                    selectionColor: pane.accentColor
                                                    selectedTextColor: Theme.accentText
                                                    wrapMode: TextEdit.WrapAnywhere
                                                    font.family: Theme.terminalFont
                                                    font.pixelSize: Theme.textBody
                                                    Accessible.name: qsTr("SSH startup command")
                                                    background: null
                                                }
                                            }
                                        }

                                        Label {
                                            text: qsTr("Environment")
                                            color: pane.textColor
                                        }
                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 86
                                            radius: Theme.radiusSmall
                                            color: Theme.controlBackground
                                            border.color: environmentField.activeFocus ? Theme.focus : pane.borderColor

                                            ScrollView {
                                                anchors.fill: parent
                                                anchors.margins: 6
                                                clip: true

                                                TextArea {
                                                    id: environmentField
                                                    objectName: "hostEnvironment"
                                                    placeholderText: qsTr("One NAME=value entry per line")
                                                    color: pane.textColor
                                                    placeholderTextColor: pane.mutedColor
                                                    selectionColor: pane.accentColor
                                                    selectedTextColor: Theme.accentText
                                                    wrapMode: TextEdit.NoWrap
                                                    font.family: Theme.terminalFont
                                                    font.pixelSize: Theme.textBody
                                                    Accessible.name: qsTr("SSH environment variables")
                                                    background: null
                                                }
                                            }
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: qsTr("Environment values are stored with the profile. Do not use this field for secrets.")
                                            color: pane.mutedColor
                                            font.family: Theme.uiFont
                                            font.pixelSize: Theme.textLabel
                                            wrapMode: Text.Wrap
                                        }
                                    }
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
                    model: [qsTr("Private key"), qsTr("Password"), qsTr("SSH agent")]
                    accessibleName: qsTr("Quick connect authentication method")
                    onCurrentIndexChanged: {
                        quickCredential.text = "";
                        if (currentIndex !== 0) {
                            quickPassphraseRequired.checked = false;
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: quickAuthentication.currentIndex === 2
                    text: qsTr("Uses identities already loaded in your Windows SSH agent. ztermy never reads the private-key material.")
                    color: pane.mutedColor
                    wrapMode: Text.WordWrap
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textLabel
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
                        enabled: (quickAuthentication.currentIndex === 2 || quickAuthentication.currentIndex === 1 || quickKeyPath.text.trim().length > 0) && (quickAuthentication.currentIndex === 2 || quickAuthentication.currentIndex === 0 || quickCredential.text.length > 0) && (quickAuthentication.currentIndex === 2 || !quickPassphraseRequired.checked || quickCredential.text.length > 0) && (!quickSaveProfile.checked || quickProfileName.text.trim().length > 0)
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
        property bool preservePendingConnection: false

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
            if (!preservePendingConnection) {
                pane.pendingConnectId = "";
                pane.pendingConnectName = "";
                pane.pendingConnectAuthentication = "";
                pane.pendingConnectNeedsHostCredential = false;
                pane.pendingConnectNeedsProxyCredential = false;
            }
            if (!preservePendingConnection && restoreItem && restoreItem.visible && restoreItem.enabled) {
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
                        if (pane.pendingConnectNeedsHostCredential || pane.pendingConnectNeedsProxyCredential) {
                            const restoreItem = portableUnlockDialog.focusRestoreItem;
                            portableUnlockDialog.focusRestoreItem = null;
                            portableUnlockDialog.preservePendingConnection = true;
                            portableUnlockDialog.close();
                            portableUnlockDialog.preservePendingConnection = false;
                            credentialDialog.focusRestoreItem = restoreItem;
                            savedCredentialField.text = "";
                            savedProxyCredentialField.text = "";
                            savedCredentialRemember.checked = true;
                            savedProxyCredentialRemember.checked = true;
                            credentialDialog.open();
                        } else if (pane.controller.connectHostProfile(pane.pendingConnectId, "", "")) {
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
        onAboutToShow: Qt.callLater(() => {
            if (pane.pendingConnectNeedsHostCredential) {
                savedCredentialField.forceActiveFocus();
            } else {
                savedProxyCredentialField.forceActiveFocus();
            }
        })
        onClosed: {
            const restoreItem = focusRestoreItem;
            focusRestoreItem = null;
            savedCredentialField.text = "";
            savedProxyCredentialField.text = "";
            pane.pendingConnectId = "";
            pane.pendingConnectName = "";
            pane.pendingConnectAuthentication = "";
            pane.pendingConnectNeedsHostCredential = false;
            pane.pendingConnectNeedsProxyCredential = false;
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
            Accessible.name: pane.pendingCredentialTitle()

            Text {
                text: pane.pendingCredentialTitle()
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
                visible: pane.pendingConnectNeedsHostCredential
                placeholderText: pane.pendingConnectAuthentication === "password" ? qsTr("SSH password") : qsTr("Private-key passphrase")
                passwordRevealable: true
                accessibleName: placeholderText
                selectByMouse: true
                onAccepted: {
                    if (pane.pendingConnectNeedsProxyCredential) {
                        savedProxyCredentialField.forceActiveFocus();
                    } else {
                        pane.connectPendingSaved();
                    }
                }
            }

            AppSwitch {
                id: savedCredentialRemember

                objectName: "savedCredentialRemember"
                Layout.fillWidth: true
                visible: pane.pendingConnectNeedsHostCredential
                checked: true
                text: qsTr("Save this credential securely")
                accessibleName: qsTr("Save this credential in the active secure store")
            }

            AppTextField {
                id: savedProxyCredentialField

                objectName: "savedProxyCredentialField"
                Layout.fillWidth: true
                visible: pane.pendingConnectNeedsProxyCredential
                placeholderText: qsTr("Proxy password")
                passwordRevealable: true
                accessibleName: placeholderText
                selectByMouse: true
                onAccepted: pane.connectPendingSaved()
            }

            AppSwitch {
                id: savedProxyCredentialRemember

                objectName: "savedProxyCredentialRemember"
                Layout.fillWidth: true
                visible: pane.pendingConnectNeedsProxyCredential
                checked: true
                text: qsTr("Save proxy credential securely")
                accessibleName: qsTr("Save the proxy credential in the active secure store")
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
                    enabled: (!pane.pendingConnectNeedsHostCredential || savedCredentialField.text.length > 0) && (!pane.pendingConnectNeedsProxyCredential || savedProxyCredentialField.text.length > 0)
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
