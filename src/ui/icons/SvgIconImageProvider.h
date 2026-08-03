#pragma once

#include <QQuickImageProvider>
#include <QString>

namespace ztermy::ui
{

class SvgIconImageProvider final : public QQuickImageProvider
{
public:
    explicit SvgIconImageProvider(QString iconDirectory = QStringLiteral(":/ztermy/icons"));

    [[nodiscard]] QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QString m_iconDirectory;
};

} // namespace ztermy::ui
