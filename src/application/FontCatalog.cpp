#include "application/FontCatalog.h"

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QRawFont>

#include <algorithm>
#include <array>

namespace
{

[[nodiscard]] QFont fontForFamily(const QString &family)
{
    QFont font(family);
    font.setPixelSize(16);
    return font;
}

[[nodiscard]] bool containsOpenTypeLigatureFeature(const QByteArray &gsubTable)
{
    constexpr std::array featureTags{"liga", "clig", "calt", "dlig"};
    return std::ranges::any_of(featureTags, [&gsubTable](const char *feature) {
        return gsubTable.contains(feature);
    });
}

} // namespace

namespace ztermy
{

FontCatalog::FontCatalog(QObject *parent) : QObject(parent), m_allFamilies(QFontDatabase::families())
{
    m_allFamilies.removeDuplicates();
    m_allFamilies.sort(Qt::CaseInsensitive);
    m_monospacedFamilies.reserve(m_allFamilies.size());
    for (const QString &family : m_allFamilies)
    {
        if (QFontDatabase::isFixedPitch(family))
        {
            m_monospacedFamilies.append(family);
        }
    }

    m_systemUiFamily = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family().trimmed();
    if (m_systemUiFamily.isEmpty())
    {
        m_systemUiFamily = QGuiApplication::font().family().trimmed();
    }
}

const QStringList &FontCatalog::allFamilies() const noexcept
{
    return m_allFamilies;
}

const QStringList &FontCatalog::monospacedFamilies() const noexcept
{
    return m_monospacedFamilies;
}

const QString &FontCatalog::systemUiFamily() const noexcept
{
    return m_systemUiFamily;
}

QString FontCatalog::effectiveUiFamily(const QString &preference) const
{
    const QString normalized = preference.trimmed();
    return normalized.isEmpty() || !containsFamily(normalized) ? m_systemUiFamily : normalized;
}

bool FontCatalog::isMonospaced(const QString &family) const
{
    const QString normalized = family.trimmed();
    return !normalized.isEmpty() && QFontDatabase::isFixedPitch(normalized);
}

bool FontCatalog::supportsCjk(const QString &family) const
{
    const QString normalized = family.trimmed();
    if (normalized.isEmpty())
    {
        return true;
    }
    const QRawFont rawFont = QRawFont::fromFont(fontForFamily(normalized));
    if (!rawFont.isValid())
    {
        return false;
    }
    const QList<quint32> glyphs = rawFont.glyphIndexesForString(QStringLiteral("中文"));
    return glyphs.size() == 2 && std::ranges::all_of(glyphs, [](const quint32 glyph) {
               return glyph != 0;
           });
}

bool FontCatalog::supportsLigatures(const QString &family) const
{
    const QString normalized = family.trimmed();
    if (normalized.isEmpty())
    {
        return false;
    }
    const QRawFont rawFont = QRawFont::fromFont(fontForFamily(normalized));
    return rawFont.isValid() && containsOpenTypeLigatureFeature(rawFont.fontTable("GSUB"));
}

void FontCatalog::applyUiFont(const QString &preference) const
{
    const QString normalized = preference.trimmed();
    QFont font = normalized.isEmpty() || !containsFamily(normalized)
                     ? QFontDatabase::systemFont(QFontDatabase::GeneralFont)
                     : QFont(normalized);
    if (font.family().isEmpty())
    {
        font.setFamily(m_systemUiFamily);
    }
    QGuiApplication::setFont(font);
}

bool FontCatalog::containsFamily(const QString &family) const
{
    return std::ranges::any_of(m_allFamilies, [&family](const QString &candidate) {
        return candidate.compare(family, Qt::CaseInsensitive) == 0;
    });
}

} // namespace ztermy
