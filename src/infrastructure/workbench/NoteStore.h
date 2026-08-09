#pragma once

#include <QString>

#include <cstdint>
#include <expected>
#include <vector>

namespace ztermy::workbench
{

inline constexpr std::size_t maximumNoteCount = 1'000;
inline constexpr std::size_t maximumNoteBytes = std::size_t{2} * 1024 * 1024;
inline constexpr std::size_t maximumIndexedNoteBytes = std::size_t{32} * 1024 * 1024;
inline constexpr std::size_t maximumNoteDepth = 16;
inline constexpr std::size_t maximumNoteSearchResults = 200;

enum class NoteStoreError : std::uint8_t
{
    invalidPath,
    unsafeLink,
    notFound,
    alreadyExists,
    unsupportedType,
    invalidUtf8,
    noteTooLarge,
    repositoryLimit,
    io,
};

struct NoteEntry final
{
    QString path;
    QString name;
    std::uint64_t size = 0;
    std::int64_t modifiedUtcMs = 0;
    bool folder = false;

    bool operator==(const NoteEntry &) const = default;
};

struct NoteSearchResult final
{
    QString path;
    QString title;
    QString snippet;

    bool operator==(const NoteSearchResult &) const = default;
};

class NoteStore final
{
public:
    explicit NoteStore(const QString &rootPath);

    [[nodiscard]] const QString &rootPath() const noexcept;
    [[nodiscard]] std::expected<std::vector<NoteEntry>, NoteStoreError> entries() const;
    [[nodiscard]] std::expected<QString, NoteStoreError> read(const QString &relativePath) const;
    [[nodiscard]] std::expected<void, NoteStoreError> save(const QString &relativePath, const QString &content) const;
    [[nodiscard]] std::expected<void, NoteStoreError> createFolder(const QString &relativePath) const;
    [[nodiscard]] std::expected<void, NoteStoreError> renameEntry(const QString &sourceRelativePath,
                                                                  const QString &destinationRelativePath) const;
    [[nodiscard]] std::expected<void, NoteStoreError> removeEntry(const QString &relativePath) const;
    [[nodiscard]] std::expected<QString, NoteStoreError> importNote(const QString &sourceFilePath,
                                                                    const QString &destinationFolder) const;
    [[nodiscard]] std::expected<void, NoteStoreError> exportNote(const QString &relativePath,
                                                                 const QString &destinationFilePath) const;
    [[nodiscard]] std::expected<std::vector<NoteSearchResult>, NoteStoreError> search(const QString &query) const;

private:
    [[nodiscard]] std::expected<QString, NoteStoreError> resolvedPath(const QString &relativePath,
                                                                      bool requireMarkdown) const;
    [[nodiscard]] std::expected<void, NoteStoreError> ensureRoot() const;

    QString m_rootPath;
};

} // namespace ztermy::workbench
