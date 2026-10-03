#include <QLoggingCategory>
#include "core/windowing/WindowPresenter.h"
#include "platform/windows/NativeWindow.h"

namespace
{
Q_LOGGING_CATEGORY(windowPreferencesLog, "ztermy.window")
}

namespace ztermy
{
void NativeWindow::refreshAnimationsEnabled()
{
    if (m_animationPreference.update(windowing::queryClientAreaAnimationsEnabled()))
    {
        qCInfo(windowPreferencesLog) << "Windows client-area animation preference changed"
                                     << "enabled=" << m_animationPreference.enabled();
        emit animationsEnabledChanged();
    }
}

void NativeWindow::refreshHighContrast()
{
    const auto updated = windowing::queryHighContrastState();
    if (!updated || *updated == m_highContrastState)
    {
        return;
    }
    const bool animationsChanged = updated->enabled != m_highContrastState.enabled;
    m_highContrastState = *updated;
    qCInfo(windowPreferencesLog) << "Windows high-contrast state changed" << "enabled=" << m_highContrastState.enabled;
    emit highContrastChanged();
    if (animationsChanged)
    {
        emit animationsEnabledChanged();
    }
    (void)applyBackdrop();
}

void NativeWindow::updateSystemAccentColor(const windowing::RgbColor color)
{
    const QColor updated(color.red, color.green, color.blue);
    if (updated == m_systemAccentColor)
    {
        return;
    }
    m_systemAccentColor = updated;
    emit systemAccentColorChanged();
}

void NativeWindow::setMaximizeButtonHovered(const bool hovered)
{
    if (m_maximizeButtonHovered == hovered)
    {
        return;
    }
    m_maximizeButtonHovered = hovered;
    qCDebug(windowPreferencesLog) << "maximize hover=" << hovered;
    emit maximizeButtonHoveredChanged();
}

void NativeWindow::setMaximizeButtonPressed(const bool pressed)
{
    if (m_maximizeButtonPressed == pressed)
    {
        return;
    }
    m_maximizeButtonPressed = pressed;
    emit maximizeButtonPressedChanged();
}

} // namespace ztermy
