pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root

    property string kind: "info"
    property string title: ""
    property string message: ""
    readonly property color statusColor: kind === "error" ? Theme.dangerText : kind === "success" ? Theme.successText : Theme.accent

    width: Math.min(340, parent ? parent.width - 32 : 340)
    height: contentColumn.implicitHeight + 24
    padding: 0
    modal: false
    dim: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    enter: MotionEnter {}

    exit: MotionExit {}

    background: AppSurface {
        elevation: 2

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 3
            radius: width / 2
            color: root.statusColor
        }
    }

    contentItem: RowLayout {
        id: contentColumn

        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 10
        anchors.topMargin: 12
        anchors.bottomMargin: 12
        spacing: 10

        AppIcon {
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
            Layout.alignment: Qt.AlignTop
            name: root.kind === "success" ? "check" : root.kind === "error" ? "close" : "transfer"
            color: root.statusColor
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3

            Text {
                Layout.fillWidth: true
                text: root.title
                color: Theme.text
                elide: Text.ElideRight
                font.family: Theme.uiFont
                font.pixelSize: Theme.textBody
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                text: root.message
                visible: text.length > 0
                color: Theme.textMuted
                elide: Text.ElideMiddle
                font.family: Theme.uiFont
                font.pixelSize: Theme.textLabel
            }
        }

        ToolButton {
            id: dismissButton

            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            onClicked: root.close()
            Accessible.name: qsTr("Dismiss transfer notification")

            contentItem: AppIcon {
                name: "close"
                color: Theme.textMuted
            }

            background: Rectangle {
                radius: width / 2
                color: dismissButton.down ? Theme.controlPressed : dismissButton.hovered ? Theme.controlHover : "transparent"
                border.color: dismissButton.visualFocus ? Theme.focus : "transparent"
                border.width: dismissButton.visualFocus ? 2 : 0
            }

            HoverHandler {
                cursorShape: Qt.PointingHandCursor
            }
        }
    }

    Timer {
        id: dismissTimer

        interval: root.kind === "error" ? 7000 : 4500
        repeat: false
        onTriggered: root.close()
    }

    function present(notification) {
        kind = notification.kind || "info";
        title = notification.title || "";
        message = notification.message || "";
        dismissTimer.restart();
        if (!opened) {
            open();
        }
    }
}
