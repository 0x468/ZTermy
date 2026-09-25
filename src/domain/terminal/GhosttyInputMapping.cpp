#include "domain/terminal/GhosttyInputMapping.h"

namespace ztermy::terminal::detail
{

[[nodiscard]] GhosttyKey ghosttyKey(const ztermy::terminal::TerminalKey key) noexcept
{
    using ztermy::terminal::TerminalKey;
    if (key >= TerminalKey::digit0 && key <= TerminalKey::digit9)
    {
        return static_cast<GhosttyKey>(static_cast<int>(GHOSTTY_KEY_DIGIT_0)
                                       + (static_cast<int>(key) - static_cast<int>(TerminalKey::digit0)));
    }
    if (key >= TerminalKey::keyA && key <= TerminalKey::keyZ)
    {
        return static_cast<GhosttyKey>(static_cast<int>(GHOSTTY_KEY_A)
                                       + (static_cast<int>(key) - static_cast<int>(TerminalKey::keyA)));
    }
    if (key >= TerminalKey::numpad0 && key <= TerminalKey::numpad9)
    {
        return static_cast<GhosttyKey>(static_cast<int>(GHOSTTY_KEY_NUMPAD_0)
                                       + (static_cast<int>(key) - static_cast<int>(TerminalKey::numpad0)));
    }
    if (key >= TerminalKey::f1 && key <= TerminalKey::f24)
    {
        return static_cast<GhosttyKey>(static_cast<int>(GHOSTTY_KEY_F1)
                                       + (static_cast<int>(key) - static_cast<int>(TerminalKey::f1)));
    }

    switch (key)
    {
        case TerminalKey::backquote:
            return GHOSTTY_KEY_BACKQUOTE;
        case TerminalKey::backslash:
            return GHOSTTY_KEY_BACKSLASH;
        case TerminalKey::bracketLeft:
            return GHOSTTY_KEY_BRACKET_LEFT;
        case TerminalKey::bracketRight:
            return GHOSTTY_KEY_BRACKET_RIGHT;
        case TerminalKey::comma:
            return GHOSTTY_KEY_COMMA;
        case TerminalKey::equal:
            return GHOSTTY_KEY_EQUAL;
        case TerminalKey::intlBackslash:
            return GHOSTTY_KEY_INTL_BACKSLASH;
        case TerminalKey::intlRo:
            return GHOSTTY_KEY_INTL_RO;
        case TerminalKey::intlYen:
            return GHOSTTY_KEY_INTL_YEN;
        case TerminalKey::minus:
            return GHOSTTY_KEY_MINUS;
        case TerminalKey::period:
            return GHOSTTY_KEY_PERIOD;
        case TerminalKey::quote:
            return GHOSTTY_KEY_QUOTE;
        case TerminalKey::semicolon:
            return GHOSTTY_KEY_SEMICOLON;
        case TerminalKey::slash:
            return GHOSTTY_KEY_SLASH;
        case TerminalKey::altLeft:
            return GHOSTTY_KEY_ALT_LEFT;
        case TerminalKey::altRight:
            return GHOSTTY_KEY_ALT_RIGHT;
        case TerminalKey::backspace:
            return GHOSTTY_KEY_BACKSPACE;
        case TerminalKey::capsLock:
            return GHOSTTY_KEY_CAPS_LOCK;
        case TerminalKey::contextMenu:
            return GHOSTTY_KEY_CONTEXT_MENU;
        case TerminalKey::controlLeft:
            return GHOSTTY_KEY_CONTROL_LEFT;
        case TerminalKey::controlRight:
            return GHOSTTY_KEY_CONTROL_RIGHT;
        case TerminalKey::enter:
            return GHOSTTY_KEY_ENTER;
        case TerminalKey::metaLeft:
            return GHOSTTY_KEY_META_LEFT;
        case TerminalKey::metaRight:
            return GHOSTTY_KEY_META_RIGHT;
        case TerminalKey::shiftLeft:
            return GHOSTTY_KEY_SHIFT_LEFT;
        case TerminalKey::shiftRight:
            return GHOSTTY_KEY_SHIFT_RIGHT;
        case TerminalKey::space:
            return GHOSTTY_KEY_SPACE;
        case TerminalKey::tab:
            return GHOSTTY_KEY_TAB;
        case TerminalKey::convert:
            return GHOSTTY_KEY_CONVERT;
        case TerminalKey::kanaMode:
            return GHOSTTY_KEY_KANA_MODE;
        case TerminalKey::nonConvert:
            return GHOSTTY_KEY_NON_CONVERT;
        case TerminalKey::deleteKey:
            return GHOSTTY_KEY_DELETE;
        case TerminalKey::end:
            return GHOSTTY_KEY_END;
        case TerminalKey::help:
            return GHOSTTY_KEY_HELP;
        case TerminalKey::home:
            return GHOSTTY_KEY_HOME;
        case TerminalKey::insert:
            return GHOSTTY_KEY_INSERT;
        case TerminalKey::pageDown:
            return GHOSTTY_KEY_PAGE_DOWN;
        case TerminalKey::pageUp:
            return GHOSTTY_KEY_PAGE_UP;
        case TerminalKey::arrowDown:
            return GHOSTTY_KEY_ARROW_DOWN;
        case TerminalKey::arrowLeft:
            return GHOSTTY_KEY_ARROW_LEFT;
        case TerminalKey::arrowRight:
            return GHOSTTY_KEY_ARROW_RIGHT;
        case TerminalKey::arrowUp:
            return GHOSTTY_KEY_ARROW_UP;
        case TerminalKey::numLock:
            return GHOSTTY_KEY_NUM_LOCK;
        case TerminalKey::numpadAdd:
            return GHOSTTY_KEY_NUMPAD_ADD;
        case TerminalKey::numpadBackspace:
            return GHOSTTY_KEY_NUMPAD_BACKSPACE;
        case TerminalKey::numpadClear:
            return GHOSTTY_KEY_NUMPAD_CLEAR;
        case TerminalKey::numpadDecimal:
            return GHOSTTY_KEY_NUMPAD_DECIMAL;
        case TerminalKey::numpadDivide:
            return GHOSTTY_KEY_NUMPAD_DIVIDE;
        case TerminalKey::numpadEnter:
            return GHOSTTY_KEY_NUMPAD_ENTER;
        case TerminalKey::numpadEqual:
            return GHOSTTY_KEY_NUMPAD_EQUAL;
        case TerminalKey::numpadMultiply:
            return GHOSTTY_KEY_NUMPAD_MULTIPLY;
        case TerminalKey::numpadSubtract:
            return GHOSTTY_KEY_NUMPAD_SUBTRACT;
        case TerminalKey::numpadSeparator:
            return GHOSTTY_KEY_NUMPAD_SEPARATOR;
        case TerminalKey::numpadUp:
            return GHOSTTY_KEY_NUMPAD_UP;
        case TerminalKey::numpadDown:
            return GHOSTTY_KEY_NUMPAD_DOWN;
        case TerminalKey::numpadRight:
            return GHOSTTY_KEY_NUMPAD_RIGHT;
        case TerminalKey::numpadLeft:
            return GHOSTTY_KEY_NUMPAD_LEFT;
        case TerminalKey::numpadBegin:
            return GHOSTTY_KEY_NUMPAD_BEGIN;
        case TerminalKey::numpadHome:
            return GHOSTTY_KEY_NUMPAD_HOME;
        case TerminalKey::numpadEnd:
            return GHOSTTY_KEY_NUMPAD_END;
        case TerminalKey::numpadInsert:
            return GHOSTTY_KEY_NUMPAD_INSERT;
        case TerminalKey::numpadDelete:
            return GHOSTTY_KEY_NUMPAD_DELETE;
        case TerminalKey::numpadPageUp:
            return GHOSTTY_KEY_NUMPAD_PAGE_UP;
        case TerminalKey::numpadPageDown:
            return GHOSTTY_KEY_NUMPAD_PAGE_DOWN;
        case TerminalKey::escape:
            return GHOSTTY_KEY_ESCAPE;
        case TerminalKey::printScreen:
            return GHOSTTY_KEY_PRINT_SCREEN;
        case TerminalKey::scrollLock:
            return GHOSTTY_KEY_SCROLL_LOCK;
        case TerminalKey::pause:
            return GHOSTTY_KEY_PAUSE;
        case TerminalKey::unidentified:
        default:
            return GHOSTTY_KEY_UNIDENTIFIED;
    }
}

[[nodiscard]] GhosttyKeyAction ghosttyKeyAction(const ztermy::terminal::TerminalKeyAction action) noexcept
{
    using ztermy::terminal::TerminalKeyAction;
    switch (action)
    {
        case TerminalKeyAction::release:
            return GHOSTTY_KEY_ACTION_RELEASE;
        case TerminalKeyAction::repeat:
            return GHOSTTY_KEY_ACTION_REPEAT;
        case TerminalKeyAction::press:
        default:
            return GHOSTTY_KEY_ACTION_PRESS;
    }
}

[[nodiscard]] GhosttyMouseAction ghosttyMouseAction(const ztermy::terminal::TerminalMouseAction action) noexcept
{
    using ztermy::terminal::TerminalMouseAction;
    switch (action)
    {
        case TerminalMouseAction::press:
            return GHOSTTY_MOUSE_ACTION_PRESS;
        case TerminalMouseAction::release:
            return GHOSTTY_MOUSE_ACTION_RELEASE;
        case TerminalMouseAction::motion:
        default:
            return GHOSTTY_MOUSE_ACTION_MOTION;
    }
}

[[nodiscard]] GhosttyMouseButton ghosttyMouseButton(const ztermy::terminal::TerminalMouseButton button) noexcept
{
    using ztermy::terminal::TerminalMouseButton;
    if (button == TerminalMouseButton::none)
    {
        return GHOSTTY_MOUSE_BUTTON_UNKNOWN;
    }
    return static_cast<GhosttyMouseButton>(static_cast<int>(GHOSTTY_MOUSE_BUTTON_LEFT)
                                           + (static_cast<int>(button) - static_cast<int>(TerminalMouseButton::left)));
}

} // namespace ztermy::terminal::detail
