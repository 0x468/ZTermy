pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: strip

    required property var controller
    property real availableWidth: 1000
    readonly property var telemetry: controller.activeRemoteTelemetry
    readonly property bool ready: telemetry.available === true
    readonly property bool showNetwork: availableWidth >= 930
    readonly property bool showLatency: availableWidth >= 1040

    implicitWidth: metricsRow.implicitWidth
    implicitHeight: 26
    visible: telemetry.state !== "paused" && (ready || telemetry.state === "loading")

    function kib(value) {
        const numeric = Number(value) || 0;
        if (numeric >= 1048576)
            return (numeric / 1048576).toFixed(1) + "G";
        if (numeric >= 1024)
            return (numeric / 1024).toFixed(1) + "M";
        return numeric.toFixed(0) + "K";
    }

    function rate(value) {
        const numeric = Number(value) || 0;
        if (numeric >= 1073741824)
            return (numeric / 1073741824).toFixed(1) + "G/s";
        if (numeric >= 1048576)
            return (numeric / 1048576).toFixed(1) + "M/s";
        if (numeric >= 1024)
            return (numeric / 1024).toFixed(1) + "K/s";
        return numeric.toFixed(0) + "B/s";
    }

    function rootDisk() {
        const disks = telemetry.disks || [];
        for (const disk of disks) {
            if (disk.mountPoint === "/")
                return disk;
        }
        return disks.length > 0 ? disks[0] : null;
    }

    function series(field) {
        const values = [];
        for (const sample of telemetry.history || []) {
            const value = Number(sample[field]);
            if (value >= 0)
                values.push(value);
        }
        return values;
    }

    function openMetric(kind, trigger) {
        detailPopup.metric = kind;
        detailPopup.trigger = trigger;
        const point = trigger.mapToItem(detailPopup.parent, 0, trigger.height + 6);
        detailPopup.x = Math.max(8, Math.min(point.x, detailPopup.parent.width - detailPopup.width - 8));
        detailPopup.y = Math.max(8, Math.min(point.y, detailPopup.parent.height - detailPopup.height - 8));
        detailPopup.open();
    }

    component MetricButton: ToolButton {
        id: metricButton

        required property string metric
        required property string iconName
        required property string label
        property color iconColor: Theme.textMuted

        implicitWidth: contentRow.implicitWidth + 8
        implicitHeight: 24
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.name: label
        onClicked: strip.openMetric(metric, metricButton)
        onHoveredChanged: {
            if (hovered) {
                hoverOpenTimer.targetButton = metricButton;
                hoverOpenTimer.restart();
            } else {
                hoverOpenTimer.stop();
                popupCloseTimer.restart();
            }
        }

        background: Rectangle {
            radius: Theme.radiusSmall
            color: metricButton.down ? Theme.controlPressed : metricButton.hovered || metricButton.visualFocus ? Theme.controlHover : "transparent"
            border.color: metricButton.visualFocus ? Theme.focus : "transparent"
        }

        contentItem: Row {
            id: contentRow
            spacing: 3

            AppIcon {
                width: 11
                height: 11
                anchors.verticalCenter: parent.verticalCenter
                name: metricButton.iconName
                color: metricButton.iconColor
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: metricButton.label
                color: Theme.textMuted
                font.family: Theme.uiFont
                font.pixelSize: Theme.textCompact
            }
        }

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }
    }

    Timer {
        id: hoverOpenTimer
        property var targetButton: null
        interval: 220
        onTriggered: {
            if (targetButton && targetButton.hovered)
                strip.openMetric(targetButton.metric, targetButton);
        }
    }

    Connections {
        target: strip.controller
        function onActiveTerminalTabChanged() {
            detailPopup.close();
        }
    }

    Timer {
        id: popupCloseTimer
        interval: 140
        onTriggered: {
            if (!detailPopup.hovered)
                detailPopup.close();
        }
    }

    Row {
        id: metricsRow
        anchors.verticalCenter: parent.verticalCenter
        spacing: 1

        BusyIndicator {
            width: 16
            height: 16
            anchors.verticalCenter: parent.verticalCenter
            running: !strip.ready && strip.telemetry.state === "loading"
            visible: running
        }

        MetricButton {
            visible: strip.ready
            metric: "cpu"
            iconName: "cpu"
            label: strip.telemetry.cpuPercent >= 0 ? Math.round(strip.telemetry.cpuPercent) + "% (" + strip.telemetry.cpuCores + "C)" : "-- (" + strip.telemetry.cpuCores + "C)"
        }

        MetricButton {
            visible: strip.ready
            metric: "memory"
            iconName: "memory"
            label: strip.kib(strip.telemetry.memoryUsedKiB) + "/" + strip.kib(strip.telemetry.memoryTotalKiB)
        }

        MetricButton {
            visible: strip.ready && strip.rootDisk() !== null
            metric: "disk"
            iconName: "disk"
            label: strip.rootDisk() ? strip.kib(strip.rootDisk().usedKiB) + "/" + strip.kib(strip.rootDisk().totalKiB) : "--"
            iconColor: strip.rootDisk() && strip.rootDisk().percent >= 90 ? Theme.danger : Theme.textMuted
        }

        MetricButton {
            visible: strip.ready && strip.showNetwork
            metric: "network"
            iconName: "network"
            label: "↓ " + strip.rate(strip.telemetry.receivedBytesPerSecond) + "  ↑ " + strip.rate(strip.telemetry.transmittedBytesPerSecond)
        }

        MetricButton {
            visible: strip.ready && strip.showLatency
            metric: "latency"
            iconName: "activity"
            label: strip.telemetry.latencyMs + "ms"
        }
    }

    Popup {
        id: detailPopup

        property string metric: "cpu"
        property var trigger: null
        readonly property bool hovered: detailHover.hovered
        readonly property real maximumPanelHeight: Math.max(160, Math.min(420, parent ? parent.height - 16 : 420))

        parent: Overlay.overlay
        width: 326
        height: Math.min(maximumPanelHeight, detailColumn.implicitHeight + 24)
        padding: 12
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: trigger = null

        enter: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.motionFast
            }
        }
        exit: Transition {
            NumberAnimation {
                property: "opacity"
                from: 1
                to: 0
                duration: Theme.motionFast
            }
        }

        background: Rectangle {
            radius: Theme.radiusControl
            color: Theme.floatingBackground
            border.color: Theme.borderStrong
        }

        contentItem: ScrollView {
            id: detailScroll

            clip: true
            contentWidth: availableWidth
            contentHeight: detailColumn.implicitHeight
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: contentHeight > availableHeight ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff

            ColumnLayout {
                id: detailColumn

                width: detailScroll.availableWidth
                spacing: 9

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        Layout.fillWidth: true
                        text: detailPopup.metric === "cpu" ? qsTr("CPU details") : detailPopup.metric === "memory" ? qsTr("Memory details") : detailPopup.metric === "disk" ? qsTr("Mounted disks") : detailPopup.metric === "network" ? qsTr("Network interfaces") : qsTr("SSH probe latency")
                        color: Theme.text
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textBody
                        font.weight: Font.DemiBold
                    }

                    ToolButton {
                        id: refreshTelemetryButton

                        implicitWidth: 26
                        implicitHeight: 26
                        onClicked: strip.controller.refreshRemoteTelemetry()
                        Accessible.name: qsTr("Refresh remote telemetry")
                        background: Rectangle {
                            radius: 13
                            color: refreshTelemetryButton.hovered ? Theme.controlHover : "transparent"
                        }
                        contentItem: AppIcon {
                            name: "refresh"
                            color: Theme.textSoft
                        }
                        AppToolTip {
                            text: qsTr("Refresh")
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: detailPopup.metric === "latency" ? qsTr("Auxiliary SSH command round trip; this is not ICMP latency.") : qsTr("Recent foreground samples")
                    color: Theme.textMuted
                    wrapMode: Text.Wrap
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textCompact
                }

                TelemetrySparkline {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    values: strip.series(detailPopup.metric === "cpu" ? "cpu" : detailPopup.metric === "memory" ? "memory" : detailPopup.metric === "disk" ? "disk" : detailPopup.metric === "latency" ? "latency" : "received")
                    upperBound: detailPopup.metric === "cpu" || detailPopup.metric === "memory" || detailPopup.metric === "disk" ? 100 : -1
                    minimumSpan: detailPopup.metric === "network" ? 1024 : detailPopup.metric === "latency" ? 10 : 8
                    lineColor: detailPopup.metric === "latency" ? "#A78BFA" : Theme.accent
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 5
                    visible: detailPopup.metric === "cpu" && (strip.telemetry.cores || []).length > 0

                    Repeater {
                        model: strip.telemetry.cores || []
                        delegate: Rectangle {
                            required property real modelData
                            required property int index
                            width: 70
                            height: 24
                            radius: Theme.radiusSmall
                            color: Theme.controlBackground
                            Text {
                                anchors.centerIn: parent
                                text: qsTr("Core %1  %2%").arg(parent.index).arg(Math.round(parent.modelData))
                                color: parent.modelData >= 90 ? Theme.dangerText : Theme.textSoft
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.textCompact
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: detailPopup.metric === "memory"

                    Text {
                        text: qsTr("Used %1 of %2").arg(strip.kib(strip.telemetry.memoryUsedKiB)).arg(strip.kib(strip.telemetry.memoryTotalKiB))
                        color: Theme.textSoft
                        font.pixelSize: Theme.textCompact
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 7
                        radius: 3.5
                        color: Theme.controlBackground
                        Rectangle {
                            width: parent.width * Math.max(0, Math.min(1, Number(strip.telemetry.memoryUsedKiB || 0) / Math.max(1, Number(strip.telemetry.memoryTotalKiB || 1))))
                            height: parent.height
                            radius: parent.radius
                            color: Theme.accent
                        }
                    }
                    Text {
                        text: qsTr("Available %1 · Cache %2 · Swap %3/%4").arg(strip.kib(strip.telemetry.memoryAvailableKiB)).arg(strip.kib(strip.telemetry.memoryCachedKiB)).arg(strip.kib(strip.telemetry.swapUsedKiB)).arg(strip.kib(strip.telemetry.swapTotalKiB))
                        color: Theme.textMuted
                        font.pixelSize: Theme.textCompact
                    }
                    Repeater {
                        model: strip.telemetry.processes || []
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: parent.modelData.command + "  (" + parent.modelData.pid + ")"
                                elide: Text.ElideRight
                                color: Theme.textSoft
                                font.family: Theme.terminalFont
                                font.pixelSize: Theme.textCompact
                            }
                            Text {
                                text: Number(parent.modelData.memoryPercent).toFixed(1) + "%"
                                color: Theme.textMuted
                                font.pixelSize: Theme.textCompact
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    visible: detailPopup.metric === "disk"
                    Repeater {
                        model: strip.telemetry.disks || []
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: parent.modelData.mountPoint
                                elide: Text.ElideMiddle
                                color: Theme.textSoft
                                font.family: Theme.terminalFont
                                font.pixelSize: Theme.textCompact
                            }
                            Text {
                                text: strip.kib(parent.modelData.usedKiB) + "/" + strip.kib(parent.modelData.totalKiB) + "  " + Math.round(parent.modelData.percent) + "%"
                                color: parent.modelData.percent >= 90 ? Theme.dangerText : Theme.textMuted
                                font.pixelSize: Theme.textCompact
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    visible: detailPopup.metric === "network"
                    Repeater {
                        model: strip.telemetry.interfaces || []
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: parent.modelData.name
                                color: Theme.textSoft
                                font.family: Theme.terminalFont
                                font.pixelSize: Theme.textCompact
                            }
                            Text {
                                text: "↓ " + strip.rate(parent.modelData.receivedBytesPerSecond) + "  ↑ " + strip.rate(parent.modelData.transmittedBytesPerSecond)
                                color: Theme.textMuted
                                font.pixelSize: Theme.textCompact
                            }
                        }
                    }
                }
            }
        }

        HoverHandler {
            id: detailHover
            onHoveredChanged: {
                if (hovered)
                    popupCloseTimer.stop();
                else
                    popupCloseTimer.restart();
            }
        }
    }
}
