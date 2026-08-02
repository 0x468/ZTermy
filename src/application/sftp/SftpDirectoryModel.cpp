#include "application/sftp/SftpDirectoryModel.h"

#include <algorithm>
#include <utility>

namespace ztermy::sftp
{
namespace
{

int typeRank(const EntryType type) noexcept
{
    switch (type)
    {
        case EntryType::Directory:
            return 0;
        case EntryType::RegularFile:
            return 1;
        case EntryType::SymbolicLink:
            return 2;
        case EntryType::Other:
            return 3;
    }
    return 3;
}

} // namespace

SftpDirectoryModel::SftpDirectoryModel(QObject *parent) : QAbstractListModel(parent) {}

int SftpDirectoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_visibleRows.size());
}

QVariant SftpDirectoryModel::data(const QModelIndex &index, const int role) const
{
    const DirectoryEntry *item = visibleEntry(index.row());
    if (item == nullptr)
    {
        return {};
    }
    switch (role)
    {
        case Qt::DisplayRole:
        case NameRole:
            return qString(item->name);
        case RemotePathRole:
            return qString(item->remotePath);
        case TypeRole:
            return typeName(item->type);
        case SizeRole:
            return QVariant::fromValue<qulonglong>(item->size);
        case ModifiedUtcSecondsRole:
            return item->modifiedUtcSeconds ? QVariant::fromValue<qlonglong>(*item->modifiedUtcSeconds) : QVariant{};
        case PermissionsRole:
            return item->permissions;
        case HiddenRole:
            return hiddenRemoteName(item->name);
        case SelectedRole:
            return m_selectedPaths.contains(item->remotePath);
        default:
            return {};
    }
}

QHash<int, QByteArray> SftpDirectoryModel::roleNames() const
{
    return {
        {NameRole, QByteArrayLiteral("name")},
        {RemotePathRole, QByteArrayLiteral("remotePath")},
        {TypeRole, QByteArrayLiteral("entryType")},
        {SizeRole, QByteArrayLiteral("size")},
        {ModifiedUtcSecondsRole, QByteArrayLiteral("modifiedUtcSeconds")},
        {PermissionsRole, QByteArrayLiteral("permissions")},
        {HiddenRole, QByteArrayLiteral("hidden")},
        {SelectedRole, QByteArrayLiteral("selected")},
    };
}

bool SftpDirectoryModel::showHidden() const noexcept
{
    return m_showHidden;
}

void SftpDirectoryModel::setShowHidden(const bool show)
{
    if (m_showHidden == show)
    {
        return;
    }
    m_showHidden = show;
    rebuildVisibleRows();
    emit showHiddenChanged();
}

QString SftpDirectoryModel::filterText() const
{
    return m_filterText;
}

void SftpDirectoryModel::setFilterText(const QString &text)
{
    const QString normalized = text.trimmed();
    if (m_filterText == normalized)
    {
        return;
    }
    m_filterText = normalized;
    rebuildVisibleRows();
    emit filterTextChanged();
}

int SftpDirectoryModel::selectedCount() const noexcept
{
    return static_cast<int>(m_selectedPaths.size());
}

void SftpDirectoryModel::setEntries(const DirectoryListingPtr &entries, const QString &remotePath)
{
    beginResetModel();
    m_entries = entries ? *entries : DirectoryListing{};
    const QByteArray pathBytes = remotePath.toUtf8();
    const auto normalizedPath =
        normalizeRemotePath(std::string_view(pathBytes.constData(), static_cast<std::size_t>(pathBytes.size())));
    if (normalizedPath && *normalizedPath != "/")
    {
        m_entries.insert(m_entries.begin(), DirectoryEntry{
                                                .name = "..",
                                                .remotePath = parentRemotePath(*normalizedPath),
                                                .type = EntryType::Directory,
                                            });
    }
    std::ranges::sort(m_entries, [](const DirectoryEntry &left, const DirectoryEntry &right) {
        if (left.name == ".." || right.name == "..")
        {
            return left.name == ".." && right.name != "..";
        }
        const int leftRank = typeRank(left.type);
        const int rightRank = typeRank(right.type);
        if (leftRank != rightRank)
        {
            return leftRank < rightRank;
        }
        return QString::localeAwareCompare(qString(left.name), qString(right.name)) < 0;
    });
    m_selectedPaths.clear();
    m_visibleRows.clear();
    for (std::size_t index = 0; index < m_entries.size(); ++index)
    {
        if ((m_showHidden || !hiddenRemoteName(m_entries[index].name)) && matchesFilter(m_entries[index]))
        {
            m_visibleRows.push_back(index);
        }
    }
    endResetModel();
    emit selectionChanged();
}

