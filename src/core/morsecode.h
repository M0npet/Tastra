// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

namespace Tastra
{

// One sign of the Morse layout: what it types and its code ('.' and '-').
struct MorseSign {
    QString text;
    QString code;
};

// Gboard's Morse code layout (support.google.com/gboard/answer/9011881),
// which Gboard has for English (US) only. The signs are International Morse
// (ITU-R M.1677-1) and the common non-ITU ones (! & ; _ $), plus the
// national letters of the keyboard's other languages:
//  - de: ä .-.-, ö ---., ü ..--, "ch" ----; ß has no sign (sent as "ss");
//  - ru: the Russian table (ё is sent as е; ъ has no sign there);
//  - uk: the Ukrainian letters (и -.--, і .., є ..-.., ї .---.); ґ has no
//    sign.
// Letters first, then digits, then punctuation.
QList<MorseSign> morseTable(const QString &language);

// What a finished code types, or empty when it is no sign.
QString morseText(const QString &code, const QString &language);

// The signs a code can still become (longer codes that start with it),
// shortest first: shown in the strip as a guide while a sign is keyed.
QList<MorseSign> morseContinuations(const QString &code, const QString &language, int limit);

// Keys a sign: dots and dashes until the sign is sent (by a pause, by
// Space, or by tapping it in the strip). No timers here; the caller keeps
// the time.
class MorseKeyer
{
public:
    static constexpr int MaxLength = 8;

    void setLanguage(const QString &language);
    QString language() const { return m_language; }
    // False when the code is already longer than any sign.
    bool dot() { return add(QLatin1Char('.')); }
    bool dash() { return add(QLatin1Char('-')); }
    // Takes back the last dot or dash; false when nothing is keyed.
    bool backspace();
    // The keyed sign's text (empty for no sign), and the code is cleared.
    QString take();
    void clear() { m_code.clear(); }

    bool pending() const { return !m_code.isEmpty(); }
    QString code() const { return m_code; }
    QString text() const { return morseText(m_code, m_language); }
    QList<MorseSign> continuations(int limit) const { return morseContinuations(m_code, m_language, limit); }

private:
    bool add(QChar symbol);

    QString m_language = QStringLiteral("en");
    QString m_code;
};

}
