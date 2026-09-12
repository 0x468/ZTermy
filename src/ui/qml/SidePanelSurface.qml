import QtQuick

Rectangle {
    required property string panelTitle
    property bool floating: false

    color: floating ? Theme.floatingBackground : Theme.panelBackground
    border.color: Theme.border
    Accessible.role: Accessible.Pane
    Accessible.name: panelTitle
}
