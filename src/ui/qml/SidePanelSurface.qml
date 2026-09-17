import QtQuick

AppSurface {
    required property string panelTitle
    property bool raised: false

    elevation: raised ? 2 : 0
    radius: 0
    Accessible.role: Accessible.Pane
    Accessible.name: panelTitle
}
