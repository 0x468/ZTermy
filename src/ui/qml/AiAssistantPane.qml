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
    readonly property var toolApproval: controller.activeAiToolApproval
    property bool contextExpanded: false
    property bool activityExpanded: false
    property bool historyExpanded: false
    property bool commandRequest: false

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
        default:
            return qsTr("Ready");
        }
    }

    function sendPrompt() {
        const prompt = promptEditor.text.trim();
        if (prompt.length === 0 || busy) {
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

    color: Theme.panelBackground
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
                    spacing: 0

                    Text {
                        text: qsTr("Terminal assistant")
                        color: Theme.text
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textBody
                        font.weight: Font.DemiBold
                    }

                    Text {
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
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.topMargin: 8
            Layout.bottomMargin: 4
            spacing: 6

            ActionButton {
                text: qsTr("Explain last failure")
                iconName: "activity"
                enabled: !pane.busy
                accessibleName: qsTr("Explain the last failed command")
                onClicked: pane.controller.explainAiLastFailure()
            }

            ActionButton {
                text: qsTr("Attach selection")
                iconName: "copy"
                enabled: !pane.busy
                accessibleName: qsTr("Attach selected terminal text to this request")
                onClicked: pane.controller.attachAiSelection()
            }

            ActionButton {
                objectName: "aiActivityToggle"
                text: qsTr("Activity")
                iconName: "activity"
                checkable: true
                checked: pane.activityExpanded
                accessibleName: pane.activityExpanded ? qsTr("Hide AI activity") : qsTr("Show AI activity")
                onClicked: {
                    pane.activityExpanded = !pane.activityExpanded;
                    if (pane.activityExpanded) {
                        pane.historyExpanded = false;
                    }
                }
            }

            ActionButton {
                objectName: "aiHistoryToggle"
                text: qsTr("History")
                iconName: "history"
                checkable: true
                checked: pane.historyExpanded
                enabled: pane.controller.aiConversationHistoryEnabled
                accessibleName: pane.historyExpanded ? qsTr("Hide AI conversation history") : qsTr("Show AI conversation history")
                onClicked: {
                    pane.historyExpanded = !pane.historyExpanded;
                    if (pane.historyExpanded) {
                        pane.activityExpanded = false;
                        pane.controller.aiConversationHistory.reload();
                    }
                }
            }

            Item {
                Layout.fillWidth: true
            }

            ActionButton {
                text: qsTr("Clear")
                iconName: "trash"
                enabled: !pane.busy && pane.conversation !== null && pane.conversation.count > 0
                accessibleName: qsTr("Clear this AI conversation")
                onClicked: pane.controller.clearAiConversation()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.bottomMargin: 4
            visible: pane.controller.activeAiControlOwner !== "unavailable"
            spacing: 7

            Rectangle {
                Layout.preferredHeight: 24
                Layout.preferredWidth: controlOwnerLabel.implicitWidth + 18
                radius: 12
                color: pane.controller.activeAiControlOwner === "user" ? Theme.selectedBackground : Theme.controlBackground
                border.color: pane.controller.activeAiControlOwner === "user" ? Theme.warning : Theme.success

                Text {
                    id: controlOwnerLabel

                    anchors.centerIn: parent
                    text: pane.controller.activeAiControlOwner === "user" ? qsTr("User control") : qsTr("Agent ready")
                    color: pane.controller.activeAiControlOwner === "user" ? Theme.warning : Theme.successText
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textCompact
                    font.weight: Font.Medium
                }
            }

            Text {
                Layout.fillWidth: true
                text: pane.controller.activeAiControlOwner === "user" ? qsTr("AI terminal writes are paused until you resume them.") : qsTr("Typing in the terminal takes control immediately.")
                color: Theme.textSubtle
                elide: Text.ElideRight
                font.family: Theme.uiFont
                font.pixelSize: Theme.textCompact
            }

            ActionButton {
                objectName: "aiControlHandoffButton"
                text: pane.controller.activeAiControlOwner === "user" ? qsTr("Resume agent") : qsTr("Take control")
                iconName: pane.controller.activeAiControlOwner === "user" ? "play" : "terminal"
                enabled: pane.controller.activeAiControlOwner !== "user" || !pane.busy
                accessibleName: pane.controller.activeAiControlOwner === "user" ? qsTr("Resume AI terminal write control") : qsTr("Take terminal control from AI")
                onClicked: {
                    if (pane.controller.activeAiControlOwner === "user") {
                        pane.controller.resumeAiAgentControl();
                    } else {
                        pane.controller.takeAiControl();
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
            color: pane.toolApproval.highRisk ? Theme.dangerSurface : Theme.raisedBackground
            border.color: pane.toolApproval.highRisk ? Theme.dangerBorder : Theme.accent
            border.width: 1
            Accessible.role: Accessible.Pane
            Accessible.name: pane.toolApproval.kind.indexOf("queue_sftp_") === 0 ? qsTr("AI SFTP transfer approval") : pane.toolApproval.kind === "interrupt_command" ? qsTr("AI interrupt approval") : pane.toolApproval.kind === "write_to_pty" ? qsTr("AI terminal input approval") : pane.toolApproval.kind === "save_runbook" ? qsTr("AI runbook approval") : qsTr("AI command approval")

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
                        name: pane.toolApproval.highRisk ? "warning" : "terminal"
                        color: pane.toolApproval.highRisk ? Theme.dangerText : Theme.accent
                    }

                    Text {
                        Layout.fillWidth: true
                        text: pane.toolApproval.highRisk ? qsTr("High-risk command requires approval") : pane.toolApproval.kind.indexOf("queue_sftp_") === 0 ? qsTr("SFTP transfer requires approval") : pane.toolApproval.kind === "interrupt_command" ? qsTr("Terminal interrupt requires approval") : pane.toolApproval.kind === "write_to_pty" ? qsTr("Terminal input requires approval") : pane.toolApproval.kind === "save_runbook" ? qsTr("Runbook save requires approval") : qsTr("Command requires approval")
                        color: pane.toolApproval.highRisk ? Theme.dangerText : Theme.text
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textBody
                        font.weight: Font.DemiBold
                    }
                }

                TextEdit {
                    Layout.fillWidth: true
                    Layout.maximumHeight: 120
                    text: pane.toolApproval.command || ""
                    color: Theme.text
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    textFormat: TextEdit.PlainText
                    font.family: Theme.terminalFont
                    font.pixelSize: Theme.textBody
                    Accessible.name: pane.toolApproval.kind === "interrupt_command" ? qsTr("Interrupt awaiting approval") : pane.toolApproval.kind === "write_to_pty" ? qsTr("Terminal input awaiting approval") : pane.toolApproval.kind === "save_runbook" ? qsTr("Runbook awaiting approval") : qsTr("Command awaiting approval")

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
                    visible: pane.toolApproval.highRisk && (pane.toolApproval.riskReason || "").length > 0
                    text: pane.toolApproval.riskReason || ""
                    color: Theme.dangerText
                    wrapMode: Text.WordWrap
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textCompact
                }

                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: 7

                    ActionButton {
                        text: qsTr("Deny")
                        iconName: "close"
                        accessibleName: qsTr("Deny the pending AI terminal action")
                        onClicked: pane.controller.denyAiTool()
                    }

                    ActionButton {
                        text: pane.toolApproval.kind.indexOf("queue_sftp_") === 0 ? qsTr("Queue transfer") : pane.toolApproval.kind === "interrupt_command" ? qsTr("Send Ctrl+C") : pane.toolApproval.kind === "write_to_pty" ? qsTr("Send input") : pane.toolApproval.kind === "save_runbook" ? qsTr("Save runbook") : qsTr("Run command")
                        iconName: pane.toolApproval.kind.indexOf("queue_sftp_") === 0 ? "transfer" : pane.toolApproval.kind === "interrupt_command" ? "close" : pane.toolApproval.kind === "write_to_pty" ? "composer" : pane.toolApproval.kind === "save_runbook" ? "save" : "play"
                        variant: pane.toolApproval.highRisk ? "destructive" : "primary"
                        accessibleName: pane.toolApproval.kind.indexOf("queue_sftp_") === 0 ? qsTr("Approve and queue the pending AI SFTP transfer") : pane.toolApproval.kind === "interrupt_command" ? qsTr("Approve the pending soft interrupt") : pane.toolApproval.kind === "write_to_pty" ? qsTr("Approve the pending terminal input") : pane.toolApproval.kind === "save_runbook" ? qsTr("Approve and save the pending AI runbook") : qsTr("Approve and run the pending AI command")
                        onClicked: pane.controller.approveAiTool()
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

        ListView {
            id: conversationList

            objectName: "aiConversationList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.topMargin: 6
            clip: true
            spacing: 8
            model: pane.conversation
            boundsBehavior: Flickable.StopAtBounds
            onCountChanged: Qt.callLater(positionViewAtEnd)

            delegate: Item {
                id: messageItem

                required property int index
                required property string messageRole
                required property string text
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

                        TextEdit {
                            id: messageText

                            Layout.fillWidth: true
                            text: messageItem.text.length > 0 ? messageItem.text : messageItem.state === "streaming" ? qsTr("Thinking…") : ""
                            color: Theme.text
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            textFormat: TextEdit.PlainText
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textBody
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: messageItem.state === "failed" || messageItem.truncated
                            text: messageItem.state === "failed" ? messageItem.error : qsTr("Message was truncated locally.")
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

                        RowLayout {
                            Layout.alignment: Qt.AlignRight
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
                                visible: messageItem.messageRole === "assistant" && messageItem.state === "failed" && messageItem.index === conversationList.count - 1
                                text: qsTr("Retry")
                                iconName: "refresh"
                                accessibleName: qsTr("Retry the failed assistant response")
                                onClicked: pane.controller.retryAiMessage()
                            }

                            ActionButton {
                                visible: messageItem.messageRole === "assistant" && messageItem.text.length > 0
                                text: qsTr("Protected copy")
                                iconName: "copy"
                                accessibleName: qsTr("Copy without Windows clipboard history or cloud sync")
                                onClicked: pane.controller.copyAiText(messageItem.text)
                            }
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                width: Math.min(280, parent.width - 32)
                visible: pane.conversation === null || pane.conversation.count === 0
                text: qsTr("Ask about the active terminal. ztermy sends only the bounded context shown above, after local redaction.")
                color: Theme.textMuted
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                font.family: Theme.uiFont
                font.pixelSize: Theme.textBody
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(116, Math.min(200, promptEditor.contentHeight + 78))
            color: Theme.elevatedBackground
            border.color: Theme.border

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    ActionButton {
                        text: pane.commandRequest ? qsTr("Command request") : qsTr("Ask")
                        iconName: pane.commandRequest ? "terminal" : "ai"
                        variant: pane.commandRequest ? "primary" : "default"
                        checkable: true
                        checked: pane.commandRequest
                        enabled: !pane.busy
                        accessibleName: qsTr("Toggle command generation mode")
                        onClicked: pane.commandRequest = !pane.commandRequest
                    }

                    Text {
                        Layout.fillWidth: true
                        text: pane.commandRequest ? qsTr("The response must contain one explicit shell command.") : qsTr("Ask, explain, or diagnose using the reviewed context.")
                        color: Theme.textMuted
                        elide: Text.ElideRight
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textCompact
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 8

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true

                        TextArea {
                            id: promptEditor

                            objectName: "aiPromptEditor"
                            placeholderText: pane.commandRequest ? qsTr("Describe the command you need. Enter sends · Shift+Enter adds a new line") : qsTr("Ask about this terminal. Enter sends · Shift+Enter adds a new line")
                            color: Theme.text
                            placeholderTextColor: Theme.textMuted
                            selectionColor: Theme.accent
                            selectedTextColor: Theme.accentText
                            wrapMode: TextEdit.Wrap
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textBody
                            Accessible.name: qsTr("AI message")
                            Keys.onPressed: event => {
                                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ShiftModifier) === 0 && !promptEditor.inputMethodComposing) {
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

                    ActionButton {
                        Layout.alignment: Qt.AlignBottom
                        text: pane.busy ? qsTr("Cancel") : qsTr("Send")
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
        id: activityExportDialog

        title: qsTr("Export AI activity metadata")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [qsTr("JSON files (*.json)"), qsTr("All files (*)")]
        onAccepted: pane.controller.exportAiActivity(selectedFile.toString())
    }
}
