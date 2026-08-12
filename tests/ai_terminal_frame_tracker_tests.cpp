#include "domain/ai/AiTerminalFrameTracker.h"

#include <QtTest/QTest>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
using ztermy::ai::AiTerminalFrameInput;
using ztermy::ai::AiTerminalFrameTracker;

[[nodiscard]] std::span<const std::byte> bytes(const std::string &value)
{
    return {reinterpret_cast<const std::byte *>(value.data()), value.size()};
}

class AiTerminalFrameTrackerTests final : public QObject
{
    Q_OBJECT

private slots:
    void returnsBoundedFullAndIncrementalFrames();
    void tracksIdleCursorAndAlternateScreenAcrossChunks();
};

void AiTerminalFrameTrackerTests::returnsBoundedFullAndIncrementalFrames()
{
    std::int64_t now = 100;
    AiTerminalFrameTracker tracker([&now] {
        return now;
    });
    tracker.observeFrame(AiTerminalFrameInput{.lines = {"one", "two"}, .columns = 80, .rows = 24});
    auto frame = tracker.snapshot();
    QCOMPARE(frame.revision, std::uint64_t{1});
    QVERIFY(frame.full);
    QCOMPARE(frame.lines.size(), std::size_t{2});

    now = 250;
    tracker.observeFrame(AiTerminalFrameInput{.lines = {"one", "changed"}, .columns = 80, .rows = 24});
    frame = tracker.snapshot(1);
    QCOMPARE(frame.revision, std::uint64_t{2});
    QVERIFY(!frame.full);
    QCOMPARE(frame.lines.size(), std::size_t{1});
    QCOMPARE(frame.lines.front().index, std::size_t{1});
    QCOMPARE(frame.lines.front().text, std::string("changed"));

    tracker.observeFrame(AiTerminalFrameInput{.lines = {"new", "changed"}, .columns = 80, .rows = 24});
    frame = tracker.snapshot(1);
    QVERIFY(frame.full);
    QVERIFY(frame.cursorExpired);

    const std::uint64_t beforeResize = frame.revision;
    tracker.observeFrame(AiTerminalFrameInput{.lines = {"new", "changed"}, .columns = 100, .rows = 30});
    frame = tracker.snapshot(beforeResize);
    QVERIFY(frame.full);
    QVERIFY(!frame.cursorExpired);
    QCOMPARE(frame.columns, std::uint16_t{100});
}

void AiTerminalFrameTrackerTests::tracksIdleCursorAndAlternateScreenAcrossChunks()
{
    std::int64_t now = 1'000;
    AiTerminalFrameTracker tracker([&now] {
        return now;
    });
    tracker.observeFrame(AiTerminalFrameInput{.lines = {"prompt"},
                                              .columns = 120,
                                              .rows = 30,
                                              .cursorColumn = 6,
                                              .cursorRow = 0,
                                              .cursorVisible = true});
    const std::string first = "\x1b[?10";
    const std::string second = "49h";
    tracker.observeOutput(bytes(first));
    tracker.observeOutput(bytes(second));
    auto frame = tracker.snapshot(1);
    QVERIFY(frame.alternateScreen);
    QVERIFY(!frame.full);
    QVERIFY(frame.lines.empty());
    QCOMPARE(frame.cursorColumn, std::uint16_t{6});

    now = 1'750;
    frame = tracker.snapshot(frame.revision);
    QCOMPARE(frame.idleMilliseconds, std::int64_t{750});
    const std::string leave = "\x1b[?1049l";
    tracker.observeOutput(bytes(leave));
    QVERIFY(!tracker.snapshot(frame.revision).alternateScreen);
}
} // namespace

QTEST_GUILESS_MAIN(AiTerminalFrameTrackerTests)

#include "ai_terminal_frame_tracker_tests.moc"
