#include "application/workbench/RemoteShellHistoryReader.h"

#include <QByteArray>
#include <QCoreApplication>

#include <algorithm>
#include <array>
#include <utility>

namespace ztermy::workbench
{
namespace
{
std::expected<QByteArray, QString> readFile(sftp::SftpClient &client, const std::string &path,
                                            const std::stop_token &token, const std::size_t limit)
{
    const auto failure = [] {
        return QCoreApplication::translate("AppController", "Shell history file could not be read.");
    };
    auto info = client.statEntry(path, token);
    if (!client.openFileForRead(path, token))
        return std::unexpected(failure());
    QByteArray bytes;
    std::array<char, 8192> chunk{};
    // Read a bounded tail for large histories; do not read or persist the full file.
    if (info && *info && (*info)->size > limit)
    {
        if (!client.seekFile((*info)->size - limit))
        {
            [[maybe_unused]] const auto closeResult = client.closeFile(token);
            return std::unexpected(failure());
        }
    }
    while (!token.stop_requested() && std::cmp_less(bytes.size(), limit))
    {
        const auto count = client.readFile(
            std::span(chunk).first(std::min(chunk.size(), limit - static_cast<std::size_t>(bytes.size()))), token);
        if (!count)
        {
            [[maybe_unused]] const auto closeResult = client.closeFile(token);
            return std::unexpected(failure());
        }
        if (*count == 0)
            break;
        bytes.append(chunk.data(), static_cast<qsizetype>(*count));
    }
    const auto closed = client.closeFile(token);
    if (!closed || token.stop_requested())
        return std::unexpected(failure());
    if (info && *info && (*info)->size > limit)
    {
        const auto newline = bytes.indexOf('\n');
        bytes = newline < 0 ? QByteArray{} : bytes.sliced(newline + 1);
    }
    return bytes;
}
} // namespace

std::expected<std::vector<ShellHistoryEntry>, QString>
readRemoteShellHistory(sftp::SftpClient &client, const QString &username, const std::stop_token &stopToken)
{
    auto accounts = readFile(client, "/etc/passwd", stopToken, std::size_t{256} * 1024);
    if (!accounts)
        return std::unexpected(accounts.error());
    for (const auto &line : accounts->split('\n'))
    {
        const auto fields = line.split(':');
        if (fields.size() != 7 || QString::fromUtf8(fields[0]) != username)
            continue;
        const auto shell = fields[6].trimmed();
        const auto &home = fields[5];
        QString filename;
        if (shell.endsWith("/bash"))
            filename = QStringLiteral("/.bash_history");
        else if (shell.endsWith("/zsh"))
            filename = QStringLiteral("/.zsh_history");
        else if (shell.endsWith("/fish"))
            filename = QStringLiteral("/.local/share/fish/fish_history");
        else
            break;
        auto bytes =
            readFile(client, home.toStdString() + filename.toStdString(), stopToken, std::size_t{2} * 1024 * 1024);
        if (!bytes)
            return std::unexpected(bytes.error());
        const std::string_view contents(bytes->constData(), static_cast<std::size_t>(bytes->size()));
        if (shell.endsWith("/bash"))
            return parseBashHistory(contents);
        if (shell.endsWith("/zsh"))
            return parseZshHistory(contents);
        return parseFishHistory(contents);
    }
    return std::unexpected(QCoreApplication::translate(
        "AppController",
        "The account's default Shell history file could not be identified. Session commands remain available."));
}
} // namespace ztermy::workbench
