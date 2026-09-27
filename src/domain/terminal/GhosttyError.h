#pragma once

#include <ghostty/vt.h>
#include <system_error>

namespace ztermy::terminal::detail
{
[[nodiscard]] inline std::error_code invalidArgument() noexcept
{
    return std::make_error_code(std::errc::invalid_argument);
}

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

[[nodiscard]] inline const std::error_category &ghosttyErrorCategory() noexcept
{
    static GhosttyErrorCategory category;
    return category;
}

[[nodiscard]] inline std::error_code ghosttyError(const GhosttyResult result) noexcept
{
    return {static_cast<int>(result), ghosttyErrorCategory()};
}

} // namespace ztermy::terminal::detail
