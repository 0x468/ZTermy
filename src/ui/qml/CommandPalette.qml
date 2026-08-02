pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: palette

    required property var controller
    property real reveal: 0.0
    property var focusRestoreItem: null
    readonly property var filteredActions: filterActions(controller.actions, searchField.text)
    readonly property string commandPaletteShortcut: shortcutFor("application.commandPalette")

    visible: false
    objectName: "commandPalette"
    opacity: reveal
    z: 500
    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Command palette")

    function filterActions(source, query) {
        const needle = query.trim().toLocaleLowerCase();
        const result = [];
        for (let index = 0; index < source.length; ++index) {
            const action = source[index];
            if (!action.paletteVisible) {
                continue;
            }
            const haystack = (action.label + " " + action.description + " " + action.categoryLabel + " " + action.id + " " + action.shortcut).toLocaleLowerCase();
            if (needle.length === 0 || haystack.indexOf(needle) >= 0) {
                result.push(action);
            }
        }
        return result;
    }

    function shortcutFor(actionId) {
        for (let index = 0; index < controller.actions.length; ++index) {
            if (controller.actions[index].id === actionId) {
                return controller.actions[index].shortcut;
            }
        }
        return "";
    }

    function firstEnabledIndex(start, direction) {
        if (filteredActions.length === 0) {
            return -1;
        }
        let index = Math.max(0, Math.min(start, filteredActions.length - 1));
        for (let count = 0; count < filteredActions.length; ++count) {
            if (filteredActions[index].enabled) {
                return index;
            }
            index = (index + direction + filteredActions.length) % filteredActions.length;
        }
        return -1;
    }

    function moveSelection(direction) {
        const start = actionList.currentIndex < 0 ? (direction > 0 ? 0 : filteredActions.length - 1) : actionList.currentIndex + direction;
        actionList.currentIndex = firstEnabledIndex((start + filteredActions.length) % Math.max(1, filteredActions.length), direction);
        if (actionList.currentIndex >= 0) {
            actionList.positionViewAtIndex(actionList.currentIndex, ListView.Contain);
        }
    }

    function executeCurrent() {
        if (actionList.currentIndex < 0 || actionList.currentIndex >= filteredActions.length) {
            return;
        }
        execute(filteredActions[actionList.currentIndex].id);
    }

    function execute(actionId) {
        if (controller.triggerAction(actionId)) {
            close();
        }
    }

    function open() {
        if (!visible && palette.Window.window) {
            focusRestoreItem = palette.Window.window.activeFocusItem;
        }
        visible = true;
        searchField.text = "";
        reveal = Theme.animationsEnabled ? 0.0 : 1.0;
        actionList.currentIndex = firstEnabledIndex(0, 1);
        if (Theme.animationsEnabled) {
            revealAnimation.restart();
        }
        Qt.callLater(searchField.forceActiveFocus);
    }

    function close() {
        const restore = focusRestoreItem;
        focusRestoreItem = null;
        revealAnimation.stop();
        reveal = 0.0;
        visible = false;
        if (restore) {
            Qt.callLater(restore.forceActiveFocus);
        }
    }

    onFilteredActionsChanged: actionList.currentIndex = firstEnabledIndex(0, 1)

    NumberAnimation {
        id: revealAnimation
        target: palette
        property: "reveal"
        from: 0.0
        to: 1.0
        duration: Theme.motionFast
        easing.type: Easing.OutCubic
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.dark ? "#73000000" : "#520D1726"

        TapHandler {
            onTapped: palette.close()
        }
    }

    Rectangle {
        id: panel

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Math.max(54, Math.min(104, palette.height * 0.12))
        width: Math.min(720, Math.max(360, palette.width - 32))
        height: Math.min(520, Math.max(176, actionList.contentHeight + 62))
        radius: Theme.radiusPanel
        color: Theme.elevatedBackground
        border.color: Theme.borderStrong
        scale: 0.985 + (palette.reveal * 0.015)
        clip: true

        TapHandler {
            onTapped: eventPoint => eventPoint.accepted = true
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 58
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                spacing: 12

                AppIcon {
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    name: "search"
                    color: Theme.textMuted
                }

                AppTextField {
                    id: searchField

                    objectName: "commandPaletteSearch"
                    Layout.fillWidth: true
                    background: Item {}
                    placeholderText: qsTr("Type an action or shortcut")
                    accessibleName: qsTr("Search commands")
                    font.pixelSize: 15
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Escape) {
                            palette.close();
                            event.accepted = true;
                        } else if (event.key === Qt.Key_Down) {
                            palette.moveSelection(1);
                            event.accepted = true;
                        } else if (event.key === Qt.Key_Up) {
                            palette.moveSelection(-1);
                            event.accepted = true;
                        } else if (event.key === Qt.Key_Home) {
                            actionList.currentIndex = palette.firstEnabledIndex(0, 1);
                            event.accepted = true;
                        } else if (event.key === Qt.Key_End) {
                            actionList.currentIndex = palette.firstEnabledIndex(palette.filteredActions.length - 1, -1);
                            event.accepted = true;
                        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            palette.executeCurrent();
                            event.accepted = true;
                        }
                    }
                }

                Rectangle {
                    implicitWidth: shortcutHint.implicitWidth + 14
                    implicitHeight: 24
                    radius: Theme.radiusSmall
                    color: Theme.controlBackground
                    border.color: Theme.border

                    Text {
                        id: shortcutHint
                        anchors.centerIn: parent
                        text: palette.commandPaletteShortcut
                        color: Theme.textMuted
                        font.family: Theme.terminalFont
                        font.pixelSize: Theme.textCompact
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.border
            }

            ListView {
                id: actionList

                objectName: "commandPaletteList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: palette.filteredActions
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Available commands")

                delegate: Rectangle {
                    id: actionRow

                    required property var modelData
                    required property int index

                    width: actionList.width
                    height: 58
                    color: index === actionList.currentIndex ? Theme.controlHover : "transparent"
                    opacity: modelData.enabled ? 1.0 : 0.48
                    Accessible.role: Accessible.ListItem
                    Accessible.name: modelData.shortcut.length > 0 ? modelData.label + ", " + modelData.shortcut : modelData.label
                    Accessible.description: modelData.categoryLabel + ". " + modelData.description
                    Accessible.selected: index === actionList.currentIndex

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 18
                        anchors.rightMargin: 18
                        spacing: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                Layout.fillWidth: true
                                text: actionRow.modelData.label
                                color: Theme.text
                                elide: Text.ElideRight
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textBody
                                font.weight: Font.Medium
                            }

                            Text {
                                Layout.fillWidth: true
                                text: actionRow.modelData.categoryLabel + " · " + actionRow.modelData.description
                                color: Theme.textMuted
                                elide: Text.ElideRight
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textCompact
                            }
                        }

                        Rectangle {
                            visible: actionRow.modelData.shortcut.length > 0
                            implicitWidth: actionShortcut.implicitWidth + 14
                            implicitHeight: 24
                            radius: Theme.radiusSmall
                            color: Theme.controlBackground
                            border.color: Theme.border

                            Text {
                                id: actionShortcut
                                anchors.centerIn: parent
                                text: actionRow.modelData.shortcut
                                color: Theme.textSoft
                                font.family: Theme.terminalFont
                                font.pixelSize: Theme.textCompact
                            }
                        }
                    }

                    HoverHandler {
                        id: actionHover
                        enabled: actionRow.modelData.enabled
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onHoveredChanged: {
                            if (hovered && actionRow.modelData.enabled) {
                                actionList.currentIndex = actionRow.index;
                            }
                        }
                    }

                    TapHandler {
                        enabled: actionRow.modelData.enabled
                        onTapped: palette.execute(actionRow.modelData.id)
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: palette.filteredActions.length === 0
                    text: qsTr("No matching actions")
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textBody
                }
            }
        }
    }
}
