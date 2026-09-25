#include "ui/terminal/TerminalItem.h"
#include "ui/terminal/TerminalLayoutMetrics.h"

#include "platform/windows/WindowsTerminalInput.h"
#include "ui/terminal/TerminalQuickSelect.h"
#include "ui/terminal/TerminalRowReuseAnalysis.h"
#include "ui/terminal/TerminalTextLayout.h"

#include <QClipboard>
#include <QColor>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFocusEvent>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHash>
#include <QHoverEvent>
#include <QImage>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QStyleHints>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>

namespace
{

constexpr std::array<QFont::Tag, 4> ligatureFeatures{
    QFont::Tag{"liga"},
    QFont::Tag{"clig"},
    QFont::Tag{"calt"},
    QFont::Tag{"dlig"},
};

void configureLigatures(QFont &font, const bool enabled)
{
    for (const QFont::Tag feature : ligatureFeatures)
    {
        if (enabled)
        {
            font.unsetFeature(feature);
        }
        else
        {
            font.setFeature(feature, 0);
        }
    }
}

[[nodiscard]] ztermy::terminal::TerminalMouseButton terminalMouseButton(const Qt::MouseButton button) noexcept
{
    using ztermy::terminal::TerminalMouseButton;
    switch (button)
    {
        case Qt::LeftButton:
            return TerminalMouseButton::left;
        case Qt::RightButton:
            return TerminalMouseButton::right;
        case Qt::MiddleButton:
            return TerminalMouseButton::middle;
        case Qt::BackButton:
            return TerminalMouseButton::eight;
        case Qt::ForwardButton:
            return TerminalMouseButton::nine;
        case Qt::ExtraButton4:
            return TerminalMouseButton::ten;
        case Qt::ExtraButton5:
            return TerminalMouseButton::eleven;
        default:
            return TerminalMouseButton::none;
    }
}

} // namespace

namespace ztermy::ui
{

TerminalItem::TerminalItem(QQuickItem *parent) : QQuickItem(parent)
{
    m_statusText = tr("Starting local terminal...");
    setFlag(ItemHasContents, true);
    setFlag(ItemAcceptsInputMethod, true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton | Qt::BackButton | Qt::ForwardButton
                            | Qt::ExtraButton4 | Qt::ExtraButton5);
    setAcceptHoverEvents(true);
    setFlag(ItemAcceptsDrops, true);
    setActiveFocusOnTab(true);

    m_font.setFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")});
    m_font.setPixelSize(14);
    m_font.setStyleHint(QFont::Monospace);
    m_font.setFixedPitch(true);
    refreshFontMetrics();

    m_cursorBlinkTimer.setInterval(530);
    QObject::connect(&m_cursorBlinkTimer, &QTimer::timeout, this, [this] {
        // Only the focused viewport blinks; the others keep a steady cursor.
        if (!isVisible() || !hasActiveFocus() || !m_snapshot || !m_snapshot->cursor.visible)
        {
            return;
        }
        m_cursorBlinkPhase = !m_cursorBlinkPhase;
        m_renderMetrics.recordCursorInvalidation();
        invalidateRenderer(false);
    });
    m_cursorBlinkTimer.start();

    m_selectionAutoscrollTimer.setInterval(32);
    QObject::connect(&m_selectionAutoscrollTimer, &QTimer::timeout, this, [this] {
        constexpr qint64 dwellMilliseconds = 75;
        if (!m_selecting || m_selectionAutoscrollDirection == 0 || !m_selectionEdgeDwell.isValid()
            || m_selectionEdgeDwell.elapsed() < dwellMilliseconds)
        {
            return;
        }
        const qreal edge = m_selectionAutoscrollDirection < 0 ? verticalPadding : height() - verticalPadding;
        const qreal distance = std::abs(m_selectionPointerPosition.y() - edge);
        const int distanceRows = static_cast<int>(std::floor(distance / std::max<qreal>(cellHeight(), 1.0)));
        const int acceleration = static_cast<int>((m_selectionEdgeDwell.elapsed() - dwellMilliseconds) / 600);
        const int rows = std::clamp(1 + distanceRows + acceleration, 1, 12) * m_selectionAutoscrollDirection;
        auto gesture =
            selectionGesture(terminal::TerminalSelectionGestureType::autoscrollTick, m_selectionPointerPosition);
        gesture.scrollRows = rows;
        emit selectionGestureRequested(gesture);
    });
    m_focusOutTimer.setSingleShot(true);
    m_focusOutTimer.setInterval(0);
    QObject::connect(&m_focusOutTimer, &QTimer::timeout, this, [this] {
        if (!hasActiveFocus())
        {
            reportFocus(false);
        }
    });
    QObject::connect(this, &QQuickItem::visibleChanged, this, [this] {
        if (!isVisible())
        {
            cancelSelectionGesture();
            reportFocus(false);
        }
        else if (hasActiveFocus())
        {
            reportFocus(true);
        }
    });
}

QString TerminalItem::statusText() const
{
    return m_statusText;
}

QString TerminalItem::fontFamily() const
{
    return m_font.families().value(0);
}

int TerminalItem::fontPixelSize() const noexcept
{
    return m_font.pixelSize();
}

bool TerminalItem::ligaturesEnabled() const noexcept
{
    return m_ligaturesEnabled;
}

qreal TerminalItem::backgroundOpacity() const noexcept
{
    return m_backgroundOpacity;
}

QString TerminalItem::cursorPreference() const
{
    return m_cursorPreference;
}

bool TerminalItem::cursorBlink() const noexcept
{
    return m_cursorBlink;
}

bool TerminalItem::copyOnSelect() const noexcept
{
    return m_copyOnSelect;
}

bool TerminalItem::keepSelectionAfterCopy() const noexcept
{
    return m_keepSelectionAfterCopy;
}

bool TerminalItem::confirmMultilinePaste() const noexcept
{
    return m_confirmMultilinePaste;
}

bool TerminalItem::multilinePastePending() const noexcept
{
    return !m_pendingMultilinePaste.isEmpty();
}

QString TerminalItem::rightClickBehavior() const
{
    return m_rightClickBehavior;
}

QString TerminalItem::middleClickBehavior() const
{
    return m_middleClickBehavior;
}

QString TerminalItem::wordDelimiters() const
{
    return m_wordDelimiters;
}

int TerminalItem::scrollRowsPerWheel() const noexcept
{
    return m_scrollRowsPerWheel;
}

bool TerminalItem::hasSelection() const noexcept
{
    return m_hasSelection;
}

bool TerminalItem::selectionMatchesKeywordHighlight() const noexcept
{
    return m_selectionMatchesKeywordHighlight;
}

bool TerminalItem::scrollbarVisible() const noexcept
{
    return m_snapshot && m_snapshot->scrollbar.total > m_snapshot->scrollbar.visible;
}

qreal TerminalItem::scrollbarPosition() const noexcept
{
    if (!scrollbarVisible())
        return 1.0;
    const std::uint64_t maximumOffset = m_snapshot->scrollbar.total - m_snapshot->scrollbar.visible;
    return maximumOffset == 0 ? 1.0
                              : static_cast<qreal>(m_snapshot->scrollbar.offset) / static_cast<qreal>(maximumOffset);
}

qreal TerminalItem::scrollbarPageRatio() const noexcept
{
    if (!m_snapshot || m_snapshot->scrollbar.total == 0)
        return 1.0;
    return std::clamp(
        static_cast<qreal>(m_snapshot->scrollbar.visible) / static_cast<qreal>(m_snapshot->scrollbar.total), 0.0, 1.0);
}

bool TerminalItem::selectionActionVisible() const noexcept
{
    return m_selectionActionVisible;
}

QPointF TerminalItem::selectionActionPosition() const noexcept
{
    return m_selectionActionPosition;
}

bool TerminalItem::selectionActionPreferBelow() const noexcept
{
    return m_selectionActionPreferBelow;
}

QRectF TerminalItem::terminalCursorRectangle() const
{
    return inputCursorRectangle();
}

QVariantList TerminalItem::keywordHighlightRules() const
{
    return m_keywordHighlightRuleValues;
}

QString TerminalItem::searchQuery() const
{
    return m_searchQuery;
}

bool TerminalItem::searchCaseSensitive() const noexcept
{
    return m_searchCaseSensitive;
}

QColor TerminalItem::searchMatchBackground() const
{
    return m_searchMatchBackground;
}

QColor TerminalItem::searchCurrentBackground() const
{
    return m_searchCurrentBackground;
}

QColor TerminalItem::searchCurrentForeground() const
{
    return m_searchCurrentForeground;
}

QColor TerminalItem::foregroundOverride() const
{
    return m_foregroundOverride;
}

QColor TerminalItem::backgroundOverride() const
{
    return m_backgroundOverride;
}

QColor TerminalItem::selectionBackground() const
{
    return m_selectionBackground;
}

QColor TerminalItem::selectionForeground() const
{
    return m_selectionForeground;
}

void TerminalItem::setPerformanceMetricsEnabled(const bool enabled) noexcept
{
    m_renderMetrics.setEnabled(enabled);
}

void TerminalItem::resetPerformanceMetrics() noexcept
{
    m_renderMetrics.reset();
}

TerminalRenderMetricsSnapshot TerminalItem::performanceMetrics() const noexcept
{
    return m_renderMetrics.snapshot();
}

void TerminalItem::setSnapshot(terminal::TerminalSnapshotPtr snapshot)
{
    const QRectF previousCursor = inputCursorRectangle();
    if (m_quickSelectActive)
        cancelQuickSelect();
    if (!snapshot)
    {
        cancelSelectionGesture();
        dismissSelectionAction();
        setHasSelection(false);
        const bool scrollbarWasVisible = scrollbarVisible();
        m_snapshot.reset();
        m_keywordStyles.clear();
        m_searchStyles.clear();
        m_searchStylesDirty = false;
        refreshSelectionMatchesKeywordHighlight();
        clearHoveredLink();
        invalidateRenderer(true);
        notifyInputMethod();
        if (scrollbarWasVisible)
        {
            emit scrollbarChanged();
        }
        emit cursorGeometryChanged();
        return;
    }
    const bool focusReportingBecameActive =
        snapshot->focusReportingActive && (!m_snapshot || !m_snapshot->focusReportingActive);
    m_renderMetrics.recordSnapshot(snapshot->damage, snapshot->damagedRows.size());
    const bool selectionActionMoved = m_snapshot && m_selectionActionVisible && !m_selecting
                                      && snapshot->selectionPresent && !snapshot->searchSelectionPresent
                                      && m_snapshot->scrollbar.offset != snapshot->scrollbar.offset;
    if (selectionActionMoved)
    {
        m_selectionActionPosition.ry() +=
            (static_cast<qreal>(m_snapshot->scrollbar.offset) - static_cast<qreal>(snapshot->scrollbar.offset))
            * cellHeight();
    }
    const bool selectionBecameVisible = !m_hasSelection && snapshot->selectionPresent;
    setHasSelection(snapshot->selectionPresent);
    if (!snapshot->selectionPresent || snapshot->searchSelectionPresent)
        dismissSelectionAction();
    else if (selectionBecameVisible && !m_selecting)
    {
        showSelectionAction(m_selectionPointerPosition, m_selectionActionPreferBelow);
    }
    else if (selectionActionMoved)
    {
        emit selectionActionChanged();
    }
    const bool scrollbarMoved = !m_snapshot || m_snapshot->scrollbar.total != snapshot->scrollbar.total
                                || m_snapshot->scrollbar.offset != snapshot->scrollbar.offset
                                || m_snapshot->scrollbar.visible != snapshot->scrollbar.visible;
    m_snapshot = std::move(snapshot);
    refreshKeywordStyles();
    m_searchStylesDirty = true;
    refreshSelectionMatchesKeywordHighlight();
    if (m_hoverInside)
        updateHoveredLink(m_hoverPosition, QGuiApplication::keyboardModifiers());
    if (focusReportingBecameActive)
    {
        m_lastReportedFocus.reset();
        reportFocus(hasActiveFocus());
    }
    invalidateRenderer(true);
    notifyInputMethod();
    if (scrollbarMoved)
        emit scrollbarChanged();
    if (previousCursor != inputCursorRectangle())
    {
        emit cursorGeometryChanged();
    }
}

QString TerminalItem::hoveredLink() const
{
    if (!m_snapshot)
        return {};
    const terminal::TerminalHyperlink *link = m_snapshot->hyperlink(m_hoveredLinkId);
    return link == nullptr ? QString{} : QString::fromUtf8(link->uri);
}

QPointF TerminalItem::hoveredLinkPosition() const noexcept
{
    return m_hoverPosition;
}

bool TerminalItem::quickSelectActive() const noexcept
{
    return m_quickSelectActive;
}

bool TerminalItem::copyModeActive() const noexcept
{
    return m_copyModeActive;
}

void TerminalItem::setStatusText(const QString &status)
{
    if (m_statusText == status)
        return;
    m_statusText = status;
    emit statusTextChanged();
}

void TerminalItem::setClipboardText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text, QClipboard::Clipboard);
}

