#pragma once

#include "platform/windows/WindowsUiSettings.h"

#include <QColor>
#include <QQuickView>
#include <QVariantMap>
#include <qt_windows.h>

namespace ztermy
{

class NativeWindow final : public QQuickView
{
    Q_OBJECT
    Q_PROPERTY(bool maximized READ maximized NOTIFY maximizedChanged)
    Q_PROPERTY(bool maximizeButtonHovered READ maximizeButtonHovered NOTIFY maximizeButtonHoveredChanged)
    Q_PROPERTY(bool maximizeButtonPressed READ maximizeButtonPressed NOTIFY maximizeButtonPressedChanged)
    Q_PROPERTY(bool alwaysOnTop READ alwaysOnTop NOTIFY alwaysOnTopChanged)
    Q_PROPERTY(bool systemDarkMode READ systemDarkMode NOTIFY systemDarkModeChanged)
    Q_PROPERTY(QColor systemAccentColor READ systemAccentColor NOTIFY systemAccentColorChanged)
    Q_PROPERTY(bool animationsEnabled READ animationsEnabled NOTIFY animationsEnabledChanged)
    Q_PROPERTY(bool performanceModeActive READ performanceModeActive CONSTANT)
    Q_PROPERTY(bool opaqueSurface READ opaqueSurface CONSTANT)
    Q_PROPERTY(bool highContrast READ highContrast NOTIFY highContrastChanged)
    Q_PROPERTY(QColor highContrastBackground READ highContrastBackground NOTIFY highContrastChanged)
    Q_PROPERTY(QColor highContrastText READ highContrastText NOTIFY highContrastChanged)
    Q_PROPERTY(QColor highContrastHighlight READ highContrastHighlight NOTIFY highContrastChanged)
    Q_PROPERTY(QColor highContrastHighlightText READ highContrastHighlightText NOTIFY highContrastChanged)

public:
    explicit NativeWindow(bool performanceMode = false, bool opaqueSurface = false, QWindow *parent = nullptr);
    ~NativeWindow() override;

    [[nodiscard]] bool load(QVariantMap initialProperties = {});
    void releaseResources();
    [[nodiscard]] bool maximized() const noexcept;
    [[nodiscard]] bool maximizeButtonHovered() const noexcept;
    [[nodiscard]] bool maximizeButtonPressed() const noexcept;
    [[nodiscard]] bool alwaysOnTop() const noexcept;
    [[nodiscard]] bool systemDarkMode() const noexcept;
    [[nodiscard]] QColor systemAccentColor() const noexcept;
    [[nodiscard]] bool animationsEnabled() const noexcept;
    [[nodiscard]] bool performanceModeActive() const noexcept;
    [[nodiscard]] bool opaqueSurface() const noexcept;
    [[nodiscard]] bool highContrast() const noexcept;
    [[nodiscard]] bool closeToTrayEnabled() const noexcept;
    [[nodiscard]] bool trayIconVisible() const noexcept;
    [[nodiscard]] QColor highContrastBackground() const noexcept;
    [[nodiscard]] QColor highContrastText() const noexcept;
    [[nodiscard]] QColor highContrastHighlight() const noexcept;
    [[nodiscard]] QColor highContrastHighlightText() const noexcept;
    [[nodiscard]] bool maximizedClientMatchesWorkArea() const noexcept;

    Q_INVOKABLE void minimizeWindow();
    Q_INVOKABLE void toggleMaximize();
    Q_INVOKABLE void closeWindow();
    Q_INVOKABLE void setAlwaysOnTop(bool enabled);
    Q_INVOKABLE void toggleAlwaysOnTop();
    void setCloseToTrayEnabled(bool enabled);
    Q_INVOKABLE bool applyAppearance(const QString &backdropPreference, bool darkMode);
    Q_INVOKABLE void setTitleBarMetrics(qreal titleHeight, qreal captionLeft, qreal controlsLeft, qreal maximizeLeft,
                                        qreal maximizeWidth);

signals:
    void maximizedChanged();
    void maximizeButtonHoveredChanged();
    void maximizeButtonPressedChanged();
    void alwaysOnTopChanged();
    void systemDarkModeChanged();
    void systemAccentColorChanged();
    void animationsEnabledChanged();
    void highContrastChanged();

protected:
    bool event(QEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    static LRESULT CALLBACK windowProcedure(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam);

    [[nodiscard]] LRESULT nativeHitTest(HWND windowHandle, LPARAM lParam) const;
    [[nodiscard]] bool handleWindowProcedureMessage(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam,
                                                    LRESULT *result);
    void installWindowProcedure(HWND windowHandle);
    void uninstallWindowProcedure();
    void configureNativeWindow();
    [[nodiscard]] bool applyBackdrop();
    void updateTrayIcon();
    void removeTrayIcon() noexcept;
    void showTrayMenu();
    void restoreFromTray();
    void exitFromTray();
    void refreshAnimationsEnabled();
    void refreshHighContrast();
    void updateSystemAccentColor(windowing::RgbColor color);
    void setMaximizeButtonHovered(bool hovered);
    void setMaximizeButtonPressed(bool pressed);

    qreal m_titleHeight = 38.0;
    qreal m_captionLeft = 0.0;
    qreal m_controlsLeft = 0.0;
    qreal m_maximizeLeft = 0.0;
    qreal m_maximizeWidth = 46.0;
    bool m_maximizeButtonHovered = false;
    bool m_maximizeButtonPressed = false;
    bool m_alwaysOnTop = false;
    QString m_backdropPreference = QStringLiteral("acrylic");
    bool m_darkMode = true;
    bool m_closeToTrayEnabled = false;
    bool m_trayIconVisible = false;
    bool m_exitingFromTray = false;
    bool m_performanceMode = false;
    bool m_opaqueSurface = false;
    QColor m_systemAccentColor = QColor(QStringLiteral("#0078D4"));
    windowing::ClientAreaAnimationPreference m_animationPreference{true};
    windowing::HighContrastState m_highContrastState;
    HWND m_windowHandle = nullptr;
    WNDPROC m_originalWindowProcedure = nullptr;
};

} // namespace ztermy
