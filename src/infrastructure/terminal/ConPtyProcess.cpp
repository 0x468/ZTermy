#include "infrastructure/terminal/ConPtyProcess.h"

#include <Windows.h>

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace
{

class UniqueHandle final
{
public:
    UniqueHandle() = default;
    explicit UniqueHandle(const HANDLE handle) noexcept : m_handle(handle) {}

    ~UniqueHandle() { reset(); }

    UniqueHandle(const UniqueHandle &) = delete;
    UniqueHandle &operator=(const UniqueHandle &) = delete;

    UniqueHandle(UniqueHandle &&other) noexcept : m_handle(other.release()) {}

    UniqueHandle &operator=(UniqueHandle &&other) noexcept
    {
        if (this != &other)
        {
            reset(other.release());
        }
        return *this;
    }

    [[nodiscard]] HANDLE get() const noexcept { return m_handle; }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE;
    }

    [[nodiscard]] HANDLE release() noexcept { return std::exchange(m_handle, INVALID_HANDLE_VALUE); }

    void reset(const HANDLE replacement = INVALID_HANDLE_VALUE) noexcept
    {
        if (*this)
        {
            CloseHandle(m_handle);
        }
        m_handle = replacement;
    }

private:
    HANDLE m_handle = INVALID_HANDLE_VALUE;
};

class UniquePseudoConsole final
{
public:
    UniquePseudoConsole() = default;
    explicit UniquePseudoConsole(const HPCON handle) noexcept : m_handle(handle) {}

    ~UniquePseudoConsole() { reset(); }

    UniquePseudoConsole(const UniquePseudoConsole &) = delete;
    UniquePseudoConsole &operator=(const UniquePseudoConsole &) = delete;

    UniquePseudoConsole(UniquePseudoConsole &&other) noexcept : m_handle(other.release()) {}

    UniquePseudoConsole &operator=(UniquePseudoConsole &&other) noexcept
    {
        if (this != &other)
        {
            reset(other.release());
        }
        return *this;
    }

    [[nodiscard]] HPCON get() const noexcept { return m_handle; }

    [[nodiscard]] explicit operator bool() const noexcept { return m_handle != nullptr; }

    [[nodiscard]] HPCON release() noexcept { return std::exchange(m_handle, nullptr); }

    void reset(const HPCON replacement = nullptr) noexcept
    {
        if (*this)
        {
            ClosePseudoConsole(m_handle);
        }
        m_handle = replacement;
    }

private:
    HPCON m_handle = nullptr;
};

[[nodiscard]] std::error_code lastSystemError() noexcept
{
    return {static_cast<int>(GetLastError()), std::system_category()};
}

[[nodiscard]] std::error_code hresultError(const HRESULT result) noexcept
{
    if (HRESULT_FACILITY(result) == FACILITY_WIN32)
    {
        return {static_cast<int>(HRESULT_CODE(result)), std::system_category()};
    }
    return {static_cast<int>(result), std::system_category()};
}

[[nodiscard]] std::error_code invalidArgument() noexcept
{
    return std::make_error_code(std::errc::invalid_argument);
}

} // namespace