void TerminalItem::requestCurrentSize()
{
    m_reportedColumns = 0;
    m_reportedRows = 0;
    reportTerminalSize();
}

void TerminalItem::setFontFamily(const QString &family)
{
    const QString normalized = family.trimmed();
    if (normalized.isEmpty() || normalized.size() > 128 || fontFamily() == normalized)
        return;
    m_font.setFamilies({normalized, QStringLiteral("Consolas")});
    refreshFontMetrics();
    m_reportedColumns = 0;
    m_reportedRows = 0;
    invalidateRenderer(true);
    reportTerminalSize();
    notifyInputMethod();
    emit fontChanged();
}

void TerminalItem::setFontPixelSize(const int pixelSize)
{
    if (pixelSize < 8 || pixelSize > 32 || m_font.pixelSize() == pixelSize)
        return;
    m_font.setPixelSize(pixelSize);
    refreshFontMetrics();
    m_reportedColumns = 0;
    m_reportedRows = 0;
    invalidateRenderer(true);
    reportTerminalSize();
    notifyInputMethod();
    emit fontChanged();
}

void TerminalItem::setLigaturesEnabled(const bool enabled)
{
    if (m_ligaturesEnabled == enabled)
    {
        return;
    }
    m_ligaturesEnabled = enabled;
    configureLigatures(m_font, enabled);
    refreshFontMetrics();
    invalidateRenderer(true);
    emit fontChanged();
}

void TerminalItem::setBackgroundOpacity(const qreal opacity)
{
    const qreal normalized = std::clamp(opacity, 0.0, 1.0);
    if (qFuzzyCompare(m_backgroundOpacity, normalized))
    {
        return;
    }
    m_backgroundOpacity = normalized;
    invalidateRenderer(true);
    emit backgroundOpacityChanged();
}

void TerminalItem::setCursorPreference(const QString &preference)
{
    if ((preference != QStringLiteral("terminal") && preference != QStringLiteral("block")
         && preference != QStringLiteral("bar") && preference != QStringLiteral("underline"))
        || m_cursorPreference == preference)
    {
        return;
    }
    m_cursorPreference = preference;
    invalidateRenderer(true);
    emit cursorAppearanceChanged();
}

void TerminalItem::setCursorBlink(const bool enabled)
{
    if (m_cursorBlink == enabled)
    {
        return;
    }
    m_cursorBlink = enabled;
    m_cursorBlinkPhase = true;
    enabled ? m_cursorBlinkTimer.start() : m_cursorBlinkTimer.stop();
    invalidateRenderer(true);
    emit cursorAppearanceChanged();
}

void TerminalItem::setTerminalCursorVisible(const bool visible)
{
    if (m_terminalCursorVisible == visible)
        return;
    m_terminalCursorVisible = visible;
    invalidateRenderer(false);
    emit cursorAppearanceChanged();
}

void TerminalItem::setCopyOnSelect(const bool enabled)
{
    if (m_copyOnSelect == enabled)
    {
        return;
    }
    m_copyOnSelect = enabled;
    emit copyOnSelectChanged();
}

void TerminalItem::setKeepSelectionAfterCopy(const bool enabled)
{
    if (m_keepSelectionAfterCopy == enabled)
    {
        return;
    }
    m_keepSelectionAfterCopy = enabled;
    emit keepSelectionAfterCopyChanged();
}

void TerminalItem::setConfirmMultilinePaste(const bool enabled)
{
    if (m_confirmMultilinePaste == enabled)
    {
        return;
    }
    m_confirmMultilinePaste = enabled;
    if (!enabled && !m_pendingMultilinePaste.isEmpty())
    {
        m_pendingMultilinePaste.clear();
        emit multilinePastePendingChanged();
    }
    emit confirmMultilinePasteChanged();
}

void TerminalItem::setRightClickBehavior(const QString &behavior)
{
    static constexpr std::array<std::string_view, 4> supported{"context-menu", "copy-paste", "paste", "select-word"};
    const QByteArray encodedBehavior = behavior.toUtf8();
    const std::string_view value{encodedBehavior.constData(), static_cast<std::size_t>(encodedBehavior.size())};
    if (!std::ranges::contains(supported, value) || m_rightClickBehavior == behavior)
    {
        return;
    }
    m_rightClickBehavior = behavior;
    emit rightClickBehaviorChanged();
}

