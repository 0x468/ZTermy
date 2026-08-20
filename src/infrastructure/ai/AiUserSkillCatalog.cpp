#include "infrastructure/ai/AiUserSkillCatalog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringDecoder>
#include <QStringList>

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

namespace ztermy::ai
{
namespace
{

constexpr auto skillFileName = "SKILL.md";
constexpr auto readmeFileName = "README.txt";

constexpr auto readmeContents = std::to_array(
    "ztermy user skills\n"
    "\n"
    "Add one directory per skill. Each directory must contain a SKILL.md file that follows ztermy's portable AI "
    "skill format.\n"
    "\n"
    "Example:\n"
    "  Skills/\n"
    "    service-diagnostics/\n"
    "      SKILL.md\n"
    "\n"
    "Minimal SKILL.md:\n"
    "  ---\n"
    "  name: service-diagnostics\n"
    "  description: Diagnose service failures and identify actionable recovery steps.\n"
    "  ---\n"
    "\n"
    "  # Service diagnostics\n"
    "\n"
    "  Follow the documented service checks and summarize the likely cause.\n"
    "\n"
    "Reload User skills in ztermy after adding or editing a skill.\n");

struct Frontmatter final
{
    QString name;
    QString description;
    QString compatibility;
    QString instructions;
};

[[nodiscard]] QString unquote(QString value)
{
    value = value.trimmed();
    if (value.size() < 2)
    {
        return value;
    }
    if (value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\''))
    {
        value = value.mid(1, value.size() - 2);
        return value.replace(QStringLiteral("''"), QStringLiteral("'"));
    }
    if (value.front() != QLatin1Char('"') || value.back() != QLatin1Char('"'))
    {
        return value;
    }

    QString result;
    result.reserve(value.size() - 2);
    bool escaped = false;
    for (qsizetype index = 1; index + 1 < value.size(); ++index)
    {
        const QChar character = value.at(index);
        if (!escaped && character == QLatin1Char('\\'))
        {
            escaped = true;
            continue;
        }
        if (escaped)
        {
            switch (character.unicode())
            {
                case 'n':
                    result.append(QLatin1Char('\n'));
                    break;
                case 'r':
                    result.append(QLatin1Char('\r'));
                    break;
                case 't':
                    result.append(QLatin1Char('\t'));
                    break;
                default:
                    result.append(character);
                    break;
            }
            escaped = false;
        }
        else
        {
            result.append(character);
        }
    }
    if (escaped)
    {
        result.append(QLatin1Char('\\'));
    }
    return result;
}

[[nodiscard]] QString blockScalar(const QStringList &lines, qsizetype &index, const QChar style)
{
    QStringList values;
    while (index + 1 < lines.size())
    {
        const QString &next = lines.at(index + 1);
        if (!next.isEmpty() && !next.front().isSpace())
        {
            break;
        }
        ++index;
        values.push_back(next.trimmed());
    }
    return style == QLatin1Char('>') ? values.join(QLatin1Char(' ')).trimmed()
                                     : values.join(QLatin1Char('\n')).trimmed();
}

[[nodiscard]] std::optional<Frontmatter> parseFrontmatter(const QString &contents)
{
    const QString normalized = QString(contents)
                                   .replace(QStringLiteral("\r\n"), QStringLiteral("\n"))
                                   .replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QStringList lines = normalized.split(QLatin1Char('\n'));
    if (lines.empty() || lines.front().trimmed() != QStringLiteral("---"))
    {
        return std::nullopt;
    }

    qsizetype closing = -1;
    for (qsizetype index = 1; index < lines.size(); ++index)
    {
        if (lines.at(index).trimmed() == QStringLiteral("---"))
        {
            closing = index;
            break;
        }
    }
    if (closing < 0)
    {
        return std::nullopt;
    }

    Frontmatter result;
    for (qsizetype index = 1; index < closing; ++index)
    {
        const QString &line = lines.at(index);
        if (line.isEmpty() || line.front().isSpace() || line.trimmed().startsWith(QLatin1Char('#')))
        {
            continue;
        }
        const qsizetype colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
        {
            continue;
        }
        const QString key = line.left(colon).trimmed();
        QString value = line.mid(colon + 1).trimmed();
        if (value == QStringLiteral(">") || value == QStringLiteral("|"))
        {
            value = blockScalar(lines, index, value.front());
        }
        else
        {
            value = unquote(std::move(value));
        }
        if (key == QStringLiteral("name"))
        {
            result.name = value;
        }
        else if (key == QStringLiteral("description"))
        {
            result.description = value;
        }
        else if (key == QStringLiteral("compatibility"))
        {
            result.compatibility = value;
        }
    }
    result.instructions = lines.mid(closing + 1).join(QLatin1Char('\n')).trimmed();
    return result;
}

[[nodiscard]] bool ensureReadme(const QString &rootPath)
{
    const QDir root(rootPath);
    if (!root.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).isEmpty())
    {
        return true;
    }

    QSaveFile file(root.filePath(QString::fromLatin1(readmeFileName)));
    constexpr auto readmeSize = static_cast<qint64>(readmeContents.size() - 1);
    return file.open(QIODevice::WriteOnly) && file.write(readmeContents.data(), readmeSize) == readmeSize
           && file.commit();
}

