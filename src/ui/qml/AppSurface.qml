pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects

// Elevation ladder (ADR 0120). Every opaque panel, card, popup, menu, toast
// and dialog is an AppSurface; nothing else paints its own shadow.
//   0  panel: navigation rails, docked side panels (flat, hairline)
//   1  card: grouped settings, editors, state panels (hairline)
//   2  floating: menus, tool tips, popovers, toasts, drawers, drag previews
//   3  dialog: modal dialogs and the command palette
Rectangle {
    id: surface

    property int elevation: 1
    property bool compact: false
    property bool shadow: elevation >= 2
    readonly property bool floating: elevation >= 2
    readonly property bool shadowVisible: shadow && Theme.shadowsEnabled

    radius: compact ? Theme.radiusControl : Theme.radiusPanel
    color: elevation <= 0 ? Theme.panelBackground : elevation === 1 ? Theme.elevatedBackground : Theme.floatingBackground
    border.color: floating ? Theme.borderStrong : Theme.border
    border.width: 1

    // The shadow is cast by a plain rectangle that the surface fully covers,
    // so content changes never re-render an offscreen texture.
    Loader {
        z: -1
        anchors.fill: parent
        active: surface.shadowVisible
        sourceComponent: Item {
            Rectangle {
                id: caster

                anchors.fill: parent
                radius: surface.radius
                color: surface.color
            }

            MultiEffect {
                anchors.fill: caster
                source: caster
                shadowEnabled: true
                shadowColor: Theme.shadowColor
                shadowOpacity: surface.elevation >= 3 ? Theme.shadowOpacityDialog : Theme.shadowOpacityFloating
                shadowBlur: surface.elevation >= 3 ? Theme.shadowBlurDialog : Theme.shadowBlurFloating
                shadowVerticalOffset: surface.elevation >= 3 ? Theme.shadowOffsetDialog : Theme.shadowOffsetFloating
                autoPaddingEnabled: true
            }
        }
    }
}