void TerminalItem::setMiddleClickBehavior(const QString &behavior)
{
    static constexpr std::array<std::string_view, 3> supported{"disabled", "paste", "context-menu"};
    const QByteArray encodedBehavior = behavior.toUtf8();
    const std::string_view value{encodedBehavior.constData(), static_cast<std::size_t>(encodedBehavior.size())};
    if (!std::ranges::contains(supported, value) || m_middleClickBehavior == behavior)
    {
        return;
    }
    m_middleClickBehavior = behavior;
    emit middleClickBehaviorChanged();
}

void TerminalItem::setWordDelimiters(const QString &delimiters)
{
    if (delimiters.size() > 128 || m_wordDelimiters == delimiters)
    {
        return;
    }
    m_wordDelimiters = delimiters;
    emit wordDelimitersChanged();
}

void TerminalItem::setScrollRowsPerWheel(const int rows)
{
    const int bounded = std::clamp(rows, 1, 20);
    if (m_scrollRowsPerWheel == bounded)
    {
        return;
    }
    m_scrollRowsPerWheel = bounded;
    emit scrollRowsPerWheelChanged();
}

void TerminalItem::setKeywordHighlightRules(const QVariantList &rules)
{
    if (m_keywordHighlightRuleValues == rules)
    {
        return;
    }
    m_keywordHighlightRuleValues = rules;
    m_keywordHighlightRules.clear();
    m_keywordHighlightRules.reserve(std::min<std::size_t>(static_cast<std::size_t>(rules.size()), maximumKeywordRules));
    for (const QVariant &value : rules)
    {
        if (m_keywordHighlightRules.size() >= maximumKeywordRules)
        {
            break;
        }
        const QVariantMap map = value.toMap();
        const QString pattern = map.value(QStringLiteral("pattern")).toString();
        if (pattern.isEmpty() || pattern.size() > maximumKeywordPatternLength)
        {
            continue;
        }
        const QColor foreground(map.value(QStringLiteral("foreground")).toString());
        const QColor background(map.value(QStringLiteral("background")).toString());
        if (!foreground.isValid() && !background.isValid())
        {
            continue;
        }
        m_keywordHighlightRules.push_back(TerminalKeywordRule{
            .id = map.value(QStringLiteral("id")).toString(),
            .pattern = pattern,
            .foreground = foreground,
            .background = background,
            .enabled = map.value(QStringLiteral("enabled"), true).toBool(),
            .caseSensitive = map.value(QStringLiteral("caseSensitive"), false).toBool(),
        });
    }
    refreshKeywordStyles();
    refreshSelectionMatchesKeywordHighlight();
    invalidateRenderer(true);
    emit keywordHighlightRulesChanged();
}

void TerminalItem::setSearchQuery(const QString &query)
{
    if (m_searchQuery == query)
    {
        return;
    }
    m_searchQuery = query;
    m_searchStylesDirty = true;
    invalidateRenderer(true);
    emit searchHighlightChanged();
}

void TerminalItem::setSearchCaseSensitive(const bool enabled)
{
    if (m_searchCaseSensitive == enabled)
    {
        return;
    }
    m_searchCaseSensitive = enabled;
    m_searchStylesDirty = true;
    invalidateRenderer(true);
    emit searchHighlightChanged();
}

void TerminalItem::setSearchMatchBackground(const QColor &value)
{
    if (m_searchMatchBackground == value)
    {
        return;
    }
    m_searchMatchBackground = value;
    m_searchStylesDirty = true;
    invalidateRenderer(true);
    emit searchHighlightChanged();
}

void TerminalItem::setSearchCurrentBackground(const QColor &value)
{
    if (m_searchCurrentBackground == value)
    {
        return;
    }
    m_searchCurrentBackground = value;
    invalidateRenderer(true);
    emit searchHighlightChanged();
}

void TerminalItem::setSearchCurrentForeground(const QColor &value)
{
    if (m_searchCurrentForeground == value)
    {
        return;
    }
    m_searchCurrentForeground = value;
    invalidateRenderer(true);
    emit searchHighlightChanged();
}

void TerminalItem::setForegroundOverride(const QColor &value)
{
    if (m_foregroundOverride == value)
    {
        return;
    }
    m_foregroundOverride = value;
    invalidateRenderer(true);
    emit paletteOverrideChanged();
}

void TerminalItem::setBackgroundOverride(const QColor &value)
{
    if (m_backgroundOverride == value)
    {
        return;
    }
    m_backgroundOverride = value;
    invalidateRenderer(true);
    emit paletteOverrideChanged();
}

void TerminalItem::setSelectionBackground(const QColor &value)
{
    if (m_selectionBackground == value)
    {
        return;
    }
    m_selectionBackground = value;
    invalidateRenderer(true);
    emit paletteOverrideChanged();
}

void TerminalItem::setSelectionForeground(const QColor &value)
{
    if (m_selectionForeground == value)
    {
        return;
    }
    m_selectionForeground = value;
    invalidateRenderer(true);
    emit paletteOverrideChanged();
}

void TerminalItem::resolveMultilinePaste(const bool accepted)
{
    QByteArray pending = std::move(m_pendingMultilinePaste);
    m_pendingMultilinePaste.clear();
    if (!pending.isEmpty())
    {
        emit multilinePastePendingChanged();
    }
    if (accepted && !pending.isEmpty())
    {
        emit pasteRequested(pending);
    }
}

void TerminalItem::scrollToFraction(const qreal fraction)
{
    scrollFractionDelta(scrollbarPosition(), fraction);
}

void TerminalItem::scrollFractionDelta(const qreal from, const qreal to)
{
    if (!scrollbarVisible())
    {
        return;
    }
    const std::uint64_t maximumOffset = m_snapshot->scrollbar.total - m_snapshot->scrollbar.visible;
    const auto offset = [maximumOffset](const qreal fraction) {
        const qreal normalized = std::clamp(fraction, 0.0, 1.0);
        return normalized <= 0.0 ? std::uint64_t{0}
               : normalized >= 1.0
                   ? maximumOffset
                   : static_cast<std::uint64_t>(std::llround(normalized * static_cast<qreal>(maximumOffset)));
    };
    const std::uint64_t currentOffset = offset(from);
    const std::uint64_t targetOffset = offset(to);
    if (targetOffset == currentOffset)
    {
        return;
    }
    const auto maximumStep = static_cast<std::uint64_t>(std::numeric_limits<int>::max());
    if (targetOffset > currentOffset)
    {
        emit scrollRequested(static_cast<int>(std::min(targetOffset - currentOffset, maximumStep)));
    }
    else
    {
        emit scrollRequested(-static_cast<int>(std::min(currentOffset - targetOffset, maximumStep)));
    }
}

void TerminalItem::scrollLines(const int rows)
{
    if (rows != 0)
    {
        emit scrollRequested(rows);
    }
}

void TerminalItem::scrollPage(const int pages)
{
    if (pages == 0 || !m_snapshot || m_snapshot->rows == 0)
    {
        return;
    }
    const int pageRows = std::max(1, static_cast<int>(m_snapshot->rows) - 1);
    const qint64 requestedRows = static_cast<qint64>(pages) * static_cast<qint64>(pageRows);
    emit scrollRequested(
        static_cast<int>(std::clamp(requestedRows, -static_cast<qint64>(std::numeric_limits<int>::max()),
                                    static_cast<qint64>(std::numeric_limits<int>::max()))));
}

void TerminalItem::dismissSelectionAction()
{
    if (!m_selectionActionVisible)
    {
        return;
    }
    m_selectionActionVisible = false;
    emit selectionActionChanged();
}

void TerminalItem::copySelection()
{
    copySelectionWithPolicy(m_keepSelectionAfterCopy);
}

void TerminalItem::copySelectionWithPolicy(const bool keepSelection)
{
    emit copyRequested();
    if (!keepSelection && m_hasSelection)
    {
        clearSelection();
    }
}

void TerminalItem::pasteClipboard()
{
    requestPasteBytes(readClipboardText().toUtf8());
}

void TerminalItem::requestPasteBytes(const QByteArray &bytes)
{
    if (bytes.isEmpty())
    {
        return;
    }
    const qsizetype lineBreaks = bytes.count('\n') > 0 ? bytes.count('\n') : bytes.count('\r');
    if (m_confirmMultilinePaste && lineBreaks > 0)
    {
        const bool wasPending = !m_pendingMultilinePaste.isEmpty();
        m_pendingMultilinePaste = bytes;
        if (!wasPending)
        {
            emit multilinePastePendingChanged();
        }
        const qsizetype maximumLineCount = std::numeric_limits<int>::max();
        emit multilinePasteConfirmationRequested(static_cast<int>(std::min(lineBreaks + 1, maximumLineCount)));
        return;
    }
    emit pasteRequested(bytes);
}

