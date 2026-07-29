#include "infrastructure/ssh/Libssh2Runtime.h"

#include <libssh2.h>

#include <new>
#include <string>

namespace
{

class Libssh2ErrorCategory final : public std::error_category
{
public:
    [[nodiscard]] const char *name() const noexcept override { return "libssh2"; }

    [[nodiscard]] std::string message(const int condition) const override
    {
        return condition == 0 ? "success" : "libssh2 initialization failed";
    }
};

[[nodiscard]] const std::error_category &libssh2ErrorCategory() noexcept
{
    static const Libssh2ErrorCategory category;
    return category;
}

} // namespace

namespace ztermy::ssh
{

std::expected<std::unique_ptr<Libssh2Runtime>, std::error_code> Libssh2Runtime::create() noexcept
{
    const int result = libssh2_init(0);
    if (result != 0)
    {
        return std::unexpected(std::error_code(result, libssh2ErrorCategory()));
    }

    const char *version = libssh2_version(LIBSSH2_VERSION_NUM);
    if (version == nullptr)
    {
        libssh2_exit();
        return std::unexpected(std::error_code(LIBSSH2_ERROR_INVAL, libssh2ErrorCategory()));
    }

    std::unique_ptr<Libssh2Runtime> runtime(new (std::nothrow) Libssh2Runtime(version));
    if (!runtime)
    {
        libssh2_exit();
        return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
    }
    return runtime;
}

Libssh2Runtime::Libssh2Runtime(const char *version) noexcept : m_version(version) {}

Libssh2Runtime::~Libssh2Runtime()
{
    libssh2_exit();
}

std::string_view Libssh2Runtime::version() const noexcept
{
    return m_version == nullptr ? std::string_view{} : std::string_view{m_version};
}

bool Libssh2Runtime::usesWindowsCng() const noexcept
{
    return libssh2_crypto_engine() == libssh2_wincng;
}

} // namespace ztermy::ssh
