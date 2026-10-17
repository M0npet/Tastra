// SPDX-License-Identifier: GPL-3.0-or-later

#include "locallexicon.h"
#include "languages.h"
#include "glidegeometry.h"
#include <QThreadPool>
#include <QMutex>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLineF>
#include <QLoggingCategory>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QStringEncoder>
#include <QVarLengthArray>
#include <QVector>

#include <hunspell/hunspell.hxx>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include <algorithm>
#include <functional>
#include <cmath>
#include <chrono>
#include <limits>
#include <thread>
#include <vector>

Q_LOGGING_CATEGORY(lcLexicon, "tastra.lexicon", QtWarningMsg)

namespace Tastra
{

// FNV-1a over the UTF-16 code units: stable, seedless, good enough here.
static quint64 wordHash(QStringView word)
{
    quint64 h = 14695981039346656037ULL;
    for (const QChar ch : word) {
        h ^= ch.unicode();
        h *= 1099511628211ULL;
    }
    return h;
}

// Immutable once built; created on a worker thread, then only used on the
// thread that owns the LocalLexicon.
struct DictionaryData
{
    QString language;
    std::unique_ptr<Hunspell> speller;
    QByteArray encoding;
    // Compact completion index: all lowercase words concatenated, plus one
    // sorted offset per word. Bit 31 marks words the dictionary capitalizes.
    QString blob;
    std::vector<quint32> entries;

    // Word frequency (sorted by word for prefix search); rank 1 = most common.
    struct FreqEntry { QString word; quint32 rank; bool capital; };
    std::vector<FreqEntry> freq;
    // Words of the frequency list that Hunspell does not know: chat words
    // ("окей", "naja", "ok"), names, British spellings, subtitle noise.
    // Never offered, but not "corrected" away either (sorted hashes).
    std::vector<quint64> listedOnly;
    bool isListedOnly(QStringView word) const { return std::binary_search(listedOnly.begin(), listedOnly.end(), wordHash(word)); }

    // Bundled word pairs (data/bigrams, tools/build-bigrams.py): how often a
    // word followed another in the corpora. Sorted pair hashes with their
    // counts (10 bytes a pair) for scoring, and each previous word's most
    // common followers for the strip (indices into freq, so only words of
    // the frequency list Hunspell accepts; capital: "das Unternehmen").
    std::vector<quint64> pairHashes;
    std::vector<quint16> pairCounts;
    struct Follower { quint64 previous; quint32 freqIndex; quint16 count; bool capital; };
    std::vector<Follower> followers;
    static constexpr int FollowersKept = 6;

    static quint64 pairHash(QStringView previous, QStringView word)
    {
        quint64 h = wordHash(previous);
        for (const QChar ch : QStringView(u"\x1f")) { h ^= ch.unicode(); h *= 1099511628211ULL; }
        for (const QChar ch : word) { h ^= ch.unicode(); h *= 1099511628211ULL; }
        return h;
    }
    quint32 pairCount(QStringView previous, QStringView word) const
    {
        const quint64 h = pairHash(previous, word);
        const auto it = std::lower_bound(pairHashes.begin(), pairHashes.end(), h);
        return it != pairHashes.end() && *it == h ? pairCounts[size_t(it - pairHashes.begin())] : 0;
    }
    std::pair<size_t, size_t> followerRange(QStringView previous) const
    {
        const quint64 h = wordHash(previous);
        const auto lo = std::lower_bound(followers.begin(), followers.end(), h, [](const Follower &f, quint64 v) { return f.previous < v; });
        auto hi = lo;
        while (hi != followers.end() && hi->previous == h) ++hi;
        return {size_t(lo - followers.begin()), size_t(hi - followers.begin())};
    }

    std::pair<size_t, size_t> freqRange(QStringView prefix) const
    {
        auto less = [](const FreqEntry &e, QStringView p) { return QStringView(e.word).compare(p) < 0; };
        const auto lo = std::lower_bound(freq.begin(), freq.end(), prefix, less);
        const QString upper = prefix.toString() + QChar(0xFFFF);
        const auto hi = std::lower_bound(lo, freq.end(), QStringView(upper), less);
        return {size_t(lo - freq.begin()), size_t(hi - freq.begin())};
    }

    const FreqEntry *freqFind(QStringView word) const
    {
        const auto [lo, hi] = freqRange(word);
        return lo < hi && freq[lo].word == word ? &freq[lo] : nullptr;
    }

    static constexpr quint32 CapitalFlag = 0x80000000u;

    QStringView wordAt(size_t i) const
    {
        const quint32 start = entries[i] & ~CapitalFlag;
        qsizetype end = blob.indexOf(QLatin1Char('\n'), start);
        return QStringView(blob).mid(start, end - start);
    }
    bool capitalizedAt(size_t i) const { return entries[i] & CapitalFlag; }

    std::pair<size_t, size_t> prefixRange(QStringView prefix) const
    {
        auto less = [this](quint32 entry, QStringView p) {
            const quint32 start = entry & ~CapitalFlag;
            const qsizetype end = blob.indexOf(QLatin1Char('\n'), start);
            return QStringView(blob).mid(start, end - start).compare(p) < 0;
        };
        const auto lo = std::lower_bound(entries.begin(), entries.end(), prefix, less);
        const QString upper = prefix.toString() + QChar(0xFFFF);
        const auto hi = std::lower_bound(lo, entries.end(), QStringView(upper), less);
        return {size_t(lo - entries.begin()), size_t(hi - entries.begin())};
    }

    bool contains(QStringView word) const
    {
        const auto [lo, hi] = prefixRange(word);
        return lo < hi && wordAt(lo) == word;
    }

    QByteArray encode(const QString &word) const
    {
        if (encoding == "UTF-8") return word.toUtf8();
        if (encoding == "ISO8859-1" || encoding == "ISO-8859-1") return word.toLatin1();
        QStringEncoder encoder(encoding.constData());
        if (!encoder.isValid()) return {};
        return encoder.encode(word);
    }

