pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property var controller: null
    property var node: ({})
    property bool cursorBlink: true
    property bool copyOnSelect: false
    property bool confirmMultilinePaste: true
    property string defaultFontFamily: "Cascadia Mono"
    property int defaultFontSize: 14
    property bool defaultLigatures: true
    property real defaultBackgroundOpacity: 1.0
    property string defaultCursor: "terminal"
    // Dynamic self-loading is required because QML rejects static recursive type instantiation.
    // qmllint disable missing-property
    readonly property var activeViewport: contentLoader.item && contentLoader.item["activeViewport"] ? contentLoader.item["activeViewport"] : null
    // qmllint enable missing-property
    readonly property string statusText: activeViewport ? activeViewport.statusText : ""
    readonly property bool scrollbarVisible: activeViewport ? activeViewport.scrollbarVisible : false
    readonly property real scrollbarPosition: activeViewport ? activeViewport.scrollbarPosition : 1.0
    readonly property real scrollbarPageRatio: activeViewport ? activeViewport.scrollbarPageRatio : 1.0

    signal multilinePasteConfirmationRequested(var viewport, int lineCount)
    signal browseHostsRequested

    function forceActiveFocus() {
        // qmllint disable missing-property
        if (contentLoader.item && contentLoader.item["focusActivePane"]) {
            contentLoader.item["focusActivePane"]();
        }
    // qmllint enable missing-property
    }

    function requestCurrentSize() {
        if (activeViewport) {
            activeViewport.requestCurrentSize();
        }
    }

    function scrollToFraction(position) {
        if (activeViewport) {
            activeViewport.scrollToFraction(position);
        }
    }

    Loader {
        id: contentLoader

        anchors.fill: parent
        sourceComponent: !root.node || !root.node.kind ? null : root.node.kind === "split" ? splitComponent : leafComponent
    }

    Component {
        id: leafComponent

        Rectangle {
            id: leaf

            readonly property var node: root.node
            readonly property var tab: node.tab || ({})
            readonly property var activeViewport: node.active ? viewport : null

            function focusActivePane() {
                if (node.active) {
                    viewport.forceActiveFocus();
                }
            }

            function closePane() {
                if (root.controller.activateTerminalPane(node.id)) {
                    root.controller.closeActiveTerminalPane();
                }
            }

            color: "transparent"
            border.color: node.active ? Theme.accent : Theme.border
            border.width: node.active ? 2 : 1
            radius: Theme.radiusControl
            clip: true

            TerminalView {
                id: viewport

                property var attachedController: null
                property string attachedPaneId: ""

                function attachToController() {
                    const paneId = leaf.node.id || "";
                    const attachmentChanged = attachedController !== root.controller || attachedPaneId !== paneId;
                    if (attachedController && attachedPaneId.length > 0 && attachmentChanged) {
                        attachedController.detachTerminalViewport(attachedPaneId, viewport);
                        attachedController = null;
                        attachedPaneId = "";
                    }
                    if (!root.controller || paneId.length === 0) {
                        return;
                    }
                    root.controller.attachTerminalViewport(paneId, viewport);
                    attachedController = root.controller;
                    attachedPaneId = paneId;
                    if (attachmentChanged && leaf.node.active) {
                        forceActiveFocus();
                    }
                }

                objectName: "terminalViewport-" + leaf.node.id
                anchors.fill: parent
                anchors.margins: leaf.node.active ? 2 : 1
                focus: !!leaf.node.active
                fontFamily: leaf.tab.sessionFontFamily && leaf.tab.sessionFontFamily.length > 0 ? leaf.tab.sessionFontFamily : root.defaultFontFamily
                fontPixelSize: leaf.tab.sessionFontSize > 0 ? leaf.tab.sessionFontSize : root.defaultFontSize
                ligaturesEnabled: leaf.tab.sessionFontSize > 0 ? leaf.tab.sessionLigatures : root.defaultLigatures
                backgroundOpacity: leaf.tab.sessionBackgroundOpacity >= 0 ? leaf.tab.sessionBackgroundOpacity : root.defaultBackgroundOpacity
                cursorPreference: leaf.tab.sessionCursor && leaf.tab.sessionCursor.length > 0 ? leaf.tab.sessionCursor : root.defaultCursor
                foregroundOverride: leaf.tab.sessionForeground || ""
                backgroundOverride: leaf.tab.sessionBackground || ""
                cursorBlink: root.cursorBlink
                copyOnSelect: root.copyOnSelect
                confirmMultilinePaste: root.confirmMultilinePaste

                Component.onCompleted: Qt.callLater(attachToController)
                Component.onDestruction: {
                    if (attachedController && attachedPaneId.length > 0) {
                        attachedController.detachTerminalViewport(attachedPaneId, viewport);
                    }
                }
                onActiveFocusChanged: {
                    if (activeFocus) {
                        root.controller.activateTerminalPane(leaf.node.id);
                    }
                }
                onMultilinePasteConfirmationRequested: lineCount => root.multilinePasteConfirmationRequested(viewport, lineCount)

                Connections {
                    target: root

                    function onControllerChanged() {
                        Qt.callLater(viewport.attachToController);
                    }

                    function onNodeChanged() {
                        Qt.callLater(viewport.attachToController);
                    }
                }
            }

            RowLayout {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 8
                spacing: 2
                visible: !!leaf.node.active && root.controller
                z: 10

                ToolButton {
                    id: horizontalSplitButton

                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    hoverEnabled: true
                    contentItem: AppIcon {
                        name: "split-horizontal"
                        color: Theme.text
                    }
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: horizontalSplitButton.down ? Theme.controlPressed : horizontalSplitButton.hovered ? Theme.controlHover : Theme.floatingBackground
                    }
                    onClicked: root.controller.splitActiveTerminal("horizontal", false)
                    Accessible.name: qsTr("Split pane horizontally")
                    AppToolTip {
                        text: qsTr("Split horizontally")
                    }
                }

                ToolButton {
                    id: verticalSplitButton

                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    hoverEnabled: true
                    contentItem: AppIcon {
                        name: "split-vertical"
                        color: Theme.text
                    }
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: verticalSplitButton.down ? Theme.controlPressed : verticalSplitButton.hovered ? Theme.controlHover : Theme.floatingBackground
                    }
                    onClicked: root.controller.splitActiveTerminal("vertical", false)
                    Accessible.name: qsTr("Split pane vertically")
                    AppToolTip {
                        text: qsTr("Split vertically")
                    }
                }

                ToolButton {
                    id: duplicatePaneButton

                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    hoverEnabled: true
                    contentItem: AppIcon {
                        name: "copy"
                        color: Theme.text
                    }
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: duplicatePaneButton.down ? Theme.controlPressed : duplicatePaneButton.hovered ? Theme.controlHover : Theme.floatingBackground
                    }
                    onClicked: root.controller.splitActiveTerminal("horizontal", true)
                    Accessible.name: qsTr("Duplicate active pane")
                    AppToolTip {
                        text: qsTr("Duplicate pane")
                    }
                }

                ToolButton {
                    id: closePaneButton

                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    hoverEnabled: true
                    contentItem: AppIcon {
                        name: "close"
                        color: Theme.text
                    }
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: closePaneButton.down ? Theme.controlPressed : closePaneButton.hovered ? Theme.controlHover : Theme.floatingBackground
                    }
                    onClicked: root.controller.closeActiveTerminalPane()
                    visible: root.controller && root.controller.activeTerminalWorkspace.paneCount > 1
                    Accessible.name: qsTr("Close active pane")
                    AppToolTip {
                        text: qsTr("Close pane")
                    }
                }
            }

            StatePanel {
                anchors.centerIn: parent
                width: Math.max(180, Math.min(440, parent.width - 24))
                visible: leaf.tab.kind === "ssh" && leaf.tab.connecting
                z: 9
                kind: "loading"
                heading: qsTr("Connecting to SSH host")
                description: leaf.tab.status || ""
                detail: qsTr("Connection setup runs outside the interface thread. You can close this pane to cancel.")

                ActionButton {
                    text: qsTr("Cancel connection")
                    accessibleName: qsTr("Cancel SSH connection and close pane")
                    onClicked: leaf.closePane()
                }
            }

            StatePanel {
                anchors.centerIn: parent
                width: Math.max(180, Math.min(440, parent.width - 24))
                visible: leaf.tab.kind === "ssh" && leaf.tab.reconnecting
                z: 9
                kind: "loading"
                heading: qsTr("Reconnecting to SSH host")
                description: leaf.tab.status || ""
                detail: qsTr("Automatic retries use bounded exponential backoff and never retain credentials in the terminal pane.")

                ActionButton {
                    text: qsTr("Cancel reconnect")
                    accessibleName: qsTr("Cancel automatic SSH reconnect")
                    onClicked: root.controller.cancelTerminalReconnect(leaf.tab.sessionId)
                }
            }

            StatePanel {
                anchors.centerIn: parent
                width: Math.max(180, Math.min(440, parent.width - 24))
                visible: leaf.tab.kind === "ssh" && leaf.tab.canReconnect && !leaf.tab.connecting && !leaf.tab.reconnecting && !leaf.tab.remoteClosed && !leaf.tab.failed
                z: 9
                kind: "disconnected"
                heading: qsTr("SSH session is disconnected")
                description: qsTr("Reconnect to continue using this restored terminal tab.")
                detail: qsTr("The tab layout was restored, but SSH connections are not kept alive after ztermy exits.")

                ActionButton {
                    text: qsTr("Reconnect")
                    accessibleName: qsTr("Reconnect restored SSH terminal pane")
                    variant: "primary"
                    onClicked: root.controller.reconnectTerminalTab(leaf.tab.sessionId)
                }

                ActionButton {
                    text: qsTr("Close pane")
                    accessibleName: qsTr("Close disconnected SSH terminal pane")
                    onClicked: leaf.closePane()
                }
            }

            StatePanel {
                anchors.centerIn: parent
                width: Math.max(180, Math.min(440, parent.width - 24))
                visible: leaf.tab.kind === "ssh" && leaf.tab.remoteClosed && !leaf.tab.reconnecting
                z: 9
                kind: "disconnected"
                heading: qsTr("SSH session ended")
                description: leaf.tab.status || ""
                detail: qsTr("The remote host closed the terminal connection. Reconnect is available for saved host profiles.")

                ActionButton {
                    visible: !!leaf.tab.canReconnect
                    text: qsTr("Reconnect")
                    accessibleName: qsTr("Reconnect saved SSH terminal pane")
                    variant: "primary"
                    onClicked: root.controller.reconnectTerminalTab(leaf.tab.sessionId)
                }

                ActionButton {
                    text: qsTr("Close pane")
                    accessibleName: qsTr("Close ended SSH terminal pane")
                    onClicked: leaf.closePane()
                }

                ActionButton {
                    text: qsTr("Review host")
                    accessibleName: qsTr("Return to SSH host profiles")
                    onClicked: root.browseHostsRequested()
                }
            }

            StatePanel {
                anchors.centerIn: parent
                width: Math.max(180, Math.min(440, parent.width - 24))
                visible: leaf.tab.kind === "ssh" && leaf.tab.failed && !leaf.tab.reconnecting
                z: 9
                kind: "error"
                heading: qsTr("SSH session unavailable")
                description: leaf.tab.status || ""
                detail: qsTr("Review the saved host and authentication settings, or retry the connection.")

                ActionButton {
                    visible: !!leaf.tab.canReconnect
                    text: qsTr("Reconnect")
                    accessibleName: qsTr("Reconnect saved SSH terminal pane")
                    variant: "primary"
                    onClicked: root.controller.reconnectTerminalTab(leaf.tab.sessionId)
                }

                ActionButton {
                    text: qsTr("Close pane")
                    accessibleName: qsTr("Close failed SSH terminal pane")
                    onClicked: leaf.closePane()
                }

                ActionButton {
                    text: qsTr("Review host")
                    accessibleName: qsTr("Return to SSH host profiles")
                    onClicked: root.browseHostsRequested()
                }
            }
        }
    }

    Component {
        id: splitComponent

        SplitView {
            id: split

            readonly property var node: root.node
            // qmllint disable missing-property
            readonly property var activeViewport: firstLoader.item && firstLoader.item["activeViewport"] ? firstLoader.item["activeViewport"] : secondLoader.item && secondLoader.item["activeViewport"] ? secondLoader.item["activeViewport"] : null
            // qmllint enable missing-property

            function focusActivePane() {
                firstNode.forceActiveFocus();
                secondNode.forceActiveFocus();
            }
            orientation: node.orientation === "horizontal" ? Qt.Horizontal : Qt.Vertical
            handle: Rectangle {
                implicitWidth: split.orientation === Qt.Horizontal ? 6 : split.width
                implicitHeight: split.orientation === Qt.Vertical ? 6 : split.height
                color: SplitHandle.hovered || SplitHandle.pressed ? Theme.accent : Theme.border

                Behavior on color {
                    ColorAnimation {
                        duration: Theme.motionFast
                    }
                }
            }

            Item {
                id: firstNode

                function forceActiveFocus() {
                    if (firstLoader.item) {
                        // qmllint disable missing-property
                        firstLoader.item["forceActiveFocus"]();
                        // qmllint enable missing-property
                    }
                }
                SplitView.minimumWidth: 240
                SplitView.minimumHeight: 160
                SplitView.preferredWidth: split.orientation === Qt.Horizontal ? split.width * split.node.ratio : -1
                SplitView.preferredHeight: split.orientation === Qt.Vertical ? split.height * split.node.ratio : -1

                Loader {
                    id: firstLoader

                    anchors.fill: parent
                    source: Qt.resolvedUrl("TerminalSplitNode.qml")
                    onLoaded: {
                        item.controller = root.controller;
                        item.node = split.node.first;
                        item.cursorBlink = root.cursorBlink;
                        item.copyOnSelect = root.copyOnSelect;
                        item.confirmMultilinePaste = root.confirmMultilinePaste;
                        item.defaultFontFamily = root.defaultFontFamily;
                        item.defaultFontSize = root.defaultFontSize;
                        item.defaultLigatures = root.defaultLigatures;
                        item.defaultBackgroundOpacity = root.defaultBackgroundOpacity;
                        item.defaultCursor = root.defaultCursor;
                    }
                }

                Binding {
                    target: firstLoader.item
                    property: "controller"
                    value: root.controller
                    when: firstLoader.item !== null
                }
                Binding {
                    target: firstLoader.item
                    property: "node"
                    value: split.node.first
                    when: firstLoader.item !== null
                }
                Binding {
                    target: firstLoader.item
                    property: "cursorBlink"
                    value: root.cursorBlink
                    when: firstLoader.item !== null
                }
                Binding {
                    target: firstLoader.item
                    property: "copyOnSelect"
                    value: root.copyOnSelect
                    when: firstLoader.item !== null
                }
                Binding {
                    target: firstLoader.item
                    property: "confirmMultilinePaste"
                    value: root.confirmMultilinePaste
                    when: firstLoader.item !== null
                }
                Binding {
                    target: firstLoader.item
                    property: "defaultFontFamily"
                    value: root.defaultFontFamily
                    when: firstLoader.item !== null
                }
                Binding {
                    target: firstLoader.item
                    property: "defaultFontSize"
                    value: root.defaultFontSize
                    when: firstLoader.item !== null
                }
                Binding {
                    target: firstLoader.item
                    property: "defaultLigatures"
                    value: root.defaultLigatures
                    when: firstLoader.item !== null
                }
                Binding {
                    target: firstLoader.item
                    property: "defaultBackgroundOpacity"
                    value: root.defaultBackgroundOpacity
                    when: firstLoader.item !== null
                }
                Binding {
                    target: firstLoader.item
                    property: "defaultCursor"
                    value: root.defaultCursor
                    when: firstLoader.item !== null
                }

                Connections {
                    target: firstLoader.item
                    function onMultilinePasteConfirmationRequested(viewport, lineCount) {
                        root.multilinePasteConfirmationRequested(viewport, lineCount);
                    }
                    function onBrowseHostsRequested() {
                        root.browseHostsRequested();
                    }
                }
            }

            Item {
                id: secondNode

                function forceActiveFocus() {
                    if (secondLoader.item) {
                        // qmllint disable missing-property
                        secondLoader.item["forceActiveFocus"]();
                        // qmllint enable missing-property
                    }
                }
                SplitView.minimumWidth: 240
                SplitView.minimumHeight: 160
                SplitView.fillWidth: split.orientation === Qt.Horizontal
                SplitView.fillHeight: split.orientation === Qt.Vertical

                Loader {
                    id: secondLoader

                    anchors.fill: parent
                    source: Qt.resolvedUrl("TerminalSplitNode.qml")
                    onLoaded: {
                        item.controller = root.controller;
                        item.node = split.node.second;
                        item.cursorBlink = root.cursorBlink;
                        item.copyOnSelect = root.copyOnSelect;
                        item.confirmMultilinePaste = root.confirmMultilinePaste;
                        item.defaultFontFamily = root.defaultFontFamily;
                        item.defaultFontSize = root.defaultFontSize;
                        item.defaultLigatures = root.defaultLigatures;
                        item.defaultBackgroundOpacity = root.defaultBackgroundOpacity;
                        item.defaultCursor = root.defaultCursor;
                    }
                }

                Binding {
                    target: secondLoader.item
                    property: "controller"
                    value: root.controller
                    when: secondLoader.item !== null
                }
                Binding {
                    target: secondLoader.item
                    property: "node"
                    value: split.node.second
                    when: secondLoader.item !== null
                }
                Binding {
                    target: secondLoader.item
                    property: "cursorBlink"
                    value: root.cursorBlink
                    when: secondLoader.item !== null
                }
                Binding {
                    target: secondLoader.item
                    property: "copyOnSelect"
                    value: root.copyOnSelect
                    when: secondLoader.item !== null
                }
                Binding {
                    target: secondLoader.item
                    property: "confirmMultilinePaste"
                    value: root.confirmMultilinePaste
                    when: secondLoader.item !== null
                }
                Binding {
                    target: secondLoader.item
                    property: "defaultFontFamily"
                    value: root.defaultFontFamily
                    when: secondLoader.item !== null
                }
                Binding {
                    target: secondLoader.item
                    property: "defaultFontSize"
                    value: root.defaultFontSize
                    when: secondLoader.item !== null
                }
                Binding {
                    target: secondLoader.item
                    property: "defaultLigatures"
                    value: root.defaultLigatures
                    when: secondLoader.item !== null
                }
                Binding {
                    target: secondLoader.item
                    property: "defaultBackgroundOpacity"
                    value: root.defaultBackgroundOpacity
                    when: secondLoader.item !== null
                }
                Binding {
                    target: secondLoader.item
                    property: "defaultCursor"
                    value: root.defaultCursor
                    when: secondLoader.item !== null
                }

                Connections {
                    target: secondLoader.item
                    function onMultilinePasteConfirmationRequested(viewport, lineCount) {
                        root.multilinePasteConfirmationRequested(viewport, lineCount);
                    }
                    function onBrowseHostsRequested() {
                        root.browseHostsRequested();
                    }
                }
            }

            onResizingChanged: {
                if (resizing || width <= 0 || height <= 0) {
                    return;
                }
                const ratio = orientation === Qt.Horizontal ? firstNode.width / width : firstNode.height / height;
                root.controller.setTerminalSplitRatio(node.id, ratio);
            }
        }
    }
}
