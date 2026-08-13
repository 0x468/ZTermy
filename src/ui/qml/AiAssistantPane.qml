pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Rectangle {
    id: pane

    objectName: "aiAssistantPane"
    required property var controller
    required property var activeTab
    readonly property bool busy: controller.activeAiState === "starting" || controller.activeAiState === "retrying" || controller.activeAiState === "streaming" || controller.activeAiState === "cancelling"
    readonly property var conversation: controller.activeAiConversation
    readonly property var toolApproval: controller.activeAiToolApproval || ({
            "visible": false,
            "kind": "",
            "command": "",
            "highRisk": false,
            "riskReason": ""
        })
    readonly property string approvalKind: typeof toolApproval.kind === "string" ? toolApproval.kind : ""
    readonly property string approvalCommand: typeof toolApproval.command === "string" ? toolApproval.command : ""
    readonly property string approvalRiskReason: typeof toolApproval.riskReason === "string" ? toolApproval.riskReason : ""
    readonly property bool approvalHighRisk: toolApproval.highRisk === true
    readonly property bool approvalRuleSupported: toolApproval.ruleSupported === true
    readonly property string approvalRuleSubject: typeof toolApproval.ruleSubject === "string" ? toolApproval.ruleSubject : ""
    readonly property string approvalRuleDefaultMatcher: typeof toolApproval.ruleDefaultMatcher === "string" ? toolApproval.ruleDefaultMatcher : "exact"
    readonly property bool approvalProfileAvailable: toolApproval.profileAvailable === true
    property bool contextExpanded: false
    property bool activityExpanded: false
    property bool historyExpanded: false
    property bool commandRequest: false
    readonly property var slashCommands: [
        {
            "command": "/new",
            "title": qsTr("New conversation"),
            "description": qsTr("Start a clean Agent conversation")
        },
        {
            "command": "/history",
            "title": qsTr("Conversation history"),
            "description": qsTr("Open saved AI conversations")
        },
        {
            "command": "/explain",
            "title": qsTr("Explain last failure"),
            "description": qsTr("Explain the most recent failed command")
        },
        {
            "command": "/selection",
            "title": qsTr("Attach selection"),
            "description": qsTr("Attach selected terminal text")
        },
        {
            "command": "/last",
            "title": qsTr("Attach last command"),
            "description": qsTr("Attach the most recent terminal command")
        },
        {
            "command": "/last3",
            "title": qsTr("Attach last 3 commands"),
            "description": qsTr("Attach the three most recent terminal commands")
        },
        {
            "command": "/last5",
            "title": qsTr("Attach last 5 commands"),
            "description": qsTr("Attach the five most recent terminal commands")
        },
        {
            "command": "/command",
            "title": qsTr("Generate command"),
            "description": qsTr("Switch to an explicit shell-command request")
        }
    ]

    component ContextToolButton: ToolButton {
        id: contextButton

        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        implicitWidth: 26
        implicitHeight: 26
        background: Rectangle {
            radius: width / 2
            color: contextButton.down ? Theme.controlPressed : contextButton.hovered ? Theme.controlHover : "transparent"
            border.color: contextButton.activeFocus ? Theme.focus : "transparent"
            border.width: contextButton.activeFocus ? 2 : 0
        }

        HoverHandler {
            cursorShape: contextButton.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        }
    }

    function stateLabel() {
        switch (controller.activeAiState) {
        case "starting":
            return qsTr("Starting");
        case "retrying":
            return qsTr("Retrying");
        case "streaming":
            return qsTr("Responding");
        case "cancelling":
            return qsTr("Cancelling");
        case "error":
            return qsTr("Needs attention");
        case "cancelled":
            return qsTr("Cancelled");
        default:
            return qsTr("Ready");
        }
    }

    function toolStateLabel(state, resultCode) {
        if (state === "queued")
            return qsTr("Queued");
        if (state === "awaiting_approval")
            return qsTr("Waiting for approval");
        if (state === "running")
            return qsTr("Running");
        if (state === "succeeded")
            return qsTr("Completed");
        if (state === "cancelled")
            return qsTr("Cancelled");
        if (state === "failed")
            return resultCode.length > 0 ? qsTr("Failed · %1").arg(resultCode) : qsTr("Failed");
        return state;
    }

    function sendPrompt() {
        const prompt = promptEditor.text.trim();
        if (prompt.length === 0 || busy) {
            return;
        }
        if (prompt.startsWith("/") && executeSlashCommand(prompt)) {
            promptEditor.clear();
            return;
        }
        const accepted = commandRequest ? controller.sendAiCommandRequest(prompt) : controller.sendAiMessage(prompt);
        if (accepted) {
            promptEditor.clear();
        }
    }

    function focusEditor() {
        Qt.callLater(promptEditor.forceActiveFocus);
    }

    function permissionModeIndex(token) {
        return token === "read-only" ? 0 : token === "edit" ? 2 : token === "auto" ? 3 : token === "yolo" ? 4 : 1;
    }

    function modelOptions() {
        const models = [];
        const configured = pane.controller.aiModel || "";
        if (configured.length > 0)
            models.push(configured);
        const available = pane.controller.aiAvailableModels || [];
        for (let index = 0; index < available.length; ++index) {
            if (models.indexOf(available[index]) < 0)
                models.push(available[index]);
        }
        return models;
    }

    function selectModel(model) {
        if (!model || model === pane.controller.aiModel)
            return;
        pane.controller.saveAiProviderSettings(pane.controller.aiProviderPreference, pane.controller.aiBaseUrl, pane.controller.aiEndpointPath, model, pane.controller.aiAutomaticContext, pane.controller.aiPermissionPreference);
    }

    function slashSuggestions() {
        const text = promptEditor ? promptEditor.text.trim().toLocaleLowerCase() : "";
        if (!text.startsWith("/") || text.indexOf(" ") >= 0)
            return [];
        return slashCommands.filter(item => item.command.startsWith(text));
    }

    function applySlashSuggestion(item) {
        if (!item)
            return;
        promptEditor.text = item.command + (item.command === "/command" ? " " : "");
        promptEditor.cursorPosition = promptEditor.text.length;
        promptEditor.forceActiveFocus();
    }

    function activateSlashSuggestion(item) {
        if (!item)
            return;
        if (item.command === "/command") {
            applySlashSuggestion(item);
            return;
        }
        if (executeSlashCommand(item.command))
            promptEditor.clear();
    }

    function executeSlashCommand(prompt) {
        const separator = prompt.indexOf(" ");
        const command = (separator < 0 ? prompt : prompt.slice(0, separator)).toLocaleLowerCase();
        const argument = separator < 0 ? "" : prompt.slice(separator + 1).trim();
        switch (command) {
        case "/new":
            pane.historyExpanded = false;
            pane.activityExpanded = false;
            pane.controller.clearAiConversation();
            return true;
        case "/history":
            pane.historyExpanded = true;
            pane.activityExpanded = false;
            pane.controller.aiConversationHistory.reload();
            return true;
        case "/explain":
            return pane.controller.explainAiLastFailure();
        case "/selection":
            return pane.controller.attachAiSelection();
        case "/last":
            return pane.controller.attachAiRecentCommands(1);
        case "/last3":
            return pane.controller.attachAiRecentCommands(3);
        case "/last5":
            return pane.controller.attachAiRecentCommands(5);
        case "/command":
            pane.commandRequest = true;
            return argument.length === 0 || pane.controller.sendAiCommandRequest(argument);
        default:
            return false;
        }
    }

    function approvalScopeTokens() {
        return approvalProfileAvailable ? ["once", "session", "profile", "global"] : ["once", "session", "global"];
    }

    function approvalScopeLabels() {
        return approvalProfileAvailable ? [qsTr("This time"), qsTr("This session"), qsTr("This Profile"), qsTr("All Profiles")] : [qsTr("This time"), qsTr("This session"), qsTr("All Profiles")];
    }

    function matcherIndex(token) {
        return token === "prefix" ? 1 : token === "glob" ? 2 : token === "regex" ? 3 : token === "all" ? 4 : 0;
    }

    function approvePendingTool() {
        if (!approvalRuleSupported || approvalScopeBox.currentValue === "once") {
            controller.approveAiTool();
            return;
        }
        controller.approveAiToolWithRule(approvalScopeBox.currentValue, approvalMatcherBox.currentValue, approvalPatternField.text);
    }

    function denyPendingTool() {
        if (!approvalRuleSupported || approvalScopeBox.currentValue === "once") {
            controller.denyAiTool();
            return;
        }
        controller.denyAiToolWithRule(approvalScopeBox.currentValue, approvalMatcherBox.currentValue, approvalPatternField.text);
    }

    onToolApprovalChanged: {
        if (!toolApproval.visible)
            return;
        approvalScopeBox.currentIndex = 0;
        approvalMatcherBox.currentIndex = matcherIndex(approvalRuleDefaultMatcher);
        approvalPatternField.text = approvalRuleSubject;
    }

    color: Theme.panelBackground
    clip: true
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Terminal AI assistant")
    onVisibleChanged: {
        if (visible) {
            focusEditor();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: Theme.elevatedBackground
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 8

                AppIcon {
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    name: "ai"
                    color: Theme.accent
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 0

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("ztermy Agent")
                        elide: Text.ElideRight
                        color: Theme.text
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textBody
                        font.weight: Font.DemiBold
                    }

                    Text {
                        Layout.fillWidth: true
                        text: pane.activeTab ? pane.activeTab.title : ""
                        color: Theme.textSubtle
                        elide: Text.ElideRight
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textCompact
                    }
                }

                Rectangle {
                    Layout.preferredHeight: 22
                    Layout.preferredWidth: statusLabel.implicitWidth + 16
                    radius: 11
                    color: pane.controller.activeAiState === "error" ? Theme.dangerSurface : Theme.controlBackground
                    border.color: pane.busy ? Theme.accent : Theme.border

                    Text {
                        id: statusLabel

                        anchors.centerIn: parent
                        text: pane.stateLabel()
                        color: pane.controller.activeAiState === "error" ? Theme.dangerSurfaceText : Theme.textSoft
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textCompact
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.topMargin: 8
            Layout.bottomMargin: 4
            spacing: 6

            Text {
                Layout.fillWidth: true
                text: pane.conversation && pane.conversation.count > 0 ? qsTr("Conversation") : qsTr("New conversation")
                color: Theme.textMuted
                font.family: Theme.uiFont
                font.pixelSize: Theme.textCompact
            }

            ContextToolButton {
                objectName: "aiHistoryToggle"
                enabled: pane.controller.aiConversationHistoryEnabled
                Accessible.name: pane.historyExpanded ? qsTr("Hide AI conversation history") : qsTr("Show AI conversation history")
                onClicked: {
                    pane.historyExpanded = !pane.historyExpanded;
                    if (pane.historyExpanded) {
                        pane.activityExpanded = false;
                        pane.controller.aiConversationHistory.reload();
                    }
                }
                contentItem: AppIcon {
                    name: "history"
                    color: pane.historyExpanded ? Theme.accent : Theme.textMuted
                }
                AppToolTip {
                    text: qsTr("Conversation history")
                }
            }

            ContextToolButton {
                objectName: "aiNewConversationButton"
                enabled: !pane.busy
                Accessible.name: qsTr("Start a new AI conversation")
                onClicked: {
                    pane.historyExpanded = false;
                    pane.activityExpanded = false;
                    pane.controller.clearAiConversation();
                }
                contentItem: AppIcon {
                    name: "plus"
                    color: Theme.textMuted
                }
                AppToolTip {
                    text: qsTr("New conversation")
                }
            }

            ContextToolButton {
                objectName: "aiConversationMoreButton"
                Accessible.name: qsTr("More conversation actions")
                onClicked: conversationMenu.popup()
                contentItem: AppIcon {
                    name: "more"
                    color: Theme.textMuted
                }
                AppToolTip {
                    text: qsTr("More")
                }
            }
        }

        StatusMessage {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            kind: "error"
            text: pane.controller.activeAiError
        }

        ToolButton {
            id: contextToggle

            objectName: "aiContextToggle"
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.preferredHeight: 32
            visible: pane.controller.activeAiContextItems.length > 0
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            text: qsTr("Request context · %n item(s)", "", pane.controller.activeAiContextItems.length)
            Accessible.name: text
            onClicked: pane.contextExpanded = !pane.contextExpanded

            contentItem: RowLayout {
                spacing: 6

                AppIcon {
                    Layout.preferredWidth: 15
                    Layout.preferredHeight: 15
                    name: pane.contextExpanded ? "chevron-down" : "chevron-right"
                    color: Theme.textMuted
                }

                Text {
                    Layout.fillWidth: true
                    text: contextToggle.text
                    color: Theme.textSoft
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textCompact
                    font.weight: Font.Medium
                }
            }
            background: Rectangle {
                radius: Theme.radiusSmall
                color: contextToggle.down ? Theme.controlPressed : contextToggle.hovered ? Theme.controlHover : "transparent"
                border.color: contextToggle.activeFocus ? Theme.focus : "transparent"
                border.width: contextToggle.activeFocus ? 2 : 0
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.preferredHeight: pane.contextExpanded ? Math.min(170, contextColumn.implicitHeight + 16) : 0
            visible: pane.contextExpanded && pane.controller.activeAiContextItems.length > 0
            clip: true
            radius: Theme.radiusControl
            color: Theme.raisedBackground
            border.color: Theme.border

            Behavior on Layout.preferredHeight {
                NumberAnimation {
                    duration: Theme.motionFast
                    easing.type: Easing.OutCubic
                }
            }

            ScrollView {
                id: contextScroll

                anchors.fill: parent
                anchors.margins: 8
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                ColumnLayout {
                    id: contextColumn

                    width: contextScroll.availableWidth
                    spacing: 6

                    Repeater {
                        model: pane.controller.activeAiContextItems

                        delegate: Rectangle {
                            id: contextItem

                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredHeight: 30
                            radius: Theme.radiusSmall
                            color: Theme.controlBackground

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 6

                                Text {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    text: contextItem.modelData.title
                                    color: Theme.textSoft
                                    elide: Text.ElideMiddle
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textCompact
                                }

                                Text {
                                    text: contextItem.modelData.quality
                                    color: contextItem.modelData.quality === "rich" ? Theme.successText : Theme.warning
                                    font.family: Theme.terminalFont
                                    font.pixelSize: Theme.textCompact
                                }

                                Text {
                                    visible: contextItem.modelData.redacted || contextItem.modelData.truncated
                                    text: contextItem.modelData.redacted ? qsTr("redacted") : qsTr("truncated")
                                    color: Theme.warning
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textCompact
                                }

                                ContextToolButton {
                                    id: pinButton

                                    checked: contextItem.modelData.pinned
                                    checkable: true
                                    Accessible.name: checked ? qsTr("Unpin %1").arg(contextItem.modelData.title) : qsTr("Pin %1").arg(contextItem.modelData.title)
                                    onClicked: pane.controller.setAiContextItemPinned(contextItem.modelData.id, checked)
                                    contentItem: AppIcon {
                                        name: "bookmark"
                                        color: pinButton.checked ? Theme.accent : Theme.textMuted
                                    }

                                    AppToolTip {
                                        text: pinButton.checked ? qsTr("Unpin context") : qsTr("Pin context")
                                    }
                                }

                                ContextToolButton {
                                    Accessible.name: qsTr("Remove %1 from context").arg(contextItem.modelData.title)
                                    onClicked: pane.controller.removeAiContextItem(contextItem.modelData.id)
                                    contentItem: AppIcon {
                                        name: "close"
                                        color: Theme.textMuted
                                    }

                                    AppToolTip {
                                        text: qsTr("Remove from this request")
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: pane.controller.activeAiContextPreview.length > 0
                        text: pane.controller.activeAiContextPreview
                        color: Theme.textMuted
                        wrapMode: Text.WrapAnywhere
                        font.family: Theme.terminalFont
                        font.pixelSize: Theme.textCompact
                    }

                    ActionButton {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Reset context")
                        iconName: "refresh"
                        accessibleName: qsTr("Restore automatic context items")
                        onClicked: pane.controller.resetAiContextItems()
                    }
                }
            }
        }

        Rectangle {
            id: approvalCard

            objectName: "aiToolApprovalCard"
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.topMargin: 6
            Layout.preferredHeight: approvalContent.implicitHeight + 20
            visible: pane.toolApproval.visible === true
            radius: Theme.radiusPanel
            color: pane.approvalHighRisk ? Theme.dangerSurface : Theme.raisedBackground
            border.color: pane.approvalHighRisk ? Theme.dangerBorder : Theme.accent
            border.width: 1
            Accessible.role: Accessible.Pane
            Accessible.name: pane.approvalKind.indexOf("queue_sftp_") === 0 ? qsTr("AI SFTP transfer approval") : pane.approvalKind === "interrupt_command" ? qsTr("AI interrupt approval") : pane.approvalKind === "write_to_pty" ? qsTr("AI terminal input approval") : pane.approvalKind === "save_runbook" ? qsTr("AI runbook approval") : qsTr("AI command approval")

            ColumnLayout {
                id: approvalContent

                anchors.fill: parent
                anchors.margins: 10
                spacing: 7

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 7

                    AppIcon {
                        Layout.preferredWidth: 17
                        Layout.preferredHeight: 17
                        name: pane.approvalHighRisk ? "warning" : "terminal"
                        color: pane.approvalHighRisk ? Theme.dangerText : Theme.accent
                    }

                    Text {
                        Layout.fillWidth: true
                        text: pane.approvalHighRisk ? qsTr("High-risk command requires approval") : pane.approvalKind.indexOf("queue_sftp_") === 0 ? qsTr("SFTP transfer requires approval") : pane.approvalKind === "interrupt_command" ? qsTr("Terminal interrupt requires approval") : pane.approvalKind === "write_to_pty" ? qsTr("Terminal input requires approval") : pane.approvalKind === "save_runbook" ? qsTr("Runbook save requires approval") : qsTr("Command requires approval")
                        color: pane.approvalHighRisk ? Theme.dangerText : Theme.text
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textBody
                        font.weight: Font.DemiBold
                    }
                }

                TextEdit {
                    Layout.fillWidth: true
                    Layout.maximumHeight: 120
                    text: pane.approvalCommand
                    color: Theme.text
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    textFormat: TextEdit.PlainText
                    font.family: Theme.terminalFont
                    font.pixelSize: Theme.textBody
                    Accessible.name: pane.approvalKind === "interrupt_command" ? qsTr("Interrupt awaiting approval") : pane.approvalKind === "write_to_pty" ? qsTr("Terminal input awaiting approval") : pane.approvalKind === "save_runbook" ? qsTr("Runbook awaiting approval") : qsTr("Command awaiting approval")

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -6
                        z: -1
                        radius: Theme.radiusSmall
                        color: Theme.controlBackground
                        border.color: Theme.border
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: pane.approvalHighRisk && pane.approvalRiskReason.length > 0
                    text: pane.approvalRiskReason
                    color: Theme.dangerText
                    wrapMode: Text.WordWrap
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textCompact
                }

                GridLayout {
                    Layout.fillWidth: true
                    visible: pane.approvalRuleSupported
                    columns: width < 330 ? 1 : 2
                    columnSpacing: 7
                    rowSpacing: 7

                    AppComboBox {
                        id: approvalScopeBox

                        Layout.fillWidth: true
                        model: pane.approvalScopeTokens()
                        displayTextModel: pane.approvalScopeLabels()
                        accessibleName: qsTr("Permission rule duration")
                    }

                    AppComboBox {
                        id: approvalMatcherBox

                        Layout.fillWidth: true
                        visible: approvalScopeBox.currentValue !== "once"
                        model: ["exact", "prefix", "glob", "regex", "all"]
                        displayTextModel: [qsTr("Exact action"), qsTr("Starts with"), qsTr("Wildcard"), qsTr("Regular expression"), qsTr("Any action of this type")]
                        accessibleName: qsTr("Permission rule matcher")
                    }

                    AppTextField {
                        id: approvalPatternField

                        Layout.fillWidth: true
                        Layout.columnSpan: parent.columns
                        visible: approvalScopeBox.currentValue !== "once" && approvalMatcherBox.currentValue !== "all"
                        compact: true
                        placeholderText: qsTr("Command or action pattern")
                        accessibleName: placeholderText
                    }
                }

                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: 7

                    ActionButton {
                        text: pane.approvalRuleSupported && approvalScopeBox.currentValue !== "once" ? qsTr("Deny & remember") : qsTr("Deny")
                        iconName: "close"
                        accessibleName: qsTr("Deny the pending AI terminal action")
                        onClicked: pane.denyPendingTool()
                    }

                    ActionButton {
                        text: pane.approvalRuleSupported && approvalScopeBox.currentValue !== "once" ? qsTr("Allow & remember") : pane.approvalKind.indexOf("queue_sftp_") === 0 ? qsTr("Queue transfer") : pane.approvalKind === "interrupt_command" ? qsTr("Send Ctrl+C") : pane.approvalKind === "write_to_pty" ? qsTr("Send input") : pane.approvalKind === "save_runbook" ? qsTr("Save runbook") : qsTr("Run command")
                        iconName: pane.approvalKind.indexOf("queue_sftp_") === 0 ? "transfer" : pane.approvalKind === "interrupt_command" ? "close" : pane.approvalKind === "write_to_pty" ? "composer" : pane.approvalKind === "save_runbook" ? "save" : "play"
                        variant: pane.approvalHighRisk ? "destructive" : "primary"
                        accessibleName: pane.approvalKind.indexOf("queue_sftp_") === 0 ? qsTr("Approve and queue the pending AI SFTP transfer") : pane.approvalKind === "interrupt_command" ? qsTr("Approve the pending soft interrupt") : pane.approvalKind === "write_to_pty" ? qsTr("Approve the pending terminal input") : pane.approvalKind === "save_runbook" ? qsTr("Approve and save the pending AI runbook") : qsTr("Approve and run the pending AI command")
                        onClicked: pane.approvePendingTool()
                    }
                }
            }
        }

        Rectangle {
            id: historyPanel

            objectName: "aiHistoryPanel"
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.topMargin: pane.historyExpanded ? 6 : 0
            Layout.preferredHeight: pane.historyExpanded ? 212 : 0
            visible: pane.historyExpanded
            clip: true
            radius: Theme.radiusPanel
            color: Theme.raisedBackground
            border.color: Theme.border
            Accessible.role: Accessible.Pane
            Accessible.name: qsTr("Encrypted AI conversation history")

            Behavior on Layout.preferredHeight {
                NumberAnimation {
                    duration: Theme.motionFast
                    easing.type: Easing.OutCubic
                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Encrypted history · %n conversation(s)", "", pane.controller.aiConversationHistory.count)
                        color: Theme.text
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textBody
                        font.weight: Font.DemiBold
                    }

                    BusyIndicator {
                        implicitWidth: 18
                        implicitHeight: 18
                        running: pane.controller.aiConversationHistory.busy
                        visible: running
                    }

                    Text {
                        visible: pane.controller.aiConversationHistory.errorCode.length > 0
                        text: pane.controller.aiConversationHistory.errorCode
                        color: Theme.danger
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textCompact
                    }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 5
                    model: pane.controller.aiConversationHistory
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Rectangle {
                        id: historyItem

                        required property string conversationId
                        required property string title
                        required property date updatedAt
                        required property int messageCount
                        required property string preview
                        width: ListView.view.width
                        height: 56
                        radius: Theme.radiusSmall
                        color: historyHover.hovered ? Theme.controlHover : Theme.controlBackground
                        border.color: Theme.border

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 9
                            anchors.rightMargin: 6
                            spacing: 8

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    Layout.fillWidth: true
                                    text: historyItem.title
                                    color: Theme.text
                                    elide: Text.ElideRight
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textLabel
                                    font.weight: Font.DemiBold
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("%n message(s)", "", historyItem.messageCount) + " · " + historyItem.updatedAt.toLocaleString(Qt.locale(), Locale.ShortFormat) + " · " + historyItem.preview
                                    color: Theme.textMuted
                                    elide: Text.ElideRight
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textCompact
                                }
                            }

                            ActionButton {
                                text: qsTr("Restore")
                                accessibleName: qsTr("Restore saved conversation")
                                onClicked: {
                                    if (pane.controller.restoreAiConversationHistory(historyItem.conversationId)) {
                                        pane.historyExpanded = false;
                                    }
                                }
                            }

                            ContextToolButton {
                                Accessible.name: qsTr("Delete saved conversation")
                                onClicked: pane.controller.aiConversationHistory.remove(historyItem.conversationId)
                                contentItem: AppIcon {
                                    name: "trash"
                                    color: Theme.textMuted
                                }
                            }
                        }

                        HoverHandler {
                            id: historyHover
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: parent.count === 0 && !pane.controller.aiConversationHistory.busy
                        text: qsTr("No saved conversations")
                        color: Theme.textMuted
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }
                }
            }
        }

        Rectangle {
            id: activityPanel

            objectName: "aiActivityPanel"
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.topMargin: pane.activityExpanded ? 6 : 0
            Layout.preferredHeight: pane.activityExpanded ? 196 : 0
            visible: pane.activityExpanded
            clip: true
            radius: Theme.radiusPanel
            color: Theme.raisedBackground
            border.color: Theme.border
            Accessible.role: Accessible.Pane
            Accessible.name: qsTr("AI activity audit")

            Behavior on Layout.preferredHeight {
                NumberAnimation {
                    duration: Theme.motionFast
                    easing.type: Easing.OutCubic
                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 7

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("AI activity · %n item(s)", "", pane.controller.aiActivity.count)
                        color: Theme.text
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textBody
                        font.weight: Font.DemiBold
                    }

                    Text {
                        text: qsTr("Metadata only")
                        color: Theme.textMuted
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textCompact
                    }

                    ContextToolButton {
                        Accessible.name: qsTr("Export AI activity")
                        onClicked: activityExportDialog.open()
                        contentItem: AppIcon {
                            name: "save"
                            color: Theme.textMuted
                        }
                        AppToolTip {
                            text: qsTr("Export audit metadata")
                        }
                    }

                    ContextToolButton {
                        enabled: pane.controller.aiActivity.count > 0
                        Accessible.name: qsTr("Clear AI activity")
                        onClicked: pane.controller.clearAiActivity()
                        contentItem: AppIcon {
                            name: "trash"
                            color: Theme.textMuted
                        }
                        AppToolTip {
                            text: qsTr("Delete audit metadata")
                        }
                    }
                }

                ListView {
                    id: activityList

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 5
                    model: pane.controller.aiActivity
                    boundsBehavior: Flickable.StopAtBounds
                    onCountChanged: Qt.callLater(positionViewAtEnd)

                    delegate: Rectangle {
                        id: activityItem

                        required property string timestamp
                        required property string toolName
                        required property string state
                        required property string resultCode
                        required property string permissionMode
                        required property bool sideEffecting
                        required property bool highRisk
                        width: ListView.view.width
                        height: 42
                        radius: Theme.radiusSmall
                        color: Theme.controlBackground
                        border.color: activityItem.highRisk ? Theme.dangerBorder : Theme.border

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 7

                            AppIcon {
                                Layout.preferredWidth: 15
                                Layout.preferredHeight: 15
                                name: activityItem.sideEffecting ? "terminal" : "search"
                                color: activityItem.highRisk ? Theme.dangerText : Theme.accent
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: 0

                                Text {
                                    Layout.fillWidth: true
                                    text: activityItem.toolName
                                    color: Theme.text
                                    elide: Text.ElideRight
                                    font.family: Theme.terminalFont
                                    font.pixelSize: Theme.textCompact
                                    font.weight: Font.Medium
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: activityItem.permissionMode + " · " + activityItem.resultCode
                                    color: Theme.textMuted
                                    elide: Text.ElideRight
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textCompact
                                }
                            }

                            Text {
                                text: activityItem.state
                                color: activityItem.state === "failed" || activityItem.state === "cancelled" ? Theme.dangerText : activityItem.state === "succeeded" ? Theme.successText : Theme.warning
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textCompact
                                font.weight: Font.Medium
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: activityList.count === 0
                        text: qsTr("No AI tool activity yet.")
                        color: Theme.textMuted
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textCompact
                    }
                }
            }
        }

        Item {
            id: conversationArea

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.topMargin: 6

            ListView {
                id: conversationList

                property bool stickToBottom: true
                readonly property bool closeToBottom: contentHeight <= height || contentY >= contentHeight - height - 24

                objectName: "aiConversationList"
                anchors.fill: parent
                clip: true
                spacing: 8
                model: pane.conversation
                boundsBehavior: Flickable.StopAtBounds
                onCountChanged: scrollToBottomIfNeeded()
                onContentHeightChanged: scrollToBottomIfNeeded()
                onContentYChanged: {
                    if (moving || dragging || flicking)
                        stickToBottom = closeToBottom;
                }
                onMovementEnded: stickToBottom = closeToBottom

                function scrollToBottomIfNeeded() {
                    if (!stickToBottom)
                        return;
                    Qt.callLater(() => {
                        if (conversationList.stickToBottom)
                            conversationList.positionViewAtEnd();
                    });
                }

                delegate: Item {
                    id: messageItem

                    required property int index
                    required property string messageRole
                    required property string text
                    required property string reasoning
                    required property string state
                    required property string error
                    required property bool truncated
                    required property var inputTokens
                    required property var outputTokens
                    required property var cachedInputTokens
                    required property var reasoningTokens
                    required property bool usageAvailable
                    required property var wallTimeMilliseconds
                    required property var firstTokenMilliseconds
                    required property var retryCount
                    required property bool estimatedCostKnown
                    required property real estimatedCostUsd
                    required property string costCatalogDate
                    required property bool longContextRates
                    required property string commandSuggestion
                    required property bool hasCommandSuggestion
                    required property var toolActivities
                    readonly property bool reasoningActive: state === "streaming" && reasoning.length > 0 && text.length === 0
                    width: ListView.view.width
                    height: messageBubble.implicitHeight

                    Rectangle {
                        id: messageBubble

                        anchors.right: messageItem.messageRole === "user" ? parent.right : undefined
                        anchors.left: messageItem.messageRole === "user" ? undefined : parent.left
                        width: messageItem.messageRole === "user" ? Math.min(parent.width * 0.92, Math.max(150, messageText.implicitWidth + 24)) : parent.width * 0.92
                        implicitHeight: messageColumn.implicitHeight + 18
                        radius: Theme.radiusPanel
                        color: messageItem.messageRole === "user" ? Theme.selectedBackground : Theme.elevatedBackground
                        border.color: messageItem.state === "failed" ? Theme.dangerBorder : Theme.border

                        ColumnLayout {
                            id: messageColumn

                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 9
                            spacing: 5

                            Button {
                                id: reasoningToggle

                                property bool autoManaged: true
                                property bool manualExpanded: false
                                readonly property bool expanded: autoManaged ? messageItem.reasoningActive : manualExpanded
                                Layout.fillWidth: true
                                Layout.preferredHeight: visible ? 30 : 0
                                visible: messageItem.messageRole === "assistant" && messageItem.reasoning.length > 0
                                text: messageItem.reasoningActive ? qsTr("Thinking…") : expanded ? qsTr("Hide model reasoning") : qsTr("Show model reasoning")
                                Accessible.name: text
                                onClicked: {
                                    manualExpanded = !expanded;
                                    autoManaged = false;
                                }

                                contentItem: RowLayout {
                                    spacing: 6

                                    AppIcon {
                                        Layout.preferredWidth: 14
                                        Layout.preferredHeight: 14
                                        name: reasoningToggle.expanded ? "chevron-down" : "chevron-right"
                                        color: Theme.textMuted
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: reasoningToggle.text
                                        color: Theme.textSoft
                                        font.family: Theme.uiFont
                                        font.pixelSize: Theme.textCompact
                                    }
                                }
                                background: Rectangle {
                                    radius: Theme.radiusSmall
                                    color: reasoningToggle.down ? Theme.controlPressed : reasoningToggle.hovered ? Theme.controlHover : "transparent"
                                }
                            }

                            MarkdownMessage {
                                Layout.fillWidth: true
                                Layout.maximumHeight: 180
                                visible: reasoningToggle.visible && reasoningToggle.expanded
                                source: messageItem.reasoning
                                color: Theme.textMuted
                                font.pixelSize: Theme.textCompact
                                onCopyRequested: text => pane.controller.copyAiText(text)
                            }

                            Repeater {
                                model: messageItem.toolActivities

                                delegate: Rectangle {
                                    id: toolCard

                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: toolCardContent.implicitHeight + 14
                                    radius: Theme.radiusControl
                                    color: Theme.controlBackground
                                    border.width: 1
                                    border.color: modelData.highRisk ? Theme.dangerBorder : modelData.state === "failed" ? Theme.dangerBorder : Theme.border

                                    RowLayout {
                                        id: toolCardContent

                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 8
                                        spacing: 7

                                        BusyIndicator {
                                            id: toolBusy

                                            Layout.preferredWidth: 16
                                            Layout.preferredHeight: 16
                                            running: toolCard.modelData.state === "queued" || toolCard.modelData.state === "running" || toolCard.modelData.state === "awaiting_approval"
                                            visible: running
                                        }

                                        AppIcon {
                                            Layout.preferredWidth: 15
                                            Layout.preferredHeight: 15
                                            visible: !toolBusy.visible
                                            name: toolCard.modelData.state === "succeeded" ? "check" : toolCard.modelData.state === "cancelled" || toolCard.modelData.state === "failed" ? "close" : toolCard.modelData.sideEffecting ? "terminal" : "search"
                                            color: toolCard.modelData.state === "succeeded" ? Theme.successText : toolCard.modelData.state === "failed" ? Theme.dangerText : Theme.textMuted
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            spacing: 1

                                            Text {
                                                Layout.fillWidth: true
                                                text: toolCard.modelData.name
                                                color: Theme.text
                                                elide: Text.ElideRight
                                                font.family: Theme.terminalFont
                                                font.pixelSize: Theme.textCompact
                                                font.weight: Font.DemiBold
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                visible: toolCard.modelData.summary.length > 0
                                                text: toolCard.modelData.summary
                                                color: Theme.textMuted
                                                elide: Text.ElideRight
                                                font.family: Theme.terminalFont
                                                font.pixelSize: Theme.textCompact
                                            }
                                        }

                                        Text {
                                            text: pane.toolStateLabel(toolCard.modelData.state, toolCard.modelData.resultCode)
                                            color: toolCard.modelData.state === "succeeded" ? Theme.successText : toolCard.modelData.state === "failed" ? Theme.dangerText : toolCard.modelData.state === "cancelled" ? Theme.warning : Theme.textMuted
                                            elide: Text.ElideRight
                                            font.family: Theme.uiFont
                                            font.pixelSize: Theme.textCompact
                                            font.weight: Font.Medium
                                        }
                                    }
                                }
                            }

                            MarkdownMessage {
                                id: messageText

                                Layout.fillWidth: true
                                source: messageItem.text.length > 0 ? messageItem.text : messageItem.state === "streaming" ? qsTr("Thinking…") : ""
                                color: Theme.text
                                textFormat: messageItem.messageRole === "assistant" ? TextEdit.MarkdownText : TextEdit.PlainText
                                font.pixelSize: Theme.textBody
                                onCopyRequested: text => pane.controller.copyAiText(text)
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: messageItem.state === "failed" || messageItem.state === "cancelled" || messageItem.truncated
                                text: messageItem.state === "failed" ? messageItem.error : messageItem.state === "cancelled" ? qsTr("Cancelled") : qsTr("Message was truncated locally.")
                                color: messageItem.state === "failed" ? Theme.dangerText : Theme.warning
                                wrapMode: Text.WordWrap
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textCompact
                            }

                            Text {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignRight
                                visible: messageItem.usageAvailable
                                text: {
                                    let parts = [qsTr("%1 in").arg(messageItem.inputTokens), qsTr("%1 out").arg(messageItem.outputTokens)];
                                    if (Number(messageItem.cachedInputTokens) > 0)
                                        parts.push(qsTr("%1 cached").arg(messageItem.cachedInputTokens));
                                    if (Number(messageItem.reasoningTokens) > 0)
                                        parts.push(qsTr("%1 reasoning").arg(messageItem.reasoningTokens));
                                    return parts.join(" · ");
                                }
                                color: Theme.textSubtle
                                horizontalAlignment: Text.AlignRight
                                wrapMode: Text.WordWrap
                                font.family: Theme.terminalFont
                                font.pixelSize: Theme.textCompact
                            }

                            Text {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignRight
                                visible: Number(messageItem.wallTimeMilliseconds) > 0 || Number(messageItem.retryCount) > 0
                                text: Number(messageItem.firstTokenMilliseconds) >= 0 ? qsTr("%1 ms first · %2 ms total · %3 retries").arg(messageItem.firstTokenMilliseconds).arg(messageItem.wallTimeMilliseconds).arg(messageItem.retryCount) : qsTr("%1 ms total · %2 retries").arg(messageItem.wallTimeMilliseconds).arg(messageItem.retryCount)
                                color: Theme.textSubtle
                                horizontalAlignment: Text.AlignRight
                                wrapMode: Text.WordWrap
                                font.family: Theme.terminalFont
                                font.pixelSize: Theme.textCompact
                            }

                            Text {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignRight
                                visible: messageItem.estimatedCostKnown
                                text: (messageItem.longContextRates ? qsTr("Est. $%1 · long-context rates · catalog %2") : qsTr("Est. $%1 · catalog %2")).arg(Number(messageItem.estimatedCostUsd).toFixed(6)).arg(messageItem.costCatalogDate)
                                color: Theme.textSubtle
                                horizontalAlignment: Text.AlignRight
                                wrapMode: Text.WordWrap
                                font.family: Theme.terminalFont
                                font.pixelSize: Theme.textCompact
                            }

                            Flow {
                                Layout.fillWidth: true
                                Layout.preferredHeight: implicitHeight
                                spacing: 6

                                ActionButton {
                                    visible: messageItem.messageRole === "assistant" && messageItem.state === "complete" && messageItem.hasCommandSuggestion
                                    text: qsTr("Insert")
                                    iconName: "composer"
                                    accessibleName: qsTr("Insert the suggested command without running it")
                                    onClicked: pane.controller.insertTerminalCommand(messageItem.commandSuggestion)
                                }

                                ActionButton {
                                    visible: messageItem.messageRole === "assistant" && messageItem.state === "complete" && messageItem.hasCommandSuggestion
                                    text: qsTr("Run")
                                    iconName: "play"
                                    variant: "primary"
                                    accessibleName: qsTr("Run the suggested command in the active terminal")
                                    onClicked: pane.controller.runTerminalCommand(messageItem.commandSuggestion)
                                }

                                ActionButton {
                                    visible: messageItem.messageRole === "assistant" && (messageItem.state === "failed" || messageItem.state === "cancelled") && messageItem.index === conversationList.count - 1
                                    text: qsTr("Retry")
                                    iconName: "refresh"
                                    accessibleName: qsTr("Retry the failed assistant response")
                                    onClicked: pane.controller.retryAiMessage()
                                }

                                ActionButton {
                                    visible: messageItem.messageRole === "assistant" && messageItem.text.length > 0
                                    text: qsTr("Copy")
                                    iconName: "copy"
                                    accessibleName: qsTr("Copy assistant response")
                                    onClicked: pane.controller.copyAiText(messageItem.text)
                                }
                            }
                        }
                    }
                }
            }

            Button {
                id: returnToLatestButton

                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 10
                z: 4
                width: 34
                height: 34
                visible: conversationList.count > 0 && !conversationList.closeToBottom
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.name: qsTr("Return to latest response")
                onClicked: {
                    conversationList.stickToBottom = true;
                    conversationList.positionViewAtEnd();
                }

                contentItem: AppIcon {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    name: "chevron-down"
                    color: Theme.text
                }

                background: Rectangle {
                    radius: width / 2
                    color: returnToLatestButton.down ? Theme.controlPressed : returnToLatestButton.hovered ? Theme.controlHover : Theme.elevatedBackground
                    border.color: returnToLatestButton.activeFocus ? Theme.focus : Theme.borderStrong
                    border.width: returnToLatestButton.activeFocus ? 2 : 1
                }

                AppToolTip {
                    text: qsTr("Return to latest response")
                }

                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }
            }

            Text {
                anchors.centerIn: parent
                width: Math.min(280, parent.width - 32)
                visible: pane.conversation === null || pane.conversation.count === 0
                text: qsTr("Ask about the active terminal, diagnose failures, or request a command.")
                color: Theme.textMuted
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                font.family: Theme.uiFont
                font.pixelSize: Theme.textBody
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(132, Math.min(352, promptEditor.contentHeight + 92 + slashCommandList.implicitHeight))
            color: Theme.elevatedBackground
            border.color: Theme.border

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    TextArea {
                        id: promptEditor

                        objectName: "aiPromptEditor"
                        placeholderText: pane.commandRequest ? qsTr("Describe the command you need · Enter sends · Shift+Enter adds a new line") : qsTr("Message ztermy Agent · Enter sends · Shift+Enter adds a new line")
                        color: Theme.text
                        placeholderTextColor: Theme.textMuted
                        selectionColor: Theme.accent
                        selectedTextColor: Theme.accentText
                        wrapMode: TextEdit.Wrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textBody
                        Accessible.name: qsTr("AI message")
                        Keys.onPressed: event => {
                            if (slashCommandList.visible && (event.key === Qt.Key_Down || event.key === Qt.Key_Up)) {
                                const offset = event.key === Qt.Key_Down ? 1 : -1;
                                slashCommandList.currentIndex = Math.max(0, Math.min(slashCommandList.count - 1, slashCommandList.currentIndex + offset));
                                event.accepted = true;
                                return;
                            }
                            if (slashCommandList.visible && event.key === Qt.Key_Tab) {
                                pane.applySlashSuggestion(slashCommandList.currentIndex >= 0 ? slashCommandList.suggestions[slashCommandList.currentIndex] : null);
                                event.accepted = true;
                                return;
                            }
                            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ShiftModifier) === 0 && !promptEditor.inputMethodComposing) {
                                if (slashCommandList.visible && slashCommandList.currentIndex >= 0) {
                                    pane.activateSlashSuggestion(slashCommandList.suggestions[slashCommandList.currentIndex]);
                                    event.accepted = true;
                                    return;
                                }
                                pane.sendPrompt();
                                event.accepted = true;
                            }
                        }
                        background: Rectangle {
                            radius: Theme.radiusControl
                            color: Theme.controlBackground
                            border.color: promptEditor.activeFocus ? Theme.focus : Theme.border
                            border.width: promptEditor.activeFocus ? 2 : 1
                        }
                    }
                }

                ListView {
                    id: slashCommandList

                    readonly property var suggestions: pane.slashSuggestions()

                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    implicitHeight: visible ? Math.min(132, contentHeight) : 0
                    visible: suggestions.length > 0
                    clip: true
                    spacing: 2
                    model: suggestions
                    currentIndex: 0
                    boundsBehavior: Flickable.StopAtBounds
                    onSuggestionsChanged: currentIndex = 0

                    delegate: Rectangle {
                        id: slashCommandDelegate

                        required property var modelData
                        required property int index

                        width: ListView.view.width
                        height: 40
                        radius: Theme.radiusSmall
                        color: slashCommandDelegate.ListView.isCurrentItem ? Theme.selectedBackground : slashCommandHover.hovered ? Theme.controlHover : "transparent"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 8

                            Text {
                                Layout.preferredWidth: 76
                                text: slashCommandDelegate.modelData.command
                                color: Theme.accent
                                font.family: Theme.terminalFont
                                font.pixelSize: Theme.textCompact
                                font.weight: Font.DemiBold
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: 0

                                Text {
                                    Layout.fillWidth: true
                                    text: slashCommandDelegate.modelData.title
                                    color: Theme.text
                                    elide: Text.ElideRight
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textCompact
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: slashCommandDelegate.modelData.description
                                    color: Theme.textSubtle
                                    elide: Text.ElideRight
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textCompact
                                }
                            }
                        }

                        TapHandler {
                            onTapped: {
                                slashCommandList.currentIndex = slashCommandDelegate.index;
                                pane.activateSlashSuggestion(slashCommandDelegate.modelData);
                            }
                        }

                        HoverHandler {
                            id: slashCommandHover
                        }
                    }

                    ScrollBar.vertical: ScrollBar {}
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    ContextToolButton {
                        id: attachContextButton

                        enabled: !pane.busy
                        Accessible.name: qsTr("Attach terminal context")
                        onClicked: attachmentMenu.popup()
                        contentItem: AppIcon {
                            name: "plus"
                            color: Theme.textMuted
                        }
                        AppToolTip {
                            text: qsTr("Attach selected text or recent commands")
                        }
                    }

                    ContextToolButton {
                        id: commandRequestButton

                        checkable: true
                        checked: pane.commandRequest
                        enabled: !pane.busy
                        Accessible.name: qsTr("Toggle command generation mode")
                        onClicked: pane.commandRequest = !pane.commandRequest
                        contentItem: AppIcon {
                            name: "terminal"
                            color: commandRequestButton.checked ? Theme.accent : Theme.textMuted
                        }
                        AppToolTip {
                            text: pane.commandRequest ? qsTr("Command generation enabled") : qsTr("Generate a shell command")
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    AppComboBox {
                        id: modelBox

                        objectName: "aiModelBox"
                        Layout.preferredWidth: pane.width < 380 ? 94 : 124
                        model: pane.modelOptions()
                        currentIndex: Math.max(0, model.indexOf(pane.controller.aiModel))
                        accessibleName: qsTr("AI model")
                        enabled: !pane.busy && model.length > 0
                        onActivated: index => pane.selectModel(model[index])

                        AppToolTip {
                            text: qsTr("Model · %1").arg(pane.controller.aiModel)
                        }
                    }

                    AppComboBox {
                        id: agentModeBox

                        objectName: "aiAgentModeBox"
                        Layout.preferredWidth: pane.width < 380 ? 82 : 96
                        model: ["read-only", "ask", "edit", "auto", "yolo"]
                        displayTextModel: [qsTr("Read-only"), qsTr("Ask"), qsTr("Edit"), qsTr("Auto"), qsTr("YOLO")]
                        currentIndex: pane.permissionModeIndex(pane.controller.aiPermissionPreference)
                        accessibleName: qsTr("Agent execution mode")
                        enabled: !pane.busy
                        onActivated: index => pane.controller.setAiPermissionMode(model[index])
                    }

                    ActionButton {
                        objectName: "aiSendButton"
                        Layout.preferredWidth: pane.busy ? 82 : 36
                        text: pane.busy ? qsTr("Cancel") : ""
                        iconName: pane.busy ? "close" : "play"
                        variant: pane.busy ? "destructive" : "primary"
                        enabled: pane.busy || promptEditor.text.trim().length > 0
                        accessibleName: text
                        onClicked: {
                            if (pane.busy) {
                                pane.controller.cancelAiMessage();
                            } else {
                                pane.sendPrompt();
                            }
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: conversationExportDialog

        title: qsTr("Export AI conversation")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "md"
        nameFilters: [qsTr("Markdown files (*.md)"), qsTr("All files (*)")]
        onAccepted: pane.controller.exportAiConversation(selectedFile.toString())
    }

    FileDialog {
        id: activityExportDialog

        title: qsTr("Export AI activity metadata")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [qsTr("JSON files (*.json)"), qsTr("All files (*)")]
        onAccepted: pane.controller.exportAiActivity(selectedFile.toString())
    }

    AppMenu {
        id: attachmentMenu

        AppMenuItem {
            text: qsTr("Selected terminal text")
            onTriggered: pane.controller.attachAiSelection()
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: qsTr("Last command")
            onTriggered: pane.controller.attachAiRecentCommands(1)
        }
        AppMenuItem {
            text: qsTr("Last 3 commands")
            onTriggered: pane.controller.attachAiRecentCommands(3)
        }
        AppMenuItem {
            text: qsTr("Last 5 commands")
            onTriggered: pane.controller.attachAiRecentCommands(5)
        }
    }

    AppMenu {
        id: conversationMenu

        AppMenuItem {
            text: qsTr("Export conversation")
            enabled: !pane.busy && pane.conversation !== null && pane.conversation.count > 0
            onTriggered: conversationExportDialog.open()
        }

        AppMenuSeparator {}

        AppMenuItem {
            text: pane.activityExpanded ? qsTr("Hide activity details") : qsTr("Show activity details")
            onTriggered: {
                pane.activityExpanded = !pane.activityExpanded;
                if (pane.activityExpanded) {
                    pane.historyExpanded = false;
                }
            }
        }
        AppMenuItem {
            text: qsTr("Explain last failed command")
            enabled: !pane.busy
            onTriggered: pane.controller.explainAiLastFailure()
        }
    }
}