    bool spell(const QString &word) const
    {
        if (!speller || word.isEmpty()) return false;
        const QByteArray bytes = encode(word);
        return !bytes.isEmpty() && speller->spell(std::string(bytes.constData(), bytes.size()));
    }
};

namespace
{

QStringList coreWords(const QString &code)
{
    // The languages of 0.7.19 have no list of their own: their frequency
    // list and dictionary are the words (and never English ones).
    if (languageInfo(code)) return {};
    if (code == QStringLiteral("de")) {
        return {
            QStringLiteral("aber"), QStringLiteral("alle"), QStringLiteral("auch"), QStringLiteral("auf"),
            QStringLiteral("aus"), QStringLiteral("bei"), QStringLiteral("bin"), QStringLiteral("bitte"),
            QStringLiteral("danke"), QStringLiteral("das"), QStringLiteral("dein"), QStringLiteral("der"),
            QStringLiteral("die"), QStringLiteral("du"), QStringLiteral("ein"), QStringLiteral("eine"),
            QStringLiteral("für"), QStringLiteral("gut"), QStringLiteral("hallo"), QStringLiteral("haben"),
            QStringLiteral("heute"), QStringLiteral("hier"), QStringLiteral("ich"), QStringLiteral("ist"),
            QStringLiteral("ja"), QStringLiteral("kann"), QStringLiteral("machen"), QStringLiteral("mein"),
            QStringLiteral("mit"), QStringLiteral("morgen"), QStringLiteral("nicht"), QStringLiteral("noch"),
            QStringLiteral("oder"), QStringLiteral("schon"), QStringLiteral("sehr"), QStringLiteral("sein"),
            QStringLiteral("sie"), QStringLiteral("so"), QStringLiteral("und"), QStringLiteral("uns"),
            QStringLiteral("was"), QStringLiteral("wenn"), QStringLiteral("wie"), QStringLiteral("wir"),
            QStringLiteral("wo"), QStringLiteral("wollen"), QStringLiteral("zeit"), QStringLiteral("zu")
        };
    }
    if (code == QStringLiteral("uk")) {
        return {
            QStringLiteral("але"), QStringLiteral("без"), QStringLiteral("буде"), QStringLiteral("бути"),
            QStringLiteral("вам"), QStringLiteral("вас"), QStringLiteral("вже"), QStringLiteral("він"),
            QStringLiteral("вона"), QStringLiteral("вони"), QStringLiteral("все"), QStringLiteral("гарно"),
            QStringLiteral("де"), QStringLiteral("для"), QStringLiteral("добре"), QStringLiteral("дякую"),
            QStringLiteral("є"), QStringLiteral("зараз"), QStringLiteral("і"), QStringLiteral("його"),
            QStringLiteral("коли"), QStringLiteral("може"), QStringLiteral("можна"), QStringLiteral("ми"),
            QStringLiteral("мене"), QStringLiteral("на"), QStringLiteral("нам"), QStringLiteral("не"),
            QStringLiteral("ні"), QStringLiteral("потрібно"), QStringLiteral("привіт"), QStringLiteral("про"),
            QStringLiteral("так"), QStringLiteral("тебе"), QStringLiteral("тепер"), QStringLiteral("ти"),
            QStringLiteral("тут"), QStringLiteral("у"), QStringLiteral("хочу"), QStringLiteral("це"),
            QStringLiteral("цей"), QStringLiteral("час"), QStringLiteral("що"), QStringLiteral("як"),
            QStringLiteral("я"), QStringLiteral("який"), QStringLiteral("якщо")
        };
    }
    if (code == QStringLiteral("ru")) {
        return {
            QStringLiteral("а"), QStringLiteral("без"), QStringLiteral("будет"), QStringLiteral("быть"),
            QStringLiteral("вам"), QStringLiteral("вас"), QStringLiteral("всё"), QStringLiteral("вы"),
            QStringLiteral("где"), QStringLiteral("да"), QStringLiteral("для"), QStringLiteral("добро"),
            QStringLiteral("есть"), QStringLiteral("ещё"), QStringLiteral("здесь"), QStringLiteral("и"),
            QStringLiteral("как"), QStringLiteral("когда"), QStringLiteral("может"), QStringLiteral("можно"),
            QStringLiteral("мы"), QStringLiteral("мне"), QStringLiteral("на"), QStringLiteral("нам"),
            QStringLiteral("не"), QStringLiteral("нет"), QStringLiteral("но"), QStringLiteral("он"),
            QStringLiteral("она"), QStringLiteral("они"), QStringLiteral("очень"), QStringLiteral("по"),
            QStringLiteral("пока"), QStringLiteral("потом"), QStringLiteral("привет"), QStringLiteral("про"),
            QStringLiteral("сейчас"), QStringLiteral("спасибо"), QStringLiteral("так"), QStringLiteral("тебя"),
            QStringLiteral("ты"), QStringLiteral("у"), QStringLiteral("хочу"), QStringLiteral("что"),
            QStringLiteral("это"), QStringLiteral("я"), QStringLiteral("если")
        };
    }

    return {
        QStringLiteral("a"), QStringLiteral("about"), QStringLiteral("after"), QStringLiteral("all"),
        QStringLiteral("also"), QStringLiteral("and"), QStringLiteral("are"), QStringLiteral("as"),
        QStringLiteral("at"), QStringLiteral("be"), QStringLiteral("because"), QStringLiteral("but"),
        QStringLiteral("can"), QStringLiteral("do"), QStringLiteral("for"), QStringLiteral("from"),
        QStringLiteral("good"), QStringLiteral("have"), QStringLiteral("hello"), QStringLiteral("here"),
        QStringLiteral("how"), QStringLiteral("i"), QStringLiteral("if"), QStringLiteral("in"),
        QStringLiteral("is"), QStringLiteral("it"), QStringLiteral("just"), QStringLiteral("know"),
        QStringLiteral("like"), QStringLiteral("me"), QStringLiteral("more"), QStringLiteral("my"),
        QStringLiteral("need"), QStringLiteral("no"), QStringLiteral("not"), QStringLiteral("now"),
        QStringLiteral("of"), QStringLiteral("on"), QStringLiteral("one"), QStringLiteral("or"),
        QStringLiteral("please"), QStringLiteral("so"), QStringLiteral("thank"), QStringLiteral("that"),
        QStringLiteral("the"), QStringLiteral("there"), QStringLiteral("this"), QStringLiteral("time"),
        QStringLiteral("to"), QStringLiteral("today"), QStringLiteral("want"), QStringLiteral("was"),
        QStringLiteral("we"), QStringLiteral("what"), QStringLiteral("when"), QStringLiteral("where"),
        QStringLiteral("will"), QStringLiteral("with"), QStringLiteral("yes"), QStringLiteral("you"),
        QStringLiteral("your")
    };
}

// Only strings that are never valid words go here: without a dictionary they
// are the sole autocorrections, so they must be unambiguous.
QHash<QString, QString> curatedTypos(const QString &code)
{
    QHash<QString, QString> map;
    auto add = [&map](const char *typo, const char *word) {
        map.insert(QString::fromUtf8(typo), QString::fromUtf8(word));
    };
    if (languageInfo(code)) return map;
    if (code == QStringLiteral("de")) {
        add("dsa", "das"); add("udn", "und"); add("nciht", "nicht"); add("ncith", "nicht");
        add("hlalo", "hallo"); add("danek", "danke"); add("iwe", "wie");
    } else if (code == QStringLiteral("uk")) {
        add("првиіт", "привіт"); add("дяукю", "дякую");
        add("щоо", "що"); add("якщко", "якщо");
    } else if (code == QStringLiteral("ru")) {
        add("првиет", "привет"); add("пирвет", "привет"); add("спаисбо", "спасибо");
        add("сапсибо", "спасибо"); add("сейчса", "сейчас"); add("очнеь", "очень");
    } else {
        add("teh", "the"); add("hte", "the"); add("adn", "and"); add("taht", "that");
        add("waht", "what"); add("thier", "their"); add("recieve", "receive");
        add("becuase", "because"); add("wiht", "with"); add("thsi", "this");
    }
    return map;
}

QStringList dictionaryNames(const QString &code)
{
    if (const QStringList names = languageDictionaries(code); !names.isEmpty()) return names;
    if (code == QStringLiteral("de")) {
        return {QStringLiteral("de_DE"), QStringLiteral("de_DE_frami"), QStringLiteral("de_AT"), QStringLiteral("de_CH")};
    }
    if (code == QStringLiteral("uk")) return {QStringLiteral("uk_UA")};
    if (code == QStringLiteral("ru")) return {QStringLiteral("ru_RU")};
    return {QStringLiteral("en_US"), QStringLiteral("en_GB")};
}

QString capitalizeFirst(const QString &word)
{
    if (word.isEmpty()) return word;
    QString result = word;
    result[0] = result.at(0).toUpper();
    return result;
}

QStringList &searchPathOverride()
{
    static QStringList paths;
    return paths;
}

// Learned words are saved by one background thread: a save of 10 000 words
// and 30 000 pairs takes over 100 ms, too long for a keystroke. Saves that
// queue up while one is being written collapse into one of the newest lists.
QThreadPool &learningWriter()
{
    static QThreadPool *pool = [] {
        auto *p = new QThreadPool;
        p->setMaxThreadCount(1);
        return p;
    }();
    return *pool;
}

struct LearningSnapshot
{
    QHash<QString, int> words;
    QHash<QString, int> bigrams;
    QStringList forgotten;
};

struct LearningSaves
{
    QMutex mutex;
    QHash<QString, LearningSnapshot> pending;     // by language
    bool scheduled = false;
};

LearningSaves &learningSaves()
{
    static auto *saves = new LearningSaves;
    return *saves;
}

void writeLearning(const QString &language, const LearningSnapshot &snapshot)
{
    QVariantMap words;
    for (auto it = snapshot.words.constBegin(); it != snapshot.words.constEnd(); ++it) words.insert(it.key(), it.value());
    QVariantMap bigrams;
    for (auto it = snapshot.bigrams.constBegin(); it != snapshot.bigrams.constEnd(); ++it) bigrams.insert(it.key(), it.value());
    QSettings settings;
    settings.setValue(QStringLiteral("learning/%1/words").arg(language), words);
    settings.setValue(QStringLiteral("learning/%1/bigrams").arg(language), bigrams);
    settings.setValue(QStringLiteral("learning/%1/forgotten").arg(language), snapshot.forgotten);
}

void saveLearningInBackground(const QString &language, LearningSnapshot snapshot)
{
    LearningSaves &saves = learningSaves();
    QMutexLocker lock(&saves.mutex);
    saves.pending.insert(language, std::move(snapshot));
    if (saves.scheduled) return;                  // the queued save takes these
    saves.scheduled = true;
    learningWriter().start([] {
        LearningSaves &saves = learningSaves();
        for (;;) {
            QHash<QString, LearningSnapshot> batch;
            {
                QMutexLocker lock(&saves.mutex);
                if (saves.pending.isEmpty()) {
                    saves.scheduled = false;
                    return;
                }
                batch.swap(saves.pending);
            }
            for (auto it = batch.cbegin(); it != batch.cend(); ++it) writeLearning(it.key(), it.value());
        }
    });
}

// LatinIME's user history dictionary holds at most 10 000 words and 30 000
// word pairs (HeaderPolicy::DEFAULT_MAX_NGRAM_COUNTS); once it reaches 1.2
// times that (ForgettingCurveUtils::ENTRY_COUNT_HARD_LIMIT_WEIGHT), the
// lowest-ranked entries are removed down to the maximum
// (LanguageModelDictContent::truncateEntries). Here the rank is the count;
// LatinIME also lets entries fade with time, which these counts do not.
void pruneLearned(QHash<QString, int> &counts, qsizetype max)
{
    if (counts.size() < max + max / 5) return;
    std::vector<int> values;
    values.reserve(size_t(counts.size()));
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) values.push_back(it.value());
    const size_t cut = values.size() - size_t(max);
    std::nth_element(values.begin(), values.begin() + qptrdiff(cut), values.end());
    const int threshold = values[cut];               // the max-th largest count
    // Everything above the threshold stays; of the entries at it, as many as
    // fit (at least one).
    qsizetype above = 0;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) if (it.value() > threshold) ++above;
    qsizetype roomAtThreshold = max - above;
    for (auto it = counts.begin(); it != counts.end();) {
        if (it.value() < threshold || (it.value() == threshold && roomAtThreshold-- <= 0)) it = counts.erase(it);
        else ++it;
    }
}

QStringList &frequencyPathOverride()
{
    static QStringList paths;
    return paths;
}

QStringList &blocklistPathOverride()
{
    static QStringList paths;
    return paths;
}

QStringList &bigramPathOverride()
{
    static QStringList paths;
    return paths;
}

QString &userDictionaryFileOverride()
{
    static QString path;
    return path;
}

void loadFrequency(DictionaryData &data, const QString &language, const QStringList &paths)
{
    QFile file;
    for (const QString &dir : paths) {
        file.setFileName(dir + QLatin1Char('/') + language + QStringLiteral(".txt"));
        if (file.exists()) break;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    const QString text = QString::fromUtf8(file.readAll());
    quint32 rank = 0;
    for (QStringView line : QStringView(text).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString word = line.trimmed().toString();
        if (word.isEmpty()) continue;
        ++rank;
        bool capital = false;
        if (data.speller) {
            // Keep only real words; German nouns and names are valid only
            // capitalized, so remember that for display.
            if (!data.spell(word)) {
                if (!data.spell(capitalizeFirst(word))) {
                    // Below 20 000 the list's unknown words turn into typos
                    // people make too ("realy"): measured, keeping 50 000
                    // left 0.4 % more typos uncorrected.
                    if (rank <= 20000) data.listedOnly.push_back(wordHash(word));
                    continue;
                }
                capital = true;
            }
        }
        data.freq.push_back({word, rank, capital});
    }
    std::sort(data.listedOnly.begin(), data.listedOnly.end());
    data.listedOnly.shrink_to_fit();
    std::sort(data.freq.begin(), data.freq.end(), [](const auto &a, const auto &b) { return a.word < b.word; });
    data.freq.erase(std::unique(data.freq.begin(), data.freq.end(), [](const auto &a, const auto &b) { return a.word == b.word; }), data.freq.end());
    data.freq.shrink_to_fit();
}

// After loadFrequency: followers point into the sorted frequency list.
void loadBigrams(DictionaryData &data, const QString &language, const QStringList &paths)
{
    QFile file;
    for (const QString &dir : paths) {
        file.setFileName(dir + QLatin1Char('/') + language + QStringLiteral(".txt"));
        if (file.exists()) break;
    }
    if (data.freq.empty() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    const QString text = QString::fromUtf8(file.readAll());
    std::vector<std::pair<quint64, quint16>> pairs;
    pairs.reserve(size_t(text.count(QLatin1Char('\n'))));
    QStringView kept;                       // the previous word whose followers are being taken
    int taken = 0;
    for (QStringView line : QStringView(text).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        if (line.startsWith(QLatin1Char('#'))) continue;
        const qsizetype a = line.indexOf(QLatin1Char(' '));
        const qsizetype b = a < 0 ? -1 : line.indexOf(QLatin1Char(' '), a + 1);
        if (b < 0) continue;
        const QStringView previous = line.left(a), shown = line.mid(a + 1, b - a - 1);
        const quint16 count = quint16(qMin(65535u, line.mid(b + 1).toUInt()));
        if (count == 0 || shown.isEmpty()) continue;
        const bool capital = shown.front().isUpper();
        const QString word = capital ? shown.toString().toLower() : shown.toString();
        pairs.emplace_back(DictionaryData::pairHash(previous, word), count);
        // The file lists each previous word's followers most common first.
        if (previous != kept) { kept = previous; taken = 0; }
        if (taken >= DictionaryData::FollowersKept) continue;
        if (const auto *entry = data.freqFind(word)) {
            data.followers.push_back({wordHash(previous), quint32(entry - data.freq.data()), count, capital});
            ++taken;
        }
    }
    std::sort(pairs.begin(), pairs.end());
    data.pairHashes.reserve(pairs.size());
    data.pairCounts.reserve(pairs.size());
    for (const auto &[hash, count] : pairs) {
        data.pairHashes.push_back(hash);
        data.pairCounts.push_back(count);
    }
    std::stable_sort(data.followers.begin(), data.followers.end(), [](const auto &x, const auto &y) {
        return x.previous != y.previous ? x.previous < y.previous : x.count > y.count;
    });
    data.followers.shrink_to_fit();
}

bool isWordText(QStringView word)
{
    if (word.isEmpty() || word.size() > 48) return false;
    for (const QChar ch : word) {
        if (!ch.isLetter() && ch != QLatin1Char('\'') && ch != QLatin1Char('-')) return false;
    }
    return true;
}

QString decodeText(const QByteArray &raw, const QByteArray &encoding)
{
    if (encoding.isEmpty() || encoding == "UTF-8") return QString::fromUtf8(raw);
    if (encoding == "ISO8859-1" || encoding == "ISO-8859-1") return QString::fromLatin1(raw);
    QStringDecoder decoder(encoding.constData());
    if (!decoder.isValid()) return {};
    return decoder.decode(raw);
}

std::shared_ptr<DictionaryData> loadDictionary(const QString &language, const QStringList &searchPaths,
                                               const QStringList &frequencyPaths, const QStringList &bigramPaths)
{
    QElapsedTimer timer;
    timer.start();
    auto data = std::make_shared<DictionaryData>();
    data->language = language;

    QString base;
    for (const QString &name : dictionaryNames(language)) {
        for (const QString &dir : searchPaths) {
            const QString candidate = dir + QLatin1Char('/') + name;
            if (QFileInfo::exists(candidate + QStringLiteral(".dic")) && QFileInfo::exists(candidate + QStringLiteral(".aff"))) {
                base = candidate;
                break;
            }
        }
        if (!base.isEmpty()) break;
    }
    if (base.isEmpty()) {
        qCInfo(lcLexicon) << "no dictionary for" << language;
        loadFrequency(*data, language, frequencyPaths);
        loadBigrams(*data, language, bigramPaths);
        return data;
    }

    const QByteArray aff = QFile::encodeName(base + QStringLiteral(".aff"));
    const QByteArray dic = QFile::encodeName(base + QStringLiteral(".dic"));
    data->speller = std::make_unique<Hunspell>(aff.constData(), dic.constData());
    data->encoding = QByteArray(data->speller->get_dict_encoding().c_str()).toUpper();

    QFile file(QString::fromLocal8Bit(dic));
    if (!file.open(QIODevice::ReadOnly)) return data;
    const QString text = decodeText(file.readAll(), data->encoding);

    struct Item { QString word; bool capital; };
    std::vector<Item> items;
    items.reserve(text.count(QLatin1Char('\n')));
    bool first = true;
    for (QStringView line : QStringView(text).split(QLatin1Char('\n'))) {
        if (first) { first = false; continue; }          // entry count
        if (line.isEmpty() || line.front().isSpace() || line.front() == QLatin1Char('#')) continue;
        qsizetype cut = line.size();
        for (qsizetype i = 0; i < line.size(); ++i) {
            const QChar ch = line.at(i);
            if ((ch == QLatin1Char('/') && (i == 0 || line.at(i - 1) != QLatin1Char('\\'))) || ch.isSpace()) { cut = i; break; }
        }
        const QStringView word = line.left(cut);
        if (!isWordText(word)) continue;
        const QString lower = word.toString().toLower();
        if (word.size() > 1 && word.toString().toUpper() == word) continue;   // acronyms
        items.push_back({lower, word.front().isUpper()});
    }
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        const int c = a.word.compare(b.word);
        return c < 0 || (c == 0 && !a.capital && b.capital);
    });

    qsizetype total = 0;
    for (const Item &item : items) total += item.word.size() + 1;
    data->blob.reserve(total);
    data->entries.reserve(items.size());
    const QString *previous = nullptr;
    for (const Item &item : items) {
        if (previous && *previous == item.word) continue;   // lowercase variant sorted first wins
        data->entries.push_back(quint32(data->blob.size()) | (item.capital ? DictionaryData::CapitalFlag : 0));
        data->blob += item.word;
        data->blob += QLatin1Char('\n');
        previous = &item.word;
    }
    data->blob.squeeze();
    loadFrequency(*data, language, frequencyPaths);
    loadBigrams(*data, language, bigramPaths);
    qCInfo(lcLexicon) << "loaded" << language << data->entries.size() << "entries," << data->freq.size()
                      << "frequency words," << data->pairHashes.size() << "word pairs in" << timer.elapsed() << "ms";
    return data;
}


