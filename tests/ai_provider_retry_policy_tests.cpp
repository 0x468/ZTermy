#include "domain/ai/AiProviderRetryPolicy.h"

#include <QtTest/QTest>

namespace
{

using ztermy::ai::AiProviderError;
using ztermy::ai::AiProviderErrorCode;
using ztermy::ai::AiProviderRetryLimits;
using ztermy::ai::AiProviderRetryPolicy;

class AiProviderRetryPolicyTests final : public QObject
{
    Q_OBJECT

private slots:
    void retriesBoundedTransientFailures();
    void honorsBoundedRetryAfter();
    void rejectsPermanentAndCancelledFailures();
};

void AiProviderRetryPolicyTests::retriesBoundedTransientFailures()
{
    const AiProviderRetryPolicy policy(AiProviderRetryLimits{.maxRetries = 2,
                                                             .baseDelayMilliseconds = 500,
                                                             .maxDelayMilliseconds = 8'000,
                                                             .jitterPercent = 20});
    const AiProviderError error{.code = AiProviderErrorCode::network, .message = "offline", .retryable = true};

    const auto first = policy.decide(error, 0, 0.0);
    QVERIFY(first.retry);
    QCOMPARE(first.delayMilliseconds, std::uint64_t{400});

    const auto second = policy.decide(error, 1, 1.0);
    QVERIFY(second.retry);
    QCOMPARE(second.delayMilliseconds, std::uint64_t{1'200});

    QVERIFY(!policy.decide(error, 2).retry);
}

void AiProviderRetryPolicyTests::honorsBoundedRetryAfter()
{
    const AiProviderRetryPolicy policy(AiProviderRetryLimits{.maxRetries = 2,
                                                             .baseDelayMilliseconds = 500,
                                                             .maxDelayMilliseconds = 8'000,
                                                             .jitterPercent = 20});

    auto error = AiProviderError{.code = AiProviderErrorCode::rateLimited,
                                 .message = "slow down",
                                 .retryAfterMilliseconds = 2'500,
                                 .retryable = true};
    QCOMPARE(policy.decide(error, 0).delayMilliseconds, std::uint64_t{2'500});

    error.retryAfterMilliseconds = 60'000;
    QCOMPARE(policy.decide(error, 0).delayMilliseconds, std::uint64_t{8'000});
}

void AiProviderRetryPolicyTests::rejectsPermanentAndCancelledFailures()
{
    const AiProviderRetryPolicy policy;
    for (const auto code : {AiProviderErrorCode::authentication, AiProviderErrorCode::quotaExceeded,
                            AiProviderErrorCode::invalidRequest, AiProviderErrorCode::cancelled})
    {
        const AiProviderError error{.code = code, .message = "stop", .retryable = true};
        QVERIFY(!policy.decide(error, 0).retry);
    }

    const AiProviderError serverError{.code = AiProviderErrorCode::server,
                                      .message = "retry disabled",
                                      .retryable = false};
    QVERIFY(!policy.decide(serverError, 0).retry);
}

} // namespace

QTEST_GUILESS_MAIN(AiProviderRetryPolicyTests)

#include "ai_provider_retry_policy_tests.moc"
