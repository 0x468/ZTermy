#pragma once

#include <cstdint>
#include <string>

namespace ztermy::terminal
{

enum class TerminalKeyAction : std::uint8_t
{
    release,
    press,
    repeat,
};

enum class TerminalKey : std::uint8_t
{
    unidentified,
    backquote,
    backslash,
    bracketLeft,
    bracketRight,
    comma,
    digit0,
    digit1,
    digit2,
    digit3,
    digit4,
    digit5,
    digit6,
    digit7,
    digit8,
    digit9,
    equal,
    intlBackslash,
    intlRo,
    intlYen,
    keyA,
    keyB,
    keyC,
    keyD,
    keyE,
    keyF,
    keyG,
    keyH,
    keyI,
    keyJ,
    keyK,
    keyL,
    keyM,
    keyN,
    keyO,
    keyP,
    keyQ,
    keyR,
    keyS,
    keyT,
    keyU,
    keyV,
    keyW,
    keyX,
    keyY,
    keyZ,
    minus,
    period,
    quote,
    semicolon,
    slash,
    altLeft,
    altRight,
    backspace,
    capsLock,
    contextMenu,
    controlLeft,
    controlRight,
    enter,
    metaLeft,
    metaRight,
    shiftLeft,
    shiftRight,
    space,
    tab,
    convert,
    kanaMode,
    nonConvert,
    deleteKey,
    end,
    help,
    home,
    insert,
    pageDown,
    pageUp,
    arrowDown,
    arrowLeft,
    arrowRight,
    arrowUp,
    numLock,
    numpad0,
    numpad1,
    numpad2,
    numpad3,
    numpad4,
    numpad5,
    numpad6,
    numpad7,
    numpad8,
    numpad9,
    numpadAdd,
    numpadBackspace,
    numpadClear,
    numpadDecimal,
    numpadDivide,
    numpadEnter,
    numpadEqual,
    numpadMultiply,
    numpadSubtract,
    numpadSeparator,
    numpadUp,
    numpadDown,
    numpadRight,
    numpadLeft,
    numpadBegin,
    numpadHome,
    numpadEnd,
    numpadInsert,
    numpadDelete,
    numpadPageUp,
    numpadPageDown,
    escape,
    f1,
    f2,
    f3,
    f4,
    f5,
    f6,
    f7,
    f8,
    f9,
    f10,
    f11,
    f12,
    f13,
    f14,
    f15,
    f16,
    f17,
    f18,
    f19,
    f20,
    f21,
    f22,
    f23,
    f24,
    printScreen,
    scrollLock,
    pause,
};

using TerminalModifiers = std::uint16_t;
inline constexpr TerminalModifiers terminalModifierShift = 1U << 0U;
inline constexpr TerminalModifiers terminalModifierControl = 1U << 1U;
inline constexpr TerminalModifiers terminalModifierAlt = 1U << 2U;
inline constexpr TerminalModifiers terminalModifierSuper = 1U << 3U;
inline constexpr TerminalModifiers terminalModifierCapsLock = 1U << 4U;
inline constexpr TerminalModifiers terminalModifierNumLock = 1U << 5U;
inline constexpr TerminalModifiers terminalModifierShiftSide = 1U << 6U;
inline constexpr TerminalModifiers terminalModifierControlSide = 1U << 7U;
inline constexpr TerminalModifiers terminalModifierAltSide = 1U << 8U;
inline constexpr TerminalModifiers terminalModifierSuperSide = 1U << 9U;

struct TerminalKeyEvent final
{
    TerminalKeyAction action = TerminalKeyAction::press;
    TerminalKey key = TerminalKey::unidentified;
    TerminalModifiers modifiers = 0;
    TerminalModifiers consumedModifiers = 0;
    std::string text;
    std::uint32_t unshiftedCodepoint = 0;
    bool composing = false;
};

enum class TerminalMouseAction : std::uint8_t
{
    press,
    release,
    motion,
};

enum class TerminalMouseButton : std::uint8_t
{
    none,
    left,
    right,
    middle,
    four,
    five,
    six,
    seven,
    eight,
    nine,
    ten,
    eleven,
};

struct TerminalMouseEvent final
{
    TerminalMouseAction action = TerminalMouseAction::motion;
    TerminalMouseButton button = TerminalMouseButton::none;
    TerminalModifiers modifiers = 0;
    double positionX = 0.0;
    double positionY = 0.0;
    std::uint32_t screenWidthPixels = 0;
    std::uint32_t screenHeightPixels = 0;
    std::uint32_t cellWidthPixels = 0;
    std::uint32_t cellHeightPixels = 0;
    std::uint32_t paddingTopPixels = 0;
    std::uint32_t paddingBottomPixels = 0;
    std::uint32_t paddingRightPixels = 0;
    std::uint32_t paddingLeftPixels = 0;
    bool anyButtonPressed = false;
};

} // namespace ztermy::terminal
