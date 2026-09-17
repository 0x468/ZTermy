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

    // The model only carries stable identity; labels and visibility are
    // bound inside the delegate so toggling headers, zoom or pane count
    // does not rebuild the array and re-create every button.
    Repeater {
        model: ["headers", "zoom", "detach", "copy", "new", "close"]
        delegate: AppIconButton {
            id: button
            required property string modelData
            objectName: "terminalPaneAction-" + modelData + "-" + root.paneId
            visible: {
                switch (modelData) {
                case "zoom":
                case "close":
                    return root.paneCount > 1 && !root.detached;
                case "copy":
                case "new":
                    return !root.detached;
                default:
                    return true;
                }
            }
            Layout.preferredWidth: 28
            Layout.preferredHeight: root.detached ? 32 : 28
            label: {
                switch (modelData) {
                case "headers":
                    return root.headersVisible ? qsTr("Hide headers") : qsTr("Show headers");
                case "zoom":
                    return root.zoomed ? qsTr("Restore pane layout") : qsTr("Zoom this pane within its tab");
                case "detach":
                    return root.detached ? qsTr("Reattach terminal pane") : qsTr("Detach terminal pane");
                case "copy":
                    return qsTr("Copy pane — new session, same profile or Shell");
                case "new":
                    return qsTr("New pane — choose a host or local Shell");
                default:
                    return qsTr("Close this pane");
                }
            }
            iconName: modelData === "headers" ? "list" : modelData === "zoom" ? "locate" : modelData === "detach" ? "external-link" : modelData === "copy" ? "copy" : modelData === "new" ? "plus" : "close"
            selected: (modelData === "headers" && root.headersVisible) || (modelData === "zoom" && root.zoomed)
            iconColor: selected ? Theme.accent : Theme.text
            toolTipEnabled: !newPaneMenu.visible
            onClicked: {
                switch (modelData) {
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
            objectName: "detachedWindowAction-" + modelData + "-" + root.paneId
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            kind: modelData
            chrome: detachedChrome
            nativeMaximizeHandling: false
            accessibleName: modelData === "minimize" ? qsTranslate("TitleWindowActions", "Minimize") : modelData === "close" ? qsTranslate("TitleWindowActions", "Close") : detachedChrome.maximized ? qsTranslate("TitleWindowActions", "Restore") : qsTranslate("TitleWindowActions", "Maximize")
            onActivated: {
                const window = root.Window.window;
                if (modelData === "minimize")
                    WindowControl.minimize(window);
                else if (modelData === "maximize")
                    WindowControl.toggleMaximize(window);
                else
                    window.close();
            }
        }
    }
    AppMenu {
        id: newPaneMenu
        objectName: "terminalNewPaneMenu-" + root.paneId
        // Host and shell entries are only instantiated once the menu is first
        // opened; every pane used to build the full list on creation.
        property bool populated: false
        onAboutToShow: populated = true
        AppMenuItem {
            text: qsTr("Default local Shell")
            iconName: "terminal"
            onTriggered: root.createPane("", "", false)
        }
        Instantiator {
            active: newPaneMenu.populated
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
            active: newPaneMenu.populated
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
