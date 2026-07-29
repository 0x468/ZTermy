#include "platform/windows/CrashDiagnostics.h"

#include <QDir>
#include <QLoggingCategory>
#include <QStandardPaths>

#include <Windows.h>
#include <DbgHelp.h>

#if defined(_DEBUG)
#include <crtdbg.h>
#endif

#include <array>
#include <atomic>
#include <cwchar>
#include <string>

Q_LOGGING_CATEGORY(crashDiagnosticsLog, "ztermy.crash")

namespace
{

constexpr std::size_t pathCapacity = 1024;
std::array<wchar_t, pathCapacity> dumpDirectory{};
std::atomic_flag dumpInProgress = ATOMIC_FLAG_INIT;

[[nodiscard]] std::wstring makeDumpPath(const wchar_t *reason)
{
    SYSTEMTIME time{};
    GetLocalTime(&time);

    std::array<wchar_t, pathCapacity> path{};
    _snwprintf_s(path.data(), path.size(), _TRUNCATE, L"%ls\\ztermy-%04u%02u%02u-%02u%02u%02u-%lu-%ls.dmp",
                 dumpDirectory.data(), time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
                 GetCurrentProcessId(), reason);
    return path.data();
}

void writeMiniDump(EXCEPTION_POINTERS *exceptionPointers, const wchar_t *reason)
{
    if (dumpDirectory.front() == L'\0' || dumpInProgress.test_and_set())
    {
        return;
    }

    const std::wstring dumpPath = makeDumpPath(reason);
    const HANDLE dumpFile = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dumpFile != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION exceptionInformation{
            .ThreadId = GetCurrentThreadId(),
            .ExceptionPointers = exceptionPointers,
            .ClientPointers = FALSE,
        };
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dumpFile, MiniDumpWithThreadInfo,
                          exceptionPointers == nullptr ? nullptr : &exceptionInformation, nullptr, nullptr);
        CloseHandle(dumpFile);
    }

    dumpInProgress.clear();
}

LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS *exceptionPointers)
{
    writeMiniDump(exceptionPointers, L"unhandled");
    return EXCEPTION_CONTINUE_SEARCH;
}

#if defined(_DEBUG)
int crtReportHook(const int reportType, char *, int *)
{
    if (reportType == _CRT_ASSERT || reportType == _CRT_ERROR)
    {
        writeMiniDump(nullptr, reportType == _CRT_ASSERT ? L"crt-assert" : L"crt-error");
    }
    return FALSE;
}
#endif

} // namespace

namespace ztermy::diagnostics
{

void initialize()
{
    const QString directory =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/crashes");
    if (!QDir().mkpath(directory))
    {
        qCWarning(crashDiagnosticsLog) << "Unable to create crash dump directory" << directory;
        return;
    }

    const std::wstring nativeDirectory = QDir::toNativeSeparators(directory).toStdWString();
    wcsncpy_s(dumpDirectory.data(), dumpDirectory.size(), nativeDirectory.c_str(), _TRUNCATE);
    SetUnhandledExceptionFilter(&unhandledExceptionFilter);
#if defined(_DEBUG)
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, &crtReportHook);
#endif
    qCInfo(crashDiagnosticsLog) << "Crash diagnostics enabled"
                                << "directory=" << directory;
}

QString crashDirectoryPath()
{
    return QString::fromWCharArray(dumpDirectory.data());
}

} // namespace ztermy::diagnostics
