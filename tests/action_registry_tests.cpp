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
    void presentationTracksShortcutChangesAndContext();
    void reconnectShortcutIsConfigurableAndPreservesExistingBindings();
};

void ActionRegistryTests::presentationTracksShortcutChangesAndContext()
{
    ztermy::actions::ActionRegistry registry;
    const QString id = QStringLiteral("terminal.copy");
    const auto initial = registry.actions(true);
    const auto initialWithoutTerminal = registry.actions(false);
    QVERIFY(!actionById(initialWithoutTerminal, id).value(QStringLiteral("enabled")).toBool());
    const auto shortcut = [&](bool terminal) {
        return actionById(registry.actions(terminal), id).value(QStringLiteral("shortcut")).toString();
    };
    QVERIFY(registry.setShortcut(id, QString{}).valid());
    QVERIFY(shortcut(true).isEmpty());
    QVERIFY(shortcut(false).isEmpty());
    QVERIFY(!actionById(initial, id).value(QStringLiteral("shortcut")).toString().isEmpty());
    QVERIFY(registry.resetShortcut(id));
    QCOMPARE(shortcut(true), registry.defaultShortcut(id));
    QCOMPARE(shortcut(false), registry.defaultShortcut(id));
    registry.setOverrides({{id, QString{}}});
    QVERIFY(shortcut(true).isEmpty());
    QVERIFY(shortcut(false).isEmpty());
    QVERIFY(registry.resetAllShortcuts());
    QCOMPARE(shortcut(true), registry.defaultShortcut(id));
    QCOMPARE(shortcut(false), registry.defaultShortcut(id));
    QVERIFY(actionById(registry.actions(true), id).value(QStringLiteral("enabled")).toBool());
}

void ActionRegistryTests::exposesStableMetadataAndContext()
{
    const ztermy::actions::ActionRegistry registry;
    const QVariantMap palette = actionById(registry.actions(false), QStringLiteral("application.commandPalette"));
    QCOMPARE(palette.value(QStringLiteral("shortcut")).toString(), QStringLiteral("Ctrl+Shift+P"));
    QVERIFY(palette.value(QStringLiteral("enabled")).toBool());
    QVERIFY(palette.value(QStringLiteral("paletteVisible")).toBool());

    const QVariantMap transfers = actionById(registry.actions(false), QStringLiteral("application.transfers"));
    QVERIFY(transfers.value(QStringLiteral("enabled")).toBool());
    QVERIFY(transfers.value(QStringLiteral("paletteVisible")).toBool());

    const QVariantMap importScripts = actionById(registry.actions(false), QStringLiteral("scripts.import"));
    QCOMPARE(importScripts.value(QStringLiteral("category")).toString(), QStringLiteral("scripts"));
    QVERIFY(importScripts.value(QStringLiteral("enabled")).toBool());

    const QVariantMap terminalFind = actionById(registry.actions(false), QStringLiteral("terminal.find"));
    QVERIFY(!terminalFind.value(QStringLiteral("enabled")).toBool());
    QVERIFY(
        actionById(registry.actions(true), QStringLiteral("terminal.find")).value(QStringLiteral("enabled")).toBool());
    QCOMPARE(actionById(registry.actions(true), QStringLiteral("terminal.copy"))
                 .value(QStringLiteral("shortcut"))
                 .toString(),
             QStringLiteral("Ctrl+Shift+C"));
    QCOMPARE(actionById(registry.actions(true), QStringLiteral("terminal.paste"))
                 .value(QStringLiteral("shortcut"))
                 .toString(),
             QStringLiteral("Ctrl+Shift+V"));

    const QVariantMap sftp = actionById(registry.actions(true), QStringLiteral("terminal.sftp"));
    QCOMPARE(sftp.value(QStringLiteral("category")).toString(), QStringLiteral("terminal"));
    QVERIFY(sftp.value(QStringLiteral("paletteVisible")).toBool());
    QVERIFY(sftp.value(QStringLiteral("shortcut")).toString().isEmpty());

    const QVariantMap sessionLog = actionById(registry.actions(true), QStringLiteral("terminal.sessionLog"));
    QVERIFY(sessionLog.value(QStringLiteral("enabled")).toBool());
    QVERIFY(sessionLog.value(QStringLiteral("paletteVisible")).toBool());

    const QVariantMap split = actionById(registry.actions(true), QStringLiteral("terminal.splitHorizontal"));
    QCOMPARE(split.value(QStringLiteral("shortcut")).toString(), QStringLiteral("Alt+Shift+H"));
    QVERIFY(split.value(QStringLiteral("enabled")).toBool());
    QVERIFY(!actionById(registry.actions(false), QStringLiteral("terminal.focusNextPane"))
                 .value(QStringLiteral("enabled"))
                 .toBool());
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

void ActionRegistryTests::reconnectShortcutIsConfigurableAndPreservesExistingBindings()
{
    ztermy::actions::ActionRegistry registry;
    const QString id = QStringLiteral("terminal.reconnect");
    QCOMPARE(registry.defaultShortcut(id), QStringLiteral("Ctrl+R"));
    QVERIFY(!registry.allowsAutoRepeat(id));
    QVERIFY(!registry.enabled(id, false));
    QVERIFY(actionById(registry.actions(true), id).value(QStringLiteral("paletteVisible")).toBool());
    QVERIFY(registry.setShortcut(id, QStringLiteral("Ctrl+Alt+R")).valid());
    QCOMPARE(registry.effectiveShortcut(id), QStringLiteral("Ctrl+Alt+R"));
    QVERIFY(registry.setShortcut(id, {}).valid());
    QVERIFY(registry.effectiveShortcut(id).isEmpty());
    QVERIFY(registry.resetShortcut(id));
    QCOMPARE(registry.effectiveShortcut(id), QStringLiteral("Ctrl+R"));

    // Both earlier and later catalog entries keep a legacy customized Ctrl+R.
    for (const auto *owner : {"application.hosts", "terminal.find"})
    {
        const QString ownerId = QString::fromLatin1(owner);
        registry.setOverrides({{ownerId, QStringLiteral("Ctrl+R")}});
        QCOMPARE(registry.effectiveShortcut(ownerId), QStringLiteral("Ctrl+R"));
        QVERIFY(registry.effectiveShortcut(id).isEmpty());
        QCOMPARE(registry.validateShortcut(id, QStringLiteral("Ctrl+R")).error,
                 ztermy::actions::ShortcutValidationError::Conflict);
        registry.setOverrides(registry.overrides());
        QCOMPARE(registry.effectiveShortcut(ownerId), QStringLiteral("Ctrl+R"));
        QVERIFY(registry.effectiveShortcut(id).isEmpty());
    }
    registry.setOverrides({{id, QStringLiteral("Ctrl+Alt+R")}});
    QCOMPARE(registry.effectiveShortcut(id), QStringLiteral("Ctrl+Alt+R"));
}

QTEST_MAIN(ActionRegistryTests)

#include "action_registry_tests.moc"
