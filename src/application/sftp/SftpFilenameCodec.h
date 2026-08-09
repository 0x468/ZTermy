#pragma once

#include "application/sftp/SftpClient.h"

#include <memory>
#include <string>

namespace ztermy::sftp
{

[[nodiscard]] std::unique_ptr<SftpClient> withFilenameEncoding(std::unique_ptr<SftpClient> client,
                                                               std::string encoding);

} // namespace ztermy::sftp
