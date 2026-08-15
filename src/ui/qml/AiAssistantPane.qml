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
    readonly property string approvalRuleDefaultPattern: typeof toolApproval.ruleDefaultPattern === "string" ? toolApproval.ruleDefaultPattern : approvalRuleSubject
    readonly property bool approvalProfileAvailable: toolApproval.profileAvailable === true
    property bool contextExpanded: false
    property bool activityExpanded: false
    property bool historyExpanded: false
    property bool commandRequest: false
    property bool webSearchEnabled: false
    property var selectedSkillSlugs: []
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
        if ((prompt.length === 0 && pendingImageCount() === 0) || busy) {
            return;
        }
        if (prompt.startsWith("/") && executeSlashCommand(prompt)) {
            promptEditor.clear();
            return;
        }
        const accepted = controller.sendAiPrompt(prompt, commandRequest, selectedSkillSlugs, webSearchEnabled && controller.aiWebSearchAvailable);
        if (accepted) {
            promptEditor.clear();
            selectedSkillSlugs = [];
        }
    }

    function pendingImageCount() {
        const items = pane.controller.activeAiContextItems || [];
        let count = 0;
        for (let index = 0; index < items.length; ++index) {
            if (items[index].kind === "image")
                ++count;
        }
        return count;
    }

    function addSelectedSkill(slug) {
        if (!slug || selectedSkillSlugs.indexOf(slug) >= 0 || selectedSkillSlugs.length >= 4)
            return;
        selectedSkillSlugs = selectedSkillSlugs.concat([slug]);
    }

    function removeSelectedSkill(slug) {
        const next = [];
        for (let index = 0; index < selectedSkillSlugs.length; ++index) {
            if (selectedSkillSlugs[index] !== slug)
                next.push(selectedSkillSlugs[index]);
        }
        selectedSkillSlugs = next;
    }

    function focusEditor() {
        Qt.callLater(promptEditor.forceActiveFocus);
    }

    function permissionModeIndex(token) {
        return token === "read-only" ? 0 : token === "auto" ? 2 : token === "yolo" ? 3 : 1;
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

    function agentOption(token) {
        const options = pane.controller.aiAgentOptions || [];
        for (let index = 0; index < options.length; ++index) {
            if (options[index].id === token)
                return options[index];
        }
        return {
            "id": token,
            "name": token,
            "description": "",
            "state": "unavailable",
            "available": false
        };
    }

    function selectAgent(token) {
        if (token === pane.controller.aiAgentPreference)
            return;
        pane.controller.setAiAgentPreference(token);
    }

    function selectModel(model) {
        if (!model || model === pane.controller.aiModel)
            return;
        pane.controller.saveAiProviderSettings(pane.controller.aiProviderPreference, pane.controller.aiBaseUrl, pane.controller.aiEndpointPath, model, pane.controller.aiAutomaticContext, pane.controller.aiPermissionPreference);
    }

    function slashSuggestions() {
        const text = promptEditor ? promptEditor.text.trim().toLocaleLowerCase() : "";
        const mention = mentionQuery();
        if (mention !== null)
            return mentionSuggestions(mention);
        if (!text.startsWith("/") || text.indexOf(" ") >= 0)
            return [];
        const commands = slashCommands.slice();
        const quickMessages = pane.controller.aiQuickMessages || [];
        const quickMessageSlugs = [];
        for (let index = 0; index < quickMessages.length; ++index) {
            const message = quickMessages[index];
            quickMessageSlugs.push(message.slug);
            commands.push({
                "command": "/" + message.slug,
                "title": message.name,
                "description": qsTr("Quick message · %1").arg(message.description || qsTr("Reusable prompt")),
                "content": message.content,
                "quickMessage": true
            });
        }
        const skills = pane.controller.aiUserSkills || [];
        for (let skillIndex = 0; skillIndex < skills.length; ++skillIndex) {
            const skill = skills[skillIndex];
            if (skill.ready !== true || quickMessageSlugs.indexOf(skill.id) >= 0 || selectedSkillSlugs.indexOf(skill.id) >= 0)
                continue;
            commands.push({
                "command": "/" + skill.id,
                "title": skill.name,
                "description": qsTr("Skill · %1").arg(skill.description),
                "skill": true,
                "skillId": skill.id
            });
        }
        const query = text.slice(1);
        return commands.filter(item => item.command.startsWith(text) || item.title.toLocaleLowerCase().indexOf(query) >= 0);
    }

    function mentionQuery() {
        if (!promptEditor)
            return null;
        const match = promptEditor.text.match(/(?:^|\s)@([^\s@]*)$/);
        return match ? match[1].toLocaleLowerCase() : null;
    }

    function mentionSuggestions(query) {
        const suggestions = [
            {
                "command": "@selection",
                "title": qsTr("Selected terminal text"),
                "description": qsTr("Attach the current terminal selection"),
                "contextAction": "selection"
            },
            {
                "command": "@last",
                "title": qsTr("Last command"),
                "description": qsTr("Attach the most recent command and its output"),
                "contextAction": "last"
            },
            {
                "command": "@last3",
                "title": qsTr("Last 3 commands"),
                "description": qsTr("Attach the three most recent command blocks"),
                "contextAction": "last3"
            },
            {
                "command": "@last5",
                "title": qsTr("Last 5 commands"),
                "description": qsTr("Attach the five most recent command blocks"),
                "contextAction": "last5"
            },
            {
                "command": "@files",
                "title": qsTr("Local text files"),
                "description": qsTr("Choose one or more text files"),
                "contextAction": "files"
            },
            {
                "command": "@images",
                "title": qsTr("Images"),
                "description": qsTr("Choose one or more images"),
                "contextAction": "images"
            }
        ];
        return suggestions.filter(item => query.length === 0 || item.command.slice(1).startsWith(query) || item.title.toLocaleLowerCase().indexOf(query) >= 0);
    }

    function removeActiveMention() {
        const marker = promptEditor.text.lastIndexOf("@");
        if (marker < 0)
            return;
        promptEditor.text = promptEditor.text.slice(0, marker).replace(/\s+$/, "");
        promptEditor.cursorPosition = promptEditor.text.length;
    }

    function activateContextSuggestion(item) {
        if (!item || !item.contextAction)
            return false;
        let accepted = true;
        switch (item.contextAction) {
        case "selection":
            accepted = pane.controller.attachAiSelection();
            break;
        case "last":
            accepted = pane.controller.attachAiRecentCommands(1);
            break;
        case "last3":
            accepted = pane.controller.attachAiRecentCommands(3);
            break;
        case "last5":
            accepted = pane.controller.attachAiRecentCommands(5);
            break;
        case "files":
            textAttachmentDialog.open();
            break;
        case "images":
            imageAttachmentDialog.open();
            break;
        default:
            return false;
        }
        if (accepted)
            removeActiveMention();
        promptEditor.forceActiveFocus();
        return true;
    }

    function applySlashSuggestion(item) {
        if (!item)
            return;
        if (item.contextAction) {
            activateContextSuggestion(item);
            return;
        }
        if (item.skill === true) {
            pane.addSelectedSkill(item.skillId);
            promptEditor.clear();
            promptEditor.forceActiveFocus();
            return;
        }
        promptEditor.text = item.quickMessage === true ? item.content : item.command + (item.command === "/command" ? " " : "");
        promptEditor.cursorPosition = promptEditor.text.length;
        promptEditor.forceActiveFocus();
    }

    function activateSlashSuggestion(item) {
        if (!item)
            return;
        if (item.contextAction) {
            activateContextSuggestion(item);
            return;
        }
        if (item.skill === true) {
            applySlashSuggestion(item);
            return;
        }
        if (item.quickMessage === true) {
            applySlashSuggestion(item);
            return;
        }
        if (item.command === "/command") {
            applySlashSuggestion(item);
            return;
        }
        if (executeSlashCommand(item.command))
            promptEditor.clear();
    }

    onActiveTabChanged: selectedSkillSlugs = []

    Component.onCompleted: controller.ensureAiUserSkillsLoaded()

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

    function approvalRuleDescription() {
        const scope = approvalScopeBox.currentValue;
        const scopeText = scope === "once" ? qsTr("Only this pending action; no rule is saved.") : scope === "session" ? qsTr("Until this terminal session is closed.") : scope === "profile" ? qsTr("Saved for future sessions that use this profile.") : qsTr("Saved for every profile and session.");
        if (scope === "once")
            return scopeText;
        const matcher = approvalMatcherBox.currentValue;
        const matcherText = matcher === "exact" ? qsTr("Matches the entire action exactly.") : matcher === "prefix" ? qsTr("Matches this command-token prefix and later arguments.") : matcher === "glob" ? qsTr("Matches the entire action with * and ? wildcards.") : matcher === "regex" ? qsTr("Matches the entire action with an expert regular expression.") : qsTr("Matches every action in this capability.");
        return scopeText + " " + matcherText;
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
        approvalPatternField.text = approvalRuleDefaultPattern;
    }

    color: Theme.panelBackground
    clip: true
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Terminal AI assistant")
    onVisibleChanged: {
        if (visible) {
            controller.refreshAiAgents();
            focusEditor();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            color: Theme.elevatedBackground
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 4

                ToolButton {
                    id: agentPickerButton

                    objectName: "aiAgentPickerButton"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    enabled: !pane.busy && !pane.controller.aiAgentsLoading
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    Accessible.name: qsTr("Choose terminal AI Agent")
                    onClicked: agentMenu.popup()
                    contentItem: RowLayout {
                        spacing: 7

                        AppIcon {
                            Layout.preferredWidth: 17
                            Layout.preferredHeight: 17
                            name: "ai"
                            color: Theme.accent
                        }

                        Text {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: pane.agentOption(pane.controller.aiAgentPreference).name
                            elide: Text.ElideRight
                            color: Theme.text
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textBody
                            font.weight: Font.DemiBold
                        }

                        AppIcon {
                            Layout.preferredWidth: 12
                            Layout.preferredHeight: 12
                            name: "chevron-down"
                            color: Theme.textMuted
                        }
                    }
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: agentPickerButton.down ? Theme.controlPressed : agentPickerButton.hovered ? Theme.controlHover : "transparent"
                        border.color: agentPickerButton.activeFocus ? Theme.focus : "transparent"
                        border.width: agentPickerButton.activeFocus ? 2 : 0
                    }

                    HoverHandler {
                        cursorShape: agentPickerButton.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    }

                    AppToolTip {
                        text: (pane.activeTab ? pane.activeTab.title + " · " : "") + pane.agentOption(pane.controller.aiAgentPreference).description
                    }

                    AppMenu {
                        id: agentMenu

                        y: agentPickerButton.height + 4

                        AppMenuItem {
                            text: qsTr("ztermy Agent")
                            checkable: true
                            checked: pane.controller.aiAgentPreference === "ztermy"
                            enabled: !pane.busy
                            onTriggered: pane.selectAgent("ztermy")
                        }

                        AppMenuItem {
                            text: pane.agentOption("codex").available ? qsTr("Codex") : pane.controller.aiAgentsLoading ? qsTr("Codex · detecting") : qsTr("Codex · unavailable")
                            checkable: true
                            checked: pane.controller.aiAgentPreference === "codex"
                            enabled: !pane.busy && pane.agentOption("codex").available
                            onTriggered: pane.selectAgent("codex")
                        }

                        AppMenuSeparator {}

                        AppMenuItem {
                            text: pane.controller.aiAgentsLoading ? qsTr("Detecting Agents…") : qsTr("Detect Agents")
                            enabled: !pane.controller.aiAgentsLoading
                            onTriggered: pane.controller.refreshAiAgents()
                        }
                    }
                }

                ContextToolButton {
                    objectName: "aiExportConversationButton"
                    enabled: !pane.busy && pane.conversation !== null && pane.conversation.count > 0
                    Accessible.name: qsTr("Export AI conversation")
                    onClicked: conversationExportDialog.open()
                    contentItem: AppIcon {
                        name: "save"
                        color: Theme.textMuted
                    }
                    AppToolTip {
                        text: qsTr("Export conversation")
                    }
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
            Layout.preferredHeight: 0
            visible: false
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
            visible: !pane.historyExpanded && pane.contextExpanded && pane.controller.activeAiContextItems.length > 0
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
                            property bool expanded: false
                            readonly property bool expandable: modelData.kind !== "image" && modelData.preview && modelData.preview.length > 0
                            readonly property real summaryHeight: modelData.kind === "image" ? 44 : 30

                            Layout.fillWidth: true
                            Layout.preferredHeight: contextItem.summaryHeight + (contextItem.expanded ? Math.min(88, contextPreviewText.implicitHeight + 8) : 0)
                            radius: Theme.radiusSmall
                            color: Theme.controlBackground

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 0

                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: contextItem.summaryHeight
                                    spacing: 6

                                    ContextToolButton {
                                        visible: contextItem.expandable
                                        Accessible.name: contextItem.expanded ? qsTr("Collapse %1 preview").arg(contextItem.modelData.title) : qsTr("Expand %1 preview").arg(contextItem.modelData.title)
                                        onClicked: contextItem.expanded = !contextItem.expanded
                                        contentItem: AppIcon {
                                            name: contextItem.expanded ? "chevron-down" : "chevron-right"
                                            color: Theme.textMuted
                                        }

                                        AppToolTip {
                                            text: contextItem.expanded ? qsTr("Collapse preview") : qsTr("Preview context")
                                        }
                                    }

                                    Image {
                                        Layout.preferredWidth: 32
                                        Layout.preferredHeight: 32
                                        visible: contextItem.modelData.kind === "image"
                                        source: visible ? contextItem.modelData.previewUrl : ""
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        cache: true
                                    }

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
                                        color: contextItem.modelData.quality === "rich" ? Theme.successText : contextItem.modelData.quality === "image" ? Theme.accent : Theme.warning
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

                                        visible: contextItem.modelData.kind !== "image"
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

                                Text {
                                    id: contextPreviewText

                                    Layout.fillWidth: true
                                    Layout.leftMargin: 24
                                    Layout.rightMargin: 4
                                    Layout.bottomMargin: 6
                                    visible: contextItem.expanded
                                    text: contextItem.modelData.preview || ""
                                    color: Theme.textMuted
                                    wrapMode: Text.WrapAnywhere
                                    maximumLineCount: 5
                                    elide: Text.ElideRight
                                    font.family: Theme.terminalFont
                                    font.pixelSize: Theme.textCompact
                                }
                            }
                        }
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
            focusPolicy: Qt.StrongFocus
            activeFocusOnTab: true
            Accessible.role: Accessible.Pane
            Accessible.name: pane.approvalKind.indexOf("queue_sftp_") === 0 ? qsTr("AI SFTP transfer approval") : pane.approvalKind === "interrupt_command" ? qsTr("AI interrupt approval") : pane.approvalKind === "write_to_pty" ? qsTr("AI terminal input approval") : pane.approvalKind === "save_runbook" ? qsTr("AI runbook approval") : qsTr("AI command approval")
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    pane.approvePendingTool();
                    event.accepted = true;
                } else if (event.key === Qt.Key_Escape) {
                    pane.denyPendingTool();
                    event.accepted = true;
                }
            }
            onVisibleChanged: {
                if (visible) {
                    forceActiveFocus();
                }
            }

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

                Flickable {
                    id: approvalCommandViewport

                    objectName: "aiApprovalCommandViewport"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(120, Math.max(42, approvalCommandEdit.contentHeight + 12))
                    Layout.maximumHeight: 120
                    contentWidth: width
                    contentHeight: approvalCommandEdit.contentHeight + 12
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true
                    Accessible.name: pane.approvalKind === "interrupt_command" ? qsTr("Interrupt awaiting approval") : pane.approvalKind === "write_to_pty" ? qsTr("Terminal input awaiting approval") : pane.approvalKind === "save_runbook" ? qsTr("Runbook awaiting approval") : qsTr("Command awaiting approval")

                    Rectangle {
                        anchors.fill: parent
                        z: 0
                        radius: Theme.radiusSmall
                        color: Theme.controlBackground
                        border.color: Theme.border
                    }

                    TextEdit {
                        id: approvalCommandEdit

                        objectName: "aiApprovalCommandText"
                        x: 6
                        y: 6
                        z: 1
                        width: Math.max(0, approvalCommandViewport.width - 12)
                        height: contentHeight
                        text: pane.approvalCommand
                        color: Theme.text
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        textFormat: TextEdit.PlainText
                        font.family: Theme.terminalFont
                        font.pixelSize: Theme.textBody
                    }

                    ScrollBar.vertical: ScrollBar {
                        policy: approvalCommandViewport.contentHeight > approvalCommandViewport.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
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

                Text {
                    Layout.fillWidth: true
                    visible: pane.approvalRuleSupported
                    text: pane.approvalRuleDescription()
                    color: Theme.textSubtle
                    wrapMode: Text.WordWrap
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textCompact
                }

                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: 7

                    Text {
                        Layout.alignment: Qt.AlignVCenter
                        text: qsTr("Enter approve · Esc deny")
                        color: Theme.textSubtle
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textCompact
                    }

                    ActionButton {
                        text: pane.approvalRuleSupported && approvalScopeBox.currentValue !== "once" ? qsTr("Deny & remember") : qsTr("Deny")
                        iconName: "close"
                        accessibleName: qsTr("Deny the pending AI terminal action")
                        onClicked: pane.denyPendingTool()
                    }

                    ActionButton {
                        text: pane.approvalRuleSupported && approvalScopeBox.currentValue !== "once" ? qsTr("Allow & remember") : pane.approvalKind.indexOf("queue_sftp_") === 0 ? qsTr("Queue transfer") : pane.approvalKind === "interrupt_command" ? qsTr("Send Ctrl+C") : pane.approvalKind === "write_to_pty" ? qsTr("Send input") : pane.approvalKind === "save_runbook" ? qsTr("Save runbook") : qsTr("Run command")
                        iconName: pane.approvalKind.indexOf("queue_sftp_") === 0 ? "transfer" : pane.approvalKind === "interrupt_command" ? "close" : pane.approvalKind === "write_to_pty" ? "compose" : pane.approvalKind === "save_runbook" ? "save" : "play"
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
            Layout.bottomMargin: pane.historyExpanded ? 8 : 0
            Layout.fillHeight: pane.historyExpanded
            Layout.preferredHeight: 0
            visible: pane.historyExpanded
            clip: true
            radius: Theme.radiusPanel
            color: Theme.raisedBackground
            border.color: Theme.border
            Accessible.role: Accessible.Pane
            Accessible.name: qsTr("AI conversation history")

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("All conversations · %n conversation(s)", "", pane.controller.aiConversationHistory.count)
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

                    ContextToolButton {
                        Accessible.name: qsTr("Close conversation history")
                        onClicked: pane.historyExpanded = false
                        contentItem: AppIcon {
                            name: "close"
                            color: Theme.textMuted
                        }

                        AppToolTip {
                            text: qsTr("Close")
                        }
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
                        required property string agent
                        width: ListView.view.width
                        height: 52
                        radius: Theme.radiusSmall
                        color: historyContentHover.hovered ? Theme.controlHover : Theme.controlBackground
                        border.color: historyItem.activeFocus ? Theme.focus : Theme.border
                        border.width: historyItem.activeFocus ? 2 : 1
                        focusPolicy: Qt.StrongFocus
                        activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Restore conversation %1").arg(historyItem.title)
                        Keys.onPressed: event => {
                            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                                if (pane.controller.restoreAiConversationHistory(historyItem.conversationId))
                                    pane.historyExpanded = false;
                                event.accepted = true;
                            }
                        }

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
                                    text: pane.agentOption(historyItem.agent).name + " · " + qsTr("%n message(s)", "", historyItem.messageCount) + " · " + historyItem.updatedAt.toLocaleString(Qt.locale(), Locale.ShortFormat) + " · " + historyItem.preview
                                    color: Theme.textMuted
                                    elide: Text.ElideRight
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textCompact
                                }

                                HoverHandler {
                                    id: historyContentHover

                                    cursorShape: Qt.PointingHandCursor
                                }

                                TapHandler {
                                    onTapped: {
                                        historyItem.forceActiveFocus();
                                        if (pane.controller.restoreAiConversationHistory(historyItem.conversationId))
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
            visible: !pane.historyExpanded

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
                    required property var imageAttachments
                    required property var sources
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

                            Flow {
                                Layout.fillWidth: true
                                Layout.preferredHeight: visible ? implicitHeight : 0
                                visible: messageItem.imageAttachments.length > 0
                                spacing: 6

                                Repeater {
                                    model: messageItem.imageAttachments

                                    delegate: Rectangle {
                                        id: messageImageCard

                                        required property var modelData
                                        width: Math.min(164, Math.max(112, messageColumn.width))
                                        height: 112
                                        radius: Theme.radiusControl
                                        color: Theme.controlBackground
                                        border.color: Theme.border
                                        clip: true
                                        Accessible.role: Accessible.Graphic
                                        Accessible.name: qsTr("Attached image %1").arg(modelData.fileName)

                                        Image {
                                            anchors.fill: parent
                                            anchors.margins: 1
                                            source: messageImageCard.modelData.previewUrl
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                            cache: true
                                        }

                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            height: 25
                                            color: Qt.rgba(0, 0, 0, 0.62)

                                            Text {
                                                anchors.fill: parent
                                                anchors.leftMargin: 7
                                                anchors.rightMargin: 7
                                                text: messageImageCard.modelData.fileName
                                                color: "white"
                                                elide: Text.ElideMiddle
                                                verticalAlignment: Text.AlignVCenter
                                                font.family: Theme.uiFont
                                                font.pixelSize: Theme.textCompact
                                            }
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

                            Button {
                                id: sourcesToggle

                                property bool expanded: false
                                Layout.fillWidth: true
                                Layout.preferredHeight: visible ? 30 : 0
                                visible: messageItem.messageRole === "assistant" && messageItem.sources.length > 0
                                text: expanded ? qsTr("Hide sources · %1").arg(messageItem.sources.length) : qsTr("Sources · %1").arg(messageItem.sources.length)
                                Accessible.name: text
                                onClicked: expanded = !expanded

                                contentItem: RowLayout {
                                    spacing: 6

                                    AppIcon {
                                        Layout.preferredWidth: 14
                                        Layout.preferredHeight: 14
                                        name: sourcesToggle.expanded ? "chevron-down" : "chevron-right"
                                        color: Theme.textMuted
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: sourcesToggle.text
                                        color: Theme.textSoft
                                        font.family: Theme.uiFont
                                        font.pixelSize: Theme.textCompact
                                    }
                                }
                                background: Rectangle {
                                    radius: Theme.radiusSmall
                                    color: sourcesToggle.down ? Theme.controlPressed : sourcesToggle.hovered ? Theme.controlHover : "transparent"
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.preferredHeight: visible ? implicitHeight : 0
                                visible: sourcesToggle.visible && sourcesToggle.expanded
                                spacing: 3

                                Repeater {
                                    model: messageItem.sources

                                    delegate: Button {
                                        id: sourceButton

                                        required property var modelData
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        Layout.preferredHeight: 38
                                        hoverEnabled: true
                                        focusPolicy: Qt.StrongFocus
                                        Accessible.name: qsTr("Open source %1").arg(modelData.title || modelData.url)
                                        onClicked: Qt.openUrlExternally(modelData.url)

                                        contentItem: ColumnLayout {
                                            spacing: 0

                                            Text {
                                                Layout.fillWidth: true
                                                Layout.minimumWidth: 0
                                                text: sourceButton.modelData.title || sourceButton.modelData.url
                                                color: sourceButton.hovered || sourceButton.activeFocus ? Theme.accent : Theme.text
                                                elide: Text.ElideRight
                                                font.family: Theme.uiFont
                                                font.pixelSize: Theme.textCompact
                                                font.weight: Font.Medium
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                Layout.minimumWidth: 0
                                                text: sourceButton.modelData.url
                                                color: Theme.textSubtle
                                                elide: Text.ElideMiddle
                                                font.family: Theme.terminalFont
                                                font.pixelSize: Theme.textCompact
                                            }
                                        }
                                        background: Rectangle {
                                            radius: Theme.radiusSmall
                                            color: sourceButton.down ? Theme.controlPressed : sourceButton.hovered ? Theme.controlHover : Theme.controlBackground
                                            border.color: sourceButton.activeFocus ? Theme.focus : Theme.border
                                            border.width: sourceButton.activeFocus ? 2 : 1
                                        }

                                        HoverHandler {
                                            cursorShape: Qt.PointingHandCursor
                                        }

                                        AppToolTip {
                                            text: sourceButton.modelData.citedText || sourceButton.modelData.url
                                        }
                                    }
                                }
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

            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(300, parent.width - 32)
                visible: pane.conversation === null || pane.conversation.count === 0
                spacing: 10

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Ask about the active terminal, diagnose failures, or request a command.")
                    color: Theme.textMuted
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textBody
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: pane.controller.aiConversationHistoryEnabled && pane.controller.aiConversationHistory.count > 0
                    spacing: 3

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Recent conversations")
                            color: Theme.textSubtle
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textCompact
                            font.weight: Font.Medium
                        }

                        ToolButton {
                            id: viewAllHistoryButton

                            implicitHeight: 26
                            text: qsTr("View all")
                            hoverEnabled: true
                            focusPolicy: Qt.StrongFocus
                            Accessible.name: qsTr("View all AI conversations")
                            onClicked: {
                                pane.historyExpanded = true;
                                pane.activityExpanded = false;
                                pane.controller.aiConversationHistory.reload();
                            }
                            contentItem: Text {
                                text: viewAllHistoryButton.text
                                color: viewAllHistoryButton.hovered || viewAllHistoryButton.activeFocus ? Theme.accent : Theme.textMuted
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textCompact
                            }
                            background: Rectangle {
                                radius: Theme.radiusSmall
                                color: viewAllHistoryButton.down ? Theme.controlPressed : viewAllHistoryButton.hovered ? Theme.controlHover : "transparent"
                                border.color: viewAllHistoryButton.activeFocus ? Theme.focus : "transparent"
                                border.width: viewAllHistoryButton.activeFocus ? 2 : 0
                            }

                            HoverHandler {
                                cursorShape: Qt.PointingHandCursor
                            }
                        }
                    }

                    Repeater {
                        model: pane.controller.aiConversationHistory

                        delegate: Rectangle {
                            id: recentConversation

                            required property int index
                            required property string conversationId
                            required property string title
                            required property date updatedAt
                            required property string agent
                            Layout.fillWidth: true
                            Layout.preferredHeight: visible ? 34 : 0
                            visible: index < 3
                            radius: Theme.radiusSmall
                            color: recentHover.hovered ? Theme.controlHover : "transparent"
                            border.color: recentConversation.activeFocus ? Theme.focus : "transparent"
                            border.width: recentConversation.activeFocus ? 2 : 0
                            focusPolicy: visible ? Qt.StrongFocus : Qt.NoFocus
                            activeFocusOnTab: visible
                            Accessible.role: Accessible.Button
                            Accessible.name: qsTr("Restore conversation %1").arg(recentConversation.title)
                            Keys.onPressed: event => {
                                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                                    pane.controller.restoreAiConversationHistory(recentConversation.conversationId);
                                    event.accepted = true;
                                }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 7
                                anchors.rightMargin: 7
                                spacing: 8

                                Text {
                                    Layout.fillWidth: true
                                    text: recentConversation.title
                                    color: Theme.textSoft
                                    elide: Text.ElideRight
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textLabel
                                }

                                Text {
                                    text: pane.agentOption(recentConversation.agent).name
                                    color: Theme.textSubtle
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textCompact
                                }

                                Text {
                                    text: recentConversation.updatedAt.toLocaleString(Qt.locale(), Locale.ShortFormat)
                                    color: Theme.textSubtle
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.textCompact
                                }
                            }

                            HoverHandler {
                                id: recentHover

                                cursorShape: Qt.PointingHandCursor
                            }

                            TapHandler {
                                onTapped: pane.controller.restoreAiConversationHistory(recentConversation.conversationId)
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            id: composerPanel

            Layout.fillWidth: true
            Layout.preferredHeight: visible ? Math.max(132, Math.min(420, promptEditor.contentHeight + 92 + slashCommandList.implicitHeight + (selectedSkillFlow.visible ? 34 : 0) + (composerContextList.visible ? 34 : 0))) : 0
            visible: !pane.historyExpanded
            color: Theme.elevatedBackground
            border.color: Theme.border

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                ListView {
                    id: composerContextList

                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 28 : 0
                    visible: pane.controller.activeAiContextItems.length > 0
                    orientation: ListView.Horizontal
                    spacing: 5
                    clip: true
                    model: pane.controller.activeAiContextItems
                    boundsBehavior: Flickable.StopAtBounds

                    header: ToolButton {
                        id: composerContextToggle

                        width: 28
                        height: 28
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        Accessible.name: pane.contextExpanded ? qsTr("Hide request context details") : qsTr("Show request context details")
                        onClicked: pane.contextExpanded = !pane.contextExpanded
                        contentItem: AppIcon {
                            anchors.centerIn: parent
                            width: 14
                            height: 14
                            name: pane.contextExpanded ? "chevron-down" : "chevron-right"
                            color: Theme.accent
                        }
                        background: Rectangle {
                            radius: height / 2
                            color: composerContextToggle.down ? Theme.controlPressed : composerContextToggle.hovered ? Theme.controlHover : Theme.controlBackground
                            border.color: composerContextToggle.activeFocus ? Theme.focus : Theme.border
                            border.width: composerContextToggle.activeFocus ? 2 : 1
                        }
                    }

                    delegate: Rectangle {
                        id: composerContextChip

                        required property var modelData
                        width: Math.min(176, Math.max(92, composerContextTitle.implicitWidth + 46))
                        height: 28
                        radius: height / 2
                        color: Theme.controlBackground
                        border.color: Theme.border

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 9
                            anchors.rightMargin: 3
                            spacing: 4

                            AppIcon {
                                Layout.preferredWidth: 13
                                Layout.preferredHeight: 13
                                name: composerContextChip.modelData.kind === "image" ? "file" : composerContextChip.modelData.kind === "selection" ? "copy" : "terminal"
                                color: Theme.accent
                            }

                            Text {
                                id: composerContextTitle

                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: composerContextChip.modelData.title
                                elide: Text.ElideMiddle
                                color: Theme.textSoft
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textCompact
                            }

                            ContextToolButton {
                                implicitWidth: 22
                                implicitHeight: 22
                                Accessible.name: qsTr("Remove %1 from context").arg(composerContextChip.modelData.title)
                                onClicked: pane.controller.removeAiContextItem(composerContextChip.modelData.id)
                                contentItem: AppIcon {
                                    name: "close"
                                    color: Theme.textMuted
                                }
                            }
                        }
                    }

                    ScrollBar.horizontal: ScrollBar {
                        policy: composerContextList.contentWidth > composerContextList.width ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                    }
                }

                Flow {
                    id: selectedSkillFlow

                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? childrenRect.height : 0
                    visible: pane.selectedSkillSlugs.length > 0
                    spacing: 5

                    Repeater {
                        model: pane.selectedSkillSlugs

                        delegate: Rectangle {
                            id: selectedSkillChip

                            required property string modelData
                            width: selectedSkillContent.implicitWidth + 14
                            height: 26
                            radius: height / 2
                            color: Theme.selectedBackground
                            border.color: Theme.accent

                            RowLayout {
                                id: selectedSkillContent

                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 3
                                spacing: 3

                                Text {
                                    text: "/" + selectedSkillChip.modelData
                                    color: Theme.accent
                                    font.family: Theme.terminalFont
                                    font.pixelSize: Theme.textCompact
                                    font.weight: Font.DemiBold
                                }

                                ToolButton {
                                    id: removeSkillButton

                                    implicitWidth: 20
                                    implicitHeight: 20
                                    hoverEnabled: true
                                    focusPolicy: Qt.StrongFocus
                                    Accessible.name: qsTr("Remove skill %1").arg(selectedSkillChip.modelData)
                                    onClicked: pane.removeSelectedSkill(selectedSkillChip.modelData)

                                    contentItem: AppIcon {
                                        anchors.centerIn: parent
                                        width: 11
                                        height: 11
                                        name: "close"
                                        color: Theme.textMuted
                                    }

                                    background: Rectangle {
                                        radius: width / 2
                                        color: removeSkillButton.down ? Theme.controlPressed : removeSkillButton.hovered ? Theme.controlHover : "transparent"
                                        border.color: removeSkillButton.activeFocus ? Theme.focus : "transparent"
                                        border.width: removeSkillButton.activeFocus ? 2 : 0
                                    }

                                    HoverHandler {
                                        cursorShape: Qt.PointingHandCursor
                                    }
                                }
                            }
                        }
                    }
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth

                    TextArea {
                        id: promptEditor

                        width: parent.width
                        objectName: "aiPromptEditor"
                        placeholderText: pane.commandRequest ? qsTr("Describe the command you need · Enter sends · Shift+Enter adds a new line") : qsTr("Message ztermy Agent · @ context · / commands")
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
                            text: qsTr("Attach images, text files, selected text, or recent commands")
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

                    ContextToolButton {
                        id: webSearchButton

                        objectName: "aiWebSearchButton"
                        checkable: true
                        checked: pane.webSearchEnabled && pane.controller.aiWebSearchAvailable
                        enabled: !pane.busy && pane.controller.aiWebSearchAvailable
                        Accessible.name: qsTr("Use web search")
                        onClicked: pane.webSearchEnabled = !pane.webSearchEnabled
                        contentItem: AppIcon {
                            name: "search"
                            color: webSearchButton.checked ? Theme.accent : Theme.textMuted
                        }
                        AppToolTip {
                            text: pane.controller.aiWebSearchAvailable ? webSearchButton.checked ? qsTr("Web search enabled for new prompts") : qsTr("Let the model search the web when useful") : qsTr("Native web search is unavailable for this provider")
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    AppComboBox {
                        id: modelBox

                        objectName: "aiModelBox"
                        visible: pane.controller.aiAgentPreference === "ztermy" && pane.width >= 430
                        Layout.preferredWidth: pane.width < 500 ? 104 : 124
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
                        model: ["read-only", "ask", "auto", "yolo"]
                        displayTextModel: [qsTr("Read-only"), qsTr("Ask"), qsTr("Auto"), qsTr("YOLO")]
                        currentIndex: pane.permissionModeIndex(pane.controller.aiPermissionPreference)
                        accessibleName: qsTr("Agent execution mode")
                        enabled: !pane.busy
                        onActivated: index => pane.controller.setAiPermissionMode(model[index])

                        AppToolTip {
                            text: agentModeBox.currentValue === "read-only" ? qsTr("Read tools only; action and MCP tools are hidden") : agentModeBox.currentValue === "ask" ? qsTr("Ask in the approval card before every side effect") : agentModeBox.currentValue === "auto" ? qsTr("Run ordinary actions automatically; ask for high-risk commands and MCP tools") : qsTr("Run without approval prompts; explicit deny rules and safety boundaries still apply")
                        }
                    }

                    ActionButton {
                        objectName: "aiSendButton"
                        Layout.preferredWidth: pane.busy ? 82 : 36
                        text: pane.busy ? qsTr("Cancel") : ""
                        iconName: pane.busy ? "close" : "play"
                        variant: pane.busy ? "destructive" : "primary"
                        enabled: pane.busy || promptEditor.text.trim().length > 0 || pane.pendingImageCount() > 0
                        accessibleName: pane.busy ? qsTr("Cancel") : qsTr("Send")
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
        id: textAttachmentDialog

        title: qsTr("Attach text files")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Text files (*.txt *.md *.json *.yaml *.yml *.toml *.ini *.cfg *.conf *.log *.csv *.xml *.html *.css *.js *.ts *.py *.sh *.ps1)"), qsTr("All files (*)")]
        onAccepted: {
            const urls = [];
            for (const file of selectedFiles) {
                urls.push(file.toString());
            }
            pane.controller.attachAiTextFiles(urls);
        }
    }

    FileDialog {
        id: imageAttachmentDialog

        title: qsTr("Attach images")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp *.gif)"), qsTr("All files (*)")]
        onAccepted: {
            const urls = [];
            for (const file of selectedFiles) {
                urls.push(file.toString());
            }
            pane.controller.attachAiImageFiles(urls);
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
        AppMenuItem {
            text: qsTr("Local text files…")
            onTriggered: textAttachmentDialog.open()
        }
        AppMenuItem {
            text: qsTr("Images…")
            onTriggered: imageAttachmentDialog.open()
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
