#include "domain/ai/AiContextRedactor.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>
#include <QString>

#include <algorithm>
#include <functional>
#include <numeric>
#include <utility>

namespace ztermy::ai
{
namespace
{

using Replacement = std::function<QString(const QRegularExpressionMatch &)>;

[[nodiscard]] constexpr std::size_t kindIndex(const AiSecretKind kind) noexcept
{
    return static_cast<std::size_t>(kind);
}

[[nodiscard]] QString fromUtf8(const std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] std::string utf8(const QString &value)
{
    const auto bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] QString placeholder(const AiSecretKind kind)
{
    switch (kind)
    {
    case AiSecretKind::privateKey:
        return QStringLiteral("[REDACTED:PRIVATE_KEY]");
    case AiSecretKind::authorizationHeader:
        return QStringLiteral("[REDACTED:AUTHORIZATION]");
    case AiSecretKind::uriCredential:
        return QStringLiteral("[REDACTED:URI_CREDENTIAL]");
    case AiSecretKind::apiKey:
        return QStringLiteral("[REDACTED:API_KEY]");
    case AiSecretKind::shellAssignment:
        return QStringLiteral("[REDACTED:SHELL_SECRET]");
    case AiSecretKind::userRule:
        return QStringLiteral("[REDACTED:USER_RULE]");
    case AiSecretKind::count:
        break;
    }
    return QStringLiteral("[REDACTED]");
}

void replacePattern(QString &text,
                    const QRegularExpression &expression,
                    const AiSecretKind kind,
                    AiRedactionResult &result,
                    const Replacement &replacement)
{
    auto iterator = expression.globalMatch(text);
    QString output;
    qsizetype consumed = 0;
    std::size_t matches = 0;
    while (iterator.hasNext())
    {
        const auto match = iterator.next();
        if (!match.hasMatch())
        {
            continue;
        }
        output += QStringView(text).mid(consumed, match.capturedStart() - consumed);
        output += replacement(match);
        consumed = match.capturedEnd();
        ++matches;
    }
    if (matches == 0)
    {
        return;
    }
    output += QStringView(text).mid(consumed);
    text = std::move(output);
    result.counts[kindIndex(kind)] += matches;
}

void replaceLiteral(QString &text,
                    const QString &literal,
                    const Qt::CaseSensitivity sensitivity,
                    AiRedactionResult &result)
{
    QString output;
    qsizetype consumed = 0;
    std::size_t matches = 0;
    auto position = text.indexOf(literal, consumed, sensitivity);
    while (position >= 0)
    {
        output += QStringView(text).mid(consumed, position - consumed);
        output += placeholder(AiSecretKind::userRule);
        consumed = position + literal.size();
        ++matches;
        position = text.indexOf(literal, consumed, sensitivity);
    }
    if (matches == 0)
    {
        return;
    }
    output += QStringView(text).mid(consumed);
    text = std::move(output);
    result.counts[kindIndex(AiSecretKind::userRule)] += matches;
}

[[nodiscard]] QRegularExpression builtInExpression(const QString &pattern,
                                                   const bool caseInsensitive = false)
{
    QRegularExpression::PatternOptions options = QRegularExpression::UseUnicodePropertiesOption;
    if (caseInsensitive)
    {
        options |= QRegularExpression::CaseInsensitiveOption;
    }
    return QRegularExpression(pattern, options);
}

void applyBuiltInRules(QString &text, AiRedactionResult &result)
{
    static const auto privateKey = builtInExpression(
        QStringLiteral(R"((-----BEGIN ([A-Z0-9 ]*PRIVATE KEY)-----)[\s\S]{0,65535}?-----END \2-----)"));
    static const auto authorization = builtInExpression(
        QStringLiteral(R"((Authorization[ \t]*:[ \t]*)(?:Bearer|Basic)[ \t]+[A-Za-z0-9._~+/=\-]{4,})"),
        true);
    static const auto uriCredential = builtInExpression(
        QStringLiteral(R"(([A-Za-z][A-Za-z0-9+.-]*://)[^/\s:@]+:[^/\s@]+@)"));
    static const auto apiKey = builtInExpression(
        QStringLiteral(R"((?:sk-(?:proj-)?[A-Za-z0-9_-]{16,}|gh[pousr]_[A-Za-z0-9]{20,}|AKIA[0-9A-Z]{16}|AIza[0-9A-Za-z_-]{30,}|xox[baprs]-[A-Za-z0-9-]{10,}))"));
    static const auto shellAssignment = builtInExpression(
        QStringLiteral(R"(([A-Z_][A-Z0-9_]*(?:PASSWORD|PASSWD|TOKEN|SECRET|API_KEY|PRIVATE_KEY|ACCESS_KEY)[A-Z0-9_]*[ \t]*=[ \t]*)(?!\[REDACTED:)(?:"[^"\r\n]*"|'[^'\r\n]*'|[^\s;\r\n]+))"),
        true);

    replacePattern(text, privateKey, AiSecretKind::privateKey, result, [](const auto &) {
        return placeholder(AiSecretKind::privateKey);
    });
    replacePattern(text, authorization, AiSecretKind::authorizationHeader, result, [](const auto &match) {
        return match.captured(1) + placeholder(AiSecretKind::authorizationHeader);
    });
    replacePattern(text, uriCredential, AiSecretKind::uriCredential, result, [](const auto &match) {
        return match.captured(1) + placeholder(AiSecretKind::uriCredential) + '@';
    });
    replacePattern(text, apiKey, AiSecretKind::apiKey, result, [](const auto &) {
        return placeholder(AiSecretKind::apiKey);
    });
    replacePattern(text, shellAssignment, AiSecretKind::shellAssignment, result, [](const auto &match) {
        return match.captured(1) + placeholder(AiSecretKind::shellAssignment);
    });
}

void applyUserRules(QString &text,
                    const std::span<const AiUserRedactionRule> rules,
                    AiRedactionResult &result)
{
    for (const auto &rule : rules)
    {
        if (!rule.enabled)
        {
            continue;
        }
        if (rule.pattern.empty() || rule.pattern.size() > 512)
        {
            result.invalidRuleIds.push_back(rule.id);
            continue;
        }
        if (rule.type == AiRedactionRuleType::literal)
        {
            replaceLiteral(text,
                           fromUtf8(rule.pattern),
                           rule.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive,
                           result);
            continue;
        }

        QRegularExpression::PatternOptions options = QRegularExpression::UseUnicodePropertiesOption;
        if (!rule.caseSensitive)
        {
            options |= QRegularExpression::CaseInsensitiveOption;
        }
        const auto boundedPattern = QStringLiteral("(*LIMIT_MATCH=100000)(*LIMIT_DEPTH=1000)")
                                    + fromUtf8(rule.pattern);
        const QRegularExpression expression(boundedPattern, options);
        if (!expression.isValid())
        {
            result.invalidRuleIds.push_back(rule.id);
            continue;
        }
        replacePattern(text, expression, AiSecretKind::userRule, result, [](const auto &) {
            return placeholder(AiSecretKind::userRule);
        });
    }
}

} // namespace

std::size_t AiRedactionResult::totalRedactions() const noexcept
{
    return std::accumulate(counts.begin(), counts.end(), std::size_t{0});
}

AiRedactionResult AiContextRedactor::redact(const std::string_view text,
                                            const std::span<const AiUserRedactionRule> userRules) const
{
    AiRedactionResult result;
    auto normalized = fromUtf8(text);
    applyBuiltInRules(normalized, result);
    applyUserRules(normalized, userRules, result);
    result.text = utf8(normalized);
    return result;
}

} // namespace ztermy::ai
