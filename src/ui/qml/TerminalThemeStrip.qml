pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// Summary of the active terminal theme (ADR 0121): name, ANSI swatches and
// a hint that opens the picker. Reads the resolved palette from the controller.
Item {
    id: strip

    property var controller: null
    readonly property var theme: controller ? controller.terminalThemeColors : ({})
    readonly property color ink: theme.foreground || Theme.terminalForeground

    signal activated

    implicitHeight: 56

    AppSurface {
        anchors.fill: parent
        elevation: 1
        compact: true
        color: strip.theme.background || Theme.terminalBackground
        border.color: action.visualFocus ? Theme.focus : action.hovered ? Theme.accent : Theme.border
        border.width: action.visualFocus ? 2 : 1

        Behavior on border.color {
            MotionColor {}
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: Theme.spacingRelated

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    Layout.fillWidth: true
                    text: strip.theme.name || ""
                    color: strip.ink
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textBody
                    font.weight: Font.DemiBold
                }

                Row {
                    spacing: 3

                    Repeater {
                        model: strip.theme.ansi || []

                        delegate: Rectangle {
                            required property var modelData

                            width: 10
                            height: 10
                            radius: Theme.radiusSmall
                            color: modelData
                        }
                    }
                }
            }

            Text {
                text: qsTr("Change…")
                color: strip.ink
                opacity: 0.8
                font.family: Theme.uiFont
                font.pixelSize: Theme.textLabel
            }
        }
    }

    KeyboardAction {
        id: action
        anchors.fill: parent
        accessibleName: qsTr("Choose terminal theme")
        onActivated: strip.activated()
    }
}