bool isAdjacentTransposition(const QString &a, const QString &b)
{
    if (a.size() != b.size() || a.size() < 2) return false;
    int first = -1;
    for (int i = 0; i < a.size(); ++i) {
        if (a.at(i) != b.at(i)) { first = i; break; }
    }
    if (first < 0 || first + 1 >= a.size()) return false;
    return a.at(first) == b.at(first + 1) && a.at(first + 1) == b.at(first)
        && QStringView(a).mid(first + 2) == QStringView(b).mid(first + 2);
}

bool differsByRepeatedLetter(const QString &shorter, const QString &longer)
{
    if (longer.size() != shorter.size() + 1) return false;
    for (int i = 0; i < longer.size(); ++i) {
        QString removed = longer;
        removed.remove(i, 1);
        if (removed == shorter) {
            return (i > 0 && longer.at(i - 1) == longer.at(i))
                || (i + 1 < longer.size() && longer.at(i + 1) == longer.at(i));
        }
    }
    return false;
}

int levenshtein(const QString &a, const QString &b)
{
    QVector<int> prev(b.size() + 1);
    QVector<int> cur(b.size() + 1);
    for (int j = 0; j <= b.size(); ++j) prev[j] = j;
    for (int i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (int j = 1; j <= b.size(); ++j) {
            const int cost = a.at(i - 1) == b.at(j - 1) ? 0 : 1;
            cur[j] = qMin(qMin(cur[j - 1] + 1, prev[j] + 1), prev[j - 1] + cost);
        }
        prev.swap(cur);
    }
    return prev[b.size()];
}

QString collapsed(const QString &word)
{
    QString result;
    for (const QChar ch : word) {
        if (result.isEmpty() || result.back() != ch) result.append(ch);
    }
    return result;
}

}

LocalLexicon::LocalLexicon()
{
    m_coreWords = coreWords(m_language);
    m_coreWordSet = QSet<QString>(m_coreWords.cbegin(), m_coreWords.cend());
    m_typoMap = curatedTypos(m_language);
}

LocalLexicon::~LocalLexicon()
{
    flushLearning();
    waitForLearningWrites();
    if (m_pending.valid()) m_pending.wait();
}

void LocalLexicon::waitForLearningWrites()
{
    learningWriter().waitForDone();
}

void LocalLexicon::setDictionarySearchPaths(const QStringList &paths)
{
    searchPathOverride() = paths;
}

void LocalLexicon::setFrequencySearchPaths(const QStringList &paths)
{
    frequencyPathOverride() = paths;
}

QStringList LocalLexicon::frequencySearchPaths()
{
    if (!frequencyPathOverride().isEmpty()) return frequencyPathOverride();
    return {QStringLiteral(":/tastra/frequency")};
}

void LocalLexicon::setBigramSearchPaths(const QStringList &paths)
{
    bigramPathOverride() = paths;
}

QStringList LocalLexicon::bigramSearchPaths()
{
    if (!bigramPathOverride().isEmpty()) return bigramPathOverride();
    return {QStringLiteral(":/tastra/bigrams")};
}

void LocalLexicon::setBlocklistSearchPaths(const QStringList &paths)
{
    blocklistPathOverride() = paths;
}

QStringList LocalLexicon::blocklistSearchPaths()
{
    if (!blocklistPathOverride().isEmpty()) return blocklistPathOverride();
    return {QStringLiteral(":/tastra/blocklist")};
}

void LocalLexicon::setBlockOffensive(bool enabled) { m_blockOffensive = enabled; }

void LocalLexicon::setUserDictionaryFile(const QString &path) { userDictionaryFileOverride() = path; }

namespace
{
// Loading a language allocates far more temporaries (the word-list text,
// parse buffers, Hunspell's affix tables while it builds) than it keeps, and
// glibc keeps those freed pages mapped. Handing them back after a load
// measured (tools/bench_memory, resident set after loading):
// en 31 -> 24 MB, ru 47 -> 35 MB, uk 80 -> 61 MB. Runs once per load.
void releaseFreedMemory()
{
#if defined(__GLIBC__)
    malloc_trim(0);
#endif
}

// A language's word list kept only to answer "is this a word there?": 64-bit
// hashes in a sorted vector instead of the strings (tools/bench_memory: a
// 50 000-word list added 4.4 MB resident as QSet<QString>, 1.1 MB as
// hashes, 0.4 MB of which is the vector itself). Two different words
// sharing a hash among 50 000 is a ~1e-10 chance and would only cost an extra
// layout offer.
class WordHashes
{
public:
    static quint64 hashOf(QStringView word) { return wordHash(word); }
    void add(QStringView word) { m_hashes.push_back(hashOf(word)); }
    void finish(const std::vector<quint64> &blocked)
    {
        std::sort(m_hashes.begin(), m_hashes.end());
        m_hashes.erase(std::unique(m_hashes.begin(), m_hashes.end()), m_hashes.end());
        for (const quint64 b : blocked) {
            const auto it = std::lower_bound(m_hashes.begin(), m_hashes.end(), b);
            if (it != m_hashes.end() && *it == b) m_hashes.erase(it);
        }
        m_hashes.shrink_to_fit();
    }
    bool contains(QStringView word) const
    {
        return !word.isEmpty() && std::binary_search(m_hashes.begin(), m_hashes.end(), hashOf(word));
    }

private:
    std::vector<quint64> m_hashes;
};

struct ForeignWords
{
    QMutex mutex;
    QHash<QString, WordHashes> sets;
    QSet<QString> loading;
};

ForeignWords &foreignWords()
{
    static ForeignWords words;
    return words;
}

WordHashes readForeignWords(const QString &language, const QStringList &frequencyPaths, const QStringList &blockPaths)
{
    WordHashes words;
    for (const QString &dir : frequencyPaths) {
        QFile file(dir + QLatin1Char('/') + language + QStringLiteral(".txt"));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const QString text = QString::fromUtf8(file.readAll()).toLower();
        for (QStringView line : QStringView(text).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            line = line.trimmed();
            if (!line.isEmpty()) words.add(line);
        }
        break;
    }
    // Never offer that language's offensive words either.
    std::vector<quint64> blocked;
    for (const QString &dir : blockPaths) {
        QFile file(dir + QLatin1Char('/') + language + QStringLiteral(".txt"));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const QString text = QString::fromUtf8(file.readAll()).toLower();
        for (QStringView line : QStringView(text).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            line = line.trimmed();
            if (!line.isEmpty()) blocked.push_back(WordHashes::hashOf(line));
        }
        break;
    }
    words.finish(blocked);
    return words;
}
}

void LocalLexicon::clearForeignWordCacheForTesting()
{
    QMutexLocker lock(&foreignWords().mutex);
    foreignWords().sets.clear();
    foreignWords().loading.clear();
}

void LocalLexicon::preloadForeignWordsForTesting(const QString &language)
{
    WordHashes words = readForeignWords(language, frequencySearchPaths(), blocklistSearchPaths());
    QMutexLocker lock(&foreignWords().mutex);
    foreignWords().sets.insert(language, std::move(words));
}

bool LocalLexicon::knownInLanguage(const QString &language, const QString &word)
{
    ForeignWords &f = foreignWords();
    {
        QMutexLocker lock(&f.mutex);
        const auto it = f.sets.constFind(language);
        if (it != f.sets.constEnd()) return it->contains(word.toLower());
        if (f.loading.contains(language)) return false;
        f.loading.insert(language);
    }
    // Load off the typing path (measured: ~115 ms for two lists on the GUI
    // thread); offers start once the list is there.
    const QStringList frequencyPaths = frequencySearchPaths();
    const QStringList blockPaths = blocklistSearchPaths();
    QThreadPool::globalInstance()->start([language, frequencyPaths, blockPaths] {
        WordHashes words = readForeignWords(language, frequencyPaths, blockPaths);
        {
            QMutexLocker lock(&foreignWords().mutex);
            foreignWords().sets.insert(language, std::move(words));
            foreignWords().loading.remove(language);
        }
        releaseFreedMemory();
    });
    return false;
}

