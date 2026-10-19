pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    property alias fontFamily: sample.fontFamily
    property alias fontPointSize: sample.fontPointSize
    property alias fontWeight: sample.fontWeight
    property alias lineHeightScale: sample.lineHeightScale
    property alias cellWidthScale: sample.cellWidthScale
    property alias ligaturesEnabled: sample.ligaturesEnabled

    implicitHeight: Math.min(240, Math.max(120, sample.cellPixelHeight * 4 + 30))
    color: Theme.terminalBackground
    radius: Theme.radiusControl
    border.color: Theme.border
    clip: true

    Flickable {
        anchors.fill: parent
        anchors.margins: 1
        contentWidth: Math.max(width, sample.width)
        contentHeight: sample.height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.horizontal: ScrollBar {}
        ScrollBar.vertical: ScrollBar {}

        TerminalFontPreview {
            id: sample
            objectName: "terminalFontPreview"
            width: cellPixelWidth * 64 + 30
            height: cellPixelHeight * 4 + 30
            foregroundOverride: Theme.terminalForeground
            backgroundOverride: Theme.terminalBackground
            backgroundOpacity: 1.0
            enabled: false
        }
    }
}
