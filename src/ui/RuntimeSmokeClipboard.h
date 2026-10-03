#pragma once

#include <QMimeData>
#include <memory>

namespace ztermy::ui
{
[[nodiscard]] inline std::unique_ptr<QMimeData> cloneMimeData(const QMimeData *source)
{
    auto clone = std::make_unique<QMimeData>();
    if (source == nullptr)
    {
        return clone;
    }
    for (const QString &format : source->formats())
    {
        clone->setData(format, source->data(format));
    }
    return clone;
}

} // namespace ztermy::ui
