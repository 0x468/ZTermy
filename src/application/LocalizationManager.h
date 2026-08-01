#pragma once

#include "core/config/ApplicationSettings.h"

#include <QLocale>
#include <QObject>
#include <QString>

#include <memory>

class QQmlEngine;
class QTranslator;

namespace ztermy
{

class LocalizationManager final : public QObject
{
    Q_OBJECT

public:
    explicit LocalizationManager(QObject *parent = nullptr);
    ~LocalizationManager() override;

    LocalizationManager(const LocalizationManager &) = delete;
    LocalizationManager &operator=(const LocalizationManager &) = delete;

    [[nodiscard]] bool apply(config::LanguagePreference preference, QQmlEngine *engine = nullptr);
    [[nodiscard]] QString effectiveLanguage() const;

    [[nodiscard]] static config::LanguagePreference resolve(config::LanguagePreference preference,
                                                            const QLocale &systemLocale = QLocale::system());

signals:
    void effectiveLanguageChanged();

private:
    std::unique_ptr<QTranslator> m_translator;
    config::LanguagePreference m_effectiveLanguage = config::LanguagePreference::english;
};

} // namespace ztermy
