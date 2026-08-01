#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace ztermy
{

class FontCatalog final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList allFamilies READ allFamilies CONSTANT)
    Q_PROPERTY(QStringList monospacedFamilies READ monospacedFamilies CONSTANT)
    Q_PROPERTY(QString systemUiFamily READ systemUiFamily CONSTANT)

public:
    explicit FontCatalog(QObject *parent = nullptr);

    [[nodiscard]] const QStringList &allFamilies() const noexcept;
    [[nodiscard]] const QStringList &monospacedFamilies() const noexcept;
    [[nodiscard]] const QString &systemUiFamily() const noexcept;

    [[nodiscard]] Q_INVOKABLE QString effectiveUiFamily(const QString &preference) const;
    [[nodiscard]] Q_INVOKABLE bool isMonospaced(const QString &family) const;
    [[nodiscard]] Q_INVOKABLE bool supportsCjk(const QString &family) const;
    [[nodiscard]] Q_INVOKABLE bool supportsLigatures(const QString &family) const;
    Q_INVOKABLE void applyUiFont(const QString &preference) const;

private:
    [[nodiscard]] bool containsFamily(const QString &family) const;

    QStringList m_allFamilies;
    QStringList m_monospacedFamilies;
    QString m_systemUiFamily;
};

} // namespace ztermy