void TerminalItem::selectVisibleTerminal()
{
    if (!m_snapshot || m_snapshot->columns == 0 || m_snapshot->rows == 0)
    {
        return;
    }
    emit selectionRequested(0, 0, static_cast<quint16>(m_snapshot->columns - 1),
                            static_cast<quint16>(m_snapshot->rows - 1), false);
    setHasSelection(true);
}

void TerminalItem::selectAllTerminal()
{
    if (!m_snapshot || m_snapshot->columns == 0 || m_snapshot->rows == 0)
    {
        return;
    }
    emit selectAllRequested();
}

void TerminalItem::clearSelection()
{
    emit clearSelectionRequested();
    setHasSelection(false);
    dismissSelectionAction();
}

void TerminalItem::requestContextMenu()
{
    const QRectF cursor = inputCursorRectangle();
    emit contextMenuRequested(cursor.left(), cursor.bottom());
}

void TerminalItem::copyHoveredLink()
{
    const QString link = hoveredLink();
    if (!link.isEmpty())
    {
        QGuiApplication::clipboard()->setText(link);
    }
}

void TerminalItem::startQuickSelect()
{
    if (!m_snapshot)
    {
        return;
    }
    std::vector<TerminalQuickSelectTarget> targets = quickSelectTargets(*m_snapshot);
    if (targets.empty())
    {
        return;
    }
    clearPreedit();
    if (m_copyModeActive)
    {
        cancelCopyMode();
    }
    m_quickSelectTargets = std::move(targets);
    m_quickSelectInput.clear();
    m_quickSelectActive = true;
    emit quickSelectChanged();
    invalidateRenderer(true);
    forceActiveFocus(Qt::ShortcutFocusReason);
}

void TerminalItem::cancelQuickSelect()
{
    if (!m_quickSelectActive)
    {
        return;
    }
    m_quickSelectActive = false;
    m_quickSelectTargets.clear();
    m_quickSelectInput.clear();
    emit quickSelectChanged();
    invalidateRenderer(true);
}

void TerminalItem::startCopyMode()
{
    if (!m_snapshot || m_copyModeActive)
    {
        return;
    }
    cancelQuickSelect();
    clearPreedit();
    m_copyModeActive = true;
    emit copyModeActionRequested(terminal::TerminalCopyModeAction{.type = terminal::TerminalCopyModeActionType::begin});
    emit copyModeChanged();
    invalidateRenderer(true);
    forceActiveFocus(Qt::ShortcutFocusReason);
}

void TerminalItem::cancelCopyMode()
{
    if (!m_copyModeActive)
    {
        return;
    }
    m_copyModeActive = false;
    emit copyModeActionRequested(
        terminal::TerminalCopyModeAction{.type = terminal::TerminalCopyModeActionType::cancel});
    emit copyModeChanged();
    invalidateRenderer(true);
}

void TerminalItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    cancelSelectionGesture();
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    reportTerminalSize();
    // A QSGSimpleTextureNode scales its existing texture when only its target
    // rectangle changes. Repaint at the new pixel size immediately instead.
    invalidateRenderer(true);
    notifyInputMethod();
}

void TerminalItem::keyPressEvent(QKeyEvent *event)
{
    const bool modifierOnly = event->key() == Qt::Key_Control || event->key() == Qt::Key_Shift
                              || event->key() == Qt::Key_Alt || event->key() == Qt::Key_Meta;
    if (!modifierOnly)
    {
        dismissSelectionAction();
    }
    if (m_copyModeActive && handleCopyModeKey(event))
    {
        event->accept();
        return;
    }
    if (m_quickSelectActive && handleQuickSelectKey(event))
    {
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Control && m_hoverInside)
    {
        m_controlModifierDown = true;
        updateHoveredLink(m_hoverPosition, event->modifiers() | Qt::ControlModifier);
    }
    const bool control = event->modifiers().testFlag(Qt::ControlModifier);
    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
    if ((event->key() == Qt::Key_Menu) || (shift && event->key() == Qt::Key_F10))
    {
        requestContextMenu();
        event->accept();
        return;
    }
    if ((control && event->key() == Qt::Key_Insert)
        || (control && !shift && event->key() == Qt::Key_C && m_hasSelection))
    {
        copySelection();
        event->accept();
        return;
    }
    if (shift && !control && event->key() == Qt::Key_Insert)
    {
        pasteClipboard();
        event->accept();
        return;
    }

    const auto action =
        event->isAutoRepeat() ? terminal::TerminalKeyAction::repeat : terminal::TerminalKeyAction::press;
    const auto key = platform::windows::terminalKeyEvent(*event, action, !m_preeditText.isEmpty());
    if (key.key == terminal::TerminalKey::unidentified && key.text.empty())
    {
        QQuickItem::keyPressEvent(event);
        return;
    }

    if (m_hasSelection && !modifierOnly)
    {
        clearSelection();
    }
    emit keyEventGenerated(key);
    event->accept();
}

void TerminalItem::keyReleaseEvent(QKeyEvent *event)
{
    if (m_copyModeActive)
    {
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Control && m_hoverInside)
    {
        m_controlModifierDown = false;
        updateHoveredLink(m_hoverPosition, event->modifiers() & ~Qt::ControlModifier);
    }
    else if (event->key() == Qt::Key_Control)
    {
        m_controlModifierDown = false;
    }
    if (event->isAutoRepeat())
    {
        event->accept();
        return;
    }
    const auto key =
        platform::windows::terminalKeyEvent(*event, terminal::TerminalKeyAction::release, !m_preeditText.isEmpty());
    if (key.key == terminal::TerminalKey::unidentified)
    {
        QQuickItem::keyReleaseEvent(event);
        return;
    }
    emit keyEventGenerated(key);
    event->accept();
}

QString TerminalItem::readClipboardText() const
{
    return QGuiApplication::clipboard()->text(QClipboard::Clipboard);
}

void TerminalItem::inputMethodEvent(QInputMethodEvent *event)
{
    m_preeditText = event->preeditString();
    m_preeditCursorPosition = m_preeditText.size();
    m_preeditCursorVisible = true;
    m_cursorBlinkPhase = true;
    for (const QInputMethodEvent::Attribute &attribute : event->attributes())
    {
        if (attribute.type == QInputMethodEvent::Cursor)
        {
            m_preeditCursorPosition = std::clamp<qsizetype>(attribute.start, qsizetype{0}, m_preeditText.size());
            m_preeditCursorVisible = attribute.length != 0;
            break;
        }
    }

    if (!event->commitString().isEmpty())
    {
        emit inputGenerated(event->commitString().toUtf8());
    }

    invalidateRenderer(true);
    notifyInputMethod();
    event->accept();
}

QVariant TerminalItem::inputMethodQuery(const Qt::InputMethodQuery query) const
{
    if (query == Qt::ImEnabled)
    {
        return !m_copyModeActive && !m_quickSelectActive;
    }
    if (query == Qt::ImCursorRectangle)
    {
        return inputCursorRectangle();
    }
    if (query == Qt::ImSurroundingText || query == Qt::ImCurrentSelection)
    {
        return QString{};
    }
    if (query == Qt::ImCursorPosition || query == Qt::ImAnchorPosition || query == Qt::ImAbsolutePosition)
    {
        return 0;
    }
    return QQuickItem::inputMethodQuery(query);
}

void TerminalItem::focusInEvent(QFocusEvent *event)
{
    m_focusOutTimer.stop();
    restartCursorBlink();
    reportFocus(true);
    QQuickItem::focusInEvent(event);
}

void TerminalItem::focusOutEvent(QFocusEvent *event)
{
    cancelSelectionGesture();
    m_controlModifierDown = false;
    clearPreedit();
    restartCursorBlink();
    m_focusOutTimer.start();
    QQuickItem::focusOutEvent(event);
}

void TerminalItem::restartCursorBlink()
{
    const bool wasShown = m_cursorBlinkPhase;
    m_cursorBlinkPhase = true;
    if (m_cursorBlink)
        m_cursorBlinkTimer.start();
    if (!wasShown)
        invalidateRenderer(false);
}

