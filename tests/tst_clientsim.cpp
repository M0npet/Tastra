// SPDX-License-Identifier: GPL-3.0-or-later

// The typing engine against a modelled GTK 3 / Firefox text field behind
// KWin (tests/textinputclient.h): what the user finally sees in the field.

#include <QtTest/QTest>

#include <QCoreApplication>
#include <QRandomGenerator>
#include <QSettings>
#include <QStandardPaths>

#include "core/keyboardcontroller.h"
#include "core/locallexicon.h"
#include "core/typingengine.h"
#include "core/voicecommands.h"
#include "glidepaths.h"
#include "textinputclient.h"

namespace
{

// One keyboard + one field, with a clock the engine and the client share.
struct Session {
    explicit Session(TextInputClient::Echo echo = TextInputClient::Echo::Current)
        : client(echo), controller(client), engine(controller)
    {
        engine.setClockForTesting([this] { return now; });
        engine.setLanguage(QStringLiteral("en"));
        client.engine = &engine;
        client.advance = [this](qint64 ms) { now += ms; };
        // A field that reports its text, as text-input-v3 clients do.
        client.setText(QString());
    }

    // A user action, then the client's answer, then a human pause.
    template<typename F>
    void act(F &&f)
    {
        f();
        client.deliver();
        now += 180;
    }
    void type(const QString &letters)
    {
        for (const QChar ch : letters) {
            if (ch == QLatin1Char(' ')) act([&] { engine.space(); });
            else act([&] { engine.typeLetter(QString(ch)); });
        }
    }

    qint64 now = 100000;
    TextInputClient client;
    Tastra::KeyboardController controller;
    Tastra::TypingEngine engine;
};

}

class ClientSimTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("TastraTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_clientsim"));
        Tastra::LocalLexicon::setDictionarySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        Tastra::LocalLexicon::setBlocklistSearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        Tastra::LocalLexicon::setUserDictionaryFile(QStringLiteral(TASTRA_TEST_DATA "/empty/none.txt"));
        Tastra::LocalLexicon::setBigramSearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
    }

    void init() { QSettings().clear(); }

    void echoModes_data()
    {
        QTest::addColumn<int>("echo");
        QTest::newRow("gtk") << int(TextInputClient::Echo::Current);
        QTest::newRow("firefox") << int(TextInputClient::Echo::Lagging);
    }

    void plainTypingAndAutocorrect_data() { echoModes_data(); }
    void plainTypingAndAutocorrect()
    {
        QFETCH(int, echo);
        Session s{static_cast<TextInputClient::Echo>(echo)};
        s.engine.setAutoCapitalizationEnabled(false);
        s.type(QStringLiteral("teh cat "));
        QCOMPARE(s.client.visible(), QStringLiteral("the cat "));
        QCOMPARE(s.client.badDeletes, 0);
    }

    // Tapping at the end of a word and typing on: the word is resumed as
    // the word being typed, so a suggestion replaces all of it (LatinIME
    // restartSuggestionsOnWordTouchedByCursor; Gboard underlines it again).
    void aSuggestionAfterTypingOnAnOldWordReplacesItInOrder_data() { echoModes_data(); }
    void aSuggestionAfterTypingOnAnOldWordReplacesItInOrder()
    {
        QFETCH(int, echo);
        Session s{static_cast<TextInputClient::Echo>(echo)};
        s.client.setText(QStringLiteral("I need hel"));
        s.act([&] { s.engine.typeLetter(QStringLiteral("p")); });
        QCOMPARE(s.engine.currentWord(), QStringLiteral("help"));
        s.act([&] { s.engine.chooseSuggestion(QStringLiteral("helpful")); });
        s.act([&] { s.engine.space(); });
        QCOMPARE(s.client.visible(), QStringLiteral("I need helpful "));
        QCOMPARE(s.client.badDeletes, 0);
    }

    void aCorrectionAfterTypingOnAnOldWordReplacesItInOrder_data() { echoModes_data(); }
    void aCorrectionAfterTypingOnAnOldWordReplacesItInOrder()
    {
        QFETCH(int, echo);
        Session s{static_cast<TextInputClient::Echo>(echo)};
        s.client.setText(QStringLiteral("hel"));
        s.act([&] { s.engine.typeLetter(QStringLiteral("o")); });
        s.act([&] { s.engine.chooseSuggestion(QStringLiteral("hello")); });
        QCOMPARE(s.client.visible().trimmed(), QStringLiteral("hello"));
        QCOMPARE(s.client.badDeletes, 0);
    }

    // "hello", Space, Backspace, "s": one word "hellos", as in Gboard; the
    // "s" alone is never autocorrected.
    void backspaceIntoAWordContinuesIt_data() { echoModes_data(); }
    void backspaceIntoAWordContinuesIt()
    {
        QFETCH(int, echo);
        Session s{static_cast<TextInputClient::Echo>(echo)};
        s.engine.setAutoCapitalizationEnabled(false);
        s.type(QStringLiteral("cat "));
        s.act([&] { s.engine.backspace(); });
        QCOMPARE(s.client.visible(), QStringLiteral("cat"));
        QCOMPARE(s.engine.currentWord(), QStringLiteral("cat"));
        s.type(QStringLiteral("s "));
        QCOMPARE(s.client.visible(), QStringLiteral("cats "));
        QCOMPARE(s.client.badDeletes, 0);
    }
    // With composition turned off the letters are committed one by one; a
    // suggestion tap before the client confirmed the word used Backspace
    // keys, which GTK and Firefox apply after the commit sent with them.
    void aSuggestionTapWithoutCompositionReplacesTheWordInOrder_data() { echoModes_data(); }
    void aSuggestionTapWithoutCompositionReplacesTheWordInOrder()
    {
        QFETCH(int, echo);
        Session s{static_cast<TextInputClient::Echo>(echo)};
        s.engine.setCompositionEnabled(false);
        s.engine.setAutoCapitalizationEnabled(false);
        s.type(QStringLiteral("we hepl"));
        s.act([&] { s.engine.chooseSuggestion(QStringLiteral("help")); });
        QCOMPARE(s.client.visible(), QStringLiteral("we help "));
        // The last letter's answer has not reached the keyboard yet.
        s.type(QStringLiteral("yu"));
        s.engine.typeLetter(QStringLiteral("o"));
        s.client.deliver(false);
        s.now += 200;
        s.act([&] { s.engine.chooseSuggestion(QStringLiteral("you")); });
        QCOMPARE(s.client.visible(), QStringLiteral("we help you "));
        QCOMPARE(s.client.badDeletes, 0);
    }

    // A word picked from the strip in the middle of the text, where a space
    // or punctuation already follows: no second space (the replaced word's
    // path did this already), so nothing is left over when KWin commits the
    // keyboard's preedit on the next tap.
    void aSuggestionBeforeASpaceAddsNoSecondSpace_data()
    {
        QTest::addColumn<int>("echo");
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("at");
        QTest::addColumn<QString>("typed");
        QTest::addColumn<QString>("chosen");
        QTest::addColumn<QString>("expected");
        for (int echo : {int(TextInputClient::Echo::Current), int(TextInputClient::Echo::Lagging)}) {
            const char *mode = echo == int(TextInputClient::Echo::Current) ? "gtk" : "firefox";
            QTest::addRow("%s before a space", mode) << echo << QStringLiteral("I dogs") << 1 << QStringLiteral(" lov")
                                                     << QStringLiteral("love") << QStringLiteral("I love dogs");
            QTest::addRow("%s before a period", mode) << echo << QStringLiteral("I need.") << 6 << QStringLiteral(" hel")
                                                      << QStringLiteral("help") << QStringLiteral("I need help.");
            QTest::addRow("%s at the end", mode) << echo << QStringLiteral("I need") << 6 << QStringLiteral(" hel")
                                                 << QStringLiteral("help") << QStringLiteral("I need help ");
            // A word the cursor was put after, replaced from the strip.
            QTest::addRow("%s old word", mode) << echo << QStringLiteral("I like teh dogs") << 10 << QString()
                                               << QStringLiteral("the") << QStringLiteral("I like the dogs");
        }
    }
    void aSuggestionBeforeASpaceAddsNoSecondSpace()
    {
        QFETCH(int, echo);
        QFETCH(QString, text);
        QFETCH(int, at);
        QFETCH(QString, typed);
        QFETCH(QString, chosen);
        QFETCH(QString, expected);
        Session s{static_cast<TextInputClient::Echo>(echo)};
        s.engine.setAutoCapitalizationEnabled(false);
        s.client.setText(text);
        s.client.placeCursor(at);
        s.type(typed);
        s.act([&] { s.engine.chooseSuggestion(chosen); });
        s.client.tapAtEnd();
        QCOMPARE(s.client.visible(), expected);
        QCOMPARE(s.client.badDeletes, 0);
    }

    // Gboard Smart Compose as a chip: a habit of the user's typed three
    // times, then offered while typing and after a Space; in the middle of
    // the text no second space is left before what follows.
    void aPhraseFromTheStripFitsIn_data()
    {
        QTest::addColumn<int>("echo");
        QTest::addColumn<bool>("composition");
        for (int echo : {int(TextInputClient::Echo::Current), int(TextInputClient::Echo::Lagging)}) {
            const char *mode = echo == int(TextInputClient::Echo::Current) ? "gtk" : "firefox";
            QTest::addRow("%s composing", mode) << echo << true;
            QTest::addRow("%s plain", mode) << echo << false;
        }
    }
    void aPhraseFromTheStripFitsIn()
    {
        QFETCH(int, echo);
        QFETCH(bool, composition);
        Session s{static_cast<TextInputClient::Echo>(echo)};
        s.engine.clearLearning();
        s.engine.setCompositionEnabled(composition);
        s.engine.setAutoCapitalizationEnabled(false);
        s.engine.setAutocorrectEnabled(false);
        for (int i = 0; i < 3; ++i) {
            s.type(QStringLiteral("see you soon"));
            s.act([&] { s.engine.typeText(QStringLiteral(".")); });
            s.act([&] { s.engine.space(); });
        }
        s.type(QStringLiteral("se"));
        QCOMPARE(s.engine.phraseSuggestion(), QStringLiteral("see you soon"));
        s.act([&] { s.engine.chooseSuggestion(s.engine.phraseSuggestion()); });
        s.client.tapAtEnd();
        QCOMPARE(s.client.visible(), QStringLiteral("see you soon. see you soon. see you soon. see you soon "));
        QCOMPARE(s.client.badDeletes, 0);

        s.client.setText(QStringLiteral("I will ."));
        s.client.placeCursor(7);
        s.type(QStringLiteral("see "));
        QCOMPARE(s.engine.phraseSuggestion(), QStringLiteral("you soon"));
        s.act([&] { s.engine.chooseSuggestion(s.engine.phraseSuggestion()); });
        s.client.tapAtEnd();
        QCOMPARE(s.client.visible(), QStringLiteral("I will see you soon."));
        QCOMPARE(s.client.badDeletes, 0);

        s.client.setText(QStringLiteral("I will ."));
        s.client.placeCursor(7);
        s.type(QStringLiteral("se"));
        s.act([&] { s.engine.chooseSuggestion(s.engine.phraseSuggestion()); });
        s.client.tapAtEnd();
        QCOMPARE(s.client.visible(), QStringLiteral("I will see you soon."));
        QCOMPARE(s.client.badDeletes, 0);
    }

    // The same for a glide in the middle of the text: a space before it
    // (after "I"), none after it (" dogs" follows); Backspace right after
    // the glide erases exactly the glided word.
    void aGlideInTheMiddleOfTheTextFitsIn_data() { echoModes_data(); }
    void aGlideInTheMiddleOfTheTextFitsIn()
    {
        QFETCH(int, echo);
        Session s{static_cast<TextInputClient::Echo>(echo)};
        s.engine.setAutoCapitalizationEnabled(false);
        s.client.setText(QStringLiteral("I dogs"));
        s.client.placeCursor(1);
        auto glide = [&] {
            s.act([&] {
                s.engine.beginGlide(QStringLiteral("t"));
                s.engine.glideThrough(QStringLiteral("h"));
                s.engine.glideThrough(QStringLiteral("e"));
                QVERIFY(!s.engine.endGlide().isEmpty());
            });
        };
        glide();
        QCOMPARE(s.client.visible(), QStringLiteral("I the dogs"));
        s.act([&] { s.engine.backspace(); });
        QCOMPARE(s.client.visible(), QStringLiteral("I  dogs"));
        s.client.tapAtEnd();
        QCOMPARE(s.client.text, QStringLiteral("I  dogs"));
        QCOMPARE(s.client.badDeletes, 0);
    }

    // The other reading of a glide and one Backspace after it, in Firefox
    // too: both delete the glided word, which GTK must measure on the text
    // as it is, not as Firefox's cache last had it.
    void aGlidedWordCanBeSwappedAndErased_data()
    {
        QTest::addColumn<int>("echo");
        QTest::addColumn<bool>("underline");
        for (const bool underline : {true, false}) {
            QTest::addRow("gtk underline=%d", int(underline)) << int(TextInputClient::Echo::Current) << underline;
            QTest::addRow("firefox underline=%d", int(underline)) << int(TextInputClient::Echo::Lagging) << underline;
        }
    }
    void aGlidedWordCanBeSwappedAndErased()
    {
        QFETCH(int, echo);
        QFETCH(bool, underline);
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(":/tastra/frequency")});
        Session s{static_cast<TextInputClient::Echo>(echo)};
        QVERIFY(s.engine.waitForDictionaryForTesting(10000));
        s.engine.setAutoCapitalizationEnabled(false);
        s.engine.setCompositionEnabled(underline);
        // An empty field: Firefox's cache has no text at all yet when the
        // glided word is deleted again.
        s.client.setText(QString());
        const auto centres = GlidePaths::centresFor(QStringLiteral("en"));
        auto glide = [&] {
            s.act([&] {
                s.engine.endGlidePath(GlidePaths::pathFor(QStringLiteral("too"), centres, 0.0, 3), centres,
                                      GlidePaths::KeyWidth);
            });
        };
        glide();
        QCOMPARE(s.client.visible(), QStringLiteral("to "));
        QVERIFY2(s.engine.suggestions().contains(QStringLiteral("too")), qPrintable(s.engine.suggestions().join(',')));
        s.act([&] { s.engine.chooseSuggestion(QStringLiteral("too")); });
        QCOMPARE(s.client.visible(), QStringLiteral("too "));
        s.act([&] { s.engine.backspace(); });
        QCOMPARE(s.client.visible(), QString());
        glide();
        s.act([&] { s.engine.space(); });
        glide();
        s.act([&] { s.engine.backspace(); });
        QCOMPARE(s.client.visible(), QStringLiteral("to "));
        QCOMPARE(s.client.badDeletes, 0);
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
    }

    // Gboard voice commands on the text: dictation keeps its spaces, "clear"
    // takes the last sentence, "delete last word" the last word, "new line"
    // turns the space after the dictation into a line break.
    void voiceCommandsEditTheDictatedText_data() { echoModes_data(); }
    void voiceCommandsEditTheDictatedText()
    {
        QFETCH(int, echo);
        Session s{static_cast<TextInputClient::Echo>(echo)};
        s.act([&] { s.engine.insertDictation(QStringLiteral("Hello world.")); });
        s.act([&] { s.engine.insertDictation(QStringLiteral("How are you?")); });
        QCOMPARE(s.client.visible(), QStringLiteral("Hello world. How are you? "));
        s.act([&] { s.engine.deleteLastSentence(); });
        QCOMPARE(s.client.visible(), QStringLiteral("Hello world."));
        s.act([&] { s.engine.insertDictation(QStringLiteral("Bye now.")); });
        QCOMPARE(s.client.visible(), QStringLiteral("Hello world. Bye now. "));
        s.act([&] { s.engine.deleteLastWord(); });
        QCOMPARE(s.client.visible(), QStringLiteral("Hello world. Bye "));
        s.act([&] { s.engine.insertLineBreak(QStringLiteral("\n")); });
        QCOMPARE(s.client.visible(), QStringLiteral("Hello world. Bye\n"));
        s.act([&] { s.engine.insertDictation(QStringLiteral("next line")); });
        QCOMPARE(s.client.visible(), QStringLiteral("Hello world. Bye\nNext line "));
        s.act([&] { s.engine.deleteLastSentence(); });
        QCOMPARE(s.client.visible(), QStringLiteral("Hello world. Bye\n"));
        s.act([&] { s.engine.deleteKnownText(); });
        QCOMPARE(s.client.visible(), QString());
        QCOMPARE(s.client.badDeletes, 0);
    }

    // Gboard's detailed voice edits on the modelled clients: only the part
    // after the edit is retyped, nothing is left over or doubled.
    void voiceEditsChangeTheTextBeforeTheCursor_data() { echoModes_data(); }
    void voiceEditsChangeTheTextBeforeTheCursor()
    {
        QFETCH(int, echo);
        Session s{static_cast<TextInputClient::Echo>(echo)};
        auto edit = [&](const char *said) {
            s.act([&] {
                const auto edited = Tastra::applyVoiceEdit(s.engine.knownTextBeforeCursor(),
                                                           Tastra::parseVoiceCommand(QString::fromUtf8(said)));
                QVERIFY(edited);
                s.engine.replaceKnownText(*edited);
            });
        };
        s.act([&] { s.engine.insertDictation(QStringLiteral("See you tomorrow at the station.")); });
        edit("Change tomorrow to today.");
        QCOMPARE(s.client.visible(), QStringLiteral("See you today at the station. "));
        edit("Insert main before station");
        QCOMPARE(s.client.visible(), QStringLiteral("See you today at the main station. "));
        edit("Delete today");
        QCOMPARE(s.client.visible(), QStringLiteral("See you at the main station. "));
        // Typing goes on after it (capitals come from the keyboard's Shift state, not here).
        s.type(QStringLiteral("ok "));
        QCOMPARE(s.client.visible(), QStringLiteral("See you at the main station. ok "));
        s.act([&] { s.engine.insertDictation(QStringLiteral("bye now")); });
        edit("Capitalize now.");
        QCOMPARE(s.client.visible(), QStringLiteral("See you at the main station. ok bye Now "));
        QCOMPARE(s.client.badDeletes, 0);
    }

    // tst_typingstress against the modelled clients: Backspace keys and
    // text-input requests reach them on different channels, so any action
    // that mixes both in one go shows up here.
    void randomTypingOnRealClients_data() { echoModes_data(); }
    void randomTypingOnRealClients()
    {
        QFETCH(int, echo);
        for (quint32 seed = 1; seed <= 60; ++seed) {
            QRandomGenerator rng(seed * 104729u + quint32(echo));
            Session s{static_cast<TextInputClient::Echo>(echo)};
            s.engine.setAutocorrectEnabled(false);
            s.engine.setAutoCapitalizationEnabled(false);
            s.engine.setDoubleSpacePeriodEnabled(false);
            QString expected, log;
            int wordLength = 0;
            bool heldAfterWord = false, swapped = false, heldSpace = false, afterSuggestion = false;
            const QString letters = QStringLiteral("abcdeéжщї");
            for (int step = 0; step < 200; ++step) {
                const int op = rng.bounded(100);
                if (op < 58) {
                    const QString ch(letters.at(rng.bounded(letters.size())));
                    s.engine.typeLetter(ch);
                    expected += ch;
                    log += ch;
                    ++wordLength;
                    heldAfterWord = swapped = heldSpace = afterSuggestion = false;
                } else if (op < 62) {
                    s.engine.enter();
                    log += QStringLiteral("⏎");
                    if (heldSpace) expected.chop(1);
                    expected += QLatin1Char('\n');
                    wordLength = 0;
                    heldAfterWord = swapped = heldSpace = afterSuggestion = false;
                } else if (op < 78) {
                    s.engine.space();
                    log += QStringLiteral("␣");
                    if (afterSuggestion) {
                        afterSuggestion = false;          // confirms the suggestion's space
                    } else if (swapped) {
                        swapped = false;
                    } else {
                        expected += QLatin1Char(' ');
                        heldAfterWord = wordLength > 0;
                        heldSpace = wordLength > 0 || heldSpace;
                    }
                    wordLength = 0;
                } else if (op < 86) {
                    const QString p = QStringList{QStringLiteral(","), QStringLiteral("."), QStringLiteral("!")}.at(rng.bounded(3));
                    s.engine.typeText(p);
                    log += p;
                    if (heldAfterWord) {
                        expected.chop(1);
                        expected += p + QLatin1Char(' ');
                        swapped = heldSpace = true;
                    } else {
                        expected += p;
                        swapped = heldSpace = false;
                    }
                    heldAfterWord = afterSuggestion = false;
                    wordLength = 0;
                } else if (op >= 96 && !s.engine.currentWord().isEmpty()) {
                    // The word itself from the strip: replaced by itself, plus a space.
                    const QString word = s.engine.currentWord();
                    s.engine.chooseSuggestion(word);
                    log += QStringLiteral("[") + word + QStringLiteral("]");
                    expected += QLatin1Char(' ');
                    wordLength = 0;
                    heldAfterWord = heldSpace = afterSuggestion = true;
                    swapped = false;
                } else {
                    s.engine.backspace();
                    log += QStringLiteral("⌫");
                    if (swapped) {
                        const QChar punct = expected.at(expected.size() - 2);
                        expected.chop(2);
                        expected += QLatin1Char(' ');
                        expected += punct;
                    } else if (!expected.isEmpty()) {
                        expected.chop(1);
                    }
                    wordLength = 0;
                    while (wordLength < expected.size() && letters.contains(expected.at(expected.size() - 1 - wordLength)))
                        ++wordLength;
                    heldAfterWord = swapped = heldSpace = afterSuggestion = false;
                }
                s.client.deliver();
                s.now += 40 + rng.bounded(300);
                if (s.client.visible() != expected) qWarning() << "seed" << seed << log.right(40);
                QCOMPARE(s.client.visible(), expected);
                QCOMPARE(s.client.badDeletes, 0);
            }
        }
    }
    // Whole words from a small vocabulary, so habits form, and the phrase
    // chip taken whenever there is one: the field always shows what the
    // user meant, on both client models.
    void randomPhrasesOnRealClients_data() { echoModes_data(); }
    void randomPhrasesOnRealClients()
    {
        QFETCH(int, echo);
        int phrasesChosen = 0;
        for (quint32 seed = 1; seed <= 30; ++seed) {
            QRandomGenerator rng(seed * 7919u + quint32(echo));
            Session s{static_cast<TextInputClient::Echo>(echo)};
            s.engine.clearLearning();
            s.engine.setAutocorrectEnabled(false);
            s.engine.setAutoCapitalizationEnabled(false);
            s.engine.setDoubleSpacePeriodEnabled(false);
            const QStringList words = {QStringLiteral("see"), QStringLiteral("you"), QStringLiteral("soon"),
                                       QStringLiteral("go"), QStringLiteral("home")};
            QString expected, log;
            bool held = false, afterWord = false;     // held: a space Enter drops
            for (int step = 0; step < 150; ++step) {
                const int op = rng.bounded(100);
                const QString phrase = s.engine.phraseSuggestion();
                if (op < 30 && !phrase.isEmpty()) {
                    // While typing the phrase replaces the typed letters.
                    if (!s.engine.currentWord().isEmpty()) expected.chop(s.engine.currentWord().size());
                    s.engine.chooseSuggestion(phrase);
                    log += QStringLiteral("[") + phrase + QStringLiteral("]");
                    expected += phrase + QLatin1Char(' ');
                    held = afterWord = true;
                    ++phrasesChosen;
                } else if (op < 80) {
                    // A word; mostly the habitual order, so habits form.
                    const int at = rng.bounded(100) < 80 ? step % 3 : rng.bounded(words.size());
                    const QString word = words.at(at);
                    const int letters = 1 + rng.bounded(word.size());
                    for (int i = 0; i < letters; ++i) {
                        s.engine.typeLetter(QString(word.at(i)));
                        s.client.deliver();
                        s.now += 40 + rng.bounded(200);
                    }
                    log += word.left(letters);
                    expected += word.left(letters);
                    held = afterWord = false;
                    if (letters < word.size() && !s.engine.phraseSuggestion().isEmpty()) continue;   // take it next
                    for (int i = letters; i < word.size(); ++i) {
                        s.engine.typeLetter(QString(word.at(i)));
                        s.client.deliver();
                        s.now += 40 + rng.bounded(200);
                    }
                    expected += word.mid(letters);
                    log += word.mid(letters) + QStringLiteral("␣");
                    s.engine.space();
                    expected += QLatin1Char(' ');
                    held = afterWord = true;
                } else if (op < 90 && (afterWord || !s.engine.currentWord().isEmpty())) {
                    // A word, its space, then "." (the space moves after it).
                    if (!s.engine.currentWord().isEmpty()) {
                        s.engine.space();
                        expected += QLatin1Char(' ');
                    }
                    log += QStringLiteral(".");
                    s.client.deliver();
                    s.now += 120;
                    s.engine.typeText(QStringLiteral("."));
                    expected.chop(1);
                    expected += QStringLiteral(". ");
                    held = true;
                    afterWord = false;
                } else {
                    s.engine.enter();
                    log += QStringLiteral("⏎");
                    if (held) expected.chop(1);
                    expected += QLatin1Char('\n');
                    held = afterWord = false;
                }
                s.client.deliver();
                s.now += 40 + rng.bounded(300);
                if (s.client.visible() != expected) qWarning() << "seed" << seed << log.right(60) << s.client.visible().right(30) << expected.right(30);
                QCOMPARE(s.client.visible(), expected);
                QCOMPARE(s.client.badDeletes, 0);
            }
        }
        QVERIFY2(phrasesChosen > 50, qPrintable(QString::number(phrasesChosen)));
    }

    // At the start of an empty paragraph the first letter is committed (rich
    // editors break a composition there); a suggestion that changes it
    // replaces it too instead of typing after it ("ythe").
    void aSuggestionCanChangeTheFirstLetterOfAParagraph_data() { echoModes_data(); }
    void aSuggestionCanChangeTheFirstLetterOfAParagraph()
    {
        QFETCH(int, echo);
        Session s{static_cast<TextInputClient::Echo>(echo)};
        s.engine.setAutoCapitalizationEnabled(false);
        s.type(QStringLiteral("yhe"));
        s.act([&] { s.engine.chooseSuggestion(QStringLiteral("the")); });
        QCOMPARE(s.client.visible(), QStringLiteral("the "));
        QCOMPARE(s.client.badDeletes, 0);
        for (const QString &typed : {QStringLiteral("Cst"), QStringLiteral("Xat")}) {
            Session line{static_cast<TextInputClient::Echo>(echo)};
            line.client.setText(QStringLiteral("ok\n"));
            line.type(typed);
            line.act([&] { line.engine.chooseSuggestion(QStringLiteral("Cat")); });
            QCOMPARE(line.client.visible(), QStringLiteral("ok\nCat "));
            QCOMPARE(line.client.badDeletes, 0);
        }
    }
    // LatinIME onStartBatchInput: a glide right after a word or a period
    // starts with a space, and the word the cursor touches is never
    // replaced by the glided one.
    void aGlideRightAfterAWordOrAPeriodGetsASpace_data()
    {
        QTest::addColumn<QString>("setup");
        QTest::addColumn<QString>("expected");
        QTest::newRow("typed word") << QStringLiteral("type:a cat") << QStringLiteral("a cat the ");
        QTest::newRow("cursor put after a word") << QStringLiteral("text:a cat") << QStringLiteral("a cat the ");
        QTest::newRow("backspace into a word") << QStringLiteral("type:a cat |") << QStringLiteral("a cat the ");
        QTest::newRow("period") << QStringLiteral("type:hi.") << QStringLiteral("hi. The ");
        QTest::newRow("after a space") << QStringLiteral("type:a ") << QStringLiteral("a the ");
        QTest::newRow("empty field") << QString() << QStringLiteral("The ");
        QTest::newRow("after a bracket") << QStringLiteral("text:(") << QStringLiteral("(the ");
    }
    void aGlideRightAfterAWordOrAPeriodGetsASpace()
    {
        QFETCH(QString, setup);
        QFETCH(QString, expected);
        Session s;
        if (setup.startsWith(QLatin1String("text:"))) s.client.setText(setup.mid(5));
        if (setup.startsWith(QLatin1String("type:"))) {
            QString typed = setup.mid(5);
            const bool thenBackspace = typed.endsWith(QLatin1Char('|'));
            if (thenBackspace) typed.chop(1);
            for (const QChar ch : typed) {
                if (ch.isLetter()) s.act([&] { s.engine.typeLetter(QString(ch)); });
                else if (ch == QLatin1Char(' ')) s.act([&] { s.engine.space(); });
                else s.act([&] { s.engine.typeText(QString(ch)); });
            }
            if (thenBackspace) s.act([&] { s.engine.backspace(); });
        }
        s.act([&] {
            s.engine.beginGlide(QStringLiteral("t"));
            s.engine.glideThrough(QStringLiteral("h"));
            s.engine.glideThrough(QStringLiteral("e"));
            QVERIFY(!s.engine.endGlide().isEmpty());
        });
        QCOMPARE(s.client.visible(), expected);
    }
};

QTEST_GUILESS_MAIN(ClientSimTest)
#include "tst_clientsim.moc"
