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
    // ADR 0120: one switch for material, shadows and motion. "full" keeps the
    // native material, shadows and complete motion; "reduced" drops shadows
    // and shortens motion; "off" paints solid surfaces without motion.
    property string effectsTier: "full"
    property string accentPreference: "ztermy"
    property color systemAccent: "#0078D4"
    property color customAccent: "#22C55E"
    // ADR 0124: the active palette drives both terminal and application surfaces.
    property var terminalPalette: ({})
    readonly property var skin: surfacePalette(terminalPalette)

    // Shared with theme thumbnails; previewing does not mutate live sessions.
    function surfacePalette(p) {
        const isDark = p && typeof p.dark === "boolean" ? p.dark : preference !== "light";
        const bg = p && p.background ? p.background : isDark ? "#0B1017" : "#FFFFFF";
        const fg = p && p.foreground ? p.foreground : isDark ? "#F8FAFC" : "#0F172A";
        const ink = mixColor(fg, isDark ? "#FFFFFF" : "#17131F", isDark ? 0.44 : 0.55);
        return {
            chrome: mixColor(bg, isDark ? "#FFFFFF" : fg, isDark ? 0.055 : 0.08),
            panel: mixColor(bg, fg, isDark ? 0.038 : 0.035),
            content: mixColor(bg, fg, 0.012),
            card: mixColor(bg, "#FFFFFF", isDark ? 0.047 : 0.52),
            field: mixColor(bg, isDark ? "#000000" : "#FFFFFF", isDark ? 0.13 : 0.7),
            floating: mixColor(bg, "#FFFFFF", isDark ? 0.09 : 0.78),
            text: ink,
            muted: mixColor(bg, ink, isDark ? 0.66 : 0.80),
            border: mixColor(bg, ink, isDark ? 0.18 : 0.21)
        };
    }
    readonly property bool terminalPaletteReady: !!terminalPalette && typeof terminalPalette.background === "string"
    readonly property color terminalBackground: terminalPaletteReady ? terminalPalette.background : (dark ? "#0B1017" : "#FFFFFF")
    readonly property color terminalForeground: terminalPaletteReady ? terminalPalette.foreground : (dark ? "#F8FAFC" : "#0F172A")
    readonly property color terminalCursor: terminalPaletteReady ? terminalPalette.cursor : terminalForeground
    readonly property color terminalSelectionBackground: terminalPaletteReady ? terminalPalette.selectionBackground : "#2A5B91"
    readonly property color terminalSelectionForeground: terminalPaletteReady ? terminalPalette.selectionForeground : "#FFFFFF"
    readonly property var terminalAnsi: terminalPaletteReady ? terminalPalette.ansi : []
    readonly property bool terminalAccentHint: terminalPaletteReady && typeof terminalPalette.accent === "string" && terminalPalette.accent.length === 7
    readonly property bool dark: highContrast ? relativeLuminance(highContrastBackground) < 0.5 : terminalPaletteReady ? !!terminalPalette.dark : preference === "dark" || (preference === "system" && systemDark)
    readonly property bool fullEffects: effectsTier === "full"
    readonly property bool reducedEffects: effectsTier === "reduced"
    readonly property bool materialEnabled: fullEffects && !highContrast
    readonly property bool shadowsEnabled: fullEffects && !highContrast
    readonly property bool motionEnabled: animationsEnabled && effectsTier !== "off"
    // The material the native window should actually apply once the effects
    // tier and high-contrast mode are taken into account.
    readonly property string effectiveBackdrop: materialEnabled ? backdropPreference : "solid"
    readonly property bool micaBackdrop: effectiveBackdrop === "mica"
    readonly property bool micaAltBackdrop: effectiveBackdrop === "micaAlt"
    readonly property bool aeroBackdrop: effectiveBackdrop === "aero"
    readonly property bool acrylicBackdrop: effectiveBackdrop === "acrylic"
    readonly property bool transparentBackdrop: effectiveBackdrop === "transparent"
    readonly property bool solidBackdrop: effectiveBackdrop === "solid"
    readonly property bool backdropActive: micaBackdrop || micaAltBackdrop || aeroBackdrop || acrylicBackdrop || transparentBackdrop
    readonly property bool adjustableBackdrop: aeroBackdrop || acrylicBackdrop || transparentBackdrop
    readonly property real normalizedBackdropOpacity: Math.max(0.0, Math.min(1.0, backdropOpacity))

    // ADR 0120: the material only shows through the chrome (title bar, tab
    // strip) and the terminal workspace. Every other surface is opaque and
    // expresses depth through elevation, never through stacked alpha.
    readonly property real chromeAlpha: adjustableBackdrop ? normalizedBackdropOpacity : micaBackdrop ? 0.60 : micaAltBackdrop ? 0.72 : 1.0
    readonly property real workspaceAlpha: adjustableBackdrop ? normalizedBackdropOpacity : micaBackdrop ? 0.88 : micaAltBackdrop ? 0.92 : 1.0

    readonly property color windowBackground: highContrast ? highContrastBackground : backdropActive ? "transparent" : skin.content
    readonly property color chromeBackground: highContrast ? highContrastBackground : withAlpha(skin.chrome, chromeAlpha)
    readonly property color workspaceBackground: highContrast ? highContrastBackground : withAlpha(terminalBackground, workspaceAlpha)
    // Opaque skin ladder: content pages sit on contentBackground, navigation
    // and side panels on panelBackground, cards and popups above them.
    readonly property color contentBackground: highContrast ? highContrastBackground : skin.content
    readonly property color panelBackground: highContrast ? highContrastBackground : skin.panel
    readonly property color raisedBackground: highContrast ? highContrastBackground : skin.card
    readonly property color elevatedBackground: highContrast ? highContrastBackground : skin.card
    readonly property color controlBackground: highContrast ? highContrastBackground : skin.card
    readonly property color controlDisabled: highContrast ? highContrastBackground : skin.panel
    // Keep ordinary control labels readable in every Windows high-contrast
    // palette. System highlight colors are reserved for accent controls and
    // text selection, where the matching highlight-text color is also used.
    readonly property color controlPressed: highContrast ? mixColor(highContrastBackground, highContrastText, 0.32) : mixColor(skin.card, skin.text, 0.16)
    readonly property color controlHover: highContrast ? mixColor(highContrastBackground, highContrastText, 0.18) : mixColor(skin.card, skin.text, 0.09)
    // Caption buttons sit directly on the chrome surface. The ordinary light
    // control hover is intentionally subtle on cards and fields, but is too
    // close to the light chrome tint to remain visible through a backdrop.
    readonly property color captionPressed: controlPressed
    readonly property color captionHover: controlHover
    // Selected title-bar tab: an opaque card that reads lighter than the
    // chrome in both skins (the light chrome tint equals controlBackground,
    // so the control colour cannot mark a selected tab there).
    readonly property color tabSelectedBackground: highContrast ? selectedBackground : skin.content
    readonly property color fieldBackground: highContrast ? highContrastBackground : skin.field
    readonly property color floatingBackground: highContrast ? highContrastBackground : skin.floating

    // Workspace controls use the same ink as the rest of the themed interface.
    readonly property bool workspaceDark: highContrast ? dark : terminalPaletteReady ? !!terminalPalette.dark : dark
    readonly property color workspaceText: text
    readonly property color workspaceTextSoft: textSoft
    readonly property color workspaceTextMuted: textMuted
    readonly property color workspaceTextSubtle: textSubtle
    readonly property color workspaceBorder: highContrast ? highContrastText : withAlpha(workspaceText, workspaceDark ? 0.14 : 0.18)
    readonly property color workspaceControlHover: highContrast ? controlHover : withAlpha(workspaceText, 0.10)
    readonly property color workspaceControlPressed: highContrast ? controlPressed : withAlpha(workspaceText, 0.18)
    readonly property color workspacePanelBackground: highContrast ? highContrastBackground : mixColor(terminalBackground, workspaceText, 0.05)
    readonly property color workspaceRaisedBackground: highContrast ? highContrastBackground : mixColor(terminalBackground, workspaceText, 0.11)

    readonly property color border: highContrast ? highContrastText : skin.border
    readonly property color borderStrong: highContrast ? highContrastText : mixColor(skin.content, skin.text, 0.32)
    readonly property color text: highContrast ? highContrastText : skin.text
    readonly property color textMuted: highContrast ? highContrastText : skin.muted
    readonly property color textSoft: highContrast ? highContrastText : mixColor(skin.content, skin.text, 0.84)
    readonly property color textSubtle: textMuted

    // Brand purple stays the default; adopting a theme accent is explicit.
    readonly property bool ztermyAccent: accentPreference === "ztermy" || (accentPreference === "theme" && !terminalAccentHint)
    readonly property color accentBase: accentPreference === "system" ? systemAccent : accentPreference === "custom" ? customAccent : accentPreference === "theme" && terminalAccentHint ? terminalPalette.accent : (dark ? "#B59AE8" : "#7043A4")
    readonly property color accent: highContrast ? highContrastHighlight : accentBase
    readonly property color accentText: highContrast ? highContrastHighlightText : contrastText(accentBase)
    readonly property color accentHover: mixColor(accent, accentText, 0.14)
    readonly property color accentPressed: mixColor(accent, "#000000", 0.18)
    readonly property color focus: highContrast ? highContrastHighlight : accent
    readonly property color selectedBackground: highContrast ? mixColor(highContrastBackground, highContrastText, 0.22) : mixColor(skin.panel, accent, dark ? 0.17 : 0.12)
    readonly property color selectedHover: highContrast ? mixColor(highContrastBackground, highContrastText, 0.30) : mixColor(skin.panel, accent, dark ? 0.26 : 0.20)
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

    // Radius scale: small for chips and focus rings, compact for tab/pane
    // affordances, control for buttons/fields/menus, panel for cards and
    // dialogs. Dots and pills use height / 2 rather than a token.
    readonly property int radiusSmall: 4
    readonly property int radiusCompact: 6
    readonly property int radiusControl: 8
    readonly property int radiusPanel: 12
    // Elevation shadows (AppSurface). Light skins need far less weight
    // than dark ones to read as depth rather than dirt.
    readonly property color shadowColor: dark ? "#000000" : "#0F172A"
    readonly property real shadowOpacityFloating: dark ? 0.55 : 0.16
    readonly property real shadowOpacityDialog: dark ? 0.65 : 0.24
    readonly property real shadowBlurFloating: 0.5
    readonly property real shadowBlurDialog: 1.0
    readonly property int shadowOffsetFloating: 3
    readonly property int shadowOffsetDialog: 10
    // Motion durations and easings live in the Motion singleton.

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