void TerminalItem::hoverMoveEvent(QHoverEvent *event)
{
    m_hoverInside = true;
    if (m_hoverPosition != event->position())
    {
        m_hoverPosition = event->position();
        emit hoveredLinkPositionChanged();
    }
    const auto modifiers = event->modifiers() | (m_controlModifierDown ? Qt::ControlModifier : Qt::NoModifier);
    updateHoveredLink(event->position(), modifiers);
    if (terminalOwnsMouse(event->modifiers()))
    {
        emit mouseEventGenerated(mouseEvent(terminal::TerminalMouseAction::motion, terminal::TerminalMouseButton::none,
                                            event->position(), event->modifiers(), Qt::NoButton));
        event->accept();
        return;
    }
    QQuickItem::hoverMoveEvent(event);
}

void TerminalItem::hoverLeaveEvent(QHoverEvent *event)
{
    m_hoverInside = false;
    clearHoveredLink();
    QQuickItem::hoverLeaveEvent(event);
}

void TerminalItem::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus(Qt::MouseFocusReason);
    dismissSelectionAction();
    const std::uint32_t pressedLink = hyperlinkAt(event->position());
    if (event->button() == Qt::LeftButton && pressedLink != 0 && event->modifiers().testFlag(Qt::ControlModifier))
    {
        m_pressedLinkId = pressedLink;
        m_linkPressPosition = event->position();
        event->accept();
        return;
    }
    if (terminalOwnsMouse(event->modifiers()))
    {
        emit mouseEventGenerated(mouseEvent(terminal::TerminalMouseAction::press, terminalMouseButton(event->button()),
                                            event->position(), event->modifiers(), event->buttons()));
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton)
    {
        if (m_middleClickBehavior == QStringLiteral("paste"))
        {
            pasteClipboard();
        }
        else if (m_middleClickBehavior == QStringLiteral("context-menu"))
        {
            emit contextMenuRequested(event->position().x(), event->position().y());
        }
        event->accept();
        return;
    }
    const auto point = terminalPoint(event->position());
    if (!point)
    {
        QQuickItem::mousePressEvent(event);
        return;
    }

    if (event->button() == Qt::RightButton)
    {
        if (event->modifiers().testFlag(Qt::ShiftModifier) || m_rightClickBehavior == QStringLiteral("context-menu"))
        {
            emit contextMenuRequested(event->position().x(), event->position().y());
        }
        else if (m_rightClickBehavior == QStringLiteral("copy-paste"))
        {
            if (m_hasSelection)
            {
                copySelection();
            }
            else
            {
                pasteClipboard();
            }
        }
        else if (m_rightClickBehavior == QStringLiteral("paste"))
        {
            pasteClipboard();
        }
        else
        {
            selectWordAt(*point, event->position());
            emit contextMenuRequested(event->position().x(), event->position().y());
        }
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton)
    {
        QQuickItem::mousePressEvent(event);
        return;
    }

    const auto clickInterval = static_cast<quint64>(QGuiApplication::styleHints()->mouseDoubleClickInterval());
    const bool tripleClick = m_lastDoubleClickTimestamp > 0 && event->timestamp() >= m_lastDoubleClickTimestamp
                             && event->timestamp() - m_lastDoubleClickTimestamp <= clickInterval
                             && (event->position() - m_lastDoubleClickPosition).manhattanLength()
                                    <= QGuiApplication::styleHints()->startDragDistance();
    m_lastDoubleClickTimestamp = 0;

    if (event->modifiers().testFlag(Qt::ShiftModifier) && m_hasSelection)
    {
        emit selectionRequested(m_selectionAnchor.column, m_selectionAnchor.row, point->column, point->row, false);
        m_selecting = true;
        m_extendingSelection = true;
        m_selectionMoved = false;
        m_selectionClickSelected = true;
        m_selectionPointerPosition = event->position();
        stopSelectionAutoscroll();
        showSelectionAction(event->position(), point->row > m_selectionAnchor.row);
        event->accept();
        return;
    }

    if (tripleClick)
    {
        selectLineAt(point->row, event->position());
        m_selectionClickSelected = true;
        event->accept();
        return;
    }

    m_selectionAnchor = *point;
    m_selecting = true;
    m_selectionMoved = false;
    m_selectionClickSelected = false;
    m_selectionPointerPosition = event->position();
    stopSelectionAutoscroll();
    setHasSelection(false);
    emit selectionGestureRequested(
        selectionGesture(terminal::TerminalSelectionGestureType::press, event->position(), event->timestamp()));
    event->accept();
}

void TerminalItem::mouseDoubleClickEvent(QMouseEvent *event)
{
    forceActiveFocus(Qt::MouseFocusReason);
    if (terminalOwnsMouse(event->modifiers()))
    {
        emit mouseEventGenerated(mouseEvent(terminal::TerminalMouseAction::press, terminalMouseButton(event->button()),
                                            event->position(), event->modifiers(), event->buttons()));
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton)
    {
        if (const auto point = terminalPoint(event->position()))
        {
            selectWordAt(*point, event->position());
            m_selecting = false;
            m_selectionClickSelected = true;
            stopSelectionAutoscroll();
            m_lastDoubleClickTimestamp = event->timestamp();
            m_lastDoubleClickPosition = event->position();
            event->accept();
            return;
        }
    }
    QQuickItem::mouseDoubleClickEvent(event);
}

void TerminalItem::mouseMoveEvent(QMouseEvent *event)
{
    if (terminalOwnsMouse(event->modifiers()))
    {
        emit mouseEventGenerated(mouseEvent(terminal::TerminalMouseAction::motion, terminalMouseButton(event->button()),
                                            event->position(), event->modifiers(), event->buttons()));
        event->accept();
        return;
    }
    if (!m_selecting || !event->buttons().testFlag(Qt::LeftButton))
    {
        QQuickItem::mouseMoveEvent(event);
        return;
    }
    if (const auto point = terminalPoint(event->position()))
    {
        m_selectionMoved = true;
        if (m_extendingSelection)
        {
            emit selectionRequested(m_selectionAnchor.column, m_selectionAnchor.row, point->column, point->row, false);
            stopSelectionAutoscroll();
        }
        else
        {
            auto gesture = selectionGesture(terminal::TerminalSelectionGestureType::drag, event->position());
            gesture.rectangular = event->modifiers().testFlag(Qt::AltModifier);
            emit selectionGestureRequested(gesture);
            updateSelectionAutoscroll(event->position());
        }
        m_selectionPointerPosition = event->position();
    }
    event->accept();
}

void TerminalItem::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_pressedLinkId != 0)
    {
        const std::uint32_t pressedLink = std::exchange(m_pressedLinkId, 0);
        const qreal distance = (event->position() - m_linkPressPosition).manhattanLength();
        if (distance <= QGuiApplication::styleHints()->startDragDistance()
            && hyperlinkAt(event->position()) == pressedLink && m_snapshot)
        {
            if (const terminal::TerminalHyperlink *link = m_snapshot->hyperlink(pressedLink); link != nullptr)
            {
                emit linkActivated(QString::fromUtf8(link->uri));
            }
        }
        event->accept();
        return;
    }
    if (terminalOwnsMouse(event->modifiers()))
    {
        emit mouseEventGenerated(mouseEvent(terminal::TerminalMouseAction::release,
                                            terminalMouseButton(event->button()), event->position(), event->modifiers(),
                                            event->buttons()));
        event->accept();
        return;
    }
    if (!m_selecting || event->button() != Qt::LeftButton)
    {
        QQuickItem::mouseReleaseEvent(event);
        return;
    }
    stopSelectionAutoscroll();
    if (m_extendingSelection)
    {
        if (const auto point = terminalPoint(event->position()))
        {
            emit selectionRequested(m_selectionAnchor.column, m_selectionAnchor.row, point->column, point->row, false);
            showSelectionAction(event->position(), point->row > m_selectionAnchor.row);
        }
        if ((m_selectionMoved || m_selectionClickSelected) && m_copyOnSelect)
        {
            emit copyRequested();
        }
        m_selecting = false;
        m_extendingSelection = false;
        m_selectionClickSelected = false;
        event->accept();
        return;
    }
    if (m_selectionMoved)
    {
        if (const auto point = terminalPoint(event->position()))
        {
            auto gesture = selectionGesture(terminal::TerminalSelectionGestureType::drag, event->position());
            gesture.rectangular = event->modifiers().testFlag(Qt::AltModifier);
            emit selectionGestureRequested(gesture);
            const bool nonEmpty = point->column != m_selectionAnchor.column || point->row != m_selectionAnchor.row;
            if (nonEmpty)
            {
                setHasSelection(true);
                showSelectionAction(event->position(), point->row > m_selectionAnchor.row);
            }
        }
    }
    emit selectionGestureRequested(
        selectionGesture(terminal::TerminalSelectionGestureType::release, event->position(), event->timestamp()));
    if ((m_selectionMoved || m_selectionClickSelected) && m_copyOnSelect)
    {
        emit copyRequested();
    }
    m_selecting = false;
    m_extendingSelection = false;
    m_selectionClickSelected = false;
    event->accept();
}

