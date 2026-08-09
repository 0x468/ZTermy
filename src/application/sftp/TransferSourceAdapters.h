#pragma once

#include "application/sftp/SftpClient.h"
#include "domain/sftp/TransferPlanner.h"

namespace ztermy::sftp
{

class LocalTransferSourceTree final : public TransferSourceTree
{
public:
    [[nodiscard]] std::expected<TransferSourceNode, TransferSourceError>
    stat(std::string_view sourcePath, const std::stop_token &stopToken) override;
    [[nodiscard]] std::expected<std::vector<TransferSourceNode>, TransferSourceError>
    list(std::string_view sourcePath, const std::stop_token &stopToken) override;
};

class RemoteTransferSourceTree final : public TransferSourceTree
{
public:
    explicit RemoteTransferSourceTree(SftpClient &client) noexcept;

    [[nodiscard]] std::expected<TransferSourceNode, TransferSourceError>
    stat(std::string_view sourcePath, const std::stop_token &stopToken) override;
    [[nodiscard]] std::expected<std::vector<TransferSourceNode>, TransferSourceError>
    list(std::string_view sourcePath, const std::stop_token &stopToken) override;

private:
    SftpClient &m_client;
};

} // namespace ztermy::sftp
