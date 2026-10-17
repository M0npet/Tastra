// SPDX-License-Identifier: GPL-3.0-or-later
//
// Hermetic lexicon tests: dictionaries come only from tests/data, learning
// goes to QStandardPaths test locations, never to the user's real config.

#include <QtTest/QTest>

#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "core/locallexicon.h"

using Tastra::LocalLexicon;

class LexiconTest : public QObject
{
    Q_OBJECT

private:
    static void useFixtureDictionaries()
    {
        LocalLexicon::setDictionarySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/hunspell")});
        LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        LocalLexicon::setBlocklistSearchPaths({QStringLiteral(TASTRA_TEST_DATA "/blocklist")});
        LocalLexicon::setUserDictionaryFile(QStringLiteral(TASTRA_TEST_DATA "/empty/none.txt"));
    }
    static void useNoDictionaries()
    {
        LocalLexicon::setDictionarySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        LocalLexicon::setBlocklistSearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
    }
    static void useFrequencies()
    {
        LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/frequency")});
    }
    // Small en/de/uk/ru dictionaries and frequency lists for the LatinIME
    // error model (apostrophes, diacritics, length-aware confidence).
    static void useLatinImeFixtures(LocalLexicon &lexicon, const QString &language)
    {
        LocalLexicon::setDictionarySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/latinime/hunspell")});
        // "~N" in a fixture list stands for N lines that are not words, so a
        // word after it gets a realistic rank (ranks count lines).
        static QTemporaryDir frequency;
        for (const QString &name : QDir(QStringLiteral(TASTRA_TEST_DATA "/latinime/frequency")).entryList({QStringLiteral("*.txt")})) {
            QFile in(QStringLiteral(TASTRA_TEST_DATA "/latinime/frequency/") + name);
            QFile out(frequency.filePath(name));
            QVERIFY(in.open(QIODevice::ReadOnly) && out.open(QIODevice::WriteOnly | QIODevice::Truncate));
            for (const QByteArray &line : in.readAll().split('\n')) {
                if (line.startsWith('~')) out.write(QByteArray("~\n").repeated(line.mid(1).toInt()));
                else if (!line.isEmpty()) out.write(line + '\n');
            }
        }
        LocalLexicon::setFrequencySearchPaths({frequency.path()});
        LocalLexicon::setBlocklistSearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        LocalLexicon::setUserDictionaryFile(QStringLiteral(TASTRA_TEST_DATA "/empty/none.txt"));
        lexicon.setLanguage(language);
        QVERIFY(lexicon.waitForDictionary(5000));
        static const QHash<QString, QStringList> rows = {
            {QStringLiteral("en"), {QStringLiteral("qwertyuiop"), QStringLiteral("asdfghjkl"), QStringLiteral("zxcvbnm")}},
            {QStringLiteral("de"), {QStringLiteral("qwertzuiopü"), QStringLiteral("asdfghjklöä"), QStringLiteral("yxcvbnm")}},
            {QStringLiteral("uk"), {QStringLiteral("йцукенгшщзхї"), QStringLiteral("фівапролджє"), QStringLiteral("ячсмитьбю")}},
            {QStringLiteral("ru"), {QStringLiteral("йцукенгшщзхъ"), QStringLiteral("фывапролджэ"), QStringLiteral("ячсмитьбю")}},
        };
        lexicon.setKeyboardRows(rows.value(language));
    }
    static void loaded(LocalLexicon &lexicon, const QString &language)
    {
        lexicon.setLanguage(language);
        QVERIFY(lexicon.waitForDictionary(5000));
    }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("TastraTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_lexicon"));
        QSettings().clear();
        // No bundled word pairs unless a test asks for them: the data can
        // change without moving unrelated expectations.
        LocalLexicon::setBigramSearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
    }

    void init() { QSettings().clear(); }

    void constructionDoesNotLoadDictionaries()
    {
        useFixtureDictionaries();
        LocalLexicon lexicon;
        QVERIFY(!lexicon.hasSystemDictionary());
    }

    void fixtureDictionaryLoadsInBackground()
    {
        useFixtureDictionaries();
        LocalLexicon lexicon;
        loaded(lexicon, QStringLiteral("en"));
        QVERIFY(lexicon.hasSystemDictionary());
    }

    void transpositionTypoPrefersCommonWordOverAlphabeticalNeighbours()
    {
        useFixtureDictionaries();
        LocalLexicon lexicon;
        loaded(lexicon, QStringLiteral("en"));
        // Previously ties were broken alphabetically: teh -> meh.
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("teh")), QStringLiteral("the"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("adn")), QStringLiteral("and"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("thsi")), QStringLiteral("this"));
    }

    void inflectedFormsAreValidAndNeverRewritten()
    {
        useFixtureDictionaries();
        LocalLexicon lexicon;
        loaded(lexicon, QStringLiteral("en"));
        for (const QString &word : {QStringLiteral("wants"), QStringLiteral("walked"), QStringLiteral("knows"),
                                    QStringLiteral("times"), QStringLiteral("typed"), QStringLiteral("form"),
                                    QStringLiteral("meh")}) {
            QVERIFY2(lexicon.hasWord(word), qPrintable(word));
            QVERIFY2(lexicon.bestCorrection(word).isEmpty(), qPrintable(word));
        }
    }

    void affixAwareCorrectionOfTransposedInflection()
    {
        useFixtureDictionaries();
        LocalLexicon lexicon;
        loaded(lexicon, QStringLiteral("en"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("wnats")), QStringLiteral("wants"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("helo")), QStringLiteral("hello"));
    }

    void ambiguousTypoIsSuggestedButNotAutocorrected()
    {
        useFixtureDictionaries();
        LocalLexicon lexicon;
        loaded(lexicon, QStringLiteral("en"));
        // "tex" is one substitution away from several valid rare words; no
        // candidate is common or a transposition, so do not rewrite.
        QVERIFY(lexicon.bestCorrection(QStringLiteral("tex")).isEmpty());
        QVERIFY(!lexicon.suggestions(QStringLiteral("tex"), {}).isEmpty());
    }

    void withoutDictionaryOnlyCuratedTyposAreAutocorrected()
    {
        useNoDictionaries();
        LocalLexicon lexicon;
        loaded(lexicon, QStringLiteral("en"));
        QVERIFY(!lexicon.hasSystemDictionary());
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("teh")), QStringLiteral("the"));
        // No validity oracle: valid-but-unknown words must be left alone.
        QVERIFY(lexicon.bestCorrection(QStringLiteral("knows")).isEmpty());
        QVERIFY(lexicon.bestCorrection(QStringLiteral("wants")).isEmpty());
        QVERIFY(lexicon.bestCorrection(QStringLiteral("form")).isEmpty());
    }

    void acceptedWordsBeatTheCuratedTypoList()
    {
        useNoDictionaries();
        LocalLexicon lexicon;
        loaded(lexicon, QStringLiteral("en"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("teh")), QStringLiteral("the"));
        lexicon.promoteWord(QStringLiteral("teh"));            // e.g. the user undid the correction
        QVERIFY(lexicon.bestCorrection(QStringLiteral("teh")).isEmpty());
    }

    void oneAccidentalCommitDoesNotLegitimiseATypo()
    {
        useFixtureDictionaries();
        LocalLexicon lexicon;
        loaded(lexicon, QStringLiteral("en"));
        lexicon.learnWordWithContext(QStringLiteral("teh"), {});       // slipped through once
        lexicon.learnWordWithContext(QStringLiteral("zorgle"), {});
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("teh")), QStringLiteral("the"));
        QVERIFY(!lexicon.hasWord(QStringLiteral("zorgle")));
        QVERIFY(!lexicon.suggestions(QStringLiteral("zor"), {}, 5).contains(QStringLiteral("zorgle")));
        // Used three times: now it is the user's word.
        lexicon.learnWordWithContext(QStringLiteral("zorgle"), {});
        lexicon.learnWordWithContext(QStringLiteral("zorgle"), {});
        QVERIFY(lexicon.hasWord(QStringLiteral("zorgle")));
        QVERIFY(lexicon.suggestions(QStringLiteral("zor"), {}, 5).contains(QStringLiteral("zorgle")));
    }

    void personalDictionaryKeepsCaseAndPersists()
    {
        useFixtureDictionaries();
        LocalLexicon::setUserDictionaryFile(QStringLiteral(TASTRA_TEST_DATA "/empty/none.txt"));
        {
            LocalLexicon lexicon;
            loaded(lexicon, QStringLiteral("en"));
            QVERIFY(lexicon.addUserWord(QStringLiteral("Zelenograd")));
            QVERIFY(!lexicon.addUserWord(QStringLiteral(" ")));             // rejected
            QCOMPARE(lexicon.userWords(), QStringList({QStringLiteral("Zelenograd")}));
        }
        LocalLexicon lexicon;
        loaded(lexicon, QStringLiteral("en"));
        QCOMPARE(lexicon.suggestions(QStringLiteral("zel"), {}).value(0), QStringLiteral("Zelenograd"));
        QVERIFY(lexicon.bestCorrection(QStringLiteral("zelenograd")).isEmpty());
        lexicon.removeUserWord(QStringLiteral("zelenograd"));
        QVERIFY(lexicon.userWords().isEmpty());
        QVERIFY(!lexicon.suggestions(QStringLiteral("zel"), {}, 5).contains(QStringLiteral("Zelenograd")));
    }

    void dictionaryFileAddsWordsForAllLanguages()
    {
        useFixtureDictionaries();
        LocalLexicon::setUserDictionaryFile(QStringLiteral(TASTRA_TEST_DATA "/userdict/dictionary.txt"));
        LocalLexicon lexicon;
        loaded(lexicon, QStringLiteral("en"));
        QCOMPARE(lexicon.suggestions(QStringLiteral("minis"), {}).value(0), QStringLiteral("Minisforum"));
        loaded(lexicon, QStringLiteral("ru"));
        QVERIFY(lexicon.hasWord(QStringLiteral("kyiv")));
        LocalLexicon::setUserDictionaryFile(QStringLiteral(TASTRA_TEST_DATA "/empty/none.txt"));
    }

    void forgettingAlsoRemovesFromThePersonalDictionary()
    {
        useFixtureDictionaries();
        LocalLexicon::setUserDictionaryFile(QStringLiteral(TASTRA_TEST_DATA "/empty/none.txt"));
        LocalLexicon lexicon;
        loaded(lexicon, QStringLiteral("en"));
        lexicon.addUserWord(QStringLiteral("Quokka"));
        lexicon.forgetWord(QStringLiteral("quokka"));
        QVERIFY(lexicon.userWords().isEmpty());
        QVERIFY(!lexicon.suggestions(QStringLiteral("quo"), {}, 5).contains(QStringLiteral("Quokka")));
        lexicon.addUserWord(QStringLiteral("Quokka"));          // explicit add brings it back
        QVERIFY(lexicon.suggestions(QStringLiteral("quo"), {}, 5).contains(QStringLiteral("Quokka")));
    }


    void frequencyRanksCompletionsFromTheFirstLetter()
    {
        useFixtureDictionaries();
        useFrequencies();
        LocalLexicon lexicon;
        loaded(lexicon, QStringLiteral("en"));
        // "help" is more frequent than "hello" in the fixture list.
        QCOMPARE(lexicon.suggestions(QStringLiteral("hel"), {}).value(0), QStringLiteral("help"));
        // Single letters now give real completions, most frequent first.
        QCOMPARE(lexicon.suggestions(QStringLiteral("t"), {}).value(0), QStringLiteral("the"));
        // Inflected forms come from the list; junk not in the dictionary is dropped.
        QVERIFY(lexicon.suggestions(QStringLiteral("wan"), {}).contains(QStringLiteral("wants")));
        QVERIFY(!lexicon.suggestions(QStringLiteral("qz"), {}).contains(QStringLiteral("qzxjunk")));
    }

    void germanNounsFromFrequencyKeepTheirCapital()
    {
        useFixtureDictionaries();
        useFrequencies();
        LocalLexicon lexicon;
        loaded(lexicon, QStringLiteral("de"));
        QCOMPARE(lexicon.suggestions(QStringLiteral("ha"), {}).value(0), QStringLiteral("Haus"));
        QVERIFY(lexicon.suggestions(QStringLiteral("hau"), {}).contains(QStringLiteral("Hause")));
    }

    void offensiveWordsAreNeverSuggestedButStayTypable()
    {
        useFixtureDictionaries();
        LocalLexicon lexicon;
        loaded(lexicon, QStringLiteral("en"));
        QVERIFY(!lexicon.suggestions(QStringLiteral("hel"), {}, 5).contains(QStringLiteral("hell")));
        // Exactly what the user typed is still offered.
        QVERIFY(lexicon.suggestions(QStringLiteral("hell"), {}, 5).contains(QStringLiteral("hell")));
        // Never an autocorrection target ("hlel" is a transposition of "hell").
        QVERIFY(lexicon.bestCorrection(QStringLiteral("hlel")).isEmpty());
        lexicon.setBlockOffensive(false);
        QVERIFY(lexicon.suggestions(QStringLiteral("hel"), {}, 5).contains(QStringLiteral("hell")));
    }

    void forgettingASuggestionRemovesItForGood()
    {
        useFixtureDictionaries();
        {
            LocalLexicon lexicon;
            loaded(lexicon, QStringLiteral("en"));
            for (int i = 0; i < 3; ++i) lexicon.learnWordWithContext(QStringLiteral("helsinki"), {});
            QVERIFY(lexicon.suggestions(QStringLiteral("hel"), {}).contains(QStringLiteral("helsinki")));
            lexicon.forgetWord(QStringLiteral("helsinki"));
            lexicon.forgetWord(QStringLiteral("hello"));            // a dictionary word
            QVERIFY(!lexicon.suggestions(QStringLiteral("hel"), {}, 5).contains(QStringLiteral("helsinki")));
            QVERIFY(!lexicon.suggestions(QStringLiteral("hel"), {}, 5).contains(QStringLiteral("hello")));
        }
        LocalLexicon reloaded;
        loaded(reloaded, QStringLiteral("en"));
        QVERIFY(!reloaded.suggestions(QStringLiteral("hel"), {}, 5).contains(QStringLiteral("hello")));
        QVERIFY(!reloaded.suggestions(QStringLiteral("hel"), {}, 5).contains(QStringLiteral("helsinki")));
    }

    void neighbouringKeysMakeTheCheapestCorrection()
    {
        // AOSP LatinIME: substituting a neighbouring key costs 0.0694, a
        // distant one 0.3806. "tge": g is next to h on QWERTY, far from i/o.
        useFixtureDictionaries();
        LocalLexicon lexicon;
        loaded(lexicon, QStringLiteral("en"));
        lexicon.setKeyboardRows({QStringLiteral("qwertyuiop"), QStringLiteral("asdfghjkl"), QStringLiteral("zxcvbnm")});
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("tge")), QStringLiteral("the"));
        QCOMPARE(lexicon.suggestions(QStringLiteral("tge"), {}, 3).value(0), QStringLiteral("the"));
        // Geometry decides between equally rare words: p is next to o, far
        // from i, so "toe" outranks "tie" (alphabetical order would not).
        const QStringList list = lexicon.suggestions(QStringLiteral("tpe"), {}, 6);
        QVERIFY2(list.contains(QStringLiteral("toe")) && list.contains(QStringLiteral("tie")), qPrintable(list.join(',')));
        QVERIFY2(list.indexOf(QStringLiteral("toe")) < list.indexOf(QStringLiteral("tie")), qPrintable(list.join(',')));
        lexicon.setKeyboardRows({});                               // no geometry -> no preference
        const QStringList flat = lexicon.suggestions(QStringLiteral("tpe"), {}, 6);
        QVERIFY2(flat.indexOf(QStringLiteral("tie")) < flat.indexOf(QStringLiteral("toe")), qPrintable(flat.join(',')));
    }

    void latinImeThresholdGatesAutocorrection()
    {
        useFixtureDictionaries();
        useFrequencies();
        LocalLexicon lexicon;
        loaded(lexicon, QStringLiteral("en"));
        lexicon.setKeyboardRows({QStringLiteral("qwertyuiop"), QStringLiteral("asdfghjkl"), QStringLiteral("zxcvbnm")});
        // Clear transposition of a frequent word: corrected.
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("wnats")), QStringLiteral("wants"));
        // Distant substitution towards rare words: below the modest threshold.
        QVERIFY(lexicon.bestCorrection(QStringLiteral("tex")).isEmpty());
    }

    void perKeystrokePreviewIsCheapButSpaceStillUsesHunspell()
    {
        useFixtureDictionaries();
        useFrequencies();
        LocalLexicon lexicon;
        loaded(lexicon, QStringLiteral("en"));
        // Preview (per keystroke): frequency list / stems / learned / core only.
        QCOMPARE(lexicon.correctionPreview(QStringLiteral("teh")), QStringLiteral("the"));
        QCOMPARE(lexicon.correctionPreview(QStringLiteral("wnats")), QStringLiteral("wants"));   // in the list
        // An inflection only Hunspell knows is found at Space, not in the preview.
        QVERIFY(lexicon.correctionPreview(QStringLiteral("hleped")).isEmpty());
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("hleped")), QStringLiteral("helped"));
    }

    void legacyLearningDoesNotPromoteOldTypos()
    {
        // Data written by <= 1.0.1, where every commit counted as "learned":
        // typos typed many times during testing must not become valid words.
        useFixtureDictionaries();
        {
            QSettings settings;
            QVariantMap words;
            words.insert(QStringLiteral("teh"), 7);
            words.insert(QStringLiteral("hello"), 5);
            settings.setValue(QStringLiteral("learning/en/words"), words);
        }
        LocalLexicon lexicon;
        loaded(lexicon, QStringLiteral("en"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("teh")), QStringLiteral("the"));
        QVERIFY(lexicon.suggestions(QStringLiteral("hel"), {}).contains(QStringLiteral("hello")));
        // The migration runs once; new explicit signals still work afterwards.
        lexicon.promoteWord(QStringLiteral("teh"));
        LocalLexicon reloaded;
        loaded(reloaded, QStringLiteral("en"));
        QVERIFY(reloaded.bestCorrection(QStringLiteral("teh")).isEmpty());
    }

    void wherTheFingerLandedDecidesBetweenNeighbours()
    {
        // "bst": s touches both a (left) and e (up-right). The touch point
        // inside the s key says which slip it was (LatinIME uses distances
        // from the touch point to key centres the same way).
        useFixtureDictionaries();
        LocalLexicon lexicon;
        loaded(lexicon, QStringLiteral("en"));
        lexicon.setKeyboardRows({QStringLiteral("qwertyuiop"), QStringLiteral("asdfghjkl"), QStringLiteral("zxcvbnm")});
        lexicon.setTouchOffsets({{}, QPointF(-0.40, 0.05), {}});           // left edge of s
        QCOMPARE(lexicon.suggestions(QStringLiteral("bst"), {}).value(0), QStringLiteral("bat"));
        lexicon.setTouchOffsets({{}, QPointF(0.30, -0.42), {}});           // top-right edge of s
        QCOMPARE(lexicon.suggestions(QStringLiteral("bst"), {}).value(0), QStringLiteral("bet"));
        lexicon.setTouchOffsets({});                                       // unknown: old behaviour
        QCOMPARE(lexicon.suggestions(QStringLiteral("bst"), {}).mid(0, 2).size(), 2);
    }

    void prefixCompletionsUseDictionaryAndCoreWords()
    {
        useFixtureDictionaries();
        LocalLexicon lexicon;
        loaded(lexicon, QStringLiteral("en"));
        QVERIFY(lexicon.suggestions(QStringLiteral("hel"), {}).contains(QStringLiteral("hello")));
        // Acronyms and comment lines are not completion candidates.
        QVERIFY(!lexicon.suggestions(QStringLiteral("na"), {}).contains(QStringLiteral("nasa")));
        QVERIFY(!lexicon.suggestions(QStringLiteral("ind"), {}).contains(QStringLiteral("indented")));
    }

    void learnedWordsRankFirstAndSurviveReload()
    {
        useFixtureDictionaries();
        {
            LocalLexicon lexicon;
            loaded(lexicon, QStringLiteral("en"));
            for (int i = 0; i < 3; ++i) lexicon.learnWordWithContext(QStringLiteral("helsinki"), {});
            lexicon.flushLearning();
        }
        LocalLexicon lexicon;
        loaded(lexicon, QStringLiteral("en"));
        QCOMPARE(lexicon.suggestions(QStringLiteral("hel"), {}).value(0), QStringLiteral("helsinki"));
    }

    void aSaveInTheBackgroundIsOnDiskForTheNextLoad()
    {
        // Saving runs off the typing thread; whoever loads next waits for it.
        useFixtureDictionaries();
        LocalLexicon first;
        loaded(first, QStringLiteral("en"));
        for (int i = 0; i < 16; ++i) first.learnWordWithContext(QStringLiteral("helsinki"), {});   // the 16th saves
        LocalLexicon second;
        loaded(second, QStringLiteral("en"));
        QCOMPARE(second.suggestions(QStringLiteral("hel"), {}).value(0), QStringLiteral("helsinki"));
    }

    void learnedWordsAreBoundedLikeLatinImeUserHistory()
    {
        // A word typed once among more than LatinIME keeps goes first; words
        // typed often stay. An older version's unbounded list is cut on load.
        auto name = [](int i) {
            QString w = QStringLiteral("zq");
            for (int k = 0; k < 4; ++k, i /= 26) w += QChar(u'a' + i % 26);
            return w;
        };
        const int rare = int(LocalLexicon::MaxLearnedWords * 6 / 5) + 100;
        QVariantMap words;
        for (int i = 0; i < rare; ++i) words.insert(name(i), 1);
        for (int i = rare; i < rare + 50; ++i) words.insert(name(i), 5);
        words.insert(QStringLiteral("helsinki"), 3);
        QVariantMap pairs;
        for (int i = 0; i < int(LocalLexicon::MaxLearnedPairs * 6 / 5); ++i)
            pairs.insert(QStringLiteral("the") + QChar(0x001f) + name(i), i < 10 ? 7 : 1);
        {
            QSettings settings;
            settings.setValue(QStringLiteral("learning/en/schema"), 2);
            settings.setValue(QStringLiteral("learning/en/words"), words);
            settings.setValue(QStringLiteral("learning/en/bigrams"), pairs);
        }
        useFixtureDictionaries();
        {
            LocalLexicon lexicon;
            loaded(lexicon, QStringLiteral("en"));
            QCOMPARE(lexicon.suggestions(QStringLiteral("hel"), {}).value(0), QStringLiteral("helsinki"));
            // Learning goes on: new words fill the room up to the limit again.
            for (int i = 0; i < 20; ++i) lexicon.learnWordWithContext(QStringLiteral("helsinki"), QStringLiteral("the"));
        }
        QSettings settings;
        const QVariantMap saved = settings.value(QStringLiteral("learning/en/words")).toMap();
        QVERIFY(saved.size() <= LocalLexicon::MaxLearnedWords + 1);
        QVERIFY(saved.size() >= LocalLexicon::MaxLearnedWords);
        for (int i = rare; i < rare + 50; ++i) QCOMPARE(saved.value(name(i)).toInt(), 5);
        QCOMPARE(saved.value(QStringLiteral("helsinki")).toInt(), 23);
        const QVariantMap savedPairs = settings.value(QStringLiteral("learning/en/bigrams")).toMap();
        QVERIFY(savedPairs.size() <= LocalLexicon::MaxLearnedPairs + 1);
        for (int i = 0; i < 10; ++i) QCOMPARE(savedPairs.value(QStringLiteral("the") + QChar(0x001f) + name(i)).toInt(), 7);
        QCOMPARE(savedPairs.value(QStringLiteral("the") + QChar(0x001f) + QStringLiteral("helsinki")).toInt(), 20);
    }

    void switchingLanguageKeepsOnlyOneDictionary()
    {
        useFixtureDictionaries();
        LocalLexicon lexicon;
        loaded(lexicon, QStringLiteral("en"));
        QVERIFY(lexicon.hasSystemDictionary());
        loaded(lexicon, QStringLiteral("ru"));   // fixture has no ru_RU
        QVERIFY(!lexicon.hasSystemDictionary());
        QCOMPARE(lexicon.language(), QStringLiteral("ru"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("првиет")), QStringLiteral("привет"));
    }

    void omittedApostropheIsAnAlmostFreeEdit()
    {
        // LatinIME treats a left-out apostrophe as an intentional omission
        // (nearly free), so "dont" is "don't", not a distant real word.
        LocalLexicon lexicon;
        useLatinImeFixtures(lexicon, QStringLiteral("en"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("dont")), QStringLiteral("don't"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("isnt")), QStringLiteral("isn't"));     // not "inst"
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("thats")), QStringLiteral("that's"));   // not "that"
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("youre")), QStringLiteral("you're"));   // not "your"
        // The dictionary's own spelling, capital included.
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("ive")), QStringLiteral("I've"));       // not "vie"
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("im")), QStringLiteral("I'm"));         // two letters
        QCOMPARE(lexicon.correctionPreview(QStringLiteral("dont")), QStringLiteral("don't"));  // cheap path too
        // A real word stays, but the strip offers the contraction.
        QVERIFY(lexicon.bestCorrection(QStringLiteral("cant")).isEmpty());
        const QStringList strip = lexicon.suggestions(QStringLiteral("cant"), {}, 3);
        QVERIFY2(strip.contains(QStringLiteral("can't")), qPrintable(strip.join(',')));
    }

    void englishCapitalOnlyWordsAreCapitalised()
    {
        LocalLexicon lexicon;
        useLatinImeFixtures(lexicon, QStringLiteral("en"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("i")), QStringLiteral("I"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("monday")), QStringLiteral("Monday"));
        QVERIFY(lexicon.bestCorrection(QStringLiteral("me")).isEmpty());
        QVERIFY(lexicon.bestCorrection(QStringLiteral("it")).isEmpty());
        QVERIFY(lexicon.bestCorrection(QStringLiteral("the")).isEmpty());
    }

    void ukrainianApostropheIsRestored()
    {
        LocalLexicon lexicon;
        useLatinImeFixtures(lexicon, QStringLiteral("uk"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("пять")), QStringLiteral("п'ять"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("мясо")), QStringLiteral("м'ясо"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("звязок")), QStringLiteral("зв'язок"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("обєкт")), QStringLiteral("об'єкт"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("память")), QStringLiteral("пам'ять"));
        QCOMPARE(lexicon.correctionPreview(QStringLiteral("пять")), QStringLiteral("п'ять"));
        // г typed for ґ (ґ sits behind г on the keyboard).
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("ганок")), QStringLiteral("ґанок"));
        // A typographic apostrophe is the same word.
        QVERIFY(lexicon.hasWord(QStringLiteral("пʼять")));
    }

    void germanUmlautsDigraphsAndSharpS()
    {
        // LatinIME: a base letter matches its accented forms almost for free
        // (u ~ ü), and German digraphs ue/oe/ae stand for ü/ö/ä.
        LocalLexicon lexicon;
        useLatinImeFixtures(lexicon, QStringLiteral("de"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("uber")), QStringLiteral("über"));      // not "aber"
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("ueber")), QStringLiteral("über"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("fur")), QStringLiteral("für"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("naturlich")), QStringLiteral("natürlich"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("grosse")), QStringLiteral("große"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("strasse")), QStringLiteral("Straße"));  // a noun
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("grusse")), QStringLiteral("Grüße"));    // ü and ß at once
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("hasu")), QStringLiteral("Haus"));       // nouns keep their capital
        QVERIFY(lexicon.bestCorrection(QStringLiteral("schon")).isEmpty());                    // a word in its own right
    }

    void confidenceGrowsWithWordLength()
    {
        // LatinIME normalises the score by length: one wrong letter in a long
        // word is a confident correction, a different letter in a three-letter
        // word is not ("щас" is slang, not a typo of "вас").
        LocalLexicon lexicon;
        useLatinImeFixtures(lexicon, QStringLiteral("ru"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("пожалуйсто")), QStringLiteral("пожалуйста"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("обьект")), QStringLiteral("объект"));
        QVERIFY(lexicon.bestCorrection(QStringLiteral("щас")).isEmpty());
        QVERIFY(lexicon.bestCorrection(QStringLiteral("еще")).isEmpty());
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("првиет")), QStringLiteral("привет"));   // transposition still
    }

    void aWordMissingFromTheFrequencyListIsRare()
    {
        // Hunspell knows "hoers", but the frequency list does not: it is
        // rarer than the list's last word, so the swapped letters of "ohers"
        // no longer outweigh the left-out "t" of a common word.
        LocalLexicon lexicon;
        useLatinImeFixtures(lexicon, QStringLiteral("en"));
        QVERIFY(lexicon.hasWord(QStringLiteral("hoers")));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("ohers")), QStringLiteral("others"));
        // A swap into an unlisted word with no listed rival is still made.
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("hoesr")), QStringLiteral("hoers"));
    }

    void severalNeighbouringKeysInOneWord()
    {
        // LatinIME's proximity search: a sloppy tap can land on the next key
        // more than once per word ("otjerd": j for h, d for s).
        LocalLexicon lexicon;
        useLatinImeFixtures(lexicon, QStringLiteral("en"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("otjerd")), QStringLiteral("others"));
        QVERIFY(lexicon.suggestions(QStringLiteral("otjerd"), QString(), 3).contains(QStringLiteral("others")));
        // Two of three letters is too much ("rhw" is not "the").
        QVERIFY(lexicon.bestCorrection(QStringLiteral("rhw")).isEmpty());

        LocalLexicon russian;
        useLatinImeFixtures(russian, QStringLiteral("ru"));
        QCOMPARE(russian.bestCorrection(QStringLiteral("приаеь")), QStringLiteral("привет"));
    }

    void bundledUkrainianListHasNoRussianWords()
    {
        // Ukrainian subtitles are full of Russian; "что" and "как" were in
        // the top 40 of the list and were offered while typing Ukrainian
        // (tools/drop-foreign-words.py).
        QFile file(QStringLiteral(":/tastra/frequency/uk.txt"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QStringList words = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const QSet<QString> list(words.cbegin(), words.cend());
        for (const char *russian : {"что", "как", "это", "только", "конечно", "меня", "мне", "спасибо", "хорошо", "сейчас"})
            QVERIFY2(!list.contains(QString::fromUtf8(russian)), russian);
        for (const char *ukrainian : {"що", "як", "це", "тільки", "звичайно", "мене", "мені", "дякую", "добре", "зараз", "п'ять"})
            QVERIFY2(list.contains(QString::fromUtf8(ukrainian)), ukrainian);
        QVERIFY(words.indexOf(QStringLiteral("як")) < 30);
    }

    void languagesAddedLaterCorrectWithTheirOwnWords()
    {
        // 0.7.19 languages, with the bundled lists only (no Hunspell here):
        // accents and elisions are nearly free edits, no English word is
        // offered, and the word pairs predict.
        LocalLexicon::setDictionarySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        LocalLexicon::setFrequencySearchPaths({QStringLiteral(":/tastra/frequency")});
        LocalLexicon::setBlocklistSearchPaths({QStringLiteral(":/tastra/blocklist")});
        LocalLexicon::setUserDictionaryFile(QStringLiteral(TASTRA_TEST_DATA "/empty/none.txt"));
        LocalLexicon::setBigramSearchPaths({QStringLiteral(":/tastra/bigrams")});
        const struct { const char *language, *typed, *expected; } corrections[] = {
            {"es", "cancion", "canción"}, {"es", "tambien", "también"}, {"es", "senor", "señor"},
            {"fr", "cest", "c'est"}, {"fr", "jai", "j'ai"}, {"fr", "tres", "très"}, {"fr", "deja", "déjà"},
            {"it", "perche", "perché"}, {"it", "citta", "città"},
            {"pt", "nao", "não"}, {"pt", "voce", "você"},
            {"pl", "dzien", "dzień"}, {"pl", "prosze", "proszę"},
            {"nl", "cafe", "café"},
        };
        QStringList failures;
        for (const auto &c : corrections) {
            LocalLexicon lexicon;
            lexicon.setLanguage(QString::fromUtf8(c.language));
            QVERIFY(lexicon.waitForDictionary(10000));
            const QStringList got = lexicon.suggestions(QString::fromUtf8(c.typed), {}, 3);
            if (!got.contains(QString::fromUtf8(c.expected)))
                failures << QString::fromUtf8(c.language) + QLatin1Char(':') + QString::fromUtf8(c.typed) + QLatin1String("->") + got.join('|');
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QStringLiteral(", "))));
        const struct { const char *language, *typed; } keep[] = {
            {"es", "hola"}, {"es", "esta"}, {"fr", "est"}, {"it", "ciao"}, {"pt", "obrigado"}, {"pl", "jest"}, {"nl", "zon"},
        };
        for (const auto &k : keep) {
            LocalLexicon lexicon;
            lexicon.setLanguage(QString::fromUtf8(k.language));
            QVERIFY(lexicon.waitForDictionary(10000));
            QVERIFY2(lexicon.bestCorrection(QString::fromUtf8(k.typed)).isEmpty(), k.typed);
        }
        LocalLexicon spanish;
        spanish.setLanguage(QStringLiteral("es"));
        QVERIFY(spanish.waitForDictionary(10000));
        QVERIFY(spanish.suggestions(QStringLiteral("graci"), {}, 3).contains(QStringLiteral("gracias")));
        QVERIFY(!spanish.suggestions(QStringLiteral("th"), {}, 3).contains(QStringLiteral("the")));   // no English
        LocalLexicon french;
        french.setLanguage(QStringLiteral("fr"));
        QVERIFY(french.waitForDictionary(10000));
        QVERIFY2(french.suggestions(QStringLiteral("aujour"), {}, 3).contains(QStringLiteral("aujourd'hui")),
                 qPrintable(french.suggestions(QStringLiteral("aujour"), {}, 3).join(',')));
        QVERIFY2(french.nextWords(QStringLiteral("je"), 3).size() == 3, qPrintable(french.nextWords(QStringLiteral("je"), 3).join(',')));
        LocalLexicon::setBigramSearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
    }

    void languagesAddedLaterAutocorrectWithHunspell()
    {
        // With the language's Hunspell dictionary (as on the device), Space
        // corrects the accents and elisions. Needs the dictionaries, e.g. the
        // LibreOffice ones in TASTRA_HUNSPELL_DIR; skipped without.
        const QString dir = qEnvironmentVariable("TASTRA_HUNSPELL_DIR");
        if (dir.isEmpty()) QSKIP("TASTRA_HUNSPELL_DIR not set");
        LocalLexicon::setDictionarySearchPaths({dir});
        LocalLexicon::setFrequencySearchPaths({QStringLiteral(":/tastra/frequency")});
        LocalLexicon::setBlocklistSearchPaths({QStringLiteral(":/tastra/blocklist")});
        LocalLexicon::setUserDictionaryFile(QStringLiteral(TASTRA_TEST_DATA "/empty/none.txt"));
        const struct { const char *language, *typed, *expected; } cases[] = {
            {"es", "cancion", "canción"}, {"es", "tambien", "también"}, {"es", "graicas", "gracias"},
            {"es", "esta", ""}, {"es", "hola", ""},
            {"it", "perche", "perché"}, {"it", "citta", "città"}, {"it", "grazei", "grazie"}, {"it", "ciao", ""},
            {"pt", "nao", "não"}, {"pt", "voce", "você"}, {"pt", "obrigdo", "obrigado"}, {"pt", "esta", ""},
            {"pl", "dzien", "dzień"}, {"pl", "prosze", "proszę"}, {"pl", "dziekuje", "dziękuję"}, {"pl", "jest", ""},
            {"nl", "dnak", "dank"}, {"nl", "ideeen", "ideeën"}, {"nl", "hallo", ""},
        };
        QStringList failures;
        for (const auto &c : cases) {
            LocalLexicon lexicon;
            lexicon.setLanguage(QString::fromUtf8(c.language));
            QVERIFY(lexicon.waitForDictionary(20000));
            if (!lexicon.hasSystemDictionary()) QSKIP(qPrintable(QStringLiteral("no Hunspell for ") + QString::fromUtf8(c.language)));
            const QString got = lexicon.bestCorrection(QString::fromUtf8(c.typed));
            if (got != QString::fromUtf8(c.expected))
                failures << QString::fromUtf8(c.language) + QLatin1Char(':') + QString::fromUtf8(c.typed) + QLatin1String("->") + got;
        }
        LocalLexicon::setDictionarySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QStringLiteral(", "))));
    }

    void bundledWordPairsPredictTheNextWord()
    {
        // Gboard ships a language model: "I" is followed by "have" and "am"
        // before anything was learned. The pairs come from CC BY-SA / CC BY
        // corpora (tools/build-bigrams.py); what the user typed comes first.
        LocalLexicon::setDictionarySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        LocalLexicon::setFrequencySearchPaths({QStringLiteral(":/tastra/frequency")});
        LocalLexicon::setBlocklistSearchPaths({QStringLiteral(":/tastra/blocklist")});
        LocalLexicon::setUserDictionaryFile(QStringLiteral(TASTRA_TEST_DATA "/empty/none.txt"));
        LocalLexicon::setBigramSearchPaths({QStringLiteral(":/tastra/bigrams")});
        const struct { const char *language, *previous, *next; } expected[] = {
            {"en", "i", "have"}, {"en", "i", "am"}, {"en", "thank", "you"}, {"de", "ich", "bin"},
            {"de", "guten", "Tag"},                       // a German noun keeps its capital
            {"ru", "я", "не"}, {"ru", "потому", "что"}, {"uk", "я", "не"}, {"uk", "тому", "що"},
        };
        for (const auto &e : expected) {
            LocalLexicon lexicon;
            lexicon.setLanguage(QString::fromUtf8(e.language));
            QVERIFY(lexicon.waitForDictionary(10000));
            const QStringList next = lexicon.nextWords(QString::fromUtf8(e.previous), 3);
            QVERIFY2(next.contains(QString::fromUtf8(e.next)), qPrintable(QString::fromUtf8(e.previous) + QLatin1String(": ") + next.join(QLatin1Char(','))));
        }
        LocalLexicon lexicon;
        lexicon.setLanguage(QStringLiteral("en"));
        QVERIFY(lexicon.waitForDictionary(10000));
        QVERIFY(lexicon.nextWords(QString(), 3).isEmpty());
        lexicon.learnWordWithContext(QStringLiteral("love"), QStringLiteral("i"));
        QCOMPARE(lexicon.nextWords(QStringLiteral("i"), 3).value(0), QStringLiteral("love"));
        lexicon.forgetWord(QStringLiteral("have"));
        QVERIFY(!lexicon.nextWords(QStringLiteral("i"), 3).contains(QStringLiteral("have")));
        QCOMPARE(lexicon.nextWords(QStringLiteral("i"), 3).size(), 3);
        LocalLexicon::setBigramSearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
    }

    void learnedNextWordsFollowLearningAndForgetting()
    {
        // The learned pairs are looked up by previous word; the lookup has to
        // follow every change to them.
        useFixtureDictionaries();
        LocalLexicon lexicon;
        loaded(lexicon, QStringLiteral("en"));
        QVERIFY(lexicon.nextWords(QStringLiteral("dear"), 3).isEmpty());
        lexicon.learnWordWithContext(QStringLiteral("john"), QStringLiteral("dear"));
        QCOMPARE(lexicon.nextWords(QStringLiteral("dear"), 3), QStringList{QStringLiteral("john")});
        lexicon.learnWordWithContext(QStringLiteral("anna"), QStringLiteral("dear"));   // after a lookup
        lexicon.learnWordWithContext(QStringLiteral("anna"), QStringLiteral("dear"));
        QCOMPARE(lexicon.nextWords(QStringLiteral("dear"), 3), (QStringList{QStringLiteral("anna"), QStringLiteral("john")}));
        lexicon.unlearnWordWithContext(QStringLiteral("john"), QStringLiteral("dear"));
        QCOMPARE(lexicon.nextWords(QStringLiteral("dear"), 3), QStringList{QStringLiteral("anna")});
        lexicon.learnWordWithContext(QStringLiteral("john"), QStringLiteral("dear"));
        lexicon.forgetWord(QStringLiteral("anna"));
        QCOMPARE(lexicon.nextWords(QStringLiteral("dear"), 3), QStringList{QStringLiteral("john")});
        lexicon.clearLearning();
        QVERIFY(lexicon.nextWords(QStringLiteral("dear"), 3).isEmpty());
    }

    void germanNounTypedLowercaseIsOfferedNotForced()
    {
        // As Gboard does (checked by the user on a phone): "haus" + Space
        // stays "haus", but "Haus" is offered first in the strip.
        LocalLexicon lexicon;
        useLatinImeFixtures(lexicon, QStringLiteral("de"));
        QVERIFY(lexicon.bestCorrection(QStringLiteral("haus")).isEmpty());
        QCOMPARE(lexicon.suggestions(QStringLiteral("haus"), QString(), 3).value(0), QStringLiteral("Haus"));
    }

    void listedWordsHunspellLacksAreKept()
    {
        // The frequency list has words the dictionary lacks: chat words
        // ("окей", "naja"), names, British spellings. They stay as typed;
        // only a nearly free fix (a left-out apostrophe) is still made.
        LocalLexicon lexicon;
        useLatinImeFixtures(lexicon, QStringLiteral("en"));
        QVERIFY(!lexicon.hasWord(QStringLiteral("honour")));
        QVERIFY(lexicon.listedButNotInDictionary(QStringLiteral("honour")));
        QVERIFY(lexicon.bestCorrection(QStringLiteral("honour")).isEmpty());   // not "honor"
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("honr")), QStringLiteral("honor"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("thats")), QStringLiteral("that's"));
        QVERIFY(!lexicon.suggestions(QStringLiteral("hono"), QString(), 3).contains(QStringLiteral("honour")));
        // A swap into an unlisted word needs more than three letters.
        QVERIFY(lexicon.hasWord(QStringLiteral("mog")));
        QVERIFY(lexicon.bestCorrection(QStringLiteral("omg")).isEmpty());
    }

    void wordsRunTogetherAreSplit()
    {
        // LatinIME's space omission and mistyped space: Space between two
        // words left out, or a letter above the space bar typed for it.
        LocalLexicon lexicon;
        useLatinImeFixtures(lexicon, QStringLiteral("en"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("andthe")), QStringLiteral("and the"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("andbthe")), QStringLiteral("and the"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("mondayand")), QStringLiteral("Monday and"));   // dictionary forms
        QVERIFY(lexicon.suggestions(QStringLiteral("andthe"), QString(), 3).contains(QStringLiteral("and the")));

        // A one-letter word run into the next one ("вобщем"), rather than a
        // stray first letter.
        useLatinImeFixtures(lexicon, QStringLiteral("ru"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("вобщем")), QStringLiteral("в общем"));

        // German writes compounds as one word: no "schon Haus".
        useLatinImeFixtures(lexicon, QStringLiteral("de"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("aberschon")), QStringLiteral("aber schon"));
        QVERIFY(lexicon.bestCorrection(QStringLiteral("schonhaus")) != QStringLiteral("schon Haus"));
    }

    void wordsOfAnotherEnabledLanguageAreNotCorrected()
    {
        // Gboard's multilingual typing: with German enabled next to English,
        // "danke" typed on the English layout is German, not a typo of "dance".
        LocalLexicon lexicon;
        useLatinImeFixtures(lexicon, QStringLiteral("en"));
        LocalLexicon::clearForeignWordCacheForTesting();
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("danke")), QStringLiteral("dance"));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("sind")), QStringLiteral("Sind"));
        lexicon.setCompanionLanguages({QStringLiteral("de"), QStringLiteral("en")});
        LocalLexicon::preloadForeignWordsForTesting(QStringLiteral("de"));
        QVERIFY(lexicon.bestCorrection(QStringLiteral("danke")).isEmpty());
        QVERIFY(lexicon.bestCorrection(QStringLiteral("nicht")).isEmpty());
        QVERIFY(lexicon.correctionPreview(QStringLiteral("bitte")).isEmpty());
        QVERIFY(lexicon.bestCorrection(QStringLiteral("sind")).isEmpty());      // no capital "Sind" either
        QVERIFY(lexicon.knownInCompanionLanguage(QStringLiteral("Danke")));
        // English typos are still English typos.
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("dont")), QStringLiteral("don't"));
        lexicon.setCompanionLanguages({});
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("danke")), QStringLiteral("dance"));
        LocalLexicon::clearForeignWordCacheForTesting();
    }

    void aNearlyFreeFixInTheActiveLanguageBeatsAnotherLanguagesWord()
    {
        // Ukrainian layout, Russian enabled too: "привет" is Russian and stays,
        // but "пять" is far more likely "п'ять" with the apostrophe left out
        // (the layout says which language is being typed).
        LocalLexicon lexicon;
        useLatinImeFixtures(lexicon, QStringLiteral("uk"));
        LocalLexicon::clearForeignWordCacheForTesting();
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("привет")), QStringLiteral("привіт"));
        lexicon.setCompanionLanguages({QStringLiteral("ru")});
        LocalLexicon::preloadForeignWordsForTesting(QStringLiteral("ru"));
        QVERIFY(lexicon.knownInCompanionLanguage(QStringLiteral("пять")));
        QVERIFY(lexicon.bestCorrection(QStringLiteral("привет")).isEmpty());
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("пять")), QStringLiteral("п'ять"));
        LocalLexicon::clearForeignWordCacheForTesting();
    }
};

QTEST_GUILESS_MAIN(LexiconTest)
#include "tst_lexicon.moc"
