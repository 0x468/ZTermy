pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: control

    property string heading: ""
    property string description: ""
    property string acceptText: "Continue"
    property string rejectText: "Cancel"
    property bool destructive: false
    property bool acceptEnabled: true
    property Item focusRestoreItem: null

    function openFrom(item) {
        focusRestoreItem = item;
        open();
    }

    anchors.centerIn: parent
    width: Math.min(440, Math.max(0, parent ? parent.width - 48 : 440))
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 20

    onOpened: rejectButton.forceActiveFocus()
    onClosed: {
        const restoreItem = focusRestoreItem;
        focusRestoreItem = null;
        if (restoreItem && restoreItem.visible && restoreItem.enabled) {
            Qt.callLater(() => restoreItem.forceActiveFocus());
        }
    }

    background: Rectangle {
        radius: Theme.radiusPanel
        color: Theme.elevatedBackground
        border.color: control.destructive ? Theme.dangerBorder : Theme.borderStrong
    }

    contentItem: ColumnLayout {
        spacing: 14
        Accessible.role: Accessible.Dialog
        Accessible.name: control.heading

        Text {
            Layout.fillWidth: true
            text: control.heading
            color: Theme.text
            wrapMode: Text.WordWrap
            font.family: Theme.uiFont
            font.pixelSize: 18
            font.weight: Font.DemiBold
        }

        Text {
            Layout.fillWidth: true
            text: control.description
            color: Theme.textMuted
            wrapMode: Text.WordWrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.textBody
        }

        RowLayout {
            Layout.fillWidth: true

            Item {
                Layout.fillWidth: true
            }

            ActionButton {
                id: rejectButton

                text: control.rejectText
                accessibleName: control.rejectText
                onClicked: control.reject()
            }

            ActionButton {
                text: control.acceptText
                accessibleName: control.acceptText
                enabled: control.acceptEnabled
                variant: control.destructive ? "destructive" : "primary"
                onClicked: control.accept()
            }
        }
    }
}
