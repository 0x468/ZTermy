pragma Singleton

import QtQuick

QtObject {
    property string preference: "dark"
    property bool systemDark: true
    property bool animationsEnabled: true
    property string backdropPreference: "acrylic"
    property real backdropOpacity: 1.0
    readonly property bool dark: preference === "dark" || (preference === "system" && systemDark)
    readonly property bool micaBackdrop: backdropPreference === "mica"
    readonly property bool micaAltBackdrop: backdropPreference === "micaAlt"
    readonly property bool acrylicBackdrop: backdropPreference === "acrylic"
    readonly property bool transparentBackdrop: backdropPreference === "transparent"
    readonly property bool backdropActive: micaBackdrop || micaAltBackdrop || acrylicBackdrop || transparentBackdrop
    readonly property real normalizedBackdropOpacity: Math.max(0.0, Math.min(1.0, backdropOpacity))

    readonly property color windowBackground: backdropActive ? "transparent" : (dark ? "#FF0B0F14" : "#FFF8FAFC")
    readonly property color panelBackground: withAlpha(dark ? "#111824" : "#F1F5F9", panelAlpha)
    readonly property color chromeBackground: withAlpha(dark ? "#0F1722" : "#E2E8F0", chromeAlpha)
    readonly property color contentBackground: withAlpha(dark ? "#0A0E14" : "#FFFFFF", contentAlpha)
    readonly property color workspaceBackground: withAlpha(dark ? "#0B1017" : "#FFFFFF", workspaceAlpha)
    readonly property real panelAlpha: micaBackdrop ? 0.82 : micaAltBackdrop ? 0.88 : acrylicBackdrop ? 0.72 * normalizedBackdropOpacity : transparentBackdrop ? 0.92 * normalizedBackdropOpacity : 1.0
    readonly property real chromeAlpha: micaBackdrop ? 0.60 : micaAltBackdrop ? 0.72 : acrylicBackdrop ? 0.48 * normalizedBackdropOpacity : transparentBackdrop ? 0.75 * normalizedBackdropOpacity : 1.0
    readonly property real contentAlpha: micaBackdrop ? 0.82 : micaAltBackdrop ? 0.88 : acrylicBackdrop ? 0.72 * normalizedBackdropOpacity : transparentBackdrop ? normalizedBackdropOpacity : 1.0
    readonly property real workspaceAlpha: micaBackdrop ? 0.88 : micaAltBackdrop ? 0.92 : acrylicBackdrop ? 0.78 * normalizedBackdropOpacity : transparentBackdrop ? normalizedBackdropOpacity : 1.0
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

    function withAlpha(baseColor: color, alpha: real): color {
        return Qt.rgba(baseColor.r, baseColor.g, baseColor.b, Math.max(0.0, Math.min(1.0, alpha)));
    }

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