void TerminalItem::mouseUngrabEvent()
{
    cancelSelectionGesture();
    QQuickItem::mouseUngrabEvent();
}

void TerminalItem::setHasSelection(const bool selected)
{
    if (m_hasSelection == selected)
    {
        return;
    }
    m_hasSelection = selected;
    emit hasSelectionChanged();
    if (!selected && m_selectionMatchesKeywordHighlight)
    {
        m_selectionMatchesKeywordHighlight = false;
        emit selectionMatchesKeywordHighlightChanged();
    }
}

void TerminalItem::refreshSelectionMatchesKeywordHighlight()
{
    bool matches = false;
    if (m_snapshot && m_snapshot->selectionPresent && !m_snapshot->searchSelectionPresent && !m_keywordStyles.empty())
    {
        const std::vector<TerminalKeywordCellStyle> &styles = m_keywordStyles;
        for (quint16 row = 0; row < m_snapshot->rows && !matches; ++row)
        {
            for (quint16 column = 0; column < m_snapshot->columns; ++column)
            {
                const std::size_t index = (static_cast<std::size_t>(row) * m_snapshot->columns) + column;
                const terminal::TerminalCell &cell = m_snapshot->cell(column, row);
                if (cell.selected && (styles[index].foreground.isValid() || styles[index].background.isValid()))
                {
                    matches = true;
                    break;
                }
            }
        }
    }
    if (m_selectionMatchesKeywordHighlight == matches)
    {
        return;
    }
    m_selectionMatchesKeywordHighlight = matches;
    emit selectionMatchesKeywordHighlightChanged();
}

void TerminalItem::selectWordAt(const terminal::TerminalPoint &point, const QPointF &position)
{
    if (!m_snapshot || point.row >= m_snapshot->rows || point.column >= m_snapshot->columns)
    {
        return;
    }

    quint16 column = point.column;
    while (column > 0 && m_snapshot->cell(column, point.row).displayWidth == 0)
    {
        --column;
    }
    const auto wordClass = [this](const terminal::TerminalCell &cell) {
        if (cell.grapheme.empty())
        {
            return 0;
        }
        const QString text = QString::fromUcs4(cell.grapheme.data(), static_cast<qsizetype>(cell.grapheme.size()));
        if (text.trimmed().isEmpty())
        {
            return 0;
        }
        return std::ranges::any_of(text,
                                   [this](const QChar character) {
                                       return m_wordDelimiters.contains(character);
                                   })
                   ? 1
                   : 2;
    };
    const int selectedClass = wordClass(m_snapshot->cell(column, point.row));
    quint16 first = column;
    while (first > 0)
    {
        auto previous = static_cast<quint16>(first - 1);
        while (previous > 0 && m_snapshot->cell(previous, point.row).displayWidth == 0)
        {
            --previous;
        }
        if (wordClass(m_snapshot->cell(previous, point.row)) != selectedClass)
        {
            break;
        }
        first = previous;
    }
    quint16 last = column;
    while (last + 1 < m_snapshot->columns)
    {
        quint16 next = static_cast<quint16>(last + std::max<int>(1, m_snapshot->cell(last, point.row).displayWidth));
        if (next >= m_snapshot->columns || wordClass(m_snapshot->cell(next, point.row)) != selectedClass)
        {
            break;
        }
        last = next;
    }
    const auto lastWidth = static_cast<quint16>(std::max<int>(1, m_snapshot->cell(last, point.row).displayWidth));
    const quint16 finalColumn = static_cast<quint16>(std::min<int>(m_snapshot->columns - 1, last + lastWidth - 1));
    m_selectionAnchor = {.column = first, .row = point.row};
    emit selectionRequested(first, point.row, finalColumn, point.row, false);
    setHasSelection(true);
    showSelectionAction(position, false);
    if (m_copyOnSelect)
    {
        emit copyRequested();
    }
}

void TerminalItem::selectLineAt(const quint16 row, const QPointF &position)
{
    if (!m_snapshot || row >= m_snapshot->rows || m_snapshot->columns == 0)
    {
        return;
    }
    m_selectionAnchor = {.column = 0, .row = row};
    emit selectionRequested(0, row, static_cast<quint16>(m_snapshot->columns - 1), row, false);
    setHasSelection(true);
    showSelectionAction(position, false);
    if (m_copyOnSelect)
    {
        emit copyRequested();
    }
}

terminal::TerminalSelectionGesture TerminalItem::selectionGesture(const terminal::TerminalSelectionGestureType type,
                                                                  const QPointF &position,
                                                                  const quint64 timestamp) const
{
    terminal::TerminalSelectionGesture result;
    result.type = type;
    result.positionX = position.x();
    result.positionY = position.y();
    result.columns = m_snapshot ? m_snapshot->columns : 0;
    result.cellWidthPixels = static_cast<quint32>(std::max<qreal>(1.0, std::ceil(cellWidth())));
    result.paddingLeftPixels = static_cast<quint32>(std::max<qreal>(0.0, std::ceil(horizontalPadding)));
    result.screenHeightPixels = static_cast<quint32>(std::max<qreal>(1.0, std::ceil(height())));
    result.eventTimeNanoseconds = timestamp * 1'000'000ULL;
    result.repeatIntervalNanoseconds =
        static_cast<quint64>(QGuiApplication::styleHints()->mouseDoubleClickInterval()) * 1'000'000ULL;
    result.repeatDistancePixels = QGuiApplication::styleHints()->startDragDistance();
    if (type == terminal::TerminalSelectionGestureType::drag
        || type == terminal::TerminalSelectionGestureType::autoscrollTick)
    {
        if (position.y() <= verticalPadding)
        {
            result.positionY = 0.0;
        }
        else if (position.y() >= height() - verticalPadding)
        {
            result.positionY = height();
        }
    }
    // Keep paths, URLs, host names, and command flags intact by default while
    // retaining the conventional punctuation boundaries used by terminals.
    const QList<uint> boundaryCodepoints = m_wordDelimiters.toUcs4();
    result.wordBoundaryCodepoints.reserve(static_cast<std::size_t>(boundaryCodepoints.size()));
    for (const uint codepoint : boundaryCodepoints)
    {
        result.wordBoundaryCodepoints.push_back(static_cast<char32_t>(codepoint));
    }
    if (const auto point = terminalPoint(position))
    {
        result.point = *point;
    }
    else
    {
        result.hasPoint = false;
    }
    return result;
}

void TerminalItem::updateSelectionAutoscroll(const QPointF &position)
{
    int direction = 0;
    if (position.y() < verticalPadding)
    {
        direction = -1;
    }
    else if (position.y() > height() - verticalPadding)
    {
        direction = 1;
    }
    if (direction == 0)
    {
        stopSelectionAutoscroll();
        return;
    }
    if (direction != m_selectionAutoscrollDirection)
    {
        m_selectionAutoscrollDirection = direction;
        m_selectionEdgeDwell.restart();
    }
    if (!m_selectionAutoscrollTimer.isActive())
    {
        m_selectionAutoscrollTimer.start();
    }
}

void TerminalItem::stopSelectionAutoscroll()
{
    m_selectionAutoscrollTimer.stop();
    m_selectionAutoscrollDirection = 0;
    m_selectionEdgeDwell.invalidate();
}

void TerminalItem::cancelSelectionGesture()
{
    if (!m_selecting)
    {
        stopSelectionAutoscroll();
        return;
    }
    stopSelectionAutoscroll();
    emit selectionGestureRequested(selectionGesture(terminal::TerminalSelectionGestureType::cancel, {}));
    m_selecting = false;
    m_extendingSelection = false;
    m_selectionMoved = false;
    m_selectionClickSelected = false;
}

bool TerminalItem::terminalOwnsMouse(const Qt::KeyboardModifiers &modifiers) const noexcept
{
    return m_snapshot && m_snapshot->mouseTrackingActive && !modifiers.testFlag(Qt::ShiftModifier);
}

