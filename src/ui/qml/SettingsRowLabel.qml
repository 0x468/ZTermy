import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Row label for a settings grid: the caption, a per-row reset affordance that
// appears when the draft differs from the default, and a highlight pulse used
// by the settings search to point at the row.
Item {
    id: control

    property string text: ""
    property bool dirty: false
    property bool highlighted: false

    signal reset

    implicitHeight: Math.max(label.implicitHeight, 24)
    implicitWidth: row.implicitWidth

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: -6
        anchors.rightMargin: -6
        radius: Theme.radiusCompact
        color: Theme.withAlpha(Theme.accent, control.highlighted ? 0.22 : 0.0)

        Behavior on color {
            MotionColor {}
        }
    }

    RowLayout {
        id: row

        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4

        Label {
            id: label

            text: control.text
            color: Theme.text
        }

        AppIconButton {
            visible: control.dirty
            focusPolicy: Qt.NoFocus
            implicitWidth: 20
            implicitHeight: 20
            label: qsTr("Reset %1 to default").arg(control.text)
            iconName: "refresh"
            iconColor: Theme.textMuted
            onClicked: control.reset()
        }
    }
}
