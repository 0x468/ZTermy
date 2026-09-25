pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: renameTerminalDialog
    objectName: "terminalRenameDialog"
    required property var controller
    property string workspaceId: ""

    function openFor(id, title) {
        workspaceId = id;
        renameTerminalTitleField.text = title;
        open();
    }

    anchors.centerIn: parent
    width: Math.min(420, Math.max(0, parent.width - 48))
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 20
    onOpened: {
        renameTerminalTitleField.forceActiveFocus(Qt.PopupFocusReason);
        renameTerminalTitleField.selectAll();
    }
    onClosed: renameTerminalDialog.workspaceId = ""

    Overlay.modal: Rectangle {
        color: Theme.modalScrim
    }

    background: AppSurface {
        elevation: 3
    }

    contentItem: ColumnLayout {
        spacing: 14

        Text {
            Layout.fillWidth: true
            text: qsTranslate("Main", "Rename terminal tab")
            color: Theme.text
            font.family: Theme.uiFont
            font.pixelSize: 18
            font.weight: Font.DemiBold
        }

        AppTextField {
            id: renameTerminalTitleField

            objectName: "renameTerminalTitleField"
            Layout.fillWidth: true
            accessibleName: qsTranslate("Main", "Terminal tab title")
            maximumLength: 256
            onAccepted: renameTerminalAccept.clicked()
        }

        RowLayout {
            Layout.fillWidth: true

            Item {
                Layout.fillWidth: true
            }

            ActionButton {
                text: qsTranslate("Main", "Cancel")
                accessibleName: text
                onClicked: renameTerminalDialog.close()
            }

            ActionButton {
                id: renameTerminalAccept

                text: qsTranslate("Main", "Rename")
                accessibleName: text
                variant: "primary"
                onClicked: {
                    if (renameTerminalDialog.controller.setTerminalTabTitle(renameTerminalDialog.workspaceId, renameTerminalTitleField.text)) {
                        renameTerminalDialog.close();
                    }
                }
            }
        }
    }
}
