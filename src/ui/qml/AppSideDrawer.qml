pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: drawer

    required property string panelTitle
    default property alias body: bodyColumn.data
    property bool pinned: false
    property bool dismissBlocked: false
    property string entryKey: ""
    property var payload: null
    property string pendingKey: ""
    property var pendingPayload: null
    property real slideOffset: 0
    property bool closing: false
    readonly property bool changingEntry: replacement.running || pendingKey.length > 0
    signal dismissalBlocked

    width: Math.min(420, parent ? parent.width : 420)
    height: parent ? parent.height : 0
    x: (parent ? parent.width : width) - width + slideOffset
    y: 0
    margins: -1
    padding: 18
    modal: false
    dim: false
    focus: true
    clip: true
    closePolicy: pinned || dismissBlocked ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside

    function requestClose() {
        if (dismissBlocked) {
            dismissalBlocked();
            return false;
        }
        pendingKey = "";
        pendingPayload = null;
        close();
        return true;
    }

    function present(key, value) {
        if (dismissBlocked && entryKey !== key) {
            dismissalBlocked();
            return;
        }
        if (closing) {
            pendingKey = key;
            pendingPayload = value;
        } else if (!visible || !opened) {
            replacement.stop();
            entryKey = key;
            payload = value;
            open();
        } else if (key === entryKey && !replacement.running) {
            payload = value;
        } else {
            replacement.stop();
            pendingKey = key;
            pendingPayload = value;
            replacement.start();
        }
    }

    function refresh(key, value) {
        if (key === entryKey)
            payload = value;
        if (key === pendingKey)
            pendingPayload = value;
    }

    onAboutToHide: {
        closing = true;
        replacement.stop();
        pendingKey = "";
        pendingPayload = null;
    }
    onClosed: {
        closing = false;
        entryKey = "";
        payload = null;
        if (pendingKey.length > 0) {
            entryKey = pendingKey;
            payload = pendingPayload;
            open();
        }
        pendingKey = "";
        pendingPayload = null;
    }

    enter: Transition {
        NumberAnimation {
            target: drawer
            property: "slideOffset"
            from: drawer.width
            to: 0
            duration: Theme.motionMedium / 2
            easing.type: Easing.OutCubic
        }
    }
    exit: Transition {
        NumberAnimation {
            target: drawer
            property: "slideOffset"
            to: drawer.width
            duration: Theme.motionMedium / 2
            easing.type: Easing.InCubic
        }
    }
    SequentialAnimation {
        id: replacement
        NumberAnimation {
            target: drawer
            property: "slideOffset"
            to: drawer.width
            duration: Theme.motionMedium / 2
            easing.type: Easing.InCubic
        }
        ScriptAction {
            script: {
                drawer.entryKey = drawer.pendingKey;
                drawer.payload = drawer.pendingPayload;
                drawer.pendingKey = "";
                drawer.pendingPayload = null;
            }
        }
        NumberAnimation {
            target: drawer
            property: "slideOffset"
            to: 0
            duration: Theme.motionMedium / 2
            easing.type: Easing.OutCubic
        }
    }

    background: SidePanelSurface {
        panelTitle: drawer.panelTitle
        raised: true
    }
    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: drawer.panelTitle
                color: Theme.text
                font.pixelSize: 18
                font.weight: Font.Bold
                elide: Text.ElideRight
            }
            AppIconButton {
                objectName: "sideDrawerPin"
                label: drawer.pinned ? qsTr("Unpin panel") : qsTr("Pin panel")
                iconName: "pin"
                selected: drawer.pinned
                onClicked: drawer.pinned = !drawer.pinned
            }
            AppIconButton {
                objectName: "sideDrawerClose"
                label: qsTr("Close panel")
                iconName: "close"
                onClicked: drawer.requestClose()
            }
        }
        ScrollView {
            id: bodyScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                id: bodyColumn
                width: bodyScroll.availableWidth
                spacing: 12
            }
        }
    }
}
