pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

AppSurface {
    id: bar
    required property var controller
    required property string workspaceId
    required property bool windowActive
    readonly property bool ownsActiveSession: controller.activeTerminalTabId === workspaceId
    property string paneId: ""
    property int matchCurrent: 0
    property int matchTotal: 0
    signal closed

    width: 420
    height: 42
    elevation: 2
    compact: true
    visible: false

    function syncQuery() {
        if (!visible || !ownsActiveSession)
            return;
        const currentPane = controller.activeTerminalWorkspace.activePaneId || "";
        if (paneId !== currentPane) {
            searchDelay.stop();
            paneId = currentPane;
        }
        searchField.text = controller.terminalSearchQuery;
        caseButton.checked = controller.terminalSearchCaseSensitive;
        matchCurrent = controller.terminalSearchCurrent;
        matchTotal = controller.terminalSearchTotal;
    }
    function openSearch() {
        if (!controller.activateTerminalTab(workspaceId))
            return;
        visible = true;
        syncQuery();
        searchField.forceActiveFocus();
        searchField.selectAll();
    }
    function closeSearch() {
        searchDelay.stop();
        visible = false;
        if (ownsActiveSession)
            controller.clearTerminalSearch();
        closed();
    }
    function search(backwards) {
        searchDelay.stop();
        if (windowActive && ownsActiveSession && paneId === controller.activeTerminalWorkspace.activePaneId)
            controller.searchTerminal(searchField.text, backwards, caseButton.checked);
    }
    onWindowActiveChanged: {
        if (!windowActive)
            searchDelay.stop();
    }
    onOwnsActiveSessionChanged: {
        searchDelay.stop();
        syncQuery();
    }
    onWorkspaceIdChanged: {
        searchDelay.stop();
        syncQuery();
    }
    Connections {
        target: bar.controller
        function onTerminalSearchChanged() {
            bar.syncQuery();
        }
        function onTerminalWorkspaceChanged() {
            if (bar.ownsActiveSession && bar.paneId !== (bar.controller.activeTerminalWorkspace.activePaneId || ""))
                bar.syncQuery();
        }
    }
    Timer {
        id: searchDelay
        interval: 250
        onTriggered: bar.search(false)
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 6
        spacing: 4
        AppTextField {
            id: searchField
            objectName: "terminalSearchQuery"
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            compact: true
            placeholderText: qsTranslate("Main", "Find in terminal")
            accessibleName: qsTranslate("Main", "Terminal search query")
            onTextEdited: searchDelay.restart()
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Escape) {
                    bar.closeSearch();
                    event.accepted = true;
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    bar.search((event.modifiers & Qt.ShiftModifier) !== 0);
                    event.accepted = true;
                }
            }
        }
        Text {
            Layout.preferredWidth: 46
            horizontalAlignment: Text.AlignHCenter
            text: bar.matchTotal > 0 ? bar.matchCurrent + "/" + bar.matchTotal : "0/0"
            color: Theme.textMuted
            font.family: Theme.terminalFont
            font.pixelSize: 10
        }
        ActionButton {
            id: caseButton
            Layout.preferredWidth: 30
            Layout.preferredHeight: 30
            checkable: true
            variant: checked ? "primary" : "default"
            leftPadding: 0
            rightPadding: 0
            text: "Aa"
            Accessible.name: qsTranslate("Main", "Match case")
            onClicked: bar.search(false)
        }
        AppIconButton {
            iconName: "chevron-up"
            label: qsTranslate("Main", "Previous match")
            onClicked: bar.search(true)
        }
        AppIconButton {
            iconName: "chevron-down"
            label: qsTranslate("Main", "Next match")
            onClicked: bar.search(false)
        }
        AppIconButton {
            iconName: "close"
            label: qsTranslate("Main", "Close terminal search")
            onClicked: bar.closeSearch()
        }
    }
}
