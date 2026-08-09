#include "infrastructure/workbench/ScriptStore.h"

#include "infrastructure/workbench/QuickCommandStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_set>
#include <utility>

namespace
{

constexpr qint64 currentSchemaVersion = 2;
constexpr qint64 maximumFileSize = qint64{16} * 1024 * 1024;

[[nodiscard]] std::optional<ztermy::workbench::ShellScope> parseShellScope(const QString &value)
{
    if (value == QStringLiteral("any"))
    {
        return ztermy::workbench::ShellScope::any;
    }
    if (value == QStringLiteral("posix"))
    {
        return ztermy::workbench::ShellScope::posix;
    }
    if (value == QStringLiteral("powershell"))
    {
        return ztermy::workbench::ShellScope::powershell;
    }
    return std::nullopt;
}

[[nodiscard]] QString shellScopeToken(const ztermy::workbench::ShellScope shellScope)
{
    switch (shellScope)
    {
        case ztermy::workbench::ShellScope::posix:
            return QStringLiteral("posix");
        case ztermy::workbench::ShellScope::powershell:
            return QStringLiteral("powershell");
        case ztermy::workbench::ShellScope::any:
        default:
            return QStringLiteral("any");
    }
}

[[nodiscard]] std::optional<ztermy::workbench::ScriptVariableType> parseVariableType(const QString &value)
{
    if (value == QStringLiteral("text"))
    {
        return ztermy::workbench::ScriptVariableType::text;
    }
    if (value == QStringLiteral("integer"))
    {
        return ztermy::workbench::ScriptVariableType::integer;
    }
    if (value == QStringLiteral("boolean"))
    {
        return ztermy::workbench::ScriptVariableType::boolean;
    }
    if (value == QStringLiteral("choice"))
    {
        return ztermy::workbench::ScriptVariableType::choice;
    }
    return std::nullopt;
}

[[nodiscard]] QString variableTypeToken(const ztermy::workbench::ScriptVariableType type)
{
    switch (type)
    {
        case ztermy::workbench::ScriptVariableType::integer:
            return QStringLiteral("integer");
        case ztermy::workbench::ScriptVariableType::boolean:
            return QStringLiteral("boolean");
        case ztermy::workbench::ScriptVariableType::choice:
            return QStringLiteral("choice");
        case ztermy::workbench::ScriptVariableType::text:
        default:
            return QStringLiteral("text");
    }
}

[[nodiscard]] std::optional<ztermy::workbench::ScriptContinuation> parseContinuation(const QString &value)
{
    if (value == QStringLiteral("immediate"))
    {
        return ztermy::workbench::ScriptContinuation::immediate;
    }
    if (value == QStringLiteral("literal-output"))
    {
        return ztermy::workbench::ScriptContinuation::literalOutput;
    }
    return std::nullopt;
}

[[nodiscard]] QString continuationToken(const ztermy::workbench::ScriptContinuation continuation)
{
    return continuation == ztermy::workbench::ScriptContinuation::literalOutput ? QStringLiteral("literal-output")
                                                                                : QStringLiteral("immediate");
}

[[nodiscard]] std::optional<std::int64_t> parseTimestamp(const QJsonValue &value)
{
    if (!value.isDouble())
    {
        return std::nullopt;
    }
    const double number = value.toDouble(-1.0);
    if (!std::isfinite(number) || number < 0.0 || std::floor(number) != number
        || number > static_cast<double>(std::numeric_limits<std::int64_t>::max()))
    {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(number);
}

[[nodiscard]] std::optional<std::uint32_t> parseUnsigned(const QJsonValue &value)
{
    const auto parsed = parseTimestamp(value);
    if (!parsed || *parsed > std::numeric_limits<std::uint32_t>::max())
    {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*parsed);
}

[[nodiscard]] std::optional<ztermy::workbench::ScriptVariable> parseVariable(const QJsonValue &value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const QJsonValue name = object.value(QStringLiteral("name"));
    const QJsonValue label = object.value(QStringLiteral("label"));
    const QJsonValue typeValue = object.value(QStringLiteral("type"));
    const QJsonValue defaultValue = object.value(QStringLiteral("defaultValue"));
    const QJsonValue required = object.value(QStringLiteral("required"));
    const QJsonValue choicesValue = object.value(QStringLiteral("choices"));
    const auto type = typeValue.isString() ? parseVariableType(typeValue.toString()) : std::nullopt;
    if (!name.isString() || !label.isString() || !type || !defaultValue.isString() || !required.isBool()
        || !choicesValue.isArray() || choicesValue.toArray().size() > 32)
    {
        return std::nullopt;
    }
    std::vector<std::string> choices;
    choices.reserve(static_cast<std::size_t>(choicesValue.toArray().size()));
    for (const QJsonValue &choice : choicesValue.toArray())
    {
        if (!choice.isString())
        {
            return std::nullopt;
        }
        choices.push_back(choice.toString().toStdString());
    }
    return ztermy::workbench::ScriptVariable{.name = name.toString().toStdString(),
                                             .label = label.toString().toStdString(),
                                             .type = *type,
                                             .defaultValue = defaultValue.toString().toStdString(),
                                             .choices = std::move(choices),
                                             .required = required.toBool()};
}

[[nodiscard]] QJsonObject serializeVariable(const ztermy::workbench::ScriptVariable &variable)
{
    QJsonArray choices;
    for (const std::string &choice : variable.choices)
    {
        choices.append(QString::fromStdString(choice));
    }
    return {{QStringLiteral("name"), QString::fromStdString(variable.name)},
            {QStringLiteral("label"), QString::fromStdString(variable.label)},
            {QStringLiteral("type"), variableTypeToken(variable.type)},
            {QStringLiteral("defaultValue"), QString::fromStdString(variable.defaultValue)},
            {QStringLiteral("choices"), choices},
            {QStringLiteral("required"), variable.required}};
}

[[nodiscard]] std::optional<ztermy::workbench::ScriptStep> parseStep(const QJsonValue &value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const QJsonValue command = object.value(QStringLiteral("command"));
    const QJsonValue continuationValue = object.value(QStringLiteral("continuation"));
    const QJsonValue outputMarker = object.value(QStringLiteral("outputMarker"));
    const auto continuation =
        continuationValue.isString() ? parseContinuation(continuationValue.toString()) : std::nullopt;
    const auto timeoutMs = parseUnsigned(object.value(QStringLiteral("timeoutMs")));
    if (!command.isString() || !continuation || !outputMarker.isString() || !timeoutMs)
    {
        return std::nullopt;
    }
    return ztermy::workbench::ScriptStep{.command = command.toString().toStdString(),
                                         .continuation = *continuation,
                                         .outputMarker = outputMarker.toString().toStdString(),
                                         .timeoutMs = *timeoutMs};
}

[[nodiscard]] QJsonObject serializeStep(const ztermy::workbench::ScriptStep &step)
{
    return {{QStringLiteral("command"), QString::fromStdString(step.command)},
            {QStringLiteral("continuation"), continuationToken(step.continuation)},
            {QStringLiteral("outputMarker"), QString::fromStdString(step.outputMarker)},
            {QStringLiteral("timeoutMs"), static_cast<qint64>(step.timeoutMs)}};
}

[[nodiscard]] std::optional<ztermy::workbench::ScriptDefinition> parseScript(const QJsonValue &value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const QJsonValue id = object.value(QStringLiteral("id"));
    const QJsonValue name = object.value(QStringLiteral("name"));
    const QJsonValue description = object.value(QStringLiteral("description"));
    const QJsonValue shellValue = object.value(QStringLiteral("shell"));
    const QJsonValue variableValues = object.value(QStringLiteral("variables"));
    const QJsonValue stepValues = object.value(QStringLiteral("steps"));
    const auto shell = shellValue.isString() ? parseShellScope(shellValue.toString()) : std::nullopt;
    const auto createdUtcMs = parseTimestamp(object.value(QStringLiteral("createdUtcMs")));
    const auto modifiedUtcMs = parseTimestamp(object.value(QStringLiteral("modifiedUtcMs")));
    if (!id.isString() || !name.isString() || !description.isString() || !shell || !variableValues.isArray()
        || variableValues.toArray().size() > static_cast<qsizetype>(ztermy::workbench::maximumScriptVariableCount)
        || !stepValues.isArray()
        || stepValues.toArray().size() > static_cast<qsizetype>(ztermy::workbench::maximumScriptStepCount)
        || !createdUtcMs || !modifiedUtcMs)
    {
        return std::nullopt;
    }
    ztermy::workbench::ScriptDefinition script{.id = id.toString().toStdString(),
                                               .name = name.toString().toStdString(),
                                               .description = description.toString().toStdString(),
                                               .shellScope = *shell,
                                               .createdUtcMs = *createdUtcMs,
                                               .modifiedUtcMs = *modifiedUtcMs};
    script.variables.reserve(static_cast<std::size_t>(variableValues.toArray().size()));
    for (const QJsonValue &variableValue : variableValues.toArray())
    {
        auto variable = parseVariable(variableValue);
        if (!variable)
        {
            return std::nullopt;
        }
        script.variables.push_back(std::move(*variable));
    }
    script.steps.reserve(static_cast<std::size_t>(stepValues.toArray().size()));
    for (const QJsonValue &stepValue : stepValues.toArray())
    {
        auto step = parseStep(stepValue);
        if (!step)
        {
            return std::nullopt;
        }
        script.steps.push_back(std::move(*step));
    }
    return ztermy::workbench::validScriptDefinition(script) ? std::optional{std::move(script)} : std::nullopt;
}

[[nodiscard]] QJsonObject serializeScript(const ztermy::workbench::ScriptDefinition &script)
{
    QJsonArray variables;
    for (const ztermy::workbench::ScriptVariable &variable : script.variables)
    {
        variables.append(serializeVariable(variable));
    }
    QJsonArray steps;
    for (const ztermy::workbench::ScriptStep &step : script.steps)
    {
        steps.append(serializeStep(step));
    }
    return {{QStringLiteral("id"), QString::fromStdString(script.id)},
            {QStringLiteral("name"), QString::fromStdString(script.name)},
            {QStringLiteral("description"), QString::fromStdString(script.description)},
            {QStringLiteral("shell"), shellScopeToken(script.shellScope)},
            {QStringLiteral("variables"), variables},
            {QStringLiteral("steps"), steps},
            {QStringLiteral("createdUtcMs"), script.createdUtcMs},
            {QStringLiteral("modifiedUtcMs"), script.modifiedUtcMs}};
}

[[nodiscard]] bool containsDuplicateIds(const std::span<const ztermy::workbench::ScriptDefinition> scripts)
{
    std::unordered_set<std::string_view> ids;
    ids.reserve(scripts.size());
    return std::ranges::any_of(scripts, [&ids](const ztermy::workbench::ScriptDefinition &script) {
        return !ids.insert(script.id).second;
    });
}

[[nodiscard]] ztermy::workbench::ScriptStoreError
scriptStoreError(const ztermy::workbench::QuickCommandStoreError error)
{
    switch (error)
    {
        case ztermy::workbench::QuickCommandStoreError::invalidPath:
            return ztermy::workbench::ScriptStoreError::invalidPath;
        case ztermy::workbench::QuickCommandStoreError::ioError:
            return ztermy::workbench::ScriptStoreError::ioError;
        case ztermy::workbench::QuickCommandStoreError::unsupportedVersion:
            return ztermy::workbench::ScriptStoreError::unsupportedVersion;
        case ztermy::workbench::QuickCommandStoreError::invalidFormat:
        default:
            return ztermy::workbench::ScriptStoreError::invalidFormat;
    }
}

} // namespace

namespace ztermy::workbench
{

ScriptStore::ScriptStore(QString filePath) : m_filePath(std::move(filePath)) {}

const QString &ScriptStore::filePath() const noexcept
{
    return m_filePath;
}

std::expected<std::vector<ScriptDefinition>, ScriptStoreError> ScriptStore::load() const
{
    if (m_filePath.isEmpty())
    {
        return std::unexpected(ScriptStoreError::invalidPath);
    }
    QFile file(m_filePath);
    if (!file.exists())
    {
        return std::vector<ScriptDefinition>{};
    }
    if (!file.open(QIODevice::ReadOnly) || file.size() < 0 || file.size() > maximumFileSize)
    {
        return std::unexpected(file.size() > maximumFileSize ? ScriptStoreError::invalidFormat
                                                             : ScriptStoreError::ioError);
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::unexpected(ScriptStoreError::invalidFormat);
    }
    const QJsonObject root = document.object();
    const QJsonValue version = root.value(QStringLiteral("version"));
    if (!version.isDouble() || version.toInteger(-1) != currentSchemaVersion
        || version.toDouble() != static_cast<double>(currentSchemaVersion))
    {
        return std::unexpected(version.isDouble() ? ScriptStoreError::unsupportedVersion
                                                  : ScriptStoreError::invalidFormat);
    }
    const QJsonValue values = root.value(QStringLiteral("scripts"));
    if (!values.isArray() || values.toArray().size() > static_cast<qsizetype>(maximumScriptCount))
    {
        return std::unexpected(ScriptStoreError::invalidFormat);
    }
    std::vector<ScriptDefinition> scripts;
    scripts.reserve(static_cast<std::size_t>(values.toArray().size()));
    for (const QJsonValue &value : values.toArray())
    {
        auto script = parseScript(value);
        if (!script)
        {
            return std::unexpected(ScriptStoreError::invalidFormat);
        }
        scripts.push_back(std::move(*script));
    }
    if (containsDuplicateIds(scripts))
    {
        return std::unexpected(ScriptStoreError::invalidFormat);
    }
    return scripts;
}

std::expected<std::vector<ScriptDefinition>, ScriptStoreError>
ScriptStore::loadOrMigrate(const QString &legacyQuickCommandPath) const
{
    if (QFileInfo::exists(m_filePath) || legacyQuickCommandPath.isEmpty() || !QFileInfo::exists(legacyQuickCommandPath))
    {
        return load();
    }
    const QuickCommandStore legacyStore(legacyQuickCommandPath);
    const auto legacy = legacyStore.load();
    if (!legacy)
    {
        return std::unexpected(scriptStoreError(legacy.error()));
    }
    std::vector<ScriptDefinition> scripts;
    scripts.reserve(legacy->size());
    std::ranges::transform(*legacy, std::back_inserter(scripts), scriptFromQuickCommand);
    if (const auto saved = save(scripts); !saved)
    {
        return std::unexpected(saved.error());
    }
    return scripts;
}

std::expected<void, ScriptStoreError> ScriptStore::save(const std::span<const ScriptDefinition> scripts) const
{
    if (m_filePath.isEmpty())
    {
        return std::unexpected(ScriptStoreError::invalidPath);
    }
    if (scripts.size() > maximumScriptCount || containsDuplicateIds(scripts)
        || std::ranges::any_of(scripts, [](const ScriptDefinition &script) {
               return !validScriptDefinition(script);
           }))
    {
        return std::unexpected(ScriptStoreError::invalidFormat);
    }
    QJsonArray values;
    for (const ScriptDefinition &script : scripts)
    {
        values.append(serializeScript(script));
    }
    const QByteArray data = QJsonDocument(QJsonObject{{QStringLiteral("version"), currentSchemaVersion},
                                                      {QStringLiteral("scripts"), values}})
                                .toJson(QJsonDocument::Indented);
    if (data.size() > maximumFileSize)
    {
        return std::unexpected(ScriptStoreError::invalidFormat);
    }
    const QFileInfo fileInfo(m_filePath);
    if (!fileInfo.absoluteDir().mkpath(QStringLiteral(".")))
    {
        return std::unexpected(ScriptStoreError::ioError);
    }
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
    {
        file.cancelWriting();
        return std::unexpected(ScriptStoreError::ioError);
    }
    return {};
}

} // namespace ztermy::workbench
