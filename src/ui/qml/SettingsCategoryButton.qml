import QtQuick

// Settings and workbench navigation share pointer, keyboard and surface feedback.
SideNavigationItem {
    id: control

    required property string title
    property string category: ""

    text: title
    accessibleName: qsTr("%1 settings").arg(control.title)
    implicitHeight: 36
}
