// SPDX-License-Identifier: GPL-3.0-or-later

#include "keyboardmodel.h"

#include <QHash>

#include <array>

namespace Tastra
{
namespace
{

struct LayoutDefinition
{
    const char *code;
    const char *label;
    const char *row1;
    const char *row2;
    const char *row3;
};

constexpr std::array<LayoutDefinition, 4> layouts = {{
    {"en", "English", "qwertyuiop", "asdfghjkl", "zxcvbnm"},
    {"de", "Deutsch", "qwertzuiopü", "asdfghjklöä", "yxcvbnm"},
    {"uk", "Українська", "йцукенгшщзхї", "фівапролджє", "ячсмитьбю"},
    {"ru", "Русский", "йцукенгшщзхъ", "фывапролджэ", "ячсмитьбю"},
}};

// Gboard "Select a layout" per language: the letter rows of the other
// layouts (mobile forms: no punctuation keys except Dvorak's ' , .).
struct VariantDefinition
{
    const char *code;
    const char *variant;
    const char *label;
    const char *row1;
    const char *row2;
    const char *row3;
};

constexpr std::array<VariantDefinition, 5> variants = {{
    {"en", "qwertz", "QWERTZ", "qwertzuiop", "asdfghjkl", "yxcvbnm"},
    {"en", "azerty", "AZERTY", "azertyuiop", "qsdfghjklm", "wxcvbn"},
    {"en", "dvorak", "Dvorak", "',.pyfgcrl", "aoeuidhtns", "qjkxbmwvz"},
    {"en", "colemak", "Colemak", "qwfpgjluy", "arstdhneio", "zxcvbkm"},
    {"de", "qwerty", "QWERTY", "qwertyuiopü", "asdfghjklöä", "zxcvbnm"},
}};

// The first layout of each language and its name.
QString defaultVariant(const QString &code)
{
    if (code == QStringLiteral("de")) return QStringLiteral("qwertz");
    if (code == QStringLiteral("ru") || code == QStringLiteral("uk")) return QStringLiteral("jcuken");
    return QStringLiteral("qwerty");
}

QString defaultVariantLabel(const QString &code)
{
    if (code == QStringLiteral("de")) return QStringLiteral("QWERTZ");
    if (code == QStringLiteral("ru") || code == QStringLiteral("uk")) return QString::fromUtf8("ЙЦУКЕН");
    return QStringLiteral("QWERTY");
}

QStringList splitCharacters(const QString &text)
{
    QStringList result;
    result.reserve(text.size());

    for (const QChar ch : text) {
        result.append(QString(ch));
    }

    return result;
}

}


void KeyboardModel::pressShift()
{
    if (m_layer != Layer::Alphabet) {
        return;
    }

    switch (m_shiftState) {
    case ShiftState::Lowercase:
        m_shiftState = ShiftState::OneShot;
        break;
    case ShiftState::OneShot:
        m_shiftState = ShiftState::CapsLock;
        break;
    case ShiftState::CapsLock:
        m_shiftState = ShiftState::Lowercase;
        break;
    }
}

void KeyboardModel::consumeShiftAfterLetter()
{
    if (m_shiftState == ShiftState::OneShot) {
        m_shiftState = ShiftState::Lowercase;
    }
}

void KeyboardModel::toggleSymbols()
{
    m_symbolPage = 0;
    m_layer = (m_layer == Layer::Alphabet)
        ? Layer::Symbols
        : Layer::Alphabet;

    resetShift();
}

bool KeyboardModel::uppercase() const
{
    return m_layer == Layer::Alphabet
        && m_shiftState != ShiftState::Lowercase;
}

bool KeyboardModel::capsLock() const
{
    return m_layer == Layer::Alphabet
        && m_shiftState == ShiftState::CapsLock;
}

bool KeyboardModel::symbolsActive() const
{
    return m_layer == Layer::Symbols;
}

QString KeyboardModel::textForLetter(const QString &text) const
{
    return uppercase() ? text.toUpper() : text.toLower();
}

QStringList KeyboardModel::rowsForLanguage(const QString &code, const QString &variant)
{
    for (const auto &v : variants) {
        if (QString::fromLatin1(v.code) == code && QString::fromLatin1(v.variant) == variant) {
            return {QString::fromUtf8(v.row1), QString::fromUtf8(v.row2), QString::fromUtf8(v.row3)};
        }
    }
    for (const auto &layout : layouts) {
        if (QString::fromLatin1(layout.code) == code) {
            return {QString::fromUtf8(layout.row1), QString::fromUtf8(layout.row2), QString::fromUtf8(layout.row3)};
        }
    }
    return {};
}

QStringList KeyboardModel::layoutVariants(const QString &code)
{
    QStringList ids = {defaultVariant(code)};
    for (const auto &v : variants) {
        if (QString::fromLatin1(v.code) == code) ids.append(QString::fromLatin1(v.variant));
    }
    // Gboard's Morse code layout (English US there; every language here,
    // with its letters: core/morsecode.h). Its rows are the first layout's,
    // for the lexicon; the keyboard shows the dot and dash keys instead.
    if (!rowsForLanguage(code).isEmpty()) ids.append(QStringLiteral("morse"));
    return ids;
}

QString KeyboardModel::layoutVariantLabel(const QString &code, const QString &variant)
{
    if (variant == QLatin1String("morse")) return QStringLiteral("Morse");
    for (const auto &v : variants) {
        if (QString::fromLatin1(v.code) == code && QString::fromLatin1(v.variant) == variant) return QString::fromUtf8(v.label);
    }
    return defaultVariantLabel(code);
}

QString KeyboardModel::layoutVariant(const QString &code) const
{
    const QString chosen = m_variants.value(code);
    return layoutVariants(code).contains(chosen) ? chosen : defaultVariant(code);
}

void KeyboardModel::setLayoutVariant(const QString &code, const QString &variant)
{
    if (!layoutVariants(code).contains(variant)) return;
    m_variants.insert(code, variant);
}

QStringList KeyboardModel::currentRows() const
{
    return rowsForLanguage(languageCode(), layoutVariant(languageCode()));
}

int KeyboardModel::symbolPage() const { return m_symbolPage; }

void KeyboardModel::toggleSymbolPage()
{
    if (m_layer == Layer::Alphabet) return;
    m_symbolPage = m_symbolPage == 0 ? 1 : 0;
}

namespace
{
QString currencyFor(const QString &language)
{
    if (language == QStringLiteral("de")) return QStringLiteral("€");
    if (language == QStringLiteral("ru")) return QStringLiteral("₽");
    if (language == QStringLiteral("uk")) return QStringLiteral("₴");
    return QStringLiteral("$");
}

QStringList chars(const QString &text)
{
    QStringList out;
    for (const QChar ch : text) out.append(QString(ch));
    return out;
}
}

QList<QStringList> KeyboardModel::symbolRows() const
{
    if (m_symbolPage == 0) {
        QStringList row2 = {QStringLiteral("@"), QStringLiteral("#"), currencyFor(languageCode()), QStringLiteral("_"),
                            QStringLiteral("&"), QStringLiteral("-"), QStringLiteral("+"), QStringLiteral("("), QStringLiteral(")")};
        return {chars(QStringLiteral("1234567890")), row2, chars(QStringLiteral("*\"':;!?/"))};
    }
    return {chars(QStringLiteral("~`|•√π÷×¶§")), chars(QStringLiteral("£€¥^°={}\\")), chars(QStringLiteral("%©®™[]<>"))};
}

QStringList KeyboardModel::alternatesForKey(const QString &text) const
{
    if (m_layer != Layer::Alphabet && text != QStringLiteral(".")) {
        // Gboard long-press extras on the symbol pages.
        static const QHash<QString, QString> extras = {
            {QStringLiteral("\""), QStringLiteral("«»„“”")}, {QStringLiteral("'"), QStringLiteral("‘’‚")},
            {QStringLiteral("-"), QStringLiteral("–—")}, {QStringLiteral("_"), QStringLiteral("—")},
            {QStringLiteral("!"), QStringLiteral("¡")}, {QStringLiteral("?"), QStringLiteral("¿")},
            {QStringLiteral("%"), QStringLiteral("‰")}, {QStringLiteral("*"), QStringLiteral("★•")},
            {QStringLiteral("="), QStringLiteral("≠≈±")}, {QStringLiteral("<"), QStringLiteral("≤«")},
            {QStringLiteral(">"), QStringLiteral("≥»")}, {QStringLiteral("("), QStringLiteral("[{<")},
            {QStringLiteral(")"), QStringLiteral("]}>")}, {QStringLiteral("/"), QStringLiteral("\\")},
            {QStringLiteral("&"), QStringLiteral("§")}, {QStringLiteral("#"), QStringLiteral("№")},
            {QStringLiteral("0"), QStringLiteral("°∅")}, {QStringLiteral("1"), QStringLiteral("¹½⅓¼")},
            {QStringLiteral("2"), QStringLiteral("²⅔")}, {QStringLiteral("3"), QStringLiteral("³¾")}};
        static const QString currencies = QStringLiteral("$€₴₽£¥¢");
        if (currencies.contains(text) && text.size() == 1) {
            QStringList others;
            for (const QChar ch : currencies) if (QString(ch) != text) others.append(QString(ch));
            return others;
        }
        return chars(extras.value(text));
    }
    if (m_layer != Layer::Alphabet) return {};
    if (text == QStringLiteral(".")) {
        return {QStringLiteral(","), QStringLiteral("?"), QStringLiteral("!"), QStringLiteral("'"),
                QStringLiteral("\""), QStringLiteral(":"), QStringLiteral(";"), QStringLiteral("-"),
                QStringLiteral("("), QStringLiteral(")"), QStringLiteral("@"), QStringLiteral("&"),
                QStringLiteral("%"), QStringLiteral("/"), QStringLiteral("#"), QStringLiteral("*")};
    }
    const QString lower = text.toLower();
    QStringList result;
    const QString special = alternateForKey(text);
    if (!special.isEmpty()) result.append(special);

    // Symbol hints by key position, as on Gboard: top row digits, then
    // "@#$_&-+()" and "*\"':;!?".
    static const QStringList second = {QStringLiteral("@"), QStringLiteral("#"), QStringLiteral("$"), QStringLiteral("_"),
                                       QStringLiteral("&"), QStringLiteral("-"), QStringLiteral("+"), QStringLiteral("("),
                                       QStringLiteral(")")};
    static const QStringList third = {QStringLiteral("*"), QStringLiteral("\""), QStringLiteral("'"), QStringLiteral(":"),
                                      QStringLiteral(";"), QStringLiteral("!"), QStringLiteral("?")};
    auto position = [&lower](const QStringList &row) {
        for (int i = 0; i < row.size(); ++i) if (row.at(i).toLower() == lower) return i;
        return -1;
    };
    QString symbol;
    if (const int i = position(row1()); i >= 0 && i < 10) symbol = QString::number((i + 1) % 10);
    else if (const int j = position(row2()); j >= 0 && j < second.size()) symbol = second.at(j);
    else if (const int k = position(row3()); k >= 0 && k < third.size()) symbol = third.at(k);
    if (!symbol.isEmpty()) result.append(symbol);

    if (languageCode() == QStringLiteral("en")) {
        static const QHash<QString, QString> accents = {
            {QStringLiteral("a"), QStringLiteral("àáâäãåæ")}, {QStringLiteral("e"), QStringLiteral("èéêë")},
            {QStringLiteral("i"), QStringLiteral("ìíîï")}, {QStringLiteral("o"), QStringLiteral("òóôöõøœ")},
            {QStringLiteral("u"), QStringLiteral("ùúûü")}, {QStringLiteral("c"), QStringLiteral("ç")},
            {QStringLiteral("n"), QStringLiteral("ñ")}, {QStringLiteral("y"), QStringLiteral("ýÿ")},
            {QStringLiteral("s"), QStringLiteral("ß")}};
        for (const QChar ch : accents.value(lower)) {
            result.append(uppercase() ? QString(ch).toUpper() : QString(ch));
        }
    }
    return result;
}

QString KeyboardModel::alternateForKey(const QString &text) const
{
    if (m_layer != Layer::Alphabet) {
        return {};
    }

    const QString lower = text.toLower();
    const QString code = languageCode();

    if (code == QStringLiteral("de") && lower == QStringLiteral("s")) {
        return uppercase() ? QStringLiteral("ẞ") : QStringLiteral("ß");
    }
    if (code == QStringLiteral("uk") && lower == QStringLiteral("г")) {
        return uppercase() ? QStringLiteral("Ґ") : QStringLiteral("ґ");
    }
    if (code == QStringLiteral("ru") && lower == QStringLiteral("е")) {
        return uppercase() ? QStringLiteral("Ё") : QStringLiteral("ё");
    }

    return {};
}

QString KeyboardModel::languageCode() const
{
    return QString::fromUtf8(layouts.at(m_languageIndex).code);
}

QString KeyboardModel::languageLabel() const
{
    return QString::fromUtf8(layouts.at(m_languageIndex).label);
}

QStringList KeyboardModel::languageCodes() const
{
    QStringList result;
    result.reserve(static_cast<int>(layouts.size()));
    for (const auto &layout : layouts) {
        result.append(QString::fromUtf8(layout.code));
    }
    return result;
}

QStringList KeyboardModel::languageLabels() const
{
    QStringList result;
    result.reserve(static_cast<int>(layouts.size()));
    for (const auto &layout : layouts) {
        result.append(QString::fromUtf8(layout.label));
    }
    return result;
}

QStringList KeyboardModel::row1() const
{
    return splitCharacters(currentRows().value(0));
}

QStringList KeyboardModel::row2() const
{
    return splitCharacters(currentRows().value(1));
}

QStringList KeyboardModel::row3() const
{
    return splitCharacters(currentRows().value(2));
}

void KeyboardModel::setLanguage(const QString &code)
{
    for (int i = 0; i < static_cast<int>(layouts.size()); ++i) {
        if (code == QString::fromUtf8(layouts.at(i).code)) {
            if (m_languageIndex != i) {
                m_languageIndex = i;
                resetShift();
            }
            return;
        }
    }
}

void KeyboardModel::nextLanguage()
{
    m_languageIndex = (m_languageIndex + 1) % static_cast<int>(layouts.size());
    resetShift();
}

void KeyboardModel::resetShift()
{
    m_shiftState = ShiftState::Lowercase;
}


}