void LocalLexicon::setCompanionLanguages(const QStringList &codes)
{
    m_companions = codes;
    m_companions.removeAll(m_language);
    m_companions.removeDuplicates();
    // Start the background loads now, before the first word needs them.
    for (const QString &code : std::as_const(m_companions)) knownInLanguage(code, QString());
}

bool LocalLexicon::listedButNotInDictionary(const QString &word) const
{
    adoptLoadedData();
    return m_data && m_data->isListedOnly(normalize(word));
}

bool LocalLexicon::knownInCompanionLanguage(const QString &word) const
{
    const QString w = normalize(word);
    if (w.isEmpty()) return false;
    for (const QString &code : m_companions) {
        if (code != m_language && knownInLanguage(code, w)) return true;
    }
    return false;
}

QString LocalLexicon::userDictionaryFile()
{
    if (!userDictionaryFileOverride().isEmpty()) return userDictionaryFileOverride();
    const QString env = qEnvironmentVariable("TASTRA_DICTIONARY_FILE");
    if (!env.isEmpty()) return env;
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/tastra/dictionary.txt");
}

void LocalLexicon::reloadUserDictionaryFile()
{
    m_fileWords.clear();
    QFile file(userDictionaryFile());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    const QString text = QString::fromUtf8(file.readAll());
    for (QStringView line : QStringView(text).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString word = line.trimmed().toString();
        if (word.isEmpty() || word.startsWith(QLatin1Char('#')) || word.contains(QLatin1Char(' '))) continue;
        m_fileWords.insert(normalize(word), word);
    }
}

bool LocalLexicon::isUserWord(const QString &word) const
{
    return m_userWords.contains(word) || m_fileWords.contains(word)
        || m_personalFrequency.value(word) >= PromotionCount;
}

void LocalLexicon::persistUserWords()
{
    QSettings().setValue(QStringLiteral("dictionary/%1").arg(m_language), QStringList(m_userWords.values()));
}

bool LocalLexicon::addUserWord(const QString &word)
{
    const QString display = word.trimmed();
    const QString key = normalize(display);
    if (key.isEmpty() || key.size() > 48 || display.contains(QLatin1Char(' '))) return false;
    m_userWords.insert(key, display);
    m_forgotten.remove(key);
    persistUserWords();
    ++m_unsavedLearning;
    flushLearning();
    return true;
}

void LocalLexicon::removeUserWord(const QString &word)
{
    if (m_userWords.remove(normalize(word)) > 0) persistUserWords();
}

