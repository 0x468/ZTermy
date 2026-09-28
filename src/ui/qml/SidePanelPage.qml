import QtQuick
import QtQuick.Layouts

// Fixed-height header/tools; only the body receives surplus vertical space.
Item {
    id: page

    default property alias content: body.data
    property alias header: headerSlot.data
    property alias tools: toolsSlot.data
    property real padding: 8
    property real spacing: 6

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: page.padding
        spacing: page.spacing

        ColumnLayout {
            id: headerSlot
            Layout.fillWidth: true
            Layout.fillHeight: false
            visible: children.length > 0
            spacing: page.spacing
        }

        ColumnLayout {
            id: toolsSlot
            Layout.fillWidth: true
            Layout.fillHeight: false
            visible: children.length > 0
            spacing: page.spacing
        }

        ColumnLayout {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: page.spacing
        }
    }
}
