#pragma once

#include "domain/terminal/TerminalEngine.h"
#include "ui/terminal/TerminalKeywordHighlighter.h"
#include "ui/terminal/TerminalQuickSelect.h"
#include "ui/terminal/TerminalRenderMetrics.h"

#include <QByteArray>
#include <QColor>
#include <QElapsedTimer>
#include <QFont>
#include <QPointF>
#include <QQuickItem>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <cstdint>

class QInputMethodEvent;
class QKeyEvent;
class QHoverEvent;
class QMouseEvent;
class QWheelEvent;
class QFocusEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;

namespace ztermy::ui
{

class TerminalItem : public QQuickItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(TerminalView)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontChanged)
    Q_PROPERTY(int fontPixelSize READ fontPixelSize WRITE setFontPixelSize NOTIFY fontChanged)
    Q_PROPERTY(bool ligaturesEnabled READ ligaturesEnabled WRITE setLigaturesEnabled NOTIFY fontChanged)
    Q_PROPERTY(
        qreal backgroundOpacity READ backgroundOpacity WRITE setBackgroundOpacity NOTIFY backgroundOpacityChanged)
    Q_PROPERTY(QString cursorPreference READ cursorPreference WRITE setCursorPreference NOTIFY cursorAppearanceChanged)
    Q_PROPERTY(bool cursorBlink READ cursorBlink WRITE setCursorBlink NOTIFY cursorAppearanceChanged)
    Q_PROPERTY(bool copyOnSelect READ copyOnSelect WRITE setCopyOnSelect NOTIFY copyOnSelectChanged)
    Q_PROPERTY(bool keepSelectionAfterCopy READ keepSelectionAfterCopy WRITE setKeepSelectionAfterCopy NOTIFY
                   keepSelectionAfterCopyChanged)
    Q_PROPERTY(bool confirmMultilinePaste READ confirmMultilinePaste WRITE setConfirmMultilinePaste NOTIFY
                   confirmMultilinePasteChanged)
    Q_PROPERTY(bool multilinePastePending READ multilinePastePending NOTIFY multilinePastePendingChanged)
    Q_PROPERTY(
        QString rightClickBehavior READ rightClickBehavior WRITE setRightClickBehavior NOTIFY rightClickBehaviorChanged)
    Q_PROPERTY(QString middleClickBehavior READ middleClickBehavior WRITE setMiddleClickBehavior NOTIFY
                   middleClickBehaviorChanged)
    Q_PROPERTY(QString wordDelimiters READ wordDelimiters WRITE setWordDelimiters NOTIFY wordDelimitersChanged)
    Q_PROPERTY(
        int scrollRowsPerWheel READ scrollRowsPerWheel WRITE setScrollRowsPerWheel NOTIFY scrollRowsPerWheelChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY hasSelectionChanged)
    Q_PROPERTY(bool selectionMatchesKeywordHighlight READ selectionMatchesKeywordHighlight NOTIFY
                   selectionMatchesKeywordHighlightChanged)
    Q_PROPERTY(bool scrollbarVisible READ scrollbarVisible NOTIFY scrollbarChanged)
    Q_PROPERTY(qreal scrollbarPosition READ scrollbarPosition NOTIFY scrollbarChanged)
    Q_PROPERTY(qreal scrollbarPageRatio READ scrollbarPageRatio NOTIFY scrollbarChanged)
    Q_PROPERTY(bool selectionActionVisible READ selectionActionVisible NOTIFY selectionActionChanged)
    Q_PROPERTY(QPointF selectionActionPosition READ selectionActionPosition NOTIFY selectionActionChanged)
    Q_PROPERTY(QString hoveredLink READ hoveredLink NOTIFY hoveredLinkChanged)
    Q_PROPERTY(QPointF hoveredLinkPosition READ hoveredLinkPosition NOTIFY hoveredLinkPositionChanged)
    Q_PROPERTY(bool quickSelectActive READ quickSelectActive NOTIFY quickSelectChanged)
    Q_PROPERTY(bool copyModeActive READ copyModeActive NOTIFY copyModeChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchHighlightChanged)
    Q_PROPERTY(
        bool searchCaseSensitive READ searchCaseSensitive WRITE setSearchCaseSensitive NOTIFY searchHighlightChanged)
    Q_PROPERTY(QColor searchMatchBackground READ searchMatchBackground WRITE setSearchMatchBackground NOTIFY
                   searchHighlightChanged)
    Q_PROPERTY(QColor searchCurrentBackground READ searchCurrentBackground WRITE setSearchCurrentBackground NOTIFY
                   searchHighlightChanged)
    Q_PROPERTY(QColor searchCurrentForeground READ searchCurrentForeground WRITE setSearchCurrentForeground NOTIFY
                   searchHighlightChanged)
    Q_PROPERTY(QVariantList keywordHighlightRules READ keywordHighlightRules WRITE setKeywordHighlightRules NOTIFY
                   keywordHighlightRulesChanged)
    Q_PROPERTY(
        QColor foregroundOverride READ foregroundOverride WRITE setForegroundOverride NOTIFY paletteOverrideChanged)
    Q_PROPERTY(
        QColor backgroundOverride READ backgroundOverride WRITE setBackgroundOverride NOTIFY paletteOverrideChanged)

