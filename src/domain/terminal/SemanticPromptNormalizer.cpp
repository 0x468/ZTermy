#include "domain/terminal/SemanticPromptNormalizer.h"
#include <algorithm>
#include <string_view>
#include <utility>

namespace ztermy::terminal::detail
{
[[nodiscard]] std::optional<std::string> normalizeSemanticPromptMarks(const std::span<const std::byte> bytes,
                                                                      std::string &pending)
{
    constexpr std::string_view freshLinePrompt{"\x1b]133;A"};
    const std::string_view incoming(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    std::size_t trailingPrefixLength = 0;
    for (std::size_t length = std::min(incoming.size(), freshLinePrompt.size() - 1); length > 0; --length)
    {
        if (incoming.ends_with(freshLinePrompt.substr(0, length)))
        {
            trailingPrefixLength = length;
            break;
        }
    }
    if (pending.empty() && !incoming.contains(freshLinePrompt) && trailingPrefixLength == 0)
    {
        return std::nullopt;
    }

    std::string normalized = std::move(pending);
    normalized.append(incoming);
    pending.clear();
    for (std::size_t length = std::min(normalized.size(), freshLinePrompt.size() - 1); length > 0; --length)
    {
        if (std::string_view(normalized).ends_with(freshLinePrompt.substr(0, length)))
        {
            pending.assign(normalized.end() - static_cast<std::ptrdiff_t>(length), normalized.end());
            normalized.resize(normalized.size() - length);
            break;
        }
    }
    for (std::size_t position = 0; (position = normalized.find(freshLinePrompt, position)) != std::string::npos;
         position += freshLinePrompt.size())
    {
        // Windows shells use OSC 133 A as a prompt mark and position the
        // cursor themselves. Ghostty's Kitty interpretation also inserts a
        // fresh line, which makes Nushell scroll twice per Enter.
        normalized[position + freshLinePrompt.size() - 1] = 'P';
    }
    return normalized;
}

} // namespace ztermy::terminal::detail