QStringList LocalLexicon::userWords() const
{
    QStringList words = m_userWords.values();
    std::sort(words.begin(), words.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return words;
}

void LocalLexicon::promoteWord(const QString &word)
{
    const QString w = normalize(word);
    if (w.isEmpty()) return;
    m_personalFrequency[w] = qMax(m_personalFrequency.value(w), int(PromotionCount));
    m_forgotten.remove(w);
    ++m_unsavedLearning;
    flushLearning();
}

void LocalLexicon::setKeyboardRows(const QStringList &rows)
{
    // Key centres in key-width units: the first two rows span the full width,
    // the third sits between Shift and Backspace (as on the on-screen layout).
    m_keyCentres.clear();
    for (int r = 0; r < rows.size(); ++r) {
        const QString row = rows.at(r).toLower();
        const int n = int(row.size());
        for (int i = 0; i < n; ++i) {
            const qreal x = r < 2 ? (i + 0.5) * (10.0 / n) : 1.25 + (i + 0.5) * (7.5 / n);
            m_keyCentres.insert(row.at(i), QPointF(x, r));
        }
    }
}

void LocalLexicon::setTouchOffsets(const QVector<QPointF> &offsets) { m_touchOffsets = offsets; }

// A slip to a neighbouring key is likelier when the finger landed near the
// edge facing that key (LatinIME scores by the distance from the touch point
// to key centres). Bonus 100 without touch data, 0..200 with it.
int LocalLexicon::neighbourBonus(QChar typed, QChar intended, int index) const
{
    if (index < 0 || index >= m_touchOffsets.size()) return 100;
    const auto from = m_keyCentres.constFind(typed.toLower());
    const auto to = m_keyCentres.constFind(intended.toLower());
    if (from == m_keyCentres.constEnd() || to == m_keyCentres.constEnd()) return 100;
    QPointF dir = *to - *from;
    const qreal length = std::hypot(dir.x(), dir.y());
    if (length <= 0) return 100;
    dir /= length;
    const QPointF touch = m_touchOffsets.at(index);
    const qreal towards = qBound(-0.5, touch.x() * dir.x() + touch.y() * dir.y(), 0.5);
    return int(100 + 200 * towards);
}

bool LocalLexicon::neighbours(QChar a, QChar b) const
{
    const auto pa = m_keyCentres.constFind(a.toLower());
    const auto pb = m_keyCentres.constFind(b.toLower());
    if (pa == m_keyCentres.constEnd() || pb == m_keyCentres.constEnd()) return false;
    const QPointF d = *pa - *pb;
    return d.x() * d.x() + d.y() * d.y() <= 1.5 * 1.5;
}

void LocalLexicon::loadBlocklist()
{
    m_offensive.clear();
    for (const QString &dir : blocklistSearchPaths()) {
        QFile file(dir + QLatin1Char('/') + m_language + QStringLiteral(".txt"));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const QString text = QString::fromUtf8(file.readAll());
        for (QStringView line : QStringView(text).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            m_offensive.insert(line.trimmed().toString());
        }
        break;
    }
}

QStringList LocalLexicon::apostropheVariants(const QString &typed) const
{
    // English contractions and Ukrainian words such as "п'ять" (LatinIME:
    // intentional omission of an apostrophe or hyphen).
    const LanguageInfo *info = languageInfo(m_language);
    if (m_language != QStringLiteral("en") && m_language != QStringLiteral("uk") && !(info && info->apostrophes)) return {};
    if (typed.size() < 2 || typed.contains(QLatin1Char('\''))) return {};
    QStringList variants;
    for (int i = 1; i < typed.size(); ++i) variants << QString(typed).insert(i, QLatin1Char('\''));
    return variants;
}

namespace
{
// Base letter -> accented letters it stands for, per language.
QString accentsFor(const QString &language, QChar base)
{
    if (languageInfo(language)) {
        // Ligatures stand for two letters ("coeur"): see unaccentedSpellings().
        QString accents = languageAccents(language, base);
        accents.remove(QChar(0x00E6));
        accents.remove(QChar(0x0153));
        return accents;
    }
    if (language == QStringLiteral("de")) {
        switch (base.unicode()) {
        case 'a': return QStringLiteral("ä");
        case 'o': return QStringLiteral("ö");
        case 'u': return QStringLiteral("ü");
        case 's': return QStringLiteral("ß");
        default: return {};
        }
    }
    if (language == QStringLiteral("ru") && base == QChar(0x0435)) return QStringLiteral("ё");   // е
    if (language == QStringLiteral("uk") && base == QChar(0x0433)) return QStringLiteral("ґ");   // г
    return {};
}

// What an accented spelling looks like typed without accents, both ways a
// German writes it: "über" -> "uber" and "ueber", "Straße" -> "strasse".
QStringList unaccentedSpellings(const QString &word)
{
    QString plain, digraph;
    for (const QChar ch : word) {
        switch (ch.unicode()) {
        case 0x00E4: plain += QLatin1Char('a'); digraph += QStringLiteral("ae"); break;   // ä
        case 0x00F6: plain += QLatin1Char('o'); digraph += QStringLiteral("oe"); break;   // ö
        case 0x00FC: plain += QLatin1Char('u'); digraph += QStringLiteral("ue"); break;   // ü
        case 0x00DF: plain += QStringLiteral("ss"); digraph += QStringLiteral("ss"); break; // ß
        case 0x0451: plain += QChar(0x0435); digraph += QChar(0x0435); break;            // ё -> е
        case 0x0491: plain += QChar(0x0433); digraph += QChar(0x0433); break;            // ґ -> г
        case 0x0142: plain += QLatin1Char('l'); digraph += QLatin1Char('l'); break;      // ł
        case 0x00F8: plain += QLatin1Char('o'); digraph += QLatin1Char('o'); break;      // ø
        case 0x00E6: plain += QStringLiteral("ae"); digraph += QStringLiteral("ae"); break; // æ
        case 0x0153: plain += QStringLiteral("oe"); digraph += QStringLiteral("oe"); break; // œ
        default:
            // Other accented Latin letters (é, ñ, ą, ç ...) without the accent.
            if (ch.unicode() >= 0x00C0 && ch.unicode() <= 0x017F) {
                const QString bare = QString(ch).normalized(QString::NormalizationForm_D);
                if (bare.size() > 1 && bare.front().unicode() < 0x80) {
                    plain += bare.front();
                    digraph += bare.front();
                    break;
                }
            }
            plain += ch;
            digraph += ch;
        }
    }
    QStringList forms{plain};
    if (digraph != plain) forms << digraph;
    // "ß" is also typed as a single "s" ("gros").
    if (word.contains(QChar(0x00DF))) forms << QString(word).replace(QChar(0x00DF), QLatin1Char('s'));
    return forms;
}
}

QStringList LocalLexicon::accentVariants(const QString &typed) const
{
    // One step: a base letter becomes its accented form, a German digraph
    // (ae/oe/ue) its umlaut, "ss" a "ß". Up to two steps ("grusse" -> "grüße").
    auto step = [this](const QString &w) {
        QStringList out;
        for (int i = 0; i < w.size(); ++i) {
            for (const QChar accented : accentsFor(m_language, w.at(i))) out << QString(w).replace(i, 1, accented);
            if (m_language != QStringLiteral("de") || i + 1 >= w.size()) continue;
            const QStringView pair = QStringView(w).mid(i, 2);
            if (pair == u"ss") out << QString(w).replace(i, 2, QChar(0x00DF));
            else if (pair == u"ae") out << QString(w).replace(i, 2, QChar(0x00E4));
            else if (pair == u"oe") out << QString(w).replace(i, 2, QChar(0x00F6));
            else if (pair == u"ue") out << QString(w).replace(i, 2, QChar(0x00FC));
        }
        return out;
    };
    QStringList variants;
    for (const QString &once : step(typed)) {
        if (!variants.contains(once)) variants << once;
        for (const QString &twice : step(once)) {
            if (!variants.contains(twice)) variants << twice;
        }
    }
    return variants;
}

bool LocalLexicon::isAccentVariant(const QString &typed, const QString &candidate) const
{
    if (candidate == typed) return false;
    const QStringList typedForms = unaccentedSpellings(typed);
    for (const QString &form : unaccentedSpellings(candidate)) {
        if (form == typed || typedForms.contains(form)) return true;
    }
    return false;
}

QString LocalLexicon::dictionaryForm(const QString &word) const
{
    if (const qsizetype space = word.indexOf(QLatin1Char(' ')); space > 0) {
        auto form = [this](const QString &part) {                // "i like" -> "I like"
            const QString capital = englishCapitalForm(part);
            return capital.isEmpty() ? dictionaryForm(part) : capital;
        };
        return form(word.left(space)) + QLatin1Char(' ') + form(word.mid(space + 1));
    }
    if (const auto it = m_userWords.constFind(word); it != m_userWords.constEnd()) return it.value();
    if (const auto it = m_fileWords.constFind(word); it != m_fileWords.constEnd()) return it.value();
    adoptLoadedData();
    if (!m_data) return word;
    if (const auto *entry = m_data->freqFind(word)) return entry->capital ? capitalizeFirst(word) : word;
    const auto [lo, hi] = m_data->prefixRange(word);
    if (lo < hi && m_data->wordAt(lo) == word) return m_data->capitalizedAt(lo) ? capitalizeFirst(word) : word;
    if (m_data->speller && !m_data->spell(word) && m_data->spell(capitalizeFirst(word))) return capitalizeFirst(word);
    return word;
}

QString LocalLexicon::englishCapitalForm(const QString &typed) const
{
    if (m_language != QStringLiteral("en") || typed.isEmpty()) return {};
    // Hunspell's en_US also accepts a lowercase "i" (the letter); as a word
    // it is always "I" (AOSP's English dictionary: "i" is not_a_word with
    // the shortcut "I").
    if (typed == QLatin1String("i")) return QStringLiteral("I");
    adoptLoadedData();
    if (!m_data || !m_data->speller) return {};
    if (m_data->spell(typed)) return {};
    const QString capital = capitalizeFirst(typed);
    return m_data->spell(capital) ? capital : QString();
}

bool LocalLexicon::suggestible(const QString &word) const
{
    if (m_forgotten.contains(word)) return false;
    return !(m_blockOffensive && m_offensive.contains(word));
}

void LocalLexicon::forgetWord(const QString &word)
{
    const QString w = normalize(word);
    if (w.isEmpty()) return;
    m_personalFrequency.remove(w);
    const QChar sep(0x001f);
    for (auto it = m_bigramFrequency.begin(); it != m_bigramFrequency.end();) {
        const qsizetype at = it.key().indexOf(sep);
        if (it.key().left(at) == w || it.key().mid(at + 1) == w) it = m_bigramFrequency.erase(it);
        else ++it;
    }
    m_learnedFollowersValid = false;
    m_forgotten.insert(w);
    if (m_userWords.remove(w) > 0) persistUserWords();
    ++m_unsavedLearning;
    flushLearning();
}

void LocalLexicon::forgetPair(const QString &word, const QString &next)
{
    const QString w = normalize(word);
    const QString n = normalize(next);
    if (w.isEmpty() || n.isEmpty()) return;
    adoptLoadedData();
    if (m_bigramFrequency.remove(bigramKey(w, n)) == 0) return;
    m_learnedFollowersValid = false;
    ++m_unsavedLearning;
    flushLearning();
}

int LocalLexicon::frequencyRank(const QString &word) const
{
    if (!m_data) return 0;
    const auto *entry = m_data->freqFind(word);
    return entry ? int(entry->rank) : 0;
}

QStringList LocalLexicon::dictionarySearchPaths()
{
    if (!searchPathOverride().isEmpty()) return searchPathOverride();
    const QString env = qEnvironmentVariable("TASTRA_DICTIONARY_DIRS");
    if (!env.isEmpty()) return env.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    return {
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/tastra/dictionaries"),
        QStringLiteral("/usr/share/hunspell"),
        QStringLiteral("/usr/share/myspell/dicts"),
    };
}

void LocalLexicon::setLanguage(const QString &code)
{
    if (code == m_language && m_loadStarted) return;
    if (code != m_language) flushLearning();
    m_language = code;
    m_coreWords = coreWords(code);
    m_coreWordSet = QSet<QString>(m_coreWords.cbegin(), m_coreWords.cend());
    m_typoMap = curatedTypos(code);
    loadLearning();
    loadBlocklist();
    startLoading();
}

QString LocalLexicon::language() const { return m_language; }

void LocalLexicon::startLoading()
{
    m_loadStarted = true;
    // Release the previous language right away: one dictionary in memory.
    m_data.reset();
    std::promise<std::shared_ptr<DictionaryData>> promise;
    m_pending = promise.get_future();
    std::thread([promise = std::move(promise), language = m_language, paths = dictionarySearchPaths(),
                 freqPaths = frequencySearchPaths(), bigramPaths = bigramSearchPaths()]() mutable {
        auto data = loadDictionary(language, paths, freqPaths, bigramPaths);
        // The load's temporaries are freed by now; hand the pages back here,
        // off the typing thread (measured up to 13 ms for Ukrainian).
        releaseFreedMemory();
        promise.set_value(std::move(data));
    }).detach();
}

void LocalLexicon::adoptLoadedData() const
{
    if (!m_pending.valid()) return;
    if (m_pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto data = m_pending.get();
    if (data && data->language == m_language) {
        m_data = std::move(data);
    } else if (data) {
        data.reset(); // a load for a language that is no longer selected
        releaseFreedMemory();
    }
}

bool LocalLexicon::waitForDictionary(int timeoutMs)
{
    if (!m_loadStarted) startLoading();
    if (m_pending.valid()) {
        if (timeoutMs < 0) m_pending.wait();
        else if (m_pending.wait_for(std::chrono::milliseconds(timeoutMs)) != std::future_status::ready) return false;
    }
    adoptLoadedData();
    return true;
}

bool LocalLexicon::hasSystemDictionary() const
{
    adoptLoadedData();
    return m_data && m_data->speller;
}

QString LocalLexicon::normalize(const QString &word) const
{
    QString w = word.trimmed().toLower();
    while (!w.isEmpty() && !w.front().isLetter()) w.remove(0, 1);
    while (!w.isEmpty() && !w.back().isLetter()) w.chop(1);
    return w;
}

QString LocalLexicon::alphabet() const
{
    if (const LanguageInfo *info = languageInfo(m_language)) return QString::fromUtf8(info->alphabet);
    if (m_language == QStringLiteral("de")) return QStringLiteral("abcdefghijklmnopqrstuvwxyzäöüß");
    if (m_language == QStringLiteral("uk")) return QStringLiteral("абвгґдеєжзиіїйклмнопрстуфхцчшщьюя'");
    if (m_language == QStringLiteral("ru")) return QStringLiteral("абвгдеёжзийклмнопрстуфхцчшщъыьэюя");
    return QStringLiteral("abcdefghijklmnopqrstuvwxyz");
}

bool LocalLexicon::isCore(const QString &word) const { return m_coreWordSet.contains(word); }

bool LocalLexicon::isValidWord(const QString &word) const
{
    if (word.isEmpty()) return false;
    if (isUserWord(word) || isCore(word)) return true;
    adoptLoadedData();
    if (!m_data) return false;
    if (m_data->freqFind(word) || m_data->contains(word)) return true;
    if (m_data->speller) {
        // Hunspell is case-sensitive: German nouns and names are stored
        // capitalized, so a lowercase "haus" is still a real word.
        return m_data->spell(word) || m_data->spell(capitalizeFirst(word));
    }
    return m_data->contains(word) || m_data->freqFind(word);
}

QString LocalLexicon::bigramKey(const QString &previousWord, const QString &word) const
{
    return previousWord + QChar(0x001f) + word;
}

int LocalLexicon::priorScore(const QString &candidate, const QString &previousWord) const
{
    int score = 0;
    if (m_userWords.contains(candidate) || m_fileWords.contains(candidate)) score += 400;
    // The small built-in core list is only a prior when no frequency data exists.
    if (isCore(candidate) && (!m_data || m_data->freq.empty())) score += 150;
    if (const int rank = frequencyRank(candidate); rank > 0) {
        // 360 for the most common word, falling logarithmically to 0 at 50k.
        score += qMax(0, int(360.0 * (1.0 - std::log(double(rank)) / std::log(50001.0))));
    }
    const int personal = m_personalFrequency.value(candidate);
    if (personal > 0) {
        // Personal use outweighs the generic core list after a few repetitions.
        score += qMin(450, 100 + personal * 40);
        // Bigrams only exist for learned words; skip the key allocation otherwise.
        if (!previousWord.isEmpty()) score += qMin(600, m_bigramFrequency.value(bigramKey(previousWord, candidate)) * 60);
    }
    // The bundled pairs: "i hvae" -> "have", glides after "thank" -> "you".
    // Measured on held-out text of the corpora (typos from noisy taps,
    // human-like glides; en/de/ru/uk): 30 per doubling lifted glides from
    // 88.4 to 89.7 % and left autocorrect even (76.5 -> 76.8 % right,
    // 0.65 -> 0.72 % wrong); 60 and 100 added wrong corrections.
    if (!previousWord.isEmpty() && m_data && !m_data->pairHashes.empty()) {
        if (const quint32 n = m_data->pairCount(previousWord, candidate); n > 0)
            score += qMin(300, int(30.0 * std::log2(1.0 + n)));
    }
    return score;
}

bool LocalLexicon::hasWordCheaply(const QString &word) const
{
    adoptLoadedData();
    return knownCheaply(normalize(word));
}

bool LocalLexicon::knownCheaply(const QString &word) const
{
    if (word.isEmpty()) return false;
    if (isUserWord(word) || isCore(word)) return true;
    return m_data && (m_data->freqFind(word) || m_data->contains(word));
}

QList<LocalLexicon::Candidate> LocalLexicon::correctionCandidates(const QString &typed, const QString &previousWord, bool full) const
{
    // Variants in order of likelihood (LatinIME-style error model): adjacent
    // transpositions, dropped letters, doubled letters, neighbouring keys,
    // then everything else. The Hunspell budget below is spent on the likely
    // ones first, so long unknown words (German compounds) cannot stall Space.
    QStringList ordered;
    QSet<QString> seen{typed};
    seen.reserve(int(typed.size() + 1) * int(alphabet().size()) * 2 + 64);   // no rehash per keystroke
    auto add = [&](const QString &v) { if (!seen.contains(v)) { seen.insert(v); ordered.append(v); } };
    const QString chars = alphabet();
    // The nearly free edits first (LatinIME intentional omission, accents).
    for (const QString &v : apostropheVariants(typed)) add(v);
    for (const QString &v : accentVariants(typed)) add(v);
    for (int i = 0; i + 1 < typed.size(); ++i) { QString v = typed; std::swap(v[i], v[i + 1]); add(v); }
    for (int i = 0; i < typed.size(); ++i) { QString v = typed; v.remove(i, 1); add(v); }
    for (int i = 0; i < typed.size(); ++i) { QString v = typed; v.insert(i, typed.at(i)); add(v); }
    for (int i = 0; i < typed.size(); ++i) {
        for (const QChar ch : chars) {
            if (ch != typed.at(i) && neighbours(ch, typed.at(i))) { QString v = typed; v[i] = ch; add(v); }
        }
    }
    for (int i = 0; i < typed.size(); ++i) {
        for (const QChar ch : chars) { if (typed.at(i) != ch) { QString v = typed; v[i] = ch; add(v); } }
    }
    for (int i = 0; i <= typed.size(); ++i) {
        for (const QChar ch : chars) { QString v = typed; v.insert(i, ch); add(v); }
    }
    const QStringList &variants = ordered;
    int hunspellBudget = 250;
    const bool capitalNouns = m_language == QStringLiteral("de");
    const bool hasFrequencyList = m_data && !m_data->freq.empty();
    auto rankedOrOwn = [this](const QString &v) {
        return frequencyRank(v) > 0 || isCore(v) || isUserWord(v) || m_personalFrequency.value(v) > 0;
    };

    QList<Candidate> result;
    for (const QString &v : variants) {
        if (v.isEmpty() || !suggestible(v)) continue;
        bool valid = knownCheaply(v);
        if (!valid && full && hunspellBudget > 0) {
            adoptLoadedData();
            if (m_data && m_data->speller) {
                --hunspellBudget;
                valid = m_data->spell(v) || (capitalNouns && m_data->spell(capitalizeFirst(v)));
            }
        }
        if (!valid) continue;
        Candidate c;
        c.word = v;
        if (v.size() == typed.size() + 1 && QString(v).remove(QLatin1Char('\'')) == typed) c.edit = Edit::Apostrophe;
        else if (isAccentVariant(typed, v)) c.edit = Edit::Accent;
        else if (isAdjacentTransposition(typed, v)) c.edit = Edit::Transposition;
        else if (differsByRepeatedLetter(typed, v) || differsByRepeatedLetter(v, typed)) c.edit = Edit::RepeatedLetter;
        else if (v.size() == typed.size()) {
            for (int k = 0; k < v.size(); ++k) {
                if (v.at(k) != typed.at(k)) {
                    if (neighbours(v.at(k), typed.at(k))) {
                        c.edit = Edit::NeighbourKey;
                        c.editIndex = k;
                    }
                    break;
                }
            }
        }
        c.score = 800 + priorScore(v, previousWord) - qAbs(v.size() - typed.size()) * 8;
        // A word the frequency list lacks is rarer than its last entry (its
        // wordfreq rank is beyond 50 000), not as common: "ohers" is "others",
        // not Hunspell's "hoers". Measured on seeded typos of frequent words
        // and of wordfreq words outside the list (en, de, ru, uk): right
        // 80.1 -> 82.3 %, wrong 4.5 -> 3.9 %; typos of the outside words,
        // 3-15 % of what is typed, are corrected wrongly 2.4 -> 3.1 %.
        if (hasFrequencyList && !rankedOrOwn(v)) c.score -= 70;
        if (c.edit == Edit::Apostrophe) c.score += 260;
        if (c.edit == Edit::Accent) c.score += 220;
        if (c.edit == Edit::Transposition) c.score += 150;
        if (c.edit == Edit::RepeatedLetter) c.score += 100;
        if (c.edit == Edit::NeighbourKey)                       // a finger slip to the next key
            c.score += neighbourBonus(typed.at(c.editIndex), v.at(c.editIndex), c.editIndex);
        result.append(c);
    }
    if (hasFrequencyList) addNeighbourKeyCandidates(typed, previousWord, seen, result);
    if (hasFrequencyList) addSplitCandidates(typed, previousWord, result);
    std::sort(result.begin(), result.end(), [](const Candidate &a, const Candidate &b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.word.size() != b.word.size()) return a.word.size() < b.word.size();
        return a.word < b.word;
    });
    return result;
}

void LocalLexicon::addNeighbourKeyCandidates(const QString &typed, const QString &previousWord,
                                             const QSet<QString> &seen, QList<Candidate> &result) const
{
    // Measured on taps with Gaussian noise around the key centres (sigma 0.25
    // key widths, 3000 frequent words per language, en/de/ru/uk): typos
    // corrected 71.8 -> 93.1 %, wrong 0.83 -> 0.35 %; at sigma 0.35 36 -> 80 %.
    // Each slip after the first costs 260 (a single slip scores 0..200 by
    // where the finger landed). Seeded single-edit typos (no touch data)
    // stay as before: right 82.4 -> 81.9 %, wrong 3.9 -> 4.0 %.
    const int n = int(typed.size());
    const int maxSlips = n >= 9 ? 4 : n >= 6 ? 3 : n >= 4 ? 2 : 0;
    constexpr int ExtraSlipCost = 260;
    if (maxSlips < 2 || m_keyCentres.isEmpty()) return;
    // The keys each typed letter may have been meant as: itself first, then
    // its neighbours.
    QVector<QString> options(n);
    for (int k = 0; k < n; ++k) {
        const QChar typedChar = typed.at(k);
        if (!m_keyCentres.contains(typedChar)) return;
        options[k] += typedChar;
        for (auto it = m_keyCentres.constBegin(); it != m_keyCentres.constEnd(); ++it) {
            if (it.key() != typedChar && neighbours(it.key(), typedChar)) options[k] += it.key();
        }
    }
    const auto &freq = m_data->freq;
    // The list is sorted, so the words sharing a prefix of length k form a
    // range ordered by their letter k (the prefix itself first): a trie walk
    // by binary search, with no index to build.
    struct Found { size_t entry; int slips; };
    QVarLengthArray<Found, 64> found;
    std::function<void(size_t, size_t, int, int)> walk = [&](size_t lo, size_t hi, int k, int slips) {
        if (k == n) {
            if (lo < hi && freq[lo].word.size() == n && slips >= 2 && found.size() < 256) found.append({lo, slips});
            return;
        }
        for (const QChar ch : options.at(k)) {
            const int nextSlips = slips + (ch != typed.at(k));
            if (nextSlips > maxSlips) continue;
            auto key = [k](const DictionaryData::FreqEntry &e) { return e.word.size() > k ? int(e.word.at(k).unicode()) + 1 : 0; };
            const int wanted = int(ch.unicode()) + 1;
            const auto first = std::lower_bound(freq.begin() + lo, freq.begin() + hi, wanted,
                                                [&key](const DictionaryData::FreqEntry &e, int v) { return key(e) < v; });
            const auto last = std::upper_bound(first, freq.begin() + hi, wanted,
                                               [&key](int v, const DictionaryData::FreqEntry &e) { return v < key(e); });
            if (first < last) walk(size_t(first - freq.begin()), size_t(last - freq.begin()), k + 1, nextSlips);
        }
    };
    walk(0, freq.size(), 0, 0);
    for (const Found &f : found) {
        const QString &word = freq[f.entry].word;
        if (seen.contains(word) || !suggestible(word)) continue;
        Candidate c;
        c.word = word;
        c.edit = Edit::NeighbourKeys;
        // Each slip scores like a single one (by where the finger landed);
        // every slip after the first is one more error.
        c.score = 800 + priorScore(word, previousWord) - ExtraSlipCost * (f.slips - 1);
        for (int k = 0; k < n; ++k) {
            if (word.at(k) != typed.at(k)) c.score += neighbourBonus(typed.at(k), word.at(k), k);
        }
        result.append(c);
    }
}

void LocalLexicon::addSplitCandidates(const QString &typed, const QString &previousWord, QList<Candidate> &result) const
{
    // Measured on 1500 random pairs of frequent words per language, typed
    // without the space or with a letter above the space bar for it
    // (en/de/ru/uk): Space now gives the two words in 95/97, 60/64, 92/97
    // and 89/93 % (wrong 0.2-2.3 %; German keeps its compounds together).
    // Single-word typos: right 81.9 -> 81.7 %, wrong 3.98 -> 4.00 %.
    constexpr int SplitCost = 100, MistypedSpaceCost = 150, StrayLetterSplitCost = -60, PartRankLimit = 20000;
    const int n = int(typed.size());
    if (n < 4) return;
    // A part must be a listed word; a single letter only a word of its own
    // ("a", "в", "і"; the lists also rank fragments such as German "s" from
    // "geht's"), so "ofthe" never becomes "o f the".
    static const QHash<QString, QString> oneLetterWords = {
        {QStringLiteral("en"), QStringLiteral("ai")},
        {QStringLiteral("ru"), QStringLiteral("авикосуя")},
        {QStringLiteral("uk"), QStringLiteral("авзійоуяє")},
    };
    const LanguageInfo *info = languageInfo(m_language);
    const QString oneLetter = info ? QString::fromUtf8(info->oneLetterWords) : oneLetterWords.value(m_language);
    auto partRank = [this, &oneLetter](const QString &part) {
        const int rank = frequencyRank(part);
        if (rank <= 0 || rank > PartRankLimit) return 0;
        return part.size() >= 2 || oneLetter.contains(part) ? rank : 0;
    };
    // German writes compounds as one word: "Tastaturtest" is not two words,
    // so there the second part may not be a noun.
    const bool compounds = m_language == QStringLiteral("de");
    auto validSplit = [&](const QString &first, const QString &second) {
        return partRank(first) && partRank(second) && suggestible(first) && suggestible(second)
            && !(compounds && dictionaryForm(second).front().isUpper());
    };
    auto addSplit = [&](const QString &first, const QString &second, int cost) {
        if (!validSplit(first, second)) return;
        Candidate c;
        c.word = first + QLatin1Char(' ') + second;
        c.edit = Edit::Split;
        c.score = 800 + qMin(priorScore(first, previousWord), priorScore(second, first)) - cost;
        result.append(c);
    };
    // The letters right above the space bar (the middle of the bottom row).
    auto aboveSpace = [this](QChar ch) {
        const auto it = m_keyCentres.constFind(ch);
        return it != m_keyCentres.constEnd() && it->y() == 2 && it->x() > 3.3 && it->x() < 7.4;
    };
    for (int i = 1; i < n; ++i) {
        const bool joined = validSplit(typed.left(i), typed.mid(i));
        // A one-letter word run into the next ("вобщем", "ihave") rather than
        // a stray first letter, unless that letter is next to the second one
        // (a slip while hitting it).
        const bool strayOneLetter = i == 1 && !neighbours(typed.at(0), typed.at(1));
        if (joined) addSplit(typed.left(i), typed.mid(i), strayOneLetter ? StrayLetterSplitCost : SplitCost);
        // "infront" is "in front", not "I front" with "n" for the space.
        if (i + 1 < n && aboveSpace(typed.at(i))
            && !joined && !validSplit(typed.left(i + 1), typed.mid(i + 1)))
            addSplit(typed.left(i), typed.mid(i + 1), MistypedSpaceCost);
    }
}

int LocalLexicon::correctionRank(const QString &word) const
{
    const qsizetype space = word.indexOf(QLatin1Char(' '));
    if (space < 0) return frequencyRank(word);
    const int first = frequencyRank(word.left(space));
    const int second = frequencyRank(word.mid(space + 1));
    return first > 0 && second > 0 ? qMax(first, second) : 0;
}

QStringList LocalLexicon::suggestions(const QString &word, const QString &previousWord, int limit) const
{
    const QString typed = normalize(word);
    if (typed.isEmpty() || limit <= 0) return {};
    const QString prev = normalize(previousWord);
    adoptLoadedData();

    QHash<QString, int> scored;
    QSet<QString> capitalized;
    auto consider = [&](const QString &candidate, bool capital) {
        if (scored.contains(candidate)) return;
        // What the user typed is always offered; everything else is filtered.
        if (candidate != typed && !suggestible(candidate)) return;
        int score = 1200 + priorScore(candidate, prev) - qAbs(candidate.size() - typed.size()) * 8;
        // The typed fragment itself only leads when it is a real, common word
        // ("he"), not an abbreviation-like fragment ("h", "пр").
        if (candidate == typed) {
            const int rank = frequencyRank(candidate);
            if (typed.size() > 2 || (rank > 0 && rank <= 300) || m_personalFrequency.value(candidate) > 0) score += 500;
            else score -= 300;
        }
        if (capital) { score -= 60; capitalized.insert(candidate); }
        scored.insert(candidate, score);
    };

    for (auto it = m_personalFrequency.constBegin(); it != m_personalFrequency.constEnd(); ++it) {
        if (!it.key().startsWith(typed)) continue;
        // A once-typed unknown word (likely a typo) is not offered back.
        if (isUserWord(it.key()) || isCore(it.key()) || (m_data && (m_data->freqFind(it.key()) || m_data->contains(it.key())))) {
            consider(it.key(), false);
        }
    }
    QHash<QString, QString> display;
    for (const QHash<QString, QString> *words : {&m_fileWords, &m_userWords}) {
        for (auto it = words->constBegin(); it != words->constEnd(); ++it) {
            if (!it.key().startsWith(typed)) continue;
            display.insert(it.key(), it.value());
            consider(it.key(), false);
        }
    }
    for (const QString &core : m_coreWords) {
        if (core.startsWith(typed)) consider(core, false);
    }
    if (m_data && !m_data->freq.empty()) {
        // Only the most frequent completions can win; pick them by rank with
        // a cheap integer pass before the full scoring.
        const auto [lo, hi] = m_data->freqRange(typed);
        std::vector<size_t> picks;
        picks.reserve(hi - lo);
        for (size_t i = lo; i < hi; ++i) picks.push_back(i);
        const size_t keep = qMin<size_t>(picks.size(), 12);
        std::partial_sort(picks.begin(), picks.begin() + keep, picks.end(),
                          [this](size_t a, size_t b) { return m_data->freq[a].rank < m_data->freq[b].rank; });
        for (size_t k = 0; k < keep; ++k) consider(m_data->freq[picks[k]].word, m_data->freq[picks[k]].capital);
    }
    if (m_data && typed.size() >= 2 && m_data->freq.empty()) {
        // Dictionary stems carry no frequency, so single-letter prefixes would
        // only produce alphabetical noise at a high scan cost.
        const auto [lo, hi] = m_data->prefixRange(typed);
        const size_t end = qMin(hi, lo + 400);
        for (size_t i = lo; i < end; ++i) consider(m_data->wordAt(i).toString(), m_data->capitalizedAt(i));
    }

    // "cant" is a word, but "can't" is what most people mean: offer it; the
    // same for a listed word with the accents left out ("tres" -> "très",
    // "esta" -> "está", "fur" -> "für"), as Gboard does.
    if (m_data) {
        for (const QString &v : apostropheVariants(typed)) {
            if (const auto *entry = m_data->freqFind(v)) consider(v, entry->capital);
        }
        if (typed.size() >= 2) {
            for (const QString &v : accentVariants(typed)) {
                if (const auto *entry = m_data->freqFind(v)) consider(v, entry->capital);
            }
        }
    }

    QList<std::pair<QString, int>> ordered(scored.constKeyValueBegin(), scored.constKeyValueEnd());
    std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
        if (a.second != b.second) return a.second > b.second;
        if (a.first.size() != b.first.size()) return a.first.size() < b.first.size();
        return a.first < b.first;
    });
    QStringList result;
    for (const auto &item : ordered) {
        if (result.size() >= limit) break;
        result.append(display.contains(item.first) ? display.value(item.first)
                      : capitalized.contains(item.first) ? capitalizeFirst(item.first) : item.first);
    }
    if (result.size() < limit && typed.size() >= 3 && !isValidWord(typed)) {
        for (const Candidate &c : correctionCandidates(typed, prev, false)) {
            if (result.size() >= limit) break;
            const QString form = dictionaryForm(c.word);
            if (!result.contains(form) && suggestible(c.word)) result.append(form);
        }
    }
    return result;
}

