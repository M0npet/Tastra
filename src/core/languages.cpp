// SPDX-License-Identifier: GPL-3.0-or-later

#include "languages.h"

#include <array>

namespace Tastra
{

namespace
{

constexpr std::array<LanguageInfo, 6> infos = {{
    {"es", "abcdefghijklmnopqrstuvwxyzñáéíóúü", "es_ES,es_MX,es_AR,es_CO,es_US,es_ANY", "aeouy",
     "aáàäâ eéèëê iíïìî oóòöô uúüùû nñ cç", "€", false},
    {"fr", "abcdefghijklmnopqrstuvwxyzàâæçéèêëîïôœùûüÿ'", "fr_FR,fr,fr-classique,fr_CA,fr_BE,fr_CH", "aày",
     "aàâæáä eéèêë iîïí oôœö uùûü cç yÿ", "€", true},
    {"it", "abcdefghijklmnopqrstuvwxyzàèéìíòóùú'", "it_IT,it_CH", "aeèio",
     "aàá eèé iìí oòó uùú", "€", true},
    {"pt", "abcdefghijklmnopqrstuvwxyzáâãàçéêíóôõú", "pt_BR,pt_PT", "aeoàé",
     "aáãàâ eéê ií oóõô uúü cç", "€", false},
    {"pl", "aąbcćdeęfghijklłmnńoópqrsśtuvwxyzźż", "pl_PL", "aiouwz",
     "aą cć eę lł nń oó sś zżź", "zł", false},
    {"nl", "abcdefghijklmnopqrstuvwxyzéëïöüèáóúíàäâêîôû'", "nl_NL,nl", "u",
     "aáäàâ eéëèê iïíìî oóöòô uüúùû", "€", true},
}};

}

const LanguageInfo *languageInfo(const QString &code)
{
    for (const LanguageInfo &info : infos) {
        if (code == QLatin1String(info.code)) return &info;
    }
    return nullptr;
}

QString languageAccents(const QString &code, QChar base)
{
    const LanguageInfo *info = languageInfo(code);
    if (!info) return {};
    for (const QString &group : QString::fromUtf8(info->accents).split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (group.front() == base) return group.mid(1);
    }
    return {};
}

QStringList languageDictionaries(const QString &code)
{
    const LanguageInfo *info = languageInfo(code);
    return info ? QString::fromUtf8(info->dictionaries).split(QLatin1Char(','), Qt::SkipEmptyParts) : QStringList();
}

}
