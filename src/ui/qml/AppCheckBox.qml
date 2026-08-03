pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

CheckBox {
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
        implicitWidth: 20
        implicitHeight: 20
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: Theme.radiusSmall
        color: !control.enabled ? Theme.controlDisabled : control.checked ? Theme.accent : control.hovered ? Theme.controlHover : Theme.fieldBackground
        border.color: control.activeFocus ? Theme.focus : control.checked ? Theme.accent : Theme.borderStrong
        border.width: control.activeFocus ? 2 : 1

        AppIcon {
            anchors.centerIn: parent
            width: 14
            height: 14
            visible: control.checked
            name: "check"
            color: Theme.accentText
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
