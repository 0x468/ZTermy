#pragma once

#include <expected>
#include <memory>
#include <string_view>
#include <system_error>

namespace ztermy::ssh
{

class Libssh2Runtime final
{
public:
    [[nodiscard]] static std::expected<std::unique_ptr<Libssh2Runtime>, std::error_code> create() noexcept;

    ~Libssh2Runtime();

    Libssh2Runtime(const Libssh2Runtime &) = delete;
    Libssh2Runtime &operator=(const Libssh2Runtime &) = delete;
    Libssh2Runtime(Libssh2Runtime &&) = delete;
    Libssh2Runtime &operator=(Libssh2Runtime &&) = delete;

    [[nodiscard]] std::string_view version() const noexcept;
    [[nodiscard]] bool usesWindowsCng() const noexcept;

private:
    explicit Libssh2Runtime(const char *version) noexcept;

    const char *m_version = nullptr;
};

} // namespace ztermy::ssh
