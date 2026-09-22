pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects

Item {
    id: card

    required property string codename
    required property string version
    required property string verse
    property bool compact: false
    readonly property color releaseAccent: Theme.dark ? "#D8C7F2" : "#8B593C"
    readonly property color skyTop: Theme.dark ? "#17141F" : "#FFF9F1"
    readonly property color skyBottom: Theme.dark ? "#292038" : "#EBD8C5"
    readonly property color waterTop: Theme.dark ? "#8265A2" : "#BD7548"
    readonly property color waterBottom: Theme.dark ? "#17131F" : "#6A4532"
    readonly property color moonColor: Theme.dark ? "#F3E7C1" : "#F0D7A8"
    readonly property color moonShadow: Theme.dark ? "#17151D" : "#766A61"
    readonly property real moonPhase: Motion.enabled && !Motion.reduced ? animatedMoonPhase % 1 : 0.1
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
        from: 0.1
        to: 1.1
        duration: hover.hovered ? 11000 : 32000
        loops: Animation.Infinite
        running: Motion.enabled && !Motion.reduced
    }

    NumberAnimation on animatedWavePhase {
        from: 0
        to: Math.PI * 2
        duration: hover.hovered ? 2500 : 5200
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
        id: surface

        anchors.fill: parent
        radius: Theme.radiusPanel
        color: card.skyTop
        clip: true

        Item {
            id: waterLayer

            anchors.fill: parent
            layer.enabled: true

            Canvas {
                id: waterscape

                anchors.fill: parent
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d");
                    ctx.reset();
                    const radius = Theme.radiusPanel;
                    const curve = radius * 0.55228475;
                    ctx.beginPath();
                    ctx.moveTo(radius, 0);
                    ctx.lineTo(width - radius, 0);
                    ctx.bezierCurveTo(width - radius + curve, 0, width, radius - curve, width, radius);
                    ctx.lineTo(width, height - radius);
                    ctx.bezierCurveTo(width, height - radius + curve, width - radius + curve, height, width - radius, height);
                    ctx.lineTo(radius, height);
                    ctx.bezierCurveTo(radius - curve, height, 0, height - radius + curve, 0, height - radius);
                    ctx.lineTo(0, radius);
                    ctx.bezierCurveTo(0, radius - curve, radius - curve, 0, radius, 0);
                    ctx.closePath();
                    ctx.clip();
                    const sky = ctx.createLinearGradient(0, 0, width, height);
                    sky.addColorStop(0, card.skyTop);
                    sky.addColorStop(1, card.skyBottom);
                    ctx.fillStyle = sky;
                    ctx.fillRect(0, 0, width, height);

                    const tide = Math.sin(card.wavePhase);
                    const tideHeight = (8 + card.springTide * 12) * tide + card.hoverTide * 9;
                    const base = height * 0.62 - tideHeight;
                    for (let layer = 0; layer < 4; ++layer) {
                        const y0 = base + layer * (card.compact ? 9 : 12);
                        const amplitude = 7 + layer * 3 + card.springTide * 5 + card.hoverTide * 4;
                        ctx.beginPath();
                        ctx.moveTo(0, height);
                        ctx.lineTo(0, y0);
                        for (let x = 0; x <= width + 8; x += 8) {
                            const y = y0 + Math.sin(x * 0.018 + card.wavePhase + layer * 1.7) * amplitude + Math.sin(x * 0.006 - card.wavePhase * 2) * amplitude * 0.42;
                            ctx.lineTo(x, y);
                        }
                        ctx.lineTo(width, height);
                        ctx.closePath();
                        const water = ctx.createLinearGradient(0, y0, 0, height);
                        water.addColorStop(0, layer === 0 ? card.waterTop : Qt.rgba(card.waterTop.r, card.waterTop.g, card.waterTop.b, 0.62 - layer * 0.1));
                        water.addColorStop(1, card.waterBottom);
                        ctx.fillStyle = water;
                        ctx.fill();
                        ctx.globalAlpha = 0.36 - layer * 0.055;
                        ctx.strokeStyle = card.releaseAccent;
                        ctx.lineWidth = layer === 0 ? 1.4 : 1;
                        ctx.stroke();
                        ctx.globalAlpha = 1;
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
                    function onSkyTopChanged() {
                        waterscape.requestPaint();
                    }
                    function onSkyBottomChanged() {
                        waterscape.requestPaint();
                    }
                    function onWaterTopChanged() {
                        waterscape.requestPaint();
                    }
                    function onWaterBottomChanged() {
                        waterscape.requestPaint();
                    }
                    function onReleaseAccentChanged() {
                        waterscape.requestPaint();
                    }
                    function onCompactChanged() {
                        waterscape.requestPaint();
                    }
                }
            }
        }

        Text {
            id: buildInfo

            objectName: "settingsApplicationBuildInfo"
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.leftMargin: card.compact ? 20 : 30
            anchors.topMargin: card.compact ? 18 : 24
            text: qsTr("ZTERMY / %1").arg(card.version)
            color: Theme.textMuted
            font.family: Theme.terminalFont
            font.pixelSize: card.compact ? 9 : 10
            font.letterSpacing: 0.7
        }

        Item {
            id: glyph

            anchors.left: buildInfo.left
            anchors.top: buildInfo.bottom
            anchors.topMargin: card.compact ? 5 : 7
            width: card.compact ? 126 : 154
            height: card.compact ? 92 : 112

            Text {
                id: glyphMask

                anchors.fill: parent
                text: card.codename
                color: "white"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.family: Theme.uiFont
                font.pixelSize: card.compact ? 78 : 100
                font.weight: Font.Normal
                layer.enabled: true
            }

            ShaderEffectSource {
                id: glyphMaskSource

                anchors.fill: parent
                sourceItem: glyphMask
                hideSource: true
                live: true
                visible: false
            }

            ShaderEffectSource {
                id: glyphWater

                anchors.fill: parent
                sourceItem: waterLayer
                sourceRect: Qt.rect(glyph.x + 6 + Math.sin(card.wavePhase) * 3, glyph.y + 4, glyph.width / 1.1, glyph.height / 1.1)
                textureSize: Qt.size(Math.ceil(width), Math.ceil(height))
                live: true
                visible: false
            }

            MultiEffect {
                anchors.fill: parent
                source: glyphWater
                maskEnabled: true
                maskSource: glyphMaskSource
                saturation: 0.18
                brightness: 0.12
                autoPaddingEnabled: false
            }

            Text {
                anchors.fill: parent
                text: card.codename
                color: Qt.rgba(card.releaseAccent.r, card.releaseAccent.g, card.releaseAccent.b, 0.12)
                style: Text.Outline
                styleColor: card.releaseAccent
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.family: Theme.uiFont
                font.pixelSize: card.compact ? 78 : 100
                font.weight: Font.Normal
            }
        }

        Text {
            anchors.left: buildInfo.left
            anchors.bottom: parent.bottom
            anchors.bottomMargin: card.compact ? 16 : 20
            width: glyph.width
            text: card.verse
            color: Theme.textSoft
            horizontalAlignment: Text.AlignLeft
            font.family: Theme.uiFont
            font.pixelSize: card.compact ? 13 : 15
            font.letterSpacing: card.compact ? 1.2 : 1.8
        }
    }

    Canvas {
        id: moon

        readonly property real restingSize: Math.max(0, card.height - (card.compact ? 14 : 22))
        width: restingSize
        height: restingSize
        x: card.width - width - (card.compact ? 18 : 28)
        y: hover.hovered && Motion.enabled && !Motion.reduced ? (card.compact ? -14 : -24) : (card.height - height) / 2
        scale: hover.hovered && Motion.enabled && !Motion.reduced ? 1.18 : 1
        transformOrigin: Item.Center
        z: 6

        antialiasing: true
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            const centerX = width / 2;
            const centerY = height / 2;
            const radius = Math.min(width, height) * 0.46;

            ctx.fillStyle = card.moonShadow;
            ctx.beginPath();
            ctx.arc(centerX, centerY, radius, 0, Math.PI * 2);
            ctx.fill();

            ctx.save();
            card.traceMoon(ctx, card.moonPhase, centerX, centerY, radius);
            const surface = ctx.createRadialGradient(centerX - radius * 0.32, centerY - radius * 0.34, radius * 0.08, centerX, centerY, radius);
            surface.addColorStop(0, Theme.dark ? "#FFF8DE" : "#FFF1C9");
            surface.addColorStop(0.72, card.moonColor);
            surface.addColorStop(1, Theme.dark ? "#B6A77D" : "#B99A69");
            ctx.fillStyle = surface;
            ctx.fill();
            ctx.clip();

            const craters = [[-0.34, -0.24, 0.16], [0.26, -0.33, 0.11], [0.36, 0.18, 0.19], [-0.18, 0.36, 0.1], [0.02, -0.02, 0.075], [-0.42, 0.12, 0.07], [0.16, 0.42, 0.06]];
            for (const crater of craters) {
                ctx.save();
                ctx.translate(centerX + crater[0] * radius, centerY + crater[1] * radius);
                ctx.scale(1, 0.72);
                ctx.beginPath();
                ctx.arc(0, 0, crater[2] * radius, 0, Math.PI * 2);
                ctx.fillStyle = Theme.dark ? "rgba(91, 80, 58, 0.3)" : "rgba(112, 82, 48, 0.24)";
                ctx.fill();
                ctx.lineWidth = 1;
                ctx.strokeStyle = Theme.dark ? "rgba(255, 248, 220, 0.22)" : "rgba(255, 246, 218, 0.58)";
                ctx.stroke();
                ctx.restore();
            }
            ctx.restore();

            ctx.globalAlpha = 0.34;
            ctx.strokeStyle = card.moonColor;
            ctx.lineWidth = 1;
            ctx.beginPath();
            ctx.arc(centerX, centerY, radius, 0, Math.PI * 2);
            ctx.stroke();
            ctx.globalAlpha = 1;
        }

        Connections {
            target: card
            function onMoonPhaseChanged() {
                moon.requestPaint();
            }
            function onMoonColorChanged() {
                moon.requestPaint();
            }
            function onMoonShadowChanged() {
                moon.requestPaint();
            }
        }

        Behavior on y {
            NumberAnimation {
                duration: Motion.emphasis * 2
                easing.type: Easing.OutCubic
            }
        }
        Behavior on scale {
            NumberAnimation {
                duration: Motion.emphasis * 2
                easing.type: Easing.OutCubic
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusPanel
        color: "transparent"
        border.width: 1
        border.color: Theme.border
        z: 5
    }

    HoverHandler {
        id: hover
    }
}
