#include "editorlanguage.h"

#include <QApplication>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

static QString supportedLanguage(const QString &language)
{
    if (language.startsWith(QStringLiteral("zh"))) return QStringLiteral("zh_CN");
    if (language.startsWith(QStringLiteral("ja"))) return QStringLiteral("ja_JP");
    if (language.startsWith(QStringLiteral("ko"))) return QStringLiteral("ko_KR");
    return QStringLiteral("en");
}

QString EditorLanguage::selectedLanguage()
{
    return supportedLanguage(QSettings().value(QStringLiteral("interface/language"), QLocale::system().name()).toString());
}

void EditorLanguage::setSelectedLanguage(const QString &language)
{
    QSettings().setValue(QStringLiteral("interface/language"), supportedLanguage(language));
}

void EditorLanguage::install(QApplication &application)
{
    const QString language = selectedLanguage();
    if (language == QStringLiteral("en")) return;
    for (const QString &prefix : {QStringLiteral("qt_"), QStringLiteral("qtgms_")}) {
        auto *translator = new QTranslator(&application);
        if (!translator->load(QStringLiteral(":/translations/") + prefix + language))
            qFatal("Cannot load the embedded interface translation.");
        application.installTranslator(translator);
    }
}
