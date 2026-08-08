pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

Rectangle {
    id: control

    default property alias contentData: contentLayout.data
    property string heading: ""
    property string description: ""
    property bool compact: false

    implicitHeight: cardLayout.implicitHeight + (compact ? 20 : 36)
    radius: compact ? Theme.radiusControl : Theme.radiusPanel
    color: compact ? Theme.panelBackground : Theme.elevatedBackground
    border.color: Theme.border
    Accessible.name: heading

    ColumnLayout {
        id: cardLayout

        anchors.fill: parent
        anchors.margins: control.compact ? 10 : 18
        spacing: control.compact ? 8 : 12

        Text {
            Layout.fillWidth: true
            visible: control.heading.length > 0
            text: control.heading
            color: Theme.text
            wrapMode: Text.WordWrap
            font.family: Theme.uiFont
            font.pixelSize: control.compact ? Theme.textBody : 16
            font.weight: Font.DemiBold
        }

        Text {
            Layout.fillWidth: true
            visible: control.description.length > 0
            text: control.description
            color: Theme.textMuted
            wrapMode: Text.WordWrap
            font.family: Theme.uiFont
            font.pixelSize: control.compact ? Theme.textLabel : Theme.textBody
        }

        ColumnLayout {
            id: contentLayout

            Layout.fillWidth: true
            spacing: control.compact ? 8 : 12
        }
    }
}
