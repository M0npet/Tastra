// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QStringList>

#include <future>
#include <memory>

namespace Tastra
{

struct DictionaryData;

// Local, offline word knowledge for one language at a time.
//
// * Validity ("is this a real word?") comes from libhunspell, which understands
//   affix rules, so inflected forms (wants, делаешь, gehst) are valid.
// * Completions come from a compact sorted index of the dictionary stems plus
//   the built-in core vocabulary and the user's locally learned words.
// * Only the active language is kept in memory. Loading happens on a worker
//   thread; until it finishes, core vocabulary and learned words still work.
class LocalLexicon
{
public:
    LocalLexicon();
    ~LocalLexicon();
    LocalLexicon(const LocalLexicon &) = delete;
    LocalLexicon &operator=(const LocalLexicon &) = delete;

    // Directories searched for <lang>.aff/<lang>.dic. Defaults to the
    // TASTRA_DICTIONARY_DIRS environment variable (colon separated) or the
    // standard system locations. Tests point this at fixtures.
    static void setDictionarySearchPaths(const QStringList &paths);
    static QStringList dictionarySearchPaths();
    // Directories (or qrc prefixes) searched for <lang>.txt word-frequency
    // lists, most frequent word first. Default: the bundled lists.
    static void setFrequencySearchPaths(const QStringList &paths);
    static QStringList frequencySearchPaths();
    // Bundled word pairs for next-word suggestions (default :/tastra/bigrams).
    static void setBigramSearchPaths(const QStringList &paths);
    static QStringList bigramSearchPaths();
    // Offensive-word lists (<lang>.txt), used only to filter suggestions.
    static void setBlocklistSearchPaths(const QStringList &paths);
    static QStringList blocklistSearchPaths();

    void setBlockOffensive(bool enabled);

    // Personal dictionary (Gboard/LatinIME logic). Explicit words are always
    // valid, keep their case and are suggested. Implicitly typed unknown words
    // only become valid after PromotionCount uses, so one accidental typo is
    // never legitimised. A plain-text file adds words for every language.
    static constexpr int PromotionCount = 3;
    // Learned words and word pairs kept per language, as in LatinIME's user
    // history: at 1.2 times this many, the rarest are dropped down to it, so
    // saving them stays quick.
    static constexpr qsizetype MaxLearnedWords = 10000;
    static constexpr qsizetype MaxLearnedPairs = 30000;
    static void setUserDictionaryFile(const QString &path);
    // Whether `word` is in another language's bundled frequency list; the list
    // is loaded on first use only (wrong-layout detection).
    // Never blocks: the first call starts a background load and returns false.
    static bool knownInLanguage(const QString &language, const QString &word);
    // Gboard's multilingual typing: the other enabled languages written in
    // the same script (en/de, uk/ru). A word one of them knows is never
    // "corrected" into this language. Their word lists load in the
    // background, as for wrong-layout detection.
    void setCompanionLanguages(const QStringList &codes);
    QStringList companionLanguages() const { return m_companions; }
    bool knownInCompanionLanguage(const QString &word) const;
    // A word of the frequency list that the Hunspell dictionary lacks.
    bool listedButNotInDictionary(const QString &word) const;
    static void preloadForeignWordsForTesting(const QString &language);   // blocking
    // Known without Hunspell (frequency list, stems, user and core words).
    bool hasWordCheaply(const QString &word) const;
    static void clearForeignWordCacheForTesting();
    static QString userDictionaryFile();
    bool addUserWord(const QString &word);
    void removeUserWord(const QString &word);
    QStringList userWords() const;
    void promoteWord(const QString &word);       // e.g. an undone autocorrection
    void reloadUserDictionaryFile();
    // Rows of the on-screen layout. Like LatinIME's ProximityInfo, a typo on a
    // neighbouring key is a cheaper (more likely) error than a distant one.
    void setKeyboardRows(const QStringList &rows);
    // Where each letter of the current word was touched, relative to the
    // key centre in key sizes (x right, y down; (0,0) = centre/unknown).
    void setTouchOffsets(const QVector<QPointF> &offsets);
    // Gboard-style "remove suggestion": unlearn and never suggest it again.
    void forgetWord(const QString &word);
    // Long-press on a phrase chip: `next` is no longer learned after `word`
    // (both stay words of their own).
    void forgetPair(const QString &word, const QString &next);

    void setLanguage(const QString &code);
    QString language() const;
    bool waitForDictionary(int timeoutMs = -1);
    bool hasSystemDictionary() const;

