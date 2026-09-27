#include "domain/terminal/GhosttyImageExtension.h"
#include "domain/terminal/GhosttyImagePolicy.h"
#include "domain/terminal/GhosttyImageSnapshot.h"
#include "domain/terminal/GhosttyTerminalEngine.h"

#include <QByteArray>
#include <Windows.h>
#include <Psapi.h>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
using SnapshotResult = std::expected<ztermy::terminal::TerminalSnapshot, std::error_code>;

// Keep one worker alive: per-frame thread creation would measure a different
// allocator pattern from a terminal's persistent I/O and snapshot workers.
class SnapshotWorker final
{
public:
    SnapshotWorker()
        : m_thread([this](const std::stop_token &stop) {
              for (;;)
              {
                  std::unique_lock lock(m_mutex);
                  if (!m_available.wait(lock, stop, [&] {
                          return m_task.has_value();
                      }))
                      return;
                  auto task = std::move(*m_task);
                  m_task.reset();
                  lock.unlock();
                  task();
              }
          })
    {
    }

    SnapshotResult capture(std::function<SnapshotResult()> operation)
    {
        std::packaged_task<SnapshotResult()> task(std::move(operation));
        auto result = task.get_future();
        {
            std::scoped_lock lock(m_mutex);
            m_task.emplace(std::move(task));
        }
        m_available.notify_one();
        return result.get();
    }

private:
    std::mutex m_mutex;
    std::condition_variable_any m_available;
    std::optional<std::packaged_task<SnapshotResult()>> m_task;
    std::jthread m_thread;
};
} // namespace

