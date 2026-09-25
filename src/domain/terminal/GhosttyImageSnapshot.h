#pragma once

#include "domain/terminal/TerminalImage.h"

#include <ghostty/vt.h>

#include <unordered_map>

namespace ztermy::terminal
{
// Accessed only by the terminal worker. Keep only weak references: retained
// history belongs to Ghostty, while queued/displayed snapshots own their pixels.
class GhosttyImageSnapshot final
{
public:
    GhosttyImageSnapshot() = default;
    ~GhosttyImageSnapshot();
    GhosttyImageSnapshot(const GhosttyImageSnapshot &) = delete;
    GhosttyImageSnapshot &operator=(const GhosttyImageSnapshot &) = delete;
    [[nodiscard]] GhosttyResult capture(GhosttyTerminal terminal, std::vector<TerminalImagePlacement> &placements,
                                        bool &changed);

private:
    // Rebound before each capture; reuse avoids native allocator churn per frame.
    GhosttyKittyGraphicsPlacementIterator m_iterator = nullptr;
    std::unordered_map<std::uint64_t, std::weak_ptr<const TerminalImage>> m_images;
    std::uint64_t m_generation = 0;
};
} // namespace ztermy::terminal
