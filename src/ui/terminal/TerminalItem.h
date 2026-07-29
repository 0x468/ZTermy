#pragma once

#include "domain/terminal/TerminalEngine.h"

#include <QByteArray>
#include <QFont>
#include <QQuickItem>
#include <QString>

#include <cstdint>

class QInputMethodEvent;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;

namespace ztermy::ui
{

class TerminalItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)

public:
    explicit TerminalItem(QQuickItem *parent = nullptr);

    [[nodiscard]] QString statusText() const;

public slots:
    void setSnapshot(ztermy::terminal::TerminalSnapshotPtr snapshot);
    void setStatusText(const QString &status);
    void setClipboardText(const QString &text);
    void requestCurrentSize();

signals:
    void inputGenerated(const QByteArray &bytes);
    void pasteRequested(const QByteArray &bytes);
    void scrollRequested(int rows);
    void selectionRequested(quint16 startColumn, quint16 startRow, quint16 endColumn, quint16 endRow, bool rectangular);
    void clearSelectionRequested();
    void copyRequested();
    void sizeRequested(quint16 columns, quint16 rows, quint32 cellWidthPixels, quint32 cellHeightPixels);
    void statusTextChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void reportTerminalSize();
    [[nodiscard]] std::optional<ztermy::terminal::TerminalPoint> terminalPoint(const QPointF &position) const;
    [[nodiscard]] qreal cellWidth() const;
    [[nodiscard]] qreal cellHeight() const;

    ztermy::terminal::TerminalSnapshotPtr m_snapshot;
    QFont m_font;
    QString m_statusText = QStringLiteral("Starting local terminal...");
    std::uint64_t m_revision = 0;
    quint16 m_reportedColumns = 0;
    quint16 m_reportedRows = 0;
    ztermy::terminal::TerminalPoint m_selectionAnchor;
    bool m_selecting = false;
    bool m_selectionMoved = false;
    int m_wheelRemainder = 0;
};

} // namespace ztermy::ui
