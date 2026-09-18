import QtQuick

// Floating card that follows the pointer while a tab or pane is dragged
// (elevation 2). Consumers place it beside the pointer and toggle visible.
AppSurface {
    id: preview

    property string title: ""
    property string iconName: "terminal"

    width: 220
    height: 32
    elevation: 2
    compact: true
    border.color: Theme.accent

    Row {
        id: content

        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 8

        AppIcon {
            id: icon

            anchors.verticalCenter: parent.verticalCenter
            width: 14
            height: 14
            visible: preview.iconName.length > 0
            name: preview.iconName
            color: Theme.accent
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: content.width - (icon.visible ? icon.width + content.spacing : 0)
            text: preview.title
            elide: Text.ElideRight
            color: Theme.text
            font.family: Theme.uiFont
            font.pixelSize: Theme.textLabel
        }
    }
}
