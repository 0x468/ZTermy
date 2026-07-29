import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    readonly property int titleBarHeight: 42
    readonly property int captionButtonWidth: 46
    readonly property color backgroundColor: "#F20B0F14"
    readonly property color panelColor: "#E6111824"
    readonly property color raisedColor: "#1E293B"
    readonly property color borderColor: "#263244"
    readonly property color textColor: "#F8FAFC"
    readonly property color mutedColor: "#94A3B8"
    readonly property color accentColor: "#22C55E"

    color: backgroundColor

    function reportTitleBarMetrics() {
        windowChrome.setTitleBarMetrics(
            titleBarHeight,
            width - (captionButtonWidth * 3),
            width - (captionButtonWidth * 2),
            captionButtonWidth)
    }

    Component.onCompleted: reportTitleBarMetrics()
    onWidthChanged: reportTitleBarMetrics()

    Rectangle {
        id: titleBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.titleBarHeight
        color: "#E60F1722"

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: root.borderColor
        }

        RowLayout {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 9

            Rectangle {
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                radius: 5
                color: root.accentColor

                Text {
                    anchors.centerIn: parent
                    text: ">_"
                    color: "#052E16"
                    font.family: "Cascadia Mono"
                    font.pixelSize: 9
                    font.weight: Font.Bold
                }
            }

            Text {
                text: "ztermy"
                color: root.textColor
                font.family: "Segoe UI Variable"
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }

            Rectangle {
                Layout.leftMargin: 12
                Layout.preferredWidth: 1
                Layout.preferredHeight: 16
                color: root.borderColor
            }

            RowLayout {
                Layout.leftMargin: 4
                spacing: 7

                Rectangle {
                    Layout.preferredWidth: 7
                    Layout.preferredHeight: 7
                    radius: 4
                    color: root.accentColor
                }

                Text {
                    text: "New terminal"
                    color: root.mutedColor
                    font.family: "Segoe UI Variable"
                    font.pixelSize: 12
                }
            }
        }

        Row {
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height

            CaptionButton {
                width: root.captionButtonWidth
                height: titleBar.height
                kind: "minimize"
                accessibleName: "Minimize"
                onActivated: windowChrome.minimizeWindow()
            }

            CaptionButton {
                width: root.captionButtonWidth
                height: titleBar.height
                kind: "maximize"
                accessibleName: windowChrome.maximized ? "Restore" : "Maximize"
                externallyHovered: windowChrome.maximizeButtonHovered
                externallyPressed: windowChrome.maximizeButtonPressed
                onActivated: windowChrome.toggleMaximize()
            }

            CaptionButton {
                width: root.captionButtonWidth
                height: titleBar.height
                kind: "close"
                accessibleName: "Close"
                onActivated: windowChrome.closeWindow()
            }
        }
    }

    RowLayout {
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusBar.top
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 220
            color: root.panelColor

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                Text {
                    text: "WORKSPACE"
                    color: "#64748B"
                    font.family: "Segoe UI Variable"
                    font.pixelSize: 10
                    font.letterSpacing: 1.2
                    font.weight: Font.DemiBold
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 38
                    radius: 7
                    color: root.raisedColor

                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10

                        Rectangle {
                            width: 3
                            height: 16
                            radius: 2
                            color: root.accentColor
                        }

                        Text {
                            text: "Terminal"
                            color: root.textColor
                            font.family: "Segoe UI Variable"
                            font.pixelSize: 13
                        }
                    }
                }

                Text {
                    Layout.leftMargin: 15
                    text: "Hosts"
                    color: root.mutedColor
                    font.family: "Segoe UI Variable"
                    font.pixelSize: 13
                }

                Text {
                    Layout.leftMargin: 15
                    text: "Settings"
                    color: root.mutedColor
                    font.family: "Segoe UI Variable"
                    font.pixelSize: 13
                }

                Item {
                    Layout.fillHeight: true
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 58
                    radius: 8
                    color: "#141E2B"
                    border.color: root.borderColor

                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3

                        Text {
                            text: "Local machine"
                            color: root.textColor
                            font.family: "Segoe UI Variable"
                            font.pixelSize: 12
                        }

                        Text {
                            text: "Windows 11 · ready"
                            color: root.mutedColor
                            font.family: "Segoe UI Variable"
                            font.pixelSize: 10
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#E60A0E14"

            Rectangle {
                anchors.fill: parent
                anchors.margins: 14
                radius: 9
                color: "#D90B1017"
                border.color: root.borderColor

                Column {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 9

                    Text {
                        text: "PowerShell 7"
                        color: root.mutedColor
                        font.family: "Cascadia Mono"
                        font.pixelSize: 12
                    }

                    Text {
                        text: "PS D:\\Repo\\Qt\\ztermy>"
                        color: root.textColor
                        font.family: "Cascadia Mono"
                        font.pixelSize: 14
                    }

                    Row {
                        spacing: 7

                        Rectangle {
                            width: 8
                            height: 17
                            color: root.accentColor
                        }

                        Text {
                            text: "Terminal engine milestone comes next"
                            color: "#64748B"
                            font.family: "Cascadia Mono"
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: statusBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 27
        color: "#E60F1722"

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: root.borderColor
        }

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 7

            Rectangle {
                width: 6
                height: 6
                radius: 3
                color: root.accentColor
            }

            Text {
                text: "Window shell ready"
                color: root.mutedColor
                font.family: "Segoe UI Variable"
                font.pixelSize: 10
            }
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: "UTF-8   C++23   Qt 6.8"
            color: "#64748B"
            font.family: "Cascadia Mono"
            font.pixelSize: 9
        }
    }
}
