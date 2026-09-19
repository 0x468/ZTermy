pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

Item {
    id: card

    required property string codename
    required property string version
    required property string verse
    property bool compact: false
    readonly property color releaseAccent: Theme.dark ? "#C4B5FD" : "#7C3AED"
    readonly property color waterColor: Theme.dark ? "#8EAACB" : "#6D86AC"
    property real tide: hover.hovered && Motion.enabled && !Motion.reduced ? 1 : 0

    implicitHeight: compact ? 176 : 210
    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("ztermy %1 · %2. %3").arg(version).arg(codename).arg(verse)

    Behavior on tide {
        NumberAnimation {
            duration: Motion.emphasis * 2
            easing.type: Easing.InOutSine
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusPanel
        color: Theme.panelBackground
        border.width: 1
        border.color: Theme.border
        clip: true

        Canvas {
            id: waterscape
            anchors.fill: parent
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                const moonX = width * 0.82;
                const moonY = height * 0.31 - card.tide * 6;
                const radius = card.compact ? 22 : 29;
                ctx.fillStyle = card.releaseAccent;
                ctx.beginPath();
                ctx.arc(moonX, moonY, radius, -Math.PI / 2, Math.PI / 2);
                ctx.closePath();
                ctx.fill();
                ctx.globalAlpha = 0.25;
                ctx.strokeStyle = card.releaseAccent;
                ctx.lineWidth = 1;
                ctx.beginPath();
                ctx.arc(moonX, moonY, radius, 0, Math.PI * 2);
                ctx.stroke();
                ctx.strokeStyle = card.waterColor;
                for (let line = 0; line < 5; ++line) {
                    const y = height * 0.68 + line * 10;
                    ctx.globalAlpha = 0.3 - line * 0.045;
                    ctx.beginPath();
                    ctx.moveTo(width * 0.57, y);
                    ctx.bezierCurveTo(width * 0.7, y - 8 - card.tide * 9, width * 0.88, y + 9, width, y - 3);
                    ctx.stroke();
                }
            }
            Connections {
                target: card
                function onTideChanged() {
                    waterscape.requestPaint();
                }
                function onReleaseAccentChanged() {
                    waterscape.requestPaint();
                }
                function onWaterColorChanged() {
                    waterscape.requestPaint();
                }
                function onCompactChanged() {
                    waterscape.requestPaint();
                }
            }
        }

        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: card.compact ? 20 : 30
            spacing: card.compact ? 8 : 12

            Text {
                objectName: "settingsApplicationBuildInfo"
                text: "ztermy  /  " + card.version
                color: Theme.textMuted
                font.family: Theme.terminalFont
                font.pixelSize: Theme.textLabel
                font.letterSpacing: 1
            }
            Text {
                text: card.codename
                color: card.releaseAccent
                font.family: Theme.uiFont
                font.pixelSize: card.compact ? 38 : 48
                font.weight: Font.Medium
            }
            Text {
                Layout.maximumWidth: card.width * 0.62
                text: card.verse
                color: Theme.textSoft
                wrapMode: Text.WordWrap
                font.family: Theme.uiFont
                font.pixelSize: card.compact ? 15 : 18
                font.letterSpacing: card.compact ? 1 : 2
            }
        }
    }

    HoverHandler {
        id: hover
    }
}
