pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One window-local authentication flow shared by the host page and Tab menus.
// Credentials stay in fields until submitted; no cross-window controller state.
Item {
    id: pane
    required property var controller
    property color textColor: Theme.text
    property color mutedColor: Theme.textMuted
    property color raisedColor: Theme.elevatedBackground
    property string pendingConnectId: ""
    property string pendingConnectName: ""
    property string pendingConnectAuthentication: ""
    property bool pendingConnectNeedsHostCredential: false
    property bool pendingConnectNeedsProxyCredential: false
    signal connectionStarting
    signal connectionStarted
    signal connectionError(string message)
    function showStatus(message, isError) {
        if (isError)
            connectionError(message);
    }
    function startConnection(profileId, secret, proxySecret) {
        connectionStarting();
        return controller.connectHostProfile(profileId, secret, proxySecret);
    }

    function pendingCredentialTitle() {
        if (pendingConnectNeedsHostCredential && pendingConnectNeedsProxyCredential) {
            return qsTranslate("HostConnectionPane", "Enter connection credentials");
        }
        if (pendingConnectNeedsProxyCredential) {
            return qsTranslate("HostConnectionPane", "Enter proxy password");
        }
        return pendingConnectAuthentication === "password" ? qsTranslate("HostConnectionPane", "Enter SSH password") : qsTranslate("HostConnectionPane", "Enter key passphrase");
    }

    function connectSaved(profile, sourceItem) {
        if (profile.jumpProfilesReady === false) {
            showStatus(qsTranslate("HostConnectionPane", "Save the required credentials in every jump profile before connecting."), true);
            return;
        }
        const needsHostCredential = profile.credentialRequired === true && !profile.credentialStored;
        const proxy = profile.proxy || {};
        const needsProxyCredential = proxy.type !== "none" && (proxy.username || "").length > 0 && !proxy.credentialStored;
        pendingConnectId = profile.id;
        pendingConnectName = profile.name;
        pendingConnectAuthentication = profile.effectiveAuthentication || profile.authentication;
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
            savedCredentialRemember.enabled = (profile.identityReference || "").length === 0;
            if (!savedCredentialRemember.enabled) {
                savedCredentialRemember.checked = false;
            }
            savedProxyCredentialRemember.checked = true;
            credentialDialog.open();
            return;
        }
        if (startConnection(profile.id, "", "")) {
            connectionStarted();
        } else {
            showStatus(controller.credentialOperationError.length > 0 ? controller.credentialOperationError : qsTranslate("HostConnectionPane", "The saved profile could not be connected."), true);
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
        const started = prepared && startConnection(profileId, connectionSecret, connectionProxySecret);
        if (started) {
            savedCredentialField.text = "";
            savedProxyCredentialField.text = "";
            credentialDialog.close();
            connectionStarted();
        } else {
            showStatus(controller.credentialOperationError.length > 0 ? controller.credentialOperationError : qsTranslate("HostConnectionPane", "The saved profile could not be connected."), true);
        }
    }

    Dialog {
        id: portableUnlockDialog
        objectName: "savedHostPortableUnlockDialog"

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
            Accessible.name: qsTranslate("HostConnectionPane", "Unlock portable credential vault")

            Text {
                text: qsTranslate("HostConnectionPane", "Unlock portable vault")
                color: pane.textColor
                font.family: Theme.uiFont
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }

            Text {
                Layout.preferredWidth: 380
                text: qsTranslate("HostConnectionPane", "Enter the portable-vault master password to connect to \"%1\".").arg(pane.pendingConnectName)
                color: pane.mutedColor
                wrapMode: Text.WordWrap
                font.family: Theme.uiFont
                font.pixelSize: 12
            }

            AppTextField {
                id: portableUnlockPassword

                objectName: "portableUnlockPassword"
                Layout.fillWidth: true
                placeholderText: qsTranslate("HostConnectionPane", "Master password (minimum 8 characters)")
                echoMode: TextInput.Password
                accessibleName: qsTranslate("HostConnectionPane", "Portable vault master password")
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
                    text: qsTranslate("HostConnectionPane", "Cancel")
                    accessibleName: qsTranslate("HostConnectionPane", "Cancel portable vault unlock")
                    onClicked: portableUnlockDialog.close()
                }

                ActionButton {
                    id: portableUnlockAction

                    text: qsTranslate("HostConnectionPane", "Unlock and connect")
                    accessibleName: qsTranslate("HostConnectionPane", "Unlock portable vault and connect")
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
                        } else if (pane.startConnection(pane.pendingConnectId, "", "")) {
                            portableUnlockDialog.close();
                            pane.connectionStarted();
                        } else {
                            portableUnlockStatus.text = pane.controller.credentialOperationError.length > 0 ? pane.controller.credentialOperationError : qsTranslate("HostConnectionPane", "The saved profile could not be connected.");
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: credentialDialog
        objectName: "savedHostCredentialDialog"

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

        enter: MotionEnter {}

        exit: MotionExit {}

        Overlay.modal: Rectangle {
            color: Theme.modalScrim
        }

        background: Rectangle {
            radius: Theme.radiusPanel
            color: pane.raisedColor
            border.color: Theme.borderStrong

            transform: Translate {
                y: credentialDialog.visible ? 0 : Motion.distance

                Behavior on y {
                    MotionRelocate {}
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
                text: qsTranslate("HostConnectionPane", "Authenticate to \"%1\". You can save this credential in the active secure store.").arg(pane.pendingConnectName)
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
                placeholderText: pane.pendingConnectAuthentication === "password" ? qsTranslate("HostConnectionPane", "SSH password") : qsTranslate("HostConnectionPane", "Private-key passphrase")
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
                text: qsTranslate("HostConnectionPane", "Save this credential securely")
                accessibleName: qsTranslate("HostConnectionPane", "Save this credential in the active secure store")
            }

            AppTextField {
                id: savedProxyCredentialField

                objectName: "savedProxyCredentialField"
                Layout.fillWidth: true
                visible: pane.pendingConnectNeedsProxyCredential
                placeholderText: qsTranslate("HostConnectionPane", "Proxy password")
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
                text: qsTranslate("HostConnectionPane", "Save proxy credential securely")
                accessibleName: qsTranslate("HostConnectionPane", "Save the proxy credential in the active secure store")
            }

            RowLayout {
                Layout.fillWidth: true

                Item {
                    Layout.fillWidth: true
                }

                ActionButton {
                    id: savedCredentialCancel

                    objectName: "savedCredentialCancel"
                    text: qsTranslate("HostConnectionPane", "Cancel")
                    accessibleName: qsTranslate("HostConnectionPane", "Cancel saved host authentication")
                    KeyNavigation.right: connectSavedButton
                    onClicked: credentialDialog.close()
                }

                ActionButton {
                    id: connectSavedButton

                    objectName: "savedCredentialConnect"
                    text: qsTranslate("HostConnectionPane", "Connect")
                    accessibleName: qsTranslate("HostConnectionPane", "Connect to saved SSH host")
                    enabled: (!pane.pendingConnectNeedsHostCredential || savedCredentialField.text.length > 0) && (!pane.pendingConnectNeedsProxyCredential || savedProxyCredentialField.text.length > 0)
                    variant: "primary"
                    KeyNavigation.left: savedCredentialCancel
                    onClicked: pane.connectPendingSaved()
                }
            }
        }
    }
}
