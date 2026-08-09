#include "domain/workbench/ScriptExecution.h"

#include <QTest>

#include <array>
#include <string_view>

namespace
{

[[nodiscard]] ztermy::workbench::RenderedScript sampleScript()
{
    using namespace ztermy::workbench;
    return {.id = "script-1",
            .name = "Deploy",
            .steps = {{.command = "prepare"},
                      {.command = "deploy",
                       .continuation = ScriptContinuation::literalOutput,
                       .outputMarker = "service ready",
                       .timeoutMs = 5'000},
                      {.command = "verify"}}};
}

[[nodiscard]] std::span<const std::byte> bytes(const std::string_view value)
{
    return std::as_bytes(std::span(value.data(), value.size()));
}

} // namespace

class ScriptExecutionTests final : public QObject
{
    Q_OBJECT

private slots:
    void dispatchesImmediateStepsThenWaitsAcrossChunks();
    void boundsOutputAndTimesOut();
    void fixesTargetAndCancelsWithoutLaterDispatch();
};

void ScriptExecutionTests::dispatchesImmediateStepsThenWaitsAcrossChunks()
{
    using namespace std::chrono_literals;
    ztermy::workbench::ScriptExecution execution;
    const auto started = execution.start(sampleScript(), "tab-1", 100ms);
    QVERIFY(started);
    QCOMPARE(*started, std::vector<std::string>({"prepare", "deploy"}));
    QCOMPARE(execution.snapshot().state, ztermy::workbench::ScriptExecutionState::waitingForOutput);

    QVERIFY(execution.observeOutput(bytes("noise service re"), 200ms).empty());
    const auto resumed = execution.observeOutput(bytes("ady prompt"), 300ms);
    QCOMPARE(resumed, std::vector<std::string>({"verify"}));
    QCOMPARE(execution.snapshot().state, ztermy::workbench::ScriptExecutionState::completed);
}

void ScriptExecutionTests::boundsOutputAndTimesOut()
{
    using namespace std::chrono_literals;
    ztermy::workbench::ScriptExecution execution;
    QVERIFY(execution.start(sampleScript(), "tab-1", 0ms));
    std::vector<std::byte> noise(ztermy::workbench::maximumOutputMatchWindowBytes * 2, std::byte{'x'});
    QVERIFY(execution.observeOutput(noise, 4'999ms).empty());
    QVERIFY(!execution.tick(4'999ms));
    QVERIFY(execution.tick(5'000ms));
    QCOMPARE(execution.snapshot().state, ztermy::workbench::ScriptExecutionState::timedOut);
    QVERIFY(execution.observeOutput(bytes("service ready"), 5'001ms).empty());
}

void ScriptExecutionTests::fixesTargetAndCancelsWithoutLaterDispatch()
{
    using namespace std::chrono_literals;
    ztermy::workbench::ScriptExecution execution;
    QVERIFY(execution.start(sampleScript(), "tab-fixed", 0ms));
    QCOMPARE(execution.snapshot().targetId, std::string("tab-fixed"));
    const auto secondStart = execution.start(sampleScript(), "tab-other", 0ms);
    QVERIFY(!secondStart);
    QCOMPARE(secondStart.error(), ztermy::workbench::ScriptExecutionError::alreadyActive);
    QVERIFY(execution.cancel());
    QVERIFY(!execution.active());
    QVERIFY(execution.observeOutput(bytes("service ready"), 1ms).empty());
    QVERIFY(!execution.cancel());
}

QTEST_GUILESS_MAIN(ScriptExecutionTests)

#include "script_execution_tests.moc"
