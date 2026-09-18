import QtQuick

// Title-bar tab (UI V2 chapter 6). One treatment for the Workspace, SFTP,
// Settings and terminal tabs: a hover pill inset from the bar, an opaque
// selected card whose rounded top corners meet the page below, and the shared
// focus ring. Consumers set the width; the content row centres itself unless
// a trailing inset reserves room for a close affordance, in which case it is
// left-aligned. A custom glyph (the brand mark) is parented into iconSlot.
Item {
    id: control

    property string title: ""
    property int titlePixelSize: Theme.textLabel
    property string iconName: ""
    property bool customIcon: false
    property bool selected: false
    property bool compact: false
    property string actionObjectName: ""
    property string accessibleName: title
    property string toolTip: ""
    property bool toolTipEnabled: true
    property bool doubleClickEnabled: false
    property color iconColor: selected ? Theme.accent : Theme.textMuted
    property real iconOpacity: 1
    property int iconSize: 16
    property int leadingInset: 10
    property int trailingInset: 0
    property real feedbackAmount: action.hovered || action.visualFocus ? 1 : 0
    readonly property bool hovered: action.hovered
    readonly property bool pressed: action.pressed
    readonly property bool visualFocus: action.visualFocus
    readonly property bool titleVisible: !compact && title.length > 0
    readonly property Item iconSlot: iconHost

    signal activated
    signal doubleActivated

    function focusAction() {
        action.forceActiveFocus();
    }

    implicitWidth: compact ? 38 : 112
    implicitHeight: Theme.titleBarHeight

    Behavior on feedbackAmount {
        MotionFeedback {}
    }

    // Hover pill: inset from the bar so it reads as a control, not a page.
    Rectangle {
        anchors.fill: parent
        anchors.topMargin: 5
        anchors.bottomMargin: 5
        anchors.leftMargin: 2
        anchors.rightMargin: 2
        radius: Theme.radiusCompact
        color: control.pressed ? Theme.captionPressed : Theme.captionHover
        opacity: control.selected ? 0 : control.feedbackAmount

        Behavior on color {
            MotionColor {}
        }
    }

    // Selected card: opaque and drawn over the bar's bottom hairline so it
    // joins the page below.
    Rectangle {
        anchors.fill: parent
        anchors.topMargin: 4
        topLeftRadius: Theme.radiusControl
        topRightRadius: Theme.radiusControl
        color: Theme.tabSelectedBackground
        opacity: control.selected ? 1 : 0

        Behavior on opacity {
            MotionFeedback {}
        }
    }

    Rectangle {
        visible: control.visualFocus
        anchors.fill: parent
        anchors.margins: 3
        color: "transparent"
        border.color: Theme.focus
        border.width: 1
        radius: Theme.radiusSmall
    }

    Row {
        id: content

        readonly property real available: control.width - control.leadingInset - control.trailingInset - 6

        x: control.trailingInset > 0 ? control.leadingInset : Math.max(control.leadingInset, Math.round((control.width - width) / 2))
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 1
        spacing: 8

        Item {
            id: iconHost

            anchors.verticalCenter: parent.verticalCenter
            width: control.iconSize
            height: control.iconSize
            visible: control.iconName.length > 0 || control.customIcon

            AppIcon {
                objectName: control.actionObjectName.length > 0 ? control.actionObjectName + "-icon" : ""
                anchors.fill: parent
                visible: control.iconName.length > 0
                name: control.iconName
                color: control.iconColor
                opacity: control.iconOpacity

                Behavior on color {
                    MotionColor {}
                }
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, Math.min(implicitWidth, content.available - (iconHost.visible ? iconHost.width + content.spacing : 0)))
            visible: control.titleVisible
            text: control.title
            color: control.selected ? Theme.text : Theme.textMuted
            elide: Text.ElideRight
            font.family: Theme.uiFont
            font.pixelSize: control.titlePixelSize
            font.weight: control.selected ? Font.DemiBold : Font.Normal

            Behavior on color {
                MotionColor {}
            }
        }
    }

    KeyboardAction {
        id: action

        objectName: control.actionObjectName
        anchors.fill: parent
        anchors.margins: 2
        anchors.rightMargin: control.trailingInset + 2
        accessibleName: control.accessibleName
        doubleClickEnabled: control.doubleClickEnabled
        onActivated: control.activated()
        onDoubleActivated: control.doubleActivated()
    }

    AppToolTip {
        visible: control.toolTipEnabled && action.hovered && control.toolTip.length > 0
        text: control.toolTip
    }
}
