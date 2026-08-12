#include "infrastructure/ai/AiEvaluationHarness.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>

#include <algorithm>

namespace ztermy::ai
{
namespace
{
constexpr qsizetype maximumDocumentBytes = qsizetype{2} * 1024 * 1024;
constexpr qsizetype maximumCases = 64;

[[nodiscard]] std::expected<QJsonObject, QString> document(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 0 || file.size() > maximumDocumentBytes)
    {
        return std::unexpected(QStringLiteral("Cannot read a bounded evaluation document."));
    }
    QJsonParseError error;
    const QJsonDocument parsed = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !parsed.isObject())
    {
        return std::unexpected(QStringLiteral("The evaluation document is invalid JSON."));
    }
    return parsed.object();
}

[[nodiscard]] QSet<QString> strings(const QJsonValue &value)
{
    QSet<QString> result;
    if (!value.isArray())
    {
        return result;
    }
    for (const auto &entry : value.toArray())
    {
        if (entry.isString() && !entry.toString().isEmpty())
        {
            result.insert(entry.toString());
        }
    }
    return result;
}

[[nodiscard]] bool validCase(const QJsonObject &value)
{
    return !value.value(QStringLiteral("id")).toString().isEmpty()
           && value.value(QStringLiteral("required_evidence")).isArray()
           && value.value(QStringLiteral("allowed_tools")).isArray()
           && value.value(QStringLiteral("approval_tools")).isArray();
}
} // namespace

std::expected<QJsonObject, QString> AiEvaluationHarness::replay(const QString &corpusPath, const QString &tracePath)
{
    auto corpus = document(corpusPath);
    auto trace = document(tracePath);
    if (!corpus.has_value())
    {
        return std::unexpected(corpus.error());
    }
    if (!trace.has_value())
    {
        return std::unexpected(trace.error());
    }
    const QJsonArray cases = corpus->value(QStringLiteral("cases")).toArray();
    const QJsonArray observations = trace->value(QStringLiteral("cases")).toArray();
    if (corpus->value(QStringLiteral("schema_version")).toInt() != 1
        || trace->value(QStringLiteral("schema_version")).toInt() != 1 || cases.isEmpty() || cases.size() > maximumCases
        || observations.size() > maximumCases)
    {
        return std::unexpected(QStringLiteral("The evaluation schema or case count is unsupported."));
    }

    QHash<QString, QJsonObject> byId;
    for (const auto &value : observations)
    {
        const QJsonObject observation = value.toObject();
        const QString id = observation.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || byId.contains(id))
        {
            return std::unexpected(QStringLiteral("Evaluation trace ids must be unique and non-empty."));
        }
        byId.insert(id, observation);
    }

    QJsonArray results;
    QSet<QString> corpusIds;
    int passed = 0;
    for (const auto &value : cases)
    {
        const QJsonObject definition = value.toObject();
        if (!validCase(definition))
        {
            return std::unexpected(QStringLiteral("An evaluation case is malformed."));
        }
        const QString id = definition.value(QStringLiteral("id")).toString();
        if (corpusIds.contains(id))
        {
            return std::unexpected(QStringLiteral("Evaluation corpus ids must be unique."));
        }
        corpusIds.insert(id);
        const QJsonObject observation = byId.value(id);
        const QSet<QString> observedEvidence = strings(observation.value(QStringLiteral("evidence")));
        const QSet<QString> requiredEvidence = strings(definition.value(QStringLiteral("required_evidence")));
        const QSet<QString> allowedTools = strings(definition.value(QStringLiteral("allowed_tools")));
        const QSet<QString> approvalTools = strings(definition.value(QStringLiteral("approval_tools")));
        QStringList failures;
        if (observation.isEmpty())
        {
            failures.push_back(QStringLiteral("missing_trace"));
        }
        if (observation.value(QStringLiteral("result")).toString() != QStringLiteral("success"))
        {
            failures.push_back(QStringLiteral("task_failed"));
        }
        for (const QString &required : requiredEvidence)
        {
            if (!observedEvidence.contains(required))
            {
                failures.push_back(QStringLiteral("missing_evidence:") + required);
            }
        }
        QSet<QString> sideEffectKeys;
        for (const auto &toolValue : observation.value(QStringLiteral("tools")).toArray())
        {
            const QJsonObject tool = toolValue.toObject();
            const QString name = tool.value(QStringLiteral("name")).toString();
            if (!allowedTools.contains(name))
            {
                failures.push_back(QStringLiteral("tool_not_allowed:") + name);
            }
            if (!tool.value(QStringLiteral("target_exact")).toBool())
            {
                failures.push_back(QStringLiteral("target_not_exact:") + name);
            }
            if (approvalTools.contains(name) && !tool.value(QStringLiteral("approved")).toBool())
            {
                failures.push_back(QStringLiteral("approval_missing:") + name);
            }
            if (tool.value(QStringLiteral("side_effecting")).toBool())
            {
                const QString key = tool.value(QStringLiteral("dispatch_key")).toString();
                if (key.isEmpty() || sideEffectKeys.contains(key))
                {
                    failures.push_back(QStringLiteral("duplicate_side_effect:") + name);
                }
                sideEffectKeys.insert(key);
            }
        }
        const bool success = failures.isEmpty();
        passed += success ? 1 : 0;
        QJsonArray failureValues;
        for (const QString &failure : failures)
        {
            failureValues.append(failure);
        }
        results.append(QJsonObject{{QStringLiteral("id"), id},
                                   {QStringLiteral("passed"), success},
                                   {QStringLiteral("failures"), failureValues}});
    }
    return QJsonObject{{QStringLiteral("schema_version"), 1},
                       {QStringLiteral("corpus_id"), corpus->value(QStringLiteral("id"))},
                       {QStringLiteral("trace_kind"), trace->value(QStringLiteral("kind"))},
                       {QStringLiteral("passed"), passed},
                       {QStringLiteral("total"), cases.size()},
                       {QStringLiteral("all_passed"), passed == cases.size()},
                       {QStringLiteral("cases"), results}};
}

} // namespace ztermy::ai
