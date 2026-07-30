pragma Singleton

import QtQuick

QtObject {
    property string preference: "dark"
    property bool systemDark: true
    property bool animationsEnabled: true
    property bool backdropActive: false
    readonly property bool dark: preference === "dark" || (preference === "system" && systemDark)

    readonly property color windowBackground: dark ? (backdropActive ? "#D90B0F14" : "#FF0B0F14") : (backdropActive ? "#D9F8FAFC" : "#FFF8FAFC")
    readonly property color panelBackground: dark ? (backdropActive ? "#E6111824" : "#FF111824") : (backdropActive ? "#E6F1F5F9" : "#FFF1F5F9")
    readonly property color chromeBackground: dark ? (backdropActive ? "#D90F1722" : "#FF0F1722") : (backdropActive ? "#D9E2E8F0" : "#FFE2E8F0")
    readonly property color contentBackground: dark ? (backdropActive ? "#E60A0E14" : "#FF0A0E14") : (backdropActive ? "#E6FFFFFF" : "#FFFFFFFF")
    readonly property color workspaceBackground: dark ? (backdropActive ? "#F20B1017" : "#FF0B1017") : (backdropActive ? "#F2FFFFFF" : "#FFFFFFFF")
    readonly property color raisedBackground: dark ? "#1E293B" : "#E2E8F0"
    readonly property color elevatedBackground: dark ? "#141E2B" : "#F1F5F9"
    readonly property color controlBackground: dark ? "#172033" : "#E2E8F0"
    readonly property color controlDisabled: dark ? "#131B29" : "#E8EDF3"
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
    readonly property color accentHover: dark ? "#4ADE80" : "#166534"
    readonly property color accentPressed: dark ? "#16A34A" : "#14532D"
    readonly property color accentText: dark ? "#07130B" : "#FFFFFF"
    readonly property color focus: dark ? "#86EFAC" : "#16A34A"
    readonly property color selectedBackground: dark ? "#173A2B" : "#DCFCE7"
    readonly property color selectedHover: dark ? "#1F513A" : "#BBF7D0"
    readonly property color successText: dark ? "#86EFAC" : "#15803D"
    readonly property color danger: dark ? "#EF4444" : "#DC2626"
    readonly property color dangerText: dark ? "#FCA5A5" : "#B91C1C"
    readonly property color dangerBorder: dark ? "#7F1D1D" : "#FCA5A5"
    readonly property color dangerSurface: dark ? "#991B1B" : "#DC2626"
    readonly property color dangerHover: dark ? "#B91C1C" : "#B91C1C"
    readonly property color dangerPressed: dark ? "#7F1D1D" : "#991B1B"
    readonly property color dangerSurfaceText: "#FFFFFF"
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
    readonly property int motionFast: animationsEnabled ? 120 : 0

    readonly property int spacingDense: 4
    readonly property int spacingControl: 8
    readonly property int spacingRelated: 12
    readonly property int spacingSection: 16
    readonly property int cardInset: 20
    readonly property int pageInset: 28
    readonly property int pageInsetCompact: 16
    readonly property int navigationWidth: 210
    readonly property int navigationWidthCompact: 164
    readonly property int narrowWindowWidth: 760
}
