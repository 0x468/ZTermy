#include "domain/terminal/GhosttyTerminalEngine.h"

#include <ghostty/vt.h>

namespace
{

class GhosttyErrorCategory final : public std::error_category
{
public:
    [[nodiscard]] const char *name() const noexcept override { return "libghostty-vt"; }

    [[nodiscard]] std::string message(const int condition) const override
    {
        switch (static_cast<GhosttyResult>(condition))
        {
            case GHOSTTY_SUCCESS:
                return "success";
            case GHOSTTY_OUT_OF_MEMORY:
                return "out of memory";
            case GHOSTTY_INVALID_VALUE:
                return "invalid value";
            case GHOSTTY_OUT_OF_SPACE:
                return "output buffer is too small";
            case GHOSTTY_NO_VALUE:
                return "requested value is unavailable";
            default:
                return "unknown libghostty-vt error";
        }
    }
};

[[nodiscard]] const std::error_category &ghosttyErrorCategory() noexcept
{
    static GhosttyErrorCategory category;
    return category;
}

[[nodiscard]] std::error_code ghosttyError(const GhosttyResult result) noexcept
{
    return {static_cast<int>(result), ghosttyErrorCategory()};
}

[[nodiscard]] std::error_code invalidArgument() noexcept
{
    return std::make_error_code(std::errc::invalid_argument);
}

class UniqueTerminal final
{
public:
    explicit UniqueTerminal(const GhosttyTerminal handle) noexcept : m_handle(handle) {}

    ~UniqueTerminal()
    {
        if (m_handle != nullptr)
        {
            ghostty_terminal_free(m_handle);
        }
    }

    UniqueTerminal(const UniqueTerminal &) = delete;
    UniqueTerminal &operator=(const UniqueTerminal &) = delete;

    [[nodiscard]] GhosttyTerminal get() const noexcept { return m_handle; }
    void release() noexcept { m_handle = nullptr; }

private:
    GhosttyTerminal m_handle;
};

class UniqueFormatter final
{
public:
    ~UniqueFormatter()
    {
        if (handle != nullptr)
        {
            ghostty_formatter_free(handle);
        }
    }

    UniqueFormatter(const UniqueFormatter &) = delete;
    UniqueFormatter &operator=(const UniqueFormatter &) = delete;

    GhosttyFormatter handle = nullptr;

private:
    UniqueFormatter() = default;

    friend class ztermy::terminal::GhosttyTerminalEngine;
};

class GhosttyOwnedBuffer final
{
public:
    ~GhosttyOwnedBuffer()
    {
        if (data != nullptr)
        {
            ghostty_free(nullptr, data, length);
        }
    }

    GhosttyOwnedBuffer(const GhosttyOwnedBuffer &) = delete;
    GhosttyOwnedBuffer &operator=(const GhosttyOwnedBuffer &) = delete;

    std::uint8_t *data = nullptr;
    std::size_t length = 0;

private:
    GhosttyOwnedBuffer() = default;

    friend class ztermy::terminal::GhosttyTerminalEngine;
};

} // namespace

namespace ztermy::terminal
{

struct GhosttyTerminalEngine::Impl
{
    explicit Impl(const GhosttyTerminal handle) noexcept : terminal(handle) {}

    ~Impl()
    {
        if (terminal != nullptr)
        {
            ghostty_terminal_free(terminal);
        }
    }

    GhosttyTerminal terminal = nullptr;
};

std::expected<std::unique_ptr<GhosttyTerminalEngine>, std::error_code>
GhosttyTerminalEngine::create(const TerminalGeometry geometry)
{
    if (!geometry.valid())
    {
        return std::unexpected(invalidArgument());
    }

    GhosttyTerminal terminal = nullptr;
    const GhosttyResult result = ghostty_terminal_new(nullptr, &terminal, geometry.columns, geometry.rows);
    if (result != GHOSTTY_SUCCESS)
    {
        return std::unexpected(ghosttyError(result));
    }

    UniqueTerminal terminalOwner(terminal);
    auto impl = std::make_unique<Impl>(terminalOwner.get());
    terminalOwner.release();
    auto engine = std::unique_ptr<GhosttyTerminalEngine>(new GhosttyTerminalEngine(std::move(impl)));
    if (const std::error_code resizeError = engine->resize(geometry))
    {
        return std::unexpected(resizeError);
    }
    return engine;
}

GhosttyTerminalEngine::GhosttyTerminalEngine(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

GhosttyTerminalEngine::~GhosttyTerminalEngine() = default;

std::error_code GhosttyTerminalEngine::feed(const std::span<const std::byte> bytes)
{
    if (!bytes.empty())
    {
        ghostty_terminal_vt_write(m_impl->terminal, reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size());
    }
    return {};
}

std::error_code GhosttyTerminalEngine::resize(const TerminalGeometry geometry)
{
    if (!geometry.valid())
    {
        return invalidArgument();
    }

    const GhosttyResult result = ghostty_terminal_resize(m_impl->terminal, geometry.columns, geometry.rows,
                                                         geometry.cellWidthPixels, geometry.cellHeightPixels);
    return result == GHOSTTY_SUCCESS ? std::error_code{} : ghosttyError(result);
}

std::expected<std::string, std::error_code> GhosttyTerminalEngine::plainText() const
{
    GhosttyFormatterTerminalOptions options{};
    options.size = sizeof(options);
    options.emit = GHOSTTY_FORMATTER_FORMAT_PLAIN;
    options.unwrap = true;
    options.trim = true;

    UniqueFormatter formatter;
    const GhosttyResult createResult =
        ghostty_formatter_terminal_new(nullptr, &formatter.handle, m_impl->terminal, options);
    if (createResult != GHOSTTY_SUCCESS)
    {
        return std::unexpected(ghosttyError(createResult));
    }

    GhosttyOwnedBuffer buffer;
    const GhosttyResult formatResult =
        ghostty_formatter_format_alloc(formatter.handle, nullptr, &buffer.data, &buffer.length);
    if (formatResult != GHOSTTY_SUCCESS)
    {
        return std::unexpected(ghosttyError(formatResult));
    }

    if (buffer.length == 0)
    {
        return std::string{};
    }
    return std::string(reinterpret_cast<const char *>(buffer.data), buffer.length);
}

} // namespace ztermy::terminal
