pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Window

RowLayout {
    id: root
    required property var controller
    required property string paneId
    property int paneCount: 1
    property bool headersVisible: false
    property bool zoomed: false
    property bool detached: false
    property bool dimmed: false
    signal zoomRequested
    signal detachRequested
    signal toggleHeadersRequested
    spacing: detached ? 0 : 2
    opacity: headersVisible || hover.hovered || activeFocus || newPaneMenu.visible || !dimmed ? 1 : detached ? 0 : 0.18

    Behavior on opacity {
        NumberAnimation {
            duration: Theme.animationsEnabled ? Theme.motionFast : 0
        }
    }
    HoverHandler {
        id: hover
        onHoveredChanged: {
            if (hovered) {
                root.dimmed = false;
                idle.stop();
            } else
                idle.restart();
        }
    }
    Timer {
        id: idle
        interval: 2200
        running: true
        onTriggered: root.dimmed = true
    }
    onVisibleChanged: {
        dimmed = false;
        idle.restart();
    }

    function createPane(profile, shell, copy) {
        if (controller.activateTerminalPane(paneId))
            controller.splitActiveTerminal("horizontal", copy, profile, shell);
    }

    Repeater {
        model: [
            {
                id: "headers",
                icon: "list",
                label: root.headersVisible ? qsTr("Hide headers") : qsTr("Show headers"),
                shown: true
            },
            {
                id: "zoom",
                icon: "locate",
                label: root.zoomed ? qsTr("Restore pane layout") : qsTr("Zoom this pane within its tab"),
                shown: root.paneCount > 1 && !root.detached
            },
            {
                id: "detach",
                icon: "external-link",
                label: root.detached ? qsTr("Reattach terminal pane") : qsTr("Detach terminal pane"),
                shown: true
            },
            {
                id: "copy",
                icon: "copy",
                label: qsTr("Copy pane — new session, same profile or Shell"),
                shown: !root.detached
            },
            {
                id: "new",
                icon: "plus",
                label: qsTr("New pane — choose a host or local Shell"),
                shown: !root.detached
            },
            {
                id: "close",
                icon: "close",
                label: qsTr("Close this pane"),
                shown: root.paneCount > 1 && !root.detached
            }
        ]
        delegate: AppIconButton {
            id: button
            required property var modelData
            objectName: "terminalPaneAction-" + modelData.id + "-" + root.paneId
            visible: modelData.shown
            Layout.preferredWidth: 28
            Layout.preferredHeight: root.detached ? 32 : 28
            label: modelData.label
            iconName: modelData.icon
            selected: (modelData.id === "headers" && root.headersVisible) || (modelData.id === "zoom" && root.zoomed)
            iconColor: selected ? Theme.accent : Theme.text
            toolTipEnabled: !newPaneMenu.visible
            onClicked: {
                switch (modelData.id) {
                case "headers":
                    root.toggleHeadersRequested();
                    break;
                case "zoom":
                    root.zoomRequested();
                    break;
                case "detach":
                    root.detachRequested();
                    break;
                case "copy":
                    root.createPane("", "", true);
                    break;
                case "new":
                    newPaneMenu.popup(button, 0, button.height);
                    break;
                case "close":
                    if (root.controller.activateTerminalPane(root.paneId))
                        root.controller.closeActiveTerminalPane();
                    break;
                }
            }
        }
    }
    QtObject {
        id: detachedChrome
        readonly property bool maximized: root.Window.window && root.Window.window.visibility === Window.Maximized
    }
    Repeater {
        model: root.detached ? ["minimize", "maximize", "close"] : []
        delegate: CaptionButton {
            required property string modelData
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            kind: modelData
            chrome: detachedChrome
            nativeMaximizeHandling: false
            accessibleName: modelData === "minimize" ? qsTranslate("TitleWindowActions", "Minimize") : modelData === "close" ? qsTranslate("TitleWindowActions", "Close") : detachedChrome.maximized ? qsTranslate("TitleWindowActions", "Restore") : qsTranslate("TitleWindowActions", "Maximize")
            onActivated: {
                const window = root.Window.window;
                if (modelData === "minimize")
                    window.showMinimized();
                else if (modelData === "maximize") {
                    if (detachedChrome.maximized)
                        window.showNormal();
                    else
                        window.showMaximized();
                } else
                    window.close();
            }
        }
    }
    AppMenu {
        id: newPaneMenu
        objectName: "terminalNewPaneMenu-" + root.paneId
        AppMenuItem {
            text: qsTr("Default local Shell")
            iconName: "terminal"
            onTriggered: root.createPane("", "", false)
        }
        Instantiator {
            model: root.controller.availableLocalShells
            delegate: AppMenuItem {
                required property var modelData
                text: modelData.name
                iconName: "terminal"
                enabled: !!modelData.available
                visible: modelData.id !== "automatic"
                onTriggered: root.createPane("", modelData.id, false)
            }
            onObjectAdded: (index, object) => newPaneMenu.insertItem(index + 1, object)
            onObjectRemoved: (index, object) => newPaneMenu.removeItem(object)
        }
        AppMenuSeparator {}
        Instantiator {
            model: root.controller.hostProfiles
            delegate: AppMenuItem {
                required property var modelData
                text: modelData.name
                iconName: "hosts"
                onTriggered: root.createPane(modelData.id, "", false)
            }
            onObjectAdded: (index, object) => newPaneMenu.addItem(object)
            onObjectRemoved: (index, object) => newPaneMenu.removeItem(object)
        }
    }
}