    QStringList suggestions(const QString &word, const QString &previousWord, int limit = 3) const;
    QString bestCorrection(const QString &word, const QString &previousWord = {}) const;
    // Cheap per-keystroke version: candidates are checked against the
    // frequency list, stems, learned and core words only (no Hunspell), so a
    // long unknown word cannot stall typing. Space uses bestCorrection().
    QString correctionPreview(const QString &word, const QString &previousWord = {}) const;
    QStringList nextWords(const QString &previousWord, int limit = 3) const;
    // Gboard Smart Compose, as a chip in the strip: the words the user
    // habitually types after `word` (typed at least three times, and at
    // least 60 % of what followed that word), at most `maxWords`. Only the
    // user's own pairs: on held-out text the bundled ones guessed the next
    // word right in 30 % of offers (60-90 % only for a few pairs).
    QStringList phraseAfter(const QString &word, int maxWords = 3) const;
    QString decodeGlide(const QStringList &trace, const QString &previousWord = {}) const;
    // Glide typing from the finger's path (root coordinates) and the key
    // centres of the current layout: SHARK2 / LatinIME-style comparison of the
    // path with each candidate word's ideal path, plus the word's frequency.
    QString decodeGlidePath(const QVector<QPointF> &path, const QHash<QChar, QPointF> &keyCentres,
                            qreal keyWidth, const QString &previousWord = {}) const;
    // The best readings of the path, best first (Gboard shows the others in
    // the strip after a glide).
    QStringList decodeGlidePathCandidates(const QVector<QPointF> &path, const QHash<QChar, QPointF> &keyCentres,
                                          qreal keyWidth, const QString &previousWord = {}, int limit = 4) const;

    void learnWordWithContext(const QString &word, const QString &previousWord);
    // Takes back one learnWordWithContext() (a glided word the user replaced).
    void unlearnWordWithContext(const QString &word, const QString &previousWord);
    // Saves learned words in the background (one writer; a save still
    // waiting is replaced by the newer one).
    void flushLearning();
    // Blocks until every save started so far is on disk.
    static void waitForLearningWrites();
    void clearLearning();

    bool hasWord(const QString &word) const;
    int dictionarySize() const;

private:
    // LatinIME's error model: an omitted apostrophe ("dont") is an
    // intentional omission and nearly free, a base letter for its accented
    // form ("uber", "ueber", "strasse") almost free.
    enum class Edit { Other, RepeatedLetter, Transposition, NeighbourKey, Apostrophe, Accent, NeighbourKeys, Split };
    struct Candidate {
        QString word;
        int score = 0;
        Edit edit = Edit::Other;
        int editIndex = -1;
    };

    void startLoading();
    void adoptLoadedData() const;
    void loadLearning();
    QString normalize(const QString &word) const;
    QString alphabet() const;
    bool isCore(const QString &word) const;
    bool isValidWord(const QString &word) const;
    QList<Candidate> correctionCandidates(const QString &typed, const QString &previousWord, bool full = true) const;
    QString pickCorrection(const QString &word, const QString &previousWord, bool full) const;
    bool knownCheaply(const QString &word) const;
    // The spelling the dictionary uses (capital for "I'm", German nouns).
    QString dictionaryForm(const QString &word) const;
    // English words that only exist capitalized ("i" -> "I", "monday").
    QString englishCapitalForm(const QString &typed) const;
    QStringList apostropheVariants(const QString &typed) const;
    QStringList accentVariants(const QString &typed) const;
    bool isAccentVariant(const QString &typed, const QString &candidate) const;
    int priorScore(const QString &candidate, const QString &previousWord) const;
    int frequencyRank(const QString &word) const;
    bool suggestible(const QString &word) const;
    bool isUserWord(const QString &word) const;
    void persistUserWords();
    bool neighbours(QChar a, QChar b) const;
    // Words of the frequency list that differ from the typed one only by
    // two or more neighbouring keys (LatinIME's proximity search).
    void addNeighbourKeyCandidates(const QString &typed, const QString &previousWord,
                                   const QSet<QString> &seen, QList<Candidate> &result) const;
    // Two words run together ("ofthe") or with a letter next to the space
    // bar for the space ("thisnis"): LatinIME's space omission and
    // mistyped space.
    void addSplitCandidates(const QString &typed, const QString &previousWord, QList<Candidate> &result) const;
    // The rank that decides how sure a correction is; for two words, the
    // rarer one's.
    int correctionRank(const QString &word) const;
    void loadBlocklist();
    QString bigramKey(const QString &previousWord, const QString &word) const;

    QString m_language = QStringLiteral("en");
    QStringList m_companions;
    bool m_loadStarted = false;
    mutable std::future<std::shared_ptr<DictionaryData>> m_pending;
    mutable std::shared_ptr<DictionaryData> m_data;
    QStringList m_coreWords;
    QSet<QString> m_coreWordSet;          // isCore() runs per correction variant
    QHash<QString, QString> m_typoMap;
    QHash<QString, int> m_personalFrequency;
    QHash<QString, int> m_bigramFrequency;
    // The learned next words of each previous word (from m_bigramFrequency;
    // rebuilt when pairs were removed), so nextWords() looks up instead of
    // scanning up to 36 000 pairs after every word.
    mutable QHash<QString, QStringList> m_learnedFollowers;
    mutable bool m_learnedFollowersValid = false;
    const QStringList &learnedFollowers(const QString &previousWord) const;
    int m_unsavedLearning = 0;
    bool m_blockOffensive = true;
    QSet<QString> m_offensive;
    QSet<QString> m_forgotten;
    QHash<QChar, QPointF> m_keyCentres;
    QVector<QPointF> m_touchOffsets;
    int neighbourBonus(QChar typed, QChar intended, int index) const;
    QHash<QString, QString> m_userWords;   // lowercase -> as entered
    QHash<QString, QString> m_fileWords;   // from dictionary.txt (all languages)
};

}
