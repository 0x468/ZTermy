pragma ComponentBehavior: Bound

import QtQuick

Rectangle {
    id: preview
    property var themeData: ({})
    property bool miniature: false
    readonly property var surface: Theme.surfacePalette(themeData)
    readonly property color ink: themeData.foreground || surface.text
    readonly property color accent: Theme.accentPreference === "theme" && themeData.accent ? themeData.accent : Theme.accentPreference === "custom" ? Theme.customAccent : Theme.accentPreference === "system" ? Theme.systemAccent : themeData.dark ? "#B59AE8" : "#7043A4"
    implicitHeight: miniature ? 84 : 270
    color: themeData.background || surface.content
    border.color: surface.border
    radius: Theme.radiusSmall
    clip: true
    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("Full interface preview: %1").arg(themeData.name || "")

    Rectangle {
        id: bar
        anchors.top: parent.top
        width: parent.width
        height: preview.miniature ? 15 : 30
        color: preview.surface.chrome
        Row {
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 7
            Rectangle {
                width: 6
                height: 6
                radius: 2
                color: preview.accent
            }
            Text {
                text: preview.miniature ? "" : "ztermy"
                color: preview.surface.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textLabel
            }
        }
    }
    Rectangle {
        id: sidebar
        anchors.top: bar.bottom
        anchors.bottom: parent.bottom
        width: preview.miniature ? 28 : 68
        color: preview.surface.panel
        Column {
            anchors.fill: parent
            anchors.margins: preview.miniature ? 6 : 12
            spacing: preview.miniature ? 7 : 16
            Repeater {
                model: 3
                Rectangle {
                    required property int index
                    width: sidebar.width - (preview.miniature ? 12 : 24)
                    height: preview.miniature ? 3 : 5
                    radius: 1
                    color: index === 0 ? preview.accent : preview.surface.muted
                }
            }
        }
    }
    Column {
        anchors.top: bar.bottom
        anchors.left: sidebar.right
        anchors.right: parent.right
        anchors.margins: preview.miniature ? 9 : 14
        spacing: preview.miniature ? 6 : 10
        Repeater {
            model: preview.miniature ? 3 : 0
            Rectangle {
                required property int index
                width: (preview.width - sidebar.width - 18) * (index === 0 ? 0.5 : index === 1 ? 0.85 : 0.65)
                height: 3
                color: index === 0 ? preview.accent : preview.ink
            }
        }
        Text {
            visible: !preview.miniature
            width: parent.width
            text: qsTr("❯ ztermy\nConnected · UTF-8\n\nTerminal and interface, one theme.")
            color: preview.ink
            wrapMode: Text.Wrap
            font.family: Theme.terminalFont
            font.pixelSize: Theme.textLabel
        }
        Rectangle {
            visible: !preview.miniature
            width: parent.width
            height: 28
            color: preview.themeData.selectionBackground || preview.surface.card
            Text {
                anchors.centerIn: parent
                text: qsTr("Selected text")
                color: preview.themeData.selectionForeground || preview.ink
                font.family: Theme.uiFont
                font.pixelSize: Theme.textLabel
            }
        }
        Row {
            spacing: 3
            visible: !preview.miniature
            Repeater {
                model: preview.themeData.ansi ? preview.themeData.ansi.slice(0, 8) : []
                Rectangle {
                    required property var modelData
                    width: Math.max(6, Math.min(18, (preview.width - sidebar.width - 52) / 8))
                    height: 7
                    color: modelData
                }
            }
        }
        Rectangle {
            width: preview.miniature ? 22 : Math.min(parent.width, 130)
            height: preview.miniature ? 7 : 30
            color: preview.surface.floating
            border.color: preview.surface.border
            radius: 3
            Text {
                visible: !preview.miniature
                anchors.centerIn: parent
                text: qsTr("Floating surface")
                color: preview.surface.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textLabel
            }
        }
    }
}
