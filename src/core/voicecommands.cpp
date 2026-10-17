// SPDX-License-Identifier: GPL-3.0-or-later

#include "voicecommands.h"

#include <QHash>
#include <QList>
#include <QPair>
#include <QRegularExpression>
#include <QStringList>

namespace Tastra
{

namespace
{

// Lower case, ё as е, punctuation dropped, single spaces: what Whisper
// writes for "Delete last word." and "delete last word" alike.
QString normalized(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (QChar ch : text.toLower()) {
        if (ch == QChar(0x0451)) ch = QChar(0x0435);       // ё -> е
        if (ch.isLetterOrNumber() || ch == QLatin1Char('\'')) out += ch;
        else out += QLatin1Char(' ');
    }
    return out.simplified();
}

const QHash<QString, VoiceCommand::Kind> &phrases()
{
    static const QHash<QString, VoiceCommand::Kind> table = [] {
        QHash<QString, VoiceCommand::Kind> t;
        auto add = [&t](VoiceCommand::Kind kind, std::initializer_list<const char *> list) {
            for (const char *phrase : list) t.insert(QString::fromUtf8(phrase), kind);
        };
        add(VoiceCommand::DeleteLastWord,
            {"delete last word", "delete the last word", "letztes wort löschen", "das letzte wort löschen",
             "lösche das letzte wort", "lösch das letzte wort", "удали последнее слово", "удалить последнее слово",
             "сотри последнее слово", "стереть последнее слово", "видали останнє слово", "видалити останнє слово",
             "зітри останнє слово", "стерти останнє слово"});
        add(VoiceCommand::ClearSentence,
            {"clear", "delete last sentence", "delete the last sentence", "letzten satz löschen",
             "den letzten satz löschen", "lösche den letzten satz", "удали последнее предложение",
             "удалить последнее предложение", "видали останнє речення", "видалити останнє речення"});
        add(VoiceCommand::ClearAll,
            {"clear all", "delete all", "delete everything", "alles löschen", "lösche alles", "удали все",
             "удалить все", "очистить все", "стереть все", "видали все", "видалити все", "очистити все",
             "стерти все"});
        add(VoiceCommand::Send,
            {"send", "send it", "send message", "send the message", "senden", "absenden", "abschicken",
             "nachricht senden", "отправить", "отправь", "отправить сообщение", "надіслати", "надішли",
             "відправити", "відправ", "надіслати повідомлення"});
        add(VoiceCommand::NewLine,
            {"new line", "next line", "neue zeile", "nächste zeile", "новая строка", "с новой строки",
             "новий рядок", "з нового рядка"});
        add(VoiceCommand::Undo,
            {"undo", "rückgängig", "rückgängig machen", "отменить", "отмени", "скасувати", "скасуй"});
        return t;
    }();
    return table;
}

// "Change X to Y" and the other detailed edits, as said in the four
// languages; matched on the utterance as Whisper wrote it (its case kept for
// what is inserted), without the mark it ends with.
struct EditPattern {
    VoiceCommand::Kind kind;
    QRegularExpression pattern;
};

const QList<EditPattern> &editPatterns()
{
    static const QList<EditPattern> list = [] {
        QList<EditPattern> l;
        const auto options = QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption;
        auto add = [&](VoiceCommand::Kind kind, const char *pattern) {
            l.append({kind, QRegularExpression(QString::fromUtf8(pattern), options)});
        };
        // "at the end" before "after"/"before": "insert X at the end".
        // Only the verbs of commands ("add", "remove" are too common in
        // dictation); the words referred to must also be in the text.
        add(VoiceCommand::InsertAtEnd, R"(^(?:insert|füge|вставь|вставить|встав|вставити)\s+(.+?)\s+(?:at the end|am ende(?:\s+ein)?|в конце|в конец|в кінці|в кінець)$)");
        add(VoiceCommand::InsertBefore, R"(^(?:insert|füge|вставь|вставить|встав|вставити)\s+(.+?)\s+(?:before|vor|перед)\s+(.+?)(?:\s+ein)?$)");
        add(VoiceCommand::InsertAfter, R"(^(?:insert|füge|вставь|вставить|встав|вставити)\s+(.+?)\s+(?:after|nach|после|після)\s+(.+?)(?:\s+ein)?$)");
        add(VoiceCommand::ReplaceText, R"(^(?:change|replace|ändere|ändre|ersetze|замени|заменить|исправь|исправить|заміни|замінити|виправ|виправити)\s+(.+?)\s+(?:to|with|in|zu|durch|на)\s+(.+)$)");
        add(VoiceCommand::Spell, R"(^(?:spell|buchstabiere)\s+(.+?)\s+(?:as|als)\s+(.+)$)");
        add(VoiceCommand::Capitalize, R"(^(?:capitali[sz]e)\s+(.+)$)");
        add(VoiceCommand::Capitalize, R"(^(?:schreibe?)\s+(.+?)\s+groß$)");
        add(VoiceCommand::Capitalize, R"(^(?:напиши|написать|напиши)\s+(.+?)\s+с (?:большой|заглавной) буквы$)");
        add(VoiceCommand::Capitalize, R"(^(?:напиши|написати)\s+(.+?)\s+з великої (?:літери|букви)$)");
        add(VoiceCommand::DeleteText, R"(^(?:delete|lösche|lösch|удали|удалить|сотри|стереть|видали|видалити|зітри|стерти)\s+(?:the word\s+|das wort\s+|слово\s+)?(.+)$)");
        return l;
    }();
    return list;
}

// What Whisper puts around a phrase: its end mark, quotes.
QString bareArgument(QString text)
{
    text = text.trimmed();
    static const QString marks = QString::fromUtf8("\"'“”„«»‘’`");
    while (!text.isEmpty() && (marks.contains(text.front()))) text.remove(0, 1);
    while (!text.isEmpty() && (marks.contains(text.back()) || QStringLiteral(".,!?;:").contains(text.back())
                               || text.back() == QChar(0x2026)))
        text.chop(1);
    return text.trimmed();
}

const QStringList &paragraphPhrases()
{
    static const QStringList list = {QStringLiteral("new paragraph"), QStringLiteral("neuer absatz"),
                                     QString::fromUtf8("новый абзац"), QString::fromUtf8("новий абзац")};
    return list;
}

}

VoiceCommand parseVoiceCommand(const QString &utterance)
{
    VoiceCommand command;
    const QString said = normalized(utterance);
    if (said.isEmpty()) return command;
    const auto found = phrases().constFind(said);
    if (found != phrases().constEnd()) {
        command.kind = found.value();
        if (command.kind == VoiceCommand::NewLine) command.argument = QStringLiteral("\n");
        return command;
    }
    if (paragraphPhrases().contains(said)) {
        command.kind = VoiceCommand::NewLine;
        command.argument = QStringLiteral("\n\n");
        return command;
    }
    static const QStringList nextField = {QStringLiteral("next"), QStringLiteral("next field"),
                                          QString::fromUtf8("nächstes feld"), QString::fromUtf8("следующее поле"),
                                          QString::fromUtf8("наступне поле")};
    if (nextField.contains(said)) {
        command.kind = VoiceCommand::NextField;
        return command;
    }
    const QString spoken = bareArgument(utterance);
    for (const EditPattern &edit : editPatterns()) {
        const QRegularExpressionMatch match = edit.pattern.match(spoken);
        if (!match.hasMatch()) continue;
        command.kind = edit.kind;
        command.argument = bareArgument(match.captured(1));
        command.second = bareArgument(match.captured(2));
        // Short phrases only: a long sentence that starts like a command is
        // dictation.
        auto words = [](const QString &text) { return text.split(QLatin1Char(' '), Qt::SkipEmptyParts).size(); };
        const bool inserts = edit.kind == VoiceCommand::InsertBefore || edit.kind == VoiceCommand::InsertAfter
            || edit.kind == VoiceCommand::InsertAtEnd;
        const bool fits = !command.argument.isEmpty() && words(command.argument) <= (inserts ? 10 : 5)
            && (match.lastCapturedIndex() < 2
                || (!command.second.isEmpty() && words(command.second) <= (inserts ? 5 : 8)));
        if (!fits) {
            command = VoiceCommand{};
            continue;
        }
        return command;
    }
    // "<name> emoji" / "эмодзи <name>": at most four words for the name.
    static const QStringList emojiWords = {QStringLiteral("emoji"), QStringLiteral("emojis"),
                                           QString::fromUtf8("эмодзи"), QString::fromUtf8("емодзі"),
                                           QString::fromUtf8("смайлик")};
    QStringList words = said.split(QLatin1Char(' '));
    if (words.size() >= 2 && words.size() <= 5) {
        if (emojiWords.contains(words.last())) words.removeLast();
        else if (emojiWords.contains(words.first())) words.removeFirst();
        else return command;
        command.kind = VoiceCommand::Emoji;
        command.argument = words.join(QLatin1Char(' '));
    }
    return command;
}

namespace
{

// The last whole-word occurrence of `words` in `text`, case ignored; spaces
// between the words match any run of spaces.
QRegularExpressionMatch lastOccurrence(const QString &text, const QString &words)
{
    QStringList parts = words.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for (QString &part : parts) part = QRegularExpression::escape(part);
    if (parts.isEmpty()) return {};
    const QRegularExpression pattern(QStringLiteral("(?<![\\p{L}\\p{N}'’])") + parts.join(QStringLiteral("\\s+"))
                                         + QStringLiteral("(?![\\p{L}\\p{N}])"),
                                     QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    QRegularExpressionMatch last;
    auto it = pattern.globalMatch(text);
    while (it.hasNext()) last = it.next();
    return last;
}

// `replacement` in the case of `original`: "Hello" -> "Hi", "HELLO" -> "HI".
QString inCaseOf(const QString &original, QString replacement)
{
    if (original.isEmpty() || replacement.isEmpty()) return replacement;
    const bool allCaps = original.size() >= 2 && original.toUpper() == original && original.toLower() != original;
    if (allCaps) return replacement.toUpper();
    if (original.front().isUpper() && replacement.front().isLower()) replacement[0] = replacement.front().toUpper();
    return replacement;
}

bool startsSentence(const QString &text, qsizetype at)
{
    qsizetype i = at;
    while (i > 0 && text.at(i - 1) == QLatin1Char(' ')) --i;
    return i == 0 || QStringLiteral(".!?\n").contains(text.at(i - 1));
}

}

std::optional<QString> applyVoiceEdit(const QString &text, const VoiceCommand &command)
{
    switch (command.kind) {
    case VoiceCommand::InsertAtEnd: {
        QString result = text;
        if (!result.isEmpty() && !result.back().isSpace()) result += QLatin1Char(' ');
        return result + command.argument;
    }
    case VoiceCommand::ReplaceText:
    case VoiceCommand::DeleteText:
    case VoiceCommand::InsertBefore:
    case VoiceCommand::InsertAfter:
    case VoiceCommand::Capitalize:
    case VoiceCommand::Spell:
        break;
    default:
        return std::nullopt;
    }
    const bool insert = command.kind == VoiceCommand::InsertBefore || command.kind == VoiceCommand::InsertAfter;
    const QRegularExpressionMatch found = lastOccurrence(text, insert ? command.second : command.argument);
    if (!found.hasMatch()) return std::nullopt;
    const qsizetype start = found.capturedStart();
    const qsizetype end = found.capturedEnd();
    const QString matched = found.captured();
    QString result = text;
    switch (command.kind) {
    case VoiceCommand::ReplaceText:
        result.replace(start, end - start, inCaseOf(matched, command.second));
        break;
    case VoiceCommand::Spell: {
        // "K A T Y", "K-A-T-Y", "K, A, T, Y": the letters, in the word's case.
        QString letters;
        for (const QChar ch : command.second) {
            if (ch.isLetterOrNumber() || ch == QLatin1Char('\'')) letters += ch;
        }
        if (letters.isEmpty()) return std::nullopt;
        letters = letters.toLower();
        result.replace(start, end - start, inCaseOf(matched, letters));
        break;
    }
    case VoiceCommand::Capitalize: {
        QString capital = matched;
        bool wordStart = true;
        for (QChar &ch : capital) {
            if (wordStart && ch.isLetter()) ch = ch.toUpper();
            wordStart = ch.isSpace() || ch == QLatin1Char('-');
        }
        result.replace(start, end - start, capital);
        break;
    }
    case VoiceCommand::DeleteText: {
        // With one space next to it: the one before, or at the start the one after.
        qsizetype from = start, to = end;
        if (from > 0 && result.at(from - 1) == QLatin1Char(' ')) --from;
        else if (to < result.size() && result.at(to) == QLatin1Char(' ')) ++to;
        const bool sentence = startsSentence(result, start) && matched.front().isUpper();
        result.remove(from, to - from);
        // "Well hello there" - "well" -> "Hello there".
        if (sentence && from < result.size()) {
            qsizetype next = from;
            while (next < result.size() && result.at(next) == QLatin1Char(' ')) ++next;
            if (next < result.size() && result.at(next).isLower()) result[next] = result.at(next).toUpper();
        }
        break;
    }
    case VoiceCommand::InsertBefore: {
        QString inserted = command.argument;
        // Before the first word of a sentence: the new words start it.
        if (startsSentence(text, start) && matched.front().isUpper()) {
            if (inserted.front().isLower()) inserted[0] = inserted.front().toUpper();
            const bool keepCase = matched.size() >= 2 && matched.at(1).isUpper();     // "NASA", "I'm" stays
            if (!keepCase && matched != QLatin1String("I") && !matched.startsWith(QLatin1String("I'"))) {
                result[start] = result.at(start).toLower();
            }
        }
        result.insert(start, inserted + QLatin1Char(' '));
        break;
    }
    case VoiceCommand::InsertAfter:
        result.insert(end, QLatin1Char(' ') + command.argument);
        break;
    default:
        break;
    }
    return result;
}

QString spokenEmojiAlias(const QString &name)
{
    static const QHash<QString, QString> aliases = [] {
        QHash<QString, QString> a;
        auto add = [&a](const char *emoji, std::initializer_list<const char *> names) {
            for (const char *n : names) a.insert(QString::fromUtf8(n), QString::fromUtf8(emoji));
        };
        add("\u2764\uFE0F", {"heart", "red heart", "love heart", "herz", "сердце", "сердечко", "серце", "серденько"});
        add("\U0001F603", {"smiley", "smiley face", "smiling face", "смайлик", "смайл"});
        add("\U0001F602", {"laughing", "laughing face", "lol", "lachen", "смех", "смеюсь", "сміх"});
        add("\U0001F622", {"crying", "crying face", "sad", "sad face", "weinen", "traurig", "плач", "грусть", "сум"});
        add("\U0001F618", {"kiss", "kissing face", "kuss", "поцелуй", "поцілунок"});
        add("\U0001F609", {"wink", "winking face", "zwinkern", "подмигивание", "підморгування"});
        add("\U0001F44D", {"thumbs up", "like", "daumen hoch", "лайк", "класс", "клас"});
        add("\U0001F64F", {"thank you", "thanks", "danke", "спасибо", "дякую"});
        return a;
    }();
    return aliases.value(normalized(name));
}

QString applySpokenPunctuation(const QString &dictation)
{
    QString text = dictation.trimmed();
    if (text.isEmpty()) return text;
    const auto options = QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption;
    // Anywhere: names that are never ordinary words. A mark Whisper put
    // right before or after the name goes with it.
    static const QList<QPair<QRegularExpression, QString>> anywhere = {
        {QRegularExpression(QString::fromUtf8(
             R"(\s*(?:[,.;:]\s*)?(?<!\p{L})(?:question mark|fragezeichen|вопросительный знак|знак питання)(?!\p{L})[.,!?]?)"), options),
         QStringLiteral("?")},
        {QRegularExpression(QString::fromUtf8(
             R"(\s*(?:[,.;:]\s*)?(?<!\p{L})(?:exclamation mark|exclamation point|ausrufezeichen|восклицательный знак|знак оклику)(?!\p{L})[.,!?]?)"),
             options),
         QStringLiteral("!")},
    };
    for (const auto &[pattern, mark] : anywhere) text.replace(pattern, mark);
    // Only as the last words: "comma" and "full stop" could not be meant as
    // words there. ("period", "Punkt", "точка", "крапка" can: "during that
    // period", "ein wichtiger Punkt"; Whisper writes the full stop itself.)
    static const QRegularExpression atEnd(QString::fromUtf8(
        R"(\s*(?:[,.;:]\s*)?(?<!\p{L})(comma|komma|запятая|кома|full stop)(?!\p{L})[.,!?]?\s*$)"), options);
    const QRegularExpressionMatch end = atEnd.match(text);
    if (end.hasMatch()) {
        const QString word = end.captured(1).toLower();
        text.replace(end.capturedStart(0), end.capturedLength(0),
                     word == QLatin1String("full stop") ? QStringLiteral(".") : QStringLiteral(","));
    }
    // A space after a mark that ends a sentence, and a capital after it.
    static const QRegularExpression afterMark(QStringLiteral(R"(([?!])\s*(\p{Ll}))"), QRegularExpression::UseUnicodePropertiesOption);
    QRegularExpressionMatch m;
    qsizetype from = 0;
    while ((m = afterMark.match(text, from)).hasMatch()) {
        const QString replacement = m.captured(1) + QLatin1Char(' ') + m.captured(2).toUpper();
        text.replace(m.capturedStart(0), m.capturedLength(0), replacement);
        from = m.capturedStart(0) + replacement.size();
    }
    return text;
}

}
