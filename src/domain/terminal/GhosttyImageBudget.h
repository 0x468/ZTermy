#pragma once

#include "domain/terminal/GhosttyImageExtension.h"

#include <memory>
#include <mutex>

namespace ztermy::terminal
{
// Native stored rasters only; loaders, immutable snapshots and GPU copies have
// separate lifetimes. Install before constructing any application sessions.
inline constexpr std::size_t kDefaultImageStorageBudget = std::size_t{128} * 1024 * 1024;

// Shared lifetime token, not shared terminal ownership. All engine entry points
// serialize here; teardown clears terminal under the same lock before freeing it.
struct GhosttyImageAccess final
{
    std::recursive_mutex mutex;
    GhosttyTerminal terminal = nullptr;
};

void registerImageEngine(const std::shared_ptr<GhosttyImageAccess> &access);

// Caller holds its own engine gate. Foreign gates are try-lock-only: never wait
// for another engine while processing terminal output. Busy candidates are skipped.
[[nodiscard]] std::size_t reclaimImageStorage(const std::shared_ptr<GhosttyImageAccess> &requester, std::size_t bytes,
                                              std::uint32_t protectedImageId, bool currentScreenOnly = false);
} // namespace ztermy::terminal