public:
    explicit TerminalItem(QQuickItem *parent = nullptr);

    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QString fontFamily() const;
    [[nodiscard]] int fontPixelSize() const noexcept;
    [[nodiscard]] bool ligaturesEnabled() const noexcept;
    [[nodiscard]] qreal backgroundOpacity() const noexcept;
    [[nodiscard]] QString cursorPreference() const;
    [[nodiscard]] bool cursorBlink() const noexcept;
    [[nodiscard]] bool copyOnSelect() const noexcept;
    [[nodiscard]] bool keepSelectionAfterCopy() const noexcept;
    [[nodiscard]] bool confirmMultilinePaste() const noexcept;
    [[nodiscard]] bool multilinePastePending() const noexcept;
    [[nodiscard]] QString rightClickBehavior() const;
    [[nodiscard]] QString middleClickBehavior() const;
    [[nodiscard]] QString wordDelimiters() const;
    [[nodiscard]] int scrollRowsPerWheel() const noexcept;
    [[nodiscard]] bool hasSelection() const noexcept;
    [[nodiscard]] bool selectionMatchesKeywordHighlight() const noexcept;
    [[nodiscard]] bool scrollbarVisible() const noexcept;
    [[nodiscard]] qreal scrollbarPosition() const noexcept;
    [[nodiscard]] qreal scrollbarPageRatio() const noexcept;
    [[nodiscard]] bool selectionActionVisible() const noexcept;
    [[nodiscard]] QPointF selectionActionPosition() const noexcept;
    [[nodiscard]] QString hoveredLink() const;
    [[nodiscard]] QPointF hoveredLinkPosition() const noexcept;
    [[nodiscard]] bool quickSelectActive() const noexcept;
    [[nodiscard]] bool copyModeActive() const noexcept;
    [[nodiscard]] QString searchQuery() const;
    [[nodiscard]] bool searchCaseSensitive() const noexcept;
    [[nodiscard]] QColor searchMatchBackground() const;
    [[nodiscard]] QColor searchCurrentBackground() const;
    [[nodiscard]] QColor searchCurrentForeground() const;
    [[nodiscard]] QVariantList keywordHighlightRules() const;
    [[nodiscard]] QColor foregroundOverride() const;
    [[nodiscard]] QColor backgroundOverride() const;
    void setPerformanceMetricsEnabled(bool enabled) noexcept;
    void resetPerformanceMetrics() noexcept;
    [[nodiscard]] TerminalRenderMetricsSnapshot performanceMetrics() const noexcept;