// Opt-in measurement, not a timing-sensitive CTest. Only synthetic image data.
int main(int argc, char **argv)
try
{
    using ztermy::terminal::GhosttyTerminalEngine;
    const std::string_view mode = argc > 1 ? argv[1] : "retain";
    if (mode != "retain" && mode != "upload-only" && mode != "no-retain" && mode != "image-only"
        && mode != "metadata-only" && mode != "iterator-only" && mode != "rgb" && mode != "streamed"
        && mode != "cross-thread" && mode != "alternating" && mode != "multi")
        return 11;
    int cycles = mode == "multi" ? 3 : 20;
    if (argc > 2)
    {
        const std::string_view count(argv[2]);
        const auto parsed = std::from_chars(count.data(), count.data() + count.size(), cycles);
        if (parsed.ec != std::errc{} || parsed.ptr != count.data() + count.size() || cycles < 1 || cycles > 200)
            return 11;
    }
    const auto start = std::chrono::steady_clock::now();
    const auto measure = [&](std::string_view phase, int operations) {
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                                  sizeof(counters)))
            return false;
        // Only this isolated diagnostic process locks heaps; never the GUI.
        std::array<HANDLE, 128> heaps{};
        const DWORD count = GetProcessHeaps(static_cast<DWORD>(heaps.size()), heaps.data());
        if (count == 0 || count > heaps.size())
            return false;
        std::uint64_t busyBytes = 0, busyBlocks = 0;
        for (DWORD i = 0; i < count; ++i)
        {
            if (!HeapLock(heaps[i]))
                return false;
            PROCESS_HEAP_ENTRY entry{};
            while (HeapWalk(heaps[i], &entry))
                if ((entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) != 0)
                {
                    busyBytes += entry.cbData;
                    ++busyBlocks;
                }
            const DWORD walkError = GetLastError();
            const bool unlocked = HeapUnlock(heaps[i]) != FALSE;
            if (!unlocked || walkError != ERROR_NO_MORE_ITEMS)
                return false;
        }
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        std::cout << phase << ',' << operations << ',' << elapsed.count() << ',' << counters.PrivateUsage << ','
                  << counters.WorkingSetSize << ',' << busyBytes << ',' << busyBlocks << ','
                  << ztermy_ghostty_image_budget_usage() << '\n';
        return true;
    };
    std::cout << "phase,operations,elapsed_ms,private_bytes,working_set_bytes,heap_busy_bytes,heap_busy_blocks,stored_"
                 "image_bytes\n";
    if (!measure("process", 0))
        return 1;
    if (mode == "multi")
    {
        // Eight independent terminal engines, four visible 4-MiB rasters each.
        // Sample without a GUI so native storage and retained snapshot copies
        // can be distinguished; this deliberately does not measure GPU memory.
        int limitMiB = 128;
        if (argc > 3)
        {
            const std::string_view limit(argv[3]);
            const auto parsed = std::from_chars(limit.data(), limit.data() + limit.size(), limitMiB);
            if (parsed.ec != std::errc{} || parsed.ptr != limit.data() + limit.size() || limitMiB < 0 || limitMiB > 512)
                return 11;
        }
        if (ztermy_ghostty_configure_image_budget(limitMiB == 0 ? (std::numeric_limits<std::size_t>::max)()
                                                                : static_cast<std::size_t>(limitMiB) * 1024 * 1024)
            != GHOSTTY_SUCCESS)
            return 15;
        std::array<std::unique_ptr<GhosttyTerminalEngine>, 8> engines;
        std::array<std::optional<ztermy::terminal::TerminalSnapshot>, 8> snapshots;
        for (auto &terminal : engines)
        {
            auto result = GhosttyTerminalEngine::create(
                {.columns = 80, .rows = 24, .cellWidthPixels = 8, .cellHeightPixels = 16});
            if (!result)
                return 2;
            terminal = std::move(*result);
        }
        const QByteArray encoded = QByteArray(qsizetype{1024} * 1024 * 4, '\x7f').toBase64();
        const auto feed = [](GhosttyTerminalEngine &terminal, std::string_view bytes) {
            return !terminal.feed(std::as_bytes(std::span(bytes)));
        };
        std::atomic<bool> succeeded{true};
        std::atomic<int> accepted{0}, rejected{0};
        for (int cycle = 0; cycle < cycles; ++cycle)
        {
            // Keep the previous UI snapshots alive during replacement, as a
            // slow consumer would. Only one previous frame per terminal.
            std::vector<std::jthread> producers;
            producers.reserve(engines.size());
            for (auto &terminal : engines)
                producers.emplace_back([&, target = terminal.get()] {
                    // Start below any previous cursor position and move every
                    // old placement completely beyond the 24-row viewport.
                    for (int row = 0; row < 48; ++row)
                        if (!feed(*target, "\r\n"))
                            succeeded = false;
                    for (int index = 0; index < 4; ++index)
                    {
                        const QByteArray position = "\x1b[" + QByteArray::number(index * 4 + 1) + ";1H";
                        if (!feed(*target,
                                  std::string_view(position.constData(), static_cast<std::size_t>(position.size()))))
                            succeeded = false;
                        for (qsizetype offset = 0; offset < encoded.size(); offset += 4096)
                        {
                            QByteArray command = offset == 0 ? "\x1b_Ga=T,f=32,s=1024,v=1024,c=10,r=2,C=1,i="
                                                                   + QByteArray::number(cycle * 4 + index + 1) + ","
                                                             : QByteArrayLiteral("\x1b_G");
                            command += offset + 4096 < encoded.size() ? "m=1;" : "m=0;";
                            command += encoded.mid(offset, 4096) + "\x1b\\";
                            if (!feed(*target,
                                      std::string_view(command.constData(), static_cast<std::size_t>(command.size()))))
                                succeeded = false;
                        }
                        const auto replyBytes = target->takePtyWrite();
                        const std::string_view reply(reinterpret_cast<const char *>(replyBytes.data()),
                                                     replyBytes.size());
                        if (reply.find(";OK") != std::string::npos)
                            ++accepted;
                        else if (reply.find("ENOMEM") != std::string::npos)
                            ++rejected;
                        else
                            succeeded = false;
                    }
                });
            producers.clear(); // jthread joins before sampling heaps or snapshots.
            if (!succeeded || !measure("multi_uploaded", cycle + 1))
                return 16;
            std::size_t visible = 0;
            for (std::size_t index = 0; index < engines.size(); ++index)
            {
                auto result = engines[index]->snapshot();
                if (!result)
                    return 6;
                visible += result->images.size();
                snapshots[index] = std::move(*result);
            }
            std::cerr << "round=" << cycle + 1 << " accepted=" << accepted << " rejected=" << rejected
                      << " visible=" << visible << '\n';
            if (!measure("multi_snapshots", cycle + 1))
                return 1;
        }
        for (auto &terminal : engines)
            if (!feed(*terminal, "\x1b_Ga=d,d=A;\x1b\\"))
                return 8;
        if (!measure("multi_deleted_snapshots_retained", cycles))
            return 1;
        snapshots = {};
        if (!measure("multi_snapshots_released", cycles))
            return 1;
        engines = {};
        return ztermy_ghostty_image_budget_usage() == 0 && measure("multi_destroyed", cycles) ? 0 : 17;
    }
    auto created =
        GhosttyTerminalEngine::create({.columns = 80, .rows = 24, .cellWidthPixels = 8, .cellHeightPixels = 16});
    if (!created)
        return 2;
    auto engine = std::move(*created);
    std::unique_ptr<std::remove_pointer_t<GhosttyTerminal>, decltype(&ghostty_terminal_free)> native(
        nullptr, &ghostty_terminal_free);
    ztermy::terminal::GhosttyImageSnapshot imageCapture;
    if (mode == "image-only" || mode == "metadata-only" || mode == "iterator-only")
    {
        GhosttyTerminal handle = nullptr;
        if (ghostty_terminal_new(nullptr, &handle, 80, 24) != GHOSTTY_SUCCESS)
            return 12;
        native.reset(handle);
        if (ztermy::terminal::installGhosttyImagePolicy(handle) != GHOSTTY_SUCCESS
            || ghostty_terminal_resize(handle, 80, 24, 8, 16) != GHOSTTY_SUCCESS)
            return 13;
    }
    const auto send = [&](std::string_view text) {
        if (native)
        {
            ghostty_terminal_vt_write(native.get(), reinterpret_cast<const std::uint8_t *>(text.data()), text.size());
            return true;
        }
        return !engine->feed(std::as_bytes(std::span(text)));
    };
    const auto capture = [&]() -> std::expected<ztermy::terminal::TerminalSnapshot, std::error_code> {
        if (!native)
            return engine->snapshot();
        ztermy::terminal::TerminalSnapshot result;
        if (mode == "metadata-only" || mode == "iterator-only")
        {
            GhosttyKittyGraphics graphics = nullptr;
            GhosttyKittyGraphicsPlacementIterator iterator = nullptr;
            if (ghostty_terminal_get(native.get(), GHOSTTY_TERMINAL_DATA_KITTY_GRAPHICS, static_cast<void *>(&graphics))
                    != GHOSTTY_SUCCESS
                || ghostty_kitty_graphics_placement_iterator_new(nullptr, &iterator) != GHOSTTY_SUCCESS)
                return std::unexpected(std::make_error_code(std::errc::io_error));
            const auto status = ghostty_kitty_graphics_get(graphics, GHOSTTY_KITTY_GRAPHICS_DATA_PLACEMENT_ITERATOR,
                                                           static_cast<void *>(&iterator));
            if (status == GHOSTTY_SUCCESS)
                while (ghostty_kitty_graphics_placement_next(iterator))
                    result.images.emplace_back();
            ghostty_kitty_graphics_placement_iterator_free(iterator);
            if (status != GHOSTTY_SUCCESS)
                return std::unexpected(std::make_error_code(std::errc::io_error));
            return result;
        }
        bool changed = false;
        if (imageCapture.capture(native.get(), result.images, changed) != GHOSTTY_SUCCESS)
            return std::unexpected(std::make_error_code(std::errc::io_error));
        return result;
    };
    const auto worker = mode == "cross-thread" || mode == "alternating" ? std::make_unique<SnapshotWorker>() : nullptr;
    std::size_t captures = 0;
    const auto captureImage = [&] {
        const bool onWorker = worker && (mode != "alternating" || ++captures % 2 == 0);
        return onWorker ? worker->capture(capture) : capture();
    };
    if (!send("\x1b_Ga=T,f=24,s=1,v=1,i=1,p=1,C=1,q=2;/wAA\x1b\\"))
        return 3;
    if (mode == "iterator-only")
    {
        for (int i = 1; i <= 1000; ++i)
        {
            const auto snapshot = capture();
            if (!snapshot || snapshot->images.size() != 1 || (i % 100 == 0 && !measure(mode, i)))
                return 14;
        }
        return 0;
    }
    for (int batch = 1; batch <= 20; ++batch)
    {
        for (int i = 0; i < 5000; ++i)
            if (!send("\x1b_Ga=p,i=1,p=1,C=1,q=2;\x1b\\"))
                return 4;
        if (!measure("replace_placement", batch * 5000))
            return 1;
    }
    // Deliberately hold old immutable frames, then release them. This separates
    // live consumer ownership from a cache that accidentally retains old pixels.
    std::vector<ztermy::terminal::TerminalSnapshot> frames;
    std::vector<std::weak_ptr<const ztermy::terminal::TerminalImage>> pixels;
    const bool rgb = mode == "rgb" || mode == "streamed" || worker;
    const QByteArray encoded = QByteArray(qsizetype{512} * 512 * (rgb ? 3 : 4), '\xff').toBase64();
    for (int cycle = 0; cycle < cycles; ++cycle)
    {
        for (int frame = 0; frame < 16; ++frame)
        {
            for (qsizetype offset = 0; offset < encoded.size(); offset += 4096)
            {
                QByteArray command = offset != 0 ? "\x1b_Gm="
                                     : rgb       ? "\x1b_Ga=T,f=24,s=512,v=512,i=1,p=1,C=1,q=2,m="
                                                 : "\x1b_Ga=T,f=32,s=512,v=512,i=1,p=1,C=1,q=2,m=";
                command += offset + 4096 < encoded.size() ? "1;" : "0;";
                command += encoded.mid(offset, 4096) + "\x1b\\";
                if (!send(std::string_view(command.data(), static_cast<std::size_t>(command.size()))))
                    return 5;
                // Isolate snapshots taken while a multipart image is incomplete,
                // as a live transport can interleave output and UI frame delivery.
                if ((mode == "streamed" || worker) && offset % (qsizetype{4096} * 8) == 0 && !captureImage())
                    return 6;
            }
            if (mode != "upload-only")
            {
                auto snapshot = captureImage();
                if (!snapshot || snapshot->images.size() != 1)
                    return 6;
                pixels.push_back(snapshot->images.front().image);
                if (mode == "retain")
                    frames.push_back(std::move(*snapshot));
            }
            if (!measure(mode, cycle * 16 + frame + 1))
                return 1;
        }
        frames.clear();
        for (const auto &pixel : pixels)
            if (!pixel.expired())
                return 7;
        pixels.clear();
        if (!measure("release_frames", (cycle + 1) * 16) || !send("\x1b_Ga=d,d=A;\x1b\\"))
            return 8;
        const auto empty = captureImage();
        if (!empty || !empty->images.empty() || !measure("delete_image", cycle + 1))
            return 9;
    }
    engine.reset();
    native.reset();
    return measure("destroy_engine", 1) ? 0 : 1;
}
catch (...)
{
    return 10;
}
