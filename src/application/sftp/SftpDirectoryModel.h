#pragma once

#include "application/sftp/SftpSession.h"

#include <QAbstractListModel>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ztermy::sftp
{

class SftpDirectoryModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(QString viewMode READ viewMode WRITE setViewMode NOTIFY viewModeChanged)

public:
    enum Role : std::uint16_t
    {
        NameRole = Qt::UserRole + 1,
        RemotePathRole,
        TypeRole,
        SizeRole,
        ModifiedUtcSecondsRole,
        PermissionsRole,
        HiddenRole,
        SelectedRole,
        DepthRole,
        ExpandedRole,
        ExpandableRole,
        LoadingRole,
        LoadErrorRole,
    };

    explicit SftpDirectoryModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] bool showHidden() const noexcept;
    void setShowHidden(bool show);
    [[nodiscard]] QString filterText() const;
    void setFilterText(const QString &text);
    [[nodiscard]] int selectedCount() const noexcept;
    [[nodiscard]] QString viewMode() const;
    void setViewMode(const QString &mode);

    void setEntries(const DirectoryListingPtr &entries, const QString &remotePath = {});
    [[nodiscard]] Q_INVOKABLE QVariantMap entry(int row) const;
    Q_INVOKABLE bool setSelected(int row, bool selected);
    Q_INVOKABLE void clearSelection();
    [[nodiscard]] Q_INVOKABLE QStringList selectedPaths() const;
    Q_INVOKABLE bool toggleExpanded(int row);
    void applyTreeEntries(const QString &remotePath, const DirectoryListingPtr &entries);
    void applyTreeError(const QString &remotePath, const QString &message);

signals:
    void showHiddenChanged();
    void filterTextChanged();
    void selectionChanged();
    void viewModeChanged();
    void treeDirectoryRequested(const QString &remotePath);

private:
    void rebuildVisibleRows();
    [[nodiscard]] const DirectoryEntry *visibleEntry(int row) const noexcept;
    void appendTreeRows(const DirectoryListing &entries, int depth, std::unordered_set<std::string> &ancestors);
    static void sortEntries(DirectoryListing &entries);
    [[nodiscard]] bool matchesFilter(const DirectoryEntry &entry) const;
    [[nodiscard]] static QString qString(std::string_view text);
    [[nodiscard]] static QString typeName(EntryType type);

    DirectoryListing m_entries;
    std::vector<std::size_t> m_visibleRows;
    struct TreeState final
    {
        DirectoryListing children;
        QString error;
        bool loaded = false;
        bool expanded = false;
        bool loading = false;
    };
    struct TreeRow final
    {
        DirectoryEntry entry;
        QString error;
        int depth = 0;
        bool expanded = false;
        bool loading = false;
    };
    std::unordered_map<std::string, TreeState> m_treeStates;
    std::vector<TreeRow> m_treeRows;
    std::unordered_set<std::string> m_selectedPaths;
    QString m_filterText;
    bool m_treeMode = false;
    bool m_showHidden = false;
};

} // namespace ztermy::sftp
