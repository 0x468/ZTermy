pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: control

    property var families: []
    property string family: ""
    property string accessibleName: ""
    property string searchObjectName: "fontPickerSearch"
    property string systemFamily: ""
    property bool showFontPreview: true
    signal familyActivated(string family)

    function familyLabel(value) {
        return value.length === 0 ? qsTr("System default (%1)").arg(systemFamily) : value;
    }

    function filteredFamilies(query) {
        const normalized = query.trim().toLocaleLowerCase();
        const result = [];
        for (let index = 0; index < families.length; ++index) {
            const candidate = families[index];
            if (normalized.length === 0 || familyLabel(candidate).toLocaleLowerCase().includes(normalized)) {
                result.push(candidate);
            }
        }
        return result;
    }

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    leftPadding: 12
    rightPadding: 36
    topPadding: 7
    bottomPadding: 7
    implicitHeight: 34
    Accessible.name: accessibleName
    onClicked: fontPopup.open()

    contentItem: Text {
        text: control.familyLabel(control.family)
        color: control.enabled ? Theme.text : Theme.textSubtle
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        font.family: control.showFontPreview && control.family.length > 0 ? control.family : Theme.uiFont
        font.pixelSize: Theme.textBody
    }

    indicator: AppIcon {
        x: control.width - width - 12
        y: (control.height - height) / 2
        width: 16
        height: 16
        name: "chevron-down"
        color: control.enabled ? Theme.textSoft : Theme.textSubtle
        rotation: fontPopup.visible ? 180 : 0

        Behavior on rotation {
            NumberAnimation {
                duration: Theme.motionFast
            }
        }
    }

    background: Rectangle {
        implicitWidth: 180
        implicitHeight: 34
        radius: Theme.radiusControl
        color: control.enabled ? Theme.fieldBackground : Theme.controlDisabled
        border.color: control.visualFocus ? Theme.focus : control.hovered ? Theme.borderStrong : Theme.border
        border.width: control.visualFocus ? 2 : 1
    }

    Popup {
        id: fontPopup

        y: control.height + 4
        width: control.width
        height: Math.min(320, searchLayout.implicitHeight + 2)
        padding: 1
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: {
            fontSearch.text = "";
            fontList.currentIndex = Math.max(0, fontList.model.indexOf(control.family));
            Qt.callLater(() => fontSearch.forceActiveFocus());
        }
        onClosed: control.forceActiveFocus(Qt.PopupFocusReason)

        contentItem: ColumnLayout {
            id: searchLayout

            spacing: 4

            AppTextField {
                id: fontSearch

                objectName: control.searchObjectName
                Layout.fillWidth: true
                Layout.margins: 6
                Layout.bottomMargin: 2
                compact: true
                placeholderText: qsTr("Search installed fonts")
                accessibleName: qsTr("Search installed fonts")

                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Down) {
                        fontList.currentIndex = Math.min(fontList.count - 1, fontList.currentIndex + 1);
                        event.accepted = true;
                    } else if (event.key === Qt.Key_Up) {
                        fontList.currentIndex = Math.max(0, fontList.currentIndex - 1);
                        event.accepted = true;
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        if (fontList.currentIndex >= 0 && fontList.currentIndex < fontList.count) {
                            control.familyActivated(fontList.model[fontList.currentIndex]);
                            fontPopup.close();
                            control.forceActiveFocus();
                        }
                        event.accepted = true;
                    }
                }
            }

            ListView {
                id: fontList

                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 260)
                clip: true
                model: control.filteredFamilies(fontSearch.text)
                currentIndex: 0
                keyNavigationEnabled: false
                ScrollIndicator.vertical: ScrollIndicator {}

                delegate: ItemDelegate {
                    id: fontOption

                    required property int index
                    required property string modelData

                    width: fontList.width
                    implicitHeight: 34
                    highlighted: fontList.currentIndex === index || control.family === modelData
                    Accessible.name: control.familyLabel(modelData)
                    onHoveredChanged: {
                        if (hovered) {
                            fontList.currentIndex = index;
                        }
                    }
                    onClicked: {
                        control.familyActivated(modelData);
                        fontPopup.close();
                        control.forceActiveFocus();
                    }

                    contentItem: Text {
                        text: control.familyLabel(fontOption.modelData)
                        color: Theme.text
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        font.family: control.showFontPreview && fontOption.modelData.length > 0 ? fontOption.modelData : Theme.uiFont
                        font.pixelSize: Theme.textBody
                    }

                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: fontOption.highlighted ? Theme.selectedHover : Theme.floatingBackground
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: fontList.count === 0
                    text: qsTr("No matching fonts")
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textBody
                }
            }
        }

        background: Rectangle {
            radius: Theme.radiusControl
            color: Theme.floatingBackground
            border.color: Theme.borderStrong
        }
    }

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
}
