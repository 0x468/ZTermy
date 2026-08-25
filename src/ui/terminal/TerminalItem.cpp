#include "ui/terminal/TerminalItem.h"

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
#include <QHoverEvent>
#include <QImage>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
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

Q_LOGGING_CATEGORY(terminalRenderLog, "ztermy.terminal.render")

constexpr qreal horizontalPadding = 16.0;
constexpr qreal verticalPadding = 14.0;

class TerminalTextureNode final : public QSGSimpleTextureNode
{
public:
#if !defined(NDEBUG)
    void recordTiming(const qint64 paintNanoseconds, const qint64 textureNanoseconds, const QSize &frameSize,
                      const ztermy::terminal::TerminalDamageKind damage, const std::size_t damagedRowCount)
    {
        paintTimes[sampleCount] = paintNanoseconds;
        textureTimes[sampleCount] = textureNanoseconds;
        ++sampleCount;
        damagedRows += damagedRowCount;
        fullFrames += damage == ztermy::terminal::TerminalDamageKind::full ? 1U : 0U;
        partialFrames += damage == ztermy::terminal::TerminalDamageKind::partial ? 1U : 0U;
        cleanFrames += damage == ztermy::terminal::TerminalDamageKind::none ? 1U : 0U;
        if (sampleCount != paintTimes.size())
        {
            return;
        }

        auto sortedPaintTimes = paintTimes;
        auto sortedTextureTimes = textureTimes;
        std::ranges::sort(sortedPaintTimes);
        std::ranges::sort(sortedTextureTimes);
        constexpr std::size_t percentile95Index = ((timingSampleCount * 95U) - 1U) / 100U;
        constexpr double nanosecondsPerMillisecond = 1'000'000.0;
        qCDebug(terminalRenderLog)
            << "renderer timing"
            << "samples=" << timingSampleCount << "pixels=" << frameSize
            << "paintP95Ms=" << (static_cast<double>(sortedPaintTimes[percentile95Index]) / nanosecondsPerMillisecond)
            << "textureCreateP95Ms="
            << (static_cast<double>(sortedTextureTimes[percentile95Index]) / nanosecondsPerMillisecond)
            << "fullFrames=" << fullFrames << "partialFrames=" << partialFrames << "cleanFrames=" << cleanFrames
            << "damagedRows=" << damagedRows;

        sampleCount = 0;
        damagedRows = 0;
        fullFrames = 0;
        partialFrames = 0;
        cleanFrames = 0;
    }
#endif

    std::uint64_t revision = 0;
    QSize pixelSize;
    QImage image;
    ztermy::terminal::TerminalSnapshotPtr previousDiagnosticSnapshot;
    std::vector<ztermy::ui::TerminalKeywordCellStyle> keywordStyles;
    QSGSimpleTextureNode *cursorNode = nullptr;
    QRectF cursorRect;

private:
#if !defined(NDEBUG)
    static constexpr std::size_t timingSampleCount = 120;
    std::array<qint64, timingSampleCount> paintTimes{};
    std::array<qint64, timingSampleCount> textureTimes{};
    std::size_t sampleCount = 0;
    std::size_t damagedRows = 0;
    std::size_t fullFrames = 0;
    std::size_t partialFrames = 0;
    std::size_t cleanFrames = 0;
#endif
};

[[nodiscard]] QColor color(const ztermy::terminal::TerminalColor terminalColor)
{
    return {terminalColor.red, terminalColor.green, terminalColor.blue};
}

[[nodiscard]] bool sameColor(const ztermy::terminal::TerminalColor left,
                             const ztermy::terminal::TerminalColor right) noexcept
{
    return left.red == right.red && left.green == right.green && left.blue == right.blue;
}

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

[[nodiscard]] bool ligatureRunCell(const ztermy::terminal::TerminalCell &cell)
{
    return !cell.invisible && cell.displayWidth == 1 && cell.grapheme.size() == 1 && cell.grapheme.front() >= U'!'
           && cell.grapheme.front() <= U'~';
}

