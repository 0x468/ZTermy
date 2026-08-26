pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

Rectangle {
    id: control

    default property alias actionData: actionRow.data
    property string kind: "empty"
    property string heading: ""
    property string description: ""
    property string detail: ""
    property bool centered: false
    property var steps: []
    property int activeStep: 0
    readonly property bool error: kind === "error"
    readonly property bool loading: kind === "loading"
    readonly property int stepCount: steps && steps.length !== undefined ? steps.length : 0
    readonly property int normalizedActiveStep: stepCount > 0 ? Math.max(0, Math.min(activeStep, stepCount - 1)) : -1

    implicitHeight: stateLayout.implicitHeight + 36
    radius: Theme.radiusPanel
    color: Theme.elevatedBackground
    border.color: error ? Theme.dangerBorder : loading ? Theme.accent : Theme.border
    Accessible.role: error ? Accessible.AlertMessage : Accessible.StaticText
    Accessible.name: heading + (description.length > 0 ? ". " + description : "")

    ColumnLayout {
        id: stateLayout

        anchors.fill: parent
        anchors.margins: 18
        spacing: 7

        Text {
            Layout.fillWidth: true
            text: control.heading
            color: control.error ? Theme.dangerText : control.loading ? Theme.successText : Theme.text
            horizontalAlignment: control.centered ? Text.AlignHCenter : Text.AlignLeft
            wrapMode: Text.WordWrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.textBody
            font.weight: Font.DemiBold
        }

        Text {
            Layout.fillWidth: true
            visible: control.description.length > 0
            text: control.description
            color: control.error ? Theme.textSoft : Theme.textMuted
            horizontalAlignment: control.centered ? Text.AlignHCenter : Text.AlignLeft
            wrapMode: Text.WordWrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.textLabel
        }

        Text {
            Layout.fillWidth: true
            visible: control.detail.length > 0
            text: control.detail
            color: Theme.textMuted
            horizontalAlignment: control.centered ? Text.AlignHCenter : Text.AlignLeft
            wrapMode: Text.WordWrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.textLabel
            opacity: 0.82
        }

        Item {
            id: loadingTrack

            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 3 : 0
            Layout.topMargin: visible ? 2 : 0
            visible: control.loading
            clip: true

            Rectangle {
                anchors.fill: parent
                radius: height / 2
                color: Theme.border
            }

            Rectangle {
                id: loadingIndicator

                x: Theme.animationsEnabled ? -width : 0
                width: Theme.animationsEnabled ? Math.max(36, loadingTrack.width * 0.32) : loadingTrack.width
                height: loadingTrack.height
                radius: height / 2
                color: Theme.accent

                NumberAnimation on x {
                    from: -loadingIndicator.width
                    to: loadingTrack.width
                    duration: 1100
                    loops: Animation.Infinite
                    running: loadingTrack.visible && Theme.animationsEnabled
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: visible ? 3 : 0
            visible: control.stepCount > 0
            spacing: 6

            Repeater {
                model: control.stepCount

                delegate: RowLayout {
                    id: stepRow

                    required property int index
                    readonly property bool currentStep: index === control.normalizedActiveStep
                    readonly property bool completedStep: index < control.normalizedActiveStep

                    Layout.fillWidth: true
                    spacing: 8

                    Rectangle {
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                        radius: width / 2
                        color: stepRow.currentStep ? Theme.accent : stepRow.completedStep ? Theme.selectedBackground : Theme.controlBackground
                        border.color: stepRow.currentStep || stepRow.completedStep ? Theme.accent : Theme.border

                        Text {
                            anchors.centerIn: parent
                            text: String(stepRow.index + 1)
                            color: stepRow.currentStep ? Theme.accentText : stepRow.completedStep ? Theme.accent : Theme.textMuted
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.textCompact
                            font.weight: stepRow.currentStep ? Font.DemiBold : Font.Normal
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: String(control.steps[stepRow.index])
                        color: stepRow.currentStep ? Theme.text : stepRow.completedStep ? Theme.textSoft : Theme.textMuted
                        wrapMode: Text.WordWrap
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textLabel
                        font.weight: stepRow.currentStep ? Font.DemiBold : Font.Normal
                    }
                }
            }
        }

        RowLayout {
            id: actionRow

            Layout.fillWidth: true
            Layout.topMargin: visible ? 3 : 0
            visible: children.length > 1

            Item {
                Layout.fillWidth: true
            }
        }
    }
}
