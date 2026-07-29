#pragma once

#include <optional>

namespace ztermy::windowing
{

class ClientAreaAnimationPreference final
{
public:
    explicit constexpr ClientAreaAnimationPreference(const bool enabled = true) noexcept : m_enabled(enabled) {}

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
    bool m_enabled = true;
};

[[nodiscard]] std::optional<bool> queryClientAreaAnimationsEnabled() noexcept;

} // namespace ztermy::windowing
