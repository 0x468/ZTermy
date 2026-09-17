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

    enter: MotionEnter {}

    exit: MotionExit {}

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
