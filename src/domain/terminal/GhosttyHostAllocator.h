#pragma once

struct GhosttyAllocator;

namespace ztermy::terminal
{
// Process-lifetime adapter; the terminal retains this pointer until destruction.
[[nodiscard]] const GhosttyAllocator *ghosttyHostAllocator() noexcept;
} // namespace ztermy::terminal
