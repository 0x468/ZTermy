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
    readonly property bool adjustableBackdrop: acrylicBackdrop || transparentBackdrop
    readonly property real normalizedBackdropOpacity: Math.max(0.0, Math.min(1.0, backdropOpacity))

    readonly property color windowBackground: backdropActive ? "transparent" : (dark ? "#FF0B0F14" : "#FFF8FAFC")
    readonly property color panelBackground: withAlpha(dark ? "#111824" : "#F1F5F9", panelAlpha)
    readonly property color chromeBackground: withAlpha(dark ? "#0F1722" : "#E2E8F0", chromeAlpha)
    readonly property color contentBackground: withAlpha(dark ? "#0A0E14" : "#FFFFFF", contentAlpha)
    readonly property color workspaceBackground: withAlpha(dark ? "#0B1017" : "#FFFFFF", workspaceAlpha)
    readonly property real panelAlpha: adjustableBackdrop ? normalizedBackdropOpacity : micaBackdrop ? 0.82 : micaAltBackdrop ? 0.88 : 1.0
    readonly property real chromeAlpha: adjustableBackdrop ? normalizedBackdropOpacity : micaBackdrop ? 0.60 : micaAltBackdrop ? 0.72 : 1.0
    readonly property real contentAlpha: adjustableBackdrop ? normalizedBackdropOpacity : micaBackdrop ? 0.82 : micaAltBackdrop ? 0.88 : 1.0
    readonly property real workspaceAlpha: adjustableBackdrop ? normalizedBackdropOpacity : micaBackdrop ? 0.88 : micaAltBackdrop ? 0.92 : 1.0

    // The native window supplies one material layer. QML surfaces add tint and
    // hierarchy without creating independent blur regions. Adjustable
    // backdrops reach true opaque at 100%, while cards and controls retain a
    // readable tint at 0%.
    readonly property real elevatedAlpha: adjustableBackdrop ? mixAlpha(dark ? 0.64 : 0.84, normalizedBackdropOpacity) : micaBackdrop ? 0.82 : micaAltBackdrop ? 0.88 : 1.0
    readonly property real raisedAlpha: adjustableBackdrop ? mixAlpha(dark ? 0.70 : 0.88, normalizedBackdropOpacity) : micaBackdrop ? 0.86 : micaAltBackdrop ? 0.91 : 1.0
    readonly property real controlAlpha: adjustableBackdrop ? mixAlpha(dark ? 0.78 : 0.92, normalizedBackdropOpacity) : micaBackdrop ? 0.90 : micaAltBackdrop ? 0.94 : 1.0
    readonly property real fieldAlpha: adjustableBackdrop ? mixAlpha(dark ? 0.84 : 0.96, normalizedBackdropOpacity) : micaBackdrop ? 0.94 : micaAltBackdrop ? 0.97 : 1.0
    readonly property real floatingAlpha: adjustableBackdrop ? mixAlpha(0.94, normalizedBackdropOpacity) : 0.96

    readonly property color raisedBackground: withAlpha(dark ? "#1E293B" : "#E2E8F0", raisedAlpha)
    readonly property color elevatedBackground: withAlpha(dark ? "#141E2B" : "#F1F5F9", elevatedAlpha)
    readonly property color controlBackground: withAlpha(dark ? "#172033" : "#E2E8F0", controlAlpha)
    readonly property color controlDisabled: withAlpha(dark ? "#131B29" : "#E8EDF3", controlAlpha)
    readonly property color controlPressed: withAlpha(dark ? "#263244" : "#CBD5E1", controlAlpha)
    readonly property color controlHover: withAlpha(dark ? "#1F2A3A" : "#DCE5EF", controlAlpha)
    readonly property color fieldBackground: withAlpha(dark ? "#111827" : "#FFFFFF", fieldAlpha)
    readonly property color floatingBackground: withAlpha(dark ? "#1E293B" : "#FFFFFF", floatingAlpha)

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
    readonly property int motionMedium: animationsEnabled ? 180 : 0
    readonly property int motionSlow: animationsEnabled ? 220 : 0
    readonly property int motionDistanceSmall: animationsEnabled ? 8 : 0

    function withAlpha(baseColor: color, alpha: real): color {
        return Qt.rgba(baseColor.r, baseColor.g, baseColor.b, Math.max(0.0, Math.min(1.0, alpha)));
    }

    function mixAlpha(minimumAlpha: real, amount: real): real {
        const clampedMinimum = Math.max(0.0, Math.min(1.0, minimumAlpha));
        const clampedAmount = Math.max(0.0, Math.min(1.0, amount));
        return clampedMinimum + ((1.0 - clampedMinimum) * clampedAmount);
    }

    readonly property int spacingDense: 4
    readonly property int spacingControl: 8
    readonly property int spacingRelated: 12
    readonly property int spacingSection: 16
    readonly property int cardInset: 20
    readonly property int pageInset: 28
    readonly property int pageInsetCompact: 16
    readonly property int titleBarHeight: 38
    readonly property int navigationWidth: 210
    readonly property int navigationWidthCompact: 164
    readonly property int narrowWindowWidth: 760
}
