#include "ui/icons/SvgIconImageProvider.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QLoggingCategory>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <memory>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.size() != 3)
        return 2;
    bool valid = false;
    const int cycles = arguments[2].toInt(&valid);
    if (!valid || cycles < 1 || cycles > 100)
        return 2;
    const auto &mode = arguments[1];
    std::size_t peakCached = 0;
    int delivered = 0;
    if (mode == QStringLiteral("icons"))
    {
        ztermy::ui::SvgIconImageProvider provider(QStringLiteral(ZTERMY_PROBE_ICON_DIRECTORY));
        const auto names = QDir(QStringLiteral(ZTERMY_PROBE_ICON_DIRECTORY)).entryList({"*.svg"}, QDir::Files);
        if (names.size() != 79)
            return 3;
        for (int cycle = 0; cycle < cycles; ++cycle)
            for (const auto &name : names)
            {
                const auto color = QStringLiteral("%1").arg(cycle * 997 + 0x222222, 6, 16, QLatin1Char('0'));
                const auto image = provider.requestImage(name.chopped(4) + u'/' + color, nullptr, {40, 40});
                if (image.isNull())
                    return 4;
                peakCached = std::max(peakCached, provider.cachedImageCount());
            }
        // 40x40x4 images must remain inside the production 4 MiB cache budget.
        if (peakCached > 4 * 1024 * 1024 / (40 * 40 * 4))
            return 5;
    }
    else if (mode == QStringLiteral("callbacks"))
    {
        for (int cycle = 0; cycle < cycles; ++cycle)
            for (const bool cancel : {false, true})
            {
                auto receiver = std::make_unique<QObject>();
                auto payload = std::make_shared<QByteArray>(4096, 'x');
                const std::weak_ptr<QByteArray> lifetime = payload;
                QTimer::singleShot(0, receiver.get(), [payload, &delivered] {
                    ++delivered;
                });
                payload.reset();
                if (cancel)
                    receiver.reset();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
                if (!lifetime.expired())
                    return 7;
            }
        if (delivered != cycles)
            return 8;
    }
    else if (mode == QStringLiteral("logging"))
    {
        // Isolate Qt's process-global logging registry from product lifetimes.
        for (int cycle = 0; cycle < cycles; ++cycle)
        {
            const QLoggingCategory category("ztermy.heap.probe");
            qCInfo(category) << "Qt logging baseline" << cycle;
        }
    }
    else if (mode != QStringLiteral("baseline"))
        return 2;
    QTextStream(stdout) << "mode=" << mode << " cycles=" << cycles << " peakCached=" << peakCached
                        << " delivered=" << delivered << '\n';
    return 0;
}
