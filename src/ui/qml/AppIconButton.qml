pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

ToolButton {
    id: control

    required property string label
    required property string iconName
    property color iconColor: selected ? Theme.accent : Theme.textSoft
    property string toolTipText: label
    property bool toolTipEnabled: true
    property bool selected: false

    implicitWidth: 28
    implicitHeight: 28
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: label
    Keys.onReturnPressed: event => {
        if (!event.isAutoRepeat)
            control.click();
        event.accepted = true;
    }
    Keys.onEnterPressed: event => {
        if (!event.isAutoRepeat)
            control.click();
        event.accepted = true;
    }

    contentItem: AppIcon {
        name: control.iconName
        color: control.enabled ? control.iconColor : Theme.textSubtle
    }

    background: Item {
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height)
            height: width
            radius: width / 2
            property real feedbackAmount: control.down || control.hovered ? 1 : 0
            readonly property color feedbackColor: control.down ? Theme.controlPressed : Theme.controlHover
            color: Theme.withAlpha(feedbackColor, feedbackColor.a * feedbackAmount)
            border.color: control.visualFocus ? Theme.focus : "transparent"
            border.width: control.visualFocus ? 2 : 0
            Behavior on feedbackAmount {
                NumberAnimation {
                    duration: Theme.motionFast
                }
            }
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            width: control.selected ? 12 : 0
            height: 2
            radius: height / 2
            color: Theme.accent
            Behavior on width {
                NumberAnimation {
                    duration: Theme.motionFast
                    easing.type: Easing.OutCubic
                }
            }
        }
    }

    AppToolTip {
        objectName: "iconButtonToolTip"
        hoverTarget: control
        text: control.toolTipText
        visible: control.toolTipEnabled && control.enabled && control.hovered && text.length > 0
    }
    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
}