terminal::TerminalMouseEvent TerminalItem::mouseEvent(const terminal::TerminalMouseAction action,
                                                      const terminal::TerminalMouseButton button,
                                                      const QPointF &position, const Qt::KeyboardModifiers modifiers,
                                                      const Qt::MouseButtons buttons) const
{
    terminal::TerminalMouseButton effectiveButton = button;
    if (effectiveButton == terminal::TerminalMouseButton::none)
    {
        if (buttons.testFlag(Qt::LeftButton))
        {
            effectiveButton = terminal::TerminalMouseButton::left;
        }
        else if (buttons.testFlag(Qt::RightButton))
        {
            effectiveButton = terminal::TerminalMouseButton::right;
        }
        else if (buttons.testFlag(Qt::MiddleButton))
        {
            effectiveButton = terminal::TerminalMouseButton::middle;
        }
    }
    return {.action = action,
            .button = effectiveButton,
            .modifiers = platform::windows::terminalModifiers(modifiers),
            .positionX = position.x(),
            .positionY = position.y(),
            .screenWidthPixels = static_cast<std::uint32_t>(std::max<qreal>(1.0, std::ceil(width()))),
            .screenHeightPixels = static_cast<std::uint32_t>(std::max<qreal>(1.0, std::ceil(height()))),
            .cellWidthPixels = static_cast<std::uint32_t>(std::max<qreal>(1.0, std::ceil(cellWidth()))),
            .cellHeightPixels = static_cast<std::uint32_t>(std::max<qreal>(1.0, std::ceil(cellHeight()))),
            .paddingTopPixels = static_cast<std::uint32_t>(verticalPadding),
            .paddingBottomPixels = static_cast<std::uint32_t>(verticalPadding),
            .paddingRightPixels = static_cast<std::uint32_t>(horizontalPadding),
            .paddingLeftPixels = static_cast<std::uint32_t>(horizontalPadding),
            .anyButtonPressed = buttons != Qt::NoButton};
}

void TerminalItem::reportFocus(const bool focused)
{
    const bool effective = focused && isVisible();
    if (m_lastReportedFocus == effective)
    {
        return;
    }
    m_lastReportedFocus = effective;
    emit focusEventGenerated(effective);
}

void TerminalItem::wheelEvent(QWheelEvent *event)
{
    const bool remoteMouse = terminalOwnsMouse(event->modifiers());
    const bool alternateScroll = m_snapshot && m_snapshot->alternateScrollActive && !remoteMouse;
    const auto emitRemoteSteps = [this, event, remoteMouse, alternateScroll](int steps) {
        steps = std::clamp(steps, -64, 64);
        if (remoteMouse)
        {
            const auto button = steps > 0 ? terminal::TerminalMouseButton::four : terminal::TerminalMouseButton::five;
            for (int index = 0; index < std::abs(steps); ++index)
            {
                emit mouseEventGenerated(mouseEvent(terminal::TerminalMouseAction::press, button, event->position(),
                                                    event->modifiers(), event->buttons()));
            }
            return;
        }
        if (alternateScroll)
        {
            terminal::TerminalKeyEvent key{.action = terminal::TerminalKeyAction::press,
                                           .key = steps > 0 ? terminal::TerminalKey::arrowUp
                                                            : terminal::TerminalKey::arrowDown,
                                           .modifiers = platform::windows::terminalModifiers(event->modifiers())};
            for (int index = 0; index < std::abs(steps * m_scrollRowsPerWheel); ++index)
            {
                emit keyEventGenerated(key);
            }
        }
    };

    if (!event->pixelDelta().isNull())
    {
        m_pixelWheelRemainder += event->pixelDelta().y();
        const qreal rowHeight = std::max<qreal>(cellHeight(), 1.0);
        const int rows = static_cast<int>(m_pixelWheelRemainder / rowHeight);
        m_pixelWheelRemainder -= rows * rowHeight;
        if (rows != 0)
        {
            if (remoteMouse || alternateScroll)
            {
                emitRemoteSteps(rows);
            }
            else
            {
                emit scrollRequested(-rows);
            }
        }
        event->accept();
        return;
    }
    m_wheelRemainder += event->angleDelta().y();
    const int steps = m_wheelRemainder / 120;
    m_wheelRemainder -= steps * 120;
    if (steps != 0)
    {
        if (remoteMouse || alternateScroll)
        {
            emitRemoteSteps(steps);
        }
        else
        {
            emit scrollRequested(-m_wheelAcceleration.scale(steps, event->timestamp()) * m_scrollRowsPerWheel);
        }
    }
    event->accept();
}