QString LocalLexicon::pickCorrection(const QString &word, const QString &previousWord, bool full) const
{
    const QString typed = normalize(word);
    if (typed.isEmpty()) return {};
    // Words the user has kept (typed and committed, or restored by undoing a
    // correction) are never rewritten, not even from the curated typo list.
    if (isUserWord(typed)) return {};
    // A word of another enabled language ("danke" on the English layout)
    // is that language's word, not a typo (Gboard multilingual typing).
    // Only a nearly free fix in the active language still wins below.
    // ... and a listed word Hunspell lacks ("окей", "naja", "honour") is the
    // user's word too.
    const bool otherLanguageWord = knownInCompanionLanguage(typed) || listedButNotInDictionary(typed);
    // "i" -> "I", "monday" -> "Monday": only the capital is a word.
    if (const QString capital = englishCapitalForm(typed); !capital.isEmpty() && !otherLanguageWord) return capital;
    const auto typo = m_typoMap.constFind(typed);
    if (typed.size() >= 3 && typo != m_typoMap.constEnd() && !otherLanguageWord) return typo.value();

    // Without a real dictionary there is no way to know that the typed word
    // is wrong; rewriting "knows" or "form" would corrupt valid text.
    if (!hasSystemDictionary()) return {};
    if (isValidWord(typed)) return {};
    // Two-character tokens are too ambiguous, except for a left-out
    // apostrophe ("im" -> "I'm").
    if (typed.size() < 2) return {};

    const QList<Candidate> candidates = correctionCandidates(typed, normalize(previousWord), full);
    if (candidates.isEmpty()) return {};
    const Candidate &best = candidates.first();
    if (typed.size() < 3 && best.edit != Edit::Apostrophe) return {};
    // "пять" with Russian enabled: still "п'ять" on the Ukrainian layout,
    // whose apostrophe was left out; "привет" stays Russian.
    if (otherLanguageWord && best.edit != Edit::Apostrophe && best.edit != Edit::Accent) return {};
    const int rank = correctionRank(best.word);
    const int personal = m_personalFrequency.value(best.word);
    const int margin = candidates.size() > 1 ? best.score - candidates.at(1).score : 1000;
    if (margin == 0) return {};
    // LatinIME normalises its score by length: a different letter in a short
    // word is a big change ("щас" is not a typo of "вас").
    const bool shortDistantSubstitution =
        typed.size() <= 4 && best.edit == Edit::Other && best.word.size() == typed.size();
    if (shortDistantSubstitution && personal < 2) return {};
    const bool known = rank > 0 || isCore(best.word) || personal > 0 || isUserWord(best.word);
    // A swap in three letters changes two of them: "omg" is not "mog".
    const bool confident = (best.edit == Edit::Transposition && (known || typed.size() >= 4))
        || (best.edit == Edit::Apostrophe && known)
        || (best.edit == Edit::Accent && (isCore(best.word) || (rank > 0 && rank <= 20000)))
        || isCore(best.word)
        || personal >= 2
        || (rank > 0 && rank <= 3000 && margin >= 60)
        // One wrong letter in a long word: still clearly that word.
        || (typed.size() >= 6 && rank > 0 && rank <= 20000 && margin >= 60);
    if (!confident) return {};
    return dictionaryForm(best.word);
}

