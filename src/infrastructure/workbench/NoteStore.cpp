#include "infrastructure/workbench/NoteStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringDecoder>

#include <algorithm>

namespace
{

[[nodiscard]] bool reservedWindowsName(const QString &segment)
{
    const QString base = segment.section(QLatin1Char('.'), 0, 0).toUpper();
    if (base == QStringLiteral("CON") || base == QStringLiteral("PRN") || base == QStringLiteral("AUX")
        || base == QStringLiteral("NUL"))
    {
        return true;
    }
    if (base.size() == 4 && (base.startsWith(QStringLiteral("COM")) || base.startsWith(QStringLiteral("LPT"))))
    {
        return base.back() >= QLatin1Char('1') && base.back() <= QLatin1Char('9');
    }
    return false;
}

[[nodiscard]] bool validRelativePath(const QString &path, const bool requireMarkdown)
{
    if (path.isEmpty() || path.size() > 1024 || QDir::isAbsolutePath(path) || path.contains(QLatin1Char('\\'))
        || path.contains(QStringLiteral("//")))
    {
        return false;
    }
    const QStringList segments = path.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    if (segments.empty() || segments.size() > static_cast<qsizetype>(ztermy::workbench::maximumNoteDepth))
    {
        return false;
    }
    for (const QString &segment : segments)
    {
        if (segment.isEmpty() || segment == QStringLiteral(".") || segment == QStringLiteral("..")
            || segment.size() > 255 || segment.endsWith(QLatin1Char('.')) || segment.endsWith(QLatin1Char(' '))
            || segment.contains(QRegularExpression(QStringLiteral(R"([<>:"|?*\x00-\x1F])")))
            || reservedWindowsName(segment))
        {
            return false;
        }
    }
    return !requireMarkdown || segments.back().endsWith(QStringLiteral(".md"), Qt::CaseInsensitive);
}

[[nodiscard]] bool insideRoot(const QString &root, const QString &candidate)
{
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    return candidate.compare(root, sensitivity) == 0 || candidate.startsWith(root + QLatin1Char('/'), sensitivity);
}

[[nodiscard]] bool unsafeLink(const QFileInfo &info)
{
    return info.isSymLink() || info.isJunction();
}

struct RepositoryTotals final
{
    std::size_t notes = 0;
    std::size_t bytes = 0;
};

[[nodiscard]] std::expected<void, ztermy::workbench::NoteStoreError>
collectEntries(const QString &absoluteDirectory, const QString &relativeDirectory, const std::size_t depth,
               std::vector<ztermy::workbench::NoteEntry> &result, RepositoryTotals &totals)
{
    if (depth > ztermy::workbench::maximumNoteDepth)
    {
        return std::unexpected(ztermy::workbench::NoteStoreError::repositoryLimit);
    }
    const QFileInfoList children =
        QDir(absoluteDirectory)
            .entryInfoList(QDir::NoDotAndDotDot | QDir::AllDirs | QDir::Files, QDir::DirsFirst | QDir::Name);
    for (const QFileInfo &child : children)
    {
        if (unsafeLink(child))
        {
            return std::unexpected(ztermy::workbench::NoteStoreError::unsafeLink);
        }
        const QString relativePath =
            relativeDirectory.isEmpty() ? child.fileName() : relativeDirectory + QLatin1Char('/') + child.fileName();
        if (child.isDir())
        {
            if (!validRelativePath(relativePath, false))
            {
                return std::unexpected(ztermy::workbench::NoteStoreError::invalidPath);
            }
            result.push_back({.path = relativePath,
                              .name = child.fileName(),
                              .modifiedUtcMs = child.lastModified().toUTC().toMSecsSinceEpoch(),
                              .folder = true});
            auto collected = collectEntries(child.absoluteFilePath(), relativePath, depth + 1, result, totals);
            if (!collected)
            {
                return collected;
            }
            continue;
        }
        if (!child.fileName().endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
        {
            continue;
        }
        if (!validRelativePath(relativePath, true) || child.size() < 0
            || child.size() > static_cast<qint64>(ztermy::workbench::maximumNoteBytes))
        {
            return std::unexpected(child.size() > static_cast<qint64>(ztermy::workbench::maximumNoteBytes)
                                       ? ztermy::workbench::NoteStoreError::noteTooLarge
                                       : ztermy::workbench::NoteStoreError::invalidPath);
        }
        ++totals.notes;
        totals.bytes += static_cast<std::size_t>(child.size());
        if (totals.notes > ztermy::workbench::maximumNoteCount
            || totals.bytes > ztermy::workbench::maximumIndexedNoteBytes)
        {
            return std::unexpected(ztermy::workbench::NoteStoreError::repositoryLimit);
        }
        result.push_back({.path = relativePath,
                          .name = child.completeBaseName(),
                          .size = static_cast<std::uint64_t>(child.size()),
                          .modifiedUtcMs = child.lastModified().toUTC().toMSecsSinceEpoch(),
                          .folder = false});
    }
    return {};
}

[[nodiscard]] std::expected<QString, ztermy::workbench::NoteStoreError> decodeUtf8(const QByteArray &bytes)
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString decoded = decoder.decode(bytes);
    if (decoder.hasError())
    {
        return std::unexpected(ztermy::workbench::NoteStoreError::invalidUtf8);
    }
    if (!decoded.isEmpty() && decoded.front() == QChar::ByteOrderMark)
    {
        decoded.removeFirst();
    }
    return decoded;
}

} // namespace

namespace ztermy::workbench
{

NoteStore::NoteStore(const QString &rootPath)
    : m_rootPath(QDir::fromNativeSeparators(QFileInfo(rootPath).absoluteFilePath()))
{
    m_rootPath = QDir::cleanPath(m_rootPath);
}

const QString &NoteStore::rootPath() const noexcept
{
    return m_rootPath;
}

std::expected<void, NoteStoreError> NoteStore::ensureRoot() const
{
    const QFileInfo existing(m_rootPath);
    if (existing.exists())
    {
        return existing.isDir() && !unsafeLink(existing) ? std::expected<void, NoteStoreError>{}
                                                         : std::unexpected(NoteStoreError::unsafeLink);
    }
    return QDir().mkpath(m_rootPath) ? std::expected<void, NoteStoreError>{} : std::unexpected(NoteStoreError::io);
}

std::expected<QString, NoteStoreError> NoteStore::resolvedPath(const QString &relativePath,
                                                               const bool requireMarkdown) const
{
    if (!validRelativePath(relativePath, requireMarkdown))
    {
        return std::unexpected(requireMarkdown && !relativePath.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)
                                   ? NoteStoreError::unsupportedType
                                   : NoteStoreError::invalidPath);
    }
    if (auto root = ensureRoot(); !root)
    {
        return std::unexpected(root.error());
    }
    QString candidate = QDir::cleanPath(QDir::fromNativeSeparators(QDir(m_rootPath).absoluteFilePath(relativePath)));
    if (!insideRoot(m_rootPath, candidate))
    {
        return std::unexpected(NoteStoreError::invalidPath);
    }
    QString current = m_rootPath;
    for (const QString &segment : relativePath.split(QLatin1Char('/')))
    {
        current = QDir(current).filePath(segment);
        const QFileInfo info(current);
        if (info.exists() && unsafeLink(info))
        {
            return std::unexpected(NoteStoreError::unsafeLink);
        }
    }
    return candidate;
}

std::expected<std::vector<NoteEntry>, NoteStoreError> NoteStore::entries() const
{
    if (auto root = ensureRoot(); !root)
    {
        return std::unexpected(root.error());
    }
    std::vector<NoteEntry> result;
    RepositoryTotals totals;
    if (auto collected = collectEntries(m_rootPath, {}, 1, result, totals); !collected)
    {
        return std::unexpected(collected.error());
    }
    return result;
}

std::expected<QString, NoteStoreError> NoteStore::read(const QString &relativePath) const
{
    auto path = resolvedPath(relativePath, true);
    if (!path)
    {
        return std::unexpected(path.error());
    }
    const QFileInfo info(*path);
    if (!info.isFile())
    {
        return std::unexpected(NoteStoreError::notFound);
    }
    if (info.size() < 0 || info.size() > static_cast<qint64>(maximumNoteBytes))
    {
        return std::unexpected(NoteStoreError::noteTooLarge);
    }
    QFile file(*path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return std::unexpected(NoteStoreError::io);
    }
    return decodeUtf8(file.readAll());
}

std::expected<void, NoteStoreError> NoteStore::save(const QString &relativePath, const QString &content) const
{
    auto path = resolvedPath(relativePath, true);
    if (!path)
    {
        return std::unexpected(path.error());
    }
    const QByteArray encoded = content.toUtf8();
    if (encoded.size() > static_cast<qsizetype>(maximumNoteBytes))
    {
        return std::unexpected(NoteStoreError::noteTooLarge);
    }
    const QFileInfo target(*path);
    if (!QFileInfo(target.absolutePath()).isDir() || target.isDir())
    {
        return std::unexpected(target.isDir() ? NoteStoreError::unsupportedType : NoteStoreError::notFound);
    }
    auto listed = entries();
    if (!listed)
    {
        return std::unexpected(listed.error());
    }
    std::size_t noteCount = 0;
    std::size_t totalBytes = 0;
    for (const NoteEntry &entry : *listed)
    {
        if (!entry.folder)
        {
            ++noteCount;
            totalBytes += static_cast<std::size_t>(entry.size);
        }
    }
    if (target.exists())
    {
        totalBytes -= static_cast<std::size_t>(target.size());
    }
    else
    {
        ++noteCount;
    }
    totalBytes += static_cast<std::size_t>(encoded.size());
    if (noteCount > maximumNoteCount || totalBytes > maximumIndexedNoteBytes)
    {
        return std::unexpected(NoteStoreError::repositoryLimit);
    }
    QSaveFile file(*path);
    if (!file.open(QIODevice::WriteOnly) || file.write(encoded) != encoded.size() || !file.commit())
    {
        return std::unexpected(NoteStoreError::io);
    }
    return {};
}

std::expected<void, NoteStoreError> NoteStore::createFolder(const QString &relativePath) const
{
    auto path = resolvedPath(relativePath, false);
    if (!path)
    {
        return std::unexpected(path.error());
    }
    if (QFileInfo::exists(*path))
    {
        return std::unexpected(NoteStoreError::alreadyExists);
    }
    if (!QFileInfo(QFileInfo(*path).absolutePath()).isDir())
    {
        return std::unexpected(NoteStoreError::notFound);
    }
    return QDir().mkdir(*path) ? std::expected<void, NoteStoreError>{} : std::unexpected(NoteStoreError::io);
}

std::expected<void, NoteStoreError> NoteStore::renameEntry(const QString &sourceRelativePath,
                                                           const QString &destinationRelativePath) const
{
    auto source = resolvedPath(sourceRelativePath, false);
    if (!source)
    {
        return std::unexpected(source.error());
    }
    const QFileInfo sourceInfo(*source);
    if (!sourceInfo.exists())
    {
        return std::unexpected(NoteStoreError::notFound);
    }
    const bool note = sourceInfo.isFile();
    if (note && !sourceRelativePath.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
    {
        return std::unexpected(NoteStoreError::unsupportedType);
    }
    auto destination = resolvedPath(destinationRelativePath, note);
    if (!destination)
    {
        return std::unexpected(destination.error());
    }
    if (QFileInfo::exists(*destination))
    {
        return std::unexpected(NoteStoreError::alreadyExists);
    }
    if (!QFileInfo(QFileInfo(*destination).absolutePath()).isDir())
    {
        return std::unexpected(NoteStoreError::notFound);
    }
    return QDir().rename(*source, *destination) ? std::expected<void, NoteStoreError>{}
                                                : std::unexpected(NoteStoreError::io);
}

std::expected<void, NoteStoreError> NoteStore::removeEntry(const QString &relativePath) const
{
    auto path = resolvedPath(relativePath, false);
    if (!path)
    {
        return std::unexpected(path.error());
    }
    const QFileInfo info(*path);
    if (!info.exists())
    {
        return std::unexpected(NoteStoreError::notFound);
    }
    const bool removed = info.isDir() ? QDir(*path).removeRecursively() : QFile::remove(*path);
    return removed ? std::expected<void, NoteStoreError>{} : std::unexpected(NoteStoreError::io);
}

std::expected<QString, NoteStoreError> NoteStore::importNote(const QString &sourceFilePath,
                                                             const QString &destinationFolder) const
{
    const QFileInfo source(sourceFilePath);
    if (!source.isFile() || !source.fileName().endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
    {
        return std::unexpected(NoteStoreError::unsupportedType);
    }
    QFile file(sourceFilePath);
    if (!file.open(QIODevice::ReadOnly) || file.size() > static_cast<qint64>(maximumNoteBytes))
    {
        return std::unexpected(file.size() > static_cast<qint64>(maximumNoteBytes) ? NoteStoreError::noteTooLarge
                                                                                   : NoteStoreError::io);
    }
    auto content = decodeUtf8(file.readAll());
    if (!content)
    {
        return std::unexpected(content.error());
    }
    QString destination =
        destinationFolder.isEmpty() ? source.fileName() : destinationFolder + QLatin1Char('/') + source.fileName();
    if (auto saved = save(destination, *content); !saved)
    {
        return std::unexpected(saved.error());
    }
    return destination;
}

std::expected<void, NoteStoreError> NoteStore::exportNote(const QString &relativePath,
                                                          const QString &destinationFilePath) const
{
    auto content = read(relativePath);
    if (!content)
    {
        return std::unexpected(content.error());
    }
    QSaveFile file(destinationFilePath);
    const QByteArray encoded = content->toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(encoded) != encoded.size() || !file.commit())
    {
        return std::unexpected(NoteStoreError::io);
    }
    return {};
}

std::expected<std::vector<NoteSearchResult>, NoteStoreError> NoteStore::search(const QString &query) const
{
    const QString needle = query.trimmed();
    if (needle.isEmpty() || needle.size() > 256)
    {
        return needle.isEmpty()
                   ? std::expected<std::vector<NoteSearchResult>, NoteStoreError>{std::vector<NoteSearchResult>{}}
                   : std::unexpected(NoteStoreError::invalidPath);
    }
    auto listed = entries();
    if (!listed)
    {
        return std::unexpected(listed.error());
    }
    std::vector<NoteSearchResult> result;
    result.reserve((std::min)(maximumNoteSearchResults, listed->size()));
    for (const NoteEntry &entry : *listed)
    {
        if (entry.folder)
        {
            continue;
        }
        auto content = read(entry.path);
        if (!content)
        {
            return std::unexpected(content.error());
        }
        const qsizetype match = content->indexOf(needle, 0, Qt::CaseInsensitive);
        const bool titleMatch = entry.name.contains(needle, Qt::CaseInsensitive);
        if (match < 0 && !titleMatch)
        {
            continue;
        }
        const qsizetype start = match < 0 ? 0 : (std::max)(qsizetype{0}, match - 80);
        result.push_back({.path = entry.path, .title = entry.name, .snippet = content->mid(start, 240).simplified()});
        if (result.size() == maximumNoteSearchResults)
        {
            break;
        }
    }
    return result;
}

} // namespace ztermy::workbench
