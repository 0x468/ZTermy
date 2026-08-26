pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

ComboBox {
    id: control

    property string accessibleName: ""
    property var displayTextModel: []
    property var toolTipModel: []
    property string toolTipText: ""
    readonly property string effectiveDisplayText: textForIndex(currentIndex, currentText)

    function textForIndex(index, fallback) {
        return index >= 0 && index < displayTextModel.length ? displayTextModel[index] : fallback;
    }

    function toolTipForIndex(index, fallback, truncated) {
        if (index >= 0 && index < toolTipModel.length && String(toolTipModel[index]).length > 0)
            return String(toolTipModel[index]);
        return truncated ? String(fallback) : "";
    }

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    leftPadding: 12
    rightPadding: 36
    topPadding: 7
    bottomPadding: 7
    implicitHeight: 34
    Accessible.name: accessibleName

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Down && (event.modifiers & Qt.AltModifier) !== 0) {
            control.popup.open();
            event.accepted = true;
        }
    }

    contentItem: Text {
        id: selectedText

        text: control.effectiveDisplayText
        color: control.enabled ? Theme.text : Theme.textSubtle
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        font.family: Theme.uiFont
        font.pixelSize: Theme.textBody
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
        hoverEnabled: true
        text: control.textForIndex(index, modelData)
        highlighted: control.highlightedIndex === index

        contentItem: Text {
            id: optionText

            text: option.text
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

        AppToolTip {
            hoverTarget: option
            text: control.toolTipForIndex(option.index, option.text, optionText.truncated)
            visible: text.length > 0 && option.hovered
        }
    }

    background: Rectangle {
        implicitWidth: 120
        implicitHeight: 34
        radius: Theme.radiusControl
        color: control.enabled ? Theme.fieldBackground : Theme.controlDisabled
        border.color: control.visualFocus ? Theme.focus : control.hovered ? Theme.borderStrong : Theme.border
        border.width: control.visualFocus ? 2 : 1

        Behavior on color {
            ColorAnimation {
                duration: Theme.motionFast
            }
        }
    }

    popup: Popup {
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 2, 240)
        padding: 1

        enter: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.motionMedium
                easing.type: Easing.OutCubic
            }
        }

        exit: Transition {
            NumberAnimation {
                property: "opacity"
                from: 1
                to: 0
                duration: Theme.motionFast
                easing.type: Easing.InCubic
            }
        }

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

            transform: Translate {
                y: control.popup.visible ? 0 : -Theme.motionDistanceSmall

                Behavior on y {
                    NumberAnimation {
                        duration: Theme.motionMedium
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }
    }

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    AppToolTip {
        hoverTarget: control
        text: control.toolTipText.length > 0 ? control.toolTipText : selectedText.truncated ? control.effectiveDisplayText : ""
        visible: text.length > 0 && control.hovered && !control.popup.visible
    }
}
