// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "app/voicecontroller.h"
#include "core/voicecommands.h"

using namespace Tastra;

namespace
{

class FakeRecorder final : public AudioRecorder
{
public:
    bool start(QString *error) override
    {
        if (!startError.isEmpty()) { *error = startError; return false; }
        recording = true;
        return true;
    }
    std::vector<float> stop() override { recording = false; return audio; }
    bool recording = false;
    QString startError;
    std::vector<float> audio = std::vector<float>(16000, 0.1f);   // 1 s
};

class FakeRecognizer final : public SpeechRecognizer
{
public:
    QString unavailableReason() const override { return reason; }
    void recognize(std::vector<float> samples, const QString &lang,
                   std::function<void(QString, QString)> done) override
    {
        lastSamples = samples.size();
        language = lang;
        pending = std::move(done);
    }
    void finish(const QString &text, const QString &error = {}) { auto d = std::move(pending); d(text, error); }
    QString reason;
    QString language;
    size_t lastSamples = 0;
    std::function<void(QString, QString)> pending;
};

}

class VoiceTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void spokenCommandsAreRecognisedInFourLanguages()
    {
        // Gboard voice commands, said on their own (Whisper adds a capital and
        // a full stop); anything longer is dictation.
        auto kind = [](const char *said) { return parseVoiceCommand(QString::fromUtf8(said)).kind; };
        QCOMPARE(kind("Delete last word."), VoiceCommand::DeleteLastWord);
        QCOMPARE(kind("Lösche das letzte Wort."), VoiceCommand::DeleteLastWord);
        QCOMPARE(kind("Удали последнее слово"), VoiceCommand::DeleteLastWord);
        QCOMPARE(kind("Видали останнє слово!"), VoiceCommand::DeleteLastWord);
        QCOMPARE(kind("Clear."), VoiceCommand::ClearSentence);
        QCOMPARE(kind("Clear all."), VoiceCommand::ClearAll);
        QCOMPARE(kind("Удали всё."), VoiceCommand::ClearAll);
        QCOMPARE(kind("Send."), VoiceCommand::Send);
        QCOMPARE(kind("Отправить."), VoiceCommand::Send);
        QCOMPARE(kind("Надіслати"), VoiceCommand::Send);
        QCOMPARE(kind("Absenden."), VoiceCommand::Send);
        QCOMPARE(kind("Undo."), VoiceCommand::Undo);
        QCOMPARE(kind("Rückgängig machen."), VoiceCommand::Undo);
        const VoiceCommand line = parseVoiceCommand(QStringLiteral("New line."));
        QCOMPARE(line.kind, VoiceCommand::NewLine);
        QCOMPARE(line.argument, QStringLiteral("\n"));
        QCOMPARE(parseVoiceCommand(QString::fromUtf8("Новый абзац.")).argument, QStringLiteral("\n\n"));
        const VoiceCommand heart = parseVoiceCommand(QStringLiteral("Heart emoji."));
        QCOMPARE(heart.kind, VoiceCommand::Emoji);
        QCOMPARE(heart.argument, QStringLiteral("heart"));
        QCOMPARE(parseVoiceCommand(QString::fromUtf8("Эмодзи сердце")).argument, QString::fromUtf8("сердце"));
        // Dictation, not commands.
        QCOMPARE(kind("Please send me the file."), VoiceCommand::None);
        QCOMPARE(kind("I will delete the last word later."), VoiceCommand::None);
        QCOMPARE(kind("Hello."), VoiceCommand::None);
        QCOMPARE(kind("emoji"), VoiceCommand::None);
        QCOMPARE(kind("this is a long sentence about my favourite emoji"), VoiceCommand::None);
    }

    void detailedEditsAreParsedAndApplied()
    {
        // Gboard's detailed edits (English only there): "change X to Y",
        // "delete X", "insert X before Y" / "at the end", "capitalize X",
        // "spell X as Y"; here also in German, Russian, Ukrainian.
        auto parse = [](const char *said) { return parseVoiceCommand(QString::fromUtf8(said)); };
        VoiceCommand c = parse("Change tomorrow to today.");
        QCOMPARE(c.kind, VoiceCommand::ReplaceText);
        QCOMPARE(c.argument, QStringLiteral("tomorrow"));
        QCOMPARE(c.second, QStringLiteral("today"));
        QCOMPARE(parse("Replace cat with dog").kind, VoiceCommand::ReplaceText);
        c = parse("Замени «завтра» на «сегодня».");
        QCOMPARE(c.kind, VoiceCommand::ReplaceText);
        QCOMPARE(c.argument, QString::fromUtf8("завтра"));
        QCOMPARE(c.second, QString::fromUtf8("сегодня"));
        QCOMPARE(parse("Ersetze Hund durch Katze.").second, QStringLiteral("Katze"));
        QCOMPARE(parse("Заміни кіт на пес").kind, VoiceCommand::ReplaceText);
        c = parse("Delete really.");
        QCOMPARE(c.kind, VoiceCommand::DeleteText);
        QCOMPARE(c.argument, QStringLiteral("really"));
        QCOMPARE(parse("Удали слово очень.").argument, QString::fromUtf8("очень"));
        QCOMPARE(parse("Lösche wirklich").kind, VoiceCommand::DeleteText);
        c = parse("Insert very before good.");
        QCOMPARE(c.kind, VoiceCommand::InsertBefore);
        QCOMPARE(c.argument, QStringLiteral("very"));
        QCOMPARE(c.second, QStringLiteral("good"));
        c = parse("Вставь очень после было.");
        QCOMPARE(c.kind, VoiceCommand::InsertAfter);
        QCOMPARE(parse("Füge sehr vor gut ein.").second, QStringLiteral("gut"));
        c = parse("Insert see you soon at the end.");
        QCOMPARE(c.kind, VoiceCommand::InsertAtEnd);
        QCOMPARE(c.argument, QStringLiteral("see you soon"));
        QCOMPARE(parse("Capitalize monday.").kind, VoiceCommand::Capitalize);
        QCOMPARE(parse("Напиши москва с большой буквы.").argument, QString::fromUtf8("москва"));
        QCOMPARE(parse("Schreibe berlin groß.").kind, VoiceCommand::Capitalize);
        c = parse("Spell Katie as K A T Y.");
        QCOMPARE(c.kind, VoiceCommand::Spell);
        QCOMPARE(c.second, QStringLiteral("K A T Y"));
        QCOMPARE(parse("Next.").kind, VoiceCommand::NextField);
        QCOMPARE(parse("Следующее поле").kind, VoiceCommand::NextField);
        // Commands that already were, and dictation.
        QCOMPARE(parse("Delete last word.").kind, VoiceCommand::DeleteLastWord);
        QCOMPARE(parse("Delete all.").kind, VoiceCommand::ClearAll);
        QCOMPARE(parse("Add salt after boiling.").kind, VoiceCommand::None);
        QCOMPARE(parse("Remove the lid before use.").kind, VoiceCommand::None);
        QCOMPARE(parse("Change is the only constant in life and we have to accept it to grow.").kind,
                 VoiceCommand::None);                                               // too long for a command

        // Applied to the text before the cursor; the last occurrence, whole
        // words, in the case of what is replaced.
        auto apply = [](const char *text, const char *said) {
            return applyVoiceEdit(QString::fromUtf8(text), parseVoiceCommand(QString::fromUtf8(said)));
        };
        QCOMPARE(apply("See you tomorrow. ", "Change tomorrow to today."), QStringLiteral("See you today. "));
        QCOMPARE(apply("Tomorrow is fine, tomorrow ", "change tomorrow to today"), QStringLiteral("Tomorrow is fine, today "));
        QCOMPARE(apply("Hello there. ", "Change hello to hi"), QStringLiteral("Hi there. "));
        QCOMPARE(apply("CALL ME ", "change call to ring"), QStringLiteral("RING ME "));
        QVERIFY(!apply("Hello there. ", "Change bye to hi"));                       // not there: dictation
        QVERIFY(!apply("cats ", "Change cat to dog"));                              // whole words only
        QCOMPARE(apply("It was really really good ", "Delete really."), QStringLiteral("It was really good "));
        QCOMPARE(apply("Well hello there ", "delete well"), QStringLiteral("Hello there "));
        QCOMPARE(apply("Hi world. ", "Delete world"), QStringLiteral("Hi. "));
        QCOMPARE(apply("It was good. ", "Insert very before good."), QStringLiteral("It was very good. "));
        QCOMPARE(apply("Hello there ", "insert oh before hello"), QStringLiteral("Oh hello there "));
        QCOMPARE(apply("I think so ", "insert well before I"), QStringLiteral("Well I think so "));
        QCOMPARE(apply("Было хорошо ", "Вставь очень после было"), QString::fromUtf8("Было очень хорошо "));
        QCOMPARE(apply("Done", "Insert thanks at the end."), QStringLiteral("Done thanks"));
        QCOMPARE(apply("see you on monday ", "Capitalize monday."), QStringLiteral("see you on Monday "));
        QCOMPARE(apply("в москва ", "Напиши москва с большой буквы"), QString::fromUtf8("в Москва "));
        QCOMPARE(apply("Hi Katie, ", "Spell Katie as K A T Y."), QStringLiteral("Hi Katy, "));
        QCOMPARE(apply("Привет, мир ", "Замени мир на всех"), QString::fromUtf8("Привет, всех "));
        QCOMPARE(apply("one  two three ", "change one two to four"), QStringLiteral("four three "));
    }

    void shortEmojiNamesMeanTheUsualEmoji()
    {
        QCOMPARE(spokenEmojiAlias(QStringLiteral("Heart")), QString::fromUtf8("\u2764\uFE0F"));
        QCOMPARE(spokenEmojiAlias(QString::fromUtf8("сердечко")), QString::fromUtf8("\u2764\uFE0F"));
        QCOMPARE(spokenEmojiAlias(QStringLiteral("smiley")), QString::fromUtf8("\U0001F603"));
        QCOMPARE(spokenEmojiAlias(QString::fromUtf8("плач")), QString::fromUtf8("\U0001F622"));
        QVERIFY(spokenEmojiAlias(QStringLiteral("pizza")).isEmpty());          // the keywords know that one
    }

    void spokenPunctuationBecomesMarks()
    {
        auto said = [](const char *text) { return applySpokenPunctuation(QString::fromUtf8(text)); };
        QCOMPARE(said("how are you question mark i am fine"), QString::fromUtf8("how are you? I am fine"));
        QCOMPARE(said("Wie geht es dir Fragezeichen"), QString::fromUtf8("Wie geht es dir?"));
        QCOMPARE(said("Как дела вопросительный знак"), QString::fromUtf8("Как дела?"));
        QCOMPARE(said("Привіт знак оклику як справи"), QString::fromUtf8("Привіт! Як справи"));
        QCOMPARE(said("Wow exclamation point."), QString::fromUtf8("Wow!"));
        QCOMPARE(said("Hello comma"), QString::fromUtf8("Hello,"));
        QCOMPARE(said("Ну и ладно, запятая."), QString::fromUtf8("Ну и ладно,"));
        QCOMPARE(said("That is the end full stop"), QString::fromUtf8("That is the end."));
        // Words that are words: left alone.
        QCOMPARE(said("Das ist ein wichtiger Punkt."), QString::fromUtf8("Das ist ein wichtiger Punkt."));
        QCOMPARE(said("С моей точки зрения, это так."), QString::fromUtf8("С моей точки зрения, это так."));
        QCOMPARE(said("drei Komma fünf Liter"), QString::fromUtf8("drei Komma fünf Liter"));
        QCOMPARE(said("during that period"), QString::fromUtf8("during that period"));
    }

    void tapToRecordTapToRecognize()
    {
        FakeRecorder recorder;
        FakeRecognizer recognizer;
        VoiceController voice(&recorder, &recognizer);
        voice.setLanguage(QStringLiteral("uk"));
        QSignalSpy text(&voice, &VoiceController::textRecognized);

        QVERIFY(voice.available());
        QCOMPARE(voice.state(), QStringLiteral("idle"));
        voice.toggle();
        QCOMPARE(voice.state(), QStringLiteral("recording"));
        QVERIFY(recorder.recording);
        voice.toggle();
        QCOMPARE(voice.state(), QStringLiteral("recognizing"));
        QCOMPARE(recognizer.language, QStringLiteral("uk"));
        QCOMPARE(recognizer.lastSamples, size_t(16000));
        recognizer.finish(QStringLiteral(" привіт світ "));
        QCOMPARE(voice.state(), QStringLiteral("idle"));
        QCOMPARE(text.size(), 1);
        QCOMPARE(text.at(0).at(0).toString(), QStringLiteral("привіт світ"));
    }

    void missingModelIsReportedNotRecorded()
    {
        FakeRecorder recorder;
        FakeRecognizer recognizer;
        recognizer.reason = QStringLiteral("model missing");
        VoiceController voice(&recorder, &recognizer);
        QVERIFY(!voice.available());
        voice.toggle();
        QVERIFY(!recorder.recording);
        QCOMPARE(voice.state(), QStringLiteral("unavailable"));
        QCOMPARE(voice.message(), QStringLiteral("model missing"));
    }

    void microphoneErrorsAndTooShortClips()
    {
        FakeRecorder recorder;
        FakeRecognizer recognizer;
        VoiceController voice(&recorder, &recognizer);
        recorder.startError = QStringLiteral("no microphone");
        voice.toggle();
        QCOMPARE(voice.state(), QStringLiteral("idle"));
        QCOMPARE(voice.message(), QStringLiteral("no microphone"));

        recorder.startError.clear();
        recorder.audio = std::vector<float>(1600, 0.1f);          // 0.1 s: accidental tap
        voice.toggle();
        voice.toggle();
        QCOMPARE(voice.state(), QStringLiteral("idle"));
        QVERIFY(!recognizer.pending);                               // nothing sent
    }

    void emptyResultsAndCancelledRecognitionInsertNothing()
    {
        FakeRecorder recorder;
        FakeRecognizer recognizer;
        VoiceController voice(&recorder, &recognizer);
        QSignalSpy text(&voice, &VoiceController::textRecognized);

        voice.toggle(); voice.toggle();
        recognizer.finish(QStringLiteral("  "));
        QCOMPARE(text.size(), 0);

        voice.toggle(); voice.toggle();
        voice.cancel();                                             // e.g. keyboard hidden
        QCOMPARE(voice.state(), QStringLiteral("idle"));
        recognizer.finish(QStringLiteral("late result"));
        QCOMPARE(text.size(), 0);

        voice.toggle(); voice.toggle();
        recognizer.finish({}, QStringLiteral("decoder failed"));
        QCOMPARE(voice.message(), QStringLiteral("decoder failed"));
        QCOMPARE(text.size(), 0);
    }

    void noBackendsMeansUnavailable()
    {
        VoiceController voice(nullptr, nullptr);
        QVERIFY(!voice.available());
        voice.toggle();
        QCOMPARE(voice.state(), QStringLiteral("unavailable"));
    }
};

QTEST_GUILESS_MAIN(VoiceTest)
#include "tst_voice.moc"
