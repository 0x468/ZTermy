#pragma once

#include <QAbstractTextDocumentLayout>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QQuickItem>
#include <QQuickTextDocument>
#include <QSet>
#include <QTextDocument>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

namespace ztermy::ui
{
// Created only by the synthetic memory benchmark. Never reads or writes message text.
class AiMemoryDiagnostics final : public QObject
{
    Q_OBJECT

public:
    explicit AiMemoryDiagnostics(const QString &path, QObject *parent) : QObject(parent)
    {
        m_writer = std::jthread([this, path](const std::stop_token &stop) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            {
                m_writeFailed.store(true);
                return;
            }
            std::unique_lock lock(m_mutex);
            for (;;)
            {
                m_ready.wait(lock, stop, [this] {
                    return !m_pending.isEmpty();
                });
                const QByteArray bytes = std::exchange(m_pending, {});
                const bool done = stop.stop_requested();
                lock.unlock();
                if (!bytes.isEmpty() && (file.write(bytes) != bytes.size() || !file.flush()))
                    m_writeFailed.store(true);
                if (done)
                    break;
                lock.lock();
            }
        });
        m_clock.start();
        m_timer.setInterval(500);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            flush();
        });
        m_timer.start();
    }

    ~AiMemoryDiagnostics() override { finish(); }

    void finish()
    {
        if (!m_writer.joinable())
            return;
        m_timer.stop();
        flush();
        m_writer.request_stop();
        m_writer.join();
    }

    Q_INVOKABLE void track(const QString &kind, QObject *object, int row)
    {
        if (object == nullptr || m_seen.contains(object))
            return;
        m_seen.insert(object);
        object->setProperty("_memoryRow", row);
        object->setProperty("_memoryKind", kind);
        const QString key = kind + QStringLiteral(":%1").arg(row);
        ++m_counts[key + QStringLiteral(".created")];
        const qint64 live = ++m_counts[key + QStringLiteral(".live")];
        m_counts[key + QStringLiteral(".peakLive")] = std::max(m_counts.value(key + QStringLiteral(".peakLive")), live);
        connect(object, &QObject::destroyed, this, [this, object, key] {
            m_seen.remove(object);
            ++m_counts[key + QStringLiteral(".destroyed")];
            --m_counts[key + QStringLiteral(".live")];
            maybeFlush();
        });
        if (auto *item = qobject_cast<QQuickItem *>(object))
        {
            const auto geometry = [this, item, key](const QString &signal) {
                ++m_counts[key + signal];
                m_counts[key + QStringLiteral(".maxHeight")] =
                    std::max(m_counts.value(key + QStringLiteral(".maxHeight")), qRound64(item->height()));
                // Do not query implicitWidth/Height: their getters can trigger layout.
                m_geometry.insert(key, QJsonObject{{QStringLiteral("width"), item->width()},
                                                   {QStringLiteral("height"), item->height()},
                                                   {QStringLiteral("y"), item->y()}});
                maybeFlush();
            };
            connect(item, &QQuickItem::widthChanged, this, [geometry] {
                geometry(QStringLiteral(".width"));
            });
            connect(item, &QQuickItem::heightChanged, this, [geometry] {
                geometry(QStringLiteral(".height"));
            });
            connect(item, &QQuickItem::implicitWidthChanged, this, [geometry] {
                geometry(QStringLiteral(".implicitWidth"));
            });
            connect(item, &QQuickItem::implicitHeightChanged, this, [geometry] {
                geometry(QStringLiteral(".implicitHeight"));
            });
            geometry(QStringLiteral(".initial"));
            auto *wrapper = object->property("textDocument").value<QQuickTextDocument *>();
            if (wrapper != nullptr)
            {
                auto *document = wrapper->textDocument();
                track(QStringLiteral("document-") + kind, document, row);
                connect(document, &QTextDocument::contentsChanged, this, [this, document, key] {
                    ++m_counts[key + QStringLiteral(".documentUpdates")];
                    m_counts[key + QStringLiteral(".characters")] = document->characterCount();
                    maybeFlush();
                });
                connect(document->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this,
                        [this, key](const QSizeF &size) {
                            ++m_counts[key + QStringLiteral(".documentLayouts")];
                            m_geometry.insert(key + QStringLiteral(".document"),
                                              QJsonObject{{QStringLiteral("width"), size.width()},
                                                          {QStringLiteral("height"), size.height()}});
                            maybeFlush();
                        });
            }
        }
        maybeFlush();
    }

    Q_INVOKABLE void recordEvent(const QString &name, int row, int length = 0)
    {
        const QString key = name + QStringLiteral(":%1").arg(row);
        ++m_counts[key];
        m_counts[key + QStringLiteral(".length")] = length;
        maybeFlush();
    }

    void flush()
    {
        if (!m_writer.joinable())
            return;
        QJsonObject counts;
        for (auto iterator = m_counts.cbegin(); iterator != m_counts.cend(); ++iterator)
            counts.insert(iterator.key(), iterator.value());
        const QJsonObject record{{QStringLiteral("utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                                 {QStringLiteral("elapsedMs"), m_clock.elapsed()},
                                 {QStringLiteral("droppedSnapshots"), m_droppedSnapshots},
                                 {QStringLiteral("writeFailed"), m_writeFailed.load()},
                                 {QStringLiteral("counts"), counts},
                                 {QStringLiteral("geometry"), m_geometry}};
        const QByteArray bytes = QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n';
        {
            const std::scoped_lock lock(m_mutex);
            // Keep diagnostics bounded even on a stalled disk; counters remain cumulative.
            if (!m_pending.isEmpty())
                ++m_droppedSnapshots;
            m_pending = bytes;
        }
        m_ready.notify_one();
        m_lastFlush = m_clock.elapsed();
    }

    [[nodiscard]] QQuickItem *textItem(int row, const QString &kind) const
    {
        for (QObject *object : m_seen)
        {
            auto *item = qobject_cast<QQuickItem *>(object);
            if (item != nullptr && item->window() != nullptr && item->isVisible()
                && object->property("_memoryRow").toInt() == row && object->property("_memoryKind").toString() == kind
                && object->property("textDocument").isValid())
                return item;
        }
        return nullptr;
    }

private:
    void maybeFlush()
    {
        if (m_clock.elapsed() - m_lastFlush >= 500)
            flush();
    }
    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_lastFlush = 0;
    QHash<QString, qint64> m_counts;
    QSet<QObject *> m_seen;
    QJsonObject m_geometry;
    std::mutex m_mutex;
    std::condition_variable_any m_ready;
    QByteArray m_pending;
    qint64 m_droppedSnapshots = 0;
    std::atomic_bool m_writeFailed = false;
    std::jthread m_writer;
};
} // namespace ztermy::ui
