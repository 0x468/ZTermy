pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Rectangle {
    id: root

    objectName: "workspaceKeychainPane"
    required property var controller
    signal editHostRequested(string profileId)
    signal securitySettingsRequested

    readonly property var store: controller.keychain
    property string section: "keys"
    property string filterText: ""
    property var pendingRemoval: ({})
    readonly property var sectionEntries: section === "identities" ? store.identities : store.keys.filter(item => section === "certificates" ? item.kind === "certificate" : item.kind === "key")
    readonly property var visibleEntries: {
        const query = filterText.trim().toLocaleLowerCase();
        return query.length === 0 ? sectionEntries : sectionEntries.filter(item => [item.label, item.username, item.type, item.privateKeyPath, item.authentication].join(" ").toLocaleLowerCase().includes(query));
    }

    function openEditor(mode, item) {
        const source = item || ({});
        editorDialog.mode = mode;
        editorDialog.editingItem = source;
        labelField.text = source.label || "";
        usernameField.text = source.username || "root";
        authenticationBox.currentIndex = Math.max(0, ["password", "private-key", "certificate", "agent"].indexOf(source.authentication || "password"));
        credentialRequiredSwitch.checked = source.credentialRequired === undefined ? true : source.credentialRequired;
        secretField.text = "";
        rememberSecretSwitch.checked = source.credentialStored === undefined ? true : source.credentialStored;
        privateKeyPathField.text = "";
        certificatePathField.text = "";
        keyTypeBox.currentIndex = 0;
        editorDialog.open();
        Qt.callLater(labelField.forceActiveFocus);
    }

    color: Theme.contentBackground

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: qsTr("Keychain")
                    color: Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: 22
                    font.weight: Font.Bold
                }
                Rectangle {
                    implicitWidth: countLabel.implicitWidth + 14
                    implicitHeight: 24
                    radius: height / 2
                    color: Theme.selectedBackground
                    Text {
                        id: countLabel
                        anchors.centerIn: parent
                        text: qsTr("%1 items").arg(root.store.keys.length + root.store.identities.length)
                        color: Theme.accent
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                        font.weight: Font.DemiBold
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                ActionButton {
                    text: qsTr("Storage settings")
                    iconName: "settings"
                    onClicked: root.securitySettingsRequested()
                }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Manage reusable SSH keys, OpenSSH certificates, and identities shared by host profiles.")
                color: Theme.textMuted
                wrapMode: Text.Wrap
                font.family: Theme.uiFont
                font.pixelSize: Theme.textLabel
            }
        }

        Flow {
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            spacing: 8
            ActionButton {
                objectName: "keychainGenerateAction"
                text: qsTr("Generate key")
                iconName: "plus"
                enabled: !root.store.busy && root.store.keyGeneratorAvailable
                onClicked: root.openEditor("generate")
            }
            ActionButton {
                text: qsTr("Import key")
                iconName: "download"
                enabled: !root.store.busy
                onClicked: root.openEditor("import")
            }
            ActionButton {
                text: qsTr("Import certificate")
                iconName: "security"
                enabled: !root.store.busy
                onClicked: root.openEditor("certificate")
            }
            ActionButton {
                text: qsTr("New identity")
                iconName: "user"
                variant: "primary"
                enabled: !root.store.busy
                onClicked: root.openEditor("identity")
            }
        }

        Flow {
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            spacing: 8
            Repeater {
                model: [
                    {
                        token: "keys",
                        label: qsTr("Keys"),
                        icon: "key"
                    },
                    {
                        token: "certificates",
                        label: qsTr("Certificates"),
                        icon: "security"
                    },
                    {
                        token: "identities",
                        label: qsTr("Identities"),
                        icon: "user"
                    }
                ]
                delegate: ActionButton {
                    required property var modelData
                    text: modelData.label
                    iconName: modelData.icon
                    variant: root.section === modelData.token ? "primary" : "default"
                    onClicked: root.section = modelData.token
                }
            }
        }

        AppTextField {
            Layout.fillWidth: true
            accessibleName: qsTr("Search keychain")
            placeholderText: qsTr("Search label, username, key type, or path")
            text: root.filterText
            onTextEdited: root.filterText = text
        }
        StatusMessage {
            Layout.fillWidth: true
            kind: "error"
            text: root.store.operationError
        }
        StatePanel {
            Layout.fillWidth: true
            visible: root.store.busy && root.visibleEntries.length === 0
            kind: "loading"
            centered: true
            heading: qsTr("Updating keychain")
            description: qsTr("Key files and metadata are being processed in the background.")
        }
        StatePanel {
            Layout.fillWidth: true
            visible: !root.store.busy && root.visibleEntries.length === 0 && root.store.operationError.length === 0
            centered: true
            heading: root.filterText.length > 0 ? qsTr("No matching keychain items") : root.section === "identities" ? qsTr("No identities yet") : root.section === "certificates" ? qsTr("No certificates yet") : qsTr("No keys yet")
            description: root.height < 440 ? "" : root.section === "identities" ? qsTr("Create an identity to reuse a username and authentication source across hosts.") : qsTr("Generate a key or import an existing OpenSSH key into ztermy-managed storage.")
        }

        Item {
            Layout.fillHeight: true
            visible: root.visibleEntries.length === 0
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.visibleEntries.length > 0
            clip: true
            model: root.visibleEntries
            spacing: 8
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }
            delegate: Rectangle {
                id: card
                required property var modelData
                width: ListView.view.width
                height: cardContent.implicitHeight + 24
                radius: Theme.radiusControl
                color: cardHover.hovered ? Theme.controlHover : Theme.raisedBackground
                border.color: Theme.border
                HoverHandler {
                    id: cardHover
                }
                ColumnLayout {
                    id: cardContent
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Rectangle {
                            Layout.preferredWidth: 42
                            Layout.preferredHeight: 42
                            radius: Theme.radiusControl
                            color: Theme.selectedBackground
                            AppIcon {
                                anchors.centerIn: parent
                                width: 20
                                height: 20
                                name: root.section === "identities" ? "user" : card.modelData.kind === "certificate" ? "security" : "key"
                                color: Theme.accent
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: card.modelData.label
                                    color: Theme.text
                                    elide: Text.ElideRight
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textBody
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    text: root.section === "identities" ? card.modelData.authentication : card.modelData.type
                                    color: Theme.accent
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textLabel
                                    font.weight: Font.DemiBold
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: root.section === "identities" ? card.modelData.username : card.modelData.privateKeyPath
                                color: Theme.textMuted
                                elide: Text.ElideMiddle
                                font.family: root.section === "identities" ? Theme.uiFont : Theme.terminalFont
                                font.pixelSize: Theme.textLabel
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: card.modelData.referenceCount > 0
                                text: root.section === "identities" ? qsTr("Used by: %1").arg(card.modelData.profileNames.join(", ")) : qsTr("Used by identities: %1").arg(card.modelData.identityNames.join(", "))
                                color: Theme.textSubtle
                                elide: Text.ElideRight
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textCompact
                            }
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight
                        spacing: 8
                        ActionButton {
                            visible: root.section === "identities"
                            text: qsTr("Edit")
                            iconName: "edit"
                            enabled: !root.store.busy
                            onClicked: root.openEditor("identity", card.modelData)
                        }
                        ActionButton {
                            visible: root.section !== "identities"
                            text: qsTr("Copy path")
                            iconName: "copy"
                            enabled: (card.modelData.publicKeyPath || card.modelData.certificatePath || "").length > 0
                            onClicked: root.store.copyText(card.modelData.kind === "certificate" ? card.modelData.certificatePath : card.modelData.publicKeyPath)
                        }
                        ActionButton {
                            text: qsTr("Remove")
                            iconName: "trash"
                            variant: "destructive"
                            enabled: !root.store.busy
                            onClicked: {
                                root.pendingRemoval = card.modelData;
                                removeDialog.openFrom(this);
                            }
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: editorDialog
        property string mode: ""
        property var editingItem: ({})
        anchors.centerIn: parent
        width: Math.min(540, Math.max(0, parent ? parent.width - 40 : 540))
        height: Math.min(620, Math.max(0, parent ? parent.height - 40 : 620))
        modal: true
        dim: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        padding: 20
        Overlay.modal: Rectangle {
            color: Theme.modalScrim
        }
        background: Rectangle {
            radius: Theme.radiusPanel
            color: Theme.floatingBackground
            border.color: Theme.borderStrong
        }
        contentItem: ScrollView {
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 12
                Text {
                    Layout.fillWidth: true
                    text: editorDialog.mode === "generate" ? qsTr("Generate SSH key") : editorDialog.mode === "import" ? qsTr("Import SSH key") : editorDialog.mode === "certificate" ? qsTr("Import OpenSSH certificate") : editorDialog.editingItem.id ? qsTr("Edit identity") : qsTr("New identity")
                    color: Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
                Label {
                    text: qsTr("Label")
                    color: Theme.text
                }
                AppTextField {
                    id: labelField
                    Layout.fillWidth: true
                    accessibleName: qsTr("Keychain item label")
                    placeholderText: qsTr("For example: Production root")
                }
                Label {
                    visible: editorDialog.mode === "generate"
                    text: qsTr("Key type")
                    color: Theme.text
                }
                AppComboBox {
                    id: keyTypeBox
                    Layout.fillWidth: true
                    visible: editorDialog.mode === "generate"
                    model: ["ed25519", "rsa", "ecdsa"]
                    displayTextModel: ["ED25519", "RSA", "ECDSA"]
                }
                AppComboBox {
                    id: keyBitsBox
                    Layout.fillWidth: true
                    visible: editorDialog.mode === "generate" && keyTypeBox.currentIndex !== 0
                    model: keyTypeBox.currentIndex === 1 ? ["4096", "3072", "2048"] : ["521", "384", "256"]
                    displayTextModel: model
                }
                Label {
                    visible: editorDialog.mode === "import" || editorDialog.mode === "certificate"
                    text: qsTr("Private key file")
                    color: Theme.text
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: editorDialog.mode === "import" || editorDialog.mode === "certificate"
                    AppTextField {
                        id: privateKeyPathField
                        Layout.fillWidth: true
                        accessibleName: qsTr("Private key file")
                    }
                    ActionButton {
                        text: qsTr("Browse")
                        iconName: "folder"
                        onClicked: privateKeyDialog.open()
                    }
                }
                Label {
                    visible: editorDialog.mode === "certificate"
                    text: qsTr("OpenSSH certificate file")
                    color: Theme.text
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: editorDialog.mode === "certificate"
                    AppTextField {
                        id: certificatePathField
                        Layout.fillWidth: true
                        accessibleName: qsTr("OpenSSH certificate file")
                    }
                    ActionButton {
                        text: qsTr("Browse")
                        iconName: "folder"
                        onClicked: certificateFileDialog.open()
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: editorDialog.mode === "import" || editorDialog.mode === "certificate"
                    text: qsTr("The imported key is copied into ztermy-managed storage. Profiles reference the keychain identity instead of the original system path.")
                    color: Theme.textMuted
                    wrapMode: Text.WordWrap
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textLabel
                }
                Label {
                    visible: editorDialog.mode === "identity"
                    text: qsTr("Username")
                    color: Theme.text
                }
                AppTextField {
                    id: usernameField
                    Layout.fillWidth: true
                    visible: editorDialog.mode === "identity"
                    accessibleName: qsTr("Identity username")
                }
                Label {
                    visible: editorDialog.mode === "identity"
                    text: qsTr("Authentication")
                    color: Theme.text
                }
                AppComboBox {
                    id: authenticationBox
                    Layout.fillWidth: true
                    visible: editorDialog.mode === "identity"
                    model: ["password", "private-key", "certificate", "agent"]
                    displayTextModel: [qsTr("Password"), qsTr("Private key"), qsTr("Certificate"), qsTr("SSH agent")]
                }
                Label {
                    visible: editorDialog.mode === "identity" && authenticationBox.currentIndex > 0 && authenticationBox.currentIndex < 3
                    text: authenticationBox.currentIndex === 2 ? qsTr("Certificate") : qsTr("Key")
                    color: Theme.text
                }
                AppComboBox {
                    id: keyBox
                    Layout.fillWidth: true
                    visible: editorDialog.mode === "identity" && authenticationBox.currentIndex > 0 && authenticationBox.currentIndex < 3
                    readonly property var filteredKeys: root.store.keys.filter(item => authenticationBox.currentIndex === 2 ? item.kind === "certificate" : item.kind === "key")
                    model: filteredKeys.map(item => item.id)
                    displayTextModel: filteredKeys.map(item => item.label + " · " + item.type)
                    accessibleName: qsTr("Identity key")
                }
                AppSwitch {
                    id: credentialRequiredSwitch
                    Layout.fillWidth: true
                    visible: editorDialog.mode === "identity" && authenticationBox.currentIndex > 0 && authenticationBox.currentIndex < 3
                    text: qsTr("This key requires a passphrase")
                    accessibleName: text
                }
                Label {
                    visible: editorDialog.mode !== "identity" || authenticationBox.currentIndex !== 3
                    text: editorDialog.mode === "identity" && authenticationBox.currentIndex === 0 ? qsTr("Password") : qsTr("Passphrase (optional)")
                    color: Theme.text
                }
                AppTextField {
                    id: secretField
                    Layout.fillWidth: true
                    visible: editorDialog.mode !== "identity" || authenticationBox.currentIndex !== 3
                    passwordRevealable: true
                    accessibleName: qsTr("Credential")
                    placeholderText: editorDialog.editingItem.credentialStored ? qsTr("Leave empty to keep the saved credential") : ""
                }
                AppSwitch {
                    id: rememberSecretSwitch
                    Layout.fillWidth: true
                    visible: editorDialog.mode === "identity"
                    text: qsTr("Save credential")
                    accessibleName: text
                }
                Item {
                    Layout.fillHeight: true
                    Layout.minimumHeight: 8
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    spacing: 8
                    layoutDirection: Qt.RightToLeft
                    ActionButton {
                        text: qsTr("Save")
                        variant: "primary"
                        enabled: !root.store.busy
                        onClicked: {
                            let started = false;
                            if (editorDialog.mode === "generate")
                                started = root.store.generateKey(labelField.text, keyTypeBox.model[keyTypeBox.currentIndex], Number(keyBitsBox.model[keyBitsBox.currentIndex] || 0), secretField.text);
                            else if (editorDialog.mode === "import")
                                started = root.store.importKey(privateKeyPathField.text, labelField.text, true, secretField.text);
                            else if (editorDialog.mode === "certificate")
                                started = root.store.importCertificate(privateKeyPathField.text, certificatePathField.text, labelField.text, true, secretField.text);
                            else
                                started = root.store.saveIdentity(editorDialog.editingItem.id || "", labelField.text, usernameField.text, authenticationBox.model[authenticationBox.currentIndex], keyBox.model.length > 0 ? keyBox.model[Math.max(0, keyBox.currentIndex)] : "", credentialRequiredSwitch.checked, secretField.text, rememberSecretSwitch.checked);
                            if (started) {
                                secretField.text = "";
                                editorDialog.close();
                            }
                        }
                    }
                    ActionButton {
                        text: qsTr("Cancel")
                        onClicked: editorDialog.close()
                    }
                }
            }
        }
    }

    FileDialog {
        id: privateKeyDialog
        title: qsTr("Choose an SSH private key")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("SSH private keys (*)"), qsTr("All files (*)")]
        onAccepted: privateKeyPathField.text = selectedFile.toString()
    }
    FileDialog {
        id: certificateFileDialog
        title: qsTr("Choose an OpenSSH certificate")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("OpenSSH certificates (*-cert.pub)"), qsTr("All files (*)")]
        onAccepted: certificatePathField.text = selectedFile.toString()
    }
    ConfirmationDialog {
        id: removeDialog
        heading: root.section === "identities" ? qsTr("Remove identity?") : qsTr("Remove keychain item?")
        description: root.pendingRemoval.referenceCount > 0 ? qsTr("This item is still used and must be unlinked first.") : qsTr("This removes the keychain record. Referenced source files outside ztermy are not deleted.")
        acceptText: qsTr("Remove")
        destructive: true
        onAccepted: {
            if (root.section === "identities")
                root.store.removeIdentity(root.pendingRemoval.id);
            else
                root.store.removeKey(root.pendingRemoval.id);
            root.pendingRemoval = ({});
        }
        onRejected: root.pendingRemoval = ({})
    }
}
