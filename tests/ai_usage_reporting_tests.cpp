#include "domain/ai/AiUsageReporting.h"

#include <QtTest/QTest>

#include <cmath>

namespace
{

using ztermy::ai::AiProviderKind;
using ztermy::ai::AiTokenUsage;
using ztermy::ai::AiUsageEstimator;

class AiUsageReportingTests final : public QObject
{
    Q_OBJECT

private slots:
    void estimatesOfficialOpenAiStandardRates();
    void appliesCachedAndLongContextRates();
    void declinesUnknownOrThirdPartyPricing();
};

void AiUsageReportingTests::estimatesOfficialOpenAiStandardRates()
{
    const auto estimate =
        AiUsageEstimator::estimate(AiProviderKind::openAiResponses, "GPT-5.6-Terra",
                                   AiTokenUsage{.inputTokens = 200'000, .outputTokens = 100'000}, true);

    QVERIFY(estimate.usd.has_value());
    QVERIFY(std::abs(*estimate.usd - 2.0) < 0.000001);
    QCOMPARE(estimate.catalogDate, std::string_view("2026-08-12"));
    QVERIFY(!estimate.longContextRatesApplied);
}

void AiUsageReportingTests::appliesCachedAndLongContextRates()
{
    const auto estimate = AiUsageEstimator::estimate(
        AiProviderKind::openAiResponses, "gpt-5.6-luna",
        AiTokenUsage{.inputTokens = 300'000, .outputTokens = 100'000, .cachedInputTokens = 100'000}, true);

    QVERIFY(estimate.usd.has_value());
    // 200k uncached at $2/M + 100k cached at $0.20/M + 100k output at $9/M.
    QVERIFY(std::abs(*estimate.usd - 1.32) < 0.000001);
    QVERIFY(estimate.longContextRatesApplied);
}

void AiUsageReportingTests::declinesUnknownOrThirdPartyPricing()
{
    const AiTokenUsage usage{.inputTokens = 100, .outputTokens = 20};
    QVERIFY(!AiUsageEstimator::estimate(AiProviderKind::ollama, "gpt-5.6", usage, true).usd.has_value());
    QVERIFY(!AiUsageEstimator::estimate(AiProviderKind::openAiResponses, "gpt-5.6", usage, false).usd.has_value());
    QVERIFY(!AiUsageEstimator::estimate(AiProviderKind::openAiResponses, "gpt-5-mini", usage, true).usd.has_value());
    QVERIFY(!AiUsageEstimator::estimate(AiProviderKind::openAiResponses, "gpt-5.4-mini", usage, true).usd.has_value());
    QVERIFY(!AiUsageEstimator::estimate(AiProviderKind::openAiResponses, "custom-model", usage, true).usd.has_value());
}

} // namespace

QTEST_GUILESS_MAIN(AiUsageReportingTests)

#include "ai_usage_reporting_tests.moc"
