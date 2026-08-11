pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: pane

    objectName: "aiAssistantPane"
    required property var controller
    required property var activeTab
    readonly property bool busy: controller.activeAiState === "starting" || controller.activeAiState === "retrying" || controller.activeAiState === "streaming"
    readonly property var conversation: controller.activeAiConversation
    property bool contextExpanded: false

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
        if (controller.sendAiMessage(prompt)) {
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

                required property string messageRole
                required property string text
                required property string state
                required property string error
                required property bool truncated
                required property var inputTokens
                required property var outputTokens
                width: ListView.view.width
                height: messageBubble.implicitHeight

                Rectangle {
                    id: messageBubble

                    anchors.right: messageItem.messageRole === "user" ? parent.right : undefined
                    anchors.left: messageItem.messageRole === "user" ? undefined : parent.left
                    width: Math.min(parent.width * 0.92, Math.max(150, messageText.implicitWidth + 24))
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
                            Layout.alignment: Qt.AlignRight
                            visible: Number(messageItem.inputTokens) + Number(messageItem.outputTokens) > 0
                            text: qsTr("%1 in · %2 out").arg(messageItem.inputTokens).arg(messageItem.outputTokens)
                            color: Theme.textSubtle
                            font.family: Theme.terminalFont
                            font.pixelSize: Theme.textCompact
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
            Layout.preferredHeight: Math.max(86, Math.min(170, promptEditor.contentHeight + 48))
            color: Theme.elevatedBackground
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    TextArea {
                        id: promptEditor

                        objectName: "aiPromptEditor"
                        placeholderText: qsTr("Ask about this terminal. Enter sends · Shift+Enter adds a new line")
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
