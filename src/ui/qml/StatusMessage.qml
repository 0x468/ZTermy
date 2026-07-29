pragma ComponentBehavior: Bound

import QtQuick

Text {
    id: message

    property string kind: "info"
    readonly property bool error: kind === "error"
    readonly property bool success: kind === "success"

    visible: text.length > 0
    color: error ? Theme.dangerText : success ? Theme.successText : Theme.textMuted
    wrapMode: Text.WordWrap
    font.family: Theme.uiFont
    font.pixelSize: Theme.textLabel
    Accessible.role: error ? Accessible.AlertMessage : Accessible.StaticText
    Accessible.name: text
}
