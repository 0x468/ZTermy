import QtQuick
import QtQuick.Controls

ToolTip {
    id: control

    property var hoverTarget: parent

    x: parent ? Math.round((parent.width - implicitWidth) / 2) : 0
    y: -implicitHeight - 6
    implicitWidth: Math.min(420, Math.max(implicitBackgroundWidth + leftInset + rightInset, contentItem.implicitWidth + leftPadding + rightPadding))
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset, contentItem.implicitHeight + topPadding + bottomPadding)
    margins: 8
    horizontalPadding: 10
    verticalPadding: 7
    delay: 450
    timeout: 10000
    visible: hoverTarget && (hoverTarget.hovered === true || hoverTarget.containsMouse === true)

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: Theme.motionFast
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

    contentItem: Text {
        text: control.text
        color: Theme.text
        wrapMode: Text.Wrap
        font.family: Theme.uiFont
        font.pixelSize: Theme.textLabel
        lineHeight: 1.2
    }

    background: Rectangle {
        implicitWidth: 32
        implicitHeight: 28
        radius: Theme.radiusControl
        color: Theme.floatingBackground
        border.color: Theme.borderStrong
        border.width: 1
    }
}
