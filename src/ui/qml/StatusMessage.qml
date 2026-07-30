pragma ComponentBehavior: Bound

import QtQuick

Text {
    id: message

    property string kind: "info"
    readonly property bool error: kind === "error"
    readonly property bool success: kind === "success"

    visible: text.length > 0 || opacity > 0.001
    opacity: text.length > 0 ? 1.0 : 0.0
    color: error ? Theme.dangerText : success ? Theme.successText : Theme.textMuted
    wrapMode: Text.WordWrap
    font.family: Theme.uiFont
    font.pixelSize: Theme.textLabel
    Accessible.role: error ? Accessible.AlertMessage : Accessible.StaticText
    Accessible.name: text

    Behavior on opacity {
        NumberAnimation {
            duration: message.text.length > 0 ? Theme.motionMedium : Theme.motionFast
            easing.type: message.text.length > 0 ? Easing.OutCubic : Easing.InCubic
        }
    }

    Behavior on color {
        ColorAnimation {
            duration: Theme.motionFast
        }
    }
}
