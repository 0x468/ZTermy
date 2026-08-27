#include "infrastructure/terminal/ConPtyProcess.h"

#include <QByteArray>
#include <QTest>

#include <array>
#include <chrono>
#include <future>
#include <span>
#include <string_view>

using namespace std::chrono_literals;

namespace
{

class ConPtyProcessTests final : public QObject
{
    Q_OBJECT

private slots:
    void rejectsInvalidDimensions();
    void capturesUtf8OutputFromChildProcess();
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

} // namespace

QTEST_GUILESS_MAIN(ConPtyProcessTests)

#include "conpty_process_tests.moc"
