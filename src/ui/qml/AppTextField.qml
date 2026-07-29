pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

TextField {
    id: control

    property string accessibleName: ""
    property bool compact: false
    property bool invalid: false

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    selectByMouse: true
    leftPadding: compact ? 9 : 12
    rightPadding: compact ? 9 : 12
    topPadding: compact ? 5 : 7
    bottomPadding: compact ? 5 : 7
    implicitHeight: compact ? 30 : 34
    color: Theme.text
    placeholderTextColor: Theme.textMuted
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentText
    font.family: Theme.uiFont
    font.pixelSize: compact ? 12 : Theme.textBody
    Accessible.name: accessibleName.length > 0 ? accessibleName : placeholderText

    background: Rectangle {
        implicitWidth: 120
        implicitHeight: control.compact ? 30 : 34
        radius: control.compact ? Theme.radiusSmall : Theme.radiusControl
        color: control.enabled ? Theme.fieldBackground : Theme.controlDisabled
        border.color: control.invalid
                      ? Theme.danger
                      : control.activeFocus
                        ? Theme.focus
                        : control.hovered
                          ? Theme.borderStrong
                          : Theme.border
        border.width: control.activeFocus || control.invalid ? 2 : 1

        Behavior on color {
            ColorAnimation {
                duration: Theme.motionFast
            }
        }
    }

    HoverHandler {
        cursorShape: control.enabled ? Qt.IBeamCursor : Qt.ArrowCursor
    }
}
