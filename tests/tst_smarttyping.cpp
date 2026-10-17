// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QTest>

#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>

#include "core/inputmethodbackend.h"
#include "core/keyboardcontroller.h"
#include "core/locallexicon.h"
#include "core/keyboardmodel.h"
#include "core/typingengine.h"
#include "glidepaths.h"

class FakeBackend final : public Tastra::InputMethodBackend
{
public:
    void commitText(const QString &text) override
    {
        // Contract (as KWin does it): a commit replaces the preedit atomically.
        preedit.clear();
        commits.append(text);
        events.append(QStringLiteral("commit:") + text);
    }
    void backspace() override { ++backspaces; events.append(QStringLiteral("bs")); }
    void deleteForward() override {}
    void moveLeft() override {}
    void moveRight() override {}
    void moveHome() override {}
    void moveEnd() override {}
    void enter() override { ++enters; events.append(QStringLiteral("enter")); }

    bool deleteBeforeCursor(const QString &text) override
    {
        if (!textChannel) return false;
        deletions.append(text);
        events.append(QStringLiteral("del:") + text);
        return true;
    }
    bool deleteAroundCursor(const QString &before, const QString &after) override
    {
        if (!textChannel) return false;
        events.append(QStringLiteral("delAround:") + before + QLatin1Char('|') + after);
        return true;
    }

    bool setPreedit(const QString &text) override
    {
        if (!preeditSupport) return false;
        preedit = text;
        events.append(QStringLiteral("pre:") + text);
        return true;
    }

    QStringList commits;
    QStringList deletions;
    int backspaces = 0;
    int enters = 0;
    bool textChannel = false;
    bool preeditSupport = false;
    QString preedit;
    QStringList events;
};

namespace
{
// Simulates the client echoing its text state back after an IM edit.
void echo(Tastra::TypingEngine &engine, const QString &textBeforeCursor)
{
    const int bytes = textBeforeCursor.toUtf8().size();
    engine.syncSurroundingText(textBeforeCursor, bytes, bytes);
}
}

class SmartTypingTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("TastraTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_smarttyping"));
        Tastra::LocalLexicon::setDictionarySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        Tastra::LocalLexicon::setBlocklistSearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        Tastra::LocalLexicon::setUserDictionaryFile(QStringLiteral(TASTRA_TEST_DATA "/empty/none.txt"));
        // Bundled word pairs only where a test asks for them.
        Tastra::LocalLexicon::setBigramSearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
    }

    void init() { QSettings().clear(); }

    void fallbackLexiconSuggestsPrefixesAndCorrectsTypos()
    {
        Tastra::LocalLexicon lexicon;
        lexicon.setLanguage(QStringLiteral("en"));
        QVERIFY(lexicon.waitForDictionary(5000));

        QVERIFY(lexicon.dictionarySize() > 20);
        QVERIFY(lexicon.suggestions(QStringLiteral("hel"), {}).contains(QStringLiteral("hello")));
        QCOMPARE(lexicon.bestCorrection(QStringLiteral("teh")), QStringLiteral("the"));
    }

    void autocorrectReplacesCommittedWordBeforeSpace()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        engine.setLanguage(QStringLiteral("en"));

        engine.typeLetter(QStringLiteral("t"));
        engine.typeLetter(QStringLiteral("e"));
        engine.typeLetter(QStringLiteral("h"));
        engine.space();

        QCOMPARE(backend.backspaces, 3);
        QCOMPARE(backend.commits.mid(3), QStringList({QStringLiteral("the"), QStringLiteral(" ")}));
        QCOMPARE(engine.currentWord(), QString());
    }

    void suggestionChoiceReplacesWordAndAddsSpace()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);

        engine.typeLetter(QStringLiteral("h"));
        engine.typeLetter(QStringLiteral("e"));
        engine.typeLetter(QStringLiteral("l"));
        engine.chooseSuggestion(QStringLiteral("hello"));

        QCOMPARE(backend.backspaces, 3);
        QCOMPARE(backend.commits.last(), QStringLiteral(" "));
        QCOMPARE(engine.previousWord(), QStringLiteral("hello"));
    }

    void shortTokensAreNotAggressivelyAutocorrected()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        engine.setLanguage(QStringLiteral("en"));

        engine.typeLetter(QStringLiteral("h"));
        engine.typeLetter(QStringLiteral("i"));
        engine.space();

        QCOMPARE(backend.backspaces, 0);
        QCOMPARE(backend.commits.last(), QStringLiteral(" "));
        QCOMPARE(engine.previousWord(), QStringLiteral("hi"));
    }

    void doubleSpaceProducesPeriodSpace()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);

        engine.typeLetter(QStringLiteral("h"));
        engine.typeLetter(QStringLiteral("i"));
        engine.space();
        engine.space();

        QCOMPARE(backend.backspaces, 1);
        QCOMPARE(backend.commits.last(), QStringLiteral(". "));
        QVERIFY(engine.wantsAutoUppercase());
    }

    void sentencePunctuationArmsAutoCapitalization()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);

        QVERIFY(engine.wantsAutoUppercase());
        engine.typeLetter(QStringLiteral("H"));
        QVERIFY(!engine.wantsAutoUppercase());
        engine.typeText(QStringLiteral("."));
        engine.space();
        QVERIFY(engine.wantsAutoUppercase());
    }

    void sensitiveContextSuppressesLearningAndSuggestions()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);

        engine.setSensitiveContext(true);
        engine.typeLetter(QStringLiteral("s"));
        engine.typeLetter(QStringLiteral("e"));

        QVERIFY(engine.sensitiveContext());
        QCOMPARE(engine.currentWord(), QString());
        QCOMPARE(engine.suggestions(), QStringList());
        QVERIFY(!engine.wantsAutoUppercase());
    }

    void surroundingTextSeedsCurrentAndPreviousWords()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);

        const QString text = QStringLiteral("hello wor");
        engine.syncSurroundingText(text, text.toUtf8().size(), text.toUtf8().size());

        QCOMPARE(engine.currentWord(), QStringLiteral("wor"));
        QCOMPARE(engine.previousWord(), QStringLiteral("hello"));
    }

    void staleSurroundingEchoDoesNotClobberLocalWord()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        engine.setLanguage(QStringLiteral("en"));

        engine.typeLetter(QStringLiteral("t"));
        engine.typeLetter(QStringLiteral("e"));
        QVERIFY(!engine.syncSurroundingText(QStringLiteral("t"), 1, 1));
        QCOMPARE(engine.currentWord(), QStringLiteral("te"));

        engine.typeLetter(QStringLiteral("h"));
        echo(engine, QStringLiteral("te"));
        echo(engine, QStringLiteral("teh"));
        QCOMPARE(engine.currentWord(), QStringLiteral("teh"));
        engine.space();
        QCOMPARE(backend.backspaces, 3);
        QCOMPARE(backend.commits.mid(3), QStringList({QStringLiteral("the"), QStringLiteral(" ")}));
    }


    // ---- Composition (preedit): the current word never leaves the keyboard
    // until it is final, so no deletions and no Backspace keys are needed to
    // correct it. Works even when the client echoes stale text (Firefox).
    // Mid-text by default ("Hi. " before the cursor: a sentence start, but not
    // an empty paragraph); empty paragraphs have their own test below.
    static void composing(FakeBackend &backend, Tastra::TypingEngine &engine,
                          const QString &before = QStringLiteral("Hi. "))
    {
        backend.textChannel = true;
        backend.preeditSupport = true;
        echo(engine, before);
    }

    void aCorrectionThatChangesNothingIsNoCorrection()
    {
        // "I" typed with Shift: the lexicon's answer for "i" is "I" again.
        // That must not count as an autocorrection, or Backspace after the
        // space would "undo" it and keep "i" as a word of the user's.
        Tastra::LocalLexicon::setDictionarySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/latinime/hunspell")});
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/latinime/frequency")});
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        engine.setLanguage(QStringLiteral("en"));
        QVERIFY(engine.waitForDictionaryForTesting(5000));
        composing(backend, engine);
        engine.setAutoCapitalizationEnabled(false);
        engine.typeLetter(QStringLiteral("i"));
        QCOMPARE(engine.autocorrectTarget(), QStringLiteral("I"));
        engine.backspace();
        engine.typeLetter(QStringLiteral("I"));
        QVERIFY(engine.autocorrectTarget().isEmpty());   // nothing to correct
        engine.space();
        engine.backspace();                              // the space; no correction to undo
        engine.space();
        engine.typeLetter(QStringLiteral("i"));
        QCOMPARE(engine.autocorrectTarget(), QStringLiteral("I"));   // "i" was not kept as a word

        // Clients without composition: no delete-and-retype of the same word.
        FakeBackend plain;
        plain.textChannel = true;
        Tastra::KeyboardController plainController(plain);
        Tastra::TypingEngine plainEngine(plainController);
        plainEngine.setLanguage(QStringLiteral("en"));
        QVERIFY(plainEngine.waitForDictionaryForTesting(5000));
        echo(plainEngine, QStringLiteral("Hi. "));
        plainEngine.setAutoCapitalizationEnabled(false);
        plainEngine.typeLetter(QStringLiteral("I"));
        echo(plainEngine, QStringLiteral("Hi. I"));     // the client confirms the letter
        plainEngine.space();
        QVERIFY2(plain.deletions.isEmpty(), qPrintable(plain.events.join(QLatin1Char(' '))));
        Tastra::LocalLexicon::setDictionarySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
    }

    void twoWordsRunTogetherAreSplitAndLearnedAsWords()
    {
        // LatinIME's space omission: "andthe" + Space gives "and the ", the
        // next word follows "the", and Backspace brings back what was typed.
        Tastra::LocalLexicon::setDictionarySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/latinime/hunspell")});
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/latinime/frequency")});
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        engine.setLanguage(QStringLiteral("en"));
        QVERIFY(engine.waitForDictionaryForTesting(5000));
        composing(backend, engine);
        engine.setAutoCapitalizationEnabled(false);
        for (const QChar ch : QStringLiteral("andthe")) engine.typeLetter(QString(ch));
        QCOMPARE(engine.autocorrectTarget(), QStringLiteral("and the"));
        engine.space();
        QCOMPARE(backend.preedit, QStringLiteral("and the "));
        engine.typeLetter(QStringLiteral("x"));               // commits "and the "
        QCOMPARE(backend.commits.join(QString()), QStringLiteral("and the "));
        QCOMPARE(engine.previousWord(), QStringLiteral("the"));
        engine.space();
        for (const QChar ch : QStringLiteral("andthe")) engine.typeLetter(QString(ch));
        engine.space();
        QCOMPARE(backend.preedit, QStringLiteral("and the "));
        engine.backspace();                                   // undo
        QCOMPARE(backend.preedit, QStringLiteral("andthe"));
        Tastra::LocalLexicon::setDictionarySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
    }

    void aWordOfAnotherEnabledLanguageIsKnownAndKept()
    {
        Tastra::LocalLexicon::setDictionarySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/latinime/hunspell")});
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/latinime/frequency")});
        Tastra::LocalLexicon::clearForeignWordCacheForTesting();
        Tastra::LocalLexicon::preloadForeignWordsForTesting(QStringLiteral("de"));
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        engine.setLanguage(QStringLiteral("en"));
        QVERIFY(engine.waitForDictionaryForTesting(5000));
        engine.setCompanionLanguages({QStringLiteral("de")});
        composing(backend, engine);
        engine.setAutoCapitalizationEnabled(false);
        for (const QChar ch : QStringLiteral("danke")) engine.typeLetter(QString(ch));
        QVERIFY(engine.currentWordKnown());               // no "+ Add to dictionary" offer
        QVERIFY(engine.autocorrectTarget().isEmpty());    // and no "dance"
        Tastra::LocalLexicon::clearForeignWordCacheForTesting();
        Tastra::LocalLexicon::setDictionarySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
    }

    void capitalisationFollowsLatinIMEOnTheFieldText()
    {
        auto capsAfter = [](const QString &language, const QString &before) {
            FakeBackend backend;
            Tastra::KeyboardController controller(backend);
            Tastra::TypingEngine engine(controller);
            engine.setLanguage(language);
            composing(backend, engine, before);
            return engine.wantsAutoUppercase();
        };
        QVERIFY(!capsAfter(QStringLiteral("en"), QStringLiteral("We met in the U.S. ")));   // abbreviation
        QVERIFY(capsAfter(QStringLiteral("en"), QStringLiteral("He said \"Hi.\" ")));     // American quotes
        QVERIFY(!capsAfter(QStringLiteral("de"), QStringLiteral("Wir sehen uns am 3. ")));  // German date
        QVERIFY(!capsAfter(QStringLiteral("de"), QStringLiteral("Liebe Sara,\n")));        // German letter line
        QVERIFY(capsAfter(QStringLiteral("ru"), QStringLiteral("Привет! ")));
        QVERIFY(!capsAfter(QStringLiteral("ru"), QStringLiteral("т.е. ")));                // abbreviation
    }

    void returningToAnAutocorrectedWordOffersWhatWasTyped()
    {
        // Gboard: put the cursor back at an autocorrected word and the strip
        // offers the original; choosing it restores and keeps it.
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        qint64 now = 1000;
        engine.setClockForTesting([&now] { return now; });
        composing(backend, engine);
        engine.setAutoCapitalizationEnabled(false);
        for (const QChar ch : QStringLiteral("teh")) engine.typeLetter(QString(ch));
        engine.space();                                   // -> "the " (held)
        engine.typeLetter(QStringLiteral("x"));           // correction committed
        engine.space();
        now += 2000;
        engine.resetComposition();                        // tapping into the text: KWin "reset"
        echo(engine, QStringLiteral("Hi. the"));          // cursor now right behind "the"
        QCOMPARE(engine.currentWord(), QStringLiteral("the"));
        QCOMPARE(engine.suggestions().value(0), QStringLiteral("teh"));
        engine.chooseSuggestion(QStringLiteral("teh"));
        QCOMPARE(backend.deletions.last(), QStringLiteral("the"));
        QVERIFY(backend.commits.contains(QStringLiteral("teh")));
        // Kept from now on: typing it again is not corrected.
        for (const QChar ch : QStringLiteral("teh")) engine.typeLetter(QString(ch));
        engine.space();
        QVERIFY2(!(backend.commits.join(QString()) + backend.preedit).endsWith(QStringLiteral("the ")),
                 qPrintable(backend.commits.join(QString()) + backend.preedit));
    }

    void tappingIntoAWordOffersSuggestionsForTheWholeWord()
    {
        // Gboard: put the cursor inside a word and the strip shows options for
        // the whole word; choosing one replaces the whole word.
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        qint64 now = 1000;
        engine.setClockForTesting([&now] { return now; });
        composing(backend, engine);
        engine.addUserWord(QStringLiteral("world"));
        now += 2000;
        engine.resetComposition();                                   // the user taps into the text
        const QString text = QStringLiteral("Hi. wrold is");
        const int cursor = QStringLiteral("Hi. wr").toUtf8().size();
        engine.syncSurroundingText(text, cursor, cursor);
        QCOMPARE(engine.currentWord(), QStringLiteral("wr"));
        QVERIFY2(engine.suggestions().contains(QStringLiteral("world")), qPrintable(engine.suggestions().join(',')));
        QVERIFY(engine.autocorrectTarget().isEmpty());              // no autocorrect mid-word
        engine.chooseSuggestion(QStringLiteral("world"));
        QCOMPARE(backend.events.mid(backend.events.size() - 2),
                 QStringList({QStringLiteral("delAround:wr|old"), QStringLiteral("commit:world")}));
        QVERIFY(!backend.commits.contains(QStringLiteral(" ")));     // " is" already follows: no extra space

        // Space inside a word splits it; the left part is not autocorrected.
        now += 2000;
        engine.resetComposition();
        const QString text2 = QStringLiteral("Hi. tehx is");
        const int cursor2 = QStringLiteral("Hi. teh").toUtf8().size();       // "teh|x": "teh" alone would be corrected
        engine.syncSurroundingText(text2, cursor2, cursor2);
        QCOMPARE(engine.currentWord(), QStringLiteral("teh"));
        const int deletionsBefore = backend.deletions.size();
        engine.space();
        QCOMPARE(backend.deletions.size(), deletionsBefore);
        QCOMPARE(backend.commits.last(), QStringLiteral(" "));
    }

    void wrongLayoutIsRecognisedAndOffered()
    {
        // "ghbdtn" is "привет" typed with the English layout active.
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/frequency-foreign")});
        Tastra::LocalLexicon::setBlocklistSearchPaths({QStringLiteral(TASTRA_TEST_DATA "/blocklist-foreign")});
        Tastra::LocalLexicon::clearForeignWordCacheForTesting();
        Tastra::LocalLexicon::preloadForeignWordsForTesting(QStringLiteral("ru"));
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        composing(backend, engine);
        engine.setAutoCapitalizationEnabled(false);
        const QStringList en = Tastra::KeyboardModel::rowsForLanguage(QStringLiteral("en"));
        const QStringList ru = Tastra::KeyboardModel::rowsForLanguage(QStringLiteral("ru"));
        QHash<QChar, QChar> map;
        for (int r = 0; r < 3; ++r)
            for (int i = 0; i < qMin(en.at(r).size(), ru.at(r).size()); ++i) map.insert(en.at(r).at(i), ru.at(r).at(i));
        engine.setForeignLayouts({{QStringLiteral("ru"), map}});
        for (const QChar ch : QStringLiteral("ghbdtn")) engine.typeLetter(QString(ch));
        QCOMPARE(engine.suggestions().value(0), QStringLiteral("привет"));
        QCOMPARE(engine.layoutSuggestionLanguage(), QStringLiteral("ru"));
        engine.space();
        // "hel" is still becoming an English word ("hello"): no "рук".
        for (const QChar ch : QStringLiteral("hel")) engine.typeLetter(QString(ch));
        QVERIFY(engine.layoutSuggestionWord().isEmpty());
        QVERIFY(!engine.suggestions().contains(QStringLiteral("рук")));
        engine.space();
        // Letters that read as no word of the other language: nothing offered
        // (the offensive-word filter itself: foreignWordListsDropOffensiveWords).
        for (const QChar ch : QStringLiteral("kjr")) engine.typeLetter(QString(ch));      // "лок"
        QVERIFY(engine.layoutSuggestionWord().isEmpty());
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        Tastra::LocalLexicon::setBlocklistSearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        Tastra::LocalLexicon::clearForeignWordCacheForTesting();
        QVERIFY(!Tastra::LocalLexicon::knownInLanguage(QStringLiteral("ru"), QStringLiteral("x")));
    }

    void foreignWordListsDropOffensiveWords()
    {
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/frequency-foreign")});
        Tastra::LocalLexicon::setBlocklistSearchPaths({QStringLiteral(TASTRA_TEST_DATA "/blocklist-foreign")});
        Tastra::LocalLexicon::clearForeignWordCacheForTesting();
        // The first lookup never blocks typing: it starts a background load.
        QVERIFY(!Tastra::LocalLexicon::knownInLanguage(QStringLiteral("ru"), QStringLiteral("привет")));
        QTRY_VERIFY(Tastra::LocalLexicon::knownInLanguage(QStringLiteral("ru"), QStringLiteral("привет")));
        QVERIFY(!Tastra::LocalLexicon::knownInLanguage(QStringLiteral("ru"), QStringLiteral("блок")));
        QVERIFY(Tastra::LocalLexicon::knownInLanguage(QStringLiteral("ru"), QStringLiteral("Привет")));
        QVERIFY(!Tastra::LocalLexicon::knownInLanguage(QStringLiteral("ru"), QStringLiteral("приветик")));
        QVERIFY(!Tastra::LocalLexicon::knownInLanguage(QStringLiteral("ru"), QString()));
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        Tastra::LocalLexicon::setBlocklistSearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        Tastra::LocalLexicon::clearForeignWordCacheForTesting();
    }

    void emptyParagraphCommitsTheFirstLetterBeforeComposing()
    {
        // ProseMirror (claude.ai, ChatGPT) re-renders an empty paragraph's
        // placeholder on the first input and breaks a composition started
        // there; known from ProseMirror's changelog and a Yjs report.
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        composing(backend, engine, QString());              // empty field
        engine.setAutocorrectEnabled(true);
        for (const QChar ch : QStringLiteral("Teh")) engine.typeLetter(QString(ch));
        QCOMPARE(backend.commits, QStringList({QStringLiteral("T")}));   // first letter at once
        QCOMPARE(backend.preedit, QStringLiteral("eh"));                // the rest composes
        engine.space();                                                 // "Teh" -> "The"
        QCOMPARE(backend.commits.join(QString()) + backend.preedit, QStringLiteral("The "));
        engine.typeLetter(QStringLiteral("x"));
        QCOMPARE(backend.commits.join(QString()) + backend.preedit, QStringLiteral("The x"));
        // A new paragraph starts empty again.
        engine.enter();
        engine.typeLetter(QStringLiteral("O"));
        QCOMPARE(backend.commits.last(), QStringLiteral("O"));
        QCOMPARE(backend.preedit, QString());
        // Suggestions never propose replacing the committed letter.
        for (const QString &s : engine.suggestions()) QVERIFY2(s.startsWith(QLatin1Char('O'), Qt::CaseInsensitive), qPrintable(s));
    }

    void compositionCommitsCorrectedWordWithoutDeletions()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        composing(backend, engine);

        engine.typeLetter(QStringLiteral("t"));
        engine.typeLetter(QStringLiteral("e"));
        engine.typeLetter(QStringLiteral("h"));
        QCOMPARE(backend.preedit, QStringLiteral("teh"));
        QVERIFY(backend.commits.isEmpty());
        engine.space();

        // The correction stays revertible (in the preedit) until the next key.
        QVERIFY(backend.commits.isEmpty());
        QCOMPARE(backend.preedit, QStringLiteral("the "));
        engine.typeLetter(QStringLiteral("x"));
        QCOMPARE(backend.commits, QStringList({QStringLiteral("the ")}));
        QCOMPARE(backend.preedit, QStringLiteral("x"));
        QCOMPARE(backend.backspaces, 0);
        QVERIFY(backend.deletions.isEmpty());
    }

    void backspaceRightAfterAutocorrectRevertsAndRemembers()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        composing(backend, engine);

        for (const QChar ch : QStringLiteral("teh")) engine.typeLetter(QString(ch));
        engine.space();
        QCOMPARE(backend.preedit, QStringLiteral("the "));
        engine.backspace();                                   // undo the correction
        QCOMPARE(backend.preedit, QStringLiteral("teh"));
        QCOMPARE(engine.currentWord(), QStringLiteral("teh"));
        engine.space();                                       // keep what was typed
        QCOMPARE(backend.commits, QStringList({QStringLiteral("teh")}));

        // Reverted words are learned and no longer autocorrected.
        for (const QChar ch : QStringLiteral("teh")) engine.typeLetter(QString(ch));
        engine.space();
        QCOMPARE(backend.commits, QStringList({QStringLiteral("teh"), QStringLiteral(" "), QStringLiteral("teh")}));
        QCOMPARE(backend.preedit, QStringLiteral(" "));
    }

    void correctionThenDoubleSpaceOrEnterOrPunctuation()
    {
        for (const QString &finish : {QStringLiteral("space"), QStringLiteral("enter"), QStringLiteral(",")}) {
            FakeBackend backend;
            Tastra::KeyboardController controller(backend);
            Tastra::TypingEngine engine(controller);
            composing(backend, engine);
            for (const QChar ch : QStringLiteral("teh")) engine.typeLetter(QString(ch));
            engine.space();
            if (finish == QStringLiteral("space")) {
                engine.space();
                // ". " is held in the preedit so Backspace can revert it (LatinIME).
                QCOMPARE(backend.commits, QStringList({QStringLiteral("the")}));
                QCOMPARE(backend.preedit, QStringLiteral(". "));
                continue;
            } else if (finish == QStringLiteral("enter")) {
                engine.enter();
                QCOMPARE(backend.events.mid(backend.events.size() - 2),
                         QStringList({QStringLiteral("commit:the"), QStringLiteral("enter")}));
            } else {
                engine.typeText(finish);
                // The space typed after the word moves behind the comma (both
                // held until the next key so Backspace can undo the swap).
                QCOMPARE(backend.commits.join(QString()) + backend.preedit, QStringLiteral("the, "));
                QCOMPARE(backend.backspaces, 0);
                continue;
            }
            QCOMPARE(backend.preedit, QString());
        }
    }

    void nextWordSuggestionsCanBeTurnedOff()
    {
        // Gboard: Settings -> Text correction -> Next-word suggestions.
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        composing(backend, engine);
        engine.setAutoCapitalizationEnabled(false);
        auto type = [&](const QString &word) {
            for (const QChar ch : word) engine.typeLetter(QString(ch));
            engine.space();
        };
        for (const QString &word : {QStringLiteral("good"), QStringLiteral("morning"), QStringLiteral("good"),
                                    QStringLiteral("morning"), QStringLiteral("good")}) {
            type(word);
        }
        QVERIFY2(engine.suggestions().contains(QStringLiteral("morning")), qPrintable(engine.suggestions().join(',')));
        engine.setNextWordSuggestionsEnabled(false);
        QVERIFY2(engine.suggestions().isEmpty(), qPrintable(engine.suggestions().join(',')));
        type(QStringLiteral("morning"));
        type(QStringLiteral("good"));
        QVERIFY(engine.suggestions().isEmpty());
        engine.typeLetter(QStringLiteral("m"));                    // completions still work
        QVERIFY(!engine.suggestions().isEmpty());
    }

    void thePhraseChipOffersTheUsersHabit()
    {
        // Gboard Smart Compose, as a chip: words the user habitually types
        // after a word (at least three times, at least 60 % of what followed
        // it) complete the current word, or follow a Space.
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        engine.setLanguage(QStringLiteral("en"));
        engine.clearLearning();
        composing(backend, engine);
        engine.setAutoCapitalizationEnabled(false);
        engine.setAutocorrectEnabled(false);
        auto type = [&](const QString &words) {
            for (const QString &word : words.split(QLatin1Char(' '))) {
                for (const QChar ch : word) engine.typeLetter(QString(ch));
                engine.space();
            }
        };
        auto endSentence = [&] {
            engine.typeText(QStringLiteral("."));
            engine.space();
        };
        auto written = [&] { return backend.commits.join(QString()) + backend.preedit; };

        type(QStringLiteral("see you soon"));
        endSentence();
        type(QStringLiteral("see you soon"));
        endSentence();
        type(QStringLiteral("see"));
        QVERIFY2(engine.phraseSuggestion().isEmpty(), qPrintable(engine.phraseSuggestion()));   // twice is no habit
        type(QStringLiteral("you soon"));
        endSentence();

        // While typing: the word completed and the habit after it.
        engine.typeLetter(QStringLiteral("s"));
        engine.typeLetter(QStringLiteral("e"));
        QCOMPARE(engine.phraseSuggestion(), QStringLiteral("see you soon"));
        backend.commits.clear();
        engine.chooseSuggestion(engine.phraseSuggestion());
        QCOMPARE(written(), QStringLiteral("see you soon "));
        QCOMPARE(engine.previousWord(), QStringLiteral("soon"));
        QVERIFY(engine.phraseSuggestion().isEmpty());              // nothing after "soon"
        endSentence();

        // After a Space: two words or more.
        type(QStringLiteral("see"));
        QCOMPARE(engine.phraseSuggestion(), QStringLiteral("you soon"));
        backend.commits.clear();
        engine.chooseSuggestion(engine.phraseSuggestion());
        QCOMPARE(written(), QStringLiteral(" you soon "));
        endSentence();
        type(QStringLiteral("you"));
        QVERIFY2(engine.phraseSuggestion().isEmpty(), qPrintable(engine.phraseSuggestion()));   // one word: the strip has it
        endSentence();

        // Not at the start of a sentence: "soon. See" is no habit of "soon".
        type(QStringLiteral("see you"));
        QVERIFY(engine.phraseSuggestion().isEmpty());
        endSentence();

        // Not in password fields, not when switched off.
        engine.typeLetter(QStringLiteral("s"));
        engine.typeLetter(QStringLiteral("e"));
        QVERIFY(!engine.phraseSuggestion().isEmpty());
        engine.setPhraseSuggestionsEnabled(false);
        QVERIFY(engine.phraseSuggestion().isEmpty());
        engine.setPhraseSuggestionsEnabled(true);
        QVERIFY(!engine.phraseSuggestion().isEmpty());
        engine.setSensitiveContext(true);
        QVERIFY(engine.phraseSuggestion().isEmpty());
        engine.setSensitiveContext(false);
        engine.space();
        endSentence();

        // A habit of two different continuations is none.
        for (int i = 0; i < 6; ++i) {
            type(i % 2 ? QStringLiteral("call them later") : QStringLiteral("call you back"));
            endSentence();
        }
        type(QStringLiteral("call"));
        QVERIFY2(engine.phraseSuggestion().isEmpty(), qPrintable(engine.phraseSuggestion()));
        endSentence();

        // Long-press forgets the habit, not the words.
        type(QStringLiteral("see"));
        QCOMPARE(engine.phraseSuggestion(), QStringLiteral("you soon"));
        engine.forgetPhrase(engine.phraseSuggestion());
        QVERIFY(engine.phraseSuggestion().isEmpty());
        engine.typeLetter(QStringLiteral("s"));
        engine.typeLetter(QStringLiteral("o"));
        QVERIFY(engine.phraseSuggestion().isEmpty());
        QVERIFY2(engine.suggestions().contains(QStringLiteral("soon")), qPrintable(engine.suggestions().join(',')));
    }

    void aNewSentenceHasNoPreviousWord()
    {
        // LatinIME: after a sentence ends the context is the beginning of a
        // sentence, so the next word is neither predicted from nor learned
        // after the last word before it.
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        composing(backend, engine);
        engine.setAutoCapitalizationEnabled(false);
        auto type = [&](const QString &word) {
            for (const QChar ch : word) engine.typeLetter(QString(ch));
        };
        type(QStringLiteral("thanks"));
        engine.typeText(QStringLiteral("."));
        engine.space();
        QVERIFY(engine.previousWord().isEmpty());
        type(QStringLiteral("ok"));
        engine.typeText(QStringLiteral(","));
        QCOMPARE(engine.previousWord(), QStringLiteral("ok"));        // a comma keeps it
        engine.space();
        type(QStringLiteral("fine"));
        engine.space();
        QCOMPARE(engine.previousWord(), QStringLiteral("fine"));
        engine.space();                                                // double space: ". "
        QVERIFY(engine.previousWord().isEmpty());
        type(QStringLiteral("bye"));
        engine.enter();
        QVERIFY(engine.previousWord().isEmpty());
        // From the application's text too.
        for (const auto &[text, previous] : {std::pair{QStringLiteral("See you. Then"), QString()},
                                            std::pair{QStringLiteral("See you\nThen"), QString()},
                                            std::pair{QStringLiteral("See you, then"), QStringLiteral("you")}}) {
            FakeBackend other;
            Tastra::KeyboardController otherController(other);
            Tastra::TypingEngine fresh(otherController);
            composing(other, fresh, text);
            QCOMPARE(fresh.previousWord(), previous);
        }
    }

    void theStripPredictsTheNextWordBeforeAnythingWasLearned()
    {
        // Gboard: "thank" + Space offers "you"; tapping it types "you " and
        // the strip moves on to the word after it.
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(":/tastra/frequency")});
        Tastra::LocalLexicon::setBigramSearchPaths({QStringLiteral(":/tastra/bigrams")});
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        engine.setLearningEnabled(false);
        engine.setLanguage(QStringLiteral("en"));
        QVERIFY(engine.waitForDictionaryForTesting(10000));
        composing(backend, engine);
        engine.setAutoCapitalizationEnabled(false);
        for (const QChar ch : QStringLiteral("thank")) engine.typeLetter(QString(ch));
        engine.space();
        QVERIFY2(engine.suggestions().contains(QStringLiteral("you")), qPrintable(engine.suggestions().join(',')));
        engine.chooseSuggestion(QStringLiteral("you"));
        QCOMPARE(backend.commits.join(QString()), QStringLiteral("thank you"));
        QCOMPARE(backend.preedit, QStringLiteral(" "));
        QCOMPARE(engine.previousWord(), QStringLiteral("you"));
        QVERIFY(!engine.suggestions().isEmpty());
        // Not after the end of a sentence.
        engine.typeText(QStringLiteral("."));
        engine.space();
        QVERIFY2(engine.suggestions().isEmpty(), qPrintable(engine.suggestions().join(',')));
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        Tastra::LocalLexicon::setBigramSearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
    }

    void theHeldCorrectionIsTheWordBeforeTheCursor()
    {
        // "so i" + Space: "I" is held with its space (Backspace could still
        // restore "i"), and the strip predicts what follows "I", not "so".
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(":/tastra/frequency")});
        Tastra::LocalLexicon::setBigramSearchPaths({QStringLiteral(":/tastra/bigrams")});
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        engine.setLearningEnabled(false);
        engine.setLanguage(QStringLiteral("en"));
        QVERIFY(engine.waitForDictionaryForTesting(10000));
        composing(backend, engine);
        engine.setAutoCapitalizationEnabled(false);
        for (const QChar ch : QStringLiteral("so")) engine.typeLetter(QString(ch));
        engine.space();
        engine.typeLetter(QStringLiteral("i"));
        engine.space();
        QCOMPARE(backend.preedit, QStringLiteral("I "));
        const QStringList strip = engine.suggestions();
        QVERIFY2(strip.contains(QStringLiteral("have")) || strip.contains(QStringLiteral("am")), qPrintable(strip.join(',')));
        QVERIFY2(!strip.contains(QStringLiteral("I")), qPrintable(strip.join(',')));
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        Tastra::LocalLexicon::setBigramSearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
    }

    void undoingTheDoubleSpacePeriodKeepsTheSentenceGoing()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        composing(backend, engine);
        engine.setAutoCapitalizationEnabled(false);
        for (const QChar ch : QStringLiteral("hello")) engine.typeLetter(QString(ch));
        engine.space();
        engine.space();
        QCOMPARE(backend.preedit, QStringLiteral(". "));
        QVERIFY(engine.previousWord().isEmpty());
        engine.backspace();
        QCOMPARE(backend.preedit, QStringLiteral(" "));
        QCOMPARE(engine.previousWord(), QStringLiteral("hello"));
    }

    void apostrophesAndHyphensInsideAWordBelongToIt()
    {
        // LatinIME word connectors: "don't" is one word, typed with the
        // apostrophe from the symbols page, and so is "кто-то".
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        composing(backend, engine);
        engine.setAutoCapitalizationEnabled(false);
        for (const QChar ch : QStringLiteral("don")) engine.typeLetter(QString(ch));
        engine.typeText(QStringLiteral("'"));
        engine.typeLetter(QStringLiteral("t"));
        QCOMPARE(engine.currentWord(), QStringLiteral("don't"));
        QCOMPARE(backend.preedit, QStringLiteral("don't"));
        engine.space();
        QCOMPARE(engine.previousWord(), QStringLiteral("don't"));
        for (const QChar ch : QStringLiteral("dogs")) engine.typeLetter(QString(ch));
        engine.typeText(QStringLiteral("'"));
        engine.space();
        QCOMPARE(engine.previousWord(), QStringLiteral("dogs"));        // left as typed, "dogs'"
        QVERIFY2(backend.commits.join(QString()).endsWith(QStringLiteral("dogs'")), qPrintable(backend.commits.join('|')));
        // A quote after a space is not part of a word.
        engine.typeText(QStringLiteral("'"));
        QVERIFY(engine.currentWord().isEmpty());
        // From the application's text too.
        for (const auto &[text, previous] : {std::pair{QStringLiteral("I don't "), QStringLiteral("don't")},
                                            std::pair{QStringLiteral("Он кто-то "), QStringLiteral("кто-то")},
                                            std::pair{QStringLiteral("say 'hi' "), QStringLiteral("hi")}}) {
            FakeBackend other;
            Tastra::KeyboardController otherController(other);
            Tastra::TypingEngine fresh(otherController);
            composing(other, fresh, text);
            QCOMPARE(fresh.previousWord(), previous);
        }
    }

    void aWordTheClientDroppedWhenItsInputBouncedComesBack()
    {
        // Firefox switches its text input off and on again when some field
        // attributes change; GTK 3 throws the preedit away and KWin (no touch,
        // no focus change) does not commit it. Typing "hel", bounce, "lo"
        // gave "lo" alone, and at a sentence start "Lo": "the second letter
        // replaces the first". The next letter continues the word instead.
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        qint64 now = 1000;
        engine.setClockForTesting([&now] { return now; });
        composing(backend, engine, QStringLiteral("Ok. "));
        for (const QChar ch : QStringLiteral("Hel")) engine.typeLetter(QString(ch));
        QCOMPARE(backend.preedit, QStringLiteral("Hel"));
        now += 50;
        engine.rememberCompositionBeforeDeactivation();
        engine.resetInputContext();                       // deactivate
        backend.preedit.clear();                          // GTK dropped it
        now += 40;
        engine.resetInputContext();                       // activate again
        echo(engine, QStringLiteral("Ok. "));
        engine.resetComposition();                        // KWin's reset after activation
        echo(engine, QStringLiteral("Ok. "));
        now += 300;
        QVERIFY(!engine.wantsAutoUppercase());            // "l", not "L": the word goes on
        for (const QChar ch : QStringLiteral("lo")) engine.typeLetter(QString(ch));
        QCOMPARE(backend.preedit, QStringLiteral("Hello"));
        QCOMPARE(engine.currentWord(), QStringLiteral("Hello"));
        engine.space();
        QVERIFY2(backend.commits.join(QString()).endsWith(QStringLiteral("Hello")), qPrintable(backend.commits.join('|')));

        // A client that kept the word: nothing is typed twice.
        FakeBackend keeps;
        Tastra::KeyboardController keepsController(keeps);
        Tastra::TypingEngine kept(keepsController);
        kept.setClockForTesting([&now] { return now; });
        composing(keeps, kept, QStringLiteral("Ok "));
        for (const QChar ch : QStringLiteral("hel")) kept.typeLetter(QString(ch));
        kept.rememberCompositionBeforeDeactivation();
        kept.resetInputContext();
        kept.resetInputContext();
        now += 300;
        echo(kept, QStringLiteral("Ok hel"));
        keeps.preedit.clear();
        kept.typeLetter(QStringLiteral("l"));
        // The kept word is taken back and composed on (0.7.11), never typed
        // a second time.
        QVERIFY2(!keeps.commits.join(QString()).contains(QStringLiteral("hel")), qPrintable(keeps.commits.join('|')));
        QCOMPARE(keeps.deletions, QStringList({QStringLiteral("hel")}));
        QCOMPARE(keeps.preedit, QStringLiteral("hell"));

        // Something typed in between, or much later: nothing comes back.
        FakeBackend late;
        Tastra::KeyboardController lateController(late);
        Tastra::TypingEngine slow(lateController);
        slow.setClockForTesting([&now] { return now; });
        composing(late, slow, QStringLiteral("Ok "));
        for (const QChar ch : QStringLiteral("hel")) slow.typeLetter(QString(ch));
        slow.rememberCompositionBeforeDeactivation();
        slow.resetInputContext();
        now += 5000;
        slow.resetInputContext();
        echo(slow, QStringLiteral("Other field "));
        late.preedit.clear();
        slow.typeLetter(QStringLiteral("x"));
        QCOMPARE(late.preedit, QStringLiteral("x"));
    }

    void compositionDoubleSpaceAndNextWord()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        composing(backend, engine);

        engine.typeLetter(QStringLiteral("h"));
        engine.typeLetter(QStringLiteral("i"));
        engine.space();
        engine.space();
        QCOMPARE(backend.commits, QStringList({QStringLiteral("hi")}));
        QCOMPARE(backend.preedit, QStringLiteral(". "));   // held until the next letter
        QVERIFY(engine.wantsAutoUppercase());

        engine.typeLetter(QStringLiteral("J"));
        engine.typeLetter(QStringLiteral("o"));
        engine.space();
        engine.typeLetter(QStringLiteral("b"));
        QCOMPARE(backend.commits.mid(2), QStringList({QStringLiteral("Jo"), QStringLiteral(" ")}));
        QCOMPARE(backend.preedit, QStringLiteral("b"));
        QCOMPARE(backend.backspaces, 0);
        QVERIFY(backend.deletions.isEmpty());
    }

    void compositionSuggestionTapCommitsOnce()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        composing(backend, engine);

        engine.typeLetter(QStringLiteral("h"));
        engine.chooseSuggestion(QStringLiteral("hello"));
        QCOMPARE(backend.commits, QStringList({QStringLiteral("hello")}));
        QCOMPARE(backend.preedit, QStringLiteral(" "));
        QCOMPARE(backend.backspaces, 0);
    }

    void placeholderEchoDuringCompositionKeepsPendingSpace()
    {
        // Trace 0.2.5 (claude.ai in Firefox): the empty composer reports one
        // placeholder byte that disappears once text exists, so the first echo
        // after "Slovo" + Space looked foreign; the pending space was dropped
        // and "i" was committed without it -> "Slovoi".
        FakeBackend backend;
        backend.textChannel = true;
        backend.preeditSupport = true;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        echo(engine, QStringLiteral("\n"));
        for (const QChar ch : QStringLiteral("Slovo")) engine.typeLetter(QString(ch));
        engine.space();
        echo(engine, QStringLiteral("Slovo"));
        engine.typeLetter(QStringLiteral("i"));

        // The "\n" placeholder is an empty paragraph: since 1.2.1 "S" is
        // committed before composing (ProseMirror placeholder issue).
        QCOMPARE(backend.commits.first(), QStringLiteral("S"));
        QCOMPARE(backend.commits.join(QString()), QStringLiteral("Slovo "));
        QCOMPARE(backend.preedit, QStringLiteral("i"));
    }

    void selfCausedEchoesAreIgnoredButLaterExternalEditsAdopted()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        qint64 now = 1000;
        engine.setClockForTesting([&now] { return now; });

        engine.typeLetter(QStringLiteral("a"));
        now += 5;                                   // measured echo latency: 2-36 ms
        echo(engine, QStringLiteral("zz"));
        QCOMPARE(engine.currentWord(), QStringLiteral("a"));
        now += 1000;                                // a human-speed external edit
        echo(engine, QStringLiteral("hello wor"));
        QCOMPARE(engine.currentWord(), QStringLiteral("wor"));
    }

    void glideOffersTheOtherReadingsAndReplacesTheWord()
    {
        // Gboard: right after a glide the strip shows the other words the
        // path could mean ("too" and "to" share a path); tapping one replaces
        // the glided word, and the next key press moves on.
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(":/tastra/frequency")});
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        engine.setLanguage(QStringLiteral("en"));
        QVERIFY(engine.waitForDictionaryForTesting(10000));
        composing(backend, engine);
        engine.setAutoCapitalizationEnabled(false);
        const auto centres = GlidePaths::centresFor(QStringLiteral("en"));
        const QString glided = engine.endGlidePath(GlidePaths::pathFor(QStringLiteral("too"), centres, 0.0, 3), centres,
                                                   GlidePaths::KeyWidth);
        QCOMPARE(glided, QStringLiteral("to"));                     // the commoner reading
        QVERIFY2(engine.suggestions().contains(QStringLiteral("too")), qPrintable(engine.suggestions().join(',')));
        echo(engine, QStringLiteral("Hi. to "));                       // the client confirms (with the held space)
        QVERIFY2(engine.suggestions().contains(QStringLiteral("too")), "an echo does not drop the alternatives");
        engine.chooseSuggestion(QStringLiteral("too"));
        QCOMPARE(backend.commits.last(), QStringLiteral("too"));
        QVERIFY2(backend.deletions.contains(QStringLiteral("to")), qPrintable(backend.events.join(' ')));
        QCOMPARE(backend.preedit, QStringLiteral(" "));            // the space is still held
        QVERIFY(!engine.suggestions().contains(QStringLiteral("to")));
        // Typing moves on: the alternatives of the next glide do not linger.
        engine.endGlidePath(GlidePaths::pathFor(QStringLiteral("too"), centres, 0.0, 3), centres, GlidePaths::KeyWidth);
        engine.typeLetter(QStringLiteral("a"));
        QVERIFY(!engine.suggestions().contains(QStringLiteral("too")));
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
    }

    void oneBackspaceErasesTheWholeGlidedWord()
    {
        // Gboard: "When you're swiping and a word appears that isn't what you
        // actually wanted, tap Backspace once. That'll erase the entire word."
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(":/tastra/frequency")});
        const auto centres = GlidePaths::centresFor(QStringLiteral("en"));
        for (const bool preedit : {true, false}) {
            FakeBackend backend;
            Tastra::KeyboardController controller(backend);
            Tastra::TypingEngine engine(controller);
            engine.setLanguage(QStringLiteral("en"));
            QVERIFY(engine.waitForDictionaryForTesting(10000));
            backend.textChannel = true;
            backend.preeditSupport = preedit;
            echo(engine, QStringLiteral("Hi. "));
            engine.setAutoCapitalizationEnabled(false);
            QCOMPARE(engine.endGlidePath(GlidePaths::pathFor(QStringLiteral("hello"), centres, 0.0, 5), centres,
                                         GlidePaths::KeyWidth), QStringLiteral("hello"));
            backend.events.clear();
            engine.backspace();
            QCOMPARE(backend.deletions.value(0), preedit ? QStringLiteral("hello") : QStringLiteral("hello "));
            QCOMPARE(backend.preedit, QString());
            QCOMPARE(backend.backspaces, 0);
            QVERIFY(engine.previousWord().isEmpty() || engine.previousWord() != QStringLiteral("hello"));
            // The next Backspace is an ordinary one again.
            engine.backspace();
            QCOMPARE(backend.deletions.size(), 1);
        }
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
    }

    void shiftAndCapsLockApplyToTheGlidedWord()
    {
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(":/tastra/frequency")});
        const auto centres = GlidePaths::centresFor(QStringLiteral("en"));
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        engine.setLanguage(QStringLiteral("en"));
        QVERIFY(engine.waitForDictionaryForTesting(10000));
        composing(backend, engine);
        engine.setAutoCapitalizationEnabled(false);
        const auto path = GlidePaths::pathFor(QStringLiteral("too"), centres, 0.0, 3);
        QCOMPARE(engine.endGlidePath(path, centres, GlidePaths::KeyWidth, Tastra::TypingEngine::GlideCase::Capitalized),
                 QStringLiteral("To"));
        QVERIFY2(engine.suggestions().contains(QStringLiteral("Too")), qPrintable(engine.suggestions().join(',')));
        engine.space();
        QCOMPARE(engine.endGlidePath(path, centres, GlidePaths::KeyWidth, Tastra::TypingEngine::GlideCase::AllCaps),
                 QStringLiteral("TO"));
        QVERIFY2(engine.suggestions().contains(QStringLiteral("TOO")), qPrintable(engine.suggestions().join(',')));
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
    }

    void glideAlternativeAlsoReplacesWithoutAPreedit()
    {
        // Clients without preedit get "to " committed at once; the
        // alternative replaces the word and its space on the text channel.
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(":/tastra/frequency")});
        FakeBackend backend;
        backend.textChannel = true;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        engine.setLanguage(QStringLiteral("en"));
        QVERIFY(engine.waitForDictionaryForTesting(10000));
        echo(engine, QStringLiteral("Hi. "));
        engine.setAutoCapitalizationEnabled(false);
        const auto centres = GlidePaths::centresFor(QStringLiteral("en"));
        QCOMPARE(engine.endGlidePath(GlidePaths::pathFor(QStringLiteral("too"), centres, 0.0, 3), centres, GlidePaths::KeyWidth),
                 QStringLiteral("to"));
        QCOMPARE(backend.commits.join(QString()), QStringLiteral("to "));
        engine.chooseSuggestion(QStringLiteral("too"));
        QCOMPARE(backend.deletions.value(0), QStringLiteral("to "));
        QCOMPARE(backend.commits.last(), QStringLiteral("too "));
        QCOMPARE(backend.backspaces, 0);
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
    }

    void glidedWordBehavesLikeASuggestion()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        composing(backend, engine);
        engine.setAutoCapitalizationEnabled(false);

        engine.beginGlide(QStringLiteral("h"));
        for (const QString &k : {QStringLiteral("e"), QStringLiteral("l"), QStringLiteral("o")}) engine.glideThrough(k);
        QCOMPARE(engine.endGlide(), QStringLiteral("hello"));
        QCOMPARE(backend.commits, QStringList({QStringLiteral("hello")}));
        QCOMPARE(backend.preedit, QStringLiteral(" "));     // auto space, held
        engine.space();                                     // confirms it, no period
        engine.typeLetter(QStringLiteral("w"));
        QCOMPARE(backend.commits, QStringList({QStringLiteral("hello"), QStringLiteral(" ")}));
    }

    void dictationCommitsPendingWordThenSentenceCasedText()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        composing(backend, engine);

        engine.insertDictation(QStringLiteral("привет как дела."));
        QCOMPARE(backend.commits, QStringList({QStringLiteral("Привет как дела. ")}));
        QVERIFY(engine.wantsAutoUppercase());               // ended a sentence

        engine.typeLetter(QStringLiteral("o"));
        engine.typeLetter(QStringLiteral("k"));
        engine.insertDictation(QStringLiteral("and more"));
        // The unfinished word is committed first, then a separating space.
        // (Upper-casing typed letters is the bridge's job; the engine got "o".)
        QCOMPARE(backend.commits.mid(1), QStringList({QStringLiteral("ok"), QStringLiteral(" and more ")}));
        QVERIFY(!engine.wantsAutoUppercase());
        QCOMPARE(engine.previousWord(), QStringLiteral("more"));
        // A sentence ending inside quotes, or with an ellipsis, ends it too.
        engine.insertDictation(QStringLiteral("he said \"stop.\""));
        QVERIFY(engine.wantsAutoUppercase());
        QVERIFY(engine.previousWord().isEmpty());
        engine.insertDictation(QStringLiteral("well…"));
        QVERIFY(engine.previousWord().isEmpty());
    }

    void autoSpaceAfterPunctuationIsOptInLikeGboard()
    {
        for (const bool composition : {false, true}) {
            FakeBackend backend;
            Tastra::KeyboardController controller(backend);
            Tastra::TypingEngine engine(controller);
            if (composition) composing(backend, engine);
            engine.setAutoCapitalizationEnabled(false);

            engine.typeLetter(QStringLiteral("h"));
            engine.typeText(QStringLiteral(","));
            engine.typeLetter(QStringLiteral("o"));                // default: off
            QVERIFY(!backend.events.join(QString()).contains(QStringLiteral(", ")));

            engine.setAutoSpaceAfterPunctuation(true);
            engine.space();
            engine.typeLetter(QStringLiteral("k"));
            engine.typeText(QStringLiteral("."));
            engine.typeLetter(QStringLiteral("n"));
            engine.typeText(QStringLiteral("3"));
            engine.typeText(QStringLiteral("."));                  // "3." is a number:
            engine.typeText(QStringLiteral("5"));                  // no space inside "3.5"
            const QString all = backend.commits.join(QString()) + backend.preedit;
            QVERIFY2(all.contains(QStringLiteral("k. n")), qPrintable(all));
            QVERIFY2(all.contains(QStringLiteral("3.5")), qPrintable(all));
        }
    }

    void spaceBeforePunctuationIsSwappedLikeLatinIME()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        composing(backend, engine);
        engine.setAutoCapitalizationEnabled(false);
        engine.setAutocorrectEnabled(false);

        for (const QChar ch : QStringLiteral("hi")) engine.typeLetter(QString(ch));
        engine.space();
        engine.typeText(QStringLiteral(","));        // "hi ," -> "hi, "
        QCOMPARE(backend.commits.join(QString()) + backend.preedit, QStringLiteral("hi, "));
        engine.space();                              // absorbed: still one space
        engine.typeLetter(QStringLiteral("x"));
        QCOMPARE(backend.commits.join(QString()) + backend.preedit, QStringLiteral("hi, x"));

        // LatinIME revertSwapPunctuation: Backspace right after the swap
        // restores what was typed ("hi, " -> "hi ,"), without deletions.
        engine.space();
        engine.typeLetter(QStringLiteral("y"));
        engine.space();
        engine.typeText(QStringLiteral(","));
        QCOMPARE(backend.commits.join(QString()) + backend.preedit, QStringLiteral("hi, x y, "));
        engine.backspace();
        QCOMPARE(backend.commits.join(QString()) + backend.preedit, QStringLiteral("hi, x y ,"));
        QCOMPARE(backend.backspaces, 0);
        engine.typeLetter(QStringLiteral("z"));
        QCOMPARE(backend.commits.join(QString()) + backend.preedit, QStringLiteral("hi, x y ,z"));

        // Consecutive spaces are not a "space after a word": no swap.
        engine.setDoubleSpacePeriodEnabled(false);
        engine.space();
        engine.space();
        engine.typeText(QStringLiteral("."));
        QCOMPARE(backend.commits.join(QString()) + backend.preedit, QStringLiteral("hi, x y ,z  ."));
    }

    void clearingTheFieldReArmsCapitalisation()
    {
        for (const bool composition : {false, true}) {
            FakeBackend backend;
            Tastra::KeyboardController controller(backend);
            Tastra::TypingEngine engine(controller);
            if (composition) composing(backend, engine);
            else echo(engine, QString());          // empty field known
            engine.typeLetter(QStringLiteral("H"));
            engine.typeLetter(QStringLiteral("i"));
            engine.space();
            QVERIFY(!engine.wantsAutoUppercase());
            for (int i = 0; i < 3; ++i) engine.backspace();   // field is empty again
            QVERIFY2(engine.wantsAutoUppercase(), composition ? "composition" : "immediate");
        }
    }

    // Rules from AOSP LatinIME InputLogic.tryPerformDoubleSpacePeriod /
    // RichInputConnection.revertDoubleSpacePeriod (Apache-2.0).
    void doubleSpacePeriodFollowsLatinIME()
    {
        auto visible = [](FakeBackend &b) { return b.commits.join(QString()) + b.preedit; };
        {   // two quick spaces after a word -> ". "
            FakeBackend backend; Tastra::KeyboardController controller(backend);
            Tastra::TypingEngine engine(controller); composing(backend, engine);
            qint64 now = 1000; engine.setClockForTesting([&now] { return now; });
            engine.setAutoCapitalizationEnabled(false); engine.setAutocorrectEnabled(false);
            engine.typeLetter(QStringLiteral("h")); engine.typeLetter(QStringLiteral("i"));
            engine.space(); now += 300; engine.space();
            QCOMPARE(visible(backend), QStringLiteral("hi. "));
            // Backspace right after it: ". " -> " ", and no capital next.
            engine.backspace();
            QCOMPARE(visible(backend), QStringLiteral("hi "));
            QVERIFY(!engine.wantsAutoUppercase());
            engine.typeLetter(QStringLiteral("x"));
            QCOMPARE(visible(backend), QStringLiteral("hi x"));
        }
        {   // the second space comes later than 1100 ms -> just a space
            FakeBackend backend; Tastra::KeyboardController controller(backend);
            Tastra::TypingEngine engine(controller); composing(backend, engine);
            qint64 now = 1000; engine.setClockForTesting([&now] { return now; });
            engine.setAutoCapitalizationEnabled(false); engine.setAutocorrectEnabled(false);
            engine.typeLetter(QStringLiteral("h")); engine.typeLetter(QStringLiteral("i"));
            engine.space(); now += 1500; engine.space();
            QCOMPARE(visible(backend), QStringLiteral("hi  "));
        }
        {   // after punctuation there is no second period
            FakeBackend backend; Tastra::KeyboardController controller(backend);
            Tastra::TypingEngine engine(controller); composing(backend, engine);
            qint64 now = 1000; engine.setClockForTesting([&now] { return now; });
            engine.setAutoCapitalizationEnabled(false); engine.setAutocorrectEnabled(false);
            engine.typeLetter(QStringLiteral("h")); engine.typeLetter(QStringLiteral("i"));
            engine.typeText(QStringLiteral("."));
            engine.space(); now += 200; engine.space();
            QCOMPARE(visible(backend), QStringLiteral("hi.  "));
        }
    }

    void touchOffsetsTravelWithTheWord()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        composing(backend, engine);
        engine.setAutoCapitalizationEnabled(false);
        engine.setKeyboardRows({QStringLiteral("qwertyuiop"), QStringLiteral("asdfghjkl"), QStringLiteral("zxcvbnm")});
        engine.addUserWord(QStringLiteral("bat"));
        engine.addUserWord(QStringLiteral("bet"));
        engine.typeLetter(QStringLiteral("b"));
        engine.typeLetter(QStringLiteral("s"), QPointF(0.30, -0.42));
        engine.typeLetter(QStringLiteral("t"));
        QCOMPARE(engine.suggestions().value(0), QStringLiteral("bet"));
        engine.backspace();                                   // offsets shrink with the word
        engine.typeLetter(QStringLiteral("t"));
        QCOMPARE(engine.suggestions().value(0), QStringLiteral("bet"));
    }

    void spaceAfterSuggestionIsNotADoubleSpace()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        composing(backend, engine);

        engine.typeLetter(QStringLiteral("h"));
        engine.chooseSuggestion(QStringLiteral("hello"));
        // Live report (0.2.4): tap suggestion, then Space -> "hello. ".
        // The suggestion's own space must not count as the user's first Space.
        engine.space();
        QCOMPARE(backend.commits, QStringList({QStringLiteral("hello")}));
        QCOMPARE(backend.preedit, QStringLiteral(" "));
        engine.typeLetter(QStringLiteral("w"));
        QCOMPARE(backend.commits, QStringList({QStringLiteral("hello"), QStringLiteral(" ")}));

        // A genuine second Space still produces the period.
        engine.chooseSuggestion(QStringLiteral("world"));
        engine.space();
        engine.space();
        QCOMPARE(backend.preedit, QStringLiteral(". "));
    }

    void compositionBackspaceEditsPreeditFirst()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        composing(backend, engine);

        engine.typeLetter(QStringLiteral("h"));
        engine.typeLetter(QStringLiteral("e"));
        engine.backspace();
        QCOMPARE(backend.preedit, QStringLiteral("h"));
        QCOMPARE(engine.currentWord(), QStringLiteral("h"));
        engine.backspace();
        QCOMPARE(backend.preedit, QString());
        QCOMPARE(backend.backspaces, 0);
        engine.backspace();                                  // now a real key
        QCOMPARE(backend.backspaces, 1);
    }

    void compositionEnterCommitsWordBeforeReturn()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        composing(backend, engine);

        engine.typeLetter(QStringLiteral("o"));
        engine.typeLetter(QStringLiteral("k"));
        engine.enter();
        // commit first, key second: GTK applies text-input commits before
        // queued key events, so this order is never inverted.
        QCOMPARE(backend.events.mid(backend.events.indexOf(QStringLiteral("commit:ok"))),
                 QStringList({QStringLiteral("commit:ok"), QStringLiteral("enter")}));
    }

    void compositionIgnoresEchoesAndClientResetDropsIt()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        composing(backend, engine);

        engine.typeLetter(QStringLiteral("w"));
        engine.typeLetter(QStringLiteral("o"));
        echo(engine, QStringLiteral("wo"));     // client includes its preedit
        QCOMPARE(engine.currentWord(), QStringLiteral("wo"));
        engine.resetComposition();              // client reset: it owns the text now
        QCOMPARE(engine.currentWord(), QString());
        QVERIFY(!backend.commits.contains(QStringLiteral("wo")));
    }

    void secureFieldsNeverUsePreedit()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        composing(backend, engine);
        engine.setSensitiveContext(true);
        echo(engine, QString());

        engine.typeLetter(QStringLiteral("s"));
        QCOMPARE(backend.commits, QStringList({QStringLiteral("s")}));
        QVERIFY(!backend.events.contains(QStringLiteral("pre:s")));
    }

    void clientsWithoutTextInputKeepImmediateCommits()
    {
        FakeBackend backend;
        backend.preeditSupport = true;          // backend could, client cannot
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);

        engine.typeLetter(QStringLiteral("a"));
        QCOMPARE(backend.commits, QStringList({QStringLiteral("a")}));
    }

    void oneStepStaleEchoesDoNotFlipCaseOrWord()
    {
        FakeBackend backend;
        backend.textChannel = true;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);
        echo(engine, QStringLiteral("hi. "));          // field content on focus
        // Firefox-like: each commit is followed by an echo of the state
        // *before* that commit (GTK re-reads immediately, content lags).
        engine.typeLetter(QStringLiteral("J")); echo(engine, QStringLiteral("hi. "));
        QCOMPARE(engine.currentWord(), QStringLiteral("J"));
        QVERIFY(!engine.wantsAutoUppercase());
        engine.typeLetter(QStringLiteral("o")); echo(engine, QStringLiteral("hi. J"));
        QCOMPARE(engine.currentWord(), QStringLiteral("Jo"));
        QVERIFY(!engine.wantsAutoUppercase());
    }

    void externalCursorMoveAdoptsClientState()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);

        qint64 now = 0;
        engine.setClockForTesting([&now] { return now; });
        engine.typeLetter(QStringLiteral("h"));
        engine.typeLetter(QStringLiteral("e"));
        engine.typeLetter(QStringLiteral("l"));
        // The user tapped elsewhere in the field (at human speed): the client
        // reports a state the keyboard never produced. It must be adopted,
        // otherwise a suggestion tap would replace the wrong text.
        now += 1000;
        echo(engine, QStringLiteral("hello wor"));
        QCOMPARE(engine.currentWord(), QStringLiteral("wor"));
        QCOMPARE(engine.previousWord(), QStringLiteral("hello"));
    }

    void autocorrectUsesTextChannelOnceClientConfirmedWord()
    {
        FakeBackend backend;
        backend.textChannel = true;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);

        engine.typeLetter(QStringLiteral("t")); echo(engine, QStringLiteral("t"));
        engine.typeLetter(QStringLiteral("e")); echo(engine, QStringLiteral("te"));
        engine.typeLetter(QStringLiteral("h")); echo(engine, QStringLiteral("teh"));
        engine.space();

        // Delete + insert travel on the same text-input channel, so clients
        // that apply text-input events before queued key events (GTK/Firefox)
        // cannot reorder them. No Backspace key events are involved.
        QCOMPARE(backend.backspaces, 0);
        QCOMPARE(backend.deletions, QStringList({QStringLiteral("teh")}));
        QCOMPARE(backend.commits.mid(3), QStringList({QStringLiteral("the"), QStringLiteral(" ")}));
    }

    void unconfirmedWordIsNotRewrittenOnTextInputClients()
    {
        FakeBackend backend;
        backend.textChannel = true;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);

        engine.typeLetter(QStringLiteral("t")); echo(engine, QStringLiteral("t"));
        engine.typeLetter(QStringLiteral("e"));
        engine.typeLetter(QStringLiteral("h"));
        engine.space();

        // The client has not confirmed "teh" yet; deleting now could remove
        // the wrong text. Correctness beats an autocorrection.
        QCOMPARE(backend.backspaces, 0);
        QVERIFY(backend.deletions.isEmpty());
        QCOMPARE(backend.commits, QStringList({QStringLiteral("t"), QStringLiteral("e"), QStringLiteral("h"), QStringLiteral(" ")}));
    }

    void suggestionAndDoubleSpaceUseTextChannel()
    {
        FakeBackend backend;
        backend.textChannel = true;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);

        engine.typeLetter(QStringLiteral("h")); echo(engine, QStringLiteral("h"));
        engine.typeLetter(QStringLiteral("e")); echo(engine, QStringLiteral("he"));
        engine.typeLetter(QStringLiteral("l")); echo(engine, QStringLiteral("hel"));
        engine.chooseSuggestion(QStringLiteral("hello"));
        echo(engine, QStringLiteral(""));
        echo(engine, QStringLiteral("hello"));
        echo(engine, QStringLiteral("hello "));
        // Space right after a suggestion only confirms its space (0.2.4 live
        // report); the next Space is the double-space.
        engine.space();
        QCOMPARE(backend.commits.mid(3), QStringList({QStringLiteral("hello"), QStringLiteral(" ")}));
        engine.space();

        QCOMPARE(backend.backspaces, 0);
        QCOMPARE(backend.deletions, QStringList({QStringLiteral("hel"), QStringLiteral(" ")}));
        QCOMPARE(backend.commits.mid(3), QStringList({QStringLiteral("hello"), QStringLiteral(" "), QStringLiteral(". ")}));
    }

    void stalePreSpaceEchoDoesNotReopenCommittedWord()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);

        engine.typeLetter(QStringLiteral("h"));
        engine.typeLetter(QStringLiteral("i"));
        engine.space();
        QCOMPARE(engine.currentWord(), QString());

        QVERIFY(!engine.syncSurroundingText(QStringLiteral("hi"), 2, 2));
        QCOMPARE(engine.currentWord(), QString());

        const QString committed = QStringLiteral("hi ");
        engine.syncSurroundingText(committed, committed.toUtf8().size(), committed.toUtf8().size());
        QCOMPARE(engine.currentWord(), QString());
    }

    void terminalPunctuationImmediatelyArmsCapitalization()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);

        engine.typeLetter(QStringLiteral("H"));
        engine.typeLetter(QStringLiteral("i"));
        engine.typeText(QStringLiteral("."));
        QVERIFY(engine.wantsAutoUppercase());
    }


    void repeatedBackspaceUpdatesCompositionInOneOperation()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);

        engine.typeLetter(QStringLiteral("h"));
        engine.typeLetter(QStringLiteral("e"));
        engine.typeLetter(QStringLiteral("l"));
        engine.typeLetter(QStringLiteral("l"));
        engine.typeLetter(QStringLiteral("o"));
        engine.backspaceRepeated(3);

        QCOMPARE(backend.backspaces, 3);
        QCOMPARE(engine.currentWord(), QStringLiteral("he"));
    }

    void glideDecoderCanResolveSimpleTrace()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::TypingEngine engine(controller);

        engine.beginGlide(QStringLiteral("h"));
        engine.glideThrough(QStringLiteral("e"));
        engine.glideThrough(QStringLiteral("l"));
        engine.glideThrough(QStringLiteral("l"));
        engine.glideThrough(QStringLiteral("o"));
        QCOMPARE(engine.endGlide().toLower(), QStringLiteral("hello"));
        QCOMPARE(backend.commits.last(), QStringLiteral(" "));
    }
};

QTEST_APPLESS_MAIN(SmartTypingTest)
#include "tst_smarttyping.moc"