void TerminalItem::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData() != nullptr && (event->mimeData()->hasUrls() || event->mimeData()->hasText()))
    {
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void TerminalItem::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData() != nullptr && (event->mimeData()->hasUrls() || event->mimeData()->hasText()))
    {
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void TerminalItem::dragLeaveEvent(QDragLeaveEvent *event)
{
    event->accept();
}

void TerminalItem::dropEvent(QDropEvent *event)
{
    if (event->mimeData() == nullptr)
    {
        event->ignore();
        return;
    }
    QStringList localPaths;
    if (event->mimeData()->hasUrls())
    {
        for (const QUrl &url : event->mimeData()->urls())
        {
            if (url.isLocalFile())
            {
                localPaths.push_back(url.toLocalFile());
            }
        }
    }
    if (!localPaths.isEmpty())
    {
        emit localFilesDropped(localPaths);
        event->acceptProposedAction();
        return;
    }
    if (event->mimeData()->hasText())
    {
        requestPasteBytes(event->mimeData()->text().toUtf8());
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void TerminalItem::reportTerminalSize()
{
    const terminal::TerminalGeometry geometry = currentTerminalGeometry();
    const quint16 columns = geometry.columns;
    const quint16 rows = geometry.rows;
    if (columns == m_reportedColumns && rows == m_reportedRows)
    {
        return;
    }

    m_reportedColumns = columns;
    m_reportedRows = rows;
    emit sizeRequested(columns, rows, geometry.cellWidthPixels, geometry.cellHeightPixels);
}

terminal::TerminalGeometry TerminalItem::currentTerminalGeometry() const noexcept
{
    const qreal availableWidth = std::max(0.0, width() - (horizontalPadding * 2.0));
    const qreal availableHeight = std::max(0.0, height() - (verticalPadding * 2.0));
    const auto columns = static_cast<quint16>(std::clamp(std::floor(availableWidth / cellWidth()), 1.0,
                                                         static_cast<double>(std::numeric_limits<quint16>::max())));
    const auto rows = static_cast<quint16>(std::clamp(std::floor(availableHeight / cellHeight()), 1.0,
                                                      static_cast<double>(std::numeric_limits<quint16>::max())));
    return {.columns = columns,
            .rows = rows,
            .cellWidthPixels = static_cast<quint32>(std::ceil(cellWidth())),
            .cellHeightPixels = static_cast<quint32>(std::ceil(cellHeight()))};
}

std::optional<terminal::TerminalPoint> TerminalItem::terminalPoint(const QPointF &position) const
{
    if (!m_snapshot || m_snapshot->columns == 0 || m_snapshot->rows == 0)
    {
        return std::nullopt;
    }

    const qreal columnValue = std::floor((position.x() - horizontalPadding) / cellWidth());
    const qreal rowValue = std::floor((position.y() - verticalPadding) / cellHeight());
    return terminal::TerminalPoint{
        .column = static_cast<quint16>(std::clamp(columnValue, 0.0, static_cast<qreal>(m_snapshot->columns - 1))),
        .row = static_cast<quint16>(std::clamp(rowValue, 0.0, static_cast<qreal>(m_snapshot->rows - 1)))};
}

std::uint32_t TerminalItem::hyperlinkAt(const QPointF &position) const
{
    const auto point = terminalPoint(position);
    return !point || !m_snapshot ? 0 : m_snapshot->cell(point->column, point->row).hyperlinkId;
}

void TerminalItem::updateHoveredLink(const QPointF &position, const Qt::KeyboardModifiers modifiers)
{
    const std::uint32_t id = hyperlinkAt(position);
    if (id != m_hoveredLinkId)
    {
        m_hoveredLinkId = id;
        emit hoveredLinkChanged();
        invalidateRenderer(true);
    }
    if (id != 0 && modifiers.testFlag(Qt::ControlModifier))
    {
        setCursor(Qt::PointingHandCursor);
    }
    else
    {
        unsetCursor();
    }
}

void TerminalItem::clearHoveredLink()
{
    unsetCursor();
    m_pressedLinkId = 0;
    if (m_hoveredLinkId == 0)
    {
        return;
    }
    m_hoveredLinkId = 0;
    emit hoveredLinkChanged();
    invalidateRenderer(true);
}

bool TerminalItem::handleQuickSelectKey(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
    {
        cancelQuickSelect();
        return true;
    }
    if (event->key() == Qt::Key_Backspace)
    {
        if (!m_quickSelectInput.isEmpty())
        {
            m_quickSelectInput.chop(1);
            invalidateRenderer(true);
        }
        return true;
    }

    const TerminalQuickSelectTarget *target = nullptr;
    if (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return)
    {
        const auto found = std::ranges::find_if(m_quickSelectTargets, [this](const TerminalQuickSelectTarget &value) {
            return value.label.startsWith(m_quickSelectInput, Qt::CaseInsensitive);
        });
        if (found != m_quickSelectTargets.end())
        {
            const auto another = std::ranges::find_if(
                std::next(found), m_quickSelectTargets.end(), [this](const TerminalQuickSelectTarget &value) {
                    return value.label.startsWith(m_quickSelectInput, Qt::CaseInsensitive);
                });
            if (another == m_quickSelectTargets.end())
            {
                target = &*found;
            }
        }
    }
    else if (event->key() >= Qt::Key_A && event->key() <= Qt::Key_Z)
    {
        m_quickSelectInput.append(QChar(static_cast<char16_t>('a' + event->key() - Qt::Key_A)));
        const auto found = std::ranges::find_if(m_quickSelectTargets, [this](const TerminalQuickSelectTarget &value) {
            return value.label.compare(m_quickSelectInput, Qt::CaseInsensitive) == 0;
        });
        if (found != m_quickSelectTargets.end())
        {
            target = &*found;
        }
        else if (!std::ranges::any_of(m_quickSelectTargets, [this](const TerminalQuickSelectTarget &value) {
                     return value.label.startsWith(m_quickSelectInput, Qt::CaseInsensitive);
                 }))
        {
            m_quickSelectInput.chop(1);
        }
        invalidateRenderer(true);
    }
    else
    {
        return true;
    }

    if (target != nullptr)
    {
        const TerminalQuickSelectTarget selected = *target;
        cancelQuickSelect();
        activateQuickSelectTarget(selected, event->modifiers());
    }
    return true;
}

bool TerminalItem::handleCopyModeKey(QKeyEvent *event)
{
    using Action = terminal::TerminalCopyModeAction;
    using ActionType = terminal::TerminalCopyModeActionType;
    using Motion = terminal::TerminalCopyModeMotion;

    if (event->key() == Qt::Key_Escape)
    {
        cancelCopyMode();
        return true;
    }
    if (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return || event->key() == Qt::Key_Y)
    {
        copySelection();
        cancelCopyMode();
        return true;
    }
    if (event->key() == Qt::Key_O)
    {
        emit copyModeActionRequested(Action{.type = ActionType::switchEndpoint});
        return true;
    }
    if (event->key() == Qt::Key_V)
    {
        ActionType type = ActionType::selectCharacter;
        if (event->modifiers().testFlag(Qt::ControlModifier))
        {
            type = ActionType::selectRectangle;
        }
        else if (event->modifiers().testFlag(Qt::ShiftModifier))
        {
            type = ActionType::selectLine;
        }
        emit copyModeActionRequested(Action{.type = type});
        return true;
    }

    std::optional<Motion> motion;
    const bool control = event->modifiers().testFlag(Qt::ControlModifier);
    switch (event->key())
    {
        case Qt::Key_Left:
            motion = control ? Motion::wordLeft : Motion::left;
            break;
        case Qt::Key_Right:
            motion = control ? Motion::wordRight : Motion::right;
            break;
        case Qt::Key_Up:
            motion = Motion::up;
            break;
        case Qt::Key_Down:
            motion = Motion::down;
            break;
        case Qt::Key_Home:
            motion = control ? Motion::top : Motion::lineStart;
            break;
        case Qt::Key_End:
            motion = control ? Motion::bottom : Motion::lineEnd;
            break;
        case Qt::Key_PageUp:
            motion = Motion::pageUp;
            break;
        case Qt::Key_PageDown:
            motion = Motion::pageDown;
            break;
        default:
            return true;
    }
    emit copyModeActionRequested(
        Action{.type = ActionType::move, .motion = *motion, .extend = event->modifiers().testFlag(Qt::ShiftModifier)});
    return true;
}

void TerminalItem::activateQuickSelectTarget(const TerminalQuickSelectTarget &target,
                                             const Qt::KeyboardModifiers modifiers)
{
    if (modifiers.testFlag(Qt::ControlModifier) && !target.uri.isEmpty())
    {
        emit linkActivated(target.uri);
        return;
    }
    if (modifiers.testFlag(Qt::ShiftModifier))
    {
        emit pasteRequested(target.value.toUtf8());
        return;
    }
    QGuiApplication::clipboard()->setText(target.uri.isEmpty() ? target.value : target.uri);
}

QRectF TerminalItem::inputCursorRectangle() const
{
    qreal cursorX = horizontalPadding;
    qreal cursorY = verticalPadding;
    if (m_snapshot)
    {
        cursorX += m_snapshot->cursor.column * cellWidth();
        cursorY += m_snapshot->cursor.row * cellHeight();
    }
    if (!m_preeditText.isEmpty())
    {
        const std::vector<PreeditCluster> clusters = layoutPreeditText(m_preeditText, m_font, cellWidth());
        const PreeditCursorCell cursorCell = preeditCursorCell(clusters, m_preeditCursorPosition);
        cursorX += cursorCell.column * cellWidth();
        return {cursorX, cursorY, cursorCell.width * cellWidth(), cellHeight()};
    }
    const qreal cursorWidth = m_snapshot ? m_snapshot->cursor.width * cellWidth() : cellWidth();
    return {cursorX, cursorY, cursorWidth, cellHeight()};
}

void TerminalItem::clearPreedit()
{
    if (m_preeditText.isEmpty())
    {
        return;
    }
    m_preeditText.clear();
    m_preeditCursorPosition = 0;
    m_preeditCursorVisible = true;
    invalidateRenderer(true);
}

void TerminalItem::invalidateRenderer(const bool full)
{
    m_fullInvalidationPending = m_fullInvalidationPending || full;
    ++m_revision;
    update();
}

void TerminalItem::notifyInputMethod() const
{
    if (hasActiveFocus() && QGuiApplication::inputMethod() != nullptr)
    {
        QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle | Qt::ImSurroundingText | Qt::ImCursorPosition
                                               | Qt::ImAnchorPosition);
    }
}

terminal::TerminalCursorStyle TerminalItem::effectiveCursorStyle() const noexcept
{
    if (m_cursorPreference == QStringLiteral("block"))
    {
        return terminal::TerminalCursorStyle::block;
    }
    if (m_cursorPreference == QStringLiteral("bar"))
    {
        return terminal::TerminalCursorStyle::bar;
    }
    if (m_cursorPreference == QStringLiteral("underline"))
    {
        return terminal::TerminalCursorStyle::underline;
    }
    return m_snapshot ? m_snapshot->cursor.style : terminal::TerminalCursorStyle::block;
}

void TerminalItem::refreshFontMetrics()
{
    const QFontMetricsF metrics(m_font);
    m_cellWidth = metrics.horizontalAdvance(QLatin1Char('M'));
    m_cellHeight = std::ceil(metrics.height());
    m_fontAscent = metrics.ascent();
    m_styledFontsReady.reset();
}

const QFont &TerminalItem::styledFont(const std::size_t styleBits)
{
    QFont &font = m_styledFonts[styleBits];
    if (!m_styledFontsReady.test(styleBits))
    {
        font = m_font;
        font.setBold((styleBits & styleBold) != 0);
        font.setItalic((styleBits & styleItalic) != 0);
        font.setUnderline((styleBits & styleUnderline) != 0);
        font.setStrikeOut((styleBits & styleStrikeOut) != 0);
        font.setOverline((styleBits & styleOverline) != 0);
        m_styledFontsReady.set(styleBits);
    }
    return font;
}

void TerminalItem::refreshKeywordStyles()
{
    m_keywordStyles = m_snapshot ? highlightTerminalKeywords(*m_snapshot, m_keywordHighlightRules)
                                 : std::vector<TerminalKeywordCellStyle>{};
}

void TerminalItem::refreshSearchStyles()
{
    m_searchStylesDirty = false;
    m_searchStyles.clear();
    if (!m_snapshot || m_searchQuery.isEmpty() || !m_searchMatchBackground.isValid())
    {
        return;
    }
    const std::vector<TerminalKeywordRule> searchRules{TerminalKeywordRule{
        .id = QStringLiteral("terminal-search"),
        .pattern = m_searchQuery,
        .background = m_searchMatchBackground,
        .enabled = true,
        .caseSensitive = m_searchCaseSensitive,
    }};
    m_searchStyles = highlightTerminalKeywords(*m_snapshot, searchRules);
}

} // namespace ztermy::ui
