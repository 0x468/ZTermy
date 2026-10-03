#pragma once

#include <QQuickItem>

#include <array>

namespace ztermy::ui::runtime
{

inline bool verifyWindowSettingsLayout(QObject &root)
{
    const std::array names{"settingsPreserveTerminalSessionsSwitch", "settingsRestoreDetachedWindowsSwitch",
                           "settingsReopenLocalSessionsSwitch", "settingsReconnectRemoteSessionsSwitch",
                           "settingsRetainHistoryOnReconnectSwitch"};
    std::array<QQuickItem *, 5> controls{};
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        controls[index] = root.findChild<QQuickItem *>(QString::fromLatin1(names[index]));
        if (controls[index] == nullptr || !controls[index]->isVisible())
        {
            return false;
        }
        if (index > 0
            && (controls[index]->parentItem() != controls[0]->parentItem()
                || controls[index]->y() < controls[index - 1]->y() + controls[index - 1]->height()))
        {
            return false;
        }
    }
    const qreal rootPadding = controls[0]->property("leftPadding").toReal();
    if (controls[4]->property("leftPadding").toReal() != rootPadding)
    {
        return false;
    }
    const bool savedChecked = controls[0]->property("checked").toBool();
    bool matches = true;
    for (const bool checked : {false, true})
    {
        controls[0]->setProperty("checked", checked);
        for (std::size_t index = 1; index < 4; ++index)
        {
            matches = matches && controls[index]->isEnabled() == checked
                      && controls[index]->property("leftPadding").toReal() > rootPadding;
        }
        matches = matches && controls[4]->isEnabled();
    }
    controls[0]->setProperty("checked", savedChecked);
    return matches;
}

} // namespace ztermy::ui::runtime
