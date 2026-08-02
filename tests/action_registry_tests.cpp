#include "application/actions/ActionRegistry.h"

#include <QKeySequence>
#include <QTest>

namespace
{

[[nodiscard]] QVariantMap actionById(const QVariantList &actions, const QString &id)
{
    for (const QVariant &entry : actions)
    {
        const QVariantMap action = entry.toMap();
        if (action.value(QStringLiteral("id")).toString() == id)
        {
            return action;
        }
    }
    return {};
}

} // namespace

class ActionRegistryTests final : public QObject
{
    Q_OBJECT

private slots:
    void exposesStableMetadataAndContext();
    void normalizesOverridesAndSupportsUnbind();
    void rejectsConflictsAndTerminalTextKeys();
    void ignoresUnknownAndInvalidPersistedOverrides();
    void restoresDefaults();
};

void ActionRegistryTests::exposesStableMetadataAndContext()
{
    const ztermy::actions::ActionRegistry registry;
    const QVariantMap palette = actionById(registry.actions(false), QStringLiteral("application.commandPalette"));
    QCOMPARE(palette.value(QStringLiteral("shortcut")).toString(), QStringLiteral("Ctrl+Shift+P"));
    QVERIFY(palette.value(QStringLiteral("enabled")).toBool());
    QVERIFY(palette.value(QStringLiteral("paletteVisible")).toBool());

    const QVariantMap terminalFind = actionById(registry.actions(false), QStringLiteral("terminal.find"));
    QVERIFY(!terminalFind.value(QStringLiteral("enabled")).toBool());
    QVERIFY(
        actionById(registry.actions(true), QStringLiteral("terminal.find")).value(QStringLiteral("enabled")).toBool());

    const QVariantMap sftp = actionById(registry.actions(true), QStringLiteral("terminal.sftp"));
    QCOMPARE(sftp.value(QStringLiteral("category")).toString(), QStringLiteral("terminal"));
    QVERIFY(sftp.value(QStringLiteral("paletteVisible")).toBool());
    QVERIFY(sftp.value(QStringLiteral("shortcut")).toString().isEmpty());
}

void ActionRegistryTests::normalizesOverridesAndSupportsUnbind()
{
    ztermy::actions::ActionRegistry registry;
    const auto changed = registry.setShortcut(QStringLiteral("terminal.find"), QStringLiteral(" ctrl + alt + f "));
    QVERIFY(changed.valid());
    QCOMPARE(changed.normalized, QStringLiteral("Ctrl+Alt+F"));
    QCOMPARE(registry.effectiveShortcut(QStringLiteral("terminal.find")), QStringLiteral("Ctrl+Alt+F"));
    QVERIFY(registry.overrides().contains(QStringLiteral("terminal.find")));

    const auto unbound = registry.setShortcut(QStringLiteral("terminal.find"), QString{});
    QVERIFY(unbound.valid());
    QVERIFY(registry.effectiveShortcut(QStringLiteral("terminal.find")).isEmpty());
    QVERIFY(registry.overrides().contains(QStringLiteral("terminal.find")));
}

void ActionRegistryTests::rejectsConflictsAndTerminalTextKeys()
{
    const ztermy::actions::ActionRegistry registry;
    const auto conflict = registry.validateShortcut(QStringLiteral("terminal.find"), QStringLiteral("Ctrl+Shift+P"));
    QCOMPARE(conflict.error, ztermy::actions::ShortcutValidationError::Conflict);
    QCOMPARE(conflict.conflictingActionId, QStringLiteral("application.commandPalette"));

    const auto printable = registry.validateShortcut(QStringLiteral("terminal.find"), QStringLiteral("F"));
    QCOMPARE(printable.error, ztermy::actions::ShortcutValidationError::UnmodifiedPrintable);

    const auto functionKey = registry.validateShortcut(QStringLiteral("terminal.find"), QStringLiteral("F8"));
    QVERIFY(functionKey.valid());
}

void ActionRegistryTests::ignoresUnknownAndInvalidPersistedOverrides()
{
    ztermy::actions::ActionRegistry registry;
    registry.setOverrides({
        {QStringLiteral("future.action"), QStringLiteral("Ctrl+Alt+9")},
        {QStringLiteral("terminal.find"), QStringLiteral("plain text that is not a shortcut")},
        {QStringLiteral("terminal.moveWorkbench"), QStringLiteral("Ctrl+Alt+M")},
    });
    QCOMPARE(registry.overrides().size(), 1);
    QCOMPARE(registry.effectiveShortcut(QStringLiteral("terminal.moveWorkbench")), QStringLiteral("Ctrl+Alt+M"));

    registry.setOverrides({
        {QStringLiteral("terminal.find"), QStringLiteral("Ctrl+Shift+P")},
    });
    QVERIFY(registry.overrides().isEmpty());
    QCOMPARE(registry.effectiveShortcut(QStringLiteral("terminal.find")), QStringLiteral("Ctrl+Shift+F"));

    registry.setOverrides({
        {QStringLiteral("application.commandPalette"), QString{}},
        {QStringLiteral("application.hosts"), QStringLiteral("Ctrl+Shift+P")},
    });
    QVERIFY(registry.effectiveShortcut(QStringLiteral("application.commandPalette")).isEmpty());
    QCOMPARE(registry.effectiveShortcut(QStringLiteral("application.hosts")), QStringLiteral("Ctrl+Shift+P"));
}

void ActionRegistryTests::restoresDefaults()
{
    ztermy::actions::ActionRegistry registry;
    QVERIFY(registry.setShortcut(QStringLiteral("terminal.find"), QStringLiteral("Ctrl+Alt+F")).valid());
    QVERIFY(registry.resetShortcut(QStringLiteral("terminal.find")));
    QCOMPARE(registry.effectiveShortcut(QStringLiteral("terminal.find")), QStringLiteral("Ctrl+Shift+F"));
    QVERIFY(!registry.resetShortcut(QStringLiteral("terminal.find")));

    QVERIFY(registry.setShortcut(QStringLiteral("terminal.find"), QString{}).valid());
    QVERIFY(registry.resetAllShortcuts());
    QCOMPARE(registry.effectiveShortcut(QStringLiteral("terminal.find")), QStringLiteral("Ctrl+Shift+F"));
}

QTEST_MAIN(ActionRegistryTests)

#include "action_registry_tests.moc"
