#include "infrastructure/terminal/ConPtyProcess.h"
#ifndef ZTERMY_CONPTY_REDISTRIBUTABLE_PROBE
#include "infrastructure/terminal/ConPtyRuntime.h"
#endif

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTest>

#include <Windows.h>
#include <TlHelp32.h>

#include <array>
#include <chrono>
#include <future>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace std::chrono_literals;

namespace
{
constexpr std::string_view sixelProbe = "\x1bP0;1q\"1;1;1;6#1;2;100;0;0~\x1b\\";
constexpr std::string_view kittyProbe = "\x1b_Ga=T,f=24,s=1,v=1,i=987,C=1;/wAA\x1b\\";
constexpr std::string_view unicodeProbe = "\x1b[38;2;0;3;224m\xf4\x8e\xbb\xae\xcc\x85\x1b[0m";

class ConPtyProcessTests final : public QObject
{
    Q_OBJECT

private slots:
    void rejectsInvalidDimensions();
    void capturesUtf8OutputFromChildProcess();
    void drainsFinalOutputAfterNaturalExit();
    void preservesInlineImageProtocolsFromChildProcess();
#ifndef ZTERMY_CONPTY_REDISTRIBUTABLE_PROBE
    void pinsRuntimeBinariesAgainstReplacement();
#endif
    void newShellDoesNotInheritStaleParentPath();
    void closingParallelConsolesEndsOwnedShellProcesses();
    void wakeEventInterruptsExitWait();
};

void ConPtyProcessTests::rejectsInvalidDimensions()
{
    ztermy::terminal::ConPtyProcess process;

    const std::error_code zeroDimensionError =
        process.start(L"C:\\Windows\\System32\\cmd.exe", L"cmd.exe /d /s /c \"exit 0\"", {.columns = 0, .rows = 24});
    const std::error_code overflowingDimensionError = process.start(
        L"C:\\Windows\\System32\\cmd.exe", L"cmd.exe /d /s /c \"exit 0\"", {.columns = 80, .rows = 32768});

    QCOMPARE(zeroDimensionError, std::make_error_code(std::errc::invalid_argument));
    QCOMPARE(overflowingDimensionError, std::make_error_code(std::errc::invalid_argument));
    QVERIFY(!process.running());
}

void ConPtyProcessTests::capturesUtf8OutputFromChildProcess()
{
    ztermy::terminal::ConPtyProcess process;
    const std::error_code startError =
        process.start(L"C:\\Windows\\System32\\cmd.exe", L"cmd.exe /d /q", {.columns = 80, .rows = 24});
    QVERIFY2(!startError, startError.message().c_str());

    constexpr std::string_view command = "echo ZTERMY_CONPTY_READY\r\nexit\r\n";
    const std::error_code writeError = process.write(std::as_bytes(std::span(command)));
    QVERIFY2(!writeError, writeError.message().c_str());

    auto outputFuture = std::async(std::launch::async, [&process]() {
        QByteArray output;
        std::array<std::byte, 4096> buffer{};

        // Pipe reads are arbitrary fragments, not records. Native host versions
        // can emit more than eight small startup/control fragments.
        while (output.size() < qsizetype{64} * 1024 && !output.contains("ZTERMY_CONPTY_READY"))
        {
            const auto readResult = process.read(buffer);
            if (!readResult || *readResult == 0)
            {
                break;
            }
            output.append(reinterpret_cast<const char *>(buffer.data()), static_cast<qsizetype>(*readResult));
        }
        return output;
    });

    const auto exitResult = process.waitForExit(5s);
    if (!exitResult || !*exitResult)
        process.close();
    QVERIFY(exitResult.has_value());
    QVERIFY(*exitResult);
    const std::future_status outputStatus = outputFuture.wait_for(5s);
    if (outputStatus != std::future_status::ready)
    {
        process.close();
    }
    QCOMPARE(outputStatus, std::future_status::ready);
    QVERIFY(outputFuture.get().contains("ZTERMY_CONPTY_READY"));

    process.close();
    QVERIFY(!process.running());
}

void ConPtyProcessTests::drainsFinalOutputAfterNaturalExit()
{
    for (int iteration = 0; iteration < 3; ++iteration)
    {
        ztermy::terminal::ConPtyProcess process;
        const auto error = process.start(L"C:\\Windows\\System32\\cmd.exe",
                                         L"cmd.exe /d /s /c \"for /L %i in (1,1,80) do @echo ZTERMY_TAIL_%i\"",
                                         {.columns = 80, .rows = 24});
        QVERIFY2(!error, error.message().c_str());
        auto reader = std::async(std::launch::async, [&process] {
            QByteArray output;
            std::array<std::byte, 4096> buffer{};
            while (output.size() < qsizetype{1024} * 1024)
            {
                const auto read = process.read(buffer);
                if (!read || *read == 0)
                    break;
                output.append(reinterpret_cast<const char *>(buffer.data()), static_cast<qsizetype>(*read));
            }
            return output;
        });
        const auto exited = process.waitForExit(5s);
        const auto released = exited && *exited ? process.finishOutput() : std::make_error_code(std::errc::timed_out);
        const auto drained = !released && reader.wait_for(5s) == std::future_status::ready;
        if (!drained)
            process.close();
        QVERIFY(drained);
        const auto output = reader.get();
        QVERIFY(output.contains("ZTERMY_TAIL_1"));
        QVERIFY(output.contains("ZTERMY_TAIL_80"));
        process.close();
    }
}

void ConPtyProcessTests::preservesInlineImageProtocolsFromChildProcess()
{
    ztermy::terminal::ConPtyProcess process;
    const auto executable = QCoreApplication::applicationFilePath().toStdWString();
    const auto error =
        process.start(executable, L"\"" + executable + L"\" --emit-inline-images", {.columns = 80, .rows = 24});
    QVERIFY2(!error, error.message().c_str());
    auto reader = std::async(std::launch::async, [&] {
        QByteArray output;
        std::array<std::byte, 4096> buffer{};
        while (output.size() < qsizetype{64} * 1024 && !output.contains("ZTERMY_IMAGE_PROBE_DONE"))
        {
            const auto read = process.read(buffer);
            if (!read || !*read)
                break;
            output.append(reinterpret_cast<const char *>(buffer.data()), static_cast<qsizetype>(*read));
        }
        return output;
    });
    const auto exited = process.waitForExit(5s);
    if (!exited || !*exited || reader.wait_for(5s) != std::future_status::ready)
        process.close();
    QCOMPARE(reader.wait_for(0s), std::future_status::ready);
    const auto output = reader.get();
    process.close();
    QVERIFY(output.contains("ZTERMY_IMAGE_PROBE_DONE"));
    const bool sixel = output.contains(QByteArrayView(sixelProbe.data(), static_cast<qsizetype>(sixelProbe.size())));
    const bool kitty = output.contains(QByteArrayView(kittyProbe.data(), static_cast<qsizetype>(kittyProbe.size())));
    qInfo() << "ConPTY image forwarding" << "sixel=" << sixel << "kitty=" << kitty << "outputBytes=" << output.size();
    QVERIFY(sixel);
    QVERIFY(kitty);
    const bool unicode =
        output.contains(QByteArrayView(unicodeProbe.data(), static_cast<qsizetype>(unicodeProbe.size())));
    qInfo() << "Unicode transport probe" << unicode;
    QVERIFY(unicode);
}

#ifndef ZTERMY_CONPTY_REDISTRIBUTABLE_PROBE
void ConPtyProcessTests::pinsRuntimeBinariesAgainstReplacement()
{
    QVERIFY(!ztermy::terminal::conPtyApi().error);
    const HMODULE module = GetModuleHandleW(L"conpty.dll");
    QVERIFY(module != nullptr);
    std::array<wchar_t, 32768> filename{};
    const DWORD count = GetModuleFileNameW(module, filename.data(), static_cast<DWORD>(filename.size()));
    QVERIFY(count > 0 && count < filename.size());
    const QDir directory = QFileInfo(QString::fromWCharArray(filename.data(), static_cast<int>(count))).dir();
    for (const auto &name : {QStringLiteral("conpty.dll"), QStringLiteral("OpenConsole.exe")})
    {
        const auto path = directory.filePath(name).toStdWString();
        const HANDLE writable =
            CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        const DWORD error = GetLastError();
        if (writable != INVALID_HANDLE_VALUE)
            CloseHandle(writable);
        QVERIFY(writable == INVALID_HANDLE_VALUE);
        QCOMPARE(error, DWORD{ERROR_SHARING_VIOLATION});
    }
}
#endif

void ConPtyProcessTests::newShellDoesNotInheritStaleParentPath()
{
    const auto originalValue = [](const wchar_t *name) -> std::optional<std::wstring> {
        const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
        if (length == 0)
            return std::nullopt;
        std::wstring value(length, L'\0');
        const DWORD copied = GetEnvironmentVariableW(name, value.data(), length);
        if (copied == 0 || copied >= length)
            return std::nullopt;
        value.resize(copied);
        return value;
    };
    const auto previousTerm = originalValue(L"TERM");
    const auto previousColorTerm = originalValue(L"COLORTERM");
    const auto previousWtSession = originalValue(L"WT_SESSION");
    const auto restoreTerminalIdentity = qScopeGuard([&] {
        SetEnvironmentVariableW(L"TERM", previousTerm ? previousTerm->c_str() : nullptr);
        SetEnvironmentVariableW(L"COLORTERM", previousColorTerm ? previousColorTerm->c_str() : nullptr);
        SetEnvironmentVariableW(L"WT_SESSION", previousWtSession ? previousWtSession->c_str() : nullptr);
    });
    const DWORD pathLength = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    QVERIFY(pathLength > 0);
    std::wstring originalPath(pathLength, L'\0');
    const DWORD copied = GetEnvironmentVariableW(L"PATH", originalPath.data(), pathLength);
    QVERIFY(copied > 0 && copied < pathLength);
    originalPath.resize(copied);
    const auto restorePath = qScopeGuard([&originalPath] {
        SetEnvironmentVariableW(L"PATH", originalPath.c_str());
    });
    QVERIFY(SetEnvironmentVariableW(L"PATH", L"ZTERMY_STALE_PATH_SENTINEL"));
    QVERIFY(SetEnvironmentVariableW(L"TERM", L"dumb"));
    QVERIFY(SetEnvironmentVariableW(L"COLORTERM", L"ansi"));
    QVERIFY(SetEnvironmentVariableW(L"WT_SESSION", L"ZTERMY_PARENT_WT_SESSION"));
    const auto clearTransient = qScopeGuard([] {
        SetEnvironmentVariableW(L"ZTERMY_TRANSIENT_ENV_SENTINEL", nullptr);
    });
    QVERIFY(SetEnvironmentVariableW(L"ZTERMY_TRANSIENT_ENV_SENTINEL", L"ZTERMY_TRANSIENT_VALUE"));

    ztermy::terminal::ConPtyProcess process;
    const std::error_code startError = process.start(
        L"C:\\Windows\\System32\\cmd.exe",
        L"cmd.exe /d /s /c \"echo ZTERMY_PATH=%PATH% & echo ZTERMY_TERM=%TERM% & echo ZTERMY_COLORTERM=%COLORTERM% & "
        L"echo ZTERMY_WT_SESSION=%WT_SESSION% & echo ZTERMY_TRANSIENT=%ZTERMY_TRANSIENT_ENV_SENTINEL%\"",
        {.columns = 80, .rows = 24});
    QVERIFY2(!startError, startError.message().c_str());

    auto outputFuture = std::async(std::launch::async, [&process] {
        QByteArray output;
        std::array<std::byte, 4096> buffer{};
        while (output.size() < qsizetype{64} * 1024)
        {
            const auto readResult = process.read(buffer);
            if (!readResult || *readResult == 0)
                break;
            output.append(reinterpret_cast<const char *>(buffer.data()), static_cast<qsizetype>(*readResult));
            const qsizetype marker = output.indexOf("ZTERMY_PATH=");
            if (marker >= 0 && output.contains("ZTERMY_TRANSIENT=ZTERMY_TRANSIENT_VALUE"))
                break;
        }
        return output;
    });

    const auto exited = process.waitForExit(5s);
    if (!exited || !*exited)
        process.close();
    QVERIFY(exited.has_value());
    QVERIFY(*exited);
    if (outputFuture.wait_for(5s) != std::future_status::ready)
        process.close();
    QCOMPARE(outputFuture.wait_for(0s), std::future_status::ready);
    const QByteArray output = outputFuture.get();
    const qsizetype pathMarker = output.lastIndexOf("ZTERMY_PATH=");
    QVERIFY(pathMarker >= 0);
    const qsizetype pathStart = pathMarker + QByteArrayLiteral("ZTERMY_PATH=").size();
    const qsizetype pathEnd = output.indexOf('\n', pathStart);
    QVERIFY(pathEnd > pathStart);
    QVERIFY2(!output.mid(pathStart, pathEnd - pathStart).trimmed().isEmpty(),
             "The child must receive a non-empty registry-backed PATH");
    QVERIFY(!output.contains("ZTERMY_STALE_PATH_SENTINEL"));
    QVERIFY(output.contains("ZTERMY_TERM=xterm-256color"));
    QVERIFY(output.contains("ZTERMY_COLORTERM=truecolor"));
    QVERIFY(!output.contains("ZTERMY_PARENT_WT_SESSION"));
    QVERIFY(output.contains("ZTERMY_TRANSIENT=ZTERMY_TRANSIENT_VALUE"));
}

void ConPtyProcessTests::closingParallelConsolesEndsOwnedShellProcesses()
{
    ztermy::terminal::ConPtyProcess first;
    ztermy::terminal::ConPtyProcess second;
    QVERIFY(!first.start(L"C:\\Windows\\System32\\cmd.exe", L"cmd.exe /d /q", {.columns = 80, .rows = 24}));
    QVERIFY(!second.start(L"C:\\Windows\\System32\\cmd.exe", L"cmd.exe /d /q", {.columns = 80, .rows = 24}));
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    QVERIFY(snapshot != INVALID_HANDLE_VALUE);
    const auto closeSnapshot = qScopeGuard([snapshot] {
        CloseHandle(snapshot);
    });
    std::vector<HANDLE> children;
    std::vector<HANDLE> hosts;
    const auto cleanup = qScopeGuard([&children, &hosts] {
        for (const HANDLE child : children)
        {
            if (WaitForSingleObject(child, 0) == WAIT_TIMEOUT)
                TerminateProcess(child, ERROR_CANCELLED);
            CloseHandle(child);
        }
        for (const HANDLE host : hosts)
        {
            if (WaitForSingleObject(host, 0) == WAIT_TIMEOUT)
                TerminateProcess(host, ERROR_CANCELLED);
            CloseHandle(host);
        }
    });
    PROCESSENTRY32W entry{.dwSize = sizeof(PROCESSENTRY32W)};
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry))
    {
        if (entry.th32ParentProcessID != GetCurrentProcessId())
            continue;
        std::vector<HANDLE> *owned = nullptr;
        if (_wcsicmp(entry.szExeFile, L"cmd.exe") == 0)
            owned = &children;
        else if (_wcsicmp(entry.szExeFile, L"conhost.exe") == 0 || _wcsicmp(entry.szExeFile, L"OpenConsole.exe") == 0)
            owned = &hosts;
        if (owned != nullptr)
        {
            const HANDLE handle = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
                                              FALSE, entry.th32ProcessID);
            QVERIFY(handle != nullptr);
            owned->push_back(handle);
        }
    }
    QCOMPARE(children.size(), std::size_t{2});
    QCOMPARE(hosts.size(), std::size_t{2});
    first.close();
    second.close();
    for (const HANDLE child : children)
    {
        QCOMPARE(WaitForSingleObject(child, 5'000), DWORD{WAIT_OBJECT_0});
        DWORD exitCode = STILL_ACTIVE;
        QVERIFY(GetExitCodeProcess(child, &exitCode));
        QCOMPARE(exitCode, DWORD{ERROR_CANCELLED});
    }
    for (const HANDLE host : hosts)
        QCOMPARE(WaitForSingleObject(host, 5'000), DWORD{WAIT_OBJECT_0});
}

void ConPtyProcessTests::wakeEventInterruptsExitWait()
{
    using namespace std::chrono_literals;
    ztermy::terminal::ConPtyProcess process;
    const std::error_code startError =
        process.start(L"C:\\Windows\\System32\\cmd.exe", L"cmd.exe /d /q", {.columns = 80, .rows = 24});
    QVERIFY2(!startError, startError.message().c_str());
    const auto closeProcess = qScopeGuard([&process] {
        process.close();
    });

    const HANDLE wakeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    QVERIFY(wakeEvent != nullptr);
    const auto closeEvent = qScopeGuard([wakeEvent] {
        CloseHandle(wakeEvent);
    });

    // The shell is still running: a signalled event must end the wait early.
    SetEvent(wakeEvent);
    const auto started = std::chrono::steady_clock::now();
    const auto woken = process.waitForExitOrEvent(5s, wakeEvent);
    QVERIFY(woken.has_value());
    QVERIFY(!*woken);
    QVERIFY(std::chrono::steady_clock::now() - started < 2s);

    ResetEvent(wakeEvent);
    const auto timedOut = process.waitForExitOrEvent(0ms, wakeEvent);
    QVERIFY(timedOut.has_value());
    QVERIFY(!*timedOut);

    constexpr std::string_view command = "exit\r\n";
    QVERIFY(!process.write(std::as_bytes(std::span(command))));
    const auto exited = process.waitForExitOrEvent(5s, wakeEvent);
    QVERIFY(exited.has_value());
    QVERIFY(*exited);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--emit-inline-images")
    {
        const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (!GetConsoleMode(output, &mode) || !SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
            return 2;
        if (!SetConsoleOutputCP(CP_UTF8))
            return 2;
        for (const auto bytes : {sixelProbe, kittyProbe, unicodeProbe, std::string_view("ZTERMY_IMAGE_PROBE_DONE\r\n")})
        {
            DWORD written = 0;
            if (!WriteFile(output, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)
                || written != bytes.size())
                return 3;
        }
        return 0;
    }
    QCoreApplication application(argc, argv);
    ConPtyProcessTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "conpty_process_tests.moc"
