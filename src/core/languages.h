// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>

namespace Tastra
{

// The keyboard languages added in 0.7.19 (Spanish, French, Italian,
// Portuguese, Polish, Dutch), described by data instead of code: their
// letters, Hunspell dictionaries, one-letter words, accented letters and
// currency. English, German, Russian and Ukrainian keep their own rules in
// LocalLexicon and KeyboardModel.
struct LanguageInfo {
    const char *code;
    const char *alphabet;        // letters corrections may use
    const char *dictionaries;    // Hunspell names, best first, comma-separated
    const char *oneLetterWords;  // single letters that are words of their own
    // Accented letters per base letter, as Gboard (AOSP LatinIME) offers
    // them on a long press, most used first: "aáàâ eéè" ...
    const char *accents;
    const char *currency;
    bool apostrophes;            // words with an inner apostrophe (c'est, dell'anno, zo'n)
};

// Null for the first four languages and unknown codes.
const LanguageInfo *languageInfo(const QString &code);
// The accented letters for `base` (lowercase) in that language.
QString languageAccents(const QString &code, QChar base);
QStringList languageDictionaries(const QString &code);

}
