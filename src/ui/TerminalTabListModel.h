#pragma once

#include <QAbstractListModel>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace ztermy::ui
{
// Stable UI projection: status changes update roles, never replace tab delegates.
class TerminalTabListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList tabs READ tabs WRITE setTabs NOTIFY tabsChanged)

public:
    explicit TerminalTabListModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    static constexpr int ModelData = Qt::UserRole + 1;
    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] QVariantList tabs() const;
    void setTabs(const QVariantList &tabs);

signals:
    void tabsChanged();

private:
    QList<QVariantMap> m_rows;
};
} // namespace ztermy::ui
