pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

ComboBox {
    id: control

    property string accessibleName: ""
    property string placeholderText: ""
    property string text: ""
    property bool completing: false
    property bool suppressNextCompletion: false

    editable: true
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    leftPadding: 12
    rightPadding: 36
    topPadding: 7
    bottomPadding: 7
    implicitHeight: 34
    currentIndex: -1
    selectTextByMouse: true
    Accessible.name: accessibleName.length > 0 ? accessibleName : placeholderText

    function completeFromPrefix() {
        if (completing || editText.length === 0) {
            return;
        }
        const prefix = editText;
        for (let index = 0; index < count; ++index) {
            const candidate = textAt(index);
            if (candidate.length > prefix.length && candidate.toLocaleLowerCase().indexOf(prefix.toLocaleLowerCase()) === 0) {
                completing = true;
                editText = candidate;
                input.select(prefix.length, candidate.length);
                completing = false;
                return;
            }
        }
    }

    onEditTextChanged: {
        if (text !== editText) {
            text = editText;
        }
    }
    onTextChanged: {
        if (editText !== text) {
            editText = text;
        }
    }
    onActivated: index => {
        editText = textAt(index);
        currentIndex = -1;
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Down && ((event.modifiers & Qt.AltModifier) !== 0 || !popup.visible)) {
            popup.open();
            event.accepted = true;
        } else if (event.key === Qt.Key_Escape && popup.visible) {
            popup.close();
            event.accepted = true;
        }
    }

    contentItem: TextField {
        id: input

        leftPadding: 0
        rightPadding: 0
        topPadding: 0
        bottomPadding: 0
        text: control.editText
        placeholderText: control.placeholderText
        placeholderTextColor: Theme.textMuted
        color: Theme.text
        selectionColor: Theme.accent
        selectedTextColor: Theme.accentText
        selectByMouse: true
        font.family: Theme.uiFont
        font.pixelSize: Theme.textBody
        background: null
        Accessible.name: control.accessibleName.length > 0 ? control.accessibleName : control.placeholderText
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
                control.suppressNextCompletion = true;
                Qt.callLater(() => control.suppressNextCompletion = false);
            }
            event.accepted = false;
        }
        onTextEdited: {
            control.editText = text;
            if (control.suppressNextCompletion) {
                control.suppressNextCompletion = false;
            } else {
                Qt.callLater(control.completeFromPrefix);
            }
        }
    }

    indicator: AppIcon {
        x: control.width - width - 12
        y: (control.height - height) / 2
        width: 16
        height: 16
        name: "chevron-down"
        color: control.enabled ? Theme.textSoft : Theme.textSubtle
        rotation: control.popup.visible ? 180 : 0

        Behavior on rotation {
            NumberAnimation {
                duration: Theme.motionFast
            }
        }
    }

    delegate: ItemDelegate {
        id: option

        required property int index
        required property var modelData
        width: control.width
        implicitHeight: 34
        highlighted: control.highlightedIndex === index

        contentItem: Text {
            text: option.modelData
            color: Theme.text
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            font.family: Theme.uiFont
            font.pixelSize: Theme.textBody
        }
        background: Rectangle {
            radius: Theme.radiusSmall
            color: option.highlighted ? Theme.selectedHover : Theme.floatingBackground
        }
    }

    background: Rectangle {
        implicitWidth: 120
        implicitHeight: 34
        radius: Theme.radiusControl
        color: control.enabled ? Theme.fieldBackground : Theme.controlDisabled
        border.color: control.activeFocus ? Theme.focus : control.hovered ? Theme.borderStrong : Theme.border
        border.width: control.activeFocus ? 2 : 1
    }

    popup: Popup {
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 2, 240)
        padding: 1

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle {
            radius: Theme.radiusControl
            color: Theme.floatingBackground
            border.color: Theme.borderStrong
        }
    }
}
