pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

Rectangle {
    id: control

    default property alias contentData: contentLayout.data
    property string heading: ""
    property string description: ""

    implicitHeight: cardLayout.implicitHeight + 36
    radius: Theme.radiusPanel
    color: Theme.elevatedBackground
    border.color: Theme.border
    Accessible.name: heading

    ColumnLayout {
        id: cardLayout

        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        Text {
            Layout.fillWidth: true
            visible: control.heading.length > 0
            text: control.heading
            color: Theme.text
            wrapMode: Text.WordWrap
            font.family: Theme.uiFont
            font.pixelSize: 16
            font.weight: Font.DemiBold
        }

        Text {
            Layout.fillWidth: true
            visible: control.description.length > 0
            text: control.description
            color: Theme.textMuted
            wrapMode: Text.WordWrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.textBody
        }

        ColumnLayout {
            id: contentLayout

            Layout.fillWidth: true
            spacing: 12
        }
    }
}
