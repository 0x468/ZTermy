import QtQuick
import QtQuick.Controls

MenuItem {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(34, implicitContentHeight + topPadding + bottomPadding, implicitIndicatorHeight + topPadding + bottomPadding)
    leftPadding: 10
    rightPadding: 10
    topPadding: 7
    bottomPadding: 7
    spacing: 8
    hoverEnabled: true

    contentItem: Text {
        leftPadding: control.checkable ? 22 : 0
        rightPadding: control.subMenu ? 18 : 0
        text: control.text
        color: control.enabled ? Theme.text : Theme.textSubtle
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        font.family: Theme.uiFont
        font.pixelSize: Theme.textBody
    }

    indicator: AppIcon {
        x: control.leftPadding
        y: Math.round((control.height - height) / 2)
        width: 14
        height: 14
        visible: control.checkable && control.checked
        name: "check"
        color: Theme.accent
    }

    arrow: AppIcon {
        x: control.width - width - control.rightPadding
        y: Math.round((control.height - height) / 2)
        width: 14
        height: 14
        visible: control.subMenu !== null
        name: "chevron-down"
        rotation: -90
        color: Theme.textMuted
    }

    background: Rectangle {
        x: 2
        y: 1
        width: control.width - 4
        height: control.height - 2
        radius: Theme.radiusSmall
        color: control.down ? Theme.controlPressed : control.highlighted ? Theme.controlHover : "transparent"
        border.color: control.activeFocus ? Theme.focus : "transparent"
        border.width: control.activeFocus ? 2 : 0

        Behavior on color {
            ColorAnimation {
                duration: Theme.motionFast
            }
        }
    }

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
}
