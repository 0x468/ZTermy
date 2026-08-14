#include "application/ai/AiUserSkillTool.h"

#include <QtTest/QTest>

#include <QJsonDocument>
#include <QJsonObject>

namespace
{
using namespace ztermy::ai;

class AiUserSkillToolTests final : public QObject
{
    Q_OBJECT

private slots:
    void advertisesAndPagesReadySkills();
    void loadsExactSkillInstructions();
    void buildsExplicitSelectionInstructions();
};

[[nodiscard]] std::vector<AiUserSkill> skills()
{
    return {AiUserSkill{.id = "service-diagnostics",
                        .name = "service-diagnostics",
                        .description = "Diagnose service failures.",
                        .instructions = "Inspect status and logs before proposing a recovery command.",
                        .ready = true},
            AiUserSkill{.id = "release-check",
                        .name = "release-check",
                        .description = "Validate a release.",
                        .instructions = "Run the release verification checklist.",
                        .ready = true},
            AiUserSkill{.id = "broken", .name = "broken", .ready = false}};
}

[[nodiscard]] QJsonObject parsed(const std::string &value)
{
    return QJsonDocument::fromJson(QByteArray(value.data(), static_cast<qsizetype>(value.size()))).object();
}

void AiUserSkillToolTests::advertisesAndPagesReadySkills()
{
    const auto values = skills();
    const auto definitions = AiUserSkillTool::definitions(values);
    QCOMPARE(definitions.size(), std::size_t{2});
    QCOMPARE(definitions.front().name, std::string("list_skills"));
    QVERIFY(definitions.back().description.find("service-diagnostics") != std::string::npos);

    const QJsonObject response = parsed(AiUserSkillTool::execute("list_skills", R"({"offset":0,"limit":1})", values));
    QVERIFY(response.value(QStringLiteral("ok")).toBool());
    QCOMPARE(response.value(QStringLiteral("total")).toInteger(), qint64{2});
    QCOMPARE(response.value(QStringLiteral("next_offset")).toInteger(), qint64{1});
    QVERIFY(response.value(QStringLiteral("has_more")).toBool());
}

void AiUserSkillToolTests::loadsExactSkillInstructions()
{
    const auto values = skills();
    const QJsonObject response = parsed(
        AiUserSkillTool::execute("load_skill", R"({"name":"service-diagnostics"})", values));
    QVERIFY(response.value(QStringLiteral("ok")).toBool());
    const QJsonObject skill = response.value(QStringLiteral("skill")).toObject();
    QCOMPARE(skill.value(QStringLiteral("id")).toString(), QStringLiteral("service-diagnostics"));
    QVERIFY(skill.value(QStringLiteral("instructions")).toString().contains(QStringLiteral("Inspect status")));
    QVERIFY(skill.value(QStringLiteral("user_managed_instructions")).toBool());

    const QJsonObject missing = parsed(AiUserSkillTool::execute("load_skill", R"({"name":"broken"})", values));
    QVERIFY(!missing.value(QStringLiteral("ok")).toBool());
    QCOMPARE(missing.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
             QStringLiteral("not_found"));
}

void AiUserSkillToolTests::buildsExplicitSelectionInstructions()
{
    const auto values = skills();
    const std::vector selected{std::string("release-check"), std::string("missing"),
                               std::string("service-diagnostics")};
    const std::string instructions = AiUserSkillTool::selectedInstructions(values, selected);
    QVERIFY(instructions.find("User-selected Agent Skills") != std::string::npos);
    QVERIFY(instructions.find("/release-check") != std::string::npos);
    QVERIFY(instructions.find("/service-diagnostics") != std::string::npos);
    QVERIFY(instructions.find("/missing") == std::string::npos);
}

} // namespace

QTEST_GUILESS_MAIN(AiUserSkillToolTests)

#include "ai_user_skill_tool_tests.moc"
