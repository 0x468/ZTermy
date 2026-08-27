pragma Singleton

import QtQuick

QtObject {
    property string preference: "dark"
    property bool systemDark: true
    property bool animationsEnabled: true
    property bool highContrast: false
    property color highContrastBackground: "#000000"
    property color highContrastText: "#FFFFFF"
    property color highContrastHighlight: "#1AEBFF"
    property color highContrastHighlightText: "#000000"
    property string backdropPreference: "acrylic"
    property real backdropOpacity: 1.0
    property string accentPreference: "ztermy"
    property color systemAccent: "#0078D4"
    property color customAccent: "#22C55E"
    readonly property bool dark: highContrast ? relativeLuminance(highContrastBackground) < 0.5 : preference === "dark" || (preference === "system" && systemDark)
    readonly property bool micaBackdrop: backdropPreference === "mica"
    readonly property bool micaAltBackdrop: backdropPreference === "micaAlt"
    readonly property bool acrylicBackdrop: backdropPreference === "acrylic"
    readonly property bool transparentBackdrop: backdropPreference === "transparent"
    readonly property bool solidBackdrop: backdropPreference === "solid"
    readonly property bool backdropActive: !highContrast && (micaBackdrop || micaAltBackdrop || acrylicBackdrop || transparentBackdrop)
    readonly property bool adjustableBackdrop: acrylicBackdrop || transparentBackdrop
    readonly property real normalizedBackdropOpacity: Math.max(0.0, Math.min(1.0, backdropOpacity))

    readonly property color windowBackground: highContrast ? highContrastBackground : backdropActive ? "transparent" : (dark ? "#FF0B0F14" : "#FFF8FAFC")
    readonly property color panelBackground: highContrast ? highContrastBackground : withAlpha(dark ? "#111824" : "#F1F5F9", panelAlpha)
    readonly property color chromeBackground: highContrast ? highContrastBackground : withAlpha(dark ? "#0F1722" : "#E2E8F0", chromeAlpha)
    readonly property color contentBackground: highContrast ? highContrastBackground : withAlpha(dark ? "#0A0E14" : "#FFFFFF", contentAlpha)
    readonly property color workspaceBackground: highContrast ? highContrastBackground : withAlpha(dark ? "#0B1017" : "#FFFFFF", workspaceAlpha)
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

    readonly property color raisedBackground: highContrast ? highContrastBackground : withAlpha(dark ? "#1E293B" : "#E2E8F0", raisedAlpha)
    readonly property color elevatedBackground: highContrast ? highContrastBackground : withAlpha(dark ? "#141E2B" : "#F1F5F9", elevatedAlpha)
    readonly property color controlBackground: highContrast ? highContrastBackground : withAlpha(dark ? "#172033" : "#E2E8F0", controlAlpha)
    readonly property color controlDisabled: highContrast ? highContrastBackground : withAlpha(dark ? "#131B29" : "#E8EDF3", controlAlpha)
    // Keep ordinary control labels readable in every Windows high-contrast
    // palette. System highlight colors are reserved for accent controls and
    // text selection, where the matching highlight-text color is also used.
    readonly property color controlPressed: highContrast ? mixColor(highContrastBackground, highContrastText, 0.32) : withAlpha(dark ? "#263244" : "#CBD5E1", controlAlpha)
    readonly property color controlHover: highContrast ? mixColor(highContrastBackground, highContrastText, 0.18) : withAlpha(dark ? "#1F2A3A" : "#DCE5EF", controlAlpha)
    // Caption buttons sit directly on the chrome surface. The ordinary light
    // control hover is intentionally subtle on cards and fields, but is too
    // close to the light chrome tint to remain visible through a backdrop.
    readonly property color captionPressed: highContrast ? controlPressed : withAlpha(dark ? "#263244" : "#B8C4D3", controlAlpha)
    readonly property color captionHover: highContrast ? controlHover : withAlpha(dark ? "#1F2A3A" : "#CBD5E1", controlAlpha)
    readonly property color fieldBackground: highContrast ? highContrastBackground : withAlpha(dark ? "#111827" : "#FFFFFF", fieldAlpha)
    readonly property color floatingBackground: highContrast ? highContrastBackground : withAlpha(dark ? "#1E293B" : "#FFFFFF", floatingAlpha)

    readonly property color border: highContrast ? highContrastText : dark ? "#263244" : "#CBD5E1"
    readonly property color borderStrong: highContrast ? highContrastText : dark ? "#334155" : "#94A3B8"
    readonly property color text: highContrast ? highContrastText : dark ? "#F8FAFC" : "#0F172A"
    readonly property color textMuted: highContrast ? highContrastText : dark ? "#94A3B8" : "#475569"
    readonly property color textSoft: highContrast ? highContrastText : dark ? "#CBD5E1" : "#334155"
    readonly property color textSubtle: highContrast ? highContrastText : dark ? "#64748B" : "#64748B"

    readonly property bool ztermyAccent: accentPreference === "ztermy"
    readonly property color accentBase: accentPreference === "system" ? systemAccent : customAccent
    readonly property color accent: highContrast ? highContrastHighlight : ztermyAccent ? (dark ? "#22C55E" : "#15803D") : accentBase
    readonly property color accentText: highContrast ? highContrastHighlightText : ztermyAccent ? (dark ? "#07130B" : "#FFFFFF") : contrastText(accentBase)
    readonly property color accentHover: ztermyAccent ? (dark ? "#4ADE80" : "#166534") : mixColor(accentBase, accentText, 0.14)
    readonly property color accentPressed: ztermyAccent ? (dark ? "#16A34A" : "#14532D") : mixColor(accentBase, "#000000", 0.18)
    readonly property color focus: highContrast ? highContrastHighlight : ztermyAccent ? (dark ? "#86EFAC" : "#16A34A") : mixColor(accentBase, accentText, 0.34)
    readonly property color selectedBackground: highContrast ? mixColor(highContrastBackground, highContrastText, 0.22) : ztermyAccent ? (dark ? "#173A2B" : "#DCFCE7") : mixColor(accentBase, dark ? "#0B1017" : "#FFFFFF", dark ? 0.72 : 0.84)
    readonly property color selectedHover: highContrast ? mixColor(highContrastBackground, highContrastText, 0.30) : ztermyAccent ? (dark ? "#1F513A" : "#BBF7D0") : mixColor(accentBase, dark ? "#0B1017" : "#FFFFFF", dark ? 0.58 : 0.72)
    readonly property color success: highContrast ? highContrastHighlight : dark ? "#22C55E" : "#15803D"
    readonly property color successText: highContrast ? highContrastText : dark ? "#86EFAC" : "#15803D"
    readonly property color warning: highContrast ? highContrastHighlight : dark ? "#F59E0B" : "#D97706"
    readonly property color searchMatchBackground: highContrast ? selectedBackground : mixColor(warning, dark ? "#0B1017" : "#FFFFFF", dark ? 0.68 : 0.82)
    readonly property color searchCurrentBackground: warning
    readonly property color searchCurrentForeground: highContrast ? highContrastHighlightText : contrastText(warning)
    readonly property color danger: highContrast ? highContrastHighlight : dark ? "#EF4444" : "#DC2626"
    readonly property color dangerText: highContrast ? highContrastText : dark ? "#FCA5A5" : "#B91C1C"
    readonly property color dangerBorder: highContrast ? highContrastText : dark ? "#7F1D1D" : "#FCA5A5"
    readonly property color dangerSurface: highContrast ? highContrastHighlight : dark ? "#991B1B" : "#DC2626"
    readonly property color dangerHover: highContrast ? highContrastHighlight : dark ? "#B91C1C" : "#B91C1C"
    readonly property color dangerPressed: highContrast ? highContrastHighlight : dark ? "#7F1D1D" : "#991B1B"
    readonly property color dangerSurfaceText: highContrast ? highContrastHighlightText : "#FFFFFF"
    readonly property color closeHover: "#C42B1C"
    readonly property color modalScrim: "#99000000"

    // Main.qml binds this to FontCatalog's Windows-aware effective UI family.
    // An empty fallback leaves Qt controls on the application font during startup.
    property string uiFont: ""
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

    function mixColor(first: color, second: color, amount: real): color {
        const clampedAmount = Math.max(0.0, Math.min(1.0, amount));
        return Qt.rgba(first.r + ((second.r - first.r) * clampedAmount), first.g + ((second.g - first.g) * clampedAmount), first.b + ((second.b - first.b) * clampedAmount), first.a + ((second.a - first.a) * clampedAmount));
    }

    function linearColorChannel(channel: real): real {
        return channel <= 0.04045 ? channel / 12.92 : Math.pow((channel + 0.055) / 1.055, 2.4);
    }

    function relativeLuminance(value: color): real {
        return (0.2126 * linearColorChannel(value.r)) + (0.7152 * linearColorChannel(value.g)) + (0.0722 * linearColorChannel(value.b));
    }

    function contrastText(background: color): color {
        return relativeLuminance(background) > 0.179 ? "#000000" : "#FFFFFF";
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
