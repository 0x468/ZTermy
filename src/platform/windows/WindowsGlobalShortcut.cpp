#include "platform/windows/WindowsGlobalShortcut.h"

#include <QCoreApplication>
#include <QKeySequence>
#include <qt_windows.h>
#include <utility>

namespace ztermy::windowing
{
WindowsGlobalShortcut::WindowsGlobalShortcut(QWindow *window) : m_window(window)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

WindowsGlobalShortcut::~WindowsGlobalShortcut()
{
    QCoreApplication::instance()->removeNativeEventFilter(this);
    if (m_window && m_registeredId != 0)
    {
        const auto handle = reinterpret_cast<HWND>(m_window->winId()); // NOLINT(performance-no-int-to-ptr)
        UnregisterHotKey(handle, m_registeredId);
    }
}

std::expected<GlobalShortcutKey, QString> WindowsGlobalShortcut::parse(const QString &text)
{
    if (text.trimmed().isEmpty())
        return GlobalShortcutKey{};
    const auto sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
    if (sequence.count() != 1 || sequence[0].key() == Qt::Key_unknown)
        return std::unexpected(tr("Use a single shortcut, for example Ctrl+Alt+Space."));
    const auto key = sequence[0].key();
    const auto modifiers = sequence[0].keyboardModifiers();
    GlobalShortcutKey result{.portableText = sequence.toString(QKeySequence::PortableText)};
    if (modifiers.testFlag(Qt::ControlModifier))
        result.modifiers |= MOD_CONTROL;
    if (modifiers.testFlag(Qt::AltModifier))
        result.modifiers |= MOD_ALT;
    if (modifiers.testFlag(Qt::ShiftModifier))
        result.modifiers |= MOD_SHIFT;
    if (modifiers.testFlag(Qt::MetaModifier))
        result.modifiers |= MOD_WIN;
    if ((result.modifiers & (MOD_CONTROL | MOD_ALT | MOD_WIN)) == 0)
        return std::unexpected(tr("Include Ctrl, Alt or Win so ordinary typing is not intercepted."));
    if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9))
        result.virtualKey = static_cast<unsigned int>(key);
    else if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        result.virtualKey = VK_F1 + static_cast<unsigned int>(key - Qt::Key_F1);
    else
    {
        switch (key)
        {
            case Qt::Key_Space:
                result.virtualKey = VK_SPACE;
                break;
            case Qt::Key_Tab:
                result.virtualKey = VK_TAB;
                break;
            case Qt::Key_Escape:
                result.virtualKey = VK_ESCAPE;
                break;
            case Qt::Key_Return:
                result.virtualKey = VK_RETURN;
                break;
            case Qt::Key_Insert:
                result.virtualKey = VK_INSERT;
                break;
            case Qt::Key_Delete:
                result.virtualKey = VK_DELETE;
                break;
            case Qt::Key_Home:
                result.virtualKey = VK_HOME;
                break;
            case Qt::Key_End:
                result.virtualKey = VK_END;
                break;
            case Qt::Key_PageUp:
                result.virtualKey = VK_PRIOR;
                break;
            case Qt::Key_PageDown:
                result.virtualKey = VK_NEXT;
                break;
            case Qt::Key_Left:
                result.virtualKey = VK_LEFT;
                break;
            case Qt::Key_Right:
                result.virtualKey = VK_RIGHT;
                break;
            case Qt::Key_Up:
                result.virtualKey = VK_UP;
                break;
            case Qt::Key_Down:
                result.virtualKey = VK_DOWN;
                break;
            default:
                if (key > 0 && key < 0x10000)
                {
                    const SHORT mapped = VkKeyScanW(static_cast<WCHAR>(key));
                    if (mapped != -1 && (mapped & 0x0600) == 0)
                    {
                        result.virtualKey = static_cast<unsigned int>(mapped & 0x00FF);
                        if ((mapped & 0x0100) != 0)
                            result.modifiers |= MOD_SHIFT;
                    }
                }
                break;
        }
    }
    if (result.virtualKey == 0)
        return std::unexpected(tr("This key cannot be registered as a global shortcut."));
    return result;
}

QString WindowsGlobalShortcut::configure(const QString &text)
{
    const auto key = parse(text);
    if (!key)
        return key.error();
    if (key->portableText == m_shortcut)
        return {};
    if (!m_window)
        return tr("The main window is unavailable.");
    const auto handle = reinterpret_cast<HWND>(m_window->winId()); // NOLINT(performance-no-int-to-ptr)
    const int nextId = m_registeredId == 0x5A70 ? 0x5A71 : 0x5A70;
    if (key->virtualKey != 0 && !RegisterHotKey(handle, nextId, key->modifiers | MOD_NOREPEAT, key->virtualKey))
        return tr("The global shortcut is already in use or reserved by Windows. Choose another shortcut.");
    // Acquire the replacement first; a conflict must not disable the old binding.
    if (m_registeredId != 0)
        UnregisterHotKey(handle, m_registeredId);
    m_registeredId = key->virtualKey == 0 ? 0 : nextId;
    m_shortcut = key->portableText;
    return {};
}

bool WindowsGlobalShortcut::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(eventType)
    const auto *native = static_cast<MSG *>(message);
    if (m_window && m_registeredId != 0 && native->message == WM_HOTKEY
        && native->hwnd == reinterpret_cast<HWND>(m_window->winId()) // NOLINT(performance-no-int-to-ptr)
        && std::cmp_equal(native->wParam, m_registeredId))
    {
        emit activated();
        // Qt's dispatcher passes no result storage for queue-level hotkey messages.
        if (result != nullptr)
            *result = 0;
        return true;
    }
    return false;
}
} // namespace ztermy::windowing
