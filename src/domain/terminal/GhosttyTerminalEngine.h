#pragma once

#include "domain/terminal/TerminalEngine.h"

#include <memory>

namespace ztermy::terminal
{

class GhosttyTerminalEngine final : public TerminalEngine
{
public:
    [[nodiscard]] static std::expected<std::unique_ptr<GhosttyTerminalEngine>, std::error_code>
    create(TerminalGeometry geometry);

    ~GhosttyTerminalEngine() override;

    [[nodiscard]] std::error_code feed(std::span<const std::byte> bytes) override;
    [[nodiscard]] std::error_code resize(TerminalGeometry geometry) override;
    [[nodiscard]] std::expected<std::string, std::error_code> plainText() const override;

private:
    struct Impl;

    explicit GhosttyTerminalEngine(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace ztermy::terminal
