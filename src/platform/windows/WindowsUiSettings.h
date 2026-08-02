#pragma once

#include <cstdint>
#include <optional>

namespace ztermy::windowing
{

class SystemBooleanPreference final
{
public:
    explicit constexpr SystemBooleanPreference(const bool enabled) noexcept : m_enabled(enabled) {}

    [[nodiscard]] constexpr bool enabled() const noexcept { return m_enabled; }

    [[nodiscard]] constexpr bool update(const std::optional<bool> queriedValue) noexcept
    {
        if (!queriedValue || *queriedValue == m_enabled)
        {
            return false;
        }
        m_enabled = *queriedValue;
        return true;
    }

private:
    bool m_enabled = false;
};

using ClientAreaAnimationPreference = SystemBooleanPreference;

struct RgbColor final
{
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;

    friend bool operator==(const RgbColor &, const RgbColor &) = default;
};

struct HighContrastState final
{
    bool enabled = false;
    RgbColor background{};
    RgbColor text{};
    RgbColor highlight{};
    RgbColor highlightText{};

    friend bool operator==(const HighContrastState &, const HighContrastState &) = default;
};

[[nodiscard]] std::optional<bool> queryClientAreaAnimationsEnabled() noexcept;
[[nodiscard]] std::optional<HighContrastState> queryHighContrastState() noexcept;
[[nodiscard]] constexpr RgbColor decodeColorizationArgb(const std::uint32_t argb) noexcept
{
    return {
        .red = static_cast<std::uint8_t>((argb >> 16U) & 0xFFU),
        .green = static_cast<std::uint8_t>((argb >> 8U) & 0xFFU),
        .blue = static_cast<std::uint8_t>(argb & 0xFFU),
    };
}
[[nodiscard]] std::optional<RgbColor> querySystemAccentColor() noexcept;

} // namespace ztermy::windowing
