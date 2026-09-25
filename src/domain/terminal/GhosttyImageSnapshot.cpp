#include "domain/terminal/GhosttyImageSnapshot.h"
#include "domain/terminal/GhosttyImageExtension.h"

#include <algorithm>
#include <array>
#include <new>

namespace ztermy::terminal
{
namespace
{
constexpr std::size_t maximumSnapshotBytes = std::size_t{32} * 1024 * 1024;
constexpr std::size_t maximumPlacements = 4096;

GhosttyResult readImage(GhosttyKittyGraphicsImage handle, TerminalImage &image, std::size_t budget)
{
    GhosttyKittyImageFormat format{};
    const std::uint8_t *pixels = nullptr;
    std::size_t bytes = 0;
    constexpr std::array keys{GHOSTTY_KITTY_IMAGE_DATA_WIDTH, GHOSTTY_KITTY_IMAGE_DATA_HEIGHT,
                              GHOSTTY_KITTY_IMAGE_DATA_FORMAT, GHOSTTY_KITTY_IMAGE_DATA_DATA_PTR,
                              GHOSTTY_KITTY_IMAGE_DATA_DATA_LEN};
    std::array<void *, keys.size()> values{&image.width, &image.height, &format, static_cast<void *>(&pixels), &bytes};
    const auto result =
        ghostty_kitty_graphics_image_get_multi(handle, keys.size(), keys.data(), values.data(), nullptr);
    if (result != GHOSTTY_SUCCESS)
        return result;
    std::size_t channels = 0;
    switch (format)
    {
        case GHOSTTY_KITTY_IMAGE_FORMAT_RGB:
            channels = 3;
            image.format = TerminalImageFormat::rgb;
            break;
        case GHOSTTY_KITTY_IMAGE_FORMAT_RGBA:
            channels = 4;
            image.format = TerminalImageFormat::rgba;
            break;
        case GHOSTTY_KITTY_IMAGE_FORMAT_GRAY_ALPHA:
            channels = 2;
            image.format = TerminalImageFormat::grayAlpha;
            break;
        case GHOSTTY_KITTY_IMAGE_FORMAT_GRAY:
            channels = 1;
            image.format = TerminalImageFormat::gray;
            break;
        default:
            return GHOSTTY_INVALID_VALUE;
    }
    if (!pixels || image.width == 0 || image.height == 0 || image.width > 8192 || image.height > 8192 || bytes > budget
        || std::uint64_t{image.width} * image.height * channels != bytes)
        return GHOSTTY_INVALID_VALUE;
    image.pixels.assign(pixels, pixels + bytes);
    return GHOSTTY_SUCCESS;
}
} // namespace

GhosttyImageSnapshot::~GhosttyImageSnapshot()
{
    ghostty_kitty_graphics_placement_iterator_free(m_iterator);
}

GhosttyResult GhosttyImageSnapshot::capture(GhosttyTerminal terminal, std::vector<TerminalImagePlacement> &placements,
                                            bool &changed)
{
    changed = false;
    placements.clear();
    std::erase_if(m_images, [](const auto &entry) {
        return entry.second.expired();
    });
    GhosttyKittyGraphics graphics = nullptr;
    auto result = ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_KITTY_GRAPHICS, static_cast<void *>(&graphics));
    if (result == GHOSTTY_NO_VALUE)
        return GHOSTTY_SUCCESS;
    if (result != GHOSTTY_SUCCESS)
        return result;
    std::uint64_t storageGeneration = 0;
    result = ghostty_kitty_graphics_get(graphics, GHOSTTY_KITTY_GRAPHICS_DATA_GENERATION, &storageGeneration);
    if (result != GHOSTTY_SUCCESS)
        return result;
    changed = storageGeneration != m_generation;
    if (storageGeneration == 0)
    {
        m_generation = 0;
        return GHOSTTY_SUCCESS;
    }
    if (!m_iterator)
    {
        result = ghostty_kitty_graphics_placement_iterator_new(nullptr, &m_iterator);
        if (result != GHOSTTY_SUCCESS)
            return result;
    }
    result = ghostty_kitty_graphics_get(graphics, GHOSTTY_KITTY_GRAPHICS_DATA_PLACEMENT_ITERATOR,
                                        static_cast<void *>(&m_iterator));
    if (result != GHOSTTY_SUCCESS)
        return result;
    try
    {
        std::unordered_map<std::uint64_t, std::shared_ptr<const TerminalImage>> visibleImages;
        std::size_t totalBytes = 0;
        const auto attachImage = [&](TerminalImagePlacement &placement, GhosttyKittyGraphicsImage handle) {
            std::uint64_t generation = 0;
            const auto readResult =
                ghostty_kitty_graphics_image_get(handle, GHOSTTY_KITTY_IMAGE_DATA_GENERATION, &generation);
            if (readResult != GHOSTTY_SUCCESS)
                return readResult;
            if (const auto found = visibleImages.find(generation); found != visibleImages.end())
                placement.image = found->second;
            else
            {
                const auto cached = m_images.find(generation);
                if (cached != m_images.end())
                    placement.image = cached->second.lock();
                if (!placement.image)
                {
                    auto image = std::make_shared<TerminalImage>();
                    image->generation = generation;
                    if (readImage(handle, *image, maximumSnapshotBytes - totalBytes) != GHOSTTY_SUCCESS)
                        return GHOSTTY_NO_VALUE;
                    placement.image = std::move(image);
                }
                if (placement.image->pixels.size() > maximumSnapshotBytes - totalBytes)
                    return GHOSTTY_NO_VALUE;
                totalBytes += placement.image->pixels.size();
                visibleImages.emplace(generation, placement.image);
                m_images[generation] = placement.image;
            }
            return GHOSTTY_SUCCESS;
        };
        bool hasVirtual = false;
        while (ghostty_kitty_graphics_placement_next(m_iterator) && placements.size() < maximumPlacements)
        {
            bool isVirtual = false;
            result = ghostty_kitty_graphics_placement_get(m_iterator, GHOSTTY_KITTY_GRAPHICS_PLACEMENT_DATA_IS_VIRTUAL,
                                                          &isVirtual);
            if (result != GHOSTTY_SUCCESS)
                return result;
            if (isVirtual)
            {
                hasVirtual = true;
                continue;
            }
            TerminalImagePlacement placement;
            constexpr std::array keys{
                GHOSTTY_KITTY_GRAPHICS_PLACEMENT_DATA_IMAGE_ID, GHOSTTY_KITTY_GRAPHICS_PLACEMENT_DATA_PLACEMENT_ID,
                GHOSTTY_KITTY_GRAPHICS_PLACEMENT_DATA_Z, GHOSTTY_KITTY_GRAPHICS_PLACEMENT_DATA_X_OFFSET,
                GHOSTTY_KITTY_GRAPHICS_PLACEMENT_DATA_Y_OFFSET};
            std::uint32_t offsetX = 0, offsetY = 0;
            std::array<void *, keys.size()> values{&placement.imageId, &placement.placementId, &placement.z, &offsetX,
                                                   &offsetY};
            result = ghostty_kitty_graphics_placement_get_multi(m_iterator, keys.size(), keys.data(), values.data(),
                                                                nullptr);
            if (result != GHOSTTY_SUCCESS)
                return result;
            placement.offsetX = offsetX;
            placement.offsetY = offsetY;
            const auto handle = ghostty_kitty_graphics_image(graphics, placement.imageId);
            if (!handle)
                continue;
            GhosttyKittyGraphicsPlacementRenderInfo info{};
            info.size = sizeof(info);
            result = ghostty_kitty_graphics_placement_render_info(m_iterator, handle, terminal, &info);
            if (result != GHOSTTY_SUCCESS)
                return result;
            if (!info.viewport_visible)
                continue;
            result = attachImage(placement, handle);
            if (result == GHOSTTY_NO_VALUE)
                continue;
            if (result != GHOSTTY_SUCCESS)
                return result;
            placement.column = info.viewport_col;
            placement.row = info.viewport_row;
            placement.width = info.pixel_width;
            placement.height = info.pixel_height;
            placement.sourceX = info.source_x;
            placement.sourceY = info.source_y;
            placement.sourceWidth = info.source_width;
            placement.sourceHeight = info.source_height;
            placements.push_back(std::move(placement));
        }
        if (hasVirtual && placements.size() < maximumPlacements)
        {
            std::vector<ZtermyGhosttyUnicodePlacement> fragments(maximumPlacements - placements.size());
            std::size_t count = 0;
            result = ztermy_ghostty_unicode_placements(terminal, fragments.data(), fragments.size(), &count);
            if (result != GHOSTTY_SUCCESS)
                return result;
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto &fragment = fragments[i];
                const auto handle = ghostty_kitty_graphics_image(graphics, fragment.imageId);
                if (!handle)
                    continue;
                TerminalImagePlacement placement{.imageId = fragment.imageId,
                                                 .placementId = fragment.placementId,
                                                 .column = fragment.column,
                                                 .row = fragment.row,
                                                 .z = -1,
                                                 .offsetX = fragment.offsetX,
                                                 .offsetY = fragment.offsetY,
                                                 .width = fragment.width,
                                                 .height = fragment.height,
                                                 .sourceX = fragment.sourceX,
                                                 .sourceY = fragment.sourceY,
                                                 .sourceWidth = fragment.sourceWidth,
                                                 .sourceHeight = fragment.sourceHeight};
                result = attachImage(placement, handle);
                if (result == GHOSTTY_NO_VALUE)
                    continue;
                if (result != GHOSTTY_SUCCESS)
                    return result;
                placements.push_back(std::move(placement));
            }
        }
        std::ranges::sort(placements, [](const auto &a, const auto &b) {
            if (a.z != b.z)
                return a.z < b.z;
            if (a.imageId != b.imageId)
                return a.imageId < b.imageId;
            return a.placementId < b.placementId;
        });
    }
    catch (const std::bad_alloc &)
    {
        placements.clear();
        return GHOSTTY_OUT_OF_MEMORY;
    }
    m_generation = storageGeneration;
    return GHOSTTY_SUCCESS;
}
} // namespace ztermy::terminal
