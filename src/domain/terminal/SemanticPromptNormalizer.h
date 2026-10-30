#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace ztermy::terminal::detail
{
[[nodiscard]] std::optional<std::string> normalizeSemanticPromptMarks(std::span<const std::byte> bytes,
                                                                      std::string &pending);
} // namespace ztermy::terminal::detail
