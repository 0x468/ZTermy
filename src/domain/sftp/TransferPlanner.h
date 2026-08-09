#pragma once

#include "domain/sftp/SftpTypes.h"
#include "domain/sftp/TransferBatch.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace ztermy::sftp
{

struct TransferSourceError final
{
    std::string code;
};

struct TransferSourceNode final
{
    std::string sourcePath;
    std::string name;
    EntryType type = EntryType::Other;
    std::uint64_t size = 0;
    std::optional<std::int64_t> modifiedUtcSeconds;
};

class TransferSourceTree
{
public:
    virtual ~TransferSourceTree() = default;

    TransferSourceTree(const TransferSourceTree &) = delete;
    TransferSourceTree &operator=(const TransferSourceTree &) = delete;

    [[nodiscard]] virtual std::expected<TransferSourceNode, TransferSourceError>
    stat(std::string_view sourcePath, const std::stop_token &stopToken) = 0;
    [[nodiscard]] virtual std::expected<std::vector<TransferSourceNode>, TransferSourceError>
    list(std::string_view sourcePath, const std::stop_token &stopToken) = 0;

protected:
    TransferSourceTree() = default;
};

enum class TransferPlanningError : std::uint8_t
{
    Cancelled,
    InvalidRequest,
    InvalidSource,
    DuplicateDestination,
    SourceUnavailable,
    EntryLimit,
    DepthLimit,
};

struct TransferPlanRequest final
{
    std::string batchId;
    std::string endpointId;
    std::string displayName;
    std::string destinationRoot;
    std::vector<std::string> sourceRoots;
    TransferBatchDirection direction = TransferBatchDirection::Upload;
    TransferConflictPolicy conflictPolicy = TransferConflictPolicy::Ask;
};

[[nodiscard]] std::expected<TransferBatch, TransferPlanningError>
planTransferTree(const TransferPlanRequest &request, TransferSourceTree &source, const std::stop_token &stopToken = {});

} // namespace ztermy::sftp
