#include "platform/windows/WindowsTerminalInput.h"

#include <QKeyEvent>

#include <Windows.h>

#include <array>
#include <cstdint>
#include <string>

namespace
{

using ztermy::terminal::TerminalKey;

[[nodiscard]] bool keyDown(const int virtualKey) noexcept
{
    return (::GetKeyState(virtualKey) & 0x8000) != 0;
}

[[nodiscard]] bool keyToggled(const int virtualKey) noexcept
{
    return (::GetKeyState(virtualKey) & 0x0001) != 0;
}

[[nodiscard]] bool extendedVirtualKey(const quint32 virtualKey) noexcept
{
    switch (virtualKey)
    {
        case VK_RMENU:
        case VK_RCONTROL:
        case VK_INSERT:
        case VK_DELETE:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_NUMLOCK:
        case VK_DIVIDE:
        case VK_SNAPSHOT:
        case VK_LWIN:
        case VK_RWIN:
        case VK_APPS:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] TerminalKey keypadKey(const quint32 scanCode, const quint32 virtualKey) noexcept
{
    if (virtualKey == VK_DIVIDE)
    {
        return TerminalKey::numpadDivide;
    }
    if (virtualKey == VK_RETURN)
    {
        return TerminalKey::numpadEnter;
    }
    switch (scanCode & 0xFFU)
    {
        case 0x52:
            return TerminalKey::numpad0;
        case 0x4F:
            return TerminalKey::numpad1;
        case 0x50:
            return TerminalKey::numpad2;
        case 0x51:
            return TerminalKey::numpad3;
        case 0x4B:
            return TerminalKey::numpad4;
        case 0x4C:
            return TerminalKey::numpad5;
        case 0x4D:
            return TerminalKey::numpad6;
        case 0x47:
            return TerminalKey::numpad7;
        case 0x48:
            return TerminalKey::numpad8;
        case 0x49:
            return TerminalKey::numpad9;
        case 0x4E:
            return TerminalKey::numpadAdd;
        case 0x53:
            return TerminalKey::numpadDecimal;
        case 0x37:
            return TerminalKey::numpadMultiply;
        case 0x4A:
            return TerminalKey::numpadSubtract;
        default:
            return TerminalKey::unidentified;
    }
}

[[nodiscard]] TerminalKey physicalWritingKey(const quint32 scanCode) noexcept
{
    switch (scanCode & 0xFFU)
    {
        case 0x29:
            return TerminalKey::backquote;
        case 0x2B:
            return TerminalKey::backslash;
        case 0x1A:
            return TerminalKey::bracketLeft;
        case 0x1B:
            return TerminalKey::bracketRight;
        case 0x33:
            return TerminalKey::comma;
        case 0x0B:
            return TerminalKey::digit0;
        case 0x02:
            return TerminalKey::digit1;
        case 0x03:
            return TerminalKey::digit2;
        case 0x04:
            return TerminalKey::digit3;
        case 0x05:
            return TerminalKey::digit4;
        case 0x06:
            return TerminalKey::digit5;
        case 0x07:
            return TerminalKey::digit6;
        case 0x08:
            return TerminalKey::digit7;
        case 0x09:
            return TerminalKey::digit8;
        case 0x0A:
            return TerminalKey::digit9;
        case 0x0D:
            return TerminalKey::equal;
        case 0x56:
            return TerminalKey::intlBackslash;
        case 0x1E:
            return TerminalKey::keyA;
        case 0x30:
            return TerminalKey::keyB;
        case 0x2E:
            return TerminalKey::keyC;
        case 0x20:
            return TerminalKey::keyD;
        case 0x12:
            return TerminalKey::keyE;
        case 0x21:
            return TerminalKey::keyF;
        case 0x22:
            return TerminalKey::keyG;
        case 0x23:
            return TerminalKey::keyH;
        case 0x17:
            return TerminalKey::keyI;
        case 0x24:
            return TerminalKey::keyJ;
        case 0x25:
            return TerminalKey::keyK;
        case 0x26:
            return TerminalKey::keyL;
        case 0x32:
            return TerminalKey::keyM;
        case 0x31:
            return TerminalKey::keyN;
        case 0x18:
            return TerminalKey::keyO;
        case 0x19:
            return TerminalKey::keyP;
        case 0x10:
            return TerminalKey::keyQ;
        case 0x13:
            return TerminalKey::keyR;
        case 0x1F:
            return TerminalKey::keyS;
        case 0x14:
            return TerminalKey::keyT;
        case 0x16:
            return TerminalKey::keyU;
        case 0x2F:
            return TerminalKey::keyV;
        case 0x11:
            return TerminalKey::keyW;
        case 0x2D:
            return TerminalKey::keyX;
        case 0x15:
            return TerminalKey::keyY;
        case 0x2C:
            return TerminalKey::keyZ;
        case 0x0C:
            return TerminalKey::minus;
        case 0x34:
            return TerminalKey::period;
        case 0x28:
            return TerminalKey::quote;
        case 0x27:
            return TerminalKey::semicolon;
        case 0x35:
            return TerminalKey::slash;
        default:
            return TerminalKey::unidentified;
    }
}

[[nodiscard]] TerminalKey namedKey(const QKeyEvent &event) noexcept
{
    const quint32 scanCode = event.nativeScanCode();
    const quint32 virtualKey = event.nativeVirtualKey();
    const bool extended = extendedVirtualKey(virtualKey);

    if (event.modifiers().testFlag(Qt::KeypadModifier))
    {
        const TerminalKey keypad = keypadKey(scanCode, virtualKey);
        if (keypad != TerminalKey::unidentified)
        {
            return keypad;
        }
    }

    if (const TerminalKey writing = physicalWritingKey(scanCode); writing != TerminalKey::unidentified)
    {
        if (extended && (scanCode & 0xFFU) == 0x35U)
        {
            return TerminalKey::numpadDivide;
        }
        return writing;
    }

    // Synthetic Qt events (tests, accessibility tooling and some virtual
    // keyboards) do not carry a native scan code. Keep a logical fallback for
    // those events without using it for normal Windows keyboard input.
    if (scanCode == 0)
    {
        if (event.key() >= Qt::Key_A && event.key() <= Qt::Key_Z)
        {
            return static_cast<TerminalKey>(static_cast<int>(TerminalKey::keyA) + (event.key() - Qt::Key_A));
        }
        if (event.key() >= Qt::Key_0 && event.key() <= Qt::Key_9)
        {
            return static_cast<TerminalKey>(static_cast<int>(TerminalKey::digit0) + (event.key() - Qt::Key_0));
        }
        switch (event.key())
        {
            case Qt::Key_QuoteLeft:
                return TerminalKey::backquote;
            case Qt::Key_Backslash:
                return TerminalKey::backslash;
            case Qt::Key_BracketLeft:
                return TerminalKey::bracketLeft;
            case Qt::Key_BracketRight:
                return TerminalKey::bracketRight;
            case Qt::Key_Comma:
                return TerminalKey::comma;
            case Qt::Key_Equal:
                return TerminalKey::equal;
            case Qt::Key_Minus:
                return TerminalKey::minus;
            case Qt::Key_Period:
                return TerminalKey::period;
            case Qt::Key_Apostrophe:
                return TerminalKey::quote;
            case Qt::Key_Semicolon:
                return TerminalKey::semicolon;
            case Qt::Key_Slash:
                return TerminalKey::slash;
            default:
                break;
        }
    }

    if (event.key() >= Qt::Key_F1 && event.key() <= Qt::Key_F24)
    {
        return static_cast<TerminalKey>(static_cast<int>(TerminalKey::f1) + (event.key() - Qt::Key_F1));
    }

    switch (event.key())
    {
        case Qt::Key_Alt:
        case Qt::Key_AltGr:
            return virtualKey == VK_RMENU || event.key() == Qt::Key_AltGr ? TerminalKey::altRight
                                                                          : TerminalKey::altLeft;
        case Qt::Key_Backspace:
            return TerminalKey::backspace;
        case Qt::Key_CapsLock:
            return TerminalKey::capsLock;
        case Qt::Key_Menu:
            return TerminalKey::contextMenu;
        case Qt::Key_Control:
            return virtualKey == VK_RCONTROL ? TerminalKey::controlRight : TerminalKey::controlLeft;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            return extended ? TerminalKey::numpadEnter : TerminalKey::enter;
        case Qt::Key_Meta:
            return virtualKey == VK_RWIN ? TerminalKey::metaRight : TerminalKey::metaLeft;
        case Qt::Key_Shift:
            return virtualKey == VK_RSHIFT || (scanCode & 0xFFU) == 0x36U ? TerminalKey::shiftRight
                                                                          : TerminalKey::shiftLeft;
        case Qt::Key_Space:
            return TerminalKey::space;
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            return TerminalKey::tab;
        case Qt::Key_Henkan:
            return TerminalKey::convert;
        case Qt::Key_Kana_Lock:
        case Qt::Key_Kana_Shift:
            return TerminalKey::kanaMode;
        case Qt::Key_Muhenkan:
            return TerminalKey::nonConvert;
        case Qt::Key_Delete:
            return TerminalKey::deleteKey;
        case Qt::Key_End:
            return TerminalKey::end;
        case Qt::Key_Help:
            return TerminalKey::help;
        case Qt::Key_Home:
            return TerminalKey::home;
        case Qt::Key_Insert:
            return TerminalKey::insert;
        case Qt::Key_PageDown:
            return TerminalKey::pageDown;
        case Qt::Key_PageUp:
            return TerminalKey::pageUp;
        case Qt::Key_Down:
            return TerminalKey::arrowDown;
        case Qt::Key_Left:
            return TerminalKey::arrowLeft;
        case Qt::Key_Right:
            return TerminalKey::arrowRight;
        case Qt::Key_Up:
            return TerminalKey::arrowUp;
        case Qt::Key_NumLock:
            return TerminalKey::numLock;
        case Qt::Key_Escape:
            return TerminalKey::escape;
        case Qt::Key_Print:
            return TerminalKey::printScreen;
        case Qt::Key_ScrollLock:
            return TerminalKey::scrollLock;
        case Qt::Key_Pause:
            return TerminalKey::pause;
        default:
            return TerminalKey::unidentified;
    }
}

[[nodiscard]] std::string eventText(const QKeyEvent &event, const ztermy::terminal::TerminalKeyAction action)
{
    if (action == ztermy::terminal::TerminalKeyAction::release)
    {
        return {};
    }
    const QString text = event.text();
    for (const QChar character : text)
    {
        if (character.unicode() < 0x20U || character.unicode() == 0x7FU
            || (character.unicode() >= 0xE000U && character.unicode() <= 0xF8FFU))
        {
            return {};
        }
    }
    return text.toUtf8().toStdString();
}

[[nodiscard]] std::uint32_t unshiftedCodepoint(const QKeyEvent &event) noexcept
{
    if (event.key() >= Qt::Key_A && event.key() <= Qt::Key_Z)
    {
        return static_cast<std::uint32_t>('a' + (event.key() - Qt::Key_A));
    }
    if (event.key() >= Qt::Key_0 && event.key() <= Qt::Key_9)
    {
        return static_cast<std::uint32_t>('0' + (event.key() - Qt::Key_0));
    }
    return event.key() == Qt::Key_Space ? static_cast<std::uint32_t>(' ') : 0U;
}

} // namespace

namespace ztermy::platform::windows
{

terminal::TerminalModifiers terminalModifiers(const Qt::KeyboardModifiers modifiers) noexcept
{
    terminal::TerminalModifiers result = 0;
    if (modifiers.testFlag(Qt::ShiftModifier))
    {
        result |= terminal::terminalModifierShift;
        if (keyDown(VK_RSHIFT))
        {
            result |= terminal::terminalModifierShiftSide;
        }
    }
    if (modifiers.testFlag(Qt::ControlModifier))
    {
        result |= terminal::terminalModifierControl;
        if (keyDown(VK_RCONTROL))
        {
            result |= terminal::terminalModifierControlSide;
        }
    }
    if (modifiers.testFlag(Qt::AltModifier))
    {
        result |= terminal::terminalModifierAlt;
        if (keyDown(VK_RMENU))
        {
            result |= terminal::terminalModifierAltSide;
        }
    }
    if (modifiers.testFlag(Qt::MetaModifier))
    {
        result |= terminal::terminalModifierSuper;
        if (keyDown(VK_RWIN))
        {
            result |= terminal::terminalModifierSuperSide;
        }
    }
    if (keyToggled(VK_CAPITAL))
    {
        result |= terminal::terminalModifierCapsLock;
    }
    if (keyToggled(VK_NUMLOCK))
    {
        result |= terminal::terminalModifierNumLock;
    }
    return result;
}

terminal::TerminalKeyEvent terminalKeyEvent(const QKeyEvent &event, const terminal::TerminalKeyAction action,
                                            const bool composing)
{
    const std::string text = eventText(event, action);
    const terminal::TerminalModifiers modifiers = terminalModifiers(event.modifiers());
    terminal::TerminalModifiers consumedModifiers = 0;
    const bool altGrText = !text.empty() && (modifiers & terminal::terminalModifierControl) != 0
                           && (modifiers & terminal::terminalModifierAlt) != 0;
    if (altGrText)
    {
        consumedModifiers = terminal::terminalModifierControl | terminal::terminalModifierAlt;
    }
    return {.action = action,
            .key = namedKey(event),
            .modifiers = modifiers,
            .consumedModifiers = consumedModifiers,
            .text = text,
            .unshiftedCodepoint = unshiftedCodepoint(event),
            .composing = composing};
}

} // namespace ztermy::platform::windows
