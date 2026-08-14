#pragma once

#include "domain/ai/AiCommandTracker.h"
#include "domain/ai/AiProviderTypes.h"
#include "infrastructure/ssh/Libssh2Session.h"

#include <QByteArray>

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>

namespace ztermy::ai
{

struct AiSftpReadRequest final
{
    AiSessionTarget target;
    std::string remotePath;
    std::size_t maximumBytes = 0;
    std::string encoding;
};

class AiSftpReadTool final
{
public:
    [[nodiscard]] static AiToolDefinition definition();
    [[nodiscard]] static std::expected<AiSftpReadRequest, std::string> parse(std::string_view argumentsJson,
                                                                            const AiSessionTarget &target);
    [[nodiscard]] static std::string result(const AiSftpReadRequest &request, const QByteArray &bytes, bool truncated);
    [[nodiscard]] static std::string failure(std::string_view code, std::string_view message);
    [[nodiscard]] static std::string failure(ssh::SshTransportErrorKind error);
};

} // namespace ztermy::ai
