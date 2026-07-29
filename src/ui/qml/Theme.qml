pragma Singleton

import QtQuick

QtObject {
    readonly property color windowBackground: "#F20B0F14"
    readonly property color panelBackground: "#E6111824"
    readonly property color chromeBackground: "#E60F1722"
    readonly property color contentBackground: "#E60A0E14"
    readonly property color workspaceBackground: "#0B1017"
    readonly property color raisedBackground: "#1E293B"
    readonly property color elevatedBackground: "#141E2B"
    readonly property color controlBackground: "#172033"
    readonly property color controlPressed: "#263244"
    readonly property color controlHover: "#1F2A3A"
    readonly property color fieldBackground: "#111827"
    readonly property color floatingBackground: "#F21E293B"

    readonly property color border: "#263244"
    readonly property color borderStrong: "#334155"
    readonly property color text: "#F8FAFC"
    readonly property color textMuted: "#94A3B8"
    readonly property color textSoft: "#CBD5E1"
    readonly property color textSubtle: "#64748B"

    readonly property color accent: "#22C55E"
    readonly property color accentText: "#07130B"
    readonly property color focus: "#86EFAC"
    readonly property color danger: "#EF4444"
    readonly property color closeHover: "#C42B1C"
    readonly property color modalScrim: "#99000000"

    readonly property string uiFont: "Segoe UI Variable"
    readonly property string terminalFont: "Cascadia Mono"

    readonly property int textTitle: 20
    readonly property int textBody: 13
    readonly property int textLabel: 11
    readonly property int textCompact: 9

    readonly property int radiusSmall: 4
    readonly property int radiusControl: 8
    readonly property int radiusPanel: 12
    readonly property int motionFast: 120
}
