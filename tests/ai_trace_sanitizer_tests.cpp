#include "infrastructure/ai/AiTraceSanitizer.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

namespace
{

using ztermy::ai::sanitizeAiTraceValue;

class AiTraceSanitizerTests final : public QObject
{
    Q_OBJECT

private slots:
    void omitsProviderNativeImagePayloads();
    void preservesOrdinaryProviderContent();
};

void AiTraceSanitizerTests::omitsProviderNativeImagePayloads()
{
    const QJsonObject source{{QStringLiteral("type"), QStringLiteral("base64")},
                             {QStringLiteral("media_type"), QStringLiteral("image/png")},
                             {QStringLiteral("data"), QStringLiteral("QUJDRA==")}};
    const QJsonObject request{
        {QStringLiteral("input_image"), QStringLiteral("data:image/png;base64,QUJDRA==")},
        {QStringLiteral("source"), source},
        {QStringLiteral("images"), QJsonArray{QStringLiteral("QUJDRA==")}},
    };

    const QJsonObject sanitized = sanitizeAiTraceValue(request).toObject();
    const QString dataUrl = sanitized.value(QStringLiteral("input_image")).toString();
    QVERIFY(dataUrl.startsWith(QStringLiteral("data:image/png;base64,")));
    QVERIFY(dataUrl.contains(QStringLiteral("omitted from trace")));
    QVERIFY(!dataUrl.contains(QStringLiteral("QUJDRA==")));
    const QString anthropicData =
        sanitized.value(QStringLiteral("source")).toObject().value(QStringLiteral("data")).toString();
    QVERIFY(anthropicData.contains(QStringLiteral("8 base64 characters")));
    const QString ollamaData = sanitized.value(QStringLiteral("images")).toArray().first().toString();
    QVERIFY(ollamaData.contains(QStringLiteral("8 base64 characters")));
}

void AiTraceSanitizerTests::preservesOrdinaryProviderContent()
{
    const QJsonObject request{{QStringLiteral("data"), QStringLiteral("ordinary response text")},
                              {QStringLiteral("images"), QJsonArray{QJsonObject{{QStringLiteral("id"), 7}}}}};
    QCOMPARE(sanitizeAiTraceValue(request), QJsonValue(request));
}

} // namespace

QTEST_GUILESS_MAIN(AiTraceSanitizerTests)

#include "ai_trace_sanitizer_tests.moc"
