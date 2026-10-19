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

    implicitHeight: Math.min(240, Math.max(120, sample.cellPixelHeight * 4 + 44))
    color: Theme.terminalBackground
    radius: Theme.radiusControl
    border.color: Theme.border
    clip: true

    Flickable {
        id: viewport
        objectName: "fontSampleViewport"
        anchors.fill: parent
        anchors.margins: 1
        anchors.rightMargin: 14
        anchors.bottomMargin: 14
        clip: true
        contentWidth: Math.max(width, sample.width)
        contentHeight: sample.height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.horizontal: ScrollBar {
            objectName: "fontSampleHorizontalScrollBar"
            parent: root
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 2
            height: 10
            policy: viewport.contentWidth > viewport.width ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
        }
        ScrollBar.vertical: ScrollBar {
            parent: root
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            anchors.margins: 2
            width: 10
            policy: viewport.contentHeight > viewport.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
        }

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
