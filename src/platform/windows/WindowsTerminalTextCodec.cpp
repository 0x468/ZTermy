#include "platform/windows/WindowsTerminalTextCodec.h"

#include <windows.h>

#include <QStringConverter>

#include <algorithm>
#include <limits>
#include <string>

namespace
{

constexpr UINT gb18030CodePage = 54936;

[[nodiscard]] bool gbLead(const unsigned char value) noexcept
{
    return value >= 0x81U && value <= 0xFEU;
}

[[nodiscard]] bool gbTrail(const unsigned char value) noexcept
{
    return value >= 0x40U && value <= 0xFEU && value != 0x7FU;
}

[[nodiscard]] bool gbDigit(const unsigned char value) noexcept
{
    return value >= 0x30U && value <= 0x39U;
}

[[nodiscard]] int sequenceLength(const QByteArray &bytes, const qsizetype index) noexcept
{
    const auto first = static_cast<unsigned char>(bytes.at(index));
    if (first <= 0x7FU)
    {
        return 1;
    }
    if (!gbLead(first))
    {
        return -1;
    }
    if (index + 1 >= bytes.size())
    {
        return 0;
    }
    const auto second = static_cast<unsigned char>(bytes.at(index + 1));
    if (gbTrail(second))
    {
        return 2;
    }
    if (!gbDigit(second))
    {
        return -1;
    }
    if (index + 3 >= bytes.size())
    {
        return 0;
    }
    const auto third = static_cast<unsigned char>(bytes.at(index + 2));
    const auto fourth = static_cast<unsigned char>(bytes.at(index + 3));
    return gbLead(third) && gbDigit(fourth) ? 4 : -1;
}

[[nodiscard]] QString decodeBatch(const QByteArray &bytes)
{
    if (bytes.isEmpty())
    {
        return {};
    }
    if (bytes.size() > std::numeric_limits<int>::max())
    {
        return {QChar::ReplacementCharacter};
    }
    const int sourceSize = static_cast<int>(bytes.size());
    const int required =
        MultiByteToWideChar(gb18030CodePage, MB_ERR_INVALID_CHARS, bytes.constData(), sourceSize, nullptr, 0);
    if (required <= 0)
    {
        return {QChar::ReplacementCharacter};
    }
    std::wstring decoded(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(gb18030CodePage, MB_ERR_INVALID_CHARS, bytes.constData(), sourceSize, decoded.data(),
                            required)
        != required)
    {
        return {QChar::ReplacementCharacter};
    }
    return QString::fromWCharArray(decoded.data(), required);
}

[[nodiscard]] std::expected<QByteArray, ztermy::terminal::TerminalTextCodecError> encodeGb18030(const QByteArray &utf8)
{
    if (utf8.isEmpty())
    {
        return QByteArray{};
    }
    if (utf8.size() > std::numeric_limits<int>::max())
    {
        return std::unexpected(ztermy::terminal::TerminalTextCodecError::ConversionFailed);
    }
    const int sourceSize = static_cast<int>(utf8.size());
    const int wideSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.constData(), sourceSize, nullptr, 0);
    if (wideSize <= 0)
    {
        return std::unexpected(ztermy::terminal::TerminalTextCodecError::InvalidUtf8);
    }
    std::wstring wide(static_cast<std::size_t>(wideSize), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.constData(), sourceSize, wide.data(), wideSize)
        != wideSize)
    {
        return std::unexpected(ztermy::terminal::TerminalTextCodecError::InvalidUtf8);
    }
    const int encodedSize =
        WideCharToMultiByte(gb18030CodePage, 0, wide.data(), wideSize, nullptr, 0, nullptr, nullptr);
    if (encodedSize <= 0)
    {
        return std::unexpected(ztermy::terminal::TerminalTextCodecError::ConversionFailed);
    }
    QByteArray encoded(encodedSize, Qt::Uninitialized);
    if (WideCharToMultiByte(gb18030CodePage, 0, wide.data(), wideSize, encoded.data(), encodedSize, nullptr, nullptr)
        != encodedSize)
    {
        return std::unexpected(ztermy::terminal::TerminalTextCodecError::ConversionFailed);
    }
    return encoded;
}

} // namespace

namespace ztermy::terminal
{

QString terminalEncodingToken(const TerminalEncoding encoding)
{
    return encoding == TerminalEncoding::Gb18030 ? QStringLiteral("gb18030") : QStringLiteral("utf-8");
}

std::expected<TerminalEncoding, TerminalTextCodecError> terminalEncodingFromToken(const QString &token)
{
    const QString normalized = token.trimmed().toLower();
    if (normalized == QStringLiteral("utf-8") || normalized == QStringLiteral("utf8"))
    {
        return TerminalEncoding::Utf8;
    }
    if (normalized == QStringLiteral("gb18030") || normalized == QStringLiteral("gbk"))
    {
        return TerminalEncoding::Gb18030;
    }
    return std::unexpected(TerminalTextCodecError::ConversionFailed);
}

TerminalEncoding WindowsTerminalTextCodec::encoding() const noexcept
{
    return m_encoding;
}

void WindowsTerminalTextCodec::setEncoding(const TerminalEncoding encoding) noexcept
{
    if (m_encoding == encoding)
    {
        return;
    }
    m_encoding = encoding;
    reset();
}

QByteArray WindowsTerminalTextCodec::decodeRemote(const std::span<const std::byte> bytes)
{
    if (bytes.empty())
    {
        return {};
    }
    if (m_encoding == TerminalEncoding::Utf8)
    {
        return {reinterpret_cast<const char *>(bytes.data()), static_cast<qsizetype>(bytes.size())};
    }

    QByteArray source = std::move(m_pending);
    m_pending.clear();
    source.append(reinterpret_cast<const char *>(bytes.data()), static_cast<qsizetype>(bytes.size()));

    QString decoded;
    decoded.reserve(source.size());
    qsizetype batchStart = 0;
    qsizetype index = 0;
    const auto flushBatch = [&](const qsizetype end) {
        if (end > batchStart)
        {
            decoded.append(decodeBatch(source.sliced(batchStart, end - batchStart)));
        }
    };
    while (index < source.size())
    {
        const int length = sequenceLength(source, index);
        if (length == 0)
        {
            flushBatch(index);
            m_pending = source.sliced(index);
            batchStart = source.size();
            index = source.size();
            break;
        }
        if (length < 0)
        {
            flushBatch(index);
            decoded.append(QChar::ReplacementCharacter);
            ++index;
            batchStart = index;
            continue;
        }
        index += length;
    }
    flushBatch(index);
    return decoded.toUtf8();
}

std::expected<QByteArray, TerminalTextCodecError> WindowsTerminalTextCodec::encodeRemote(const QByteArray &utf8) const
{
    if (m_encoding == TerminalEncoding::Utf8)
    {
        return utf8;
    }
    return encodeGb18030(utf8);
}

void WindowsTerminalTextCodec::reset() noexcept
{
    m_pending.clear();
}

} // namespace ztermy::terminal
