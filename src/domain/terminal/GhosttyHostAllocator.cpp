#include "domain/terminal/GhosttyHostAllocator.h"

#include <ghostty/vt.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>

namespace ztermy::terminal
{
namespace
{
// The pinned Zig C bridge passes log2(bytes), despite the allocator.h prose
// describing byte alignment. Keep allocation and deletion normalization identical.
std::size_t hostAlignment(const std::uint8_t exponent) noexcept
{
    if (exponent >= std::numeric_limits<std::size_t>::digits)
        return 0;
    return std::max(alignof(std::max_align_t), std::size_t{1} << exponent);
}

void *allocate(void *, const std::size_t length, const std::uint8_t alignment, std::uintptr_t) noexcept
{
    const auto bytes = hostAlignment(alignment);
    if (bytes == 0 || length > static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()))
        return nullptr;
    return ::operator new(std::max(length, std::size_t{1}), std::align_val_t{bytes}, std::nothrow);
}

bool resize(void *, void *, std::size_t, std::uint8_t, std::size_t, std::uintptr_t) noexcept
{
    // Declining preserves the allocation and lets Zig perform allocate/copy/free.
    return false;
}

void *remap(void *, void *, std::size_t, std::uint8_t, std::size_t, std::uintptr_t) noexcept
{
    return nullptr;
}

void release(void *, void *memory, std::size_t, const std::uint8_t alignment, std::uintptr_t) noexcept
{
    if (const auto bytes = hostAlignment(alignment); bytes != 0)
        ::operator delete(memory, std::align_val_t{bytes});
}
} // namespace

const GhosttyAllocator *ghosttyHostAllocator() noexcept
{
    static const GhosttyAllocatorVtable vtable{.alloc = allocate, .resize = resize, .remap = remap, .free = release};
    static int context = 0;
    static const GhosttyAllocator allocator{.ctx = &context, .vtable = &vtable};
    return &allocator;
}
} // namespace ztermy::terminal
