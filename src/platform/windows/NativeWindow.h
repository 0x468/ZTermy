#pragma once

#include "platform/windows/WindowsUiSettings.h"

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
    Q_PROPERTY(bool systemDarkMode READ systemDarkMode NOTIFY systemDarkModeChanged)
    Q_PROPERTY(bool animationsEnabled READ animationsEnabled NOTIFY animationsEnabledChanged)

public:
    explicit NativeWindow(QWindow *parent = nullptr);
    ~NativeWindow() override;

    [[nodiscard]] bool load(QVariantMap initialProperties = {});
    [[nodiscard]] bool maximized() const noexcept;
    [[nodiscard]] bool maximizeButtonHovered() const noexcept;
    [[nodiscard]] bool maximizeButtonPressed() const noexcept;
    [[nodiscard]] bool systemDarkMode() const noexcept;
    [[nodiscard]] bool animationsEnabled() const noexcept;
    [[nodiscard]] bool maximizedClientMatchesWorkArea() const noexcept;

    Q_INVOKABLE void minimizeWindow();
    Q_INVOKABLE void toggleMaximize();
    Q_INVOKABLE void closeWindow();
    Q_INVOKABLE bool applyAppearance(const QString &backdropPreference, bool darkMode);
    Q_INVOKABLE void setTitleBarMetrics(qreal titleHeight, qreal captionLeft, qreal controlsLeft, qreal maximizeLeft,
                                        qreal maximizeWidth);

signals:
    void maximizedChanged();
    void maximizeButtonHoveredChanged();
    void maximizeButtonPressedChanged();
    void systemDarkModeChanged();
    void animationsEnabledChanged();

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
    void refreshAnimationsEnabled();
    void setMaximizeButtonHovered(bool hovered);
    void setMaximizeButtonPressed(bool pressed);

    qreal m_titleHeight = 42.0;
    qreal m_captionLeft = 0.0;
    qreal m_controlsLeft = 0.0;
    qreal m_maximizeLeft = 0.0;
    qreal m_maximizeWidth = 46.0;
    bool m_maximizeButtonHovered = false;
    bool m_maximizeButtonPressed = false;
    QString m_backdropPreference = QStringLiteral("acrylic");
    bool m_darkMode = true;
    windowing::ClientAreaAnimationPreference m_animationPreference;
    HWND m_windowHandle = nullptr;
    WNDPROC m_originalWindowProcedure = nullptr;
};

} // namespace ztermy
