#include "application/actions/ActionRegistry.h"

#include <QCoreApplication>
#include <QKeyCombination>
#include <QKeySequence>
#include <QVariantMap>

#include <algorithm>
#include <array>

namespace
{

struct ActionDescriptor final
{
    const char *id;
    const char *category;
    const char *label;
    const char *description;
    const char *defaultShortcut;
    bool terminalRequired;
    bool paletteVisible;
    bool autoRepeat;
};

constexpr std::array actions{
    ActionDescriptor{
        .id = "application.commandPalette",
        .category = "application",
        .label = QT_TRANSLATE_NOOP("ActionRegistry", "Command palette"),
        .description = QT_TRANSLATE_NOOP("ActionRegistry", "Search and run ztermy actions."),
        .defaultShortcut = "Ctrl+Shift+P",
        .terminalRequired = false,
        .paletteVisible = true,
        .autoRepeat = false,
    },
    ActionDescriptor{
        .id = "application.hosts",
        .category = "application",
        .label = QT_TRANSLATE_NOOP("ActionRegistry", "Open Hosts"),
        .description = QT_TRANSLATE_NOOP("ActionRegistry", "Open the saved hosts workspace."),
        .defaultShortcut = "",
        .terminalRequired = false,
        .paletteVisible = true,
        .autoRepeat = false,
    },
    ActionDescriptor{
        .id = "application.settings",
        .category = "application",
        .label = QT_TRANSLATE_NOOP("ActionRegistry", "Open Settings"),
        .description = QT_TRANSLATE_NOOP("ActionRegistry", "Open the Settings work tab."),
        .defaultShortcut = "Ctrl+,",
        .terminalRequired = false,
        .paletteVisible = true,
        .autoRepeat = false,
    },
    ActionDescriptor{
        .id = "application.transfers",
        .category = "application",
        .label = QT_TRANSLATE_NOOP("ActionRegistry", "File transfers"),
        .description = QT_TRANSLATE_NOOP("ActionRegistry", "Open the application-wide file transfer center."),
        .defaultShortcut = "",
        .terminalRequired = false,
        .paletteVisible = true,
        .autoRepeat = false,
    },
    ActionDescriptor{
        .id = "terminal.newLocal",
        .category = "application",
        .label = QT_TRANSLATE_NOOP("ActionRegistry", "New local terminal"),
        .description = QT_TRANSLATE_NOOP("ActionRegistry", "Open a new local terminal tab."),
        .defaultShortcut = "Ctrl+Shift+T",
        .terminalRequired = false,
        .paletteVisible = true,
        .autoRepeat = false,
    },
    ActionDescriptor{
        .id = "scripts.import",
        .category = "scripts",
        .label = QT_TRANSLATE_NOOP("ActionRegistry", "Import command snippet library"),
        .description = QT_TRANSLATE_NOOP("ActionRegistry", "Merge a ztermy command snippet library from JSON."),
        .defaultShortcut = "",
        .terminalRequired = false,
        .paletteVisible = true,
        .autoRepeat = false,
    },
    ActionDescriptor{
        .id = "scripts.export",
        .category = "scripts",
        .label = QT_TRANSLATE_NOOP("ActionRegistry", "Export command snippet library"),
        .description = QT_TRANSLATE_NOOP("ActionRegistry", "Export all ztermy command snippets and metadata to JSON."),
        .defaultShortcut = "",
        .terminalRequired = false,
        .paletteVisible = true,
        .autoRepeat = false,
    },
    ActionDescriptor{
        .id = "tabs.close",
        .category = "tabs",
        .label = QT_TRANSLATE_NOOP("ActionRegistry", "Close terminal tab"),
        .description = QT_TRANSLATE_NOOP("ActionRegistry", "Close the active terminal tab."),
        .defaultShortcut = "Ctrl+Shift+W",
        .terminalRequired = true,
        .paletteVisible = true,
        .autoRepeat = false,
    },
    ActionDescriptor{
        .id = "tabs.next",
        .category = "tabs",
        .label = QT_TRANSLATE_NOOP("ActionRegistry", "Next terminal tab"),
        .description = QT_TRANSLATE_NOOP("ActionRegistry", "Activate the next open terminal tab."),
        .defaultShortcut = "Ctrl+Tab",
        .terminalRequired = true,
        .paletteVisible = true,
        .autoRepeat = true,
    },
    ActionDescriptor{
        .id = "tabs.previous",
        .category = "tabs",
        .label = QT_TRANSLATE_NOOP("ActionRegistry", "Previous terminal tab"),
        .description = QT_TRANSLATE_NOOP("ActionRegistry", "Activate the previous open terminal tab."),
        .defaultShortcut = "Ctrl+Shift+Tab",
        .terminalRequired = true,
        .paletteVisible = true,
        .autoRepeat = true,
    },
    ActionDescriptor{
        .id = "terminal.find",
        .category = "terminal",
        .label = QT_TRANSLATE_NOOP("ActionRegistry", "Find in terminal"),
        .description = QT_TRANSLATE_NOOP("ActionRegistry", "Search the active terminal buffer."),
        .defaultShortcut = "Ctrl+Shift+F",
        .terminalRequired = true,
        .paletteVisible = true,
        .autoRepeat = false,
    },
    ActionDescriptor{
        .id = "terminal.history",
        .category = "terminal",
        .label = QT_TRANSLATE_NOOP("ActionRegistry", "Show command history"),
        .description = QT_TRANSLATE_NOOP("ActionRegistry", "Open the active terminal's command history panel."),
        .defaultShortcut = "Ctrl+Shift+H",
        .terminalRequired = true,
        .paletteVisible = true,
        .autoRepeat = false,
    },
    ActionDescriptor{
        .id = "terminal.scripts",
        .category = "terminal",
        .label = QT_TRANSLATE_NOOP("ActionRegistry", "Show command snippets"),
        .description = QT_TRANSLATE_NOOP("ActionRegistry", "Open the reusable command snippets panel."),
        .defaultShortcut = "Ctrl+Shift+S",
        .terminalRequired = true,
        .paletteVisible = true,
        .autoRepeat = false,
    },
    ActionDescriptor{
        .id = "terminal.sftp",
        .category = "terminal",
        .label = QT_TRANSLATE_NOOP("ActionRegistry", "Show SFTP files"),
        .description = QT_TRANSLATE_NOOP("ActionRegistry", "Open the active SSH terminal's remote file browser."),
        .defaultShortcut = "",
        .terminalRequired = true,
        .paletteVisible = true,
        .autoRepeat = false,
    },
    ActionDescriptor{
        .id = "terminal.composer",
        .category = "terminal",
        .label = QT_TRANSLATE_NOOP("ActionRegistry", "Toggle command composer"),
        .description = QT_TRANSLATE_NOOP("ActionRegistry", "Show or hide the active terminal's command composer."),
        .defaultShortcut = "Ctrl+Shift+Space",
        .terminalRequired = true,
        .paletteVisible = true,
        .autoRepeat = false,
    },
    ActionDescriptor{
        .id = "terminal.sessionLog",
        .category = "terminal",
        .label = QT_TRANSLATE_NOOP("ActionRegistry", "Toggle session log"),
        .description = QT_TRANSLATE_NOOP("ActionRegistry", "Start or stop recording terminal output to a file."),
        .defaultShortcut = "",
        .terminalRequired = true,
        .paletteVisible = true,
        .autoRepeat = false,
    },
    ActionDescriptor{
        .id = "terminal.hideWorkbench",
        .category = "terminal",
        .label = QT_TRANSLATE_NOOP("ActionRegistry", "Hide terminal side panel"),
        .description = QT_TRANSLATE_NOOP("ActionRegistry", "Close the active terminal's side panel."),
        .defaultShortcut = "Ctrl+\\",
        .terminalRequired = true,
        .paletteVisible = true,
        .autoRepeat = false,
    },
    ActionDescriptor{
        .id = "terminal.moveWorkbench",
        .category = "terminal",
        .label = QT_TRANSLATE_NOOP("ActionRegistry", "Move terminal side panel"),
        .description =
            QT_TRANSLATE_NOOP("ActionRegistry", "Move the active terminal panel between the left and right sides."),
        .defaultShortcut = "",
        .terminalRequired = true,
        .paletteVisible = true,
        .autoRepeat = false,
    },
    ActionDescriptor{
        .id = "terminal.copyAddress",
        .category = "terminal",
        .label = QT_TRANSLATE_NOOP("ActionRegistry", "Copy host address"),
        .description = QT_TRANSLATE_NOOP("ActionRegistry", "Copy the active SSH host address to the clipboard."),
        .defaultShortcut = "",
        .terminalRequired = true,
        .paletteVisible = true,
        .autoRepeat = false,
    },
};

[[nodiscard]] const ActionDescriptor *findDescriptor(const QString &id) noexcept
{
    const auto found = std::ranges::find_if(actions, [&id](const ActionDescriptor &descriptor) {
        return id == QLatin1StringView(descriptor.id);
    });
    return found == actions.end() ? nullptr : &*found;
}

[[nodiscard]] QString translated(const char *source)
{
    return QCoreApplication::translate("ActionRegistry", source);
}

[[nodiscard]] QString categoryLabel(const char *category)
{
    const QLatin1StringView token(category);
    if (token == QLatin1StringView("tabs"))
    {
        return translated(QT_TRANSLATE_NOOP("ActionRegistry", "Tabs"));
    }
    if (token == QLatin1StringView("terminal"))
    {
        return translated(QT_TRANSLATE_NOOP("ActionRegistry", "Terminal"));
    }
    if (token == QLatin1StringView("scripts"))
    {
        return translated(QT_TRANSLATE_NOOP("ActionRegistry", "Command snippets"));
    }
    return translated(QT_TRANSLATE_NOOP("ActionRegistry", "Application"));
}

[[nodiscard]] bool isUnmodifiedPrintable(const QKeyCombination combination) noexcept
{
    const int key = static_cast<int>(combination.key());
    return combination.keyboardModifiers() == Qt::NoModifier && key >= 0x20 && key <= 0x7e;
}

[[nodiscard]] ztermy::actions::ShortcutValidation normalizedShortcut(const QString &actionId, const QString &shortcut)
{
    using ztermy::actions::ShortcutValidation;
    using ztermy::actions::ShortcutValidationError;
    if (findDescriptor(actionId) == nullptr)
    {
        return {.error = ShortcutValidationError::UnknownAction};
    }
    QString candidate = shortcut.trimmed();
    if (candidate.isEmpty())
    {
        return {.normalized = {}};
    }
    candidate.removeIf([](const QChar character) {
        return character.isSpace();
    });
    const QKeySequence sequence = QKeySequence::fromString(candidate, QKeySequence::PortableText);
    if (sequence.isEmpty())
    {
        return {.error = ShortcutValidationError::InvalidSequence};
    }
    if (sequence.count() != 1)
    {
        return {.error = ShortcutValidationError::MultipleSteps};
    }
    if (isUnmodifiedPrintable(sequence[0]))
    {
        return {.error = ShortcutValidationError::UnmodifiedPrintable};
    }
    const QString normalized = sequence.toString(QKeySequence::PortableText);
    return normalized.isEmpty() ? ShortcutValidation{.error = ShortcutValidationError::InvalidSequence}
                                : ShortcutValidation{.normalized = normalized};
}

} // namespace

