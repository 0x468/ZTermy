#include "application/forwarding/PortForwardingJob.h"

#include <QTest>

#include <atomic>
#include <chrono>

using namespace std::chrono_literals;

namespace
{

[[nodiscard]] ztermy::forwarding::PortForwardingRule validRule()
{
    return {
        .id = "local-test",
        .label = "Local test",
        .profileId = "profile-test",
        .type = ztermy::forwarding::PortForwardingType::Local,
        .bind = {.host = "127.0.0.1", .port = 49152},
        .destination = {.host = "127.0.0.1", .port = 22},
    };
}

[[nodiscard]] ztermy::ssh::SshConnectionRequest validRequest()
{
    return {
        .host = QStringLiteral("127.0.0.1"),
        .port = 1,
        .username = QStringLiteral("test"),
        .authentication = ztermy::ssh::SshAuthenticationMethod::Agent,
        .knownHostsPath = QStringLiteral("known_hosts"),
    };
}

} // namespace

class PortForwardingJobTests final : public QObject
{
    Q_OBJECT

private slots:
    void rejectsInvalidConfiguration();
    void ownsAndStopsWorkerLifecycle();
};

void PortForwardingJobTests::rejectsInvalidConfiguration()
{
    ztermy::forwarding::PortForwardingJob job;
    auto rule = validRule();
    rule.bind.port = 0;
    auto started = job.start(std::move(rule), validRequest(), {});
    QVERIFY(!started);
    QCOMPARE(started.error(), ztermy::forwarding::PortForwardingJobStartError::InvalidConfiguration);
    QCOMPARE(job.snapshot().state, ztermy::forwarding::PortForwardingJobState::Stopped);
}

void PortForwardingJobTests::ownsAndStopsWorkerLifecycle()
{
    ztermy::forwarding::PortForwardingJob job;
    std::atomic_uint32_t notifications = 0;
    auto started = job.start(validRule(), validRequest(), {}, [&](const auto &) {
        notifications.fetch_add(1, std::memory_order_relaxed);
    });
    QVERIFY(started);

    auto duplicate = job.start(validRule(), validRequest(), {});
    QVERIFY(!duplicate);
    QCOMPARE(duplicate.error(), ztermy::forwarding::PortForwardingJobStartError::AlreadyRunning);

    job.stop();
    const auto stopped = job.snapshot();
    QVERIFY(stopped.state == ztermy::forwarding::PortForwardingJobState::Stopped
            || stopped.state == ztermy::forwarding::PortForwardingJobState::Failed);
    QVERIFY(notifications.load(std::memory_order_relaxed) >= 1);

    started = job.start(validRule(), validRequest(), {});
    QVERIFY(started);
    job.stop();
}

QTEST_GUILESS_MAIN(PortForwardingJobTests)

#include "port_forwarding_job_tests.moc"
