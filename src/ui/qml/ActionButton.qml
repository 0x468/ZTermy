pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Button {
    id: control

    property string variant: "default"
    property string accessibleName: ""
    property string iconName: ""
    readonly property bool primary: variant === "primary"
    readonly property bool destructive: variant === "destructive"
    readonly property color restColor: primary ? Theme.accent : destructive ? Theme.dangerSurface : Theme.controlBackground
    readonly property color hoverColor: primary ? Theme.accentHover : destructive ? Theme.dangerHover : Theme.controlHover
    readonly property color pressedColor: primary ? Theme.accentPressed : destructive ? Theme.dangerPressed : Theme.controlPressed
    readonly property color foregroundColor: primary ? Theme.accentText : destructive ? Theme.dangerSurfaceText : Theme.text

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    leftPadding: 14
    rightPadding: 14
    topPadding: 7
    bottomPadding: 7
    implicitWidth: Math.max(72, contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: 34
    Accessible.name: accessibleName.length > 0 ? accessibleName : text

    contentItem: Item {
        implicitWidth: buttonContent.implicitWidth
        implicitHeight: buttonContent.implicitHeight

        Row {
            id: buttonContent

            anchors.centerIn: parent
            spacing: control.iconName.length > 0 && control.text.length > 0 ? Theme.spacingControl : 0

            AppIcon {
                width: 16
                height: 16
                visible: control.iconName.length > 0
                name: control.iconName
                color: control.enabled ? control.foregroundColor : Theme.textSubtle
            }

            Text {
                text: control.text
                color: control.enabled ? control.foregroundColor : Theme.textSubtle
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                font.family: Theme.uiFont
                font.pixelSize: Theme.textBody
                font.weight: control.primary || control.destructive ? Font.DemiBold : Font.Medium
            }
        }
    }

    background: Rectangle {
        implicitWidth: 72
        implicitHeight: 34
        radius: Theme.radiusControl
        color: !control.enabled ? Theme.controlDisabled : control.down ? control.pressedColor : control.hovered ? control.hoverColor : control.restColor
        border.color: control.activeFocus ? Theme.focus : control.primary ? Theme.accent : control.destructive ? Theme.dangerBorder : control.hovered ? Theme.borderStrong : Theme.border
        border.width: control.activeFocus ? 2 : 1

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