namespace ztermy::actions
{

QVariantList ActionRegistry::actions(const bool terminalAvailable) const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(::actions.size()));
    for (const ActionDescriptor &descriptor : ::actions)
    {
        const QString id = QLatin1StringView(descriptor.id);
        const QString shortcut = effectiveShortcut(id);
        result.push_back(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("category"), QLatin1StringView(descriptor.category)},
            {QStringLiteral("categoryLabel"), categoryLabel(descriptor.category)},
            {QStringLiteral("label"), translated(descriptor.label)},
            {QStringLiteral("description"), translated(descriptor.description)},
            {QStringLiteral("shortcut"), shortcut},
            {QStringLiteral("defaultShortcut"), QLatin1StringView(descriptor.defaultShortcut)},
            {QStringLiteral("customized"), m_overrides.contains(id)},
            {QStringLiteral("enabled"), !descriptor.terminalRequired || terminalAvailable},
            {QStringLiteral("paletteVisible"), descriptor.paletteVisible},
            {QStringLiteral("autoRepeat"), descriptor.autoRepeat},
        });
    }
    return result;
}

QString ActionRegistry::effectiveShortcut(const QString &actionId) const
{
    const ActionDescriptor *descriptor = findDescriptor(actionId);
    if (descriptor == nullptr)
    {
        return {};
    }
    const auto override = m_overrides.constFind(actionId);
    return override == m_overrides.cend() ? QString::fromLatin1(descriptor->defaultShortcut) : override.value();
}

