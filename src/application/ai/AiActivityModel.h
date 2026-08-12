#pragma once

#include <QAbstractListModel>
#include <QThreadPool>

#include <cstdint>
#include <vector>

namespace ztermy::ai
{

struct AiActivityEvent final
{
    QString conversationId;
    QString toolCallId;
    QString toolName;
    QString state;
    QString resultCode;
    QString permissionMode;
    std::uint64_t sessionGeneration = 0;
    bool sideEffecting = false;
    bool highRisk = false;
};

class AiActivityModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString persistenceError READ persistenceError CONSTANT)

public:
    enum Role : int // NOLINT(performance-enum-size) Qt model roles are int.
    {
        ActivityIdRole = Qt::UserRole + 1,
        TimestampRole,
        ConversationReferenceRole,
        ToolCallReferenceRole,
        ToolNameRole,
        StateRole,
        ResultCodeRole,
        PermissionModeRole,
        SessionGenerationRole,
        SideEffectingRole,
        HighRiskRole,
    };

    explicit AiActivityModel(QString filePath, QObject *parent = nullptr);
    ~AiActivityModel() override;

    AiActivityModel(const AiActivityModel &) = delete;
    AiActivityModel &operator=(const AiActivityModel &) = delete;

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] QString persistenceError() const;
    [[nodiscard]] QString filePath() const;

    void record(const AiActivityEvent &event);
    Q_INVOKABLE void clear();
    Q_INVOKABLE [[nodiscard]] bool exportTo(const QString &destinationPath) const;

signals:
    void countChanged();

private:
    struct Record final
    {
        QString activityId;
        QString timestampUtc;
        QString conversationReference;
        QString toolCallReference;
        QString toolName;
        QString state;
        QString resultCode;
        QString permissionMode;
        std::uint64_t sessionGeneration = 0;
        bool sideEffecting = false;
        bool highRisk = false;
    };

    [[nodiscard]] static QString referenceFor(const QString &scope, const QString &value);
    [[nodiscard]] static bool validToken(const QString &value, qsizetype maximumLength);
    [[nodiscard]] static std::vector<Record> load(const QString &filePath, QString &error);
    [[nodiscard]] static bool save(const QString &filePath, const std::vector<Record> &records);
    void scheduleSave();
    static constexpr std::size_t maximumRecords = 500;
    QString m_filePath;
    QString m_persistenceError;
    std::vector<Record> m_records;
    QThreadPool m_writerPool;
};

} // namespace ztermy::ai