[[nodiscard]] AiUserSkill warningSkill(const QString &directoryName, const AiUserSkillWarning warning)
{
    return AiUserSkill{.id = directoryName.toUtf8().toStdString(),
                       .name = directoryName.toUtf8().toStdString(),
                       .warnings = {warning},
                       .ready = false};
}

} // namespace

AiUserSkillCatalog::AiUserSkillCatalog(const QString &rootPath, const AiUserSkillLimits limits)
    : m_rootPath(QDir::cleanPath(rootPath)), m_limits(limits)
{
}

const QString &AiUserSkillCatalog::rootPath() const noexcept
{
    return m_rootPath;
}

std::expected<AiUserSkillScanResult, AiUserSkillCatalogError> AiUserSkillCatalog::scan() const
{
    if (m_rootPath.isEmpty() || !QDir().mkpath(m_rootPath))
    {
        return std::unexpected(AiUserSkillCatalogError::directoryUnavailable);
    }
    if (!ensureReadme(m_rootPath))
    {
        return std::unexpected(AiUserSkillCatalogError::directoryUnavailable);
    }

    QDir root(m_rootPath);
    const QFileInfoList entries = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                                                     QDir::Name | QDir::IgnoreCase);
    AiUserSkillScanResult result;
    result.skills.reserve(std::min(static_cast<std::size_t>(entries.size()), m_limits.maximumSkills));

    for (const QFileInfo &directory : entries)
    {
        if (result.skills.size() >= m_limits.maximumSkills)
        {
            result.skills.push_back(
                warningSkill(QStringLiteral("catalogue-limit"), AiUserSkillWarning::catalogueLimit));
            break;
        }
        const QString directoryName = directory.fileName();
        if (directory.isSymLink())
        {
            result.skills.push_back(warningSkill(directoryName, AiUserSkillWarning::symbolicLink));
            continue;
        }

        const QFileInfo skillFile(directory.filePath() + QLatin1Char('/') + QString::fromLatin1(skillFileName));
        if (!skillFile.exists())
        {
            result.skills.push_back(warningSkill(directoryName, AiUserSkillWarning::missingSkillFile));
            continue;
        }
        if (skillFile.isSymLink())
        {
            result.skills.push_back(warningSkill(directoryName, AiUserSkillWarning::symbolicLink));
            continue;
        }
        if (!skillFile.isFile() || !skillFile.isReadable())
        {
            result.skills.push_back(warningSkill(directoryName, AiUserSkillWarning::unreadableSkillFile));
            continue;
        }
        if (skillFile.size() < 0 || std::cmp_greater(skillFile.size(), m_limits.maximumSkillBytes))
        {
            result.skills.push_back(warningSkill(directoryName, AiUserSkillWarning::skillFileTooLarge));
            continue;
        }

        QFile file(skillFile.filePath());
        if (!file.open(QIODevice::ReadOnly))
        {
            result.skills.push_back(warningSkill(directoryName, AiUserSkillWarning::unreadableSkillFile));
            continue;
        }
        const QByteArray bytes = file.readAll();
        QStringDecoder decoder(QStringDecoder::Utf8);
        const QString contents = decoder.decode(bytes);
        if (decoder.hasError())
        {
            result.skills.push_back(warningSkill(directoryName, AiUserSkillWarning::invalidUtf8));
            continue;
        }

        const auto parsed = parseFrontmatter(contents);
        if (!parsed.has_value())
        {
            result.skills.push_back(warningSkill(directoryName, AiUserSkillWarning::missingFrontmatter));
            continue;
        }

        AiUserSkill skill{.id = directoryName.toUtf8().toStdString(),
                          .name = parsed->name.toUtf8().toStdString(),
                          .description = parsed->description.toUtf8().toStdString(),
                          .instructions = parsed->instructions.toUtf8().toStdString()};
        if (parsed->name.isEmpty())
        {
            skill.warnings.push_back(AiUserSkillWarning::missingName);
        }
        else if (!validAiUserSkillId(skill.name))
        {
            skill.warnings.push_back(AiUserSkillWarning::invalidName);
        }
        if (parsed->name != directoryName)
        {
            skill.warnings.push_back(AiUserSkillWarning::nameDirectoryMismatch);
        }
        if (parsed->description.isEmpty())
        {
            skill.warnings.push_back(AiUserSkillWarning::missingDescription);
        }
        else if (std::cmp_greater(parsed->description.size(), m_limits.maximumDescriptionCharacters))
        {
            skill.warnings.push_back(AiUserSkillWarning::descriptionTooLong);
        }
        if (!parsed->compatibility.isEmpty()
            && std::cmp_greater(parsed->compatibility.size(), m_limits.maximumCompatibilityCharacters))
        {
            skill.warnings.push_back(AiUserSkillWarning::compatibilityTooLong);
        }
        if (parsed->instructions.isEmpty())
        {
            skill.warnings.push_back(AiUserSkillWarning::emptyInstructions);
        }
        if (containsDisallowedAiUserSkillControl(skill.name) || containsDisallowedAiUserSkillControl(skill.description)
            || containsDisallowedAiUserSkillControl(skill.instructions))
        {
            skill.warnings.push_back(AiUserSkillWarning::disallowedControl);
        }
        skill.ready = skill.warnings.empty();
        result.skills.push_back(std::move(skill));
    }

    result.readyCount = static_cast<std::size_t>(std::ranges::count(result.skills, true, &AiUserSkill::ready));
    result.warningCount = result.skills.size() - result.readyCount;
    return result;
}

} // namespace ztermy::ai
