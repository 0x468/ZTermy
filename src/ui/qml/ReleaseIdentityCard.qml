pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import QtQuick3D

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
    readonly property real moonPhase: Motion.enabled && !Motion.reduced ? animatedMoonPhase : 0.25
    readonly property real moonRotation: Motion.enabled && !Motion.reduced ? animatedMoonRotation : 0
    readonly property real wavePhase: Motion.enabled && !Motion.reduced ? animatedWavePhase : 0
    readonly property real springTide: 0.35 + 0.65 * Math.abs(Math.cos(moonPhase * Math.PI * 2))
    property real hoverTide: hover.hovered && Motion.enabled && !Motion.reduced ? 1 : 0
    property real animatedMoonPhase
    property real animatedMoonRotation
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
        duration: hover.hovered ? 11000 : 32000
        loops: Animation.Infinite
        running: Motion.enabled && !Motion.reduced
    }

    NumberAnimation on animatedMoonRotation {
        from: 0
        to: 360
        duration: hover.hovered ? 60000 : 120000
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

    View3D {
        id: moon

        readonly property real restingSize: Math.max(0, card.height - (card.compact ? 14 : 22))
        width: restingSize
        height: restingSize
        x: card.width - width - (card.compact ? 18 : 28)
        y: hover.hovered && Motion.enabled && !Motion.reduced ? (card.compact ? -14 : -24) : (card.height - height) / 2
        scale: hover.hovered && Motion.enabled && !Motion.reduced ? 1.18 : 1
        transformOrigin: Item.Center
        z: 6

        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Transparent
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        PerspectiveCamera {
            z: 205
        }

        DirectionalLight {
            eulerRotation.x: -18
            eulerRotation.y: card.moonPhase * 360
            color: "#FFF0CE"
            ambientColor: Theme.dark ? "#272331" : "#57505E"
            brightness: 3.4
        }

        Texture {
            id: moonTexture

            source: "qrc:/ztermy/release/moon-1024.jpg"
            generateMipmaps: true
        }

        Model {
            source: "#Sphere"
            scale: Qt.vector3d(2, 2, 2)
            eulerRotation.x: 4
            eulerRotation.y: 98 + card.moonRotation
            eulerRotation.z: -2
            materials: PrincipledMaterial {
                baseColor: Theme.dark ? "#F0E8D5" : "#F3E5C4"
                baseColorMap: moonTexture
                heightMap: moonTexture
                heightAmount: 0.055
                metalness: 0
                roughness: 0.96
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
