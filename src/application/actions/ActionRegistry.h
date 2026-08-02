#pragma once

#include <QMap>
#include <QString>
#include <QVariantList>

#include <cstdint>

namespace ztermy::actions
{

enum class ShortcutValidationError : std::uint8_t
{
    None,
    UnknownAction,
    InvalidSequence,
    MultipleSteps,
    UnmodifiedPrintable,
    Conflict,
};

struct ShortcutValidation final
{
    ShortcutValidationError error = ShortcutValidationError::None;
    QString normalized;
    QString conflictingActionId;

    [[nodiscard]] bool valid() const noexcept { return error == ShortcutValidationError::None; }
};

class ActionRegistry final
{
public:
    ActionRegistry() = default;

    [[nodiscard]] QVariantList actions(bool terminalAvailable) const;
    [[nodiscard]] QString defaultShortcut(const QString &actionId) const;
    [[nodiscard]] QString effectiveShortcut(const QString &actionId) const;
    [[nodiscard]] bool contains(const QString &actionId) const noexcept;
    [[nodiscard]] bool enabled(const QString &actionId, bool terminalAvailable) const noexcept;
    [[nodiscard]] bool allowsAutoRepeat(const QString &actionId) const noexcept;
    [[nodiscard]] ShortcutValidation validateShortcut(const QString &actionId, const QString &shortcut) const;
    [[nodiscard]] ShortcutValidation setShortcut(const QString &actionId, const QString &shortcut);
    [[nodiscard]] bool resetShortcut(const QString &actionId);
    [[nodiscard]] bool resetAllShortcuts();
    void setOverrides(const QMap<QString, QString> &overrides);
    [[nodiscard]] const QMap<QString, QString> &overrides() const noexcept;

    [[nodiscard]] static QString shortcutFromKeyEvent(int key, int modifiers);
    [[nodiscard]] static QString validationMessage(ShortcutValidationError error, const QString &conflictingLabel);

private:
    QMap<QString, QString> m_overrides;
};

} // namespace ztermy::actions
