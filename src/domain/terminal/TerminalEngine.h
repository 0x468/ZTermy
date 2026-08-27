#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace ztermy::terminal
{

struct TerminalGeometry
{
    std::uint16_t columns = 80;
    std::uint16_t rows = 24;
    std::uint32_t cellWidthPixels = 0;
    std::uint32_t cellHeightPixels = 0;

    [[nodiscard]] bool valid() const noexcept { return columns > 0 && rows > 0; }
};

struct TerminalColor
{
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;

    friend bool operator==(const TerminalColor &, const TerminalColor &) = default;
};

struct TerminalCell
{
    std::u32string grapheme;
    TerminalColor foreground;
    TerminalColor background;
    bool explicitBackground = false;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    bool overline = false;
    bool invisible = false;
    bool selected = false;
    std::uint8_t displayWidth = 1;
    // Zero means no hyperlink. Non-zero values are one-based indexes into
    // TerminalSnapshot::hyperlinks so cells stay compact and URI storage is
    // shared by an immutable snapshot.
    std::uint32_t hyperlinkId = 0;

    friend bool operator==(const TerminalCell &, const TerminalCell &) = default;
};

enum class TerminalHyperlinkKind : std::uint8_t
{
    explicitOsc8,
    automaticUrl,
};

struct TerminalHyperlink final
{
    std::string uri;
    TerminalHyperlinkKind kind = TerminalHyperlinkKind::explicitOsc8;

    friend bool operator==(const TerminalHyperlink &, const TerminalHyperlink &) = default;
};

struct TerminalPoint
{
    std::uint16_t column = 0;
    std::uint16_t row = 0;

    friend bool operator==(const TerminalPoint &, const TerminalPoint &) = default;
};

struct TerminalSelection
{
    TerminalPoint start;
    TerminalPoint end;
    bool rectangular = false;
};

enum class TerminalSelectionGestureType : std::uint8_t
{
    press,
    drag,
    release,
    autoscrollTick,
    cancel,
};

// Platform-neutral pointer data consumed by the terminal worker. Coordinates
// and geometry share the same logical-pixel space; the engine uses them only
// to preserve terminal cell/word/line selection semantics.
struct TerminalSelectionGesture final
{
    TerminalSelectionGestureType type = TerminalSelectionGestureType::press;
    TerminalPoint point;
    double positionX = 0.0;
    double positionY = 0.0;
    std::uint32_t columns = 0;
    std::uint32_t cellWidthPixels = 0;
    std::uint32_t paddingLeftPixels = 0;
    std::uint32_t screenHeightPixels = 0;
    std::uint64_t eventTimeNanoseconds = 0;
    std::uint64_t repeatIntervalNanoseconds = 0;
    double repeatDistancePixels = 0.0;
    int scrollRows = 0;
    bool hasPoint = true;
    bool rectangular = false;
    std::u32string wordBoundaryCodepoints;
};

enum class TerminalCopyModeActionType : std::uint8_t
{
    begin,
    move,
    switchEndpoint,
    selectCharacter,
    selectLine,
    selectRectangle,
    cancel,
};

enum class TerminalCopyModeMotion : std::uint8_t
{
    left,
    right,
    up,
    down,
    wordLeft,
    wordRight,
    lineStart,
    lineEnd,
    pageUp,
    pageDown,
    top,
    bottom,
};

// Keyboard Copy Mode commands are interpreted by the terminal worker. The
// installed terminal selection owns tracked grid references, so output,
// scrollback pruning, and reflow cannot make a QML-side row index authoritative.
struct TerminalCopyModeAction final
{
    TerminalCopyModeActionType type = TerminalCopyModeActionType::begin;
    TerminalCopyModeMotion motion = TerminalCopyModeMotion::right;
    bool extend = false;
};

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

struct TerminalScrollbar
{
    std::uint64_t total = 0;
    std::uint64_t offset = 0;
    std::uint64_t visible = 0;
};

enum class TerminalSearchDirection : std::uint8_t
{
    forward,
    backward,
};

struct TerminalSearchResult
{
    std::uint32_t current = 0;
    std::uint32_t total = 0;
    bool wrapped = false;
};

enum class TerminalCursorStyle : std::uint8_t
{
    block,
    bar,
    underline,
    hollowBlock,
};

enum class TerminalDamageKind : std::uint8_t
{
    none,
    partial,
    full,
};

struct TerminalCursor
{
    std::uint16_t column = 0;
    std::uint16_t row = 0;
    std::uint8_t width = 1;
    TerminalCursorStyle style = TerminalCursorStyle::block;
    TerminalColor color;
    bool visible = false;
    bool blinking = false;
};

struct TerminalSnapshot
{
    std::uint16_t columns = 0;
    std::uint16_t rows = 0;
    TerminalColor defaultForeground;
    TerminalColor defaultBackground;
    TerminalCursor cursor;
    TerminalScrollbar scrollbar;
    bool selectionPresent = false;
    bool searchSelectionPresent = false;
    bool mouseTrackingActive = false;
    bool alternateScrollActive = false;
    bool focusReportingActive = false;
    TerminalDamageKind damage = TerminalDamageKind::full;
    std::vector<std::uint16_t> damagedRows;
    std::vector<TerminalCell> cells;
    std::vector<TerminalHyperlink> hyperlinks;
    // Raw value emitted by OSC 7 / OSC 9 / OSC 1337. Consumers must
    // validate and normalize it before treating it as a filesystem path.
    std::string workingDirectory;

    [[nodiscard]] const TerminalCell &cell(const std::uint16_t column, const std::uint16_t row) const
    {
        return cells.at((static_cast<std::size_t>(row) * columns) + column);
    }

    [[nodiscard]] TerminalCell &cell(const std::uint16_t column, const std::uint16_t row)
    {
        return cells.at((static_cast<std::size_t>(row) * columns) + column);
    }

    [[nodiscard]] const TerminalHyperlink *hyperlink(const std::uint32_t id) const noexcept
    {
        return id == 0 || id > hyperlinks.size() ? nullptr : &hyperlinks[id - 1];
    }
};

// A bounded page of scrollback text. Line 0 is the top of the retained
// history (the oldest scrollback row); the active screen rows follow, so the
// page space is "full screen including scrollback" (the same space as
// GhosttyPoint SCREEN coordinates).
struct TerminalScrollbackPage final
{
    std::vector<std::string> lines;
    std::size_t firstLine = 0;
    std::size_t totalLines = 0;
    std::size_t scrollbackLines = 0;

    friend bool operator==(const TerminalScrollbackPage &, const TerminalScrollbackPage &) = default;
};

enum class TerminalScrollbackAnchor : std::uint8_t
{
    head,
    tail,
};

// `offset` is measured from the selected anchor. For head reads it is the
// zero-based absolute line index. For tail reads it is the number of newest
// lines to skip, which makes paging recent output deterministic while the
// terminal keeps growing.
struct TerminalScrollbackRequest final
{
    TerminalScrollbackAnchor anchor = TerminalScrollbackAnchor::head;
    std::size_t offset = 0;
    std::size_t lineCount = 1;
};

using TerminalSnapshotPtr = std::shared_ptr<const TerminalSnapshot>;

class TerminalEngine
{
public:
    virtual ~TerminalEngine() = default;

    TerminalEngine(const TerminalEngine &) = delete;
    TerminalEngine &operator=(const TerminalEngine &) = delete;
    TerminalEngine(TerminalEngine &&) = delete;
    TerminalEngine &operator=(TerminalEngine &&) = delete;

    [[nodiscard]] virtual std::error_code feed(std::span<const std::byte> bytes) = 0;
    [[nodiscard]] virtual std::error_code resize(TerminalGeometry geometry) = 0;
    [[nodiscard]] virtual std::expected<TerminalSnapshot, std::error_code> snapshot() = 0;
    [[nodiscard]] virtual std::error_code setSelection(std::optional<TerminalSelection> selection) = 0;
    // Returns whether the gesture changed visible terminal state. This lets
    // session workers avoid rebuilding snapshots for cancel/release events and
    // autoscroll ticks already held at a scrollback boundary.
    [[nodiscard]] virtual std::expected<bool, std::error_code>
    applySelectionGesture(const TerminalSelectionGesture &gesture) = 0;
    [[nodiscard]] virtual std::expected<bool, std::error_code>
    applyCopyModeAction(const TerminalCopyModeAction &action) = 0;
    [[nodiscard]] virtual std::error_code selectAll() = 0;
    [[nodiscard]] virtual std::expected<std::optional<std::string>, std::error_code> selectedText() const = 0;
    virtual void scrollViewport(int rows) = 0;
    virtual void scrollToBottom() = 0;
    [[nodiscard]] virtual std::expected<TerminalSearchResult, std::error_code>
    search(std::string_view query, TerminalSearchDirection direction, bool caseSensitive) = 0;
    [[nodiscard]] virtual std::error_code clearSearch() = 0;
    [[nodiscard]] virtual std::expected<std::vector<std::byte>, std::error_code>
    encodePaste(std::span<const std::byte> bytes) const = 0;
    [[nodiscard]] virtual std::expected<std::vector<std::byte>, std::error_code>
    encodeKey(const TerminalKeyEvent &event) = 0;
    [[nodiscard]] virtual std::expected<std::vector<std::byte>, std::error_code>
    encodeMouse(const TerminalMouseEvent &event) = 0;
    [[nodiscard]] virtual std::expected<std::vector<std::byte>, std::error_code> encodeFocus(bool focused) const = 0;
    // Returns and clears the newest terminal-originated clipboard write.
    // Callers drain this after feed() and after releasing their engine lock.
    [[nodiscard]] virtual std::optional<std::string> takeClipboardWrite() = 0;

    // Read a bounded page of scrollback text (including the active screen at
    // the tail). firstLine is zero-based from the top of the retained
    // history. Head offsets address absolute retained lines; tail offsets skip
    // newest lines before selecting a page. This API is not intended for the
    // render path; it is used by AI/context tools to page through output that
    // scrolled off the screen.
    [[nodiscard]] virtual std::expected<TerminalScrollbackPage, std::error_code>
    scrollbackPage(TerminalScrollbackRequest request) const = 0;

    // Diagnostic representation of the active screen.
    [[nodiscard]] virtual std::expected<std::string, std::error_code> plainText() const = 0;

protected:
    TerminalEngine() = default;
};

} // namespace ztermy::terminal