namespace ztermy::terminal
{

struct ConPtyProcess::Impl
{
    UniqueHandle inputWrite;
    UniqueHandle outputRead;
    UniqueHandle process;
    UniqueHandle processThread;
    UniquePseudoConsole pseudoConsole;
};

ConPtyProcess::ConPtyProcess() : m_impl(std::make_unique<Impl>()) {}

ConPtyProcess::~ConPtyProcess()
{
    close();
}

std::error_code ConPtyProcess::start(std::wstring commandLine, const TerminalSize size,
                                     const std::wstring_view workingDirectory)
{
    if (commandLine.empty() || !size.valid())
    {
        return invalidArgument();
    }
    if (m_impl->process || m_impl->pseudoConsole)
    {
        return std::make_error_code(std::errc::operation_in_progress);
    }

    HANDLE inputReadRaw = INVALID_HANDLE_VALUE;
    HANDLE inputWriteRaw = INVALID_HANDLE_VALUE;
    if (CreatePipe(&inputReadRaw, &inputWriteRaw, nullptr, 0) == FALSE)
    {
        return lastSystemError();
    }
    UniqueHandle inputRead(inputReadRaw);
    UniqueHandle inputWrite(inputWriteRaw);

    HANDLE outputReadRaw = INVALID_HANDLE_VALUE;
    HANDLE outputWriteRaw = INVALID_HANDLE_VALUE;
    if (CreatePipe(&outputReadRaw, &outputWriteRaw, nullptr, 0) == FALSE)
    {
        return lastSystemError();
    }
    UniqueHandle outputRead(outputReadRaw);
    UniqueHandle outputWrite(outputWriteRaw);

    HPCON pseudoConsoleRaw = nullptr;
    const COORD consoleSize{
        .X = static_cast<SHORT>(size.columns),
        .Y = static_cast<SHORT>(size.rows),
    };
    const HRESULT pseudoConsoleResult =
        CreatePseudoConsole(consoleSize, inputRead.get(), outputWrite.get(), 0, &pseudoConsoleRaw);
    if (FAILED(pseudoConsoleResult))
    {
        return hresultError(pseudoConsoleResult);
    }
    UniquePseudoConsole pseudoConsole(pseudoConsoleRaw);

    inputRead.reset();
    outputWrite.reset();

    SIZE_T attributeListBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeListBytes);
    if (attributeListBytes == 0)
    {
        return lastSystemError();
    }

    auto *attributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
        HeapAlloc(GetProcessHeap(), 0, attributeListBytes)); // NOLINT(performance-no-int-to-ptr)
    if (attributeList == nullptr)
    {
        return std::make_error_code(std::errc::not_enough_memory);
    }
    if (InitializeProcThreadAttributeList(attributeList, 1, 0, &attributeListBytes) == FALSE)
    {
        const std::error_code error = lastSystemError();
        HeapFree(GetProcessHeap(), 0, attributeList);
        return error;
    }

    const auto deleteAttributeList = [&attributeList]() noexcept {
        DeleteProcThreadAttributeList(attributeList);
        HeapFree(GetProcessHeap(), 0, attributeList);
    };

    if (UpdateProcThreadAttribute(attributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pseudoConsole.get(),
                                  sizeof(HPCON), nullptr, nullptr)
        == FALSE)
    {
        const std::error_code error = lastSystemError();
        deleteAttributeList();
        return error;
    }

    STARTUPINFOEXW startupInfo{};
    startupInfo.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    // Prevent console children from preferring redirected or console-backed handles inherited
    // from the terminal host over the pseudoconsole attached through the attribute list.
    startupInfo.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.StartupInfo.hStdInput = INVALID_HANDLE_VALUE;
    startupInfo.StartupInfo.hStdOutput = INVALID_HANDLE_VALUE;
    startupInfo.StartupInfo.hStdError = INVALID_HANDLE_VALUE;
    startupInfo.lpAttributeList = attributeList;

    PROCESS_INFORMATION processInformation{};
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    std::wstring workingDirectoryStorage(workingDirectory);
    const wchar_t *workingDirectoryPointer =
        workingDirectoryStorage.empty() ? nullptr : workingDirectoryStorage.c_str();

    const BOOL processCreated =
        CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, EXTENDED_STARTUPINFO_PRESENT, nullptr,
                       workingDirectoryPointer, &startupInfo.StartupInfo, &processInformation);
    const std::error_code processError = processCreated == FALSE ? lastSystemError() : std::error_code{};
    deleteAttributeList();

    if (processCreated == FALSE)
    {
        return processError;
    }

    m_impl->inputWrite = std::move(inputWrite);
    m_impl->outputRead = std::move(outputRead);
    m_impl->process = UniqueHandle(processInformation.hProcess);
    m_impl->processThread = UniqueHandle(processInformation.hThread);
    m_impl->pseudoConsole = std::move(pseudoConsole);
    return {};
}