QVariantMap SftpDirectoryModel::entry(const int row) const
{
    const DirectoryEntry *item = visibleEntry(row);
    if (item == nullptr)
    {
        return {};
    }
    return {
        {QStringLiteral("name"), qString(item->name)},
        {QStringLiteral("remotePath"), qString(item->remotePath)},
        {QStringLiteral("entryType"), typeName(item->type)},
        {QStringLiteral("size"), QVariant::fromValue<qulonglong>(item->size)},
        {QStringLiteral("modifiedUtcSeconds"),
         item->modifiedUtcSeconds ? QVariant::fromValue<qlonglong>(*item->modifiedUtcSeconds) : QVariant{}},
        {QStringLiteral("permissions"), item->permissions},
        {QStringLiteral("hidden"), hiddenRemoteName(item->name)},
        {QStringLiteral("selected"), m_selectedPaths.contains(item->remotePath)},
    };
}

bool SftpDirectoryModel::setSelected(const int row, const bool selected)
{
    const DirectoryEntry *item = visibleEntry(row);
    if (item == nullptr)
    {
        return false;
    }
    const bool changed =
        selected ? m_selectedPaths.insert(item->remotePath).second : m_selectedPaths.erase(item->remotePath) != 0;
    if (changed)
    {
        emit dataChanged(index(row), index(row), {SelectedRole});
        emit selectionChanged();
    }
    return changed;
}

void SftpDirectoryModel::clearSelection()
{
    if (m_selectedPaths.empty())
    {
        return;
    }
    m_selectedPaths.clear();
    if (!m_visibleRows.empty())
    {
        emit dataChanged(index(0), index(rowCount() - 1), {SelectedRole});
    }
    emit selectionChanged();
}

QStringList SftpDirectoryModel::selectedPaths() const
{
    QStringList result;
    result.reserve(static_cast<qsizetype>(m_selectedPaths.size()));
    for (const DirectoryEntry &item : m_entries)
    {
        if (m_selectedPaths.contains(item.remotePath))
        {
            result.push_back(qString(item.remotePath));
        }
    }
    return result;
}

void SftpDirectoryModel::rebuildVisibleRows()
{
    beginResetModel();
    m_visibleRows.clear();
    for (std::size_t index = 0; index < m_entries.size(); ++index)
    {
        if ((m_showHidden || !hiddenRemoteName(m_entries[index].name)) && matchesFilter(m_entries[index]))
        {
            m_visibleRows.push_back(index);
        }
    }
    endResetModel();
}

const DirectoryEntry *SftpDirectoryModel::visibleEntry(const int row) const noexcept
{
    if (row < 0 || static_cast<std::size_t>(row) >= m_visibleRows.size())
    {
        return nullptr;
    }
    return &m_entries[m_visibleRows[static_cast<std::size_t>(row)]];
}

bool SftpDirectoryModel::matchesFilter(const DirectoryEntry &item) const
{
    return item.name == ".." || m_filterText.isEmpty()
           || qString(item.name).contains(m_filterText, Qt::CaseInsensitive);
}

QString SftpDirectoryModel::qString(const std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString SftpDirectoryModel::typeName(const EntryType type)
{
    switch (type)
    {
        case EntryType::RegularFile:
            return QStringLiteral("file");
        case EntryType::Directory:
            return QStringLiteral("directory");
        case EntryType::SymbolicLink:
            return QStringLiteral("symlink");
        case EntryType::Other:
            return QStringLiteral("other");
    }
    return QStringLiteral("other");
}

} // namespace ztermy::sftp
