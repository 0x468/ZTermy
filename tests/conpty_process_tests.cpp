#include "infrastructure/terminal/ConPtyProcess.h"

#include <QByteArray>
#include <QScopeGuard>
#include <QTest>

#include <Windows.h>
#include <TlHelp32.h>

#include <array>
#include <chrono>
#include <future>
#include <span>
#include <string_view>
#include <vector>

using namespace std::chrono_literals;

namespace
{

class ConPtyProcessTests final : public QObject
{
    Q_OBJECT

private slots:
    void rejectsInvalidDimensions();
    void capturesUtf8OutputFromChildProcess();
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

        for (int attempt = 0; attempt < 8 && !output.contains("ZTERMY_CONPTY_READY"); ++attempt)
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
    const auto cleanup = qScopeGuard([&children] {
        for (const HANDLE child : children)
        {
            if (WaitForSingleObject(child, 0) == WAIT_TIMEOUT)
                TerminateProcess(child, ERROR_CANCELLED);
            CloseHandle(child);
        }
    });
    PROCESSENTRY32W entry{.dwSize = sizeof(PROCESSENTRY32W)};
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry))
    {
        if (entry.th32ParentProcessID == GetCurrentProcessId() && _wcsicmp(entry.szExeFile, L"cmd.exe") == 0)
        {
            const HANDLE handle = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
                                              FALSE, entry.th32ProcessID);
            QVERIFY(handle != nullptr);
            children.push_back(handle);
        }
    }
    QCOMPARE(children.size(), std::size_t{2});
    first.close();
    second.close();
    for (const HANDLE child : children)
    {
        QCOMPARE(WaitForSingleObject(child, 5'000), DWORD{WAIT_OBJECT_0});
        DWORD exitCode = STILL_ACTIVE;
        QVERIFY(GetExitCodeProcess(child, &exitCode));
        QCOMPARE(exitCode, DWORD{ERROR_CANCELLED});
    }
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

QTEST_GUILESS_MAIN(ConPtyProcessTests)

#include "conpty_process_tests.moc"
