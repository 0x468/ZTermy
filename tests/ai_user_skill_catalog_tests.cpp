#include "infrastructure/ai/AiUserSkillCatalog.h"

#include <QtTest/QTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

namespace
{
using namespace ztermy::ai;

class AiUserSkillCatalogTests final : public QObject
{
    Q_OBJECT

private slots:
    void validatesPortableSkillDirectories();
    void reportsMalformedSkillsWithoutDiscardingReadySkills();
    void enforcesBoundedUtf8Files();
};

void writeSkill(const QString &root, const QString &directoryName, const QByteArray &contents)
{
    const QString directory = QDir(root).filePath(directoryName);
    QVERIFY(QDir().mkpath(directory));
    QFile file(QDir(directory).filePath(QStringLiteral("SKILL.md")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(contents), contents.size());
}

void AiUserSkillCatalogTests::validatesPortableSkillDirectories()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString skills = directory.filePath(QStringLiteral("Skills"));
    writeSkill(skills, QStringLiteral("service-diagnostics"),
               QByteArrayLiteral("---\nname: service-diagnostics\ndescription: >\n  Diagnose failed services and\n  "
                                 "summarize actionable recovery steps.\ncompatibility: ztermy\n---\n\n# "
                                 "Workflow\n\nInspect the service status before proposing a fix.\n"));

    const auto scanned = AiUserSkillCatalog(skills).scan();
    QVERIFY(scanned.has_value());
    QCOMPARE(scanned->readyCount, std::size_t{1});
    QCOMPARE(scanned->warningCount, std::size_t{0});
    QCOMPARE(scanned->skills.size(), std::size_t{1});
    const AiUserSkill &skill = scanned->skills.front();
    QVERIFY(skill.ready);
    QCOMPARE(skill.id, std::string("service-diagnostics"));
    QCOMPARE(skill.description, std::string("Diagnose failed services and summarize actionable recovery steps."));
    QVERIFY(skill.instructions.starts_with("# Workflow"));
}

void AiUserSkillCatalogTests::reportsMalformedSkillsWithoutDiscardingReadySkills()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString skills = directory.filePath(QStringLiteral("Skills"));
    writeSkill(
        skills, QStringLiteral("ready-skill"),
        QByteArrayLiteral("---\nname: ready-skill\ndescription: A valid skill.\n---\nUse the valid workflow.\n"));
    writeSkill(skills, QStringLiteral("Wrong Name"),
               QByteArrayLiteral("---\nname: another-name\ndescription: Invalid directory identity.\n---\nDo work.\n"));
    QVERIFY(QDir().mkpath(QDir(skills).filePath(QStringLiteral("missing-file"))));

    const auto scanned = AiUserSkillCatalog(skills).scan();
    QVERIFY(scanned.has_value());
    QCOMPARE(scanned->readyCount, std::size_t{1});
    QCOMPARE(scanned->warningCount, std::size_t{2});
    QCOMPARE(scanned->skills.size(), std::size_t{3});
    const auto mismatch = std::ranges::find(scanned->skills, std::string("Wrong Name"), &AiUserSkill::id);
    QVERIFY(mismatch != scanned->skills.end());
    QVERIFY(!mismatch->ready);
    QVERIFY(std::ranges::find(mismatch->warnings, AiUserSkillWarning::nameDirectoryMismatch)
            != mismatch->warnings.end());
}

void AiUserSkillCatalogTests::enforcesBoundedUtf8Files()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString skills = directory.filePath(QStringLiteral("Skills"));
    writeSkill(skills, QStringLiteral("invalid-utf8"), QByteArray::fromHex("fffefd"));
    writeSkill(skills, QStringLiteral("too-large"), QByteArray(257, 'x'));

    AiUserSkillLimits limits;
    limits.maximumSkillBytes = 256;
    const auto scanned = AiUserSkillCatalog(skills, limits).scan();
    QVERIFY(scanned.has_value());
    QCOMPARE(scanned->readyCount, std::size_t{0});
    QCOMPARE(scanned->warningCount, std::size_t{2});
    const auto invalid = std::ranges::find(scanned->skills, std::string("invalid-utf8"), &AiUserSkill::id);
    const auto large = std::ranges::find(scanned->skills, std::string("too-large"), &AiUserSkill::id);
    QVERIFY(invalid != scanned->skills.end());
    QVERIFY(large != scanned->skills.end());
    QCOMPARE(invalid->warnings.front(), AiUserSkillWarning::invalidUtf8);
    QCOMPARE(large->warnings.front(), AiUserSkillWarning::skillFileTooLarge);
}

} // namespace

QTEST_GUILESS_MAIN(AiUserSkillCatalogTests)

#include "ai_user_skill_catalog_tests.moc"
