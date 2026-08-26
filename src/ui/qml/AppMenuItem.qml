import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

MenuItem {
    id: control

    property string iconName: ""
    property string shortcutText: ""

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(34, implicitContentHeight + topPadding + bottomPadding, implicitIndicatorHeight + topPadding + bottomPadding)
    height: visible ? implicitHeight : 0
    leftPadding: 10
    rightPadding: 10
    topPadding: 7
    bottomPadding: 7
    spacing: 8
    hoverEnabled: true

    contentItem: RowLayout {
        spacing: 9

        Item {
            Layout.preferredWidth: control.checkable || control.iconName.length > 0 ? 16 : 0
            Layout.preferredHeight: 16

            AppIcon {
                anchors.fill: parent
                visible: control.iconName.length > 0 && !(control.checkable && control.checked)
                name: control.iconName
                color: control.enabled ? Theme.textSoft : Theme.textSubtle
            }
        }

        Text {
            Layout.fillWidth: true
            text: control.text
            color: control.enabled ? Theme.text : Theme.textSubtle
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            font.family: Theme.uiFont
            font.pixelSize: Theme.textBody
        }

        Text {
            visible: control.shortcutText.length > 0
            text: control.shortcutText
            color: Theme.textMuted
            font.family: Theme.uiFont
            font.pixelSize: Theme.textLabel
        }

        Item {
            Layout.preferredWidth: control.subMenu ? 14 : 0
        }
    }

    indicator: AppIcon {
        x: control.leftPadding
        y: Math.round((control.height - height) / 2)
        width: 14
        height: 14
        visible: control.checkable && control.checked
        name: "check"
        color: Theme.accent
    }

    arrow: AppIcon {
        x: control.width - width - control.rightPadding
        y: Math.round((control.height - height) / 2)
        width: 14
        height: 14
        visible: control.subMenu !== null
        name: "chevron-down"
        rotation: -90
        color: Theme.textMuted
    }

    background: Rectangle {
        x: 2
        y: 1
        width: control.width - 4
        height: control.height - 2
        radius: Theme.radiusSmall
        color: control.down ? Theme.controlPressed : control.highlighted ? Theme.controlHover : "transparent"
        border.color: control.visualFocus ? Theme.focus : "transparent"
        border.width: control.visualFocus ? 2 : 0

        Behavior on color {
            ColorAnimation {
                duration: Theme.motionFast
            }
        }
    }

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
}
