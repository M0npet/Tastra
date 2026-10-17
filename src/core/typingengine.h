// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <limits>

#include "locallexicon.h"

#include <QPointF>
#include <QString>
#include <QStringList>

namespace Tastra
{

class KeyboardController;

class TypingEngine
{
public:
    explicit TypingEngine(KeyboardController &controller);

    void setLanguage(const QString &code);
    QString language() const;

    void setSuggestionsEnabled(bool enabled);
    // Gboard "Next-word suggestions": predictions after a word (on by default).
    void setNextWordSuggestionsEnabled(bool enabled);
    bool nextWordSuggestionsEnabled() const { return m_nextWordSuggestions; }
    // Gboard Smart Compose, as a chip in the strip: the words the user
    // habitually types next (on by default).
    void setPhraseSuggestionsEnabled(bool enabled);
    bool phraseSuggestionsEnabled() const { return m_phraseSuggestions; }
    void setAutocorrectEnabled(bool enabled);
    void setLearningEnabled(bool enabled);
    void setAutoCapitalizationEnabled(bool enabled);
    void setDoubleSpacePeriodEnabled(bool enabled);
    void setAutoCapitalizationAllowed(bool allowed);
    void setCompositionEnabled(bool enabled);
    void setClockForTesting(std::function<qint64()> clock);
    bool waitForDictionaryForTesting(int timeoutMs) { return m_lexicon.waitForDictionary(timeoutMs); }
    void setBlockOffensive(bool enabled);
    void setKeyboardRows(const QStringList &rows);
    // Gboard "Auto-space after punctuation" (off by default).
    void setAutoSpaceAfterPunctuation(bool enabled);
    void forgetWord(const QString &word);
    bool addUserWord(const QString &word);
    void removeUserWord(const QString &word);
    QStringList userWords() const;
    bool isKnownWord(const QString &word) const;
    void reloadUserDictionaryFile();
    bool compositionEnabled() const;
    bool composing() const;

    bool suggestionsEnabled() const;
    bool autocorrectEnabled() const;
    bool learningEnabled() const;
    bool autoCapitalizationEnabled() const;
    bool doubleSpacePeriodEnabled() const;

    bool wantsAutoUppercase() const;
    bool sensitiveContext() const;
    QString currentWord() const;
    QString previousWord() const;
    QStringList suggestions() const;
    // The current word (or, after a Space, nothing) completed with the words
    // the user habitually types next: "see you soon" while "se" is typed,
    // "you soon" after "see ". Empty when there is no habit. Chosen with
    // chooseSuggestion().
    QString phraseSuggestion() const { return m_phraseSuggestion; }
    // Long-press on the phrase chip: the pairs in it are forgotten.
    void forgetPhrase(const QString &phrase);

    // touch: where the key was hit, relative to its centre in key sizes.
    void typeLetter(const QString &text, QPointF touch = {});
    void typeText(const QString &text);
    void backspace();
    void backspaceRepeated(int count);
    void space();
    void enter();
    void chooseSuggestion(const QString &word);
    void resetComposition();
    void resetInputContext();
    // Called when the text input is switched off. If a word was still being
    // composed, KWin did not commit it (it does on a touch or focus change,
    // and resets us first), so the client may throw it away: GTK 3 does when
    // Firefox switches its text input off and on again. The first surrounding
    // text after the next activation (within a short time) puts it back as a
    // preedit when the client did not keep it.
    void rememberCompositionBeforeDeactivation();
    // Voice commands (Gboard): what they delete is measured on the text this
    // keyboard knows before the cursor.
    void deleteLastWord();
    void deleteLastSentence();
    void deleteKnownText();
    void insertLineBreak(const QString &breakText);
    // Commits whatever is held in the preedit as-is (before cursor moves,
    // paste, language switch). resetComposition() instead drops it.
    void commitComposition();
    // Inserts recognized speech: commits any composition first, separates it
    // from preceding text, applies sentence case and updates the context.
    void insertDictation(const QString &text);
    QString autocorrectTarget() const;
    // Wrong-layout detection: per other language, current key -> its key.
    void setForeignLayouts(const QList<QPair<QString, QHash<QChar, QChar>>> &maps);
    void setCompanionLanguages(const QStringList &codes) { m_lexicon.setCompanionLanguages(codes); }
    QStringList companionLanguages() const { return m_lexicon.companionLanguages(); }
    QString layoutSuggestionWord() const;
    QString layoutSuggestionLanguage() const;
    // Whether the current word is a known word (computed once per keystroke).
    bool currentWordKnown() const;
    bool surroundingTextSupported() const;
    void setSensitiveContext(bool sensitive);
    bool syncSurroundingText(const QString &text, int cursorByte, int anchorByte);
    void clearLearning();
    // Writes what was learned since the last save and waits until it is on
    // disk (before the keyboard is ended).
    void saveLearning();

