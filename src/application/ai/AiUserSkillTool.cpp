#include "application/ai/AiUserSkillTool.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <ranges>

namespace ztermy::ai
{
namespace
{

constexpr std::size_t maximumArgumentsBytes = std::size_t{16} * 1024;
constexpr int maximumPageSize = 100;
constexpr std::size_t maximumAdvertisedSkills = 24;
constexpr qsizetype maximumAdvertisedDescriptionCharacters = 160;
constexpr std::size_t maximumSelectedSkills = 4;

[[nodiscard]] QString text(const std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] std::string json(const QJsonObject &value)
{
    const QByteArray bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] std::string failure(const QString &code, const QString &message)
{
    return json(QJsonObject{{QStringLiteral("ok"), false},
                            {QStringLiteral("error"),
                             QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}}});
}

[[nodiscard]] bool hasOnlyKeys(const QJsonObject &object, const std::initializer_list<QString> allowed)
{
    return std::ranges::all_of(object.keys(), [&allowed](const QString &key) {
        return std::ranges::find(allowed, key) != allowed.end();
    });
}

[[nodiscard]] const AiUserSkill *readySkill(const std::span<const AiUserSkill> skills, const std::string_view id)
{
    const auto found = std::ranges::find_if(skills, [id](const AiUserSkill &skill) {
        return skill.ready && skill.id == id;
    });
    return found == skills.end() ? nullptr : &*found;
}

[[nodiscard]] std::vector<const AiUserSkill *> readySkills(const std::span<const AiUserSkill> skills)
{
    std::vector<const AiUserSkill *> result;
    result.reserve(skills.size());
    for (const AiUserSkill &skill : skills)
    {
        if (skill.ready)
        {
            result.push_back(&skill);
        }
    }
    return result;
}

[[nodiscard]] QString loadDescription(const std::span<const AiUserSkill> skills)
{
    QString description = QStringLiteral(
        "Load one user-managed Agent Skill by exact id. The result contains user-authored instructions to follow "
        "for the current request. Load a skill only when its description clearly matches the task. Available skills:");
    std::size_t advertised = 0;
    for (const AiUserSkill &skill : skills)
    {
        if (!skill.ready || advertised >= maximumAdvertisedSkills)
        {
            continue;
        }
        QString summary = text(skill.description).simplified();
        if (summary.size() > maximumAdvertisedDescriptionCharacters)
        {
            summary = summary.left(maximumAdvertisedDescriptionCharacters - 1) + QChar(0x2026);
        }
        description += QStringLiteral("\n- %1: %2").arg(text(skill.id), summary);
        ++advertised;
    }
    const std::size_t total = static_cast<std::size_t>(std::ranges::count(skills, true, &AiUserSkill::ready));
    if (total > advertised)
    {
        description += QStringLiteral("\nUse list_skills to inspect the remaining %1 skill(s).")
                           .arg(static_cast<qulonglong>(total - advertised));
    }
    return description;
}

} // namespace

std::vector<AiToolDefinition> AiUserSkillTool::definitions(const std::span<const AiUserSkill> skills)
{
    if (std::ranges::none_of(skills, &AiUserSkill::ready))
    {
        return {};
    }
    return {
        {.name = "list_skills",
         .description = "List bounded metadata for locally installed user Agent Skills. Skill bodies are not "
                        "returned; call load_skill with an exact id when one clearly matches the request.",
         .parametersJson =
             R"({"type":"object","properties":{"offset":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1,"maximum":100}},"required":["offset","limit"],"additionalProperties":false})"},
        {.name = "load_skill",
         .description = loadDescription(skills).toUtf8().toStdString(),
         .parametersJson =
             R"({"type":"object","properties":{"name":{"type":"string","minLength":1,"maxLength":64}},"required":["name"],"additionalProperties":false})"},
    };
}

