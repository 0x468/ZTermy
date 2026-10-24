#include "ui/TerminalTabListModel.h"

#include <QSet>

namespace ztermy::ui
{
int TerminalTabListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant TerminalTabListModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid() || index.column() != 0 || index.row() < 0 || index.row() >= m_rows.size() || role != ModelData)
        return {};
    return m_rows[index.row()];
}

QHash<int, QByteArray> TerminalTabListModel::roleNames() const
{
    return {{ModelData, "modelData"}};
}

QVariantList TerminalTabListModel::tabs() const
{
    QVariantList result;
    result.reserve(m_rows.size());
    for (const auto &row : m_rows)
        result.append(row);
    return result;
}

void TerminalTabListModel::setTabs(const QVariantList &tabs)
{
    const auto idOf = [](const QVariantMap &row) {
        return row.value(QStringLiteral("id")).toString();
    };
    QSet<QString> ids;
    QList<QVariantMap> next;
    for (const auto &value : tabs)
    {
        const auto row = value.toMap();
        const auto id = idOf(row);
        if (id.isEmpty() || ids.contains(id))
            return;
        ids.insert(id);
        next.append(row);
    }
    if (next == m_rows)
        return;
    for (int row = rowCount() - 1; row >= 0; --row)
    {
        if (!ids.contains(idOf(m_rows[row])))
        {
            beginRemoveRows({}, row, row);
            m_rows.removeAt(row);
            endRemoveRows();
        }
    }
    for (int row = 0; row < next.size(); ++row)
    {
        const auto id = idOf(next[row]);
        int found = row;
        while (found < rowCount() && idOf(m_rows[found]) != id)
            ++found;
        if (found == rowCount())
        {
            beginInsertRows({}, row, row);
            m_rows.insert(row, next[row]);
            endInsertRows();
        }
        else
        {
            if (found != row)
            {
                beginMoveRows({}, found, found, {}, row);
                m_rows.move(found, row);
                endMoveRows();
            }
            if (m_rows[row] != next[row])
            {
                m_rows[row] = next[row];
                emit dataChanged(index(row), index(row), {ModelData});
            }
        }
    }
    emit tabsChanged();
}
} // namespace ztermy::ui