QString LocalLexicon::bestCorrection(const QString &word, const QString &previousWord) const
{
    return pickCorrection(word, previousWord, true);
}

QString LocalLexicon::correctionPreview(const QString &word, const QString &previousWord) const
{
    return pickCorrection(word, previousWord, false);
}

QStringList LocalLexicon::nextWords(const QString &previousWord, int limit) const
{
    const QString prev = normalize(previousWord);
    if (prev.isEmpty() || limit <= 0) return {};
    adoptLoadedData();
    // The user's own pairs first (each use counts), then the bundled ones,
    // most common first: what was typed after a word beats the corpora.
    struct Scored { QString word; int score; };
    QVector<Scored> scored;
    for (const QString &next : learnedFollowers(prev)) scored.push_back({next, m_bigramFrequency.value(bigramKey(prev, next))});
    std::sort(scored.begin(), scored.end(), [](const Scored &a, const Scored &b) {
        if (a.score != b.score) return a.score > b.score;
        return a.word < b.word;
    });
    QStringList words;
    for (const auto &item : std::as_const(scored)) words.append(item.word);
    QSet<QString> capitalHere;              // "das Unternehmen", but "wir unternehmen"
    if (m_data) {
        const auto [lo, hi] = m_data->followerRange(prev);
        for (size_t i = lo; i < hi; ++i) {
            const QString &word = m_data->freq[m_data->followers[i].freqIndex].word;
            words.append(word);
            if (m_data->followers[i].capital) capitalHere.insert(word);
        }
    }
    QStringList result;
    for (const QString &word : std::as_const(words)) {
        if (result.size() >= limit) break;
        if (!suggestible(word)) continue;
        const QString capital = capitalHere.contains(word) ? capitalizeFirst(word) : englishCapitalForm(word);
        const QString form = capital.isEmpty() ? dictionaryForm(word) : capital;
        if (!result.contains(form, Qt::CaseInsensitive)) result.append(form);
    }
    return result;
}

QStringList LocalLexicon::phraseAfter(const QString &word, int maxWords) const
{
    constexpr int HabitCount = 3;        // typed at least three times
    constexpr double HabitShare = 0.6;   // and most of what followed the word
    QStringList phrase;
    QString current = normalize(word);
    if (current.isEmpty() || maxWords <= 0) return phrase;
    adoptLoadedData();
    QSet<QString> seen{current};
    while (phrase.size() < maxWords) {
        int total = 0, top = 0;
        QString next;
        for (const QString &candidate : learnedFollowers(current)) {
            const int n = m_bigramFrequency.value(bigramKey(current, candidate));
            total += n;
            if (n > top || (n == top && candidate < next)) { top = n; next = candidate; }
        }
        if (top < HabitCount || top < total * HabitShare || !suggestible(next) || seen.contains(next)) break;
        seen.insert(next);
        const QString capital = englishCapitalForm(next);
        phrase.append(capital.isEmpty() ? dictionaryForm(next) : capital);
        current = next;
    }
    return phrase;
}

const QStringList &LocalLexicon::learnedFollowers(const QString &previousWord) const
{
    if (!m_learnedFollowersValid) {
        m_learnedFollowers.clear();
        for (auto it = m_bigramFrequency.constBegin(); it != m_bigramFrequency.constEnd(); ++it) {
            const qsizetype at = it.key().indexOf(QChar(0x001f));
            if (at > 0) m_learnedFollowers[it.key().left(at)].append(it.key().mid(at + 1));
        }
        m_learnedFollowersValid = true;
    }
    static const QStringList none;
    const auto it = m_learnedFollowers.constFind(previousWord);
    return it == m_learnedFollowers.constEnd() ? none : *it;
}

QString LocalLexicon::decodeGlidePath(const QVector<QPointF> &path, const QHash<QChar, QPointF> &keyCentres,
                                      qreal keyWidth, const QString &previousWord) const
{
    return decodeGlidePathCandidates(path, keyCentres, keyWidth, previousWord, 1).value(0);
}

