#pragma once

#include "domain/terminal/TerminalStatus.h"

#include <ghostty/vt.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <new>
#include <string_view>

namespace ztermy::terminal
{
// Worker-owned retained state: snapshots may be coalesced without losing the
// last progress report or replaying an already observed notification.
struct GhosttyStatusEvents final
{
    TerminalProgress progress;
    std::shared_ptr<const TerminalNotification> notification;
    std::chrono::steady_clock::time_point nextNotification{};
    std::uint64_t notificationSequence = 0;

    template <typename Owner, auto ValidateUtf8>
    [[nodiscard]] static GhosttyResult install(GhosttyTerminal terminal)
    {
        const GhosttyTerminalProgressReportFn progressCallback =
            [](GhosttyTerminal, void *userdata, const GhosttyTerminalProgressReport *report) noexcept {
                if (!userdata || !report || report->size < sizeof(GhosttyTerminalProgressReport))
                    return;
                auto &events = static_cast<Owner *>(userdata)->statusEvents;
                TerminalProgressState state;
                switch (report->state)
                {
                    case GHOSTTY_TERMINAL_PROGRESS_STATE_REMOVE:
                        state = TerminalProgressState::none;
                        break;
                    case GHOSTTY_TERMINAL_PROGRESS_STATE_SET:
                        state = TerminalProgressState::active;
                        break;
                    case GHOSTTY_TERMINAL_PROGRESS_STATE_ERROR:
                        state = TerminalProgressState::error;
                        break;
                    case GHOSTTY_TERMINAL_PROGRESS_STATE_INDETERMINATE:
                        state = TerminalProgressState::indeterminate;
                        break;
                    case GHOSTTY_TERMINAL_PROGRESS_STATE_PAUSE:
                        state = TerminalProgressState::paused;
                        break;
                    default:
                        return;
                }
                events.progress = {.state = state,
                                   .percentage = state == TerminalProgressState::none
                                                     ? -1
                                                     : std::clamp<int>(report->progress, -1, 100)};
            };
        const auto result = ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_PROGRESS_REPORT,
                                                 reinterpret_cast<const void *>(progressCallback));
        if (result != GHOSTTY_SUCCESS)
            return result;
        const GhosttyTerminalDesktopNotificationFn notificationCallback =
            [](GhosttyTerminal, void *userdata, const GhosttyTerminalDesktopNotification *request) noexcept {
                if (!userdata || !request || request->size < sizeof(GhosttyTerminalDesktopNotification)
                    || request->title.len > 512 || request->body.len > 4096
                    || (request->title.len && !request->title.ptr) || (request->body.len && !request->body.ptr))
                    return;
                auto &events = static_cast<Owner *>(userdata)->statusEvents;
                const auto now = std::chrono::steady_clock::now();
                if (now < events.nextNotification)
                    return;
                const std::string_view title(
                    request->title.len ? reinterpret_cast<const char *>(request->title.ptr) : "", request->title.len);
                const std::string_view body(request->body.len ? reinterpret_cast<const char *>(request->body.ptr) : "",
                                            request->body.len);
                if ((title.empty() && body.empty()) || !ValidateUtf8(title) || !ValidateUtf8(body))
                    return;
                try
                {
                    auto value = std::make_shared<TerminalNotification>();
                    value->sequence = ++events.notificationSequence;
                    value->title = title;
                    value->body = body;
                    events.notification = std::move(value);
                    events.nextNotification = now + std::chrono::seconds{2};
                }
                catch (const std::bad_alloc &)
                {
                    // Preserve the last valid notification and avoid allocation retry storms.
                    events.nextNotification = now + std::chrono::seconds{2};
                }
            };
        return ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_DESKTOP_NOTIFICATION,
                                    reinterpret_cast<const void *>(notificationCallback));
    }
};
} // namespace ztermy::terminal