public slots:
    void setSnapshot(ztermy::terminal::TerminalSnapshotPtr snapshot);
    void setStatusText(const QString &status);
    void setClipboardText(const QString &text);
    void requestCurrentSize();
    void setFontFamily(const QString &family);
    void setFontPixelSize(int pixelSize);
    void setLigaturesEnabled(bool enabled);
    void setBackgroundOpacity(qreal opacity);
    void setCursorPreference(const QString &preference);
    void setCursorBlink(bool enabled);
    void setCopyOnSelect(bool enabled);
    void setKeepSelectionAfterCopy(bool enabled);
    void setConfirmMultilinePaste(bool enabled);
    void setRightClickBehavior(const QString &behavior);
    void setMiddleClickBehavior(const QString &behavior);
    void setWordDelimiters(const QString &delimiters);
    void setScrollRowsPerWheel(int rows);
    void setSearchQuery(const QString &query);
    void setSearchCaseSensitive(bool enabled);
    void setSearchMatchBackground(const QColor &color);
    void setSearchCurrentBackground(const QColor &color);
    void setSearchCurrentForeground(const QColor &color);
    void setKeywordHighlightRules(const QVariantList &rules);
    void setForegroundOverride(const QColor &color);
    void setBackgroundOverride(const QColor &color);
    Q_INVOKABLE void resolveMultilinePaste(bool accepted);
    Q_INVOKABLE void scrollToFraction(qreal fraction);
    Q_INVOKABLE void scrollLines(int rows);
    Q_INVOKABLE void scrollPage(int pages);
    Q_INVOKABLE void dismissSelectionAction();
    Q_INVOKABLE void copySelection();
    Q_INVOKABLE void pasteClipboard();
    Q_INVOKABLE void selectVisibleTerminal();
    Q_INVOKABLE void selectAllTerminal();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void requestContextMenu();
    Q_INVOKABLE void copyHoveredLink();
    Q_INVOKABLE void startQuickSelect();
    Q_INVOKABLE void cancelQuickSelect();
    Q_INVOKABLE void startCopyMode();
    Q_INVOKABLE void cancelCopyMode();