QString ActionRegistry::defaultShortcut(const QString &actionId) const
{
    const ActionDescriptor *descriptor = findDescriptor(actionId);
    return descriptor == nullptr ? QString{} : QString::fromLatin1(descriptor->defaultShortcut);
}

bool ActionRegistry::contains(const QString &actionId) const noexcept
{
    return findDescriptor(actionId) != nullptr;
}

bool ActionRegistry::enabled(const QString &actionId, const bool terminalAvailable) const noexcept
{
    const ActionDescriptor *descriptor = findDescriptor(actionId);
    return descriptor != nullptr && (!descriptor->terminalRequired || terminalAvailable);
}

bool ActionRegistry::allowsAutoRepeat(const QString &actionId) const noexcept
{
    const ActionDescriptor *descriptor = findDescriptor(actionId);
    return descriptor != nullptr && descriptor->autoRepeat;
}

ShortcutValidation ActionRegistry::validateShortcut(const QString &actionId, const QString &shortcut) const
{
    ShortcutValidation normalized = normalizedShortcut(actionId, shortcut);
    if (!normalized.valid() || normalized.normalized.isEmpty())
    {
        return normalized;
    }
    const QKeySequence sequence = QKeySequence::fromString(normalized.normalized, QKeySequence::PortableText);
    for (const ActionDescriptor &descriptor : ::actions)
    {
        const QString otherId = QLatin1StringView(descriptor.id);
        if (otherId == actionId)
        {
            continue;
        }
        const QString otherShortcut = effectiveShortcut(otherId);
        if (!otherShortcut.isEmpty() && QKeySequence::fromString(otherShortcut, QKeySequence::PortableText) == sequence)
        {
            return {
                .error = ShortcutValidationError::Conflict,
                .normalized = normalized.normalized,
                .conflictingActionId = otherId,
            };
        }
    }
    return normalized;
}

