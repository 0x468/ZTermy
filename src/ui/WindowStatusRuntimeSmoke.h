#pragma once

class QQuickWindow;
namespace ztermy
{
class NativeWindow;
namespace ui
{
[[nodiscard]] bool verifyTrayExit(NativeWindow &window);
[[nodiscard]] bool verifyWindowStatusPresentation(NativeWindow &main, QQuickWindow &detached);
[[nodiscard]] bool captureNativeSnapFlyout(QQuickWindow &detached);
} // namespace ui
} // namespace ztermy
