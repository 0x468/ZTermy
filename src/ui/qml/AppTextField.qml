pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

TextField {
    id: control

    property string accessibleName: ""
    property bool compact: false
    property bool invalid: false
    property bool passwordRevealable: false
    property bool passwordVisible: false

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    selectByMouse: true
    leftPadding: compact ? 9 : 12
    rightPadding: passwordRevealable ? (compact ? 34 : 40) : (compact ? 9 : 12)
    topPadding: compact ? 5 : 7
    bottomPadding: compact ? 5 : 7
    implicitHeight: compact ? 30 : 34
    color: Theme.text
    placeholderTextColor: Theme.textMuted
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentText
    font.family: Theme.uiFont
    font.pixelSize: compact ? 12 : Theme.textBody
    echoMode: passwordRevealable && !passwordVisible ? TextInput.Password : TextInput.Normal
    Accessible.name: accessibleName.length > 0 ? accessibleName : placeholderText
    onTextChanged: {
        if (text.length === 0) {
            passwordVisible = false;
        }
    }

    background: Rectangle {
        implicitWidth: 120
        implicitHeight: control.compact ? 30 : 34
        radius: control.compact ? Theme.radiusSmall : Theme.radiusControl
        color: control.enabled ? Theme.fieldBackground : Theme.controlDisabled
        border.color: control.invalid ? Theme.danger : control.activeFocus ? Theme.focus : control.hovered ? Theme.borderStrong : Theme.border
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

    ToolButton {
        id: passwordRevealAction

        objectName: control.objectName.length > 0 ? control.objectName + "Reveal" : ""
        anchors.right: parent.right
        anchors.rightMargin: 2
        anchors.verticalCenter: parent.verticalCenter
        width: control.compact ? 28 : 32
        height: control.height - 4
        visible: control.passwordRevealable
        enabled: control.enabled
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.name: control.passwordVisible ? qsTr("Hide password") : qsTr("Show password")
        Accessible.role: Accessible.Button
        onClicked: control.passwordVisible = !control.passwordVisible

        contentItem: AppIcon {
            name: control.passwordVisible ? "eye-off" : "eye"
            color: passwordRevealAction.activeFocus ? Theme.text : Theme.textMuted
        }

        background: Rectangle {
            radius: Theme.radiusSmall
            color: passwordRevealAction.down ? Theme.controlPressed : passwordRevealAction.hovered ? Theme.controlHover : "transparent"
            border.color: passwordRevealAction.activeFocus ? Theme.focus : "transparent"
            border.width: passwordRevealAction.activeFocus ? 2 : 0
        }

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }
    }
}
