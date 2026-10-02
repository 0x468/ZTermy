pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    required property var controller
    property var details: ({})
    property var deferredDetails: null
    property bool resolved: false
    objectName: "launchAuthenticationDialog"
    anchors.centerIn: parent
    width: Math.min(460, Math.max(0, parent.width - 48))
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 20
    enter: MotionEnter {}
    exit: MotionExit {}

    function openFor(value) {
        // A successful submit queues the next launch before exit motion ends.
        // Preserve the outgoing popup's resolution until its input shield retires.
        if (visible) {
            deferredDetails = value;
            return;
        }
        details = value;
        resolved = false;
        status.text = "";
        credential.text = "";
        proxyCredential.text = "";
        open();
    }
    function submit() {
        const secret = credential.text;
        const proxySecret = proxyCredential.text;
        credential.text = "";
        proxyCredential.text = "";
        if (controller.completeLaunchAuthentication(secret, proxySecret, false)) {
            resolved = true;
            close();
        } else {
            status.text = qsTr("The connection could not be started. Check the credential and saved host configuration.");
        }
    }
    onOpened: credential.forceActiveFocus(Qt.PopupFocusReason)
    onClosed: {
        credential.text = "";
        proxyCredential.text = "";
        if (!resolved)
            controller.completeLaunchAuthentication("", "", true);
        details = ({});
        if (deferredDetails !== null) {
            const next = deferredDetails;
            deferredDetails = null;
            Qt.callLater(function () {
                dialog.openFor(next);
            });
        }
    }
    Overlay.modal: Rectangle {
        color: Theme.modalScrim
    }
    background: AppSurface {
        elevation: 3
        border.color: Theme.borderStrong
        transform: Translate {
            y: dialog.visible ? 0 : Motion.distance
            Behavior on y {
                MotionRelocate {}
            }
        }
    }
    contentItem: ColumnLayout {
        spacing: Theme.spacingControl
        Text {
            Layout.fillWidth: true
            text: qsTr("SSH authentication")
            color: Theme.text
            font.family: Theme.uiFont
            font.pixelSize: 18
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            text: dialog.details.target || ""
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            color: Theme.textMuted
            font.family: Theme.uiFont
        }
        AppTextField {
            id: credential
            objectName: "launchCredential"
            Layout.fillWidth: true
            placeholderText: dialog.details.privateKey ? qsTr("Private-key passphrase (leave empty for an unencrypted key)") : qsTr("Password or private-key passphrase")
            accessibleName: placeholderText
            echoMode: TextInput.Password
            maximumLength: 4096
            onAccepted: dialog.submit()
        }
        AppTextField {
            id: proxyCredential
            objectName: "launchProxyCredential"
            Layout.fillWidth: true
            visible: dialog.details.savedProfile === true
            placeholderText: qsTr("Proxy password, if required")
            accessibleName: placeholderText
            echoMode: TextInput.Password
            maximumLength: 4096
            onAccepted: dialog.submit()
        }
        StatusMessage {
            id: status
            Layout.fillWidth: true
            kind: "error"
        }
        RowLayout {
            Layout.fillWidth: true
            Item {
                Layout.fillWidth: true
            }
            ActionButton {
                objectName: "launchAuthenticationCancel"
                text: qsTr("Cancel")
                accessibleName: text
                onClicked: dialog.close()
            }
            ActionButton {
                objectName: "launchAuthenticationConfirm"
                text: qsTr("Connect")
                accessibleName: text
                variant: "primary"
                onClicked: dialog.submit()
            }
        }
    }
}
