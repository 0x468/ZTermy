pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    required property var controller
    required property Item overlayParent
    required property bool compactLayout
    property bool editorExpanded: false
    property string editingRuleId: ""
    property string pendingDeleteId: ""
    readonly property real editorWidth: ruleEditor.width
    readonly property real editorReveal: ruleEditor.reveal
    readonly property int cardColumns: width < 720 ? 1 : (width < 1080 ? 2 : 3)

    signal editorOpening

    Layout.fillWidth: true
    spacing: 8

    function profileIndex(profileId) {
        for (let index = 0; index < controller.hostProfiles.length; ++index) {
            if (controller.hostProfiles[index].id === profileId) {
                return index;
            }
        }
        return controller.hostProfiles.length > 0 ? 0 : -1;
    }

    function profileNames() {
        const names = [];
        for (const profile of controller.hostProfiles) {
            names.push(profile.name + "  ·  " + profile.username + "@" + profile.host);
        }
        return names;
    }

    function beginNew() {
        editorOpening();
        editingRuleId = "";
        labelField.text = "";
        profileBox.currentIndex = controller.hostProfiles.length > 0 ? 0 : -1;
        typeBox.currentIndex = 0;
        bindHostField.text = "127.0.0.1";
        bindPortField.text = "";
        destinationHostField.text = "127.0.0.1";
        destinationPortField.text = "";
        autoStartSwitch.checked = false;
        editorExpanded = true;
        labelField.forceActiveFocus();
    }

    function beginEdit(rule) {
        editorOpening();
        editingRuleId = rule.id;
        labelField.text = rule.label;
        profileBox.currentIndex = profileIndex(rule.profileId);
        typeBox.currentIndex = rule.type === "remote" ? 1 : (rule.type === "dynamic" ? 2 : 0);
        bindHostField.text = rule.bindHost;
        bindPortField.text = String(rule.bindPort);
        destinationHostField.text = rule.destinationHost;
        destinationPortField.text = rule.destinationPort > 0 ? String(rule.destinationPort) : "";
        autoStartSwitch.checked = rule.autoStart;
        editorExpanded = true;
        labelField.forceActiveFocus();
    }

    function closeEditor() {
        editorExpanded = false;
    }

    function typeToken() {
        return typeBox.currentIndex === 1 ? "remote" : (typeBox.currentIndex === 2 ? "dynamic" : "local");
    }

    function saveRule() {
        if (profileBox.currentIndex < 0 || profileBox.currentIndex >= controller.hostProfiles.length) {
            return;
        }
        const saved = controller.savePortForwardingRule(editingRuleId, labelField.text, controller.hostProfiles[profileBox.currentIndex].id, typeToken(), bindHostField.text, Number(bindPortField.text), destinationHostField.text, Number(destinationPortField.text), autoStartSwitch.checked);
        if (saved) {
            closeEditor();
        }
    }

    function endpointSummary(rule) {
        const bind = rule.bindHost + ":" + rule.bindPort;
        if (rule.type === "dynamic") {
            return qsTr("SOCKS5 on %1").arg(bind);
        }
        const destination = rule.destinationHost + ":" + rule.destinationPort;
        return rule.type === "remote" ? qsTr("Remote %1  →  local %2").arg(bind).arg(destination) : qsTr("Local %1  →  remote %2").arg(bind).arg(destination);
    }

    function stateLabel(state) {
        if (state === "running") {
            return qsTr("Running");
        }
        if (state === "starting") {
            return qsTr("Starting…");
        }
        if (state === "waiting") {
            return qsTr("Waiting for vault unlock");
        }
        if (state === "failed") {
            return qsTr("Failed");
        }
        return qsTr("Stopped");
    }

    function formatTraffic(rule) {
        if (rule.state !== "running" || (rule.bytesFromClients === 0 && rule.bytesToClients === 0)) {
            return qsTr("%1 clients").arg(rule.activeClients);
        }
        return qsTr("%1 clients · ↑ %2 · ↓ %3").arg(rule.activeClients).arg(formatBytes(rule.bytesFromClients)).arg(formatBytes(rule.bytesToClients));
    }

    function formatBytes(value) {
        if (value < 1024) {
            return value + " B";
        }
        if (value < 1024 * 1024) {
            return (value / 1024).toFixed(1) + " KiB";
        }
        return (value / (1024 * 1024)).toFixed(1) + " MiB";
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        AppIcon {
            Layout.preferredWidth: 15
            Layout.preferredHeight: 15
            name: "network"
            color: Theme.textMuted
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("PORT FORWARDING")
            color: Theme.textMuted
            font.family: Theme.uiFont
            font.pixelSize: 11
            font.weight: Font.DemiBold
            font.letterSpacing: 0.6
        }

        Text {
            text: root.controller.portForwardingRules.length
            color: Theme.textMuted
            font.family: Theme.uiFont
            font.pixelSize: Theme.textLabel
        }

        ActionButton {
            text: qsTr("New rule")
            iconName: "plus"
            enabled: root.controller.hostProfiles.length > 0
            accessibleName: qsTr("Create a port forwarding rule")
            onClicked: root.beginNew()
        }
    }

    StatusMessage {
        Layout.fillWidth: true
        text: root.controller.portForwardingOperationError
        kind: "error"
    }

    StatePanel {
        Layout.fillWidth: true
        visible: root.controller.portForwardingRules.length === 0
        heading: root.controller.hostProfiles.length === 0 ? qsTr("Save a host before creating a forwarding rule") : qsTr("No port forwarding rules")
        description: root.controller.hostProfiles.length === 0 ? qsTr("Forwarding rules reuse a saved host's authentication, proxy, and jump-host route.") : qsTr("Create local, remote, or dynamic SOCKS5 forwarding without opening a terminal tab.")
        centered: true
    }

    Flow {
        id: ruleFlow

        readonly property real cardWidth: Math.max(0, (width - (spacing * (root.cardColumns - 1))) / root.cardColumns)

        Layout.fillWidth: true
        Layout.preferredHeight: childrenRect.height
        spacing: Theme.spacingRelated

        Repeater {
            model: root.controller.portForwardingRules

            delegate: Rectangle {
                id: ruleCard

                required property var modelData
                readonly property bool active: modelData.state === "running" || modelData.state === "starting"

                width: ruleFlow.cardWidth
                height: 82
                radius: Theme.radiusControl
                color: cardHover.hovered ? Theme.controlHover : Theme.raisedBackground
                border.color: ruleCard.activeFocus ? Theme.focus : (modelData.state === "failed" ? Theme.danger : Theme.border)
                activeFocusOnTab: true
                Accessible.name: modelData.label + ", " + root.stateLabel(modelData.state)

                Behavior on color {
                    ColorAnimation {
                        duration: Theme.motionFast
                    }
                }

                HoverHandler {
                    id: cardHover
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 6
                    spacing: 9

                    Rectangle {
                        Layout.preferredWidth: 38
                        Layout.preferredHeight: 38
                        radius: Theme.radiusControl
                        color: Theme.selectedBackground

                        AppIcon {
                            anchors.centerIn: parent
                            width: 18
                            height: 18
                            name: "network"
                            color: ruleCard.modelData.state === "failed" ? Theme.danger : Theme.accent
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Text {
                                Layout.fillWidth: true
                                text: ruleCard.modelData.label
                                color: Theme.text
                                elide: Text.ElideRight
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textBody
                                font.weight: Font.DemiBold
                            }

                            Rectangle {
                                Layout.preferredWidth: 7
                                Layout.preferredHeight: 7
                                radius: width / 2
                                color: ruleCard.modelData.state === "running" ? Theme.success : ruleCard.modelData.state === "starting" || ruleCard.modelData.state === "waiting" ? Theme.warning : ruleCard.modelData.state === "failed" ? Theme.danger : Theme.textSubtle
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: root.endpointSummary(ruleCard.modelData)
                            color: Theme.textMuted
                            elide: Text.ElideMiddle
                            font.family: Theme.terminalFont
                            font.pixelSize: Theme.textCompact
                        }

                        Text {
                            Layout.fillWidth: true
                            text: root.stateLabel(ruleCard.modelData.state) + " · " + root.formatTraffic(ruleCard.modelData)
                            color: ruleCard.modelData.state === "failed" ? Theme.danger : Theme.textSubtle
                            elide: Text.ElideRight
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textCompact
                        }
                    }

                    ToolButton {
                        id: runButton

                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        Accessible.name: ruleCard.active ? qsTr("Stop %1").arg(ruleCard.modelData.label) : qsTr("Start %1").arg(ruleCard.modelData.label)
                        onClicked: ruleCard.active ? root.controller.stopPortForwardingRule(ruleCard.modelData.id) : root.controller.startPortForwardingRule(ruleCard.modelData.id)
                        contentItem: AppIcon {
                            name: ruleCard.active ? "close" : "play"
                            color: runButton.hovered || runButton.visualFocus ? Theme.accent : Theme.textMuted
                        }
                        background: Rectangle {
                            radius: width / 2
                            color: runButton.down ? Theme.controlPressed : runButton.hovered ? Theme.controlHover : "transparent"
                            border.color: runButton.visualFocus ? Theme.focus : "transparent"
                        }
                        AppToolTip {
                            text: ruleCard.active ? qsTr("Stop forwarding") : qsTr("Start forwarding")
                        }
                    }

                    ToolButton {
                        id: editButton

                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        Accessible.name: qsTr("Edit %1").arg(ruleCard.modelData.label)
                        onClicked: root.beginEdit(ruleCard.modelData)
                        contentItem: AppIcon {
                            name: "edit"
                            color: editButton.hovered || editButton.visualFocus ? Theme.text : Theme.textMuted
                        }
                        background: Rectangle {
                            radius: width / 2
                            color: editButton.down ? Theme.controlPressed : editButton.hovered ? Theme.controlHover : "transparent"
                            border.color: editButton.visualFocus ? Theme.focus : "transparent"
                        }
                        AppToolTip {
                            text: qsTr("Edit forwarding rule")
                        }
                    }

                    ToolButton {
                        id: ruleMoreButton

                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        Accessible.name: qsTr("More actions for %1").arg(ruleCard.modelData.label)
                        onClicked: ruleMoreMenu.open()
                        contentItem: AppIcon {
                            name: "more"
                            color: ruleMoreButton.hovered || ruleMoreButton.visualFocus || ruleMoreMenu.visible ? Theme.text : Theme.textMuted
                        }
                        background: Rectangle {
                            radius: width / 2
                            color: ruleMoreButton.down ? Theme.controlPressed : ruleMoreButton.hovered ? Theme.controlHover : "transparent"
                            border.color: ruleMoreButton.visualFocus ? Theme.focus : "transparent"
                        }
                        AppToolTip {
                            text: qsTr("More forwarding actions")
                        }
                    }
                }

                AppMenu {
                    id: ruleMoreMenu

                    x: Math.max(0, ruleCard.width - width)
                    y: ruleMoreButton.y + ruleMoreButton.height

                    AppMenuItem {
                        text: qsTr("Copy bind endpoint")
                        onTriggered: root.controller.copyPortForwardingBindEndpoint(ruleCard.modelData.id)
                    }

                    AppMenuItem {
                        text: qsTr("Duplicate rule")
                        onTriggered: root.controller.duplicatePortForwardingRule(ruleCard.modelData.id)
                    }
                }
            }
        }
    }

    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 0

        Rectangle {
            id: editorDismissRegion

            parent: root.overlayParent
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            width: Math.max(0, parent.width - ruleEditor.width)
            z: 19
            visible: root.editorExpanded && width > 0
            color: "transparent"

            TapHandler {
                onTapped: root.closeEditor()
            }
        }

        Rectangle {
            id: ruleEditor

            property real reveal: root.editorExpanded ? 1.0 : 0.0
            readonly property real targetWidth: root.compactLayout ? root.overlayParent.width : Math.min(460, Math.max(400, root.overlayParent.width * 0.38))

            parent: root.overlayParent
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: targetWidth * reveal
            z: 20
            visible: reveal > 0.001
            enabled: root.editorExpanded
            opacity: reveal
            clip: true
            color: Theme.raisedBackground
            border.color: Theme.border

            Behavior on reveal {
                NumberAnimation {
                    duration: Theme.motionMedium
                    easing.type: Easing.OutCubic
                }
            }

            ScrollView {
                anchors.fill: parent
                contentWidth: availableWidth
                contentHeight: editorColumn.implicitHeight + 40

                ColumnLayout {
                    id: editorColumn

                    x: 18
                    y: 16
                    width: Math.max(0, parent.width - 36)
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                text: root.editingRuleId.length > 0 ? qsTr("Edit port forwarding") : qsTr("New port forwarding")
                                color: Theme.text
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textTitle
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: qsTr("Uses the selected host's SSH route and saved credential.")
                                color: Theme.textMuted
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textCompact
                            }
                        }

                        ToolButton {
                            id: closeEditorButton
                            implicitWidth: 30
                            implicitHeight: 30
                            hoverEnabled: true
                            Accessible.name: qsTr("Close forwarding editor")
                            onClicked: root.closeEditor()
                            contentItem: AppIcon {
                                name: "close"
                                color: closeEditorButton.hovered ? Theme.text : Theme.textMuted
                            }
                            background: Rectangle {
                                radius: width / 2
                                color: closeEditorButton.hovered ? Theme.controlHover : "transparent"
                            }
                        }
                    }

                    Text {
                        text: qsTr("Rule name")
                        color: Theme.textMuted
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }
                    AppTextField {
                        id: labelField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Database tunnel")
                        accessibleName: qsTr("Forwarding rule name")
                        selectByMouse: true
                    }

                    Text {
                        text: qsTr("SSH host")
                        color: Theme.textMuted
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }
                    AppComboBox {
                        id: profileBox
                        Layout.fillWidth: true
                        model: root.controller.hostProfiles
                        displayTextModel: root.profileNames()
                        accessibleName: qsTr("SSH host profile")
                    }

                    Text {
                        text: qsTr("Forwarding type")
                        color: Theme.textMuted
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                    }
                    AppComboBox {
                        id: typeBox
                        Layout.fillWidth: true
                        model: ["local", "remote", "dynamic"]
                        displayTextModel: [qsTr("Local"), qsTr("Remote"), qsTr("Dynamic SOCKS5")]
                        accessibleName: qsTr("Forwarding type")
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 5

                        Text {
                            text: typeBox.currentIndex === 1 ? qsTr("Remote bind address") : qsTr("Local bind address")
                            color: Theme.textMuted
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textLabel
                        }
                        Text {
                            text: qsTr("Port")
                            color: Theme.textMuted
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textLabel
                        }
                        AppTextField {
                            id: bindHostField
                            Layout.fillWidth: true
                            placeholderText: "127.0.0.1"
                            accessibleName: qsTr("Bind address")
                            selectByMouse: true
                        }
                        AppTextField {
                            id: bindPortField
                            Layout.preferredWidth: 110
                            placeholderText: "8080"
                            accessibleName: qsTr("Bind port")
                            inputMethodHints: Qt.ImhDigitsOnly
                            validator: IntValidator {
                                bottom: 1
                                top: 65535
                            }
                            selectByMouse: true
                        }
                    }

                    StatusMessage {
                        Layout.fillWidth: true
                        visible: bindHostField.text !== "127.0.0.1" && bindHostField.text !== "::1"
                        text: typeBox.currentIndex === 1 ? qsTr("The SSH server may expose this remote port beyond localhost, depending on its GatewayPorts policy.") : qsTr("This bind address may expose the forwarded port to other devices.")
                        kind: "warning"
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        visible: typeBox.currentIndex !== 2
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 5

                        Text {
                            text: typeBox.currentIndex === 1 ? qsTr("Local destination") : qsTr("Remote destination")
                            color: Theme.textMuted
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textLabel
                        }
                        Text {
                            text: qsTr("Port")
                            color: Theme.textMuted
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textLabel
                        }
                        AppTextField {
                            id: destinationHostField
                            Layout.fillWidth: true
                            placeholderText: "127.0.0.1"
                            accessibleName: qsTr("Destination host")
                            selectByMouse: true
                        }
                        AppTextField {
                            id: destinationPortField
                            Layout.preferredWidth: 110
                            placeholderText: "22"
                            accessibleName: qsTr("Destination port")
                            inputMethodHints: Qt.ImhDigitsOnly
                            validator: IntValidator {
                                bottom: 1
                                top: 65535
                            }
                            selectByMouse: true
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        AppSwitch {
                            id: autoStartSwitch
                            accessibleName: qsTr("Start this forwarding rule when ztermy opens")
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Start automatically")
                            color: Theme.text
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textBody
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 4
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        ActionButton {
                            visible: root.editingRuleId.length > 0
                            text: qsTr("Delete")
                            iconName: "trash"
                            variant: "destructive"
                            accessibleName: qsTr("Delete forwarding rule")
                            onClicked: {
                                root.pendingDeleteId = root.editingRuleId;
                                deleteDialog.openFrom(this);
                            }
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        ActionButton {
                            text: qsTr("Cancel")
                            onClicked: root.closeEditor()
                        }
                        ActionButton {
                            text: qsTr("Save")
                            variant: "primary"
                            enabled: labelField.text.trim().length > 0 && profileBox.currentIndex >= 0 && bindHostField.text.trim().length > 0 && bindPortField.acceptableInput && (typeBox.currentIndex === 2 || (destinationHostField.text.trim().length > 0 && destinationPortField.acceptableInput))
                            onClicked: root.saveRule()
                        }
                    }
                }
            }
        }

        ConfirmationDialog {
            id: deleteDialog
            parent: root.overlayParent
            heading: qsTr("Delete forwarding rule?")
            description: qsTr("The saved rule will be removed. Any active forwarding session is stopped first.")
            acceptText: qsTr("Delete")
            destructive: true
            onAccepted: {
                if (root.controller.deletePortForwardingRule(root.pendingDeleteId)) {
                    root.closeEditor();
                }
                root.pendingDeleteId = "";
            }
        }
    }
}
