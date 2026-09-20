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
    readonly property color moonColor: Theme.dark ? "#F5E7B2" : "#E8D58B"
    readonly property color moonShadow: Theme.dark ? "#25293A" : "#D4D8E2"
    readonly property real moonPhase: Motion.enabled && !Motion.reduced ? animatedMoonPhase : 0.25
    readonly property real wavePhase: Motion.enabled && !Motion.reduced ? animatedWavePhase : 0
    readonly property real springTide: 0.35 + 0.65 * Math.abs(Math.cos(moonPhase * Math.PI * 2))
    property real hoverTide: hover.hovered && Motion.enabled && !Motion.reduced ? 1 : 0
    property real animatedMoonPhase
    property real animatedWavePhase

    implicitHeight: compact ? 176 : 210
    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("ztermy %1 · %2. %3").arg(version).arg(codename).arg(verse)

    Behavior on hoverTide {
        NumberAnimation {
            duration: Motion.emphasis * 2
            easing.type: Easing.InOutSine
        }
    }

    NumberAnimation on animatedMoonPhase {
        from: 0
        to: 1
        duration: 32000
        loops: Animation.Infinite
        running: Motion.enabled && !Motion.reduced
    }

    NumberAnimation on animatedWavePhase {
        from: 0
        to: Math.PI * 2
        duration: 4800
        loops: Animation.Infinite
        running: Motion.enabled && !Motion.reduced
    }

    function traceMoon(ctx, phase, x, y, radius) {
        const waxing = phase <= 0.5;
        const terminator = Math.cos(phase * Math.PI * 2) * radius * 4 / 3;
        ctx.beginPath();
        ctx.moveTo(x, y - radius);
        ctx.arc(x, y, radius, -Math.PI / 2, Math.PI / 2, !waxing);
        const controlX = x + (waxing ? terminator : -terminator);
        ctx.bezierCurveTo(controlX, y + radius, controlX, y - radius, x, y - radius);
        ctx.closePath();
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
                const moonY = height * 0.31;
                const radius = card.compact ? 22 : 29;

                ctx.fillStyle = card.moonShadow;
                ctx.beginPath();
                ctx.arc(moonX, moonY, radius, 0, Math.PI * 2);
                ctx.fill();

                ctx.save();
                card.traceMoon(ctx, card.moonPhase, moonX, moonY, radius);
                ctx.fillStyle = card.moonColor;
                ctx.fill();
                ctx.clip();
                const craters = [[-0.36, -0.22, 0.18], [0.28, -0.34, 0.12], [0.36, 0.2, 0.2], [-0.18, 0.38, 0.1], [0.02, -0.02, 0.08]];
                for (const crater of craters) {
                    ctx.save();
                    ctx.translate(moonX + crater[0] * radius, moonY + crater[1] * radius);
                    ctx.scale(1, 0.72);
                    ctx.beginPath();
                    ctx.arc(0, 0, crater[2] * radius, 0, Math.PI * 2);
                    ctx.fillStyle = Theme.dark ? "rgba(92, 82, 65, 0.28)" : "rgba(112, 92, 48, 0.24)";
                    ctx.fill();
                    ctx.lineWidth = 1;
                    ctx.strokeStyle = Theme.dark ? "rgba(255, 248, 220, 0.2)" : "rgba(255, 250, 226, 0.55)";
                    ctx.stroke();
                    ctx.restore();
                }
                ctx.restore();

                ctx.globalAlpha = 0.3;
                ctx.strokeStyle = card.moonColor;
                ctx.lineWidth = 1;
                ctx.beginPath();
                ctx.arc(moonX, moonY, radius, 0, Math.PI * 2);
                ctx.stroke();

                ctx.strokeStyle = card.waterColor;
                const amplitude = 3 + card.springTide * 5 + card.hoverTide * 7;
                for (let line = 0; line < 5; ++line) {
                    const y = height * 0.68 + line * 10;
                    ctx.globalAlpha = 0.3 - line * 0.045;
                    ctx.beginPath();
                    ctx.moveTo(width * 0.57, y);
                    const wave = Math.sin(card.wavePhase + line * 0.72);
                    ctx.bezierCurveTo(width * 0.69, y - amplitude * wave, width * 0.86, y + amplitude * wave, width, y - amplitude * 0.25);
                    ctx.stroke();
                }
            }
            Connections {
                target: card
                function onHoverTideChanged() {
                    waterscape.requestPaint();
                }
                function onMoonPhaseChanged() {
                    waterscape.requestPaint();
                }
                function onWavePhaseChanged() {
                    waterscape.requestPaint();
                }
                function onReleaseAccentChanged() {
                    waterscape.requestPaint();
                }
                function onWaterColorChanged() {
                    waterscape.requestPaint();
                }
                function onMoonColorChanged() {
                    waterscape.requestPaint();
                }
                function onMoonShadowChanged() {
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
                text: qsTr("ztermy  /  %1").arg(card.version)
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
