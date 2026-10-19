#pragma once

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace ztermy::ui
{
class ApplicationInfo : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString platformDescription READ platformDescription CONSTANT)
    Q_PROPERTY(QString buildCommit READ buildCommit CONSTANT)

public:
    explicit ApplicationInfo(QObject *parent = nullptr) : QObject(parent) {}
    [[nodiscard]] QString platformDescription() const;
    [[nodiscard]] QString buildCommit() const;
    Q_INVOKABLE void copyBuildCommit() const;
};
} // namespace ztermy::ui
