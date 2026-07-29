#pragma once

#include <QByteArray>

#include <cstddef>
#include <string_view>
#include <utility>

namespace ztermy::security
{

class SensitiveByteArray final
{
public:
    SensitiveByteArray() = default;
    explicit SensitiveByteArray(QByteArray &&bytes) noexcept : m_bytes(std::move(bytes)) {}

    ~SensitiveByteArray() { clear(); }

    SensitiveByteArray(const SensitiveByteArray &) = delete;
    SensitiveByteArray &operator=(const SensitiveByteArray &) = delete;

    SensitiveByteArray(SensitiveByteArray &&other) noexcept : m_bytes(std::move(other.m_bytes)) {}

    SensitiveByteArray &operator=(SensitiveByteArray &&other) noexcept
    {
        if (this != &other)
        {
            clear();
            m_bytes = std::move(other.m_bytes);
        }
        return *this;
    }

    [[nodiscard]] bool empty() const noexcept { return m_bytes.isEmpty(); }

    [[nodiscard]] std::string_view view() const noexcept
    {
        if (m_bytes.isEmpty())
        {
            return {};
        }
        return {m_bytes.constData(), static_cast<std::size_t>(m_bytes.size())};
    }

    void clear() noexcept
    {
        volatile char *bytes = m_bytes.data();
        for (qsizetype index = 0; index < m_bytes.size(); ++index)
        {
            bytes[index] = '\0';
        }
        m_bytes.clear();
        m_bytes.squeeze();
    }

private:
    QByteArray m_bytes;
};

} // namespace ztermy::security
