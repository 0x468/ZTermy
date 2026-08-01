#include "application/LocalizationManager.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QQmlEngine>
#include <QTranslator>

namespace
{

Q_LOGGING_CATEGORY(localizationLog, "ztermy.localization")

[[nodiscard]] bool usesSimplifiedChinese(const QLocale &locale)
{
    if (locale.language() != QLocale::Chinese)
    {
        return false;
    }
    if (locale.script() == QLocale::SimplifiedHanScript)
    {
        return true;
    }
    return locale.territory() == QLocale::China || locale.territory() == QLocale::Singapore;
}

[[nodiscard]] QLocale localeFor(const ztermy::config::LanguagePreference language)
{
    return language == ztermy::config::LanguagePreference::simplifiedChinese ? QLocale{QLocale::Chinese, QLocale::China}
                                                                             : QLocale{QLocale::English};
}

} // namespace

namespace ztermy
{

LocalizationManager::LocalizationManager(QObject *parent) : QObject(parent) {}

LocalizationManager::~LocalizationManager()
{
    if (m_translator)
    {
        QCoreApplication::removeTranslator(m_translator.get());
    }
}

bool LocalizationManager::apply(const config::LanguagePreference preference, QQmlEngine *engine)
{
    const config::LanguagePreference effective = resolve(preference);
    if (effective == m_effectiveLanguage)
    {
        QLocale::setDefault(localeFor(effective));
        return true;
    }

    std::unique_ptr<QTranslator> replacement;
    if (effective == config::LanguagePreference::simplifiedChinese)
    {
        replacement = std::make_unique<QTranslator>();
        if (!replacement->load(QStringLiteral(":/i18n/ztermy_zh_CN.qm")))
        {
            qCWarning(localizationLog) << "Could not load the Simplified Chinese translation catalog";
            return false;
        }
    }

    if (m_translator)
    {
        QCoreApplication::removeTranslator(m_translator.get());
    }
    if (replacement && !QCoreApplication::installTranslator(replacement.get()))
    {
        if (m_translator)
        {
            QCoreApplication::installTranslator(m_translator.get());
        }
        qCWarning(localizationLog) << "Could not install the Simplified Chinese translation catalog";
        return false;
    }

    m_translator = std::move(replacement);
    m_effectiveLanguage = effective;
    QLocale::setDefault(localeFor(effective));
    if (engine != nullptr)
    {
        engine->retranslate();
    }
    emit effectiveLanguageChanged();
    qCInfo(localizationLog) << "Applied UI language" << effectiveLanguage();
    return true;
}

QString LocalizationManager::effectiveLanguage() const
{
    return config::languagePreferenceToken(m_effectiveLanguage);
}

config::LanguagePreference LocalizationManager::resolve(const config::LanguagePreference preference,
                                                        const QLocale &systemLocale)
{
    if (preference != config::LanguagePreference::system)
    {
        return preference;
    }
    return usesSimplifiedChinese(systemLocale) ? config::LanguagePreference::simplifiedChinese
                                               : config::LanguagePreference::english;
}

} // namespace ztermy