ShortcutValidation ActionRegistry::setShortcut(const QString &actionId, const QString &shortcut)
{
    ShortcutValidation validation = validateShortcut(actionId, shortcut);
    if (validation.valid())
    {
        const ActionDescriptor *descriptor = findDescriptor(actionId);
        Q_ASSERT(descriptor != nullptr);
        if (validation.normalized == QLatin1StringView(descriptor->defaultShortcut))
        {
            m_overrides.remove(actionId);
        }
        else
        {
            m_overrides.insert(actionId, validation.normalized);
        }
    }
    return validation;
}

bool ActionRegistry::resetShortcut(const QString &actionId)
{
    return contains(actionId) && m_overrides.remove(actionId) > 0;
}

bool ActionRegistry::resetAllShortcuts()
{
    if (m_overrides.isEmpty())
    {
        return false;
    }
    m_overrides.clear();
    return true;
}

void ActionRegistry::setOverrides(const QMap<QString, QString> &overrides)
{
    QMap<QString, QString> candidates;
    for (auto entry = overrides.cbegin(); entry != overrides.cend(); ++entry)
    {
        const ShortcutValidation validation = normalizedShortcut(entry.key(), entry.value());
        if (!validation.valid())
        {
            continue;
        }
        const ActionDescriptor *descriptor = findDescriptor(entry.key());
        Q_ASSERT(descriptor != nullptr);
        if (validation.normalized != QLatin1StringView(descriptor->defaultShortcut))
        {
            candidates.insert(entry.key(), validation.normalized);
        }
    }

    m_overrides = candidates;
    QMap<QString, QString> owners;
    for (const ActionDescriptor &descriptor : ::actions)
    {
        const QString id = QLatin1StringView(descriptor.id);
        const QString shortcut = effectiveShortcut(id);
        if (shortcut.isEmpty())
        {
            continue;
        }
        const QString sequenceKey =
            QKeySequence::fromString(shortcut, QKeySequence::PortableText).toString(QKeySequence::PortableText);
        const auto owner = owners.constFind(sequenceKey);
        if (owner == owners.cend())
        {
            owners.insert(sequenceKey, id);
            continue;
        }
        if (m_overrides.contains(id))
        {
            m_overrides.remove(id);
        }
        else if (m_overrides.contains(owner.value()))
        {
            m_overrides.remove(owner.value());
            owners.insert(sequenceKey, id);
        }
    }
}

const QMap<QString, QString> &ActionRegistry::overrides() const noexcept
{
    return m_overrides;
}

QString ActionRegistry::shortcutFromKeyEvent(const int key, const int modifiers)
{
    const auto keyboardModifiers = static_cast<Qt::KeyboardModifiers>(modifiers);
    return QKeySequence(QKeyCombination(keyboardModifiers, static_cast<Qt::Key>(key)))
        .toString(QKeySequence::PortableText);
}

QString ActionRegistry::validationMessage(const ShortcutValidationError error, const QString &conflictingLabel)
{
    switch (error)
    {
        case ShortcutValidationError::UnknownAction:
            return translated(QT_TRANSLATE_NOOP("ActionRegistry", "This action is no longer available."));
        case ShortcutValidationError::InvalidSequence:
            return translated(QT_TRANSLATE_NOOP("ActionRegistry", "Press a valid key combination."));
        case ShortcutValidationError::MultipleSteps:
            return translated(QT_TRANSLATE_NOOP("ActionRegistry", "Multi-step shortcuts are not supported yet."));
        case ShortcutValidationError::UnmodifiedPrintable:
            return translated(QT_TRANSLATE_NOOP(
                "ActionRegistry", "Use Ctrl or Alt with printable keys so terminal input remains available."));
        case ShortcutValidationError::Conflict:
            return translated(QT_TRANSLATE_NOOP("ActionRegistry", "This shortcut is already assigned to %1."))
                .arg(conflictingLabel);
        case ShortcutValidationError::None:
        default:
            return {};
    }
}

} // namespace ztermy::actions
