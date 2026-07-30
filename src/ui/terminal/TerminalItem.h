#pragma once

#include "domain/terminal/TerminalEngine.h"

#include <QByteArray>
#include <QFont>
#include <QQuickItem>
#include <QString>
#include <QTimer>
#include <QtQmlIntegration/qqmlintegration.h>

#include <cstdint>

class QInputMethodEvent;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;
class QFocusEvent;

namespace ztermy::ui
{

class TerminalItem : public QQuickItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(TerminalView)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontChanged)
    Q_PROPERTY(int fontPixelSize READ fontPixelSize WRITE setFontPixelSize NOTIFY fontChanged)
    Q_PROPERTY(QString cursorPreference READ cursorPreference WRITE setCursorPreference NOTIFY cursorAppearanceChanged)
    Q_PROPERTY(bool cursorBlink READ cursorBlink WRITE setCursorBlink NOTIFY cursorAppearanceChanged)
    Q_PROPERTY(bool copyOnSelect READ copyOnSelect WRITE setCopyOnSelect NOTIFY copyOnSelectChanged)
    Q_PROPERTY(bool confirmMultilinePaste READ confirmMultilinePaste WRITE setConfirmMultilinePaste NOTIFY
                   confirmMultilinePasteChanged)

public:
    explicit TerminalItem(QQuickItem *parent = nullptr);

    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QString fontFamily() const;
    [[nodiscard]] int fontPixelSize() const noexcept;
    [[nodiscard]] QString cursorPreference() const;
    [[nodiscard]] bool cursorBlink() const noexcept;
    [[nodiscard]] bool copyOnSelect() const noexcept;
    [[nodiscard]] bool confirmMultilinePaste() const noexcept;

public slots:
    void setSnapshot(ztermy::terminal::TerminalSnapshotPtr snapshot);
    void setStatusText(const QString &status);
    void setClipboardText(const QString &text);
    void requestCurrentSize();
    void setFontFamily(const QString &family);
    void setFontPixelSize(int pixelSize);
    void setCursorPreference(const QString &preference);
    void setCursorBlink(bool enabled);
    void setCopyOnSelect(bool enabled);
    void setConfirmMultilinePaste(bool enabled);
    Q_INVOKABLE void resolveMultilinePaste(bool accepted);

signals:
    void inputGenerated(const QByteArray &bytes);
    void pasteRequested(const QByteArray &bytes);
    void scrollRequested(int rows);
    void selectionRequested(quint16 startColumn, quint16 startRow, quint16 endColumn, quint16 endRow, bool rectangular);
    void clearSelectionRequested();
    void copyRequested();
    void sizeRequested(quint16 columns, quint16 rows, quint32 cellWidthPixels, quint32 cellHeightPixels);
    void statusTextChanged();
    void fontChanged();
    void cursorAppearanceChanged();
    void copyOnSelectChanged();
    void confirmMultilinePasteChanged();
    void multilinePasteConfirmationRequested(int lineCount);

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    [[nodiscard]] QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void focusOutEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    [[nodiscard]] virtual QString readClipboardText() const;

private:
    void reportTerminalSize();
    [[nodiscard]] std::optional<ztermy::terminal::TerminalPoint> terminalPoint(const QPointF &position) const;
    [[nodiscard]] QRectF inputCursorRectangle() const;
    void clearPreedit();
    void notifyInputMethod() const;
    [[nodiscard]] qreal cellWidth() const;
    [[nodiscard]] qreal cellHeight() const;
    [[nodiscard]] ztermy::terminal::TerminalCursorStyle effectiveCursorStyle() const noexcept;

    ztermy::terminal::TerminalSnapshotPtr m_snapshot;
    QFont m_font;
    QTimer m_cursorBlinkTimer;
    QString m_statusText = QStringLiteral("Starting local terminal...");
    QString m_cursorPreference = QStringLiteral("terminal");
    QByteArray m_pendingMultilinePaste;
    std::uint64_t m_revision = 0;
    quint16 m_reportedColumns = 0;
    quint16 m_reportedRows = 0;
    ztermy::terminal::TerminalPoint m_selectionAnchor;
    QString m_preeditText;
    qsizetype m_preeditCursorPosition = 0;
    bool m_preeditCursorVisible = true;
    bool m_selecting = false;
    bool m_selectionMoved = false;
    bool m_cursorBlink = true;
    bool m_cursorBlinkPhase = true;
    bool m_copyOnSelect = false;
    bool m_confirmMultilinePaste = true;
    int m_wheelRemainder = 0;
};

} // namespace ztermy::ui