signals:
    void inputGenerated(const QByteArray &bytes);
    void keyEventGenerated(const ztermy::terminal::TerminalKeyEvent &event);
    void mouseEventGenerated(const ztermy::terminal::TerminalMouseEvent &event);
    void focusEventGenerated(bool focused);
    void pasteRequested(const QByteArray &bytes);
    void localFilesDropped(const QStringList &paths);
    void scrollRequested(int rows);
    void selectionRequested(quint16 startColumn, quint16 startRow, quint16 endColumn, quint16 endRow, bool rectangular);
    void selectionGestureRequested(const ztermy::terminal::TerminalSelectionGesture &gesture);
    void copyModeActionRequested(const ztermy::terminal::TerminalCopyModeAction &action);
    void selectAllRequested();
    void clearSelectionRequested();
    void copyRequested();
    void sizeRequested(quint16 columns, quint16 rows, quint32 cellWidthPixels, quint32 cellHeightPixels);
    void statusTextChanged();
    void fontChanged();
    void backgroundOpacityChanged();
    void cursorAppearanceChanged();
    void copyOnSelectChanged();
    void keepSelectionAfterCopyChanged();
    void confirmMultilinePasteChanged();
    void multilinePastePendingChanged();
    void rightClickBehaviorChanged();
    void middleClickBehaviorChanged();
    void wordDelimitersChanged();
    void scrollRowsPerWheelChanged();
    void hasSelectionChanged();
    void selectionMatchesKeywordHighlightChanged();
    void scrollbarChanged();
    void selectionActionChanged();
    void searchHighlightChanged();
    void keywordHighlightRulesChanged();
    void paletteOverrideChanged();
    void hoveredLinkChanged();
    void hoveredLinkPositionChanged();
    void linkActivated(const QString &uri);
    void quickSelectChanged();
    void copyModeChanged();
    void multilinePasteConfirmationRequested(int lineCount);
    void contextMenuRequested(qreal x, qreal y);

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    [[nodiscard]] QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;
    void wheelEvent(QWheelEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    [[nodiscard]] virtual QString readClipboardText() const;

private:
    void invalidateRenderer(bool full);
    void reportTerminalSize();
    [[nodiscard]] std::optional<ztermy::terminal::TerminalPoint> terminalPoint(const QPointF &position) const;
    [[nodiscard]] std::uint32_t hyperlinkAt(const QPointF &position) const;
    void updateHoveredLink(const QPointF &position, Qt::KeyboardModifiers modifiers);
    void clearHoveredLink();
    [[nodiscard]] bool handleQuickSelectKey(QKeyEvent *event);
    [[nodiscard]] bool handleCopyModeKey(QKeyEvent *event);
    void activateQuickSelectTarget(const TerminalQuickSelectTarget &target, Qt::KeyboardModifiers modifiers);
    [[nodiscard]] QRectF inputCursorRectangle() const;
    void clearPreedit();
    void notifyInputMethod() const;
    [[nodiscard]] qreal cellWidth() const;
    [[nodiscard]] qreal cellHeight() const;
    [[nodiscard]] ztermy::terminal::TerminalCursorStyle effectiveCursorStyle() const noexcept;
    void setHasSelection(bool selected);
    void refreshSelectionMatchesKeywordHighlight();
    void selectWordAt(const ztermy::terminal::TerminalPoint &point, const QPointF &position);
    void selectLineAt(quint16 row, const QPointF &position);
    [[nodiscard]] ztermy::terminal::TerminalSelectionGesture
    selectionGesture(ztermy::terminal::TerminalSelectionGestureType type, const QPointF &position,
                     quint64 timestamp = 0) const;
    void updateSelectionAutoscroll(const QPointF &position);
    void stopSelectionAutoscroll();
    void cancelSelectionGesture();
    [[nodiscard]] bool terminalOwnsMouse(const Qt::KeyboardModifiers &modifiers) const noexcept;
    [[nodiscard]] ztermy::terminal::TerminalMouseEvent
    mouseEvent(ztermy::terminal::TerminalMouseAction action, ztermy::terminal::TerminalMouseButton button,
               const QPointF &position, Qt::KeyboardModifiers modifiers, Qt::MouseButtons buttons) const;
    void reportFocus(bool focused);
    void requestPasteBytes(const QByteArray &bytes);

    ztermy::terminal::TerminalSnapshotPtr m_snapshot;
    QFont m_font;
    QTimer m_cursorBlinkTimer;
    QTimer m_selectionAutoscrollTimer;
    QTimer m_focusOutTimer;
    QElapsedTimer m_selectionEdgeDwell;
    QString m_statusText;
    QString m_cursorPreference = QStringLiteral("terminal");
    QString m_rightClickBehavior = QStringLiteral("context-menu");
    QString m_middleClickBehavior = QStringLiteral("disabled");
    QString m_wordDelimiters = QStringLiteral(" \t'\"│`|;,()[]{}<>$@:#~");
    QByteArray m_pendingMultilinePaste;
    std::uint64_t m_revision = 0;
    qreal m_backgroundOpacity = 1.0;
    quint16 m_reportedColumns = 0;
    quint16 m_reportedRows = 0;
    ztermy::terminal::TerminalPoint m_selectionAnchor;
    QPointF m_selectionActionPosition;
    QPointF m_selectionPointerPosition;
    QPointF m_hoverPosition;
    QPointF m_linkPressPosition;
    QString m_preeditText;
    QString m_quickSelectInput;
    QString m_searchQuery;
    qsizetype m_preeditCursorPosition = 0;
    bool m_preeditCursorVisible = true;
    bool m_selecting = false;
    bool m_extendingSelection = false;
    bool m_selectionMoved = false;
    bool m_selectionClickSelected = false;
    bool m_selectionActionVisible = false;
    bool m_hoverInside = false;
    bool m_controlModifierDown = false;
    bool m_quickSelectActive = false;
    bool m_copyModeActive = false;
    bool m_hasSelection = false;
    bool m_selectionMatchesKeywordHighlight = false;
    bool m_cursorBlink = true;
    bool m_cursorBlinkPhase = true;
    bool m_ligaturesEnabled = true;
    bool m_copyOnSelect = false;
    bool m_keepSelectionAfterCopy = false;
    bool m_confirmMultilinePaste = true;
    bool m_searchCaseSensitive = false;
    bool m_fullInvalidationPending = true;
    std::optional<bool> m_lastReportedFocus;
    QVariantList m_keywordHighlightRuleValues;
    std::vector<TerminalKeywordRule> m_keywordHighlightRules;
    QColor m_searchMatchBackground = QColor(245, 158, 11, 82);
    QColor m_searchCurrentBackground = QColor(245, 158, 11);
    QColor m_searchCurrentForeground = QColor(15, 23, 42);
    QColor m_foregroundOverride;
    QColor m_backgroundOverride;
    TerminalRenderMetrics m_renderMetrics;
    int m_wheelRemainder = 0;
    int m_scrollRowsPerWheel = 3;
    qreal m_pixelWheelRemainder = 0.0;
    int m_selectionAutoscrollDirection = 0;
    quint64 m_lastDoubleClickTimestamp = 0;
    std::uint32_t m_hoveredLinkId = 0;
    std::uint32_t m_pressedLinkId = 0;
    std::vector<TerminalQuickSelectTarget> m_quickSelectTargets;
    QPointF m_lastDoubleClickPosition;
};

} // namespace ztermy::ui
