#pragma once

#include "application/ai/AiActionToolDispatcher.h"
#include "application/sftp/SftpSession.h"
#include "domain/ai/AiProviderTypes.h"

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>

namespace ztermy::ai
{

struct AiSftpListRequest final
{
    AiSessionTarget target;
    std::string remotePath;
    std::size_t offset = 0;
    std::size_t limit = 0;
};

class AiSftpListTool final
{
public:
    [[nodiscard]] static AiToolDefinition definition();
    [[nodiscard]] static std::expected<AiSftpListRequest, std::string> parse(std::string_view argumentsJson,
                                                                             const AiSessionTarget &target);
    [[nodiscard]] static std::string result(const AiSftpListRequest &request, const sftp::DirectoryListing &entries);
    [[nodiscard]] static std::string failure(ssh::SshTransportErrorKind error);
    [[nodiscard]] static std::string failure(std::string_view code, std::string_view message);
};

} // namespace ztermy::ai