std::expected<std::size_t, std::error_code> ConPtyProcess::read(const std::span<std::byte> destination)
{
    if (!m_impl->outputRead)
    {
        return std::unexpected(std::make_error_code(std::errc::not_connected));
    }
    if (destination.empty())
    {
        return std::size_t{0};
    }

    const DWORD requested =
        static_cast<DWORD>(std::min<std::size_t>(destination.size(), std::numeric_limits<DWORD>::max()));
    DWORD bytesRead = 0;
    if (ReadFile(m_impl->outputRead.get(), destination.data(), requested, &bytesRead, nullptr) == FALSE)
    {
        const DWORD error = GetLastError();
        if (error == ERROR_BROKEN_PIPE || error == ERROR_OPERATION_ABORTED)
        {
            return std::size_t{0};
        }
        return std::unexpected(std::error_code{static_cast<int>(error), std::system_category()});
    }
    return static_cast<std::size_t>(bytesRead);
}

std::error_code ConPtyProcess::write(const std::span<const std::byte> source)
{
    if (!m_impl->inputWrite)
    {
        return std::make_error_code(std::errc::not_connected);
    }

    std::size_t offset = 0;
    while (offset < source.size())
    {
        const DWORD requested =
            static_cast<DWORD>(std::min<std::size_t>(source.size() - offset, std::numeric_limits<DWORD>::max()));
        DWORD bytesWritten = 0;
        if (WriteFile(m_impl->inputWrite.get(), source.data() + offset, requested, &bytesWritten, nullptr) == FALSE)
        {
            return lastSystemError();
        }
        if (bytesWritten == 0)
        {
            return std::make_error_code(std::errc::io_error);
        }
        offset += bytesWritten;
    }
    return {};
}

std::error_code ConPtyProcess::resize(const TerminalSize size)
{
    if (!size.valid())
    {
        return invalidArgument();
    }
    if (!m_impl->pseudoConsole)
    {
        return std::make_error_code(std::errc::not_connected);
    }

    const COORD consoleSize{
        .X = static_cast<SHORT>(size.columns),
        .Y = static_cast<SHORT>(size.rows),
    };
    const HRESULT result = ResizePseudoConsole(m_impl->pseudoConsole.get(), consoleSize);
    return SUCCEEDED(result) ? std::error_code{} : hresultError(result);
}

std::expected<bool, std::error_code> ConPtyProcess::waitForExit(const std::chrono::milliseconds timeout) const
{
    if (!m_impl->process)
    {
        return std::unexpected(std::make_error_code(std::errc::not_connected));
    }

    const auto timeoutCount = std::clamp<std::int64_t>(timeout.count(), 0, std::numeric_limits<DWORD>::max() - 1);
    const DWORD waitResult = WaitForSingleObject(m_impl->process.get(), static_cast<DWORD>(timeoutCount));
    if (waitResult == WAIT_OBJECT_0)
    {
        return true;
    }
    if (waitResult == WAIT_TIMEOUT)
    {
        return false;
    }
    return std::unexpected(lastSystemError());
}

bool ConPtyProcess::running() const noexcept
{
    return m_impl->process && WaitForSingleObject(m_impl->process.get(), 0) == WAIT_TIMEOUT;
}

void ConPtyProcess::close() noexcept
{
    if (!m_impl)
    {
        return;
    }

    m_impl->inputWrite.reset();
    if (m_impl->outputRead)
    {
        CancelIoEx(m_impl->outputRead.get(), nullptr);
    }
    m_impl->outputRead.reset();
    m_impl->pseudoConsole.reset();
    m_impl->processThread.reset();
    m_impl->process.reset();
}

} // namespace ztermy::terminal
