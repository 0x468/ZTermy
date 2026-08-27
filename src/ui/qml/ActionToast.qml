pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root

    property string title: ""
    property string message: ""
    property string actionText: ""
    property var payload: ({})
    signal actionTriggered(var payload)

    width: Math.min(380, parent ? parent.width - 32 : 380)
    height: contentRow.implicitHeight + 24
    padding: 0
    modal: false
    dim: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.animationsEnabled ? Theme.motionMedium : 0
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                property: "y"
                from: root.y - 6
                to: root.y
                duration: Theme.animationsEnabled ? Theme.motionMedium : 0
                easing.type: Easing.OutCubic
            }
        }
    }

    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: Theme.animationsEnabled ? Theme.motionFast : 0
        }
    }

    background: Rectangle {
        radius: Theme.radiusPanel
        color: Theme.floatingBackground
        border.color: Theme.borderStrong
        border.width: 1

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 3
            radius: 1.5
            color: Theme.accent
        }
    }

    contentItem: RowLayout {
        id: contentRow

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
            name: "highlight"
            color: Theme.accent
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

        ActionButton {
            visible: root.actionText.length > 0
            text: root.actionText
            accessibleName: root.actionText
            onClicked: {
                root.actionTriggered(root.payload);
                root.close();
            }
        }

        ToolButton {
            id: dismissButton

            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            onClicked: root.close()
            Accessible.name: qsTr("Dismiss notification")

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

        interval: 5000
        repeat: false
        onTriggered: root.close()
    }

    function present(notification) {
        title = notification.title || "";
        message = notification.message || "";
        actionText = notification.actionText || "";
        payload = notification.payload || ({});
        dismissTimer.restart();
        if (!opened)
            open();
    }
}