    void beginGlide(const QString &key);
    void glideThrough(const QString &key);
    QString endGlide();
    // Glide from the finger's path and the layout's key centres (Gboard /
    // LatinIME gesture typing). Afterwards the strip offers the other
    // readings of the path; choosing one replaces the glided word.
    // Shift before a glide capitalizes the word, Caps Lock writes it in
    // capitals; otherwise sentence case applies.
    enum class GlideCase { Auto, Capitalized, AllCaps };
    QString endGlidePath(const QVector<QPointF> &path, const QHash<QChar, QPointF> &keyCentres, qreal keyWidth,
                         GlideCase glideCase = GlideCase::Auto);
    QStringList glideTrace() const;

private:
    QString formatForSentence(const QString &word) const;
    // The word before the cursor for predictions and corrections: the
    // previous word, or a corrected word still held with its space.
    QString contextWord() const;
    bool lostWordResumes() const;
    void afterVoiceDeletion();
    // A space or punctuation right after the word at the cursor (or right
    // after the cursor), as the client last reported.
    bool textFollowsWord() const;
    bool textFollowsCursor() const;
    // LatinIME restartSuggestionsOnWordTouchedByCursor: after Backspace
    // reaches the end of a word, that word is the word being typed again
    // (suggestions for it; the next letter continues it).
    void resumeWordBeforeCursor();
    // A letter typed onto a committed word the client has confirmed: the word
    // is taken back and composed again, so that a suggestion or correction
    // replaces it in one commit.
    bool recomposeCurrentWord();
    // The other readings of the last glide, while nothing has happened since.
    bool glideAlternativesShown() const;
    void dropGlideAlternatives() { m_glideAlternatives.clear(); m_glidedWord.clear(); }
    // The last thing that happened was a glide (nothing typed since).
    bool glidedWordIsLast() const;
    void eraseGlidedWord();
    void prepareTextForGlidedWord();
    void replaceGlidedWord(const QString &word);
    void refreshSuggestions();
    void refreshWordSuggestions();
    void refreshPhraseSuggestion();
    bool replaceBeforeCursor(const QString &existing, const QString &replacement, bool allowUnconfirmed);
    void finalizeCurrentWord(bool updatePrevious = true);
    void observePunctuation(const QString &text);

    // Local model of the text before the cursor, used to tell stale client
    // echoes (states this keyboard produced earlier) from external edits.
    void commitLocal(const QString &text);
    void backspaceLocal();
    bool deleteLocal(const QString &text);
    bool deleteOwnText(const QString &text);
    void recordLocalState();
    void recordState(const QString &state);
    void forgetTextState();
    bool inSyncWithClient() const;
    void rearmSentenceStartFromText();
    bool doubleSpaceAllowedAfter(const QString &textBeforeSpace) const;
    bool atEmptyParagraph() const;
    void rememberCorrection(const QString &typed, const QString &corrected);
    bool replaceWordAroundCursor(const QString &replacement);
    QString stripHead(const QString &text) const;
    bool compositionAvailable() const;
    bool setPreeditLocal(const QString &text);
    QString corrected(const QString &word) const;
    QString takePendingText();

