#include "platform/windows/WindowsProtectedClipboard.h"

#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <utility>

namespace ztermy::windowing
{
namespace
{

constexpr auto excludeFromMonitorFormat = L"ExcludeClipboardContentFromMonitorProcessing";
constexpr auto excludeFromHistoryFormat = L"CanIncludeInClipboardHistory";
constexpr auto excludeFromCloudFormat = L"CanUploadToCloudClipboard";
constexpr int clipboardOpenAttempts = 8;

struct GlobalMemoryDeleter final
{
    void operator()(void *memory) const noexcept { GlobalFree(memory); }
};

using GlobalMemory = std::unique_ptr<void, GlobalMemoryDeleter>;

[[nodiscard]] GlobalMemory clipboardMemory(const std::span<const std::byte> bytes)
{
    GlobalMemory memory(GlobalAlloc(GMEM_MOVEABLE, bytes.size()));
    if (!memory)
    {
        return {};
    }
    void *destination = GlobalLock(memory.get());
    if (destination == nullptr)
    {
        return {};
    }
    std::memcpy(destination, bytes.data(), bytes.size());
    GlobalUnlock(memory.get());
    return memory;
}

[[nodiscard]] bool publishClipboardMemory(const UINT format, GlobalMemory memory)
{
    if (!memory || SetClipboardData(format, memory.get()) == nullptr)
    {
        return false;
    }
    [[maybe_unused]] void *transferredMemory = memory.release();
    return true;
}

[[nodiscard]] bool openClipboardWithShortRetry(const HWND owner) noexcept
{
    for (int attempt = 0; attempt < clipboardOpenAttempts; ++attempt)
    {
        if (OpenClipboard(owner) != FALSE)
        {
            return true;
        }
        if (attempt + 1 < clipboardOpenAttempts)
        {
            Sleep(1);
        }
    }
    return false;
}

} // namespace

WindowsProtectedClipboard::WindowsProtectedClipboard(QObject *parent) : QObject(parent)
{
    m_ownerWindow = CreateWindowExW(0, L"STATIC", L"ztermy protected clipboard", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                    GetModuleHandleW(nullptr), nullptr);
    m_clearTimer.setSingleShot(true);
    QObject::connect(&m_clearTimer, &QTimer::timeout, this, &WindowsProtectedClipboard::clearIfUnchanged);
}

WindowsProtectedClipboard::~WindowsProtectedClipboard()
{
    if (m_ownerWindow != nullptr)
    {
        DestroyWindow(static_cast<HWND>(m_ownerWindow));
    }
}

bool WindowsProtectedClipboard::setProtectedText(const QString &text, const std::chrono::milliseconds autoClearAfter)
{
    if (m_ownerWindow == nullptr || text.isEmpty())
    {
        return false;
    }

    m_clearTimer.stop();
    m_expectedSequence = 0;

    const auto textBytes = std::as_bytes(std::span{text.utf16(), static_cast<std::size_t>(text.size() + 1)});
    GlobalMemory nativeText = clipboardMemory(textBytes);
    const DWORD disabled = 0;
    const auto disabledBytes = std::as_bytes(std::span{&disabled, std::size_t{1}});
    GlobalMemory monitorFlag = clipboardMemory(disabledBytes);
    GlobalMemory historyFlag = clipboardMemory(disabledBytes);
    GlobalMemory cloudFlag = clipboardMemory(disabledBytes);
    const UINT monitorFormat = RegisterClipboardFormatW(excludeFromMonitorFormat);
    const UINT historyFormat = RegisterClipboardFormatW(excludeFromHistoryFormat);
    const UINT cloudFormat = RegisterClipboardFormatW(excludeFromCloudFormat);
    if (!nativeText || !monitorFlag || !historyFlag || !cloudFlag || monitorFormat == 0 || historyFormat == 0
        || cloudFormat == 0 || !openClipboardWithShortRetry(static_cast<HWND>(m_ownerWindow)))
    {
        return false;
    }

    const bool emptied = EmptyClipboard() != FALSE;
    const bool published = emptied && publishClipboardMemory(CF_UNICODETEXT, std::move(nativeText))
                           && publishClipboardMemory(monitorFormat, std::move(monitorFlag))
                           && publishClipboardMemory(historyFormat, std::move(historyFlag))
                           && publishClipboardMemory(cloudFormat, std::move(cloudFlag));
    if (!published)
    {
        EmptyClipboard();
    }
    CloseClipboard();
    if (!published)
    {
        return false;
    }

    m_expectedSequence = GetClipboardSequenceNumber();
    if (autoClearAfter > std::chrono::milliseconds::zero() && m_expectedSequence != 0)
    {
        const auto boundedDelay = std::min<std::int64_t>(autoClearAfter.count(), std::numeric_limits<int>::max());
        m_clearTimer.start(static_cast<int>(boundedDelay));
    }
    return true;
}

void WindowsProtectedClipboard::clearIfUnchanged()
{
    const std::uint32_t expectedSequence = std::exchange(m_expectedSequence, 0);
    if (expectedSequence == 0 || GetClipboardSequenceNumber() != expectedSequence)
    {
        return;
    }

    if (m_ownerWindow != nullptr && openClipboardWithShortRetry(static_cast<HWND>(m_ownerWindow)))
    {
        EmptyClipboard();
        CloseClipboard();
    }
}

} // namespace ztermy::windowing
