#include "domain/terminal/GhosttyImageBudget.h"
#include "domain/terminal/GhosttyTerminalEngine.h"

#include <algorithm>
#include <vector>

namespace ztermy::terminal
{
namespace
{
struct Registry final
{
    std::mutex mutex;
    std::vector<std::weak_ptr<GhosttyImageAccess>> engines;
};

Registry &registry()
{
    static Registry instance;
    return instance;
}

struct Candidate final
{
    std::shared_ptr<GhosttyImageAccess> access;
    ZtermyGhosttyImageRecord record;
    bool alternate = false;
};
} // namespace

bool GhosttyTerminalEngine::initializeImageBudget() noexcept
{
    return ztermy_ghostty_configure_image_budget(kDefaultImageStorageBudget) == GHOSTTY_SUCCESS;
}

void registerImageEngine(const std::shared_ptr<GhosttyImageAccess> &access)
{
    auto &pool = registry();
    std::scoped_lock lock(pool.mutex);
    std::erase_if(pool.engines, [](const auto &entry) {
        return entry.expired();
    });
    pool.engines.push_back(access);
}

std::size_t reclaimImageStorage(const std::shared_ptr<GhosttyImageAccess> &requester, const std::size_t bytes,
                                const std::uint32_t protectedImageId, const bool currentScreenOnly)
{
    if (bytes == 0)
        return 0;
    std::vector<std::shared_ptr<GhosttyImageAccess>> engines;
    GhosttyTerminalScreen activeScreen = GHOSTTY_TERMINAL_SCREEN_PRIMARY;
    if (currentScreenOnly)
    {
        if (!requester->terminal
            || ghostty_terminal_get(requester->terminal, GHOSTTY_TERMINAL_DATA_ACTIVE_SCREEN, &activeScreen)
                   != GHOSTTY_SUCCESS)
            return 0;
        engines.push_back(requester);
    }
    else
    {
        auto &pool = registry();
        std::scoped_lock lock(pool.mutex);
        for (const auto &entry : pool.engines)
            if (auto engine = entry.lock())
                engines.push_back(std::move(engine));
    }
    // Never retain the registry lock while entering any terminal engine.
    std::vector<Candidate> candidates;
    std::vector<ZtermyGhosttyImageRecord> records;
    for (const auto &access : engines)
    {
        std::unique_lock lock(access->mutex, std::try_to_lock);
        if (!lock || !access->terminal)
            continue;
        for (const bool alternate : {false, true})
        {
            if (currentScreenOnly && alternate != (activeScreen == GHOSTTY_TERMINAL_SCREEN_ALTERNATE))
                continue;
            std::size_t count = 0;
            const auto sizeResult = ztermy_ghostty_image_inventory(access->terminal, alternate, nullptr, 0, &count);
            if (count == 0 || count > 4096 || (sizeResult != GHOSTTY_SUCCESS && sizeResult != GHOSTTY_OUT_OF_MEMORY))
                continue;
            records.resize(count);
            if (ztermy_ghostty_image_inventory(access->terminal, alternate, records.data(), records.size(), &count)
                != GHOSTTY_SUCCESS)
                continue;
            for (const auto &record : records)
                if (!record.visible && !(access == requester && record.imageId == protectedImageId))
                    candidates.push_back({.access = access, .record = record, .alternate = alternate});
        }
    }
    // Native generations are process-global: oldest stored rasters go first.
    std::ranges::sort(candidates, {}, [](const Candidate &candidate) {
        return candidate.record.generation;
    });
    std::size_t released = 0;
    for (const auto &candidate : candidates)
    {
        std::unique_lock lock(candidate.access->mutex, std::try_to_lock);
        if (!lock || !candidate.access->terminal)
            continue;
        const auto &record = candidate.record;
        if (ztermy_ghostty_evict_image(candidate.access->terminal, candidate.alternate, record.imageId,
                                       record.screenGeneration, record.generation)
            != GHOSTTY_SUCCESS)
            continue;
        released += static_cast<std::size_t>(record.bytes);
        if (released >= bytes)
            break;
    }
    return released;
}
} // namespace ztermy::terminal
