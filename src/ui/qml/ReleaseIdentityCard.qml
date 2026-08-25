pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

Item {
    id: card

    required property string codename
    required property string version
    required property string verse
    property bool compact: false
    readonly property color releaseAccent: Theme.dark ? "#A78BFA" : "#7C3AED"
    readonly property color releaseAccentSoft: Theme.dark ? "#C4B5FD" : "#8B5CF6"
    readonly property color releaseHighlight: Theme.dark ? "#DDD6FE" : "#A78BFA"

    implicitHeight: compact ? 176 : 210
    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("ztermy version %1, codename %2. %3").arg(version).arg(codename).arg(verse)

    Rectangle {
        id: surface

        anchors.fill: parent
        clip: true
        radius: Theme.radiusPanel
        border.width: 1
        border.color: Theme.dark ? Qt.rgba(0.65, 0.55, 0.98, 0.34) : Qt.rgba(0.49, 0.23, 0.93, 0.26)
        gradient: Gradient {
            orientation: Gradient.Horizontal

            GradientStop {
                position: 0
                color: Theme.dark ? "#17152B" : "#F5F1FF"
            }

            GradientStop {
                position: 0.58
                color: Theme.dark ? "#1C1932" : "#FBF9FF"
            }

            GradientStop {
                position: 1
                color: Theme.dark ? "#151525" : "#F1EEFA"
            }
        }

        Rectangle {
            width: card.compact ? 210 : 320
            height: width
            x: -width * 0.42
            y: (parent.height - height) / 2
            radius: width / 2
            color: Qt.rgba(0.55, 0.36, 0.96, Theme.dark ? 0.11 : 0.13)
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: card.compact ? 10 : 28
            anchors.verticalCenter: parent.verticalCenter
            text: card.codename
            color: card.releaseAccent
            opacity: Theme.dark ? 0.055 : 0.075
            font.family: Theme.uiFont
            font.pixelSize: card.compact ? 124 : 176
            font.weight: Font.Black
        }

        Rectangle {
            id: sweep

            width: card.compact ? 64 : 104
            height: parent.height
            opacity: Theme.dark ? 0.18 : 0.12
            gradient: Gradient {
                orientation: Gradient.Horizontal

                GradientStop {
                    position: 0
                    color: "transparent"
                }

                GradientStop {
                    position: 0.5
                    color: card.releaseAccent
                }

                GradientStop {
                    position: 1
                    color: "transparent"
                }
            }

            NumberAnimation on x {
                from: -sweep.width
                to: card.width + sweep.width
                duration: 7200
                loops: Animation.Infinite
                running: card.visible && Theme.animationsEnabled
                easing.type: Easing.InOutSine
            }
        }

        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 2
            gradient: Gradient {
                orientation: Gradient.Horizontal

                GradientStop {
                    position: 0
                    color: "transparent"
                }

                GradientStop {
                    position: 0.25
                    color: card.releaseAccent
                }

                GradientStop {
                    position: 0.72
                    color: card.releaseAccentSoft
                }

                GradientStop {
                    position: 1
                    color: "transparent"
                }
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.margins: card.compact ? 18 : 28
            spacing: card.compact ? 16 : 30

            Item {
                id: codenameMedallion

                Layout.preferredWidth: card.compact ? 94 : 132
                Layout.preferredHeight: Layout.preferredWidth
                Layout.alignment: Qt.AlignVCenter

                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: "transparent"
                    border.width: 1
                    border.color: Qt.rgba(0.76, 0.71, 0.98, Theme.dark ? 0.38 : 0.50)
                }

                Item {
                    id: orbit

                    anchors.fill: parent

                    Rectangle {
                        x: (parent.width - width) / 2
                        y: -height / 2
                        width: card.compact ? 6 : 8
                        height: width
                        radius: width / 2
                        color: card.releaseHighlight
                    }

                    NumberAnimation on rotation {
                        from: 0
                        to: 360
                        duration: 12000
                        loops: Animation.Infinite
                        running: card.visible && Theme.animationsEnabled
                    }
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: card.compact ? 72 : 104
                    height: width
                    radius: card.compact ? 20 : 28
                    border.width: 1
                    border.color: Qt.rgba(0.76, 0.71, 0.98, Theme.dark ? 0.48 : 0.58)
                    gradient: Gradient {
                        GradientStop {
                            position: 0
                            color: Theme.dark ? "#32245B" : "#EDE9FE"
                        }

                        GradientStop {
                            position: 1
                            color: Theme.dark ? "#21173F" : "#D8B4FE"
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: card.codename
                        color: Theme.dark ? "#F5F3FF" : "#3B176B"
                        font.family: Theme.uiFont
                        font.pixelSize: card.compact ? 44 : 64
                        font.weight: Font.DemiBold
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: card.compact ? 7 : 11

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("ZTERMY RELEASE")
                        color: Theme.textMuted
                        elide: Text.ElideRight
                        font.family: Theme.uiFont
                        font.pixelSize: card.compact ? Theme.textCompact : Theme.textLabel
                        font.letterSpacing: card.compact ? 2 : 3
                        font.weight: Font.DemiBold
                    }

                    Rectangle {
                        Layout.preferredWidth: versionText.implicitWidth + (card.compact ? 16 : 22)
                        Layout.preferredHeight: card.compact ? 25 : 30
                        radius: height / 2
                        color: Qt.rgba(0.55, 0.36, 0.96, Theme.dark ? 0.17 : 0.13)
                        border.width: 1
                        border.color: Qt.rgba(0.55, 0.36, 0.96, Theme.dark ? 0.46 : 0.38)

                        Text {
                            id: versionText

                            objectName: "settingsApplicationBuildInfo"
                            anchors.centerIn: parent
                            text: card.version
                            color: Theme.dark ? "#DDD6FE" : "#6D28D9"
                            font.family: Theme.terminalFont
                            font.pixelSize: card.compact ? Theme.textCompact : Theme.textLabel
                            font.weight: Font.DemiBold
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Codename · %1").arg(card.codename)
                    color: Theme.text
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: card.compact ? 24 : 34
                    font.weight: Font.DemiBold
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Qt.rgba(0.76, 0.71, 0.98, Theme.dark ? 0.24 : 0.34)
                }

                Text {
                    Layout.fillWidth: true
                    text: "“" + card.verse + "”"
                    color: Theme.textSoft
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: card.compact ? Theme.textBody : 17
                    font.letterSpacing: card.compact ? 0.5 : 1
                }
            }
        }
    }
}
