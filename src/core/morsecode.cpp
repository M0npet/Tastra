// SPDX-License-Identifier: GPL-3.0-or-later

#include "morsecode.h"

#include <QHash>

#include <algorithm>

namespace Tastra
{

namespace
{

using Pairs = std::initializer_list<std::pair<const char *, const char *>>;

void append(QList<MorseSign> &table, Pairs pairs)
{
    for (const auto &[text, code] : pairs) table.append({QString::fromUtf8(text), QString::fromLatin1(code)});
}

// ITU-R M.1677-1 letters (en.wikipedia.org/wiki/Morse_code).
constexpr std::pair<const char *, const char *> latinLetters[] = {
    {"a", ".-"},   {"b", "-..."}, {"c", "-.-."}, {"d", "-.."},  {"e", "."},    {"f", "..-."}, {"g", "--."},
    {"h", "...."}, {"i", ".."},   {"j", ".---"}, {"k", "-.-"},  {"l", ".-.."}, {"m", "--"},   {"n", "-."},
    {"o", "---"},  {"p", ".--."}, {"q", "--.-"}, {"r", ".-."},  {"s", "..."},  {"t", "-"},    {"u", "..-"},
    {"v", "...-"}, {"w", ".--"},  {"x", "-..-"}, {"y", "-.--"}, {"z", "--.."},
};

void appendDigitsAndPunctuation(QList<MorseSign> &table)
{
    append(table, {{"1", ".----"}, {"2", "..---"}, {"3", "...--"}, {"4", "....-"}, {"5", "....."},
                   {"6", "-...."}, {"7", "--..."}, {"8", "---.."}, {"9", "----."}, {"0", "-----"}});
    // ITU, then the common non-ITU signs (! & ; _ $).
    append(table, {{".", ".-.-.-"}, {",", "--..--"}, {"?", "..--.."}, {"'", ".----."}, {"/", "-..-."},
                   {"(", "-.--."}, {")", "-.--.-"}, {":", "---..."}, {"=", "-...-"}, {"+", ".-.-."},
                   {"-", "-....-"}, {"\"", ".-..-."}, {"@", ".--.-."}, {"!", "-.-.--"}, {"&", ".-..."},
                   {";", "-.-.-."}, {"_", "..--.-"}, {"$", "...-..-"}});
}

QList<MorseSign> buildTable(const QString &language)
{
    QList<MorseSign> table;
    if (language == QLatin1String("ru")) {
        // Russian national table (en.wikipedia.org/wiki/Morse_code_for_non-Latin_alphabets).
        append(table, {{"а", ".-"},   {"б", "-..."}, {"в", ".--"},  {"г", "--."},   {"д", "-.."},  {"е", "."},
                       {"ж", "...-"}, {"з", "--.."}, {"и", ".."},   {"й", ".---"},  {"к", "-.-"},  {"л", ".-.."},
                       {"м", "--"},   {"н", "-."},   {"о", "---"},  {"п", ".--."},  {"р", ".-."},  {"с", "..."},
                       {"т", "-"},    {"у", "..-"},  {"ф", "..-."}, {"х", "...."},  {"ц", "-.-."}, {"ч", "---."},
                       {"ш", "----"}, {"щ", "--.-"}, {"ы", "-.--"}, {"ь", "-..-"},  {"э", "..-.."}, {"ю", "..--"},
                       {"я", ".-.-"}});
    } else if (language == QLatin1String("uk")) {
        // Ukrainian: и is Russian ы's sign, і is Russian и's, є is э's.
        append(table, {{"а", ".-"},   {"б", "-..."}, {"в", ".--"},   {"г", "--."},  {"д", "-.."},  {"е", "."},
                       {"є", "..-.."}, {"ж", "...-"}, {"з", "--.."}, {"и", "-.--"}, {"і", ".."},   {"ї", ".---."},
                       {"й", ".---"}, {"к", "-.-"},  {"л", ".-.."},  {"м", "--"},   {"н", "-."},   {"о", "---"},
                       {"п", ".--."}, {"р", ".-."},  {"с", "..."},   {"т", "-"},    {"у", "..-"},  {"ф", "..-."},
                       {"х", "...."}, {"ц", "-.-."}, {"ч", "---."},  {"ш", "----"}, {"щ", "--.-"}, {"ь", "-..-"},
                       {"ю", "..--"}, {"я", ".-.-"}});
    } else {
        for (const auto &[text, code] : latinLetters) table.append({QString::fromUtf8(text), QString::fromLatin1(code)});
        if (language == QLatin1String("de")) {
            // DARC usage (qsl.net/dk5ke/morsezeichen.html): CH has a sign of its own.
            append(table, {{"ä", ".-.-"}, {"ö", "---."}, {"ü", "..--"}, {"ch", "----"}});
        }
    }
    appendDigitsAndPunctuation(table);
    return table;
}

}

QList<MorseSign> morseTable(const QString &language)
{
    static QHash<QString, QList<MorseSign>> cache;
    auto it = cache.find(language);
    if (it == cache.end()) it = cache.insert(language, buildTable(language));
    return *it;
}

QString morseText(const QString &code, const QString &language)
{
    if (code.isEmpty()) return {};
    for (const MorseSign &sign : morseTable(language)) {
        if (sign.code == code) return sign.text;
    }
    return {};
}

QList<MorseSign> morseContinuations(const QString &code, const QString &language, int limit)
{
    QList<MorseSign> result;
    if (code.isEmpty() || limit <= 0) return result;
    for (const MorseSign &sign : morseTable(language)) {
        if (sign.code.size() > code.size() && sign.code.startsWith(code)) result.append(sign);
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const MorseSign &a, const MorseSign &b) { return a.code.size() < b.code.size(); });
    if (result.size() > limit) result.resize(limit);
    return result;
}

void MorseKeyer::setLanguage(const QString &language)
{
    if (m_language == language) return;
    m_language = language;
    m_code.clear();
}

bool MorseKeyer::add(QChar symbol)
{
    if (m_code.size() >= MaxLength) return false;
    m_code += symbol;
    return true;
}

bool MorseKeyer::backspace()
{
    if (m_code.isEmpty()) return false;
    m_code.chop(1);
    return true;
}

QString MorseKeyer::take()
{
    const QString result = text();
    m_code.clear();
    return result;
}

}
