import QtQuick
import QtQuick.Controls

Menu {
    id: control

    implicitWidth: Math.max(196, implicitBackgroundWidth + leftInset + rightInset, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset, implicitContentHeight + topPadding + bottomPadding)
    margins: 8
    overlap: 4
    topPadding: 6
    bottomPadding: 6
    leftPadding: 6
    rightPadding: 6

    delegate: AppMenuItem {}

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: Theme.motionFast
            easing.type: Easing.OutCubic
        }
    }

    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: Theme.motionFast
            easing.type: Easing.InCubic
        }
    }

    contentItem: ListView {
        implicitHeight: contentHeight
        model: control.contentModel
        currentIndex: control.currentIndex
        interactive: Window.window ? contentHeight + control.topPadding + control.bottomPadding > control.height : false
        clip: true

        ScrollIndicator.vertical: ScrollIndicator {}
    }

    background: AppSurface {
        implicitWidth: 196
        implicitHeight: 36
        elevation: 2
        compact: true
    }
}
