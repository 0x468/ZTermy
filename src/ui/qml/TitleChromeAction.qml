import QtQuick

// Flat title-bar command (UI V2 chapter 6): the new-terminal button, the tab
// overflow and the quick actions beside the caption buttons. Hover and press
// fill the bar height like a caption button; a badge or a menu is declared as
// a child. focusTarget is the item to hand focus back to after a dialog.
Rectangle {
    id: control

    property string iconName: ""
    property int iconSize: 16
    property color iconColor: Theme.textMuted
    property string actionObjectName: ""
    property string accessibleName: ""
    property string toolTip: ""
    property bool toolTipEnabled: true
    property bool menuOpen: false
    property bool doubleClickEnabled: false
    // Inset of the activation area; 0 lets a narrow control keep its full hit box.
    property int actionInset: 2
    readonly property bool hovered: action.hovered
    readonly property bool pressed: action.pressed
    readonly property bool visualFocus: action.visualFocus
    readonly property Item focusTarget: action

    signal activated
    signal doubleActivated

    function focusAction() {
        action.forceActiveFocus();
    }

    implicitWidth: 40
    implicitHeight: Theme.titleBarHeight
    color: menuOpen && !action.pressed ? Theme.captionHover : action.feedbackColor

    Rectangle {
        visible: action.visualFocus
        anchors.fill: parent
        anchors.margins: 3
        color: "transparent"
        border.color: Theme.focus
        border.width: 1
        radius: Theme.radiusSmall
    }

    AppIcon {
        anchors.centerIn: parent
        width: control.iconSize
        height: control.iconSize
        visible: control.iconName.length > 0
        name: control.iconName
        color: control.iconColor
    }

    KeyboardAction {
        id: action

        objectName: control.actionObjectName
        anchors.fill: parent
        anchors.margins: control.actionInset
        accessibleName: control.accessibleName
        doubleClickEnabled: control.doubleClickEnabled
        onActivated: control.activated()
        onDoubleActivated: control.doubleActivated()
    }

    AppToolTip {
        visible: control.toolTipEnabled && action.hovered && !control.menuOpen && control.toolTip.length > 0
        text: control.toolTip
    }
}