    KeyboardController &m_controller;
    LocalLexicon m_lexicon;
    QString m_currentWord;
    QString m_previousWord;
    QString m_wordBeforePeriod;          // restored when a double-space period is undone
    bool m_spaceCommittedAfterSuggestion = false;   // the last suggestion's space went out as text
    QString m_resumedWord;               // taken up again after Backspace (resumeWordBeforeCursor)
    QString m_previousBeforeResume;      // the context before that
    QString m_lostWord;                  // composed when the text input was switched off
    QString m_lostHead;                  // its first letter, if that was committed
    qint64 m_lostAtMs = 0;
    QStringList m_suggestions;
    QStringList m_glideTrace;
    QStringList m_glideAlternatives;   // Gboard: other readings of the last glide
    QString m_glidedWord;              // as committed
    QString m_wordBeforeGlide;
    bool m_suggestionsEnabled = true;
    bool m_nextWordSuggestions = true;
    bool m_phraseSuggestions = true;
    QString m_phraseSuggestion;
    bool m_autocorrectEnabled = true;
    bool m_learningEnabled = true;
    bool m_autoCapitalizationEnabled = true;
    bool m_doubleSpacePeriodEnabled = true;
    bool m_sentenceStart = true;
    bool m_lastActionWasSpace = false;
    bool m_sentencePunctuationPending = false;
    bool m_sensitiveContext = false;
    bool m_autoCapitalizationAllowed = true;
    bool m_surroundingSupported = false;
    bool m_compositionEnabled = true;
    bool m_composing = false;       // m_currentWord lives in the client's preedit
    bool m_pendingSpace = false;    // a space lives in the client's preedit
    bool m_spaceFromSuggestion = false; // that space was added by a suggestion
    bool m_pendingSpaceAfterWord = false; // the held space directly follows a word
    bool m_pendingPeriod = false;      // the held text is ". " from a double space
    QString m_pendingPunct;            // swapped punctuation held before the space
    // Gboard re-correction: recent autocorrections (corrected -> typed), so
    // returning the cursor to such a word offers what was typed.
    QList<QPair<QString, QString>> m_recentCorrections;
    QString m_recorrectionOriginal;
    // Gboard: the cursor inside a word -> options for the whole word.
    QString m_wordAfterCursor;     // letters right after the cursor
    QChar m_charAfterWord;         // what follows that word ('\0' = end)
    bool m_lastWasDoublePeriod = false; // immediate mode: ". " just inserted
    qint64 m_lastSpaceMs = -1;          // time of the latest Space press
    qint64 m_previousSpaceMs = -1;      // LatinIME double-space countdown start
    QString m_pendingWord;          // autocorrected word held with the pending space (revertible)
    QString m_pendingOriginal;      // what the user actually typed
    QString m_noCorrectionFor;      // word the user just reverted
    QString m_autocorrectTarget;
    QVector<QPointF> m_touchOffsets;      // one per letter of m_currentWord
    // Letters of the current word already committed: a word started in an
    // empty paragraph commits its first letter before composing the rest,
    // because rich web editors (ProseMirror: claude.ai, ChatGPT, …) re-render
    // the empty paragraph's placeholder on the first input and break an IME
    // composition started there (first letter lost / replaced).
    QString m_committedHead;
    QList<QPair<QString, QHash<QChar, QChar>>> m_foreignLayouts;
    QString m_layoutSuggestionWord;
    QString m_layoutSuggestionLanguage;
    bool m_currentWordKnown = true;
    bool m_autoSpaceAfterPunctuation = false;
    bool m_autoSpacePending = false;
    bool m_preeditRejected = false; // backend/client cannot show preedit
    bool m_inSync = false;
    std::function<qint64()> m_clock;
    qint64 m_lastLocalOpMs = std::numeric_limits<qint64>::min() / 2;
    QString m_model;                // committed text before the cursor (tail)
    QStringList m_history;          // recent states produced or confirmed
};

}
