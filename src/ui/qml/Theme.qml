pragma Singleton

import QtQuick

QtObject {
    property string preference: "dark"
    property bool systemDark: true
    readonly property bool dark: preference === "dark" || (preference === "system" && systemDark)

    readonly property color windowBackground: dark ? "#F20B0F14" : "#F2F8FAFC"
    readonly property color panelBackground: dark ? "#E6111824" : "#F2F1F5F9"
    readonly property color chromeBackground: dark ? "#E60F1722" : "#F2E2E8F0"
    readonly property color contentBackground: dark ? "#E60A0E14" : "#F2FFFFFF"
    readonly property color workspaceBackground: dark ? "#F20B1017" : "#F8FFFFFF"
    readonly property color raisedBackground: dark ? "#1E293B" : "#E2E8F0"
    readonly property color elevatedBackground: dark ? "#141E2B" : "#F1F5F9"
    readonly property color controlBackground: dark ? "#172033" : "#E2E8F0"
    readonly property color controlPressed: dark ? "#263244" : "#CBD5E1"
    readonly property color controlHover: dark ? "#1F2A3A" : "#DCE5EF"
    readonly property color fieldBackground: dark ? "#111827" : "#FFFFFF"
    readonly property color floatingBackground: dark ? "#F21E293B" : "#F2FFFFFF"

    readonly property color border: dark ? "#263244" : "#CBD5E1"
    readonly property color borderStrong: dark ? "#334155" : "#94A3B8"
    readonly property color text: dark ? "#F8FAFC" : "#0F172A"
    readonly property color textMuted: dark ? "#94A3B8" : "#475569"
    readonly property color textSoft: dark ? "#CBD5E1" : "#334155"
    readonly property color textSubtle: dark ? "#64748B" : "#64748B"

    readonly property color accent: dark ? "#22C55E" : "#15803D"
    readonly property color accentText: dark ? "#07130B" : "#FFFFFF"
    readonly property color focus: dark ? "#86EFAC" : "#16A34A"
    readonly property color danger: dark ? "#EF4444" : "#DC2626"
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