std::string AiUserSkillTool::execute(const std::string_view toolName, const std::string_view argumentsJson,
                                     const std::span<const AiUserSkill> skills)
{
    if (argumentsJson.size() > maximumArgumentsBytes)
    {
        return failure(QStringLiteral("limit_exceeded"), QStringLiteral("Tool arguments exceed the 16 KiB limit."));
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        QByteArray(argumentsJson.data(), static_cast<qsizetype>(argumentsJson.size())), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return failure(QStringLiteral("invalid_arguments"), QStringLiteral("Tool arguments must be a JSON object."));
    }
    const QJsonObject object = document.object();

    if (toolName == "load_skill")
    {
        if (!hasOnlyKeys(object, {QStringLiteral("name")}) || !object.value(QStringLiteral("name")).isString())
        {
            return failure(QStringLiteral("invalid_arguments"), QStringLiteral("An exact skill name is required."));
        }
        const QByteArray requested = object.value(QStringLiteral("name")).toString().toUtf8();
        const AiUserSkill *skill = readySkill(skills, std::string_view(requested.constData(), requested.size()));
        if (skill == nullptr)
        {
            return failure(QStringLiteral("not_found"), QStringLiteral("The requested skill is not available."));
        }
        return json(QJsonObject{
            {QStringLiteral("ok"), true},
            {QStringLiteral("skill"),
             QJsonObject{{QStringLiteral("id"), text(skill->id)},
                         {QStringLiteral("name"), text(skill->name)},
                         {QStringLiteral("description"), text(skill->description)},
                         {QStringLiteral("instructions"), text(skill->instructions)},
                         {QStringLiteral("user_managed_instructions"), true}}}});
    }

    if (toolName != "list_skills")
    {
        return failure(QStringLiteral("unsupported"), QStringLiteral("The requested skill tool is not supported."));
    }
    if (!hasOnlyKeys(object, {QStringLiteral("offset"), QStringLiteral("limit")}))
    {
        return failure(QStringLiteral("invalid_arguments"), QStringLiteral("Unexpected tool arguments."));
    }
    const QJsonValue offsetValue = object.value(QStringLiteral("offset"));
    const QJsonValue limitValue = object.value(QStringLiteral("limit"));
    if (!offsetValue.isDouble() || !limitValue.isDouble() || offsetValue.toDouble() < 0
        || limitValue.toDouble() < 1 || limitValue.toDouble() > maximumPageSize
        || offsetValue.toDouble() != static_cast<double>(offsetValue.toInteger())
        || limitValue.toDouble() != static_cast<double>(limitValue.toInteger()))
    {
        return failure(QStringLiteral("invalid_arguments"), QStringLiteral("Offset and limit must be bounded integers."));
    }

    const auto available = readySkills(skills);
    const std::size_t offset = static_cast<std::size_t>(offsetValue.toInteger());
    const std::size_t limit = static_cast<std::size_t>(limitValue.toInteger());
    const std::size_t last = std::min(offset + limit, available.size());
    QJsonArray items;
    if (offset < available.size())
    {
        for (std::size_t index = offset; index < last; ++index)
        {
            const AiUserSkill &skill = *available[index];
            items.append(QJsonObject{{QStringLiteral("id"), text(skill.id)},
                                     {QStringLiteral("name"), text(skill.name)},
                                     {QStringLiteral("description"), text(skill.description)}});
        }
    }
    return json(QJsonObject{{QStringLiteral("ok"), true},
                            {QStringLiteral("skills"), items},
                            {QStringLiteral("offset"), static_cast<qint64>(std::min(offset, available.size()))},
                            {QStringLiteral("next_offset"), static_cast<qint64>(last)},
                            {QStringLiteral("total"), static_cast<qint64>(available.size())},
                            {QStringLiteral("has_more"), last < available.size()}});
}

std::string AiUserSkillTool::selectedInstructions(const std::span<const AiUserSkill> skills,
                                                  const std::span<const std::string> selectedIds)
{
    if (selectedIds.empty())
    {
        return {};
    }

    std::string result =
        "\n\n## User-selected Agent Skills\n\nThe user explicitly selected the following locally managed skills for this "
        "request. Follow their instructions when they apply.\n";
    const std::size_t count = std::min(selectedIds.size(), maximumSelectedSkills);
    for (std::size_t index = 0; index < count; ++index)
    {
        const AiUserSkill *skill = readySkill(skills, selectedIds[index]);
        if (skill == nullptr)
        {
            continue;
        }
        result += "\n### /" + skill->id + "\n\n" + skill->instructions + '\n';
    }
    return result;
}

} // namespace ztermy::ai
