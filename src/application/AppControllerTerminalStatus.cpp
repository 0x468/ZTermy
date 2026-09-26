#include "application/AppController.h"

#include <QElapsedTimer>

namespace ztermy
{
void AppController::updateTerminalStatus(TerminalTab &tab, const terminal::TerminalSnapshotPtr &snapshot)
{
    const QString previousTitle = tab.displayTitle(m_settings.allowTerminalTitleChanges);
    const auto previousProgress = tab.snapshot ? tab.snapshot->progress : terminal::TerminalProgress{};
    const auto previousNotification = tab.snapshot ? tab.snapshot->notification : nullptr;
    tab.snapshot = snapshot;
    if (previousTitle != tab.displayTitle(m_settings.allowTerminalTitleChanges)
        || (snapshot && previousProgress != snapshot->progress))
        scheduleTerminalTabsChanged();
    if (!snapshot || !snapshot->notification || snapshot->notification == previousNotification || m_shutdownStarted)
        return;
    QElapsedTimer clock;
    clock.start();
    const auto now = clock.msecsSinceReference();
    if (now < m_nextTerminalNotificationMs)
        return;
    m_nextTerminalNotificationMs = now + 2000;
    const auto &notification = *snapshot->notification;
    const auto label = [](const std::string &value) {
        return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())).simplified();
    };
    const QString body = label(notification.title) + (notification.title.empty() ? QString{} : QStringLiteral(": "))
                         + label(notification.body);
    const auto *workspace = findTerminalWorkspace(tab.workspaceId);
    emit terminalNotificationRequested({
        {QStringLiteral("windowId"), workspace ? QString::fromStdString(workspace->windowId) : QStringLiteral("main")},
        {QStringLiteral("title"), tr("Terminal notification — %1").arg(tab.displayTitle(false))},
        {QStringLiteral("message"), body.left(1024)},
    });
}
} // namespace ztermy
