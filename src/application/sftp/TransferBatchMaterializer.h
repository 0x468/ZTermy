#pragma once

#include "application/sftp/SftpClient.h"
#include "domain/sftp/SftpTypes.h"
#include "domain/sftp/TransferBatch.h"

#include <cstdint>
#include <expected>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace ztermy::sftp
{

enum class TransferMaterializationError : std::uint8_t
{
    Cancelled,
    InvalidBatch,
    InvalidDestination,
    LocalIo,
    RemoteIo,
    DestinationConflict,
};

[[nodiscard]] std::expected<std::vector<TransferTask>, TransferMaterializationError>
materializeTransferBatch(TransferBatch &batch, SftpClient *remoteClient, std::string_view filenameEncoding,
                         const std::stop_token &stopToken = {});

} // namespace ztermy::sftp
