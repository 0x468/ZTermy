#include "domain/sftp/TransferQueue.h"

#include <QtTest/QTest>

#include <stdexcept>
#include <string>

namespace
{

ztermy::sftp::TransferTask task(const std::string &id)
{
    return ztermy::sftp::TransferTask{
        .id = id,
        .endpointId = "host-1",
        .displayName = id + ".txt",
        .sourcePath = "/remote/" + id,
        .destinationPath = "C:/local/" + id,
    };
}

class TransferQueueTests final : public QObject
{
    Q_OBJECT

private slots:
    void requiresPositiveConcurrency();
    void schedulesFifoWithinConcurrencyLimit();
    void validatesMonotonicProgressAndCompletion();
    void supportsFailureAttentionCancellationAndRetry();
    void restoresOnlyExplicitInterruptedTasks();
};

void TransferQueueTests::requiresPositiveConcurrency()
{
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, ztermy::sftp::TransferQueue(0));
}

void TransferQueueTests::schedulesFifoWithinConcurrencyLimit()
{
    ztermy::sftp::TransferQueue queue(2);
    QVERIFY(queue.enqueue(task("one")));
    QVERIFY(queue.enqueue(task("two")));
    QVERIFY(queue.enqueue(task("three")));
    QCOMPARE(queue.enqueue(task("one")).error(), ztermy::sftp::TransferQueueError::DuplicateTask);

    const auto first = queue.takeNext(100);
    const auto second = queue.takeNext(101);
    QVERIFY(first.has_value());
    QVERIFY(second.has_value());
    QCOMPARE(first ? first->id : std::string{}, std::string("one"));
    QCOMPARE(second ? second->id : std::string{}, std::string("two"));
    QVERIFY(!queue.takeNext(102).has_value());
    QCOMPARE(queue.runningCount(), std::size_t{2});

    QVERIFY(queue.complete("one", 200));
    const auto third = queue.takeNext(201);
    QVERIFY(third.has_value());
    QCOMPARE(third ? third->id : std::string{}, std::string("three"));
}

void TransferQueueTests::validatesMonotonicProgressAndCompletion()
{
    ztermy::sftp::TransferQueue queue;
    QVERIFY(queue.enqueue(task("download")));
    QVERIFY(queue.takeNext(100).has_value());
    QVERIFY(queue.updateProgress("download", 40, 100, 20));
    QCOMPARE(queue.updateProgress("download", 39, 100, 20).error(), ztermy::sftp::TransferQueueError::InvalidProgress);
    QCOMPARE(queue.updateProgress("download", 101, 100, 20).error(), ztermy::sftp::TransferQueueError::InvalidProgress);

    QVERIFY(queue.complete("download", 150));
    const auto *completed = queue.find("download");
    QVERIFY(completed != nullptr);
    QCOMPARE(completed->status, ztermy::sftp::TransferStatus::Completed);
    QCOMPARE(completed->transferredBytes, std::uint64_t{100});
    QCOMPARE(completed->bytesPerSecond, std::uint64_t{0});
    QCOMPARE(completed->finishedUtcMs, std::optional<std::int64_t>{150});
}

void TransferQueueTests::supportsFailureAttentionCancellationAndRetry()
{
    ztermy::sftp::TransferQueue queue;
    QVERIFY(queue.enqueue(task("failed")));
    QVERIFY(queue.enqueue(task("attention")));
    QVERIFY(queue.enqueue(task("cancelled")));

    QVERIFY(queue.takeNext(1).has_value());
    QVERIFY(queue.fail("failed", "connection-lost", true, 2));
    QVERIFY(queue.retry("failed"));
    QCOMPARE(queue.find("failed")->status, ztermy::sftp::TransferStatus::Queued);
    QVERIFY(queue.find("failed")->errorCode.empty());

    QVERIFY(queue.needsAttention("attention", "credential-locked"));
    QVERIFY(queue.retry("attention"));
    QCOMPARE(queue.find("attention")->status, ztermy::sftp::TransferStatus::Queued);

    QVERIFY(queue.cancel("cancelled", 3));
    QCOMPARE(queue.find("cancelled")->status, ztermy::sftp::TransferStatus::Cancelled);
    QCOMPARE(queue.retry("cancelled").error(), ztermy::sftp::TransferQueueError::InvalidTransition);
}

void TransferQueueTests::restoresOnlyExplicitInterruptedTasks()
{
    ztermy::sftp::TransferQueue queue;
    auto interrupted = task("interrupted");
    interrupted.status = ztermy::sftp::TransferStatus::Failed;
    interrupted.errorCode = "interrupted";
    interrupted.retryable = true;
    interrupted.transferredBytes = 40;
    interrupted.totalBytes = 100;

    QVERIFY(queue.restoreInterrupted(interrupted));
    QCOMPARE(queue.find("interrupted")->status, ztermy::sftp::TransferStatus::Failed);
    QVERIFY(queue.retry("interrupted"));
    QCOMPARE(queue.find("interrupted")->status, ztermy::sftp::TransferStatus::Queued);
    QCOMPARE(queue.restoreInterrupted(interrupted).error(), ztermy::sftp::TransferQueueError::DuplicateTask);

    interrupted.id = "ordinary-failure";
    interrupted.errorCode = "remote-io";
    QCOMPARE(queue.restoreInterrupted(interrupted).error(), ztermy::sftp::TransferQueueError::InvalidTask);
}

} // namespace

QTEST_GUILESS_MAIN(TransferQueueTests)

#include "transfer_queue_tests.moc"
