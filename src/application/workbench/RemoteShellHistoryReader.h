#pragma once

#include "application/sftp/SftpClient.h"
#include "domain/workbench/ShellHistory.h"

#include <QString>

namespace ztermy::workbench
{
// Uses SFTP reads only. The account database selects the login shell, not a
// guessed path or an injected command in the interactive terminal.
[[nodiscard]] std::expected<std::vector<ShellHistoryEntry>, QString>
readRemoteShellHistory(sftp::SftpClient &client, const QString &username, const std::stop_token &stopToken);
} // namespace ztermy::workbench
