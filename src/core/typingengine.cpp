// SPDX-License-Identifier: GPL-3.0-or-later

#include "typingengine.h"
#include "capsmode.h"

#include <algorithm>
#include <utility>
#include "keyboardcontroller.h"

#include <QElapsedTimer>

#include <limits>
#include <QLoggingCategory>

// Decisions and sizes only; typed text never reaches the log.
Q_LOGGING_CATEGORY(lcEngine, "tastra.engine", QtWarningMsg)

namespace Tastra
{
namespace
{

constexpr int ModelTail = 256;
constexpr int CompareTail = 64;
constexpr int MaxHistory = 64;
// Echoes of our own edits arrive 2-6 ms later (p99, device trace 0.2.5; max
// 36 ms). Unknown states inside this window are self-caused, not user edits.
constexpr qint64 SettleMs = 150;

qint64 monotonicMs()
{
    static QElapsedTimer timer = [] { QElapsedTimer t; t.start(); return t; }();
    return timer.elapsed();
}

QString tail(const QString &text)
{
    return text.size() > ModelTail ? text.right(ModelTail) : text;
}

bool sameTextState(const QString &a, const QString &b)
{
    if (a.size() >= CompareTail && b.size() >= CompareTail) return a.right(CompareTail) == b.right(CompareTail);
    return a == b;
}

// LatinIME's word connectors: inside a word they belong to it ("don't",
// "кто-то", "E-Mail").
bool isWordConnector(QChar ch)
{
    return ch == QLatin1Char('\'') || ch == QLatin1Char('-') || ch == QChar(0x2019) || ch == QChar(0x02BC);
}

// "you," -> "you": the word as the lexicon keeps it.
QString bareWord(const QString &text)
{
    qsizetype start = 0, end = text.size();
    while (start < end && !text.at(start).isLetter()) ++start;
    while (end > start && !text.at(end - 1).isLetter()) --end;
    return text.mid(start, end - start).toLower();
}

// The word right before the cursor (letters, and connectors between
// letters) and the word before that one, which is the context unless a
// sentence end or a line break lies between them.
struct WordsBeforeCursor {
    QString trailing;
    QString previous;
};

WordsBeforeCursor wordsBeforeCursor(const QString &before)
{
    auto inWord = [&before](qsizetype i) {
        const QChar ch = before.at(i);
        if (ch.isLetter()) return true;
        return isWordConnector(ch) && i > 0 && i + 1 < before.size() && before.at(i - 1).isLetter()
            && before.at(i + 1).isLetter();
    };
    WordsBeforeCursor words;
    qsizetype start = before.size();
    while (start > 0 && inWord(start - 1)) --start;
    words.trailing = before.mid(start);
    qsizetype prevEnd = start;
    while (prevEnd > 0 && !before.at(prevEnd - 1).isLetter()) --prevEnd;
    qsizetype prevStart = prevEnd;
    while (prevStart > 0 && inWord(prevStart - 1)) --prevStart;
    words.previous = before.mid(prevStart, prevEnd - prevStart).toLower();
    for (qsizetype i = prevEnd; i < start; ++i) {
        const QChar ch = before.at(i);
        if (ch == QLatin1Char('.') || ch == QLatin1Char('!') || ch == QLatin1Char('?') || ch == QLatin1Char('\n')
            || ch == QChar(0x2026)) {
            words.previous.clear();
            break;
        }
    }
    return words;
}

}

TypingEngine::TypingEngine(KeyboardController &controller)
    : m_controller(controller)
{
    refreshSuggestions();
}

void TypingEngine::setLanguage(const QString &code)
{
    m_lexicon.setLanguage(code);
    resetComposition();
}

QString TypingEngine::language() const { return m_lexicon.language(); }

void TypingEngine::setSuggestionsEnabled(bool enabled)
{
    m_suggestionsEnabled = enabled;
    refreshSuggestions();
}
void TypingEngine::setAutocorrectEnabled(bool enabled) { m_autocorrectEnabled = enabled; }
void TypingEngine::setLearningEnabled(bool enabled) { m_learningEnabled = enabled; }
void TypingEngine::setAutoCapitalizationEnabled(bool enabled) { m_autoCapitalizationEnabled = enabled; }
void TypingEngine::setDoubleSpacePeriodEnabled(bool enabled) { m_doubleSpacePeriodEnabled = enabled; }
void TypingEngine::setAutoCapitalizationAllowed(bool allowed) { m_autoCapitalizationAllowed = allowed; }
bool TypingEngine::surroundingTextSupported() const { return m_surroundingSupported; }
void TypingEngine::setClockForTesting(std::function<qint64()> clock) { m_clock = std::move(clock); }
void TypingEngine::setBlockOffensive(bool enabled) { m_lexicon.setBlockOffensive(enabled); refreshSuggestions(); }
void TypingEngine::setKeyboardRows(const QStringList &rows) { m_lexicon.setKeyboardRows(rows); }
void TypingEngine::setAutoSpaceAfterPunctuation(bool enabled) { m_autoSpaceAfterPunctuation = enabled; m_autoSpacePending = false; }
void TypingEngine::forgetWord(const QString &word) { m_lexicon.forgetWord(word); refreshSuggestions(); }
bool TypingEngine::addUserWord(const QString &word) { const bool ok = m_lexicon.addUserWord(word); refreshSuggestions(); return ok; }
void TypingEngine::removeUserWord(const QString &word) { m_lexicon.removeUserWord(word); refreshSuggestions(); }
QStringList TypingEngine::userWords() const { return m_lexicon.userWords(); }
bool TypingEngine::isKnownWord(const QString &word) const { return m_lexicon.hasWord(word); }
void TypingEngine::reloadUserDictionaryFile() { m_lexicon.reloadUserDictionaryFile(); refreshSuggestions(); }

void TypingEngine::setCompositionEnabled(bool enabled) { if (!enabled) commitComposition(); m_compositionEnabled = enabled; }
bool TypingEngine::compositionEnabled() const { return m_compositionEnabled; }
bool TypingEngine::composing() const { return m_composing || m_pendingSpace; }

bool TypingEngine::compositionAvailable() const
{
    // Preedit needs a text-input client (KWin drops it for the fake-key path)
    // and must never show secrets in clear text.
    return m_compositionEnabled && m_surroundingSupported && !m_sensitiveContext && !m_preeditRejected;
}

bool TypingEngine::setPreeditLocal(const QString &text)
{
    m_lastLocalOpMs = m_clock ? m_clock() : monotonicMs();
    if (!m_controller.setPreedit(text)) {
        qCDebug(lcEngine) << "preedit rejected -> immediate commits for this context";
        m_preeditRejected = true;
        return false;
    }
    // Clients such as Firefox include the preedit in their surrounding text.
    if (!text.isEmpty()) recordState(tail(m_model + text));
    return true;
}

QString TypingEngine::corrected(const QString &word) const
{
    if (!m_autocorrectEnabled || word.isEmpty()) return word;
    // "dogs'" or "Ost-" (as in "Ost- und Westeuropa"): left as typed.
    if (isWordConnector(word.back())) return word;
    if (!m_noCorrectionFor.isEmpty() && word.compare(m_noCorrectionFor, Qt::CaseInsensitive) == 0) return word;
    const QString correction = m_lexicon.bestCorrection(word, contextWord());
    if (correction.isEmpty()) return word;
    QString formatted = correction;
    if (word.front().isUpper()) formatted[0] = formatted.at(0).toUpper();
    return formatted;
}

// Returns what is held in the preedit after a word (corrected word, if any,
// plus the space) and clears that state. The caller commits it.
QString TypingEngine::takePendingText()
{
    QString text = m_pendingWord;
    if (!m_pendingWord.isEmpty()) {
        rememberCorrection(m_pendingOriginal, m_pendingWord);
        m_currentWord = m_pendingWord;
        finalizeCurrentWord();
    }
    text = stripHead(text);
    m_committedHead.clear();
    if (m_pendingPeriod) text += QLatin1Char('.');
    text += m_pendingPunct;
    m_pendingPunct.clear();
    m_pendingWord.clear();
    m_pendingOriginal.clear();
    m_pendingSpace = false;
    m_pendingPeriod = false;
    return text;
}

void TypingEngine::insertDictation(const QString &text)
{
    dropGlideAlternatives();
    QString phrase = text.simplified();
    if (phrase.isEmpty()) return;
    const bool hadWord = (m_composing && !m_currentWord.isEmpty()) || !m_currentWord.isEmpty();
    commitComposition();
    if (!m_sensitiveContext && m_sentenceStart && m_autoCapitalizationEnabled && m_autoCapitalizationAllowed) {
        phrase[0] = phrase.at(0).toUpper();
    }
    // A space before the phrase after a word or a mark ("Hi." + "Bye."), not
    // at the start of a line or before a mark the phrase begins with.
    const bool glued = !m_model.isEmpty() && !m_model.back().isSpace() && !phrase.front().isPunct();
    const QString separator = hadWord || glued ? QStringLiteral(" ") : QString();
    commitLocal(separator + phrase + QLatin1Char(' '));
    // The sentence ends at . ! ? or …, also inside closing quotes or brackets.
    QString trimmed = phrase;
    while (trimmed.size() > 1 && QStringLiteral("\"'»”’)]").contains(trimmed.back())) trimmed.chop(1);
    const QChar last = trimmed.back();
    m_sentenceStart = last == QLatin1Char('.') || last == QLatin1Char('!') || last == QLatin1Char('?') || last == QChar(0x2026);
    m_sentencePunctuationPending = false;
    const QStringList words = phrase.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    m_previousWord = words.isEmpty() || m_sentenceStart ? QString() : bareWord(words.last());
    m_currentWord.clear();
    m_lastActionWasSpace = false;
    m_spaceFromSuggestion = false;
    refreshSuggestions();
}

void TypingEngine::deleteLastWord()
{
    commitComposition();
    // The word with what sticks to it ("world."), and the spaces and line
    // breaks after it.
    qsizetype end = m_model.size();
    while (end > 0 && m_model.at(end - 1).isSpace()) --end;
    qsizetype start = end;
    while (start > 0 && !m_model.at(start - 1).isSpace()) --start;
    if (start == m_model.size()) return;
    replaceBeforeCursor(m_model.mid(start), QString(), true);
    afterVoiceDeletion();
}

void TypingEngine::deleteLastSentence()
{
    commitComposition();
    // Back over the sentence's own end mark, then to the previous one; the
    // space after that mark goes too ("Hi. How are you? " -> "Hi.").
    auto ends = [](QChar ch) {
        return ch == QLatin1Char('.') || ch == QLatin1Char('!') || ch == QLatin1Char('?') || ch == QChar(0x2026)
            || ch == QLatin1Char('\n');
    };
    qsizetype i = m_model.size();
    while (i > 0 && m_model.at(i - 1).isSpace() && m_model.at(i - 1) != QLatin1Char('\n')) --i;
    while (i > 0 && ends(m_model.at(i - 1)) && m_model.at(i - 1) != QLatin1Char('\n')) --i;
    while (i > 0 && !ends(m_model.at(i - 1))) --i;
    if (i == m_model.size()) return;
    replaceBeforeCursor(m_model.mid(i), QString(), true);
    afterVoiceDeletion();
}

void TypingEngine::deleteKnownText()
{
    commitComposition();
    if (m_model.isEmpty()) return;
    replaceBeforeCursor(m_model, QString(), true);
    afterVoiceDeletion();
}

QString TypingEngine::knownTextBeforeCursor()
{
    commitComposition();
    if (m_currentWord.size()) finalizeCurrentWord();
    return m_model;
}

void TypingEngine::replaceKnownText(const QString &edited)
{
    commitComposition();
    qsizetype shared = 0;
    while (shared < m_model.size() && shared < edited.size() && m_model.at(shared) == edited.at(shared)) ++shared;
    const QString existing = m_model.mid(shared);
    const QString replacement = edited.mid(shared);
    if (existing.isEmpty() && replacement.isEmpty()) return;
    if (existing.isEmpty()) commitLocal(replacement);
    else replaceBeforeCursor(existing, replacement, true);
    afterVoiceDeletion();
}

void TypingEngine::afterVoiceDeletion()
{
    m_currentWord.clear();
    const WordsBeforeCursor words = wordsBeforeCursor(m_model);
    m_previousWord = words.trailing.isEmpty() ? words.previous : words.trailing.toLower();
    m_lastActionWasSpace = false;
    rearmSentenceStartFromText();
    refreshSuggestions();
}

void TypingEngine::insertLineBreak(const QString &breakText)
{
    commitComposition();
    if (m_currentWord.size()) finalizeCurrentWord();
    // The space after the last word or dictation becomes the break.
    if (!m_model.endsWith(QLatin1Char(' ')) || !replaceBeforeCursor(QStringLiteral(" "), breakText, true)) {
        commitLocal(breakText);
    }
    m_sentenceStart = true;
    m_previousWord.clear();
    m_lastActionWasSpace = false;
    refreshSuggestions();
}

void TypingEngine::commitComposition()
{
    dropGlideAlternatives();
    if (m_composing && !m_currentWord.isEmpty()) {
        const QString word = m_currentWord;
        finalizeCurrentWord();
        commitLocal(stripHead(word));
        m_committedHead.clear();
    } else if (m_pendingSpace) {
        commitLocal(takePendingText() + QLatin1Char(' '));
    }
    m_composing = false;
    m_pendingSpace = false;
}

bool TypingEngine::suggestionsEnabled() const { return m_suggestionsEnabled; }

void TypingEngine::setNextWordSuggestionsEnabled(bool enabled)
{
    if (m_nextWordSuggestions == enabled) return;
    m_nextWordSuggestions = enabled;
    refreshSuggestions();
}
bool TypingEngine::autocorrectEnabled() const { return m_autocorrectEnabled; }
bool TypingEngine::learningEnabled() const { return m_learningEnabled; }
bool TypingEngine::autoCapitalizationEnabled() const { return m_autoCapitalizationEnabled; }
bool TypingEngine::doubleSpacePeriodEnabled() const { return m_doubleSpacePeriodEnabled; }
bool TypingEngine::wantsAutoUppercase() const
{
    if (m_sensitiveContext || !m_autoCapitalizationEnabled || !m_autoCapitalizationAllowed
        || !m_currentWord.isEmpty() || lostWordResumes()) {
        return false;
    }
    if (!m_surroundingSupported) return m_sentenceStart;   // text before the cursor unknown
    // The client reports its text: decide like LatinIME (abbreviations,
    // quotes, paragraph starts, German rules) on what precedes the cursor,
    // including a space or ". " still held in the preedit.
    QString before = m_model;
    if (m_pendingSpace) {
        before += stripHead(m_pendingWord);
        if (m_pendingPeriod) before += QLatin1Char('.');
        before += QLatin1Char(' ');
    }
    const QString lang = language();
    return CapsMode::sentenceCaps(before, false, lang == QStringLiteral("en"), lang == QStringLiteral("de"));
}
bool TypingEngine::sensitiveContext() const { return m_sensitiveContext; }
QString TypingEngine::currentWord() const { return m_currentWord; }
QString TypingEngine::previousWord() const { return m_previousWord; }
QStringList TypingEngine::suggestions() const { return m_suggestions; }
QStringList TypingEngine::glideTrace() const { return m_glideTrace; }


QString TypingEngine::contextWord() const
{
    // A corrected word held with its space (Backspace can still restore what
    // was typed) is learned only when the next key comes, but it is already
    // the word before the cursor: "so i" + Space predicts after "I".
    if (m_pendingSpace && !m_pendingWord.isEmpty()) {
        const QString last = bareWord(m_pendingWord.section(QLatin1Char(' '), -1));
        if (!last.isEmpty()) return last;
    }
    return m_previousWord;
}

QString TypingEngine::formatForSentence(const QString &word) const
{
    if (!m_autoCapitalizationEnabled || !m_autoCapitalizationAllowed || !m_sentenceStart || word.isEmpty()) return word;
    QString result = word;
    result[0] = result.at(0).toUpper();
    return result;
}

bool TypingEngine::lostWordResumes() const
{
    // See rememberCompositionBeforeDeactivation: nothing typed since, the
    // client's text is known again and does not hold the word (or holds just
    // its committed first letter, where it was).
    if (m_lostWord.isEmpty() || !m_surroundingSupported || m_composing || m_pendingSpace
        || !m_wordAfterCursor.isEmpty() || m_lastLocalOpMs >= m_lostAtMs) {
        return false;
    }
    constexpr qint64 BounceMs = 2000;
    if ((m_clock ? m_clock() : monotonicMs()) - m_lostAtMs > BounceMs) return false;
    if (m_model.endsWith(m_lostWord)) return false;                // the client kept it
    // A committed first letter stays in the text even when the client
    // reports an older text after switching back on (GTK's cache).
    return m_currentWord.isEmpty() || (!m_lostHead.isEmpty() && m_currentWord == m_lostHead);
}

void TypingEngine::typeLetter(const QString &text, QPointF touch)
{
    if (lostWordResumes()) {
        // The client threw the composed word away when its text input went
        // off and on: it is composed again, and this letter continues it.
        qCDebug(lcEngine) << "word dropped by the client on deactivation -> composing again," << m_lostWord.size() << "letters";
        m_currentWord = m_lostWord;
        m_committedHead = m_lostHead;
        m_composing = true;
        m_touchOffsets.clear();
    }
    m_lostWord.clear();
    m_lostHead.clear();
    dropGlideAlternatives();
    const qsizetype wordBefore = m_currentWord.size();
    m_lastWasDoublePeriod = false;
    if (text.isEmpty()) return;
    m_spaceFromSuggestion = false;
    if (m_sensitiveContext) {
        commitLocal(text);
        m_currentWord.clear();
        m_suggestions.clear();
        m_sentenceStart = false;
        m_lastActionWasSpace = false;
        return;
    }
    if (m_pendingSpace) commitLocal(takePendingText() + QLatin1Char(' '));
    if (m_autoSpacePending) {
        m_autoSpacePending = false;
        commitLocal(QStringLiteral(" "));
    }
    if (!m_composing && !m_currentWord.isEmpty() && m_wordAfterCursor.isEmpty() && compositionAvailable()
        && !recomposeCurrentWord() && m_currentWord == m_resumedWord) {
        // The client has not confirmed the word Backspace reached: rather
        // than typing on it uncomposed, start a new word as before.
        m_currentWord.clear();
        m_previousWord = m_previousBeforeResume;
    }
    m_resumedWord.clear();
    // Composition starts only at a word boundary so a word is never split
    // between committed text and preedit.
    const bool startingWord = !m_composing && m_currentWord.isEmpty() && compositionAvailable();
    if (startingWord) m_composing = true;
    m_currentWord += text;
    if (startingWord && atEmptyParagraph()) {
        commitLocal(text);                         // see m_committedHead
        m_committedHead = text;
    } else if (m_composing && !setPreeditLocal(stripHead(m_currentWord))) {
        m_composing = false;
        commitLocal(stripHead(m_currentWord));
        m_committedHead.clear();
    } else if (!m_composing) {
        commitLocal(text);
    }
    m_sentenceStart = false;
    m_sentencePunctuationPending = false;
    m_lastActionWasSpace = false;
    if (m_currentWord.size() == wordBefore + 1) {
        m_touchOffsets.resize(wordBefore);
        m_touchOffsets.append(touch);
    }
    refreshSuggestions();
}

void TypingEngine::typeText(const QString &text)
{
    // An apostrophe or hyphen right after a letter of the word being typed
    // continues the word, as LatinIME's word connectors do: "don't" is one
    // word for suggestions and autocorrect, not "don" and "t".
    if (text.size() == 1 && isWordConnector(text.front()) && !m_sensitiveContext && !m_pendingSpace
        && m_wordAfterCursor.isEmpty() && !m_currentWord.isEmpty() && m_currentWord.back().isLetter()) {
        typeLetter(text);
        return;
    }
    dropGlideAlternatives();
    m_resumedWord.clear();
    m_wordAfterCursor.clear();
    m_lastWasDoublePeriod = false;
    if (text.isEmpty()) return;
    m_spaceFromSuggestion = false;
    if (m_sensitiveContext) {
        commitLocal(text);
        m_currentWord.clear();
        m_suggestions.clear();
        m_lastActionWasSpace = false;
        return;
    }

    const bool punctuation = text.size() == 1 && QStringLiteral(".,!?;:").contains(text);
    m_autoSpacePending = false;
    // Only after words: "3.5" or "e.g" style tokens stay intact.
    const bool afterLetter = !m_currentWord.isEmpty() || (!m_model.isEmpty() && m_model.back().isLetter());
    const bool armAutoSpace = m_autoSpaceAfterPunctuation && punctuation && afterLetter && !m_sensitiveContext;
    if (m_composing) {
        // Word and punctuation leave in one commit; the word is corrected first.
        const QString word = punctuation ? corrected(m_currentWord) : m_currentWord;
        rememberCorrection(m_currentWord, word);
        m_currentWord = word;
        finalizeCurrentWord();
        m_composing = false;
        commitLocal(stripHead(word) + text);
        m_committedHead.clear();
    } else if (m_pendingSpace) {
        const bool afterWord = m_pendingSpaceAfterWord;
        const QString held = takePendingText();
        if (punctuation && afterWord) {
            // LatinIME/Gboard: "word ," becomes "word, " — the space typed
            // after the word moves behind the punctuation (held again so a
            // following Space only confirms it).
            // The punctuation is held with the space so Backspace can undo
            // the swap without deleting committed text (LatinIME
            // revertSwapPunctuation: "word, " -> "word ,").
            if (!held.isEmpty()) commitLocal(held);
            if (setPreeditLocal(text + QLatin1Char(' '))) {
                m_pendingSpace = true;
                m_pendingPunct = text;
                m_spaceFromSuggestion = true;
                m_pendingSpaceAfterWord = false;
            } else {
                commitLocal(text + QLatin1Char(' '));
            }
            observePunctuation(text);
            m_lastActionWasSpace = false;
            refreshSuggestions();
            return;
        }
        commitLocal(held + QStringLiteral(" ") + text);
    } else {
        if (punctuation && !m_currentWord.isEmpty()) finalizeCurrentWord();
        commitLocal(text);
    }
    observePunctuation(text);
    m_lastActionWasSpace = false;
    m_autoSpacePending = armAutoSpace;
    refreshSuggestions();
}

void TypingEngine::observePunctuation(const QString &text)
{
    if (text.contains(QLatin1Char('.')) || text.contains(QLatin1Char('!')) || text.contains(QLatin1Char('?'))) {
        m_sentencePunctuationPending = true;
        // Arm capitalization immediately. This also covers clients that do not
        // send a fresh surrounding-text event between punctuation and the next tap.
        m_sentenceStart = true;
        // A new sentence has no previous word (LatinIME: beginning-of-sentence
        // context), so neither suggestions nor learning pair across it.
        m_previousWord.clear();
    }
}

void TypingEngine::commitLocal(const QString &text)
{
    m_lastLocalOpMs = m_clock ? m_clock() : monotonicMs();
    m_controller.tapText(text);
    m_model = tail(m_model + text);
    recordLocalState();
}

void TypingEngine::backspaceLocal()
{
    m_lastLocalOpMs = m_clock ? m_clock() : monotonicMs();
    m_controller.backspace();
    if (!m_model.isEmpty()) {
        const bool pair = m_model.size() >= 2 && m_model.back().isLowSurrogate();
        m_model.chop(pair ? 2 : 1);
    }
    recordLocalState();
}

bool TypingEngine::deleteLocal(const QString &text)
{
    m_lastLocalOpMs = m_clock ? m_clock() : monotonicMs();
    if (!m_controller.deleteBeforeCursor(text)) return false;
    m_model.chop(text.size());
    recordLocalState();
    return true;
}

// Deletes text this keyboard committed right before the cursor. GTK 3 maps
// the deletion's bytes onto the text it last read from the widget, which in
// Firefox can be from before that commit; unless the client has reported
// the text back since, an empty commit first makes GTK read it again
// (imwayland.c: any commit at done -> notify_im_change).
bool TypingEngine::deleteOwnText(const QString &text)
{
    if (m_surroundingSupported && !(inSyncWithClient() && m_model.endsWith(text))) commitLocal(QString());
    return deleteLocal(text);
}

void TypingEngine::recordLocalState()
{
    m_inSync = false;
    recordState(m_model);
}

void TypingEngine::recordState(const QString &state)
{
    if (!m_history.isEmpty() && m_history.back() == state) return;
    m_history.append(state);
    if (m_history.size() > MaxHistory) m_history.removeFirst();
}

void TypingEngine::forgetTextState()
{
    m_model.clear();
    m_history = {QString()};
    m_inSync = false;
    // The keyboard no longer models the text (cursor moved, paste, client
    // reset): the next echo is the truth and must not be treated as
    // self-caused by the settle window.
    m_lastLocalOpMs = std::numeric_limits<qint64>::min() / 2;
}

// AOSP LatinIME InputLogic.tryPerformDoubleSpacePeriod (Apache-2.0): the
// second space must come within 1100 ms, and the character before the first
// space must be a letter/digit, one of ' " ) ] } > + % or a symbol (emoji).
bool TypingEngine::doubleSpaceAllowedAfter(const QString &textBeforeSpace) const
{
    if (!m_doubleSpacePeriodEnabled || !m_lastActionWasSpace || m_previousSpaceMs < 0) return false;
    if (m_lastSpaceMs - m_previousSpaceMs >= 1100) return false;
    if (textBeforeSpace.isEmpty()) return false;
    const QChar last = textBeforeSpace.back();
    if (last.isLowSurrogate()) return true;                      // emoji
    return last.isLetterOrNumber() || QStringLiteral("'\")]}>+%").contains(last)
        || last.category() == QChar::Symbol_Other;
}

bool TypingEngine::replaceWordAroundCursor(const QString &replacement)
{
    // Only on confirmed client text: a stale echo must never cut a word.
    if (!m_surroundingSupported || !inSyncWithClient() || !m_model.endsWith(m_currentWord)) return false;
    if (!m_controller.deleteAroundCursor(m_currentWord, m_wordAfterCursor)) return false;
    m_model.chop(m_currentWord.size());
    commitLocal(replacement);
    return true;
}

void TypingEngine::rememberCorrection(const QString &typed, const QString &corrected)
{
    if (typed.isEmpty() || corrected.isEmpty() || typed == corrected) return;
    for (qsizetype i = 0; i < m_recentCorrections.size(); ++i) {
        if (m_recentCorrections.at(i).first == corrected) { m_recentCorrections.removeAt(i); break; }
    }
    m_recentCorrections.prepend({corrected, typed});
    while (m_recentCorrections.size() > 20) m_recentCorrections.removeLast();
}

bool TypingEngine::atEmptyParagraph() const
{
    return m_surroundingSupported && (m_model.isEmpty() || m_model.endsWith(QLatin1Char('\n')));
}

QString TypingEngine::stripHead(const QString &text) const
{
    if (!m_committedHead.isEmpty() && text.startsWith(m_committedHead)) return text.mid(m_committedHead.size());
    return text;
}

// After deleting, an empty field (or one ending in . ! ?) starts a sentence
// again (Gboard). Only for clients that report their text: otherwise the
// keyboard cannot know what precedes the cursor.
void TypingEngine::rearmSentenceStartFromText()
{
    if (!m_surroundingSupported || !m_currentWord.isEmpty() || m_composing) return;
    QString before = m_model;
    while (!before.isEmpty() && before.back().isSpace()) before.chop(1);
    m_sentenceStart = before.isEmpty() || before.endsWith(QLatin1Char('.'))
        || before.endsWith(QLatin1Char('!')) || before.endsWith(QLatin1Char('?'));
}

bool TypingEngine::inSyncWithClient() const
{
    return m_surroundingSupported && m_inSync;
}

// What the client last reported after the word at the cursor: the text
// after the cursor does not change while typing before it.
bool TypingEngine::textFollowsWord() const
{
    return !m_charAfterWord.isNull() && (m_charAfterWord.isSpace() || m_charAfterWord.isPunct());
}

bool TypingEngine::textFollowsCursor() const
{
    return m_wordAfterCursor.isEmpty() && textFollowsWord();
}

void TypingEngine::resumeWordBeforeCursor()
{
    // Only where the keyboard knows the text: a field that reports it.
    if (!m_surroundingSupported || m_sensitiveContext || m_composing || m_pendingSpace || !m_currentWord.isEmpty()
        || !m_wordAfterCursor.isEmpty()) {
        return;
    }
    const WordsBeforeCursor words = wordsBeforeCursor(m_model);
    if (words.trailing.isEmpty()) return;
    qCDebug(lcEngine) << "backspace reached a word ->" << words.trailing.size() << "letters resumed";
    m_resumedWord = words.trailing;
    m_previousBeforeResume = m_previousWord;
    m_currentWord = words.trailing;
    m_previousWord = words.previous;
    m_touchOffsets.clear();
    m_sentenceStart = false;
}

// The word is deleted with delete_surrounding_text and shown again as the
// preedit with the next letter: both travel on the text-input channel, so the
// client applies them in order. Only on a word the client has confirmed:
// GTK maps the deletion's bytes onto the text it last reported.
bool TypingEngine::recomposeCurrentWord()
{
    if (!inSyncWithClient() || !m_model.endsWith(m_currentWord)) return false;
    const QString word = m_currentWord;
    const QString before = m_model.left(m_model.size() - word.size());
    // A composition never starts in an empty paragraph (see m_committedHead):
    // the first letter stays committed there.
    const bool paragraphStart = before.isEmpty() || before.endsWith(QLatin1Char('\n'));
    const qsizetype keep = !paragraphStart ? 0 : (word.front().isHighSurrogate() && word.size() > 1 ? 2 : 1);
    const QString taken = word.mid(keep);
    if (!taken.isEmpty() && !deleteLocal(taken)) return false;
    qCDebug(lcEngine) << "typing on a confirmed word ->" << word.size() << "letters composed again";
    m_committedHead = word.left(keep);
    m_composing = true;
    return true;
}

// Replaces `existing` (the text right before the cursor) with `replacement`.
// On text-input clients the deletion uses delete_surrounding_text so it stays
// ordered with the commit; Backspace key events travel on a different channel
// that GTK/Firefox apply after already-received text-input commits.
bool TypingEngine::replaceBeforeCursor(const QString &existing, const QString &replacement, bool allowUnconfirmed)
{
    if (existing.isEmpty()) return false;
    if (m_surroundingSupported) {
        const bool confirmed = inSyncWithClient() && m_model.endsWith(existing);
        // A deliberate choice (a suggestion tap) on text the client has not
        // reported back yet, but which this keyboard committed: GTK 3 turns
        // the deletion's bytes into characters with the text it last asked
        // the widget for, and asks again after any commit. An empty commit
        // first makes it ask now, so the deletion is measured on the text as
        // it is (imwayland.c text_input_done / notify_im_change).
        const bool refreshed = !confirmed && allowUnconfirmed && m_model.endsWith(existing);
        qCDebug(lcEngine) << "replace len" << existing.size() << "->" << replacement.size()
                          << (confirmed ? "text-channel"
                                        : refreshed ? "text-channel after refresh"
                                                    : allowUnconfirmed ? "key-fallback" : "skipped");
        if ((confirmed || refreshed) && deleteOwnText(existing)) {
            commitLocal(replacement);
            return true;
        }
        if (!confirmed && !allowUnconfirmed) return false;
    }
    for (int i = 0; i < existing.size(); ++i) backspaceLocal();
    commitLocal(replacement);
    return true;
}

void TypingEngine::finalizeCurrentWord(bool updatePrevious)
{
    if (m_currentWord.isEmpty()) return;
    // A correction can be two words ("ofthe" -> "of the"): each is learned
    // after the one before it, and the next word follows the last.
    const QStringList words = m_currentWord.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QString previous = m_previousWord;
    for (const QString &finalized : words) {
        // Persist one combined learning update instead of serializing the full
        // learned maps twice for every completed word.
        if (m_learningEnabled) m_lexicon.learnWordWithContext(finalized, previous);
        previous = finalized;
    }
    if (updatePrevious && !words.isEmpty()) m_previousWord = bareWord(previous);
    m_currentWord.clear();
}

void TypingEngine::space()
{
    dropGlideAlternatives();
    // A word Backspace reached and nothing typed on it since.
    const bool resumedUnchanged = !m_resumedWord.isEmpty() && m_currentWord == m_resumedWord && !m_composing;
    m_resumedWord.clear();
    // A Space inside a word splits it: never autocorrect the left part.
    const bool splittingWord = !m_wordAfterCursor.isEmpty() && !m_composing;
    m_wordAfterCursor.clear();
    m_lastWasDoublePeriod = false;
    // Every Space press restarts the LatinIME double-space countdown.
    m_previousSpaceMs = m_lastSpaceMs;
    m_lastSpaceMs = m_clock ? m_clock() : monotonicMs();
    m_autoSpacePending = false;
    if (m_spaceFromSuggestion) {
        // The suggestion already added a space. This Space confirms it and
        // counts as the user's first Space; only the next one makes ". ".
        m_spaceFromSuggestion = false;
        if (!m_pendingPunct.isEmpty()) {
            // The user confirmed the swapped space: the swap is final, so a
            // later Backspace deletes that space instead of undoing the swap.
            commitLocal(m_pendingPunct);
            m_pendingPunct.clear();
            if (!setPreeditLocal(QStringLiteral(" "))) {
                commitLocal(QStringLiteral(" "));
                m_pendingSpace = false;
            }
        }
        m_lastActionWasSpace = true;
        refreshSuggestions();
        return;
    }
    if (m_sensitiveContext) {
        commitLocal(QStringLiteral(" "));
        m_currentWord.clear();
        m_suggestions.clear();
        return;
    }
    if (m_composing) {
        const QString original = m_currentWord;
        const QString candidate = corrected(original);
        // Never correct away the already committed first letter.
        const QString word = (!m_committedHead.isEmpty() && !candidate.startsWith(m_committedHead)) ? original : candidate;
        m_composing = false;
        m_noCorrectionFor.clear();
        if (word != original && setPreeditLocal(stripHead(word) + QLatin1Char(' '))) {
            // Keep the correction revertible: Backspace right now restores
            // what was typed; any other key commits it. Nothing is deleted.
            m_pendingWord = word;
            m_pendingOriginal = original;
            m_pendingSpace = true;
            m_currentWord.clear();
        } else {
            m_currentWord = word;
            finalizeCurrentWord();
            commitLocal(stripHead(word));
            m_committedHead.clear();
            // Hold the space in the preedit: a second Space can then become
            // ". " with a plain commit, no deletion needed.
            if (setPreeditLocal(QStringLiteral(" "))) m_pendingSpace = true;
            else commitLocal(QStringLiteral(" "));
        }
        m_pendingSpaceAfterWord = m_pendingSpace;
        m_lastActionWasSpace = true;
        if (m_sentencePunctuationPending) {
            m_sentenceStart = true;
            m_sentencePunctuationPending = false;
        }
        refreshSuggestions();
        return;
    }
    if (m_pendingSpace) {
        const QString before = m_pendingWord.isEmpty() ? m_model : m_pendingWord;
        const bool doubleSpace = !m_pendingPeriod && doubleSpaceAllowedAfter(before);
        const QString held = takePendingText();
        if (doubleSpace) {
            // Held as ". " in the preedit so Backspace can revert it to " "
            // without deleting committed text (LatinIME revertDoubleSpacePeriod).
            if (!held.isEmpty()) commitLocal(held);
            if (setPreeditLocal(QStringLiteral(". "))) {
                m_pendingSpace = true;
                m_pendingPeriod = true;
            } else {
                commitLocal(QStringLiteral(". "));
            }
            m_lastSpaceMs = -1;               // no chained double space
            m_pendingSpaceAfterWord = false;
            m_sentenceStart = true;
            m_wordBeforePeriod = m_previousWord;      // Backspace gives it back
            m_previousWord.clear();
            m_sentencePunctuationPending = false;
            m_lastActionWasSpace = false;
            refreshSuggestions();
            return;
        }
        commitLocal(held + QLatin1Char(' '));
        if (setPreeditLocal(QStringLiteral(" "))) m_pendingSpace = true;
        else commitLocal(QStringLiteral(" "));
        m_pendingSpaceAfterWord = false;            // consecutive spaces
        m_lastActionWasSpace = true;
        refreshSuggestions();
        return;
    }
    if (m_currentWord.isEmpty()) {
        const QString beforeSpace = m_model.endsWith(QLatin1Char(' ')) ? m_model.chopped(1) : QString();
        if (doubleSpaceAllowedAfter(beforeSpace)
            && replaceBeforeCursor(QStringLiteral(" "), QStringLiteral(". "), false)) {
            m_lastWasDoublePeriod = true;
            m_lastSpaceMs = -1;               // no chained double space
            m_sentenceStart = true;
            m_wordBeforePeriod = m_previousWord;      // Backspace gives it back
            m_previousWord.clear();
            m_sentencePunctuationPending = false;
            m_lastActionWasSpace = false;
            refreshSuggestions();
            return;
        }
        commitLocal(QStringLiteral(" "));
        if (m_sentencePunctuationPending) {
            m_sentenceStart = true;
            m_sentencePunctuationPending = false;
        }
        m_lastActionWasSpace = true;
        refreshSuggestions();
        return;
    }

    if (resumedUnchanged && !splittingWord && compositionAvailable()) {
        // The word is already in the text and was learned when it was first
        // typed: the space after it is held like after any typed word, so
        // that punctuation can still swap with it and a second Space can
        // become ". ".
        m_previousWord = bareWord(m_currentWord);
        m_currentWord.clear();
        if (setPreeditLocal(QStringLiteral(" "))) m_pendingSpace = true;
        else commitLocal(QStringLiteral(" "));
        m_pendingSpaceAfterWord = m_pendingSpace;
        m_lastActionWasSpace = true;
        if (m_sentencePunctuationPending) {
            m_sentenceStart = true;
            m_sentencePunctuationPending = false;
        }
        refreshSuggestions();
        return;
    }

    if (m_autocorrectEnabled && !splittingWord) {
        const QString correction = m_lexicon.bestCorrection(m_currentWord, contextWord());
        if (!correction.isEmpty()) {
            QString formatted = correction;
            if (!m_currentWord.isEmpty() && m_currentWord.front().isUpper()) {
                formatted[0] = formatted.at(0).toUpper();
            }
            // "I" typed, "I" suggested: nothing to replace.
            if (formatted != m_currentWord && replaceBeforeCursor(m_currentWord, formatted, false)) {
                rememberCorrection(m_currentWord, formatted);
                m_currentWord = formatted;
            }
        }
    }

    finalizeCurrentWord();
    commitLocal(QStringLiteral(" "));
    m_lastActionWasSpace = true;
    if (m_sentencePunctuationPending) {
        m_sentenceStart = true;
        m_sentencePunctuationPending = false;
    }
    refreshSuggestions();
}

void TypingEngine::backspace()
{
    if (glidedWordIsLast()) {
        eraseGlidedWord();
        return;
    }
    dropGlideAlternatives();
    m_autoSpacePending = false;
    m_spaceFromSuggestion = false;
    if (m_pendingSpace && !m_pendingPunct.isEmpty()) {
        // LatinIME revertSwapPunctuation: back to the typed order.
        const QString undone = QStringLiteral(" ") + m_pendingPunct;
        m_pendingPunct.clear();
        m_pendingSpace = false;
        m_spaceFromSuggestion = false;
        commitLocal(undone);                     // replaces the held ", " atomically
        m_lastActionWasSpace = false;
        refreshSuggestions();
        return;
    }
    if (m_pendingSpace && m_pendingPeriod) {
        // LatinIME: Backspace right after a double-space period gives a single
        // space, and the next word is not capitalised.
        m_pendingPeriod = false;
        setPreeditLocal(QStringLiteral(" "));
        m_pendingSpaceAfterWord = false;
        m_sentenceStart = false;
        m_previousWord = m_wordBeforePeriod;           // the sentence goes on
        m_lastActionWasSpace = false;
        refreshSuggestions();
        return;
    }
    if (m_lastWasDoublePeriod) {
        m_lastWasDoublePeriod = false;
        if (replaceBeforeCursor(QStringLiteral(". "), QStringLiteral(" "), false)) {
            m_sentenceStart = false;
            m_previousWord = m_wordBeforePeriod;       // the sentence goes on
            m_lastActionWasSpace = false;
            refreshSuggestions();
            return;
        }
    }
    if (m_pendingSpace && !m_pendingWord.isEmpty()) {
        // Undo the autocorrection: back to exactly what was typed, still
        // composing, and do not correct that word again.
        const QString original = m_pendingOriginal;
        m_pendingWord.clear();
        m_pendingOriginal.clear();
        m_pendingSpace = false;
        m_noCorrectionFor = original;
        // Undoing a correction is an explicit "this is my word" (Gboard).
        m_lexicon.promoteWord(original);
        m_currentWord = original;
        m_composing = true;
        setPreeditLocal(stripHead(original));
        m_lastActionWasSpace = false;
        rearmSentenceStartFromText();
        refreshSuggestions();
        return;
    }
    if (m_pendingSpace) {
        m_pendingSpace = false;
        setPreeditLocal(QString());
        m_lastActionWasSpace = false;
        rearmSentenceStartFromText();
        resumeWordBeforeCursor();
        refreshSuggestions();
        return;
    }
    if (m_composing && !m_currentWord.isEmpty() && m_currentWord.size() > m_committedHead.size()) {
        m_currentWord.chop(1);
        setPreeditLocal(stripHead(m_currentWord));
        if (m_currentWord.isEmpty()) m_composing = false;
        rearmSentenceStartFromText();
        resumeWordBeforeCursor();
        refreshSuggestions();
        return;
    }
    if (m_composing && !m_committedHead.isEmpty()) {
        m_composing = false;                       // only the committed head is left
        m_committedHead.clear();
    }
    backspaceLocal();
    if (m_sensitiveContext) {
        m_currentWord.clear();
        m_suggestions.clear();
        return;
    }
    if (!m_currentWord.isEmpty()) {
        const bool resumed = m_currentWord == m_resumedWord;
        m_currentWord.chop(1);
        m_resumedWord = resumed ? m_currentWord : QString();
    } else {
        m_previousWord.clear();
        m_sentencePunctuationPending = false;
    }
    m_lastActionWasSpace = false;
    rearmSentenceStartFromText();
    resumeWordBeforeCursor();
    refreshSuggestions();
}

void TypingEngine::backspaceRepeated(int count)
{
    dropGlideAlternatives();
    m_spaceFromSuggestion = false;
    int bounded = qBound(1, count, 32);
    if (m_pendingSpace && bounded > 0) {
        if (!m_pendingWord.isEmpty()) {
            m_currentWord = m_pendingOriginal;   // revert, then keep deleting
            m_noCorrectionFor = m_pendingOriginal;
            m_lexicon.promoteWord(m_pendingOriginal);
            m_pendingWord.clear();
            m_pendingOriginal.clear();
            m_composing = true;
        }
        m_pendingSpace = false;
        setPreeditLocal(m_composing ? stripHead(m_currentWord) : QString());
        --bounded;
    }
    if (m_composing && bounded > 0) {
        const int fromPreedit = qMin(bounded, int(m_currentWord.size() - m_committedHead.size()));
        m_currentWord.chop(fromPreedit);
        setPreeditLocal(stripHead(m_currentWord));
        bounded -= fromPreedit;
        if (m_currentWord.isEmpty()) m_composing = false;
        if (bounded > 0 && !m_committedHead.isEmpty()) {
            m_composing = false;                   // continue into the committed head
            m_committedHead.clear();
        }
        if (bounded == 0) {
            m_lastActionWasSpace = false;
            rearmSentenceStartFromText();
            resumeWordBeforeCursor();
            refreshSuggestions();
            return;
        }
    }
    for (int i = 0; i < bounded; ++i) backspaceLocal();

    if (m_sensitiveContext) {
        m_currentWord.clear();
        m_suggestions.clear();
        return;
    }

    if (!m_currentWord.isEmpty()) {
        const bool resumed = m_currentWord == m_resumedWord;
        const int remove = qMin(bounded, static_cast<int>(m_currentWord.size()));
        m_currentWord.chop(remove);
        m_resumedWord = resumed ? m_currentWord : QString();
        if (remove < bounded) m_previousWord.clear();
    } else {
        m_previousWord.clear();
        m_sentencePunctuationPending = false;
    }

    m_lastActionWasSpace = false;
    rearmSentenceStartFromText();
    resumeWordBeforeCursor();
    refreshSuggestions();
}

void TypingEngine::enter()
{
    dropGlideAlternatives();
    m_resumedWord.clear();
    m_wordAfterCursor.clear();
    m_lastWasDoublePeriod = false;
    m_autoSpacePending = false;
    m_spaceFromSuggestion = false;
    if (m_pendingSpace) {
        const QString held = takePendingText();
        if (!held.isEmpty()) commitLocal(held);      // corrected word, no trailing space
        else setPreeditLocal(QString());
    }
    // Commit before the Return key: GTK applies text-input commits before
    // queued key events, so this order can never be inverted.
    if (m_composing) commitComposition();
    if (!m_currentWord.isEmpty()) finalizeCurrentWord();
    m_lastLocalOpMs = m_clock ? m_clock() : monotonicMs();
    m_controller.enter();
    m_model = tail(m_model + QLatin1Char('\n'));
    recordLocalState();
    m_sentenceStart = true;
    m_previousWord.clear();
    m_sentencePunctuationPending = false;
    m_lastActionWasSpace = false;
    refreshSuggestions();
}

void TypingEngine::chooseSuggestion(const QString &word)
{
    if (word.isEmpty() || m_sensitiveContext) return;
    if (glideAlternativesShown() && m_glideAlternatives.contains(word)) {
        replaceGlidedWord(word);
        return;
    }
    dropGlideAlternatives();
    m_resumedWord.clear();
    QString selected = word;
    if (!m_currentWord.isEmpty() && m_currentWord.front().isUpper()) {
        selected[0] = selected.at(0).toUpper();
    } else if (m_sentenceStart) {
        selected = formatForSentence(selected);
    }

    const bool composed = m_composing;
    if (m_pendingSpace) commitLocal(takePendingText() + QLatin1Char(' '));
    if (m_composing) {
        m_composing = false;
        const QString head = m_committedHead;
        if (!head.isEmpty() && !selected.startsWith(head)) {
            // The choice changes the first letter, which is already committed
            // (see m_committedHead): end the preedit with an empty commit
            // (which also makes GTK read the text again), then replace the
            // letter on the same channel.
            commitLocal(QString());
            if (!deleteLocal(head)) {
                for (qsizetype i = 0; i < head.size(); ++i) backspaceLocal();
            }
            commitLocal(selected);
        } else {
            commitLocal(stripHead(selected));    // replaces the preedit atomically
        }
        m_committedHead.clear();
    } else if (!m_currentWord.isEmpty() && !m_wordAfterCursor.isEmpty()) {
        // The cursor is inside a word: replace all of it, or nothing.
        if (!replaceWordAroundCursor(selected)) {
            refreshSuggestions();
            return;
        }
        const bool followed = textFollowsWord();
        if (!m_recorrectionOriginal.isEmpty() && selected == m_recorrectionOriginal) m_lexicon.promoteWord(selected);
        m_wordAfterCursor.clear();
        m_currentWord = selected;
        finalizeCurrentWord();
        if (!followed) commitLocal(QStringLiteral(" "));
        m_spaceFromSuggestion = !followed;
        m_lastActionWasSpace = false;
        m_sentenceStart = false;
        refreshSuggestions();
        return;
    } else if (!m_currentWord.isEmpty()) {
        // A deliberate tap: fall back to key events if the client has not
        // confirmed the word yet rather than ignoring the user's choice.
        const bool restoring = !m_recorrectionOriginal.isEmpty() && selected == m_recorrectionOriginal;
        replaceBeforeCursor(m_currentWord, selected, true);
        if (restoring) {
            m_lexicon.promoteWord(selected);      // the user insists on it
            for (qsizetype i = 0; i < m_recentCorrections.size(); ++i) {
                if (m_recentCorrections.at(i).second == selected) { m_recentCorrections.removeAt(i); break; }
            }
        }
    } else {
        commitLocal(selected);
    }
    m_currentWord = selected;
    finalizeCurrentWord();
    // In the middle of the text, before a space or punctuation, the word
    // gets no space of its own (as when a word around the cursor is
    // replaced): it would stay behind as a second space.
    const bool followed = textFollowsCursor();
    m_spaceCommittedAfterSuggestion = false;
    if (followed) {
        m_pendingSpace = false;
    } else if (!(composed || compositionAvailable()) || !setPreeditLocal(QStringLiteral(" "))) {
        commitLocal(QStringLiteral(" "));
        m_spaceCommittedAfterSuggestion = true;
    } else {
        m_pendingSpace = true;
    }
    m_pendingSpaceAfterWord = m_pendingSpace;
    m_spaceFromSuggestion = !followed;
    m_lastActionWasSpace = false;
    m_sentenceStart = false;
    refreshSuggestions();
}

QString TypingEngine::autocorrectTarget() const { return m_autocorrectTarget; }
void TypingEngine::setForeignLayouts(const QList<QPair<QString, QHash<QChar, QChar>>> &maps) { m_foreignLayouts = maps; }
QString TypingEngine::layoutSuggestionWord() const { return m_layoutSuggestionWord; }
QString TypingEngine::layoutSuggestionLanguage() const { return m_layoutSuggestionLanguage; }
bool TypingEngine::currentWordKnown() const { return m_currentWordKnown; }

void TypingEngine::refreshSuggestions()
{
    refreshWordSuggestions();
    refreshPhraseSuggestion();
}

void TypingEngine::refreshPhraseSuggestion()
{
    m_phraseSuggestion.clear();
    if (m_sensitiveContext || !m_suggestionsEnabled || !m_phraseSuggestions || glideAlternativesShown()) return;
    if (!m_layoutSuggestionWord.isEmpty() || !m_recorrectionOriginal.isEmpty()) return;
    if (!m_currentWord.isEmpty()) {
        // Not inside a word: the phrase would be glued to its rest.
        if (!m_composing && !m_wordAfterCursor.isEmpty()) return;
        const bool allCaps = m_currentWord.size() >= 2 && m_currentWord.toUpper() == m_currentWord
            && m_currentWord.toLower() != m_currentWord;
        for (const QString &candidate : std::as_const(m_suggestions)) {
            if (candidate.isEmpty() || !candidate.front().isLetter() || candidate.contains(QLatin1Char(' '))) continue;
            if (!m_committedHead.isEmpty() && !candidate.startsWith(m_committedHead, Qt::CaseInsensitive)) continue;
            QStringList rest = m_lexicon.phraseAfter(candidate, 3);
            if (rest.isEmpty()) continue;
            QString continuation = rest.join(QLatin1Char(' '));
            if (allCaps) continuation = continuation.toUpper();
            m_phraseSuggestion = candidate + QLatin1Char(' ') + continuation;
            return;
        }
        return;
    }
    // After a Space: only a habit of two words or more (one word is what
    // the next-word suggestions already show), and not across a sentence.
    if (!m_nextWordSuggestions || m_sentenceStart) return;
    const QString context = contextWord();
    if (context.isEmpty()) return;
    const QStringList rest = m_lexicon.phraseAfter(context, 3);
    if (rest.size() >= 2) m_phraseSuggestion = rest.join(QLatin1Char(' '));
}

void TypingEngine::forgetPhrase(const QString &phrase)
{
    QStringList words = phrase.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    // After a Space the phrase starts with the word that follows the context.
    if (m_currentWord.isEmpty() && !contextWord().isEmpty()) words.prepend(contextWord());
    for (qsizetype i = 1; i < words.size(); ++i) m_lexicon.forgetPair(words.at(i - 1), words.at(i));
    refreshSuggestions();
}

void TypingEngine::setPhraseSuggestionsEnabled(bool enabled)
{
    if (m_phraseSuggestions == enabled) return;
    m_phraseSuggestions = enabled;
    refreshSuggestions();
}

void TypingEngine::refreshWordSuggestions()
{
    if (m_currentWord.isEmpty() && m_pendingWord.isEmpty()) m_committedHead.clear();
    // Touch offsets follow the word: shorter after Backspace, empty for a new one.
    if (m_touchOffsets.size() > m_currentWord.size()) m_touchOffsets.resize(m_currentWord.size());
    m_lexicon.setTouchOffsets(m_touchOffsets);
    // What Space would insert instead of the typed word (shown highlighted
    // in the middle of the strip, as Gboard does).
    m_autocorrectTarget.clear();
    if (!m_sensitiveContext && m_autocorrectEnabled && !m_currentWord.isEmpty() && m_wordAfterCursor.isEmpty()
        && (m_noCorrectionFor.isEmpty() || m_currentWord.compare(m_noCorrectionFor, Qt::CaseInsensitive) != 0)) {
        // Cheap preview per keystroke; Space runs the full Hunspell check.
        QString target = m_lexicon.correctionPreview(m_currentWord, contextWord());
        if (!target.isEmpty() && m_currentWord.front().isUpper()) target[0] = target.at(0).toUpper();
        if (!m_committedHead.isEmpty() && !target.startsWith(m_committedHead, Qt::CaseInsensitive)) target.clear();
        if (!target.isEmpty() && target != m_currentWord) m_autocorrectTarget = target;
    }
    if (m_sensitiveContext || !m_suggestionsEnabled) {
        m_suggestions.clear();
        return;
    }
    if (!m_currentWord.isEmpty()) {
        const bool midWord = !m_composing && !m_wordAfterCursor.isEmpty();
        const QString wholeWord = midWord ? m_currentWord + m_wordAfterCursor : m_currentWord;
        m_suggestions = m_lexicon.suggestions(wholeWord, contextWord(), 3);
        // Letters that can still become a word are not "unknown"; otherwise
        // cheap lists first and Hunspell (slow on invalid long words) last.
        const bool stillAPrefix = std::any_of(m_suggestions.cbegin(), m_suggestions.cend(), [&](const QString &s) {
            return s.size() > wholeWord.size() && s.startsWith(wholeWord, Qt::CaseInsensitive);
        });
        m_currentWordKnown = stillAPrefix || m_lexicon.hasWordCheaply(m_currentWord)
            || m_lexicon.knownInCompanionLanguage(m_currentWord) || m_lexicon.hasWord(m_currentWord);
        m_recorrectionOriginal.clear();
        // Typed with the wrong layout ("ghbdtn" -> "привет")? Offer the word
        // of the language whose keys were meant; checked only for unknown words.
        m_layoutSuggestionWord.clear();
        m_layoutSuggestionLanguage.clear();
        // Not while the letters may still become a word of this language
        // ("cer" -> "certain"): measured on the real lists, this rule and the
        // offensive-word filter remove every false reading of 6671 EN prefixes.
        if (!m_foreignLayouts.isEmpty() && wholeWord.size() >= 3 && !stillAPrefix) {
            for (const auto &[language, map] : std::as_const(m_foreignLayouts)) {
                QString mapped;
                for (const QChar ch : wholeWord) {
                    const QChar to = map.value(ch.toLower());
                    if (to.isNull()) { mapped.clear(); break; }
                    mapped += to;
                }
                // Cheap lookup first; the validity check only for a real hit.
                if (mapped.isEmpty() || !LocalLexicon::knownInLanguage(language, mapped)) continue;
                if (midWord ? m_lexicon.hasWord(wholeWord) : m_currentWordKnown) break;
                if (wholeWord.front().isUpper()) mapped[0] = mapped.at(0).toUpper();
                m_suggestions.removeAll(mapped);
                m_suggestions.prepend(mapped);
                while (m_suggestions.size() > 3) m_suggestions.removeLast();
                m_layoutSuggestionWord = mapped;
                m_layoutSuggestionLanguage = language;
                break;
            }
        }
        if (!m_composing) {
            for (const auto &entry : std::as_const(m_recentCorrections)) {
                if (entry.first.compare(wholeWord, Qt::CaseInsensitive) == 0) {
                    m_recorrectionOriginal = entry.second;
                    m_suggestions.removeAll(entry.second);
                    m_suggestions.prepend(entry.second);
                    while (m_suggestions.size() > 3) m_suggestions.removeLast();
                    break;
                }
            }
        }
        if (!m_committedHead.isEmpty()) {
            m_suggestions.erase(std::remove_if(m_suggestions.begin(), m_suggestions.end(), [this](const QString &s) {
                return !s.startsWith(m_committedHead, Qt::CaseInsensitive);
            }), m_suggestions.end());
        }
        // Gboard: suggestions follow the case of what was typed ("Hel" ->
        // "Hello", "HEL" -> "HELLO"); words with their own casing ("iPhone")
        // are left alone.
        const bool allCaps = m_currentWord.size() >= 2 && m_currentWord.toUpper() == m_currentWord
            && m_currentWord.toLower() != m_currentWord;
        const bool initialCap = !m_currentWord.isEmpty() && m_currentWord.front().isUpper();
        for (QString &item : m_suggestions) {
            if (item.isEmpty() || item.toLower() != item) continue;
            if (allCaps) item = item.toUpper();
            else if (initialCap || m_sentenceStart) item[0] = item.at(0).toUpper();
        }
        return;
    }
    if (glideAlternativesShown()) {
        m_suggestions = m_glideAlternatives.mid(0, 3);
        return;
    }
    if (!m_nextWordSuggestions) {
        m_suggestions.clear();
        return;
    }
    m_suggestions = m_lexicon.nextWords(contextWord(), 3);
}

void TypingEngine::resetComposition()
{
    dropGlideAlternatives();
    m_resumedWord.clear();
    m_wordAfterCursor.clear();
    m_charAfterWord = QChar();
    m_spaceFromSuggestion = false;
    m_pendingWord.clear();
    m_pendingOriginal.clear();
    m_composing = false;
    m_pendingSpace = false;
    m_currentWord.clear();
    m_previousWord.clear();
    m_suggestions.clear();
    m_glideTrace.clear();
    m_sentenceStart = true;
    m_lastActionWasSpace = false;
    m_sentencePunctuationPending = false;
    forgetTextState();
    m_lexicon.flushLearning();
    refreshSuggestions();
}

void TypingEngine::resetInputContext()
{
    dropGlideAlternatives();
    m_surroundingSupported = false;
    m_preeditRejected = false;
    m_autoCapitalizationAllowed = true;
    resetComposition();
}

void TypingEngine::rememberCompositionBeforeDeactivation()
{
    m_lostWord.clear();
    m_lostHead.clear();
    if (m_sensitiveContext || !m_composing || m_currentWord.isEmpty()) return;
    m_lostWord = m_currentWord;
    m_lostHead = m_committedHead;
    m_lostAtMs = m_clock ? m_clock() : monotonicMs();
    qCDebug(lcEngine) << "deactivated while composing" << m_currentWord.size() << "letters";
}

void TypingEngine::setSensitiveContext(bool sensitive)
{
    if (m_sensitiveContext == sensitive) return;
    m_sensitiveContext = sensitive;
    resetComposition();
}

bool TypingEngine::syncSurroundingText(const QString &text, int cursorByte, int anchorByte)
{
    if (cursorByte < 0 || anchorByte < 0) return false;
    m_surroundingSupported = true;
    if (m_sensitiveContext) return false;

    const QByteArray utf8 = text.toUtf8();
    const int bounded = qBound(0, cursorByte, static_cast<int>(utf8.size()));
    const QString before = tail(QString::fromUtf8(utf8.left(bounded)));

    if (composing()) {
        // The keyboard owns the composition. Echo contents are unreliable
        // (lagging caches, placeholder characters in empty editors), and
        // abandoning a live preedit loses text; external changes end a
        // composition through reset() (observed on device when the user
        // clicks elsewhere or switches windows).
        // For the trace only (no text): does the client's text already hold
        // the word being composed, as if it had committed the composition by
        // itself (a page script in Firefox)? KWin sends no reset then.
        const QString composed = m_composing ? stripHead(m_currentWord) : QString();
        const bool holdsWord = !composed.isEmpty() && sameTextState(before, tail(m_model + composed));
        qCDebug(lcEngine) << "echo bytes" << bounded << "ignored while composing"
                          << (holdsWord ? "(client text holds the composed word)" : "");
        return false;
    }


    if (cursorByte == anchorByte) {
        // Any state this keyboard produced recently (including states with
        // its preedit) is an echo. Clients such as Firefox answer from a
        // lagging cache, so stale echoes of *older* states are normal and
        // must never be mistaken for user edits.
        for (int i = m_history.size() - 1; i >= 0; --i) {
            if (sameTextState(m_history.at(i), before)) {
                m_inSync = !composing() && i == m_history.size() - 1 && sameTextState(m_model, before);
                qCDebug(lcEngine) << "echo bytes" << bounded << "known state" << i + 1 << "of" << m_history.size()
                                  << (m_inSync ? "in-sync" : "stale/composing");
                return false;
            }
        }
    }

    const qint64 now = m_clock ? (m_clock ? m_clock() : monotonicMs()) : monotonicMs();
    if (now - m_lastLocalOpMs < SettleMs) {
        qCDebug(lcEngine) << "echo bytes" << bounded << "unknown but self-caused (settle window) -> ignored";
        m_inSync = false;
        return false;
    }

    // A state the keyboard never produced, at human speed: cursor moved,
    // selection, or the application changed the text. The client is the
    // source of truth.
    qCDebug(lcEngine) << "echo bytes" << bounded << "unknown state -> adopt";
    dropGlideAlternatives();
    m_resumedWord.clear();
    m_composing = false;
    m_pendingSpace = false;
    m_spaceFromSuggestion = false;
    m_pendingWord.clear();
    m_pendingOriginal.clear();
    m_history = {before};
    m_model = before;
    m_inSync = true;

    QString trailing;
    QString incomingPrevious;
    bool incomingSentenceStart = false;
    if (cursorByte == anchorByte) {
        const WordsBeforeCursor words = wordsBeforeCursor(before);
        trailing = words.trailing;
        incomingPrevious = words.previous;
        incomingSentenceStart = before.trimmed().isEmpty();
        if (!incomingSentenceStart && trailing.isEmpty()) {
            const QChar last = before.trimmed().back();
            incomingSentenceStart = last == QLatin1Char('.') || last == QLatin1Char('!') || last == QLatin1Char('?');
        }
    }

    QString afterWord;
    QChar charAfter;
    if (cursorByte == anchorByte) {
        const QString after = QString::fromUtf8(utf8.mid(bounded)).left(64);
        qsizetype n = 0;
        while (n < after.size() && after.at(n).isLetter()) ++n;
        afterWord = after.left(n);
        charAfter = n < after.size() ? after.at(n) : QChar();
    }
    const bool afterChanged = afterWord != m_wordAfterCursor;
    m_wordAfterCursor = afterWord;
    m_charAfterWord = charAfter;
    if (!afterChanged && m_currentWord == trailing && m_previousWord == incomingPrevious
        && m_sentenceStart == incomingSentenceStart) {
        return false;
    }
    m_currentWord = trailing;
    m_previousWord = incomingPrevious;
    m_sentenceStart = incomingSentenceStart;
    m_lastActionWasSpace = false;
    refreshSuggestions();
    return true;
}

void TypingEngine::clearLearning()
{
    m_lexicon.clearLearning();
    refreshSuggestions();
}

void TypingEngine::saveLearning()
{
    m_lexicon.flushLearning();
    LocalLexicon::waitForLearningWrites();
}

void TypingEngine::beginGlide(const QString &key)
{
    dropGlideAlternatives();
    if (m_sensitiveContext) return;
    // The word being typed is committed only once the glide gives a word: a
    // tap that rolled into a short stroke goes on typing it.
    m_glideTrace.clear();
    glideThrough(key);
}

void TypingEngine::glideThrough(const QString &key)
{
    if (key.isEmpty() || m_sensitiveContext) return;
    const QString normalized = key.left(1).toLower();
    if (m_glideTrace.isEmpty() || m_glideTrace.back() != normalized) m_glideTrace.append(normalized);
}

// LatinIME InputLogic.onStartBatchInput: a gesture right after a word, a
// digit or punctuation that is usually followed by a space starts with a
// space ("phantom space"), and it never replaces the word the cursor
// touches: that word stays as it is.
void TypingEngine::prepareTextForGlidedWord()
{
    if (m_pendingSpace) return;                  // the held space goes out first
    if (!m_currentWord.isEmpty()) {
        // A word typed on without composition, or one the cursor was put
        // next to; it was learned when it was typed.
        if (m_wordAfterCursor.isEmpty()) m_previousWord = bareWord(m_currentWord);
        m_currentWord.clear();
        m_wordAfterCursor.clear();
        m_resumedWord.clear();
    }
    if (m_model.isEmpty()) return;
    const QChar last = m_model.back();
    // Character.isLetterOrDigit, LatinIME's symbols_followed_by_space
    // (".,;:!?)]}&") and the ellipsis.
    if (last.isLetterOrNumber() || QStringLiteral(".,;:!?)]}&\u2026").contains(last)) {
        commitLocal(QStringLiteral(" "));
    }
}

QString TypingEngine::endGlidePath(const QVector<QPointF> &path, const QHash<QChar, QPointF> &keyCentres, qreal keyWidth,
                                   GlideCase glideCase)
{
    if (m_sensitiveContext) { m_glideTrace.clear(); return {}; }
    // A word still being typed, or the one the cursor is at the end of, ends
    // where the glide begins.
    const QString context = !m_currentWord.isEmpty() && (m_composing || m_wordAfterCursor.isEmpty())
        ? bareWord(m_currentWord) : contextWord();
    const QStringList readings = m_lexicon.decodeGlidePathCandidates(path, keyCentres, keyWidth, context, 4);
    // No geometry (or no reading near the path): the key sequence decoder.
    if (readings.isEmpty()) return endGlide();
    m_glideTrace.clear();
    commitComposition();
    prepareTextForGlidedWord();
    const QString before = contextWord();
    auto inCase = [glideCase](const QString &word) {
        if (glideCase == GlideCase::AllCaps) return word.toUpper();
        QString cased = word;
        if (glideCase == GlideCase::Capitalized && !cased.isEmpty()) cased[0] = cased.at(0).toUpper();
        return cased;
    };
    const QString formatted = glideCase == GlideCase::Auto ? formatForSentence(readings.first()) : inCase(readings.first());
    chooseSuggestion(formatted);
    m_glidedWord = formatted;
    m_wordBeforeGlide = before;
    m_glideAlternatives.clear();
    for (const QString &reading : readings.mid(1)) {
        QString shown = inCase(reading);
        // The same case as the glided word ("To" -> "Too").
        if (formatted.front().isUpper() && shown.front().isLower()) shown[0] = shown.at(0).toUpper();
        if (shown != formatted) m_glideAlternatives.append(shown);
    }
    refreshSuggestions();
    return formatted;
}

bool TypingEngine::glidedWordIsLast() const
{
    return !m_glidedWord.isEmpty() && m_currentWord.isEmpty() && !m_composing
        && m_previousWord == m_glidedWord.toLower();
}

void TypingEngine::eraseGlidedWord()
{
    // Gboard: one Backspace right after a glide erases the whole word (with
    // the space added after it); nothing happened since, so the text before
    // the cursor is exactly what the glide committed.
    const QString glided = m_glidedWord;
    const bool held = m_pendingSpace;
    dropGlideAlternatives();
    if (m_learningEnabled) m_lexicon.unlearnWordWithContext(glided, m_wordBeforeGlide);
    const QString committed = m_spaceCommittedAfterSuggestion ? glided + QLatin1Char(' ') : glided;
    if (held) {
        m_pendingSpace = false;
        setPreeditLocal(QString());
    }
    if (!deleteOwnText(committed)) {
        for (int i = 0; i < committed.size(); ++i) backspaceLocal();
    }
    m_previousWord = m_wordBeforeGlide;
    m_pendingSpaceAfterWord = false;
    m_spaceFromSuggestion = false;
    m_lastActionWasSpace = false;
    rearmSentenceStartFromText();
    refreshSuggestions();
}

bool TypingEngine::glideAlternativesShown() const
{
    return !m_glideAlternatives.isEmpty() && m_currentWord.isEmpty() && !m_composing
        && m_previousWord == m_glidedWord.toLower();
}

void TypingEngine::replaceGlidedWord(const QString &word)
{
    const QString glided = m_glidedWord;
    m_glideAlternatives.clear();
    if (m_learningEnabled) m_lexicon.unlearnWordWithContext(glided, m_wordBeforeGlide);
    // Nothing happened since the glide (any input, a client reset or an
    // external edit drops the alternatives), so the text before the cursor
    // is exactly what was committed: delete it on the text channel, which
    // stays ordered with the commit (Backspace key events would not).
    const bool held = m_pendingSpace;
    const QString space = m_spaceCommittedAfterSuggestion ? QStringLiteral(" ") : QString();
    const QString committed = glided + space;
    if (held) {
        m_pendingSpace = false;
        setPreeditLocal(QString());
    }
    if (!deleteOwnText(committed)) {
        for (int i = 0; i < committed.size(); ++i) backspaceLocal();   // no text channel: keys only
    }
    commitLocal(word + space);
    if (held) {
        if (setPreeditLocal(QStringLiteral(" "))) {
            m_pendingSpace = true;
        } else {
            commitLocal(QStringLiteral(" "));
            m_spaceCommittedAfterSuggestion = true;
        }
    }
    m_pendingSpaceAfterWord = m_pendingSpace;
    m_spaceFromSuggestion = held || !space.isEmpty();
    m_previousWord = m_wordBeforeGlide;
    m_currentWord = word;
    finalizeCurrentWord();
    m_glidedWord = word;
    refreshSuggestions();
}

QString TypingEngine::endGlide()
{
    if (m_sensitiveContext) { m_glideTrace.clear(); return {}; }
    const QString context = !m_currentWord.isEmpty() && (m_composing || m_wordAfterCursor.isEmpty())
        ? bareWord(m_currentWord) : contextWord();
    const QString decoded = m_lexicon.decodeGlide(m_glideTrace, context);
    m_glideTrace.clear();
    if (decoded.isEmpty()) return {};
    commitComposition();
    prepareTextForGlidedWord();

    const QString formatted = formatForSentence(decoded);
    const QString before = contextWord();
    // Same flow as tapping a suggestion: commit the word, hold the automatic
    // space so a following Space confirms it instead of making ". ".
    chooseSuggestion(formatted);
    m_glidedWord = formatted;           // one Backspace erases it
    m_wordBeforeGlide = before;
    return formatted;
}

}
