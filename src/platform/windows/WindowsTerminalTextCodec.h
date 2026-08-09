#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <expected>
#include <span>

namespace ztermy::terminal
{

enum class TerminalEncoding : std::uint8_t
{
    Utf8,
    Gb18030,
};

enum class TerminalTextCodecError : std::uint8_t
{
    InvalidUtf8,
    ConversionFailed,
};

[[nodiscard]] QString terminalEncodingToken(TerminalEncoding encoding);
[[nodiscard]] std::expected<TerminalEncoding, TerminalTextCodecError> terminalEncodingFromToken(const QString &token);

class WindowsTerminalTextCodec final
{
public:
    [[nodiscard]] TerminalEncoding encoding() const noexcept;
    void setEncoding(TerminalEncoding encoding) noexcept;

    // Remote output is converted to UTF-8 before it reaches libghostty-vt or
    // the session-log sink. Incomplete GB18030 sequences are retained across
    // reads; malformed input is replaced so a bad byte cannot terminate the
    // terminal session.
    [[nodiscard]] QByteArray decodeRemote(std::span<const std::byte> bytes);

    // Terminal input is always UTF-8 at the UI/engine boundary. Conversion
    // failures are reported instead of silently sending mojibake remotely.
    [[nodiscard]] std::expected<QByteArray, TerminalTextCodecError> encodeRemote(const QByteArray &utf8) const;

    void reset() noexcept;

private:
    TerminalEncoding m_encoding = TerminalEncoding::Utf8;
    QByteArray m_pending;
};

} // namespace ztermy::terminal
