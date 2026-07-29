pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Switch {
    id: control

    property string accessibleName: ""

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    spacing: Theme.spacingControl
    leftPadding: 4
    rightPadding: 8
    topPadding: 6
    bottomPadding: 6
    implicitHeight: 34
    Accessible.name: accessibleName.length > 0 ? accessibleName : text

    indicator: Rectangle {
        implicitWidth: 36
        implicitHeight: 20
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: height / 2
        color: !control.enabled ? Theme.controlDisabled : control.checked ? Theme.accent : control.hovered ? Theme.controlPressed : Theme.controlBackground
        border.color: control.activeFocus ? Theme.focus : control.checked ? Theme.accent : Theme.borderStrong
        border.width: control.activeFocus ? 2 : 1

        Rectangle {
            width: 14
            height: 14
            x: control.checked ? parent.width - width - 3 : 3
            anchors.verticalCenter: parent.verticalCenter
            radius: width / 2
            color: control.checked ? Theme.accentText : Theme.textSoft

            Behavior on x {
                NumberAnimation {
                    duration: Theme.motionFast
                }
            }
        }

        Behavior on color {
            ColorAnimation {
                duration: Theme.motionFast
            }
        }
    }

    contentItem: Text {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        color: control.enabled ? Theme.text : Theme.textSubtle
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
        font.family: Theme.uiFont
        font.pixelSize: Theme.textBody
    }

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
}
