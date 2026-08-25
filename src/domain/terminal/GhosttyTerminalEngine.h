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
    [[nodiscard]] std::expected<TerminalSnapshot, std::error_code> snapshot() override;
    [[nodiscard]] std::error_code setSelection(std::optional<TerminalSelection> selection) override;
    [[nodiscard]] std::expected<bool, std::error_code>
    applySelectionGesture(const TerminalSelectionGesture &gesture) override;
    [[nodiscard]] std::error_code selectAll() override;
    [[nodiscard]] std::expected<std::optional<std::string>, std::error_code> selectedText() const override;
    void scrollViewport(int rows) override;
    void scrollToBottom() override;
    [[nodiscard]] std::expected<TerminalSearchResult, std::error_code>
    search(std::string_view query, TerminalSearchDirection direction, bool caseSensitive) override;
    [[nodiscard]] std::error_code clearSearch() override;
    [[nodiscard]] std::expected<std::vector<std::byte>, std::error_code>
    encodePaste(std::span<const std::byte> bytes) const override;
    [[nodiscard]] std::expected<std::vector<std::byte>, std::error_code>
    encodeKey(const TerminalKeyEvent &event) override;
    [[nodiscard]] std::expected<std::vector<std::byte>, std::error_code>
    encodeMouse(const TerminalMouseEvent &event) override;
    [[nodiscard]] std::expected<std::vector<std::byte>, std::error_code> encodeFocus(bool focused) const override;
    [[nodiscard]] std::expected<std::string, std::error_code> plainText() const override;
    [[nodiscard]] std::expected<TerminalScrollbackPage, std::error_code>
    scrollbackPage(TerminalScrollbackRequest request) const override;

private:
    struct Impl;

    explicit GhosttyTerminalEngine(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace ztermy::terminal
