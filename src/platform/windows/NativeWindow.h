#pragma once

#include <QQuickView>
#include <qt_windows.h>

namespace ztermy
{

class NativeWindow final : public QQuickView
{
    Q_OBJECT
    Q_PROPERTY(bool maximized READ maximized NOTIFY maximizedChanged)
    Q_PROPERTY(bool maximizeButtonHovered READ maximizeButtonHovered NOTIFY maximizeButtonHoveredChanged)
    Q_PROPERTY(bool maximizeButtonPressed READ maximizeButtonPressed NOTIFY maximizeButtonPressedChanged)

public:
    explicit NativeWindow(QWindow *parent = nullptr);
    ~NativeWindow() override;

    [[nodiscard]] bool load();
    [[nodiscard]] bool maximized() const noexcept;
    [[nodiscard]] bool maximizeButtonHovered() const noexcept;
    [[nodiscard]] bool maximizeButtonPressed() const noexcept;

    Q_INVOKABLE void minimizeWindow();
    Q_INVOKABLE void toggleMaximize();
    Q_INVOKABLE void closeWindow();
    Q_INVOKABLE void setTitleBarMetrics(qreal titleHeight, qreal controlsLeft, qreal maximizeLeft, qreal maximizeWidth);

signals:
    void maximizedChanged();
    void maximizeButtonHoveredChanged();
    void maximizeButtonPressedChanged();

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
    void applyBackdrop();
    void setMaximizeButtonHovered(bool hovered);
    void setMaximizeButtonPressed(bool pressed);

    qreal m_titleHeight = 42.0;
    qreal m_controlsLeft = 0.0;
    qreal m_maximizeLeft = 0.0;
    qreal m_maximizeWidth = 46.0;
    bool m_maximizeButtonHovered = false;
    bool m_maximizeButtonPressed = false;
    HWND m_windowHandle = nullptr;
    WNDPROC m_originalWindowProcedure = nullptr;
};

} // namespace ztermy
