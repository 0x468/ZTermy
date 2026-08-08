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
    return parent.isValid() ? 0 : static_cast<int>(m_treeMode ? m_treeRows.size() : m_visibleRows.size());
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
        case DepthRole:
            return m_treeMode && index.row() >= 0 && static_cast<std::size_t>(index.row()) < m_treeRows.size()
                       ? m_treeRows[static_cast<std::size_t>(index.row())].depth
                       : 0;
        case ExpandedRole:
            return m_treeMode && index.row() >= 0 && static_cast<std::size_t>(index.row()) < m_treeRows.size()
                   && m_treeRows[static_cast<std::size_t>(index.row())].expanded;
        case ExpandableRole:
            return m_treeMode && item->type == EntryType::Directory && item->name != "..";
        case LoadingRole:
            return m_treeMode && index.row() >= 0 && static_cast<std::size_t>(index.row()) < m_treeRows.size()
                   && m_treeRows[static_cast<std::size_t>(index.row())].loading;
        case LoadErrorRole:
            return m_treeMode && index.row() >= 0 && static_cast<std::size_t>(index.row()) < m_treeRows.size()
                       ? m_treeRows[static_cast<std::size_t>(index.row())].error
                       : QString{};
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
        {DepthRole, QByteArrayLiteral("depth")},
        {ExpandedRole, QByteArrayLiteral("expanded")},
        {ExpandableRole, QByteArrayLiteral("expandable")},
        {LoadingRole, QByteArrayLiteral("loading")},
        {LoadErrorRole, QByteArrayLiteral("loadError")},
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

QString SftpDirectoryModel::viewMode() const
{
    return m_treeMode ? QStringLiteral("tree") : QStringLiteral("list");
}

void SftpDirectoryModel::setViewMode(const QString &mode)
{
    const bool tree = mode == QStringLiteral("tree");
    if (m_treeMode == tree)
    {
        return;
    }
    m_treeMode = tree;
    rebuildVisibleRows();
    emit viewModeChanged();
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
    sortEntries(m_entries);
    m_treeStates.clear();
    m_treeRows.clear();
    m_selectedPaths.clear();
    m_visibleRows.clear();
    if (m_treeMode)
    {
        std::unordered_set<std::string> ancestors;
        appendTreeRows(m_entries, 0, ancestors);
    }
    else
    {
        for (std::size_t index = 0; index < m_entries.size(); ++index)
        {
            if ((m_showHidden || !hiddenRemoteName(m_entries[index].name)) && matchesFilter(m_entries[index]))
            {
                m_visibleRows.push_back(index);
            }
        }
    }
    endResetModel();
    emit selectionChanged();
}

void SftpDirectoryModel::sortEntries(DirectoryListing &entries)
{
    std::ranges::sort(entries, [](const DirectoryEntry &left, const DirectoryEntry &right) {
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
        {QStringLiteral("depth"), data(index(row), DepthRole)},
        {QStringLiteral("expanded"), data(index(row), ExpandedRole)},
        {QStringLiteral("expandable"), data(index(row), ExpandableRole)},
        {QStringLiteral("loading"), data(index(row), LoadingRole)},
        {QStringLiteral("loadError"), data(index(row), LoadErrorRole)},
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
    const auto appendSelected = [this, &result](const DirectoryEntry &item) {
        if (m_selectedPaths.contains(item.remotePath) && !result.contains(qString(item.remotePath)))
        {
            result.push_back(qString(item.remotePath));
        }
    };
    for (const DirectoryEntry &item : m_entries)
    {
        appendSelected(item);
    }
    for (const TreeRow &row : m_treeRows)
    {
        appendSelected(row.entry);
    }
    return result;
}

bool SftpDirectoryModel::toggleExpanded(const int row)
{
    if (!m_treeMode || row < 0 || static_cast<std::size_t>(row) >= m_treeRows.size())
    {
        return false;
    }
    const DirectoryEntry &entry = m_treeRows[static_cast<std::size_t>(row)].entry;
    if (entry.type != EntryType::Directory || entry.name == "..")
    {
        return false;
    }
    TreeState &state = m_treeStates[entry.remotePath];
    state.expanded = !state.expanded;
    if (state.expanded && !state.loaded && !state.loading)
    {
        state.loading = true;
        state.error.clear();
        rebuildVisibleRows();
        emit treeDirectoryRequested(qString(entry.remotePath));
        return true;
    }
    rebuildVisibleRows();
    return true;
}

void SftpDirectoryModel::applyTreeEntries(const QString &remotePath, const DirectoryListingPtr &entries)
{
    const QByteArray encoded = remotePath.toUtf8();
    TreeState &state = m_treeStates[std::string(encoded.constData(), static_cast<std::size_t>(encoded.size()))];
    state.children = entries ? *entries : DirectoryListing{};
    std::erase_if(state.children, [](const DirectoryEntry &entry) {
        return entry.name == "." || entry.name == "..";
    });
    sortEntries(state.children);
    state.loaded = true;
    state.loading = false;
    state.expanded = true;
    state.error.clear();
    rebuildVisibleRows();
}

void SftpDirectoryModel::applyTreeError(const QString &remotePath, const QString &message)
{
    const QByteArray encoded = remotePath.toUtf8();
    TreeState &state = m_treeStates[std::string(encoded.constData(), static_cast<std::size_t>(encoded.size()))];
    state.loading = false;
    state.loaded = false;
    state.expanded = true;
    state.error = message;
    rebuildVisibleRows();
}

void SftpDirectoryModel::rebuildVisibleRows()
{
    beginResetModel();
    m_visibleRows.clear();
    m_treeRows.clear();
    if (m_treeMode)
    {
        std::unordered_set<std::string> ancestors;
        appendTreeRows(m_entries, 0, ancestors);
        endResetModel();
        return;
    }
    for (std::size_t index = 0; index < m_entries.size(); ++index)
    {
        if ((m_showHidden || !hiddenRemoteName(m_entries[index].name)) && matchesFilter(m_entries[index]))
        {
            m_visibleRows.push_back(index);
        }
    }
    endResetModel();
}

void SftpDirectoryModel::appendTreeRows(const DirectoryListing &entries, const int depth,
                                        std::unordered_set<std::string> &ancestors)
{
    constexpr int maximumDepth = 64;
    for (const DirectoryEntry &entry : entries)
    {
        if ((!m_showHidden && hiddenRemoteName(entry.name)) || !matchesFilter(entry))
        {
            continue;
        }
        const auto state = m_treeStates.find(entry.remotePath);
        const bool expanded = state != m_treeStates.end() && state->second.expanded;
        const bool loading = state != m_treeStates.end() && state->second.loading;
        const QString error = state != m_treeStates.end() ? state->second.error : QString{};
        m_treeRows.push_back(
            TreeRow{.entry = entry, .error = error, .depth = depth, .expanded = expanded, .loading = loading});
        if (!expanded || state == m_treeStates.end() || !state->second.loaded || depth >= maximumDepth
            || !ancestors.insert(entry.remotePath).second)
        {
            continue;
        }
        appendTreeRows(state->second.children, depth + 1, ancestors);
        ancestors.erase(entry.remotePath);
    }
}

const DirectoryEntry *SftpDirectoryModel::visibleEntry(const int row) const noexcept
{
    if (m_treeMode)
    {
        return row >= 0 && static_cast<std::size_t>(row) < m_treeRows.size()
                   ? &m_treeRows[static_cast<std::size_t>(row)].entry
                   : nullptr;
    }
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
