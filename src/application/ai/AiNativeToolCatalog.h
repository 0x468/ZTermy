#pragma once

#include "domain/ai/AiProviderTypes.h"
#include "domain/ai/AiReadTools.h"

#include <vector>

namespace ztermy::ai
{

struct AiNativeToolCapabilities final
{
    bool terminalBufferAvailable = false;
    bool terminalFrameAvailable = false;
    bool actionsAllowed = false;
    bool terminalWriteAvailable = false;
    bool commandWaitAvailable = false;
    bool sftpBrowserAvailable = false;
    bool sftpTransferAvailable = false;
    bool remoteTelemetryAvailable = false;
};

class AiNativeToolCatalog final
{
public:
    [[nodiscard]] static std::vector<AiToolDefinition> build(const AiTerminalReadSnapshot &terminal,
                                                             AiNativeToolCapabilities capabilities);
};

} // namespace ztermy::ai
