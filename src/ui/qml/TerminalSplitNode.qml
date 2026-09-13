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
    property bool keepSelectionAfterCopy: false
    property bool selectionActionPopupEnabled: true
    property var selectionActions: []
    property bool confirmMultilinePaste: true
    property string rightClickBehavior: "context-menu"
    property string middleClickBehavior: "disabled"
    property string wordDelimiters: " \t'\"│`|;,()[]{}<>$"
    property int scrollRowsPerWheel: 3
    property string defaultFontFamily: "Cascadia Mono"
    property int defaultFontSize: 14
    property bool defaultLigatures: true
    property real defaultBackgroundOpacity: 1.0
    property string defaultCursor: "terminal"
    property string zoomedPaneId: ""
    property bool detachedPane: false
    property bool managedPaneDrag: false
    property int paneCount: 1
    property bool headersVisible: false
    // Dynamic self-loading is required because QML rejects static recursive type instantiation.
    // qmllint disable missing-property
    readonly property var activeViewport: contentLoader.item && contentLoader.item["activeViewport"] ? contentLoader.item["activeViewport"] : null
    // qmllint enable missing-property
    readonly property string statusText: activeViewport ? activeViewport.statusText : ""

    signal multilinePasteConfirmationRequested(var viewport, int lineCount)
    signal browseHostsRequested
    signal terminalSearchRequested
    signal zoomPaneRequested(string paneId)
    signal detachPaneRequested(string paneId)
    signal toggleHeadersRequested

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

    function copySelection() {
        if (activeViewport) {
            activeViewport.copySelection();
        }
    }

    function pasteClipboard() {
        if (activeViewport) {
            activeViewport.pasteClipboard();
        }
    }

    function selectVisibleTerminal() {
        if (activeViewport) {
            activeViewport.selectVisibleTerminal();
        }
    }

    function selectAllTerminal() {
        if (activeViewport) {
            activeViewport.selectAllTerminal();
        }
    }

    function startQuickSelect() {
        if (activeViewport)
            activeViewport.startQuickSelect();
    }

    function scrollLines(rows) {
        if (activeViewport) {
            activeViewport.scrollLines(rows);
        }
    }

    function scrollPage(pages) {
        if (activeViewport) {
            activeViewport.scrollPage(pages);
        }
    }

    function actionShortcut(actionId) {
        if (!controller) {
            return "";
        }
        for (let index = 0; index < controller.actions.length; ++index) {
            if (controller.actions[index].id === actionId) {
                return controller.actions[index].shortcut;
            }
        }
        return "";
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
            readonly property bool aiConfigured: !!root.controller && root.controller.aiModel.trim().length > 0 && (root.controller.aiProviderPreference === "openai-chatgpt" ? root.controller.aiChatGptConfigured : root.controller.aiBaseUrl.trim().length > 0 && (root.controller.aiProviderPreference === "ollama" || root.controller.aiApiKeyConfigured))
            readonly property bool connectionProgressRequested: tab.kind === "ssh" && (!!tab.connecting || !!tab.reconnecting)
            readonly property bool paneHeaderVisible: root.headersVisible
            property bool connectionProgressVisible: false
            property bool connectionProgressWasReconnect: false
            property int connectionProgressLastStep: 0
            property string connectionProgressLastStatus: ""
            property real connectionProgressValue: 0.0
            property real connectionProgressOpacity: 1.0
            property bool connectionProgressPresented: false
            property bool connectionProgressFinishPending: false

            onConnectionProgressRequestedChanged: {
                if (connectionProgressRequested)
                    beginConnectionProgress();
                else
                    finishConnectionProgress();
            }
            onTabChanged: {
                if (connectionProgressRequested)
                    beginConnectionProgress();
            }

            function beginConnectionProgress() {
                connectionProgressHideTimer.stop();
                connectionProgressWasReconnect = !!tab.reconnecting;
                connectionProgressLastStep = Math.max(0, Number(tab.connectionStageIndex || 0));
                connectionProgressLastStatus = tab.status || "";
                connectionProgressOpacity = 1.0;
                if (!connectionProgressVisible) {
                    connectionProgressPresented = false;
                    connectionProgressFinishPending = false;
                    connectionProgressValue = 0.0;
                    connectionProgressVisible = true;
                    connectionProgressEntryTimer.restart();
                    return;
                }
                connectionProgressValue = progressForStep(connectionProgressLastStep);
                connectionProgressVisible = true;
            }

            function finishConnectionProgress() {
                if (!connectionProgressVisible)
                    return;
                connectionProgressLastStep = tab.connectionPhase === "connected" ? 2 : connectionProgressLastStep;
                connectionProgressLastStatus = tab.status || connectionProgressLastStatus;
                if (!connectionProgressPresented && Theme.animationsEnabled) {
                    connectionProgressFinishPending = true;
                    return;
                }
                completeConnectionProgress();
            }

            function completeConnectionProgress() {
                connectionProgressFinishPending = false;
                if (tab.connectionPhase === "connected" && Theme.animationsEnabled) {
                    connectionProgressValue = 1.0;
                    connectionProgressOpacity = 0.0;
                    connectionProgressHideTimer.restart();
                } else {
                    connectionProgressVisible = false;
                }
            }

            function progressForStep(step) {
                if (step <= 0)
                    return 0.18;
                if (step === 1)
                    return 0.62;
                return 0.84;
            }

            Timer {
                id: connectionProgressEntryTimer

                interval: 16
                repeat: false
                onTriggered: {
                    leaf.connectionProgressValue = leaf.progressForStep(leaf.connectionProgressLastStep);
                    connectionProgressPresentationTimer.restart();
                }
            }

            Timer {
                id: connectionProgressPresentationTimer

                // Let Qt Quick submit at least one fully opaque frame before a
                // fast SSH connection starts the exit animation. This does not
                // delay the connection or terminal input path.
                interval: 32
                repeat: false
                onTriggered: {
                    leaf.connectionProgressPresented = true;
                    if (leaf.connectionProgressFinishPending)
                        leaf.completeConnectionProgress();
                }
            }

            Timer {
                id: connectionProgressHideTimer

                interval: 160
                repeat: false
                onTriggered: {
                    leaf.connectionProgressVisible = false;
                    leaf.connectionProgressOpacity = 1.0;
                    leaf.connectionProgressPresented = false;
                    leaf.connectionProgressFinishPending = false;
                }
            }

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

            function restoreTerminalFocusAfterSelectionAction() {
                if (!viewport.multilinePastePending) {
                    Qt.callLater(viewport.forceActiveFocus);
                }
            }

            function dismissSelectionActionAndRestoreFocus() {
                viewport.dismissSelectionAction();
                restoreTerminalFocusAfterSelectionAction();
            }

            function openSelectionMoreMenu() {
                root.controller.activateTerminalPane(node.id);
                const menuWidth = Math.max(selectionMoreMenu.width, selectionMoreMenu.implicitWidth);
                const menuHeight = Math.max(selectionMoreMenu.height, selectionMoreMenu.implicitHeight);
                selectionMoreMenu.x = Math.max(8, Math.min(leaf.width - menuWidth - 8, selectionActionStrip.x + selectionActionStrip.width - menuWidth));
                const belowY = selectionActionStrip.y + selectionActionStrip.height + 4;
                const aboveY = selectionActionStrip.y - menuHeight - 4;
                selectionMoreMenu.y = belowY + menuHeight <= leaf.height - 8 ? belowY : Math.max(8, aboveY);
                selectionMoreMenu.open();
            }

            function selectionActionAvailable(action) {
                if (!action)
                    return false;
                if (action.id === "ai")
                    return leaf.aiConfigured;
                if (action.id === "highlight")
                    return leaf.tab.kind === "ssh";
                if (action.id === "unhighlight")
                    return leaf.tab.kind === "ssh" && viewport.selectionMatchesKeywordHighlight;
                return true;
            }

            function selectionActionsFor(primary) {
                const result = [];
                for (let index = 0; index < root.selectionActions.length; ++index) {
                    const action = root.selectionActions[index];
                    if (!!action.primary === primary)
                        result.push(action);
                }
                return result;
            }

            function selectionActionObjectName(id) {
                if (id === "copy")
                    return "terminalSelectionCopyAction";
                if (id === "ai")
                    return "terminalSelectionAiAction";
                if (id === "search")
                    return "terminalSelectionSearchAction";
                if (id === "highlight")
                    return "terminalSelectionHighlightAction";
                return "terminalSelectionRemoveHighlightAction";
            }

            function selectionActionAccessibleName(id, label) {
                if (id === "copy")
                    return qsTr("Copy terminal selection");
                if (id === "ai")
                    return qsTr("Attach terminal selection to AI");
                return label;
            }

            function finishSelectionAction(retainSelection, restoreFocus) {
                if (!retainSelection)
                    viewport.clearSelection();
                else
                    viewport.dismissSelectionAction();
                if (restoreFocus)
                    restoreTerminalFocusAfterSelectionAction();
            }

            function triggerSelectionAction(action) {
                if (!action || !root.controller.activateTerminalPane(node.id))
                    return;
                let succeeded = true;
                let restoreFocus = true;
                if (action.id === "copy") {
                    viewport.copySelectionWithPolicy(!!action.retainSelection);
                    viewport.dismissSelectionAction();
                    restoreTerminalFocusAfterSelectionAction();
                    return;
                } else if (action.id === "ai") {
                    succeeded = root.controller.attachAiSelection();
                    if (succeeded && (!leaf.tab.workbenchOpen || leaf.tab.workbenchPage !== "ai"))
                        root.controller.toggleTerminalWorkbench("ai");
                } else if (action.id === "search") {
                    succeeded = root.controller.searchTerminalSelection();
                    if (succeeded) {
                        selectionMoreMenu.restoreViewportOnClose = false;
                        root.terminalSearchRequested();
                        restoreFocus = false;
                    }
                } else if (action.id === "highlight") {
                    succeeded = root.controller.highlightTerminalSelection();
                } else if (action.id === "unhighlight") {
                    succeeded = root.controller.unhighlightTerminalSelection();
                } else {
                    succeeded = false;
                }
                if (succeeded)
                    finishSelectionAction(!!action.retainSelection, restoreFocus);
            }

            color: "transparent"
            border.color: node.active ? Theme.accent : Theme.border
            border.width: node.active ? 2 : 1
            radius: Theme.radiusControl
            clip: true

            Component.onCompleted: {
                if (connectionProgressRequested)
                    beginConnectionProgress();
            }

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
                anchors.topMargin: leaf.paneHeaderVisible ? 32 : (leaf.node.active ? 2 : 1)
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
                keepSelectionAfterCopy: root.keepSelectionAfterCopy
                confirmMultilinePaste: root.confirmMultilinePaste
                rightClickBehavior: root.rightClickBehavior
                middleClickBehavior: root.middleClickBehavior
                wordDelimiters: root.wordDelimiters
                scrollRowsPerWheel: root.scrollRowsPerWheel
                searchQuery: leaf.tab.searchQuery || ""
                searchCaseSensitive: !!leaf.tab.searchCaseSensitive
                searchMatchBackground: Theme.searchMatchBackground
                searchCurrentBackground: Theme.searchCurrentBackground
                searchCurrentForeground: Theme.searchCurrentForeground

                Component.onCompleted: Qt.callLater(attachToController)
                Component.onDestruction: {
                    if (attachedController && attachedPaneId.length > 0) {
                        attachedController.detachTerminalViewport(attachedPaneId, viewport);
                    }
                }
                onActiveFocusChanged: {
                    if (activeFocus && root.Window.window && root.Window.window.active) {
                        root.controller.activateTerminalPane(leaf.node.id);
                    }
                }
                onMultilinePasteConfirmationRequested: lineCount => root.multilinePasteConfirmationRequested(viewport, lineCount)
                onContextMenuRequested: (menuX, menuY) => {
                    terminalContextMenu.x = viewport.x + Math.max(0, Math.min(menuX, viewport.width - terminalContextMenu.width));
                    terminalContextMenu.y = viewport.y + Math.max(0, Math.min(menuY, viewport.height - terminalContextMenu.height));
                    terminalContextMenu.open();
                }
                onLinkActivated: uri => root.controller.openTerminalLink(uri)

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

            Item {
                id: paneScrollbar
                objectName: "terminalPaneScrollbar-" + leaf.node.id
                property bool recentlyScrolled: false
                anchors.top: viewport.top
                anchors.right: viewport.right
                anchors.bottom: viewport.bottom
                anchors.topMargin: !leaf.paneHeaderVisible && paneActions.visible ? paneActions.height + 12 : 6
                anchors.rightMargin: 6
                anchors.bottomMargin: 6
                width: 12
                visible: viewport.scrollbarVisible
                enabled: visible
                opacity: paneScrollbarMouse.containsMouse || paneScrollbarMouse.pressed || recentlyScrolled ? 1 : 0
                z: 8
                Behavior on opacity {
                    NumberAnimation {
                        duration: Theme.motionFast
                    }
                }

                Timer {
                    id: scrollbarIdle
                    interval: 900
                    onTriggered: paneScrollbar.recentlyScrolled = false
                }

                Connections {
                    target: viewport
                    function onScrollbarChanged() {
                        paneScrollbar.recentlyScrolled = true;
                        scrollbarIdle.restart();
                    }
                }

                Rectangle {
                    id: paneScrollbarThumb
                    objectName: "terminalPaneScrollbarThumb-" + leaf.node.id
                    readonly property real travel: Math.max(0, paneScrollbar.height - height)
                    readonly property real normalHeight: Math.min(paneScrollbar.height, Math.max(28, paneScrollbar.height * viewport.scrollbarPageRatio))
                    x: (paneScrollbar.width - width) / 2
                    y: paneScrollbarMouse.pressed ? Math.max(0, Math.min(travel, paneScrollbarMouse.lastPointerY - paneScrollbarMouse.grabOffset)) : travel * viewport.scrollbarPosition
                    width: paneScrollbarMouse.containsMouse || paneScrollbarMouse.pressed ? 6 : 4
                    height: paneScrollbarMouse.pressed ? paneScrollbarMouse.grabbedHeight : normalHeight
                    radius: width / 2
                    color: paneScrollbarMouse.pressed ? Theme.text : paneScrollbarMouse.containsMouse ? Theme.textSoft : Theme.textMuted
                }

                MouseArea {
                    id: paneScrollbarMouse
                    property real grabOffset: paneScrollbarThumb.height / 2
                    property real grabbedHeight: 28
                    property real lastPointerY: 0
                    property real requestedFraction: 0
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor

                    function applyPointer(pointerY) {
                        lastPointerY = pointerY;
                        if (paneScrollbarThumb.travel <= 0)
                            return;
                        const thumbTop = Math.max(0, Math.min(paneScrollbarThumb.travel, pointerY - grabOffset));
                        const fraction = thumbTop / paneScrollbarThumb.travel;
                        if (Math.abs(fraction - requestedFraction) > 0.001) {
                            viewport.scrollFractionDelta(requestedFraction, fraction);
                            requestedFraction = fraction;
                        }
                    }
                    onPressed: mouse => {
                        grabbedHeight = paneScrollbarThumb.normalHeight;
                        requestedFraction = viewport.scrollbarPosition;
                        lastPointerY = mouse.y;
                        const currentThumbY = paneScrollbarThumb.travel * viewport.scrollbarPosition;
                        if (mouse.y >= currentThumbY && mouse.y <= currentThumbY + grabbedHeight) {
                            grabOffset = mouse.y - currentThumbY;
                        } else {
                            grabOffset = paneScrollbarThumb.height / 2;
                            applyPointer(mouse.y);
                        }
                    }
                    onPositionChanged: mouse => {
                        if (pressed)
                            applyPointer(mouse.y);
                    }
                }
            }

            Rectangle {
                id: paneHeader
                objectName: "terminalPaneHeader-" + paneId

                property string paneId: leaf.node.id || ""
                property bool dropCompleted: false
                readonly property string paneTitle: leaf.tab.title || leaf.tab.identity || qsTr("Terminal pane")
                readonly property real dragAreaWidth: width - (paneActions.visible ? paneActions.implicitWidth + 12 : 0)

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: leaf.paneHeaderVisible ? 32 : 0
                visible: height > 0
                color: leaf.node.active ? Theme.controlBackground : Theme.chromeBackground
                border.color: paneDetachDrag.active ? Theme.accent : Theme.border
                z: 12
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: {
                        if (root.controller.activateTerminalPane(leaf.node.id))
                            viewport.forceActiveFocus();
                    }
                }
                Item {
                    id: paneDragProxy
                    parent: Overlay.overlay
                    readonly property point pointerPosition: paneHeader.mapToItem(parent, paneDetachDrag.centroid.position.x, paneDetachDrag.centroid.position.y)
                    x: pointerPosition.x
                    y: pointerPosition.y
                    width: 1
                    height: 1
                    Drag.source: paneHeader
                    Drag.keys: ["ztermy-terminal-pane"]
                    Rectangle {
                        x: 16
                        y: 18
                        width: 220
                        height: 32
                        radius: 5
                        color: Theme.elevatedBackground
                        border.color: Theme.accent
                        visible: paneDetachDrag.active && !paneHeader.dropCompleted && !root.detachedPane
                        Text {
                            anchors.fill: parent
                            anchors.margins: 8
                            text: leaf.tab.title || qsTr("Terminal pane")
                            elide: Text.ElideRight
                            color: Theme.text
                            font.family: Theme.uiFont
                        }
                    }
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.right: parent.right
                    anchors.rightMargin: paneActions.visible ? paneActions.implicitWidth + 12 : 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: leaf.tab.title || leaf.tab.identity || qsTr("Terminal pane")
                    color: leaf.node.active ? Theme.text : Theme.textMuted
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textLabel
                    font.weight: leaf.node.active ? Font.DemiBold : Font.Normal
                }

                DragHandler {
                    id: paneDetachDrag

                    target: null
                    acceptedButtons: Qt.LeftButton
                    dragThreshold: 10
                    enabled: !root.managedPaneDrag
                    onActiveChanged: {
                        if (active) {
                            if (paneActions.visible && centroid.pressPosition.x > paneHeader.width - paneActions.implicitWidth - 12) {
                                paneHeader.dropCompleted = true;
                                return;
                            }
                            paneHeader.dropCompleted = false;
                            if (root.detachedPane) {
                                paneHeader.dropCompleted = true;
                                root.Window.window.startSystemMove();
                                return;
                            }
                            paneDragProxy.Drag.active = true;
                        } else {
                            const point = paneDragProxy.mapToItem(null, 0, 0);
                            paneDragProxy.Drag.drop();
                            const window = root.Window.window;
                            if (!paneHeader.dropCompleted && window && (point.x < 0 || point.y < 0 || point.x > window.width || point.y > window.height))
                                root.detachPaneRequested(leaf.node.id);
                        }
                    }
                    onCanceled: paneDragProxy.Drag.cancel()
                }

                Shortcut {
                    sequence: "Escape"
                    enabled: paneDetachDrag.active
                    onActivated: {
                        paneHeader.dropCompleted = true;
                        paneDragProxy.Drag.cancel();
                    }
                }
            }

            DropArea {
                id: paneDropArea

                anchors.fill: parent
                keys: ["ztermy-terminal-pane"]
                enabled: !root.detachedPane && !root.managedPaneDrag
                z: 11
                onEntered: drag => {
                    // qmllint disable missing-property
                    drag.accepted = !!drag.source && drag.source["paneId"] !== leaf.node.id;
                    // qmllint enable missing-property
                }
                onDropped: drop => {
                    // qmllint disable missing-property
                    if (!drop.source || drop.source["dropCompleted"] || drop.source["paneId"] === leaf.node.id)
                        return;
                    const horizontalEdge = drop.x < width * 0.25 || drop.x > width * 0.75;
                    const center = !horizontalEdge && drop.y >= height * 0.25 && drop.y <= height * 0.75;
                    const orientation = center ? "swap" : horizontalEdge ? "horizontal" : "vertical";
                    const placeAfter = horizontalEdge ? drop.x > width / 2 : drop.y > height / 2;
                    const sourceId = drop.source["paneId"];
                    const targetId = leaf.node.id;
                    drop.source["dropCompleted"] = true;
                    drop.acceptProposedAction();
                    Qt.callLater(() => {
                        root.controller.moveTerminalPane(sourceId, targetId, orientation, placeAfter);
                    });
                    // qmllint enable missing-property
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: paneDropArea.drag.x < paneDropArea.width * 0.25 ? 0 : paneDropArea.drag.x > paneDropArea.width * 0.75 ? paneDropArea.width / 2 : 0
                    anchors.rightMargin: paneDropArea.drag.x < paneDropArea.width * 0.25 ? paneDropArea.width / 2 : 0
                    anchors.topMargin: paneDropArea.drag.x >= paneDropArea.width * 0.25 && paneDropArea.drag.x <= paneDropArea.width * 0.75 && paneDropArea.drag.y > paneDropArea.height / 2 ? paneDropArea.height / 2 : 0
                    anchors.bottomMargin: paneDropArea.drag.x >= paneDropArea.width * 0.25 && paneDropArea.drag.x <= paneDropArea.width * 0.75 && paneDropArea.drag.y <= paneDropArea.height / 2 ? paneDropArea.height / 2 : 0
                    visible: paneDropArea.containsDrag
                    color: Theme.controlHover
                    border.color: Theme.accent
                    border.width: 2
                    opacity: 0.72
                }
            }

            Item {
                id: linkHintAnchor

                property bool containsMouse: visible

                x: Math.max(12, Math.min(leaf.width - 12, viewport.x + viewport.hoveredLinkPosition.x))
                y: Math.max(34, viewport.y + viewport.hoveredLinkPosition.y - 4)
                width: 1
                height: 1
                visible: viewport.hoveredLink.length > 0 && !terminalContextMenu.visible
                z: 18

                AppToolTip {
                    hoverTarget: linkHintAnchor
                    text: qsTr("Hold Ctrl and click to open\n%1").arg(viewport.hoveredLink)
                }
            }

            AppMenu {
                id: terminalContextMenu

                property var commandActions: ({})
                property bool restoreViewportOnClose: true

                modal: false
                onAboutToShow: {
                    restoreViewportOnClose = true;
                    root.controller.activateTerminalPane(leaf.node.id);
                    commandActions = root.controller.activeCommandBlockActions();
                }

                AppMenuItem {
                    text: qsTr("Open link")
                    iconName: "external-link"
                    visible: viewport.hoveredLink.length > 0
                    onTriggered: root.controller.openTerminalLink(viewport.hoveredLink)
                }

                AppMenuItem {
                    text: qsTr("Copy link")
                    iconName: "copy"
                    visible: viewport.hoveredLink.length > 0
                    onTriggered: viewport.copyHoveredLink()
                }

                AppMenuSeparator {
                    visible: viewport.hoveredLink.length > 0
                }

                AppMenuItem {
                    text: qsTr("Copy")
                    iconName: "copy"
                    shortcutText: root.actionShortcut("terminal.copy")
                    enabled: viewport.hasSelection
                    onTriggered: viewport.copySelection()
                }

                AppMenuItem {
                    text: qsTr("Paste")
                    iconName: "paste"
                    shortcutText: root.actionShortcut("terminal.paste")
                    onTriggered: viewport.pasteClipboard()
                }

                AppMenuSeparator {
                    visible: terminalContextMenu.commandActions.commandAvailable === true
                }

                AppMenuItem {
                    text: terminalContextMenu.commandActions.commandApproximate === true ? qsTr("Copy last command (approximate)") : qsTr("Copy last command")
                    iconName: "copy"
                    visible: terminalContextMenu.commandActions.commandAvailable === true
                    onTriggered: root.controller.copyLastTerminalCommand()
                }

                AppMenuItem {
                    text: terminalContextMenu.commandActions.outputPartial === true ? qsTr("Last command output is partial") : qsTr("Copy last command output")
                    iconName: "copy"
                    visible: terminalContextMenu.commandActions.commandAvailable === true
                    enabled: terminalContextMenu.commandActions.outputAvailable === true
                    onTriggered: root.controller.copyLastTerminalCommandOutput()
                }

                AppMenuSeparator {}

                AppMenuItem {
                    text: qsTr("Select all")
                    iconName: "select-visible"
                    onTriggered: viewport.selectAllTerminal()
                }

                AppMenuItem {
                    text: qsTr("Attach selection to AI")
                    iconName: "ai"
                    visible: leaf.aiConfigured
                    enabled: viewport.hasSelection && !!root.controller
                    onTriggered: {
                        if (!root.controller.activateTerminalPane(leaf.node.id) || !root.controller.attachAiSelection()) {
                            return;
                        }
                        if (!leaf.tab.workbenchOpen || leaf.tab.workbenchPage !== "ai") {
                            root.controller.toggleTerminalWorkbench("ai");
                        }
                        viewport.dismissSelectionAction();
                    }
                }

                AppMenuItem {
                    text: qsTr("Search selection")
                    iconName: "search"
                    enabled: viewport.hasSelection
                    onTriggered: {
                        if (root.controller.activateTerminalPane(leaf.node.id) && root.controller.searchTerminalSelection()) {
                            terminalContextMenu.restoreViewportOnClose = false;
                            root.terminalSearchRequested();
                            viewport.dismissSelectionAction();
                        }
                    }
                }

                AppMenuItem {
                    text: qsTr("Highlight selection")
                    iconName: "highlight"
                    visible: leaf.tab.kind === "ssh"
                    enabled: viewport.hasSelection
                    onTriggered: {
                        if (root.controller.activateTerminalPane(leaf.node.id) && root.controller.highlightTerminalSelection()) {
                            viewport.dismissSelectionAction();
                        }
                    }
                }

                onClosed: {
                    // A multiline-paste dialog becomes the next focus owner. Do not
                    // let the closing context menu steal focus back from that modal.
                    if (terminalContextMenu.restoreViewportOnClose && !viewport.multilinePastePending) {
                        viewport.forceActiveFocus();
                    }
                }
            }

            Rectangle {
                id: selectionActionStrip

                readonly property real aboveY: viewport.y + viewport.selectionActionPosition.y - height - 9
                readonly property real belowY: viewport.y + viewport.selectionActionPosition.y + 9
                property bool idleDimmed: false

                objectName: "terminalSelectionActionStrip"
                visible: root.selectionActionPopupEnabled && !!leaf.node.active && viewport.selectionActionVisible && !viewport.multilinePastePending
                width: Math.min(selectionActionRow.implicitWidth + 8, Math.max(0, leaf.width - 16))
                height: 32
                x: Math.max(8, Math.min(leaf.width - width - 8, viewport.x + viewport.selectionActionPosition.x - (width / 2)))
                y: viewport.selectionActionPreferBelow ? (belowY + height <= leaf.height - 8 ? belowY : Math.max(8, aboveY)) : (aboveY >= 8 ? aboveY : Math.min(leaf.height - height - 8, belowY))
                z: 12
                radius: height / 2
                color: Theme.floatingBackground
                border.color: Theme.border
                border.width: 1
                clip: true
                opacity: selectionHover.hovered || selectionMoreMenu.visible ? 1.0 : idleDimmed ? 0.38 : 1.0
                onVisibleChanged: {
                    if (!visible && selectionMoreMenu.visible) {
                        selectionMoreMenu.close();
                    }
                    idleDimmed = false;
                    if (visible)
                        selectionIdleTimer.restart();
                }

                Behavior on opacity {
                    NumberAnimation {
                        duration: Theme.animationsEnabled ? Theme.motionFast : 0
                    }
                }

                HoverHandler {
                    id: selectionHover
                    onHoveredChanged: {
                        if (hovered) {
                            selectionActionStrip.idleDimmed = false;
                            selectionIdleTimer.stop();
                        } else if (selectionActionStrip.visible) {
                            selectionIdleTimer.restart();
                        }
                    }
                }

                Timer {
                    id: selectionIdleTimer
                    interval: 2200
                    repeat: false
                    onTriggered: selectionActionStrip.idleDimmed = true
                }

                RowLayout {
                    id: selectionActionRow

                    anchors.centerIn: parent
                    spacing: 2

                    Repeater {
                        model: leaf.selectionActionsFor(true)

                        delegate: ToolButton {
                            id: selectionPrimaryButton
                            required property var modelData

                            objectName: leaf.selectionActionObjectName(modelData.id)
                            visible: leaf.selectionActionAvailable(modelData)
                            Layout.preferredWidth: visible ? 28 : 0
                            Layout.preferredHeight: 28
                            hoverEnabled: true
                            focusPolicy: Qt.TabFocus
                            Accessible.role: Accessible.Button
                            Accessible.name: leaf.selectionActionAccessibleName(modelData.id, modelData.label)
                            onClicked: leaf.triggerSelectionAction(modelData)
                            Keys.onEscapePressed: event => {
                                leaf.dismissSelectionActionAndRestoreFocus();
                                event.accepted = true;
                            }

                            contentItem: AppIcon {
                                name: selectionPrimaryButton.modelData.icon
                                color: selectionPrimaryButton.modelData.id === "ai" ? Theme.accent : Theme.text
                            }
                            background: Rectangle {
                                radius: height / 2
                                color: selectionPrimaryButton.down ? Theme.controlPressed : selectionPrimaryButton.hovered ? Theme.controlHover : "transparent"
                                border.color: selectionPrimaryButton.visualFocus ? Theme.focus : "transparent"
                                border.width: selectionPrimaryButton.visualFocus ? 2 : 0
                            }

                            AppToolTip {
                                text: selectionPrimaryButton.modelData.label
                            }
                        }
                    }

                    ToolButton {
                        id: selectionMoreButton

                        objectName: "terminalSelectionMoreAction"
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        hoverEnabled: true
                        focusPolicy: Qt.TabFocus
                        Accessible.name: qsTr("More selection actions")
                        onClicked: leaf.openSelectionMoreMenu()
                        Keys.onEscapePressed: event => {
                            leaf.dismissSelectionActionAndRestoreFocus();
                            event.accepted = true;
                        }

                        contentItem: AppIcon {
                            name: "more"
                            color: Theme.text
                        }
                        background: Rectangle {
                            radius: height / 2
                            color: selectionMoreButton.down ? Theme.controlPressed : selectionMoreButton.hovered || selectionMoreMenu.visible ? Theme.controlHover : "transparent"
                            border.color: selectionMoreButton.visualFocus ? Theme.focus : "transparent"
                            border.width: selectionMoreButton.visualFocus ? 2 : 0
                        }

                        AppToolTip {
                            text: qsTr("More selection actions")
                        }
                    }
                }
            }

            AppMenu {
                id: selectionMoreMenu

                property bool restoreViewportOnClose: true

                objectName: "terminalSelectionMoreMenu"
                modal: false
                onAboutToShow: restoreViewportOnClose = true

                Instantiator {
                    model: leaf.selectionActionsFor(false)
                    delegate: AppMenuItem {
                        required property var modelData
                        objectName: leaf.selectionActionObjectName(modelData.id)
                        text: modelData.label
                        iconName: modelData.icon
                        visible: leaf.selectionActionAvailable(modelData)
                        onTriggered: leaf.triggerSelectionAction(modelData)
                    }
                    onObjectAdded: (index, object) => selectionMoreMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => selectionMoreMenu.removeItem(object)
                }

                AppMenuSeparator {}

                AppMenuItem {
                    objectName: "terminalSelectionHideAction"
                    text: qsTr("Hide for this selection")
                    iconName: "eye-off"
                    onTriggered: leaf.dismissSelectionActionAndRestoreFocus()
                }

                onClosed: {
                    if (restoreViewportOnClose) {
                        leaf.restoreTerminalFocusAfterSelectionAction();
                    }
                }
            }

            TerminalPaneToolbar {
                id: paneActions
                objectName: "terminalPaneActions-" + leaf.node.id
                controller: root.controller
                paneId: leaf.node.id
                paneCount: root.paneCount
                headersVisible: leaf.paneHeaderVisible
                zoomed: root.zoomedPaneId === leaf.node.id
                detached: root.detachedPane
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: leaf.paneHeaderVisible ? 2 : 8
                anchors.rightMargin: leaf.paneHeaderVisible ? 4 : 8
                visible: (!!leaf.node.active || root.detachedPane) && !!root.controller
                z: 13
                onZoomRequested: root.zoomPaneRequested(leaf.node.id)
                onDetachRequested: root.detachedPane ? root.detachPaneRequested("") : root.detachPaneRequested(leaf.node.id)
                onToggleHeadersRequested: root.toggleHeadersRequested()
            }

            StatePanel {
                objectName: "restoreQuarantinePanel"
                anchors.centerIn: parent
                width: Math.max(180, Math.min(440, parent.width - 24))
                visible: !!leaf.tab.restoreQuarantined
                z: 10
                kind: "warning"
                heading: qsTr("Terminal restore quarantined")
                description: leaf.tab.status || qsTr("This terminal did not complete startup during the previous restore attempt.")
                detail: qsTr("Other panes remain available. Retry only this terminal when you are ready.")

                ActionButton {
                    text: qsTr("Retry terminal")
                    accessibleName: qsTr("Retry quarantined terminal pane")
                    variant: "primary"
                    onClicked: root.controller.retryQuarantinedTerminalPane(leaf.node.id)
                }
            }

            StatePanel {
                objectName: "sshConnectionProgressPanel"
                anchors.centerIn: parent
                width: Math.max(180, Math.min(440, parent.width - 24))
                visible: leaf.connectionProgressVisible && !leaf.connectionProgressWasReconnect
                opacity: leaf.connectionProgressOpacity
                z: 9
                kind: "loading"
                heading: qsTr("Connecting to SSH host")
                description: leaf.connectionProgressRequested ? leaf.tab.status || "" : leaf.connectionProgressLastStatus
                detail: leaf.tab.connectionInteractionRequired ? qsTr("Waiting for host key confirmation.") : qsTr("Connection setup runs outside the interface thread. You can close this pane to cancel.")
                steps: [qsTr("Establish connection"), qsTr("Authenticate"), qsTr("Open terminal")]
                activeStep: leaf.connectionProgressRequested ? leaf.tab.connectionStageIndex : leaf.connectionProgressLastStep
                progress: leaf.connectionProgressValue

                Behavior on opacity {
                    NumberAnimation {
                        duration: 150
                        easing.type: Easing.OutCubic
                    }
                }

                ActionButton {
                    text: qsTr("Cancel connection")
                    accessibleName: qsTr("Cancel SSH connection and close pane")
                    onClicked: leaf.closePane()
                }
            }

            StatePanel {
                objectName: "sshReconnectProgressPanel"
                anchors.centerIn: parent
                width: Math.max(180, Math.min(440, parent.width - 24))
                visible: leaf.connectionProgressVisible && leaf.connectionProgressWasReconnect
                opacity: leaf.connectionProgressOpacity
                z: 9
                kind: "loading"
                heading: qsTr("Reconnecting to SSH host")
                description: leaf.connectionProgressRequested ? leaf.tab.status || "" : leaf.connectionProgressLastStatus
                detail: leaf.tab.connectionInteractionRequired ? qsTr("Waiting for host key confirmation.") : qsTr("Automatic retries use bounded exponential backoff and never retain credentials in the terminal pane.")
                steps: [qsTr("Establish connection"), qsTr("Authenticate"), qsTr("Open terminal")]
                activeStep: leaf.connectionProgressRequested ? leaf.tab.connectionStageIndex : leaf.connectionProgressLastStep
                progress: leaf.connectionProgressValue

                Behavior on opacity {
                    NumberAnimation {
                        duration: 150
                        easing.type: Easing.OutCubic
                    }
                }

                ActionButton {
                    text: qsTr("Cancel reconnect")
                    accessibleName: qsTr("Cancel automatic SSH reconnect")
                    onClicked: root.controller.cancelTerminalReconnect(leaf.tab.sessionId)
                }
            }

            TerminalSessionStateOverlay {
                anchors.fill: parent
                z: 9
                controller: root.controller
                tab: leaf.tab
                onCloseRequested: leaf.closePane()
                onBrowseHostsRequested: root.browseHostsRequested()
            }
        }
    }

    Component {
        id: splitComponent

        AppSplitView {
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
            leadingPane: firstNode
            trailingPane: secondNode
            ratio: node.ratio
            onRatioEdited: value => root.controller.setTerminalSplitRatio(node.id, value)

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

                Loader {
                    id: firstLoader

                    anchors.fill: parent
                    source: Qt.resolvedUrl("TerminalSplitNode.qml")
                    onLoaded: {
                        item.controller = root.controller;
                        item.node = split.node.first;
                        item.cursorBlink = root.cursorBlink;
                        item.copyOnSelect = root.copyOnSelect;
                        item.keepSelectionAfterCopy = root.keepSelectionAfterCopy;
                        item.confirmMultilinePaste = root.confirmMultilinePaste;
                        item.rightClickBehavior = root.rightClickBehavior;
                        item.middleClickBehavior = root.middleClickBehavior;
                        item.wordDelimiters = root.wordDelimiters;
                        item.scrollRowsPerWheel = root.scrollRowsPerWheel;
                        item.defaultFontFamily = root.defaultFontFamily;
                        item.defaultFontSize = root.defaultFontSize;
                        item.defaultLigatures = root.defaultLigatures;
                        item.defaultBackgroundOpacity = root.defaultBackgroundOpacity;
                        item.defaultCursor = root.defaultCursor;
                        item.zoomedPaneId = root.zoomedPaneId;
                        item.detachedPane = root.detachedPane;
                        item.managedPaneDrag = Qt.binding(() => root.managedPaneDrag);
                        item.paneCount = Qt.binding(() => root.paneCount);
                        item.headersVisible = Qt.binding(() => root.headersVisible);
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
                    property: "keepSelectionAfterCopy"
                    value: root.keepSelectionAfterCopy
                    when: firstLoader.item !== null
                }
                Binding {
                    target: firstLoader.item
                    property: "middleClickBehavior"
                    value: root.middleClickBehavior
                    when: firstLoader.item !== null
                }
                Binding {
                    target: firstLoader.item
                    property: "wordDelimiters"
                    value: root.wordDelimiters
                    when: firstLoader.item !== null
                }
                Binding {
                    target: firstLoader.item
                    property: "scrollRowsPerWheel"
                    value: root.scrollRowsPerWheel
                    when: firstLoader.item !== null
                }
                Binding {
                    target: firstLoader.item
                    property: "rightClickBehavior"
                    value: root.rightClickBehavior
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
                Binding {
                    target: firstLoader.item
                    property: "zoomedPaneId"
                    value: root.zoomedPaneId
                    when: firstLoader.item !== null
                }
                Binding {
                    target: firstLoader.item
                    property: "detachedPane"
                    value: root.detachedPane
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
                    function onTerminalSearchRequested() {
                        root.terminalSearchRequested();
                    }
                    function onZoomPaneRequested(paneId) {
                        root.zoomPaneRequested(paneId);
                    }
                    function onDetachPaneRequested(paneId) {
                        root.detachPaneRequested(paneId);
                    }
                    function onToggleHeadersRequested() {
                        root.toggleHeadersRequested();
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
                        item.keepSelectionAfterCopy = root.keepSelectionAfterCopy;
                        item.confirmMultilinePaste = root.confirmMultilinePaste;
                        item.rightClickBehavior = root.rightClickBehavior;
                        item.middleClickBehavior = root.middleClickBehavior;
                        item.wordDelimiters = root.wordDelimiters;
                        item.scrollRowsPerWheel = root.scrollRowsPerWheel;
                        item.defaultFontFamily = root.defaultFontFamily;
                        item.defaultFontSize = root.defaultFontSize;
                        item.defaultLigatures = root.defaultLigatures;
                        item.defaultBackgroundOpacity = root.defaultBackgroundOpacity;
                        item.defaultCursor = root.defaultCursor;
                        item.zoomedPaneId = root.zoomedPaneId;
                        item.detachedPane = root.detachedPane;
                        item.managedPaneDrag = Qt.binding(() => root.managedPaneDrag);
                        item.paneCount = Qt.binding(() => root.paneCount);
                        item.headersVisible = Qt.binding(() => root.headersVisible);
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
                    property: "keepSelectionAfterCopy"
                    value: root.keepSelectionAfterCopy
                    when: secondLoader.item !== null
                }
                Binding {
                    target: secondLoader.item
                    property: "middleClickBehavior"
                    value: root.middleClickBehavior
                    when: secondLoader.item !== null
                }
                Binding {
                    target: secondLoader.item
                    property: "wordDelimiters"
                    value: root.wordDelimiters
                    when: secondLoader.item !== null
                }
                Binding {
                    target: secondLoader.item
                    property: "scrollRowsPerWheel"
                    value: root.scrollRowsPerWheel
                    when: secondLoader.item !== null
                }
                Binding {
                    target: secondLoader.item
                    property: "rightClickBehavior"
                    value: root.rightClickBehavior
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
                Binding {
                    target: secondLoader.item
                    property: "zoomedPaneId"
                    value: root.zoomedPaneId
                    when: secondLoader.item !== null
                }
                Binding {
                    target: secondLoader.item
                    property: "detachedPane"
                    value: root.detachedPane
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
                    function onTerminalSearchRequested() {
                        root.terminalSearchRequested();
                    }
                    function onZoomPaneRequested(paneId) {
                        root.zoomPaneRequested(paneId);
                    }
                    function onDetachPaneRequested(paneId) {
                        root.detachPaneRequested(paneId);
                    }
                    function onToggleHeadersRequested() {
                        root.toggleHeadersRequested();
                    }
                }
            }
        }
    }
}
