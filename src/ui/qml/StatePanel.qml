pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

Rectangle {
    id: control

    default property alias actionData: actionRow.data
    property string kind: "empty"
    property string heading: ""
    property string description: ""
    property string detail: ""
    property bool centered: false
    readonly property bool error: kind === "error"
    readonly property bool loading: kind === "loading"

    implicitHeight: stateLayout.implicitHeight + 36
    radius: Theme.radiusPanel
    color: Theme.elevatedBackground
    border.color: error ? Theme.dangerBorder : loading ? Theme.accent : Theme.border
    Accessible.role: error ? Accessible.AlertMessage : Accessible.StaticText
    Accessible.name: heading + (description.length > 0 ? ". " + description : "")

    ColumnLayout {
        id: stateLayout

        anchors.fill: parent
        anchors.margins: 18
        spacing: 7

        Text {
            Layout.fillWidth: true
            text: control.heading
            color: control.error ? Theme.dangerText : control.loading ? Theme.successText : Theme.text
            horizontalAlignment: control.centered ? Text.AlignHCenter : Text.AlignLeft
            wrapMode: Text.WordWrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.textBody
            font.weight: Font.DemiBold
        }

        Text {
            Layout.fillWidth: true
            visible: control.description.length > 0
            text: control.description
            color: control.error ? Theme.textSoft : Theme.textMuted
            horizontalAlignment: control.centered ? Text.AlignHCenter : Text.AlignLeft
            wrapMode: Text.WordWrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.textLabel
        }

        Text {
            Layout.fillWidth: true
            visible: control.detail.length > 0
            text: control.detail
            color: Theme.textMuted
            horizontalAlignment: control.centered ? Text.AlignHCenter : Text.AlignLeft
            wrapMode: Text.WordWrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.textLabel
            opacity: 0.82
        }

        RowLayout {
            id: actionRow

            Layout.fillWidth: true
            Layout.topMargin: visible ? 3 : 0
            visible: children.length > 1

            Item {
                Layout.fillWidth: true
            }
        }
    }
}