[[nodiscard]] bool sameTextStyle(const ztermy::terminal::TerminalCell &first,
                                 const ztermy::terminal::TerminalCell &second)
{
    return first.selected == second.selected && first.bold == second.bold && first.italic == second.italic
           && first.underline == second.underline && first.strikethrough == second.strikethrough
           && first.overline == second.overline && first.hyperlinkId == second.hyperlinkId
           && color(first.foreground) == color(second.foreground);
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

    m_cursorBlinkTimer.setInterval(530);
    QObject::connect(&m_cursorBlinkTimer, &QTimer::timeout, this, [this] {
        if (!isVisible() || !m_snapshot || !m_snapshot->cursor.visible)
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

bool TerminalItem::confirmMultilinePaste() const noexcept
{
    return m_confirmMultilinePaste;
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

bool TerminalItem::scrollbarVisible() const noexcept
{
    return m_snapshot && m_snapshot->scrollbar.total > m_snapshot->scrollbar.visible;
}

qreal TerminalItem::scrollbarPosition() const noexcept
{
    if (!scrollbarVisible())
    {
        return 1.0;
    }
    const std::uint64_t maximumOffset = m_snapshot->scrollbar.total - m_snapshot->scrollbar.visible;
    return maximumOffset == 0 ? 1.0
                              : static_cast<qreal>(m_snapshot->scrollbar.offset) / static_cast<qreal>(maximumOffset);
}

qreal TerminalItem::scrollbarPageRatio() const noexcept
{
    if (!m_snapshot || m_snapshot->scrollbar.total == 0)
    {
        return 1.0;
    }
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

QVariantList TerminalItem::keywordHighlightRules() const
{
    return m_keywordHighlightRuleValues;
}

QColor TerminalItem::foregroundOverride() const
{
    return m_foregroundOverride;
}

QColor TerminalItem::backgroundOverride() const
{
    return m_backgroundOverride;
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
    if (m_quickSelectActive)
    {
        cancelQuickSelect();
    }
    if (!snapshot)
    {
        cancelSelectionGesture();
        dismissSelectionAction();
        setHasSelection(false);
        m_snapshot.reset();
        clearHoveredLink();
        invalidateRenderer(true);
        notifyInputMethod();
        emit scrollbarChanged();
        return;
    }
    const bool focusReportingBecameActive =
        snapshot->focusReportingActive && (!m_snapshot || !m_snapshot->focusReportingActive);
    m_renderMetrics.recordSnapshot(snapshot->damage, snapshot->damagedRows.size());
    const bool selectionBecameVisible = !m_hasSelection && snapshot->selectionPresent;
    setHasSelection(snapshot->selectionPresent);
    if (!snapshot->selectionPresent)
    {
        dismissSelectionAction();
    }
    else if (selectionBecameVisible && !m_selecting)
    {
        m_selectionActionPosition = m_selectionPointerPosition;
        m_selectionActionVisible = true;
        emit selectionActionChanged();
    }
    m_snapshot = std::move(snapshot);
    if (m_hoverInside)
    {
        updateHoveredLink(m_hoverPosition, QGuiApplication::keyboardModifiers());
    }
    if (focusReportingBecameActive)
    {
        m_lastReportedFocus.reset();
        reportFocus(hasActiveFocus());
    }
    invalidateRenderer(true);
    notifyInputMethod();
    emit scrollbarChanged();
}

QString TerminalItem::hoveredLink() const
{
    if (!m_snapshot)
    {
        return {};
    }
    const terminal::TerminalHyperlink *link = m_snapshot->hyperlink(m_hoveredLinkId);
    return link == nullptr ? QString{} : QString::fromUtf8(link->uri);
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
    {
        return;
    }
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
    {
        return;
    }
    m_font.setFamilies({normalized, QStringLiteral("Consolas")});
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
    {
        return;
    }
    m_font.setPixelSize(pixelSize);
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

void TerminalItem::setCopyOnSelect(const bool enabled)
{
    if (m_copyOnSelect == enabled)
    {
        return;
    }
    m_copyOnSelect = enabled;
    emit copyOnSelectChanged();
}

void TerminalItem::setConfirmMultilinePaste(const bool enabled)
{
    if (m_confirmMultilinePaste == enabled)
    {
        return;
    }
    m_confirmMultilinePaste = enabled;
    if (!enabled)
    {
        m_pendingMultilinePaste.clear();
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
    invalidateRenderer(true);
    emit keywordHighlightRulesChanged();
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

void TerminalItem::resolveMultilinePaste(const bool accepted)
{
    QByteArray pending = std::move(m_pendingMultilinePaste);
    m_pendingMultilinePaste.clear();
    if (accepted && !pending.isEmpty())
    {
        emit pasteRequested(pending);
    }
}

void TerminalItem::scrollToFraction(const qreal fraction)
{
    if (!scrollbarVisible())
    {
        return;
    }
    const std::uint64_t maximumOffset = m_snapshot->scrollbar.total - m_snapshot->scrollbar.visible;
    const qreal normalized = std::clamp(fraction, 0.0, 1.0);
    const std::uint64_t targetOffset =
        normalized <= 0.0   ? 0
        : normalized >= 1.0 ? maximumOffset
                            : static_cast<std::uint64_t>(std::llround(normalized * static_cast<qreal>(maximumOffset)));
    const std::uint64_t currentOffset = m_snapshot->scrollbar.offset;
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
    emit copyRequested();
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
        m_pendingMultilinePaste = bytes;
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

QSGNode *TerminalItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *node = static_cast<TerminalTextureNode *>(oldNode);
    if (node == nullptr)
    {
        node = new TerminalTextureNode;
        node->setOwnsTexture(true);
    }

    if (window() == nullptr || width() <= 0 || height() <= 0)
    {
        return node;
    }

    const qreal devicePixelRatio = window()->effectiveDevicePixelRatio();
    const QSize pixelSize{std::max(1, qRound(width() * devicePixelRatio)),
                          std::max(1, qRound(height() * devicePixelRatio))};
    if (node->revision == m_revision)
    {
        node->setRect(boundingRect());
        return node;
    }

    QElapsedTimer frameTimer;
#if !defined(NDEBUG)
    constexpr bool debugTimingEnabled = true;
#else
    constexpr bool debugTimingEnabled = false;
#endif
    const bool timingEnabled = debugTimingEnabled || m_renderMetrics.enabled();
    if (timingEnabled)
    {
        frameTimer.start();
    }

    const terminal::TerminalColor fallbackForeground{.red = 248, .green = 250, .blue = 252};
    const terminal::TerminalColor fallbackBackground{.red = 11, .green = 16, .blue = 23};
    const QColor selectionBackground(42, 91, 145);
    const QColor selectionForeground(255, 255, 255);
    const terminal::TerminalColor defaultForeground = m_snapshot ? m_snapshot->defaultForeground : fallbackForeground;
    const terminal::TerminalColor defaultBackground = m_snapshot ? m_snapshot->defaultBackground : fallbackBackground;

    QColor defaultBackgroundColor = m_backgroundOverride.isValid() ? m_backgroundOverride : color(defaultBackground);
    defaultBackgroundColor.setAlphaF(static_cast<float>(m_backgroundOpacity));
    const bool cursorOnlyPaint = !m_fullInvalidationPending && !node->image.isNull() && node->pixelSize == pixelSize
                                 && m_snapshot && m_snapshot->cursor.row < m_snapshot->rows;
    if (cursorOnlyPaint && m_preeditText.isEmpty())
    {
        if (node->cursorNode != nullptr)
        {
            node->cursorNode->setRect((!m_cursorBlink || m_cursorBlinkPhase) ? node->cursorRect : QRectF{});
        }
        const qint64 paintNanoseconds = timingEnabled ? frameTimer.nsecsElapsed() : 0;
        if (m_renderMetrics.enabled())
        {
            m_renderMetrics.recordFrame(std::chrono::nanoseconds{paintNanoseconds}, std::chrono::nanoseconds{0}, 0,
                                        terminal::TerminalDamageKind::partial, 1);
        }
#if !defined(NDEBUG)
        node->recordTiming(paintNanoseconds, 0, pixelSize, terminal::TerminalDamageKind::partial, 1);
#endif
        node->revision = m_revision;
        m_fullInvalidationPending = false;
        return node;
    }
    static const bool paintPhaseDiagnosticEnabled =
        qEnvironmentVariableIntValue("ZTERMY_PERFORMANCE_PAINT_PHASE_DIAGNOSTIC") == 1;
    const bool collectPaintPhases = paintPhaseDiagnosticEnabled && m_renderMetrics.enabled() && !cursorOnlyPaint;
    qint64 phaseMarkNanoseconds = collectPaintPhases ? frameTimer.nsecsElapsed() : 0;
    qint64 imagePreparationNanoseconds = 0;
    qint64 snapshotPreparationNanoseconds = 0;
    qint64 backgroundPaintNanoseconds = 0;
    qint64 textPaintNanoseconds = 0;
    qint64 overlayPaintNanoseconds = 0;
    if (!cursorOnlyPaint)
    {
        static const bool rowReuseDiagnosticEnabled =
            qEnvironmentVariableIntValue("ZTERMY_PERFORMANCE_ROW_REUSE_DIAGNOSTIC") == 1;
        if (rowReuseDiagnosticEnabled && m_renderMetrics.enabled() && m_snapshot && node->previousDiagnosticSnapshot)
        {
            const TerminalRowReuseAnalysis analysis =
                analyzeTerminalRowReuse(*node->previousDiagnosticSnapshot, *m_snapshot);
            m_renderMetrics.recordRowReuse(analysis.totalRows, analysis.reusableRows, analysis.shifted());
        }
        node->previousDiagnosticSnapshot = rowReuseDiagnosticEnabled ? m_snapshot : nullptr;
        node->image = QImage(pixelSize, QImage::Format_ARGB32_Premultiplied);
        node->image.setDevicePixelRatio(devicePixelRatio);
        node->image.fill(defaultBackgroundColor);
    }
    if (collectPaintPhases)
    {
        const qint64 now = frameTimer.nsecsElapsed();
        imagePreparationNanoseconds = now - phaseMarkNanoseconds;
        phaseMarkNanoseconds = now;
    }
    QImage &image = node->image;

    if (m_snapshot)
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::TextAntialiasing);
        const qreal cellWidthValue = cellWidth();
        const qreal cellHeightValue = cellHeight();
        const QFontMetricsF metrics(m_font);
        const std::vector<PreeditCluster> preeditClusters = layoutPreeditText(m_preeditText, m_font, cellWidthValue);
        const int insertedColumns = preeditColumnCount(preeditClusters);
        const int snapshotColumns = static_cast<int>(m_snapshot->columns);
        const bool terminalCursorPresent = preeditClusters.empty() && m_snapshot->cursor.visible
                                           && m_snapshot->cursor.column < m_snapshot->columns
                                           && m_snapshot->cursor.row < m_snapshot->rows;
        if (!cursorOnlyPaint)
        {
            node->keywordStyles = highlightTerminalKeywords(*m_snapshot, m_keywordHighlightRules);
        }
        const std::vector<TerminalKeywordCellStyle> &keywordStyles = node->keywordStyles;
        const quint16 firstRow = cursorOnlyPaint ? m_snapshot->cursor.row : 0;
        const quint16 lastRow = cursorOnlyPaint ? static_cast<quint16>(firstRow + 1) : m_snapshot->rows;
        if (collectPaintPhases)
        {
            const qint64 now = frameTimer.nsecsElapsed();
            snapshotPreparationNanoseconds = now - phaseMarkNanoseconds;
            phaseMarkNanoseconds = now;
        }
        if (cursorOnlyPaint)
        {
            painter.fillRect(QRectF{0.0, verticalPadding + (firstRow * cellHeightValue), width(), cellHeightValue},
                             defaultBackgroundColor);
        }

        for (quint16 row = firstRow; row < lastRow; ++row)
        {
            for (quint16 column = 0; column < m_snapshot->columns; ++column)
            {
                const terminal::TerminalCell &cell = m_snapshot->cell(column, row);
                const int displayColumn =
                    !preeditClusters.empty() && row == m_snapshot->cursor.row
                        ? shiftedTerminalColumn(column, m_snapshot->cursor.column, insertedColumns)
                        : column;
                if (displayColumn >= snapshotColumns)
                {
                    continue;
                }
                const QRectF cellRect{
                    horizontalPadding + (displayColumn * cellWidthValue),
                    verticalPadding + (row * cellHeightValue),
                    std::max<qreal>(1.0, cell.displayWidth) * cellWidthValue,
                    cellHeightValue,
                };
                if (cell.selected)
                {
                    painter.fillRect(cellRect, selectionBackground);
                }
                else if (const TerminalKeywordCellStyle &style =
                             keywordStyles[(static_cast<std::size_t>(row) * m_snapshot->columns) + column];
                         style.background.isValid())
                {
                    painter.fillRect(cellRect, style.background);
                }
                else if (cell.explicitBackground)
                {
                    painter.fillRect(cellRect, color(cell.background));
                }
            }
        }
        if (collectPaintPhases)
        {
            const qint64 now = frameTimer.nsecsElapsed();
            backgroundPaintNanoseconds = now - phaseMarkNanoseconds;
            phaseMarkNanoseconds = now;
        }

        for (quint16 row = firstRow; row < lastRow; ++row)
        {
            for (quint16 column = 0; column < m_snapshot->columns; ++column)
            {
                const terminal::TerminalCell &cell = m_snapshot->cell(column, row);
                if (cell.grapheme.empty() || cell.invisible)
                {
                    continue;
                }
                const int displayColumn =
                    !preeditClusters.empty() && row == m_snapshot->cursor.row
                        ? shiftedTerminalColumn(column, m_snapshot->cursor.column, insertedColumns)
                        : column;
                if (displayColumn >= snapshotColumns)
                {
                    continue;
                }

                QFont cellFont = m_font;
                cellFont.setBold(cell.bold);
                cellFont.setItalic(cell.italic);
                cellFont.setUnderline(cell.underline || (cell.hyperlinkId != 0 && cell.hyperlinkId == m_hoveredLinkId));
                cellFont.setStrikeOut(cell.strikethrough);
                cellFont.setOverline(cell.overline);
                painter.setFont(cellFont);
                const TerminalKeywordCellStyle &keywordStyle =
                    keywordStyles[(static_cast<std::size_t>(row) * m_snapshot->columns) + column];
                const QColor cellForeground =
                    m_foregroundOverride.isValid() && sameColor(cell.foreground, defaultForeground)
                        ? m_foregroundOverride
                        : color(cell.foreground);
                painter.setPen(cell.selected                       ? selectionForeground
                               : keywordStyle.foreground.isValid() ? keywordStyle.foreground
                                                                   : cellForeground);

                QString grapheme =
                    QString::fromUcs4(cell.grapheme.data(), static_cast<qsizetype>(cell.grapheme.size()));
                quint16 runEnd = column;
                int previousDisplayColumn = displayColumn;
                const bool currentCellHasCursor =
                    terminalCursorPresent && row == m_snapshot->cursor.row && column == m_snapshot->cursor.column;
                if (m_ligaturesEnabled && ligatureRunCell(cell) && !currentCellHasCursor)
                {
                    while (runEnd + 1 < m_snapshot->columns)
                    {
                        const terminal::TerminalCell &next = m_snapshot->cell(runEnd + 1, row);
                        const bool nextCellHasCursor = terminalCursorPresent && row == m_snapshot->cursor.row
                                                       && runEnd + 1 == m_snapshot->cursor.column;
                        const int nextDisplayColumn =
                            !preeditClusters.empty() && row == m_snapshot->cursor.row
                                ? shiftedTerminalColumn(runEnd + 1, m_snapshot->cursor.column, insertedColumns)
                                : runEnd + 1;
                        if (nextDisplayColumn >= snapshotColumns || nextDisplayColumn != previousDisplayColumn + 1
                            || nextCellHasCursor || !ligatureRunCell(next) || !sameTextStyle(cell, next))
                        {
                            break;
                        }
                        grapheme.append(QChar(static_cast<char16_t>(next.grapheme.front())));
                        ++runEnd;
                        previousDisplayColumn = nextDisplayColumn;
                    }
                }
                const QPointF baseline{horizontalPadding + (displayColumn * cellWidthValue),
                                       verticalPadding + (row * cellHeightValue) + metrics.ascent()};
                painter.drawText(baseline, grapheme);
                column = runEnd;
            }
        }
        if (!cursorOnlyPaint && m_quickSelectActive)
        {
            QFont labelFont = m_font;
            labelFont.setBold(true);
            labelFont.setPixelSize(std::max(10, m_font.pixelSize() - 1));
            painter.setFont(labelFont);
            const QFontMetricsF labelMetrics(labelFont);
            for (const TerminalQuickSelectTarget &target : m_quickSelectTargets)
            {
                if (!target.label.startsWith(m_quickSelectInput, Qt::CaseInsensitive))
                {
                    continue;
                }
                const QRectF targetRect{
                    horizontalPadding + (target.startColumn * cellWidthValue),
                    verticalPadding + (target.row * cellHeightValue),
                    std::max<qreal>(cellWidthValue, (target.endColumn - target.startColumn) * cellWidthValue),
                    cellHeightValue,
                };
                painter.fillRect(targetRect, QColor(34, 197, 94, 52));
                const qreal labelWidth = labelMetrics.horizontalAdvance(target.label) + 8.0;
                const QRectF labelRect{targetRect.left(), targetRect.top(), labelWidth, cellHeightValue};
                painter.fillRect(labelRect, QColor(15, 23, 42, 235));
                painter.setPen(QColor(248, 250, 252));
                painter.drawText(QPointF(labelRect.left() + 4.0, labelRect.top() + labelMetrics.ascent()),
                                 target.label);
            }
        }
        if (!cursorOnlyPaint && m_copyModeActive)
        {
            QFont modeFont = m_font;
            modeFont.setBold(true);
            modeFont.setPixelSize(std::max(10, m_font.pixelSize() - 2));
            painter.setFont(modeFont);
            const QString label = tr("COPY MODE  ·  Esc cancel  ·  Y copy");
            const QFontMetricsF modeMetrics(modeFont);
            const QSizeF labelSize{modeMetrics.horizontalAdvance(label) + 18.0, modeMetrics.height() + 8.0};
            const QRectF labelRect{std::max<qreal>(horizontalPadding, width() - horizontalPadding - labelSize.width()),
                                   verticalPadding, labelSize.width(), labelSize.height()};
            painter.setPen(QColor(74, 222, 128));
            painter.setBrush(QColor(15, 23, 42, 230));
            painter.drawRoundedRect(labelRect, 6.0, 6.0);
            painter.drawText(QPointF(labelRect.left() + 9.0, labelRect.top() + 4.0 + modeMetrics.ascent()), label);
        }
        if (collectPaintPhases)
        {
            const qint64 now = frameTimer.nsecsElapsed();
            textPaintNanoseconds = now - phaseMarkNanoseconds;
            phaseMarkNanoseconds = now;
        }

        if (!preeditClusters.empty() && m_snapshot->cursor.column < m_snapshot->columns
            && m_snapshot->cursor.row < m_snapshot->rows)
        {
            const qreal compositionLeft = horizontalPadding + (m_snapshot->cursor.column * cellWidthValue);
            const qreal compositionTop = verticalPadding + (m_snapshot->cursor.row * cellHeightValue);
            QFont compositionFont = m_font;
            compositionFont.setUnderline(true);
            painter.setFont(compositionFont);
            const PreeditCursorCell cursorCell = preeditCursorCell(preeditClusters, m_preeditCursorPosition);
            for (const PreeditCluster &cluster : preeditClusters)
            {
                const QRectF clusterRect{
                    compositionLeft + (cluster.column * cellWidthValue),
                    compositionTop,
                    cluster.width * cellWidthValue,
                    cellHeightValue,
                };
                painter.fillRect(clusterRect, QColor(42, 91, 145, 180));
                painter.setPen(QColor(255, 255, 255));
                painter.drawText(QPointF(clusterRect.left(), compositionTop + metrics.ascent()), cluster.text);
            }
            if (m_preeditCursorVisible && (!m_cursorBlink || m_cursorBlinkPhase))
            {
                painter.fillRect(QRectF(compositionLeft + (cursorCell.column * cellWidthValue), compositionTop, 2.0,
                                        cellHeightValue),
                                 QColor(255, 255, 255));
            }
        }
        else if (terminalCursorPresent && !cursorOnlyPaint)
        {
            node->cursorRect = QRectF{horizontalPadding + (m_snapshot->cursor.column * cellWidthValue),
                                      verticalPadding + (m_snapshot->cursor.row * cellHeightValue),
                                      m_snapshot->cursor.width * cellWidthValue, cellHeightValue};
            const QSize cursorPixelSize{
                std::max(1, qRound(node->cursorRect.width() * devicePixelRatio)),
                std::max(1, qRound(node->cursorRect.height() * devicePixelRatio)),
            };
            QImage cursorImage(cursorPixelSize, QImage::Format_ARGB32_Premultiplied);
            cursorImage.setDevicePixelRatio(devicePixelRatio);
            cursorImage.fill(Qt::transparent);
            QPainter cursorPainter(&cursorImage);
            cursorPainter.setRenderHint(QPainter::TextAntialiasing);
            const QRectF cursorCell{0.0, 0.0, node->cursorRect.width(), node->cursorRect.height()};
            cursorPainter.setPen(QPen(color(m_snapshot->cursor.color), 1.0));
            switch (effectiveCursorStyle())
            {
                case terminal::TerminalCursorStyle::bar:
                    cursorPainter.fillRect(QRectF(cursorCell.left(), cursorCell.top(), 2.0, cursorCell.height()),
                                           color(m_snapshot->cursor.color));
                    break;
                case terminal::TerminalCursorStyle::underline:
                    cursorPainter.fillRect(
                        QRectF(cursorCell.left(), cursorCell.bottom() - 2.0, cursorCell.width(), 2.0),
                        color(m_snapshot->cursor.color));
                    break;
                case terminal::TerminalCursorStyle::hollowBlock:
                    cursorPainter.drawRect(cursorCell.adjusted(0.5, 0.5, -0.5, -0.5));
                    break;
                case terminal::TerminalCursorStyle::block:
                {
                    cursorPainter.fillRect(cursorCell, color(m_snapshot->cursor.color));
                    const terminal::TerminalCell &cell =
                        m_snapshot->cell(m_snapshot->cursor.column, m_snapshot->cursor.row);
                    if (!cell.grapheme.empty() && !cell.invisible)
                    {
                        QFont cursorFont = m_font;
                        cursorFont.setBold(cell.bold);
                        cursorFont.setItalic(cell.italic);
                        cursorFont.setUnderline(cell.underline);
                        cursorFont.setStrikeOut(cell.strikethrough);
                        cursorFont.setOverline(cell.overline);
                        cursorPainter.setFont(cursorFont);
                        cursorPainter.setPen(m_backgroundOverride.isValid()
                                                     && sameColor(cell.background, defaultBackground)
                                                 ? m_backgroundOverride
                                                 : color(cell.background));
                        cursorPainter.drawText(
                            QPointF(cursorCell.left(), metrics.ascent()),
                            QString::fromUcs4(cell.grapheme.data(), static_cast<qsizetype>(cell.grapheme.size())));
                    }
                    break;
                }
            }
            if (node->cursorNode == nullptr)
            {
                node->cursorNode = new QSGSimpleTextureNode;
                node->cursorNode->setOwnsTexture(true);
                node->cursorNode->setFiltering(QSGTexture::Linear);
                node->appendChildNode(node->cursorNode);
            }
            node->cursorNode->setTexture(window()->createTextureFromImage(cursorImage));
        }
        if (node->cursorNode != nullptr)
        {
            node->cursorNode->setRect(terminalCursorPresent && (!m_cursorBlink || m_cursorBlinkPhase) ? node->cursorRect
                                                                                                      : QRectF{});
        }
        if (collectPaintPhases)
        {
            overlayPaintNanoseconds = frameTimer.nsecsElapsed() - phaseMarkNanoseconds;
        }
    }
    else if (node->cursorNode != nullptr)
    {
        node->cursorNode->setRect({});
        if (collectPaintPhases)
        {
            overlayPaintNanoseconds = frameTimer.nsecsElapsed() - phaseMarkNanoseconds;
        }
    }

    const qint64 paintNanoseconds = timingEnabled ? frameTimer.nsecsElapsed() : 0;
    if (collectPaintPhases)
    {
        m_renderMetrics.recordPaintPhases({
            .imagePreparation = std::chrono::nanoseconds{imagePreparationNanoseconds},
            .snapshotPreparation = std::chrono::nanoseconds{snapshotPreparationNanoseconds},
            .backgroundPaint = std::chrono::nanoseconds{backgroundPaintNanoseconds},
            .textPaint = std::chrono::nanoseconds{textPaintNanoseconds},
            .overlayPaint = std::chrono::nanoseconds{overlayPaintNanoseconds},
        });
    }
    QSGTexture *newTexture = window()->createTextureFromImage(image);
    const qint64 textureNanoseconds = timingEnabled ? frameTimer.nsecsElapsed() - paintNanoseconds : 0;
    const terminal::TerminalDamageKind damage = cursorOnlyPaint ? terminal::TerminalDamageKind::partial
                                                : m_snapshot    ? m_snapshot->damage
                                                                : terminal::TerminalDamageKind::full;
    const std::size_t damagedRowCount = cursorOnlyPaint ? std::size_t{1}
                                        : m_snapshot    ? m_snapshot->damagedRows.size()
                                                        : std::size_t{0};
    if (m_renderMetrics.enabled())
    {
        const auto pixelCount =
            static_cast<std::uint64_t>(pixelSize.width()) * static_cast<std::uint64_t>(pixelSize.height());
        m_renderMetrics.recordFrame(std::chrono::nanoseconds{paintNanoseconds},
                                    std::chrono::nanoseconds{textureNanoseconds}, pixelCount, damage, damagedRowCount);
    }
#if !defined(NDEBUG)
    node->recordTiming(paintNanoseconds, textureNanoseconds, pixelSize, damage, damagedRowCount);
#endif
    node->setTexture(newTexture);
    node->setRect(boundingRect());
    node->setFiltering(QSGTexture::Linear);
    node->revision = m_revision;
    node->pixelSize = pixelSize;
    m_fullInvalidationPending = false;
    return node;
}

void TerminalItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    cancelSelectionGesture();
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    reportTerminalSize();
    update();
    notifyInputMethod();
}

void TerminalItem::keyPressEvent(QKeyEvent *event)
{
    dismissSelectionAction();
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

    if (m_hasSelection)
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
        updateHoveredLink(m_hoverPosition, event->modifiers() & ~Qt::ControlModifier);
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
    reportFocus(true);
    QQuickItem::focusInEvent(event);
}

void TerminalItem::focusOutEvent(QFocusEvent *event)
{
    cancelSelectionGesture();
    clearPreedit();
    m_focusOutTimer.start();
    QQuickItem::focusOutEvent(event);
}

void TerminalItem::hoverMoveEvent(QHoverEvent *event)
{
    m_hoverInside = true;
    m_hoverPosition = event->position();
    updateHoveredLink(event->position(), event->modifiers());
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
                clearSelection();
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
        m_selectionActionPosition = event->position();
        m_selectionActionVisible = true;
        emit selectionActionChanged();
        event->accept();
        return;
    }

    m_selectionAnchor = *point;
    m_selecting = true;
    m_selectionMoved = false;
    m_selectionClickSelected = tripleClick;
    m_selectionPointerPosition = event->position();
    stopSelectionAutoscroll();
    setHasSelection(tripleClick);
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
            m_selectionAnchor = *point;
            m_selecting = true;
            m_selectionMoved = false;
            m_selectionClickSelected = true;
            m_selectionPointerPosition = event->position();
            stopSelectionAutoscroll();
            emit selectionGestureRequested(
                selectionGesture(terminal::TerminalSelectionGestureType::press, event->position(), event->timestamp()));
            setHasSelection(true);
            m_selectionActionPosition = event->position();
            m_selectionActionVisible = true;
            emit selectionActionChanged();
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
        auto gesture = selectionGesture(terminal::TerminalSelectionGestureType::drag, event->position());
        gesture.rectangular = event->modifiers().testFlag(Qt::AltModifier);
        emit selectionGestureRequested(gesture);
        m_selectionPointerPosition = event->position();
        updateSelectionAutoscroll(event->position());
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
                m_selectionActionPosition = event->position();
                m_selectionActionVisible = true;
                emit selectionActionChanged();
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
    const auto wordClass = [](const terminal::TerminalCell &cell) {
        if (cell.grapheme.empty())
        {
            return 0;
        }
        const QString text = QString::fromUcs4(cell.grapheme.data(), static_cast<qsizetype>(cell.grapheme.size()));
        if (text.trimmed().isEmpty())
        {
            return 0;
        }
        return std::ranges::all_of(text,
                                   [](const QChar character) {
                                       return character.isLetterOrNumber() || character == QLatin1Char('_');
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
    emit selectionRequested(first, point.row, finalColumn, point.row, false);
    setHasSelection(true);
    m_selectionActionPosition = position;
    m_selectionActionVisible = true;
    emit selectionActionChanged();
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
    m_selectionActionPosition = position;
    m_selectionActionVisible = true;
    emit selectionActionChanged();
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
            emit scrollRequested(-steps * m_scrollRowsPerWheel);
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
    const qreal availableWidth = std::max(0.0, width() - (horizontalPadding * 2.0));
    const qreal availableHeight = std::max(0.0, height() - (verticalPadding * 2.0));
    const auto columns = static_cast<quint16>(std::clamp(std::floor(availableWidth / cellWidth()), 1.0,
                                                         static_cast<double>(std::numeric_limits<quint16>::max())));
    const auto rows = static_cast<quint16>(std::clamp(std::floor(availableHeight / cellHeight()), 1.0,
                                                      static_cast<double>(std::numeric_limits<quint16>::max())));
    if (columns == m_reportedColumns && rows == m_reportedRows)
    {
        return;
    }

    m_reportedColumns = columns;
    m_reportedRows = rows;
    emit sizeRequested(columns, rows, static_cast<quint32>(std::ceil(cellWidth())),
                       static_cast<quint32>(std::ceil(cellHeight())));
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

qreal TerminalItem::cellWidth() const
{
    return std::ceil(QFontMetricsF(m_font).horizontalAdvance(QLatin1Char('M')));
}

qreal TerminalItem::cellHeight() const
{
    return std::ceil(QFontMetricsF(m_font).height());
}

} // namespace ztermy::ui