QStringList LocalLexicon::decodeGlidePathCandidates(const QVector<QPointF> &path, const QHash<QChar, QPointF> &keyCentres,
                                                    qreal keyWidth, const QString &previousWord, int limit) const
{
    if (path.size() < 2 || keyCentres.isEmpty() || keyWidth <= 0 || limit <= 0) return {};
    double length = 0.0;
    for (int i = 1; i < path.size(); ++i) length += QLineF(path.at(i - 1), path.at(i)).length();
    if (length < keyWidth * 0.5) return {};   // a wobble on one key, not a glide
    adoptLoadedData();
    const QString prev = normalize(previousWord);

    // Where the finger came down and lifted: that key, and any whose centre
    // is close enough to have been meant.
    auto keysNear = [&](const QPointF &point) {
        QSet<QChar> keys;
        QChar nearest;
        double best = std::numeric_limits<double>::infinity();
        for (auto it = keyCentres.constBegin(); it != keyCentres.constEnd(); ++it) {
            const double d = QLineF(point, it.value()).length();
            if (d < best) { best = d; nearest = it.key(); }
            if (d <= keyWidth * 0.9) keys.insert(it.key());
        }
        keys.insert(nearest);
        return keys;
    };
    const QSet<QChar> starts = keysNear(path.first());
    const QSet<QChar> ends = keysNear(path.last());

    QSet<QString> seen;
    QStringList candidates;
    auto consider = [&](const QString &word) {
        if (word.size() < 2 || seen.contains(word)) return;
        if (!starts.contains(word.front()) || !ends.contains(word.back())) return;
        if (!suggestible(word)) return;
        seen.insert(word);
        candidates.append(word);
    };
    for (auto it = m_personalFrequency.constBegin(); it != m_personalFrequency.constEnd(); ++it) {
        if (isUserWord(it.key()) || isCore(it.key()) || knownCheaply(it.key())) consider(it.key());
    }
    for (const QHash<QString, QString> *words : {&m_fileWords, &m_userWords}) {
        for (auto it = words->constBegin(); it != words->constEnd(); ++it) consider(it.key());
    }
    for (const QString &core : m_coreWords) consider(core);
    if (m_data) {
        for (const QChar start : starts) {
            if (!m_data->freq.empty()) {
                const auto [lo, hi] = m_data->freqRange(QString(start));
                for (size_t i = lo; i < hi; ++i) consider(m_data->freq[i].word);
            } else {
                const auto [lo, hi] = m_data->prefixRange(QString(start));
                for (size_t i = lo; i < hi; ++i) consider(m_data->wordAt(i).toString());
            }
        }
    }

    // SHARK2-style channels on evenly resampled paths: location (point by
    // point, in key widths), shape (position and size normalised), and how
    // close each of the word's keys comes to the finger's path.
    constexpr int Samples = 32;
    const QVector<QPointF> sampled = Glide::resample(path, Samples);
    const QVector<QPointF> dense = Glide::resample(path, 96);
    struct Reading { QString word; double score; double location; };
    QVector<Reading> readings;
    for (const QString &word : std::as_const(candidates)) {
        const QVector<QPointF> ideal = Glide::idealPath(word, keyCentres);
        if (ideal.size() < 2) continue;
        double letters = 0.0;
        double worstLetter = 0.0;
        for (const QPointF &key : ideal) {
            const double d = Glide::distanceToPath(key, dense) / keyWidth;
            letters += d;
            worstLetter = std::max(worstLetter, d);
        }
        // A key may lie a key and a half from the path: real fingers cut the
        // corners of a word ("the" passes between t, h and e rather than
        // over h), and a narrower limit dropped the word before scoring.
        if (worstLetter > 1.5) continue;
        // Nor may the word's path be far longer than the finger's: a short
        // slide from j to k is not "junk" (its keys all lie near the slide).
        double idealLength = 0.0;
        for (int i = 1; i < ideal.size(); ++i) idealLength += QLineF(ideal.at(i - 1), ideal.at(i)).length();
        if (idealLength > 1.6 * length + keyWidth) continue;
        letters /= ideal.size();
        const QVector<QPointF> idealSampled = Glide::resample(ideal, Samples);
        double location = 0.0;
        for (int i = 0; i < Samples; ++i) location += QLineF(sampled.at(i), idealSampled.at(i)).length();
        location /= Samples * keyWidth;
        if (location > 1.5) continue;
        const double shape = Glide::shapeDistance(sampled, idealSampled);
        // Weights from grid searches on synthetic glides (tools in
        // docs/ledger.md): first on exact paths of the 200 most frequent words
        // per language, then on human-like ones (key noise, corners cut,
        // smoothed) of frequent and rarer words. The letters weight was 260;
        // 180 keeps exact paths as they were and lifts cut and sloppy ones.
        // The misses left are mostly ambiguous paths ("too"/"to",
        // "dass"/"das"), which the strip offers as alternatives.
        const double score = 1000.0 - 420.0 * location - 180.0 * letters - 200.0 * shape
                           + 0.6 * priorScore(word, prev);
        readings.append({word, score, location});
    }
    std::sort(readings.begin(), readings.end(), [](const Reading &a, const Reading &b) {
        return a.score != b.score ? a.score > b.score : a.word < b.word;
    });
    // Nothing if even the best reading strays far from the path; the others
    // only while they are close to the best one.
    if (readings.isEmpty() || readings.first().location > 1.2) return {};
    QStringList result;
    for (const Reading &reading : std::as_const(readings)) {
        if (result.size() >= limit || reading.score < readings.first().score - 220.0) break;
        if (reading.location > 1.2) continue;
        const QString form = dictionaryForm(reading.word);
        if (!result.contains(form)) result.append(form);
    }
    return result;
}

QString LocalLexicon::decodeGlide(const QStringList &trace, const QString &previousWord) const
{
    if (trace.isEmpty()) return {};
    QString sequence;
    for (const QString &part : trace) {
        const QString normalized = normalize(part);
        if (!normalized.isEmpty()) sequence += normalized.front();
    }
    sequence = collapsed(sequence);
    if (sequence.size() < 2) return {};
    adoptLoadedData();
    const QString prev = normalize(previousWord);

    QSet<QString> candidates;
    auto consider = [&](const QString &word) {
        if (word.isEmpty() || word.front() != sequence.front() || word.back() != sequence.back()) return;
        if (qAbs(word.size() - sequence.size()) > qMax(3, int(sequence.size() / 2))) return;
        candidates.insert(word);
    };
    for (auto it = m_personalFrequency.constBegin(); it != m_personalFrequency.constEnd(); ++it) consider(it.key());
    for (const QString &core : m_coreWords) consider(core);
    if (m_data) {
        const auto [lo, hi] = m_data->prefixRange(sequence.left(1));
        for (size_t i = lo; i < hi; ++i) {
            const QStringView w = m_data->wordAt(i);
            if (!w.isEmpty() && w.back() == sequence.back()) consider(w.toString());
        }
    }

    QString best;
    int bestScore = -100000;
    for (const QString &word : candidates) {
        const int distance = levenshtein(collapsed(word), sequence);
        int score = 1000 - distance * 120 - qAbs(word.size() - sequence.size()) * 12;
        score += qMin(240, m_personalFrequency.value(word) * 20);
        if (isCore(word)) score += 60;
        if (!prev.isEmpty()) score += qMin(480, m_bigramFrequency.value(bigramKey(prev, word)) * 60);
        if (score > bestScore || (score == bestScore && word < best)) {
            bestScore = score;
            best = word;
        }
    }
    return bestScore >= 520 ? best : QString();
}

void LocalLexicon::loadLearning()
{
    waitForLearningWrites();                 // what was learned last is on disk
    m_personalFrequency.clear();
    m_bigramFrequency.clear();
    m_learnedFollowersValid = false;
    m_unsavedLearning = 0;
    QSettings settings;
    const QVariantMap words = settings.value(QStringLiteral("learning/%1/words").arg(m_language)).toMap();
    // Schema 2 (1.1.0): counts written by older versions counted every commit
    // (typos included), so they are capped below the promotion threshold once;
    // known typos are dropped. Ranking survives, legitimacy is re-earned.
    const QString schemaKey = QStringLiteral("learning/%1/schema").arg(m_language);
    const bool legacy = settings.value(schemaKey, 1).toInt() < 2;
    for (auto it = words.constBegin(); it != words.constEnd(); ++it) {
        const QString word = normalize(it.key());
        int count = it.value().toInt();
        if (legacy) {
            if (m_typoMap.contains(word)) continue;
            count = qMin(count, PromotionCount - 1);
        }
        if (!word.isEmpty() && count > 0) m_personalFrequency.insert(word, count);
    }
    if (legacy) {
        settings.setValue(schemaKey, 2);
        if (!words.isEmpty()) ++m_unsavedLearning;          // persist the migrated counts
    }
    m_forgotten.clear();
    for (const QString &w : settings.value(QStringLiteral("learning/%1/forgotten").arg(m_language)).toStringList()) m_forgotten.insert(w);
    m_userWords.clear();
    for (const QString &w : settings.value(QStringLiteral("dictionary/%1").arg(m_language)).toStringList()) {
        if (!normalize(w).isEmpty()) m_userWords.insert(normalize(w), w);
    }
    reloadUserDictionaryFile();
    const QVariantMap bigrams = settings.value(QStringLiteral("learning/%1/bigrams").arg(m_language)).toMap();
    for (auto it = bigrams.constBegin(); it != bigrams.constEnd(); ++it) {
        if (it.value().toInt() > 0) m_bigramFrequency.insert(it.key(), it.value().toInt());
    }
    // Older versions kept everything.
    const qsizetype loaded = m_personalFrequency.size() + m_bigramFrequency.size();
    pruneLearned(m_personalFrequency, MaxLearnedWords);
    pruneLearned(m_bigramFrequency, MaxLearnedPairs);
    if (m_personalFrequency.size() + m_bigramFrequency.size() < loaded) ++m_unsavedLearning;
}

void LocalLexicon::learnWordWithContext(const QString &word, const QString &previousWord)
{
    const QString w = normalize(word);
    if (w.size() < 2 || w.size() > 48) return;
    m_personalFrequency[w] = qMin(1000, m_personalFrequency.value(w) + 1);
    if (m_personalFrequency.value(w) >= PromotionCount) m_forgotten.remove(w);
    const QString prev = normalize(previousWord);
    if (!prev.isEmpty()) {
        int &count = m_bigramFrequency[bigramKey(prev, w)];
        if (count == 0 && m_learnedFollowersValid) m_learnedFollowers[prev].append(w);
        count = qMin(1000, count + 1);
    }
    pruneLearned(m_personalFrequency, MaxLearnedWords);
    const qsizetype pairs = m_bigramFrequency.size();
    pruneLearned(m_bigramFrequency, MaxLearnedPairs);
    if (m_bigramFrequency.size() != pairs) m_learnedFollowersValid = false;
    // Batch disk writes: one settings flush per 16 words (and on context or
    // language change / shutdown) instead of one per word.
    if (++m_unsavedLearning >= 16) flushLearning();
}

void LocalLexicon::unlearnWordWithContext(const QString &word, const QString &previousWord)
{
    const QString w = normalize(word);
    if (w.isEmpty()) return;
    if (const int count = m_personalFrequency.value(w); count > 1) m_personalFrequency[w] = count - 1;
    else m_personalFrequency.remove(w);
    const QString prev = normalize(previousWord);
    if (!prev.isEmpty()) {
        const QString key = bigramKey(prev, w);
        if (const int count = m_bigramFrequency.value(key); count > 1) {
            m_bigramFrequency[key] = count - 1;
        } else if (m_bigramFrequency.remove(key) > 0 && m_learnedFollowersValid) {
            if (auto it = m_learnedFollowers.find(prev); it != m_learnedFollowers.end()) {
                it->removeOne(w);
                if (it->isEmpty()) m_learnedFollowers.erase(it);
            }
        }
    }
    ++m_unsavedLearning;
}

void LocalLexicon::flushLearning()
{
    if (m_unsavedLearning == 0) return;
    m_unsavedLearning = 0;
    // The copies share the data until typing changes it.
    saveLearningInBackground(m_language, {m_personalFrequency, m_bigramFrequency, QStringList(m_forgotten.values())});
}

void LocalLexicon::clearLearning()
{
    m_personalFrequency.clear();
    m_bigramFrequency.clear();
    m_learnedFollowersValid = false;
    m_unsavedLearning = 0;
    waitForLearningWrites();                 // no older save may come after this
    QSettings settings;
    settings.remove(QStringLiteral("learning/%1").arg(m_language));
}

bool LocalLexicon::hasWord(const QString &word) const { return isValidWord(normalize(word)); }

int LocalLexicon::dictionarySize() const
{
    adoptLoadedData();
    return int(m_coreWords.size()) + (m_data ? int(m_data->entries.size()) : 0);
}

}
