#include "domain/terminal/GhosttyImagePolicy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace ztermy::terminal
{
namespace
{
bool deviceAttributes(GhosttyTerminal, void *, GhosttyDeviceAttributes *attributes)
{
    if (!attributes)
        return false;
    *attributes = {};
    attributes->primary.conformance_level = GHOSTTY_DA_CONFORMANCE_VT220;
    attributes->primary.features[0] = GHOSTTY_DA_FEATURE_SIXEL;
    attributes->primary.features[1] = GHOSTTY_DA_FEATURE_ANSI_COLOR;
    attributes->primary.num_features = 2;
    attributes->secondary.device_type = GHOSTTY_DA_DEVICE_TYPE_VT220;
    return true;
}
} // namespace

GhosttyResult installGhosttyImagePolicy(GhosttyTerminal terminal)
{
    const std::uint64_t storageBytes = std::uint64_t{32} * 1024 * 1024;
    const std::size_t chunkBytes = std::size_t{16} * 1024;
    const bool disabled = false;
    const std::array<std::pair<GhosttyTerminalOption, const void *>, 7> options{{
        {GHOSTTY_TERMINAL_OPT_KITTY_IMAGE_STORAGE_LIMIT, &storageBytes},
        {GHOSTTY_TERMINAL_OPT_KITTY_IMAGE_MEDIUM_FILE, &disabled},
        {GHOSTTY_TERMINAL_OPT_KITTY_IMAGE_MEDIUM_TEMP_FILE, nullptr},
        {GHOSTTY_TERMINAL_OPT_KITTY_IMAGE_MEDIUM_SHARED_MEM, &disabled},
        {GHOSTTY_TERMINAL_OPT_APC_MAX_BYTES, &chunkBytes},
        {GHOSTTY_TERMINAL_OPT_APC_MAX_BYTES_KITTY, &chunkBytes},
        {GHOSTTY_TERMINAL_OPT_DEVICE_ATTRIBUTES, reinterpret_cast<const void *>(&deviceAttributes)},
    }};
    for (const auto &[option, value] : options)
        if (const auto result = ghostty_terminal_set(terminal, option, value); result != GHOSTTY_SUCCESS)
            return result;
    return GHOSTTY_SUCCESS;
}
} // namespace ztermy::terminal
