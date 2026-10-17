// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QTest>
#include <QtTest/QSignalSpy>

#include <QCoreApplication>
#include <QSettings>
#include <QLocale>
#include <QStandardPaths>

#include "app/tracelog.h"
#include "app/voicecontroller.h"
#include "core/locallexicon.h"
#include "glidepaths.h"

#include <QFile>
#include <QLoggingCategory>
#include <QTemporaryDir>

Q_LOGGING_CATEGORY(lcTraceProbe, "tastra.test", QtWarningMsg)
Q_LOGGING_CATEGORY(lcOtherProbe, "other.test", QtWarningMsg)

#include "app/keyboarduibridge.h"
#include "app/uitranslator.h"
#include "core/systemclipboard.h"
#include "core/clipboardhistory.h"
#include "core/keychords.h"
#include "app/keyboardhider.h"
#include "core/inputmethodbackend.h"
#include "core/keyboardcontroller.h"

class FakeBackend final : public Tastra::InputMethodBackend
{
public:
    void commitText(const QString &text) override
    {
        preedit.clear();
        commits.append(text);
        events.append(QStringLiteral("commit:") + text);
    }

    void backspace() override
    {
        ++backspaceCount;
    }

    void deleteForward() override
    {
        ++deleteForwardCount;
    }

    void moveUp() override { events.append(QStringLiteral("up")); }
    void moveDown() override { events.append(QStringLiteral("down")); }
    void moveLeft() override
    {
        ++moveLeftCount;
        events.append(QStringLiteral("left"));
    }

    bool setPreedit(const QString &text) override
    {
        preedit = text;
        events.append(QStringLiteral("pre:") + text);
        return true;
    }

    QString preedit;
    QStringList events;

    void moveRight() override
    {
        ++moveRightCount;
    }

    void moveHome() override
    {
        ++moveHomeCount;
    }

    void moveEnd() override
    {
        ++moveEndCount;
    }

    void enter() override
    {
        ++enterCount;
    }
    void tab() override { ++tabCount; }

    QStringList commits;
    int tabCount = 0;
    int backspaceCount = 0;
    int deleteForwardCount = 0;
    int moveLeftCount = 0;
    int moveRightCount = 0;
    int moveHomeCount = 0;
    int moveEndCount = 0;
    int enterCount = 0;
};

// The desktop clipboard as data control would give it (KWin never offers
// the regular one to an input panel).
class FakeClipboard final : public Tastra::SystemClipboard
{
public:
    QString text() const override { return m_text; }
    bool isSensitive() const override { return sensitive; }
    void setText(const QString &text) override { m_text = text; m_image.clear(); sensitive = false; ++sets; Q_EMIT changed(); }
    void clear() override { m_text.clear(); m_image.clear(); Q_EMIT changed(); }
    QByteArray image() const override { return m_image; }
    QString imageMime() const override { return m_image.isEmpty() ? QString() : m_mime; }
    void setImage(const QByteArray &data, const QString &mime) override
    {
        m_text.clear();
        m_image = data;
        m_mime = mime;
        ++imageSets;
        Q_EMIT changed();
    }
    void copiedElsewhere(const QString &text, bool secret = false) { m_text = text; m_image.clear(); sensitive = secret; Q_EMIT changed(); }
    void imageCopiedElsewhere(const QByteArray &data, const QString &mime)
    {
        m_text.clear();
        m_image = data;
        m_mime = mime;
        sensitive = false;
        Q_EMIT changed();
    }
    int sets = 0;
    int imageSets = 0;
    bool sensitive = false;

private:
    QString m_text;
    QByteArray m_image;
    QString m_mime;
};

class FakeChords final : public Tastra::KeyChordSender
{
public:
    void send(const QList<int> &modifiers, int key) override { sent.append({modifiers, key}); }
    QList<QPair<QList<int>, int>> sent;
};

class KeyboardUiBridgeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("TastraTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_keyboarduibridge"));
        Tastra::LocalLexicon::setDictionarySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        Tastra::LocalLexicon::setBlocklistSearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        Tastra::LocalLexicon::setUserDictionaryFile(QStringLiteral(TASTRA_TEST_DATA "/empty/none.txt"));
    }

    void init() { QSettings().clear(); }

    void copyAndCutTakeTheApplicationsSelection()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        FakeClipboard clipboard;
        bridge.setSystemClipboard(&clipboard);
        QSignalSpy selection(&bridge, &Tastra::KeyboardUiBridge::selectionChanged);

        QVERIFY(!bridge.hasSelection());
        bridge.copySelection();                                     // nothing selected
        QCOMPARE(clipboard.sets, 0);
        // "Привіт світ" with "світ" selected (byte offsets, as KWin sends them).
        const QString text = QStringLiteral("Привіт світ");
        const int start = QStringLiteral("Привіт ").toUtf8().size(), end = text.toUtf8().size();
        bridge.setSurroundingText(text, end, start);
        QVERIFY(bridge.hasSelection());
        QCOMPARE(selection.count(), 1);
        bridge.copySelection();
        QCOMPARE(clipboard.text(), QStringLiteral("світ"));
        QCOMPARE(bridge.clipboardText(), QStringLiteral("світ"));   // read back through the same object

        const int before = backend.backspaceCount;
        bridge.setSurroundingText(QStringLiteral("Hello world"), 0, 5);   // anchor after the cursor
        bridge.cutSelection();
        QCOMPARE(clipboard.text(), QStringLiteral("Hello"));
        QCOMPARE(backend.backspaceCount, before + 1);                   // the selection is deleted
        QVERIFY(!bridge.hasSelection());

        // Never from a password field.
        bridge.setSurroundingText(QStringLiteral("secret"), 6, 0);
        bridge.setContentType(0, 8);
        QVERIFY(!bridge.hasSelection());
        bridge.copySelection();
        QCOMPARE(clipboard.text(), QStringLiteral("Hello"));
        bridge.setContentType(0, 0);

        // Text copied in another application reaches the history and the chip.
        clipboard.copiedElsewhere(QStringLiteral("from Firefox"));
        QCOMPARE(bridge.clipboardText(), QStringLiteral("from Firefox"));
        QVERIFY(bridge.clipboardHistory().contains(QStringLiteral("from Firefox")));
        QCOMPARE(bridge.clipboardSuggestion(), QStringLiteral("from Firefox"));
        bridge.setSystemClipboard(nullptr);                         // back to QClipboard
    }

    void copiedImagesArePastedByTheApplication()
    {
        // Gboard keeps and pastes images and screenshots. An input method can
        // only send text, so the image goes on the clipboard and the
        // application pastes it itself (Ctrl+V through KWin's fake input).
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.clearClipboardHistory();
        FakeClipboard clipboard;
        FakeChords chords;
        bridge.setSystemClipboard(&clipboard);
        QVERIFY(!bridge.canPasteImages());
        bridge.setKeyChordSender(&chords);
        QVERIFY(bridge.canPasteImages());

        const QByteArray shot = QByteArray("\x89PNG\r\n\x1a\n", 8) + QByteArray(64, 's');
        clipboard.imageCopiedElsewhere(shot, QStringLiteral("image/png"));
        const QString key = Tastra::ClipboardHistory::imageKey(shot, QStringLiteral("image/png"));
        QCOMPARE(bridge.clipboardHistory().value(0), key);
        QVERIFY(bridge.isClipboardImage(key));
        QVERIFY(bridge.clipboardImage().startsWith(QStringLiteral("image://tastraclipboard/current/")));
        QCOMPARE(bridge.clipboardImageBytes(bridge.clipboardImage().mid(24)), shot);
        QCOMPARE(bridge.clipboardItemImage(key), QStringLiteral("image://tastraclipboard/") + key.section(QLatin1Char(':'), 1));
        QCOMPARE(bridge.clipboardImageBytes(key.section(QLatin1Char(':'), 1)), shot);
        QVERIFY(bridge.clipboardSuggestion().isEmpty());                 // no text chip
        QCOMPARE(bridge.clipboardImageSuggestion(), bridge.clipboardItemImage(key));

        // The strip's chip: already on the clipboard, only Ctrl+V.
        bridge.pasteClipboardImageSuggestion();
        QCOMPARE(chords.sent.size(), 1);
        QCOMPARE(chords.sent.last().first, QList<int>{Tastra::EvdevKey::LeftCtrl});
        QCOMPARE(chords.sent.last().second, Tastra::EvdevKey::V);
        QVERIFY(bridge.clipboardImageSuggestion().isEmpty());
        QCOMPARE(clipboard.imageSets, 0);

        // From the history after other copies: back on the clipboard, then Ctrl+V.
        clipboard.copiedElsewhere(QStringLiteral("some text"));
        QVERIFY(bridge.clipboardImage().isEmpty());
        bridge.pasteClipboardHistory(int(bridge.clipboardHistory().indexOf(key)));
        QCOMPARE(clipboard.imageSets, 1);
        QCOMPARE(clipboard.image(), shot);
        QCOMPARE(chords.sent.size(), 2);
        QCOMPARE(backend.commits, QStringList());                        // no text typed
        QVERIFY(bridge.clipboardImageSuggestion().isEmpty());            // its own copy is no news
        QCOMPARE(bridge.clipboardHistory().value(0), key);
        // Paste in the panel with an image on the clipboard: Ctrl+V.
        bridge.pasteClipboard();
        QCOMPARE(chords.sent.size(), 3);

        // Never into a password field.
        bridge.setContentType(0, 8);
        QVERIFY(!bridge.canPasteImages());
        bridge.pasteClipboardHistory(0);
        bridge.pasteClipboard();
        QCOMPARE(chords.sent.size(), 3);
        bridge.setContentType(0, 0);
        bridge.setKeyChordSender(nullptr);
        bridge.setSystemClipboard(nullptr);
        bridge.clearClipboardHistory();
    }

    void passwordsFromAPasswordManagerAreNeitherKeptNorShown()
    {
        // KeePassXC & co. mark secrets (x-kde-passwordManagerHint): Paste
        // still inserts them, but no history entry, no chip, no visible text.
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        FakeClipboard clipboard;
        bridge.setSystemClipboard(&clipboard);
        clipboard.copiedElsewhere(QStringLiteral("s3cret-pass"), true);
        QVERIFY(bridge.hasClipboardText());
        QVERIFY(!bridge.clipboardHistory().contains(QStringLiteral("s3cret-pass")));
        QVERIFY(bridge.clipboardSuggestion().isEmpty());
        QVERIFY(!bridge.clipboardPreview().contains(QStringLiteral("s3cret")));
        bridge.pasteClipboard();
        QCOMPARE(backend.commits.join(QString()), QStringLiteral("s3cret-pass"));

        // A long text: the chip and the panel show its start only.
        const QString page = QString(QStringLiteral("0123456789")).repeated(100000);
        clipboard.copiedElsewhere(page);
        QVERIFY(bridge.clipboardPreview().size() <= 300);
        QCOMPARE(bridge.property("clipboardSuggestion").toString().size(), 300);
        QCOMPARE(bridge.clipboardSuggestion(), page);                // what a tap pastes
    }

    void terminalsGetTheirOwnShortcutsAndCutNeedsASelection()
    {
        using namespace Tastra::EvdevKey;
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        FakeClipboard clipboard;
        bridge.setSystemClipboard(&clipboard);
        FakeChords chords;
        bridge.setKeyChordSender(&chords);
        // An application that reports its text: Copy and Cut follow its
        // selection (Cut with nothing selected cuts a line in some editors).
        bridge.setSurroundingText(QStringLiteral("Hello world"), 5, 5);
        QVERIFY(!bridge.canCopy() && !bridge.canCut());
        bridge.cutSelection();
        QVERIFY(chords.sent.isEmpty());
        bridge.setSurroundingText(QStringLiteral("Hello world"), 0, 5);
        QVERIFY(bridge.canCopy() && bridge.canCut());
        bridge.resetInputContext();
        QVERIFY(bridge.canCopy());                                   // nothing reported yet

        // A terminal (content purpose 12): Ctrl+C would interrupt the
        // program, Ctrl+Z suspend it; Copy is Ctrl+Shift+C, nothing else.
        bridge.setContentType(0, 12);
        QVERIFY(!bridge.canSelect() && !bridge.canCut());
        QVERIFY(bridge.canCopy());
        bridge.copySelection();
        QCOMPARE(chords.sent.last(), (QPair<QList<int>, int>({LeftCtrl, LeftShift}, C)));
        const qsizetype sent = chords.sent.size();
        bridge.cutSelection();
        bridge.undo();
        bridge.redo();
        bridge.selectAll();
        bridge.toggleSelectMode();
        QCOMPARE(chords.sent.size(), sent);
        QVERIFY(!bridge.selectMode());
        bridge.setContentType(0, 0);
        QVERIFY(bridge.canSelect());
    }

    void theToolbarIsChosenAndOrdered()
    {
        // Gboard: choose the toolbar's features and their order; settings
        // always stays; the choice is kept.
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        const QStringList defaults{QStringLiteral("clipboard"), QStringLiteral("emoji"), QStringLiteral("text-editing"),
                                   QStringLiteral("resize"), QStringLiteral("settings")};
        QCOMPARE(bridge.toolbarActionIds(), defaults);
        QCOMPARE(bridge.toolbarChoices(), defaults + QStringList{QStringLiteral("one-handed")});
        bridge.setToolbarItemShown(QStringLiteral("one-handed"), true);
        bridge.setToolbarItemShown(QStringLiteral("clipboard"), false);
        bridge.setToolbarItemShown(QStringLiteral("settings"), false);           // stays
        bridge.moveToolbarItem(QStringLiteral("one-handed"), -10);
        const QStringList chosen{QStringLiteral("one-handed"), QStringLiteral("emoji"), QStringLiteral("text-editing"),
                                 QStringLiteral("resize"), QStringLiteral("settings")};
        QCOMPARE(bridge.toolbarActionIds(), chosen);
        QCOMPARE(bridge.toolbarActions().first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("one-handed"));
        {
            Tastra::KeyboardModel again;
            Tastra::KeyboardUiBridge restarted(controller, again);
            QCOMPARE(restarted.toolbarActionIds(), chosen);
        }
        QCOMPARE(bridge.layoutMode(), QStringLiteral("full"));
        bridge.activateToolbarAction(QStringLiteral("one-handed"));
        QCOMPARE(bridge.layoutMode(), QStringLiteral("right"));
        bridge.activateToolbarAction(QStringLiteral("one-handed"));
        QCOMPARE(bridge.layoutMode(), QStringLiteral("full"));
        // A stored list from another version: unknown ids go, settings comes back.
        QSettings().setValue(QStringLiteral("toolbarItems"), QStringList{QStringLiteral("gif"), QStringLiteral("emoji")});
        Tastra::KeyboardModel other;
        Tastra::KeyboardUiBridge older(controller, other);
        QCOMPARE(older.toolbarActionIds(), (QStringList{QStringLiteral("emoji"), QStringLiteral("settings")}));
    }

    void voiceCommandsActInsteadOfBeingTyped()
    {
        // Gboard voice typing: "send", "clear all", "undo", "pizza emoji"
        // said on their own act; anything else is typed, with spoken
        // punctuation as marks.
        using namespace Tastra::EvdevKey;
        using Chord = QPair<QList<int>, int>;
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        FakeChords chords;
        bridge.setKeyChordSender(&chords);

        bridge.handleRecognizedSpeech(QStringLiteral("are you there question mark"));
        QCOMPARE(backend.commits.join(QString()), QStringLiteral("Are you there? "));
        const int enters = backend.enterCount;
        bridge.handleRecognizedSpeech(QStringLiteral("Send."));
        QCOMPARE(backend.enterCount, enters + 1);
        QCOMPARE(backend.commits.join(QString()), QStringLiteral("Are you there? "));   // not typed
        bridge.handleRecognizedSpeech(QStringLiteral("Clear all."));
        QCOMPARE(chords.sent.mid(chords.sent.size() - 2), (QList<Chord>{{{LeftCtrl}, A}, {{}, BackSpace}}));
        bridge.handleRecognizedSpeech(QStringLiteral("Undo."));
        QCOMPARE(chords.sent.last(), (Chord{{LeftCtrl}, Z}));
        backend.commits.clear();
        bridge.handleRecognizedSpeech(QStringLiteral("Pizza emoji."));
        QVERIFY2(backend.commits.join(QString()).contains(QString::fromUtf8("🍕")), qPrintable(backend.commits.join('|')));
        QVERIFY(bridge.recentEmojis().contains(QString::fromUtf8("🍕")));
        backend.commits.clear();
        bridge.handleRecognizedSpeech(QStringLiteral("Heart emoji."));
        QVERIFY2(backend.commits.join(QString()).contains(QString::fromUtf8("\u2764")), qPrintable(backend.commits.join('|')));
        backend.commits.clear();
        bridge.handleRecognizedSpeech(QStringLiteral("Qwzx emoji."));               // no such emoji: typed
        QVERIFY(backend.commits.join(QString()).contains(QStringLiteral("Qwzx emoji.")));

        // Gboard's detailed edits: on the words before the cursor; words that
        // are not there make it dictation.
        backend.commits.clear();
        bridge.handleRecognizedSpeech(QStringLiteral("see you tomorrow"));
        QCOMPARE(backend.commits.join(QString()), QStringLiteral("See you tomorrow "));
        const int backspaces = backend.backspaceCount;
        bridge.handleRecognizedSpeech(QStringLiteral("Change tomorrow to today."));
        // Only what differs is retyped: "morrow " -> "day " (no surrounding text here: Backspace).
        QCOMPARE(backend.backspaceCount, backspaces + 7);
        QCOMPARE(backend.commits.last(), QStringLiteral("day "));
        bridge.handleRecognizedSpeech(QStringLiteral("Change Monday to Friday."));
        QCOMPARE(backend.commits.last(), QStringLiteral("Change Monday to Friday. "));   // typed
        bridge.handleRecognizedSpeech(QStringLiteral("Next."));
        QCOMPARE(backend.tabCount, 1);

        // Never in a password field: there it is all dictation, no commands.
        bridge.setContentType(0, 8);
        const qsizetype sent = chords.sent.size();
        bridge.handleRecognizedSpeech(QStringLiteral("Clear all."));
        QCOMPARE(chords.sent.size(), sent);
        bridge.setContentType(0, 0);
        bridge.setKeyChordSender(nullptr);
    }

    void slidingFromBackspaceSelectsWordsAndDeletesThemOnRelease()
    {
        // Gboard / LatinIME gesture delete: slide left from Backspace, the
        // words to go are selected in the application; slide back, fewer;
        // lift the finger, the selection is deleted; back at the start,
        // nothing is.
        using namespace Tastra::EvdevKey;
        using Chord = QPair<QList<int>, int>;
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        QVERIFY(!bridge.deleteGestureSelects());                     // no shortcuts: deletes as it slides
        FakeChords chords;
        bridge.setKeyChordSender(&chords);
        QVERIFY(bridge.deleteGestureSelects());

        bridge.tapLetter(QStringLiteral("h"));
        bridge.tapLetter(QStringLiteral("i"));
        bridge.deleteGestureTo(1);
        QVERIFY(bridge.currentWord().isEmpty());                     // the word is finished first
        bridge.deleteGestureTo(3);
        bridge.deleteGestureTo(2);
        QCOMPARE(chords.sent, (QList<Chord>{{{LeftCtrl, LeftShift}, Left}, {{LeftCtrl, LeftShift}, Left},
                                            {{LeftCtrl, LeftShift}, Left}, {{LeftCtrl, LeftShift}, Right}}));
        const int keys = backend.backspaceCount;
        bridge.endDeleteGesture();
        QCOMPARE(chords.sent.last(), (Chord{{}, BackSpace}));
        QCOMPARE(backend.backspaceCount, keys);                      // not the input method's own key

        chords.sent.clear();
        bridge.deleteGestureTo(2);
        bridge.deleteGestureTo(0);
        bridge.endDeleteGesture();
        QCOMPARE(chords.sent, (QList<Chord>{{{LeftCtrl, LeftShift}, Left}, {{LeftCtrl, LeftShift}, Left},
                                            {{LeftCtrl, LeftShift}, Right}, {{LeftCtrl, LeftShift}, Right}}));

        // Cancelled (the touch was taken away): the selection is undone.
        chords.sent.clear();
        bridge.deleteGestureTo(1);
        bridge.cancelDeleteGesture();
        QCOMPARE(chords.sent, (QList<Chord>{{{LeftCtrl, LeftShift}, Left}, {{LeftCtrl, LeftShift}, Right}}));
        // The field went away mid-slide: nothing more is sent anywhere.
        chords.sent.clear();
        bridge.deleteGestureTo(2);
        bridge.deactivateInputContext();
        bridge.endDeleteGesture();
        bridge.cancelDeleteGesture();
        QCOMPARE(chords.sent.size(), 2);

        // Passwords and terminals: never selected by shortcuts.
        bridge.setContentType(0, 8);
        QVERIFY(!bridge.deleteGestureSelects());
        bridge.setContentType(0, 12);
        QVERIFY(!bridge.deleteGestureSelects());
        bridge.setContentType(0, 0);
        bridge.setKeyChordSender(nullptr);
    }

    void selectSelectAllCopyAndCutAsShortcuts()
    {
        // With KWin's fake input the panel sends real shortcuts, so the
        // application selects and copies itself (Gboard's editing panel).
        using namespace Tastra::EvdevKey;
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        FakeClipboard clipboard;
        bridge.setSystemClipboard(&clipboard);
        QVERIFY(!bridge.canSelect());
        QVERIFY(!bridge.canCopy());                                 // nothing selected, no shortcuts
        FakeChords chords;
        bridge.setKeyChordSender(&chords);
        QVERIFY(bridge.canSelect() && bridge.canCopy());

        bridge.selectAll();
        bridge.copySelection();
        QCOMPARE(chords.sent.size(), 2);
        QCOMPARE(chords.sent.at(0), (QPair<QList<int>, int>({LeftCtrl}, A)));
        QCOMPARE(chords.sent.at(1), (QPair<QList<int>, int>({LeftCtrl}, C)));
        QCOMPARE(clipboard.sets, 0);                                // the application copies

        // Select: arrows, Home and End extend the selection.
        const int moved = backend.moveLeftCount;
        bridge.toggleSelectMode();
        QVERIFY(bridge.selectMode());
        bridge.moveLeft();
        bridge.moveEnd();
        QCOMPARE(backend.moveLeftCount, moved);                     // not the plain key
        QCOMPARE(chords.sent.at(2), (QPair<QList<int>, int>({LeftShift}, Left)));
        QCOMPARE(chords.sent.at(3), (QPair<QList<int>, int>({LeftShift}, End)));
        bridge.cutSelection();
        QCOMPARE(chords.sent.at(4), (QPair<QList<int>, int>({LeftCtrl}, X)));
        QVERIFY(!bridge.selectMode());                              // a cut ends selecting
        bridge.moveLeft();
        QCOMPARE(backend.moveLeftCount, moved + 1);

        // Leaving the panel ends selecting too.
        bridge.toggleSelectMode();
        bridge.closePanel();
        QVERIFY(!bridge.selectMode());

        // A word being typed is finished before a shortcut acts.
        bridge.tapLetter(QStringLiteral("h"));
        bridge.tapLetter(QStringLiteral("i"));
        bridge.selectAll();
        QVERIFY(bridge.currentWord().isEmpty());

        // Undo / Redo as the application's shortcuts.
        bridge.undo();
        bridge.redo();
        QCOMPARE(chords.sent.at(chords.sent.size() - 2), (QPair<QList<int>, int>({LeftCtrl}, Z)));
        QCOMPARE(chords.sent.last(), (QPair<QList<int>, int>({LeftCtrl, LeftShift}, Z)));

        // Never in password fields.
        const int before = chords.sent.size();
        bridge.setContentType(0, 8);
        QVERIFY(!bridge.canCopy());
        bridge.copySelection();
        bridge.cutSelection();
        bridge.undo();
        QCOMPARE(chords.sent.size(), before);
        bridge.setContentType(0, 0);
        bridge.setKeyChordSender(nullptr);
        bridge.setSystemClipboard(nullptr);
    }

    void interfaceLanguageFollowsTheSystemOrTheSetting()
    {
        // Gboard follows the system language; Tastra too, unless one is chosen.
        QCOMPARE(Tastra::UiTranslator::resolve(QStringLiteral("system"), QLocale(QStringLiteral("de_DE"))), QStringLiteral("de"));
        QCOMPARE(Tastra::UiTranslator::resolve(QStringLiteral("system"), QLocale(QStringLiteral("uk_UA"))), QStringLiteral("uk"));
        QCOMPARE(Tastra::UiTranslator::resolve(QStringLiteral("system"), QLocale(QStringLiteral("fr_FR"))), QStringLiteral("en"));
        QCOMPARE(Tastra::UiTranslator::resolve(QStringLiteral("ru"), QLocale(QStringLiteral("de_DE"))), QStringLiteral("ru"));

        // The tables (the app installs one translator; this test has no app).
        Tastra::UiTranslator table;
        QVERIFY(table.loadLanguage(QStringLiteral("de")));
        QCOMPARE(table.translate("SettingsPanel", "Paste"), QStringLiteral("Einfügen"));
        QCOMPARE(table.translate("Tastra", "No microphone found"), QStringLiteral("Kein Mikrofon gefunden"));
        QCOMPARE(table.translate("Main", "＋ Add “%1” to dictionary"), QStringLiteral("＋ „%1“ zum Wörterbuch hinzufügen"));
        QVERIFY(table.translate("Main", "not a text of ours").isNull());      // falls back to English
        QVERIFY(table.loadLanguage(QStringLiteral("uk")));
        QCOMPARE(table.translate("SettingsPanel", "Paste"), QStringLiteral("Вставити"));
        QVERIFY(table.loadLanguage(QStringLiteral("en")));
        QVERIFY(table.isEmpty());

        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        QSignalSpy changed(&bridge, &Tastra::KeyboardUiBridge::uiLanguageChanged);
        bridge.setUiLanguage(QStringLiteral("de"));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(bridge.uiLanguageLabel(), QStringLiteral("Deutsch"));
        QCOMPARE(QSettings().value(QStringLiteral("uiLanguage")).toString(), QStringLiteral("de"));
        bridge.cycleUiLanguage();                                   // de -> ru -> uk -> system -> en
        QCOMPARE(bridge.uiLanguageLabel(), QStringLiteral("Русский"));
        bridge.cycleUiLanguage();
        QCOMPARE(bridge.uiLanguageLabel(), QStringLiteral("Українська"));
        bridge.cycleUiLanguage();
        QCOMPARE(bridge.uiLanguage(), QStringLiteral("system"));
        QVERIFY(bridge.uiLanguageLabel().startsWith(QStringLiteral("System (")));
        bridge.cycleUiLanguage();
        QCOMPARE(bridge.uiLanguageLabel(), QStringLiteral("English"));
        bridge.setUiLanguage(QStringLiteral("xx"));                // unknown: back to the system's
        QCOMPARE(bridge.uiLanguage(), QStringLiteral("system"));

        // A new start reads the choice back.
        bridge.setUiLanguage(QStringLiteral("uk"));
        Tastra::KeyboardUiBridge again(controller, model);
        QCOMPARE(again.uiLanguage(), QStringLiteral("uk"));
        QCOMPARE(again.uiLanguageLabel(), QStringLiteral("Українська"));
    }

    // KWin sends text-input-v1 enums and maps PIN to password (8); 9 is date.
    void dateFieldIsNotTreatedAsSecret()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);

        bridge.setContentType(0, 9);
        QVERIFY(!bridge.secureInput());
        bridge.setContentType(0, 8);
        QVERIFY(bridge.secureInput());
        bridge.setContentType(0x40, 0);
        QVERIFY(bridge.secureInput());
    }

    void urlAndEmailFieldsDisableSmartTypingWithoutBeingSecret()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);

        for (quint32 purpose : {5u, 6u}) {
            bridge.setContentType(0, purpose);
            QVERIFY(!bridge.secureInput());
            QVERIFY(!bridge.uppercase());             // no auto-capitalization
            bridge.tapLetter(QStringLiteral("h"));
            bridge.tapLetter(QStringLiteral("e"));
            QVERIFY(bridge.suggestions().isEmpty());  // no suggestions / learning
            bridge.resetInputContext();
        }
        QCOMPARE(backend.commits.first(), QStringLiteral("h"));
    }

    void lowercaseHintDisablesAutoCapitalization()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);

        QVERIFY(bridge.uppercase());                   // sentence start by default
        bridge.setContentType(0x8, 0);
        QVERIFY(!bridge.uppercase());
        bridge.resetInputContext();
        QVERIFY(bridge.uppercase());
    }

    void cursorMovesAndLanguageSwitchCommitTheComposedWordFirst()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setSurroundingText(QStringLiteral("Hi. "), 4, 4);   // mid-text        // text-input client

        bridge.tapLetter(QStringLiteral("o"));
        bridge.tapLetter(QStringLiteral("k"));
        QCOMPARE(backend.preedit, QStringLiteral("Ok"));
        bridge.moveLeft();
        QCOMPARE(backend.events.mid(backend.events.size() - 2),
                 QStringList({QStringLiteral("commit:Ok"), QStringLiteral("left")}));

        bridge.setSurroundingText(QStringLiteral("x"), 1, 1);
        bridge.tapLetter(QStringLiteral("n"));
        bridge.nextLanguage();
        QCOMPARE(backend.commits.last(), QStringLiteral("n"));
    }

    void compositionCanBeSwitchedOff()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setSurroundingText(QString(), 0, 0);

        QVERIFY(bridge.compositionEnabled());
        bridge.setCompositionEnabled(false);
        bridge.tapLetter(QStringLiteral("a"));
        QCOMPARE(backend.commits, QStringList({QStringLiteral("A")}));
        QVERIFY(backend.preedit.isEmpty());
    }

    void traceIsOptInAndOnlyCapturesKeyboardCategories()
    {
        QTemporaryDir dir;
        QVERIFY(!Tastra::enableTraceIfRequested(dir.path()));   // no flag file
        QFile flag(dir.path() + QStringLiteral("/trace.enable"));
        QVERIFY(flag.open(QIODevice::WriteOnly));
        flag.close();
        QVERIFY(Tastra::enableTraceIfRequested(dir.path()));

        qCDebug(lcTraceProbe) << "probe bytes" << 3;
        qCWarning(lcOtherProbe) << "not ours";
        QFile log(dir.path() + QStringLiteral("/trace.log"));
        QVERIFY(log.open(QIODevice::ReadOnly));
        const QByteArray content = log.readAll();
        QVERIFY(content.contains("tastra.test: probe bytes 3"));
        QVERIFY(!content.contains("not ours"));
    }

    void dictationFlowsFromVoiceIntoTheField()
    {
        struct Recorder final : Tastra::AudioRecorder {
            bool start(QString *) override { return true; }
            std::vector<float> stop() override { return std::vector<float>(16000, 0.1f); }
        } recorder;
        struct Recognizer final : Tastra::SpeechRecognizer {
            QString unavailableReason() const override { return {}; }
            void recognize(std::vector<float>, const QString &lang, std::function<void(QString, QString)> done) override
            {
                language = lang;
                done(QStringLiteral("hello there."), {});
            }
            QString language;
        } recognizer;

        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        QVERIFY(!bridge.voiceBuilt());
        Tastra::VoiceController voice(&recorder, &recognizer);
        bridge.setVoiceController(&voice);
        QVERIFY(bridge.voiceBuilt());
        bridge.setLanguage(QStringLiteral("de"));

        bridge.toggleVoice();
        QCOMPARE(bridge.voiceState(), QStringLiteral("recording"));
        bridge.toggleVoice();
        QCOMPARE(recognizer.language, QStringLiteral("de"));
        QCOMPARE(backend.commits.last(), QStringLiteral("Hello there. "));
        QVERIFY(bridge.uppercase());                          // sentence ended
        QCOMPARE(bridge.voiceState(), QStringLiteral("idle"));
    }

    void emojiSuggestionFollowsAnExactWordAndInsertsAfterIt()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setAutoCapitalizationEnabled(false);
        for (const QChar ch : QStringLiteral("pizza")) bridge.tapLetter(QString(ch));
        QVERIFY(bridge.suggestions().contains(QStringLiteral("🍕")));
        bridge.selectSuggestion(QStringLiteral("🍕"));
        QCOMPARE(backend.commits.join(QString()), QStringLiteral("pizza 🍕"));
        bridge.setEmojiSuggestionsEnabled(false);
        for (const QChar ch : QStringLiteral(" pizza")) {
            if (ch == QLatin1Char(' ')) bridge.space(); else bridge.tapLetter(QString(ch));
        }
        QVERIFY(!bridge.suggestions().contains(QStringLiteral("🍕")));
    }

    void aPhraseHabitTakesTheLastChipAndCanBeForgotten()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.clearLearnedWords();
        bridge.setAutoCapitalizationEnabled(false);
        bridge.setAutocorrectEnabled(false);
        auto type = [&](const QString &text) {
            for (const QChar ch : text) {
                if (ch == QLatin1Char(' ')) bridge.space();
                else if (ch.isLetter()) bridge.tapLetter(QString(ch));
                else bridge.tapText(QString(ch));
            }
        };
        type(QStringLiteral("see you soon. see you soon. see you soon. se"));
        QCOMPARE(bridge.phraseSuggestion(), QStringLiteral("see you soon"));
        QCOMPARE(bridge.phraseHeadLength(), 3);                    // "see", the rest dimmed
        QCOMPARE(bridge.suggestions().last(), bridge.phraseSuggestion());
        QVERIFY(bridge.suggestions().size() <= 3);
        // Long-press: the habit goes, the words stay.
        bridge.forgetSuggestion(bridge.phraseSuggestion());
        QVERIFY(bridge.phraseSuggestion().isEmpty());
        QVERIFY2(bridge.suggestions().contains(QStringLiteral("see")), qPrintable(bridge.suggestions().join(',')));

        type(QStringLiteral("e you soon. see you soon. see you soon. see "));
        QCOMPARE(bridge.phraseSuggestion(), QStringLiteral("you soon"));
        QCOMPARE(bridge.phraseHeadLength(), 3);
        backend.commits.clear();
        bridge.selectSuggestion(bridge.phraseSuggestion());
        QCOMPARE(backend.commits.join(QString()), QStringLiteral("you soon "));     // after "see "

        // Settings -> Text correction -> Phrase suggestions, kept.
        type(QStringLiteral(". see "));
        QVERIFY(!bridge.phraseSuggestion().isEmpty());
        bridge.setPhraseSuggestions(false);
        QVERIFY(bridge.phraseSuggestion().isEmpty());
        QVERIFY(!bridge.suggestions().contains(QStringLiteral("you soon")));
        QCOMPARE(QSettings().value(QStringLiteral("phraseSuggestions")).toBool(), false);
        Tastra::KeyboardUiBridge again(controller, model);
        QVERIFY(!again.phraseSuggestions());
    }

    void morseKeysTypeLettersAfterAPauseOrSpace()
    {
        // Gboard's Morse code layout (support.google.com/gboard/answer/9011881):
        // a sign is typed after the character timeout, or by Space; with a
        // word timeout a longer pause ends the word with a space.
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setLanguage(QStringLiteral("en"));
        bridge.setAutoCapitalizationEnabled(false);
        bridge.setAutocorrectEnabled(false);
        QVERIFY(!bridge.morseLayout());
        while (!bridge.morseLayout()) bridge.cycleLetterLayout();
        QCOMPARE(QSettings().value(QStringLiteral("layoutVariant/en")).toString(), QStringLiteral("morse"));
        QCOMPARE(bridge.morseCharacterTimeout(), 800);                           // defaults
        QCOMPARE(bridge.morseWordTimeout(), 0);
        auto written = [&] { return backend.commits.join(QString()); };

        // "Never": Space sends.
        bridge.setMorseTimeouts(0, 0);
        bridge.morseDot();
        bridge.morseDot();
        bridge.morseDot();
        QCOMPARE(bridge.morseCode(), QStringLiteral("..."));
        QCOMPARE(bridge.morseText(), QStringLiteral("s"));
        QVERIFY(!bridge.morseHints().isEmpty());
        QCOMPARE(bridge.morseHints().first().toMap().value(QStringLiteral("text")).toString(), QStringLiteral("h"));
        QTest::qWait(100);
        QVERIFY(written().isEmpty());
        bridge.space();                                                           // sends, no space
        QCOMPARE(written(), QStringLiteral("s"));
        QVERIFY(bridge.morseCode().isEmpty());
        // Backspace takes back a dot or dash first.
        bridge.morseDash();
        bridge.morseDash();
        bridge.backspace();
        QCOMPARE(bridge.morseCode(), QStringLiteral("-"));
        bridge.space();
        QCOMPARE(written(), QStringLiteral("st"));
        bridge.space();                                                           // nothing keyed: a space
        QCOMPARE(written(), QStringLiteral("st "));
        // No sign: nothing is typed.
        for (int i = 0; i < 6; ++i) bridge.morseDash();
        bridge.space();
        QCOMPARE(written(), QStringLiteral("st "));
        // A sign from the strip instead of the keyed one.
        bridge.morseDot();
        bridge.morseDash();
        bridge.chooseMorseHint(QStringLiteral("w"));
        QCOMPARE(written(), QStringLiteral("st w"));
        // Another key sends what was keyed first.
        bridge.morseDot();
        bridge.tapText(QStringLiteral("!"));
        QCOMPARE(written(), QStringLiteral("st we!"));

        // Timeouts (the pause itself: tst_qmlkeyboard, this test has no event loop).
        bridge.setMorseTimeouts(60, 250);
        QCOMPARE(QSettings().value(QStringLiteral("morseWordTimeout")).toInt(), 250);
        bridge.cycleMorseCharacterTimeout();
        QCOMPARE(bridge.morseCharacterTimeout(), 800);                           // unknown value: the default
        bridge.cycleMorseCharacterTimeout();
        bridge.cycleMorseCharacterTimeout();
        bridge.cycleMorseCharacterTimeout();
        QCOMPARE(bridge.morseCharacterTimeout(), 0);                              // 1.2, 2.0, then never
        bridge.cycleMorseWordTimeout();
        QCOMPARE(bridge.morseWordTimeout(), 0);

        // German "ch", Russian letters; switching the language forgets the keyed code.
        backend.commits.clear();
        bridge.setLanguage(QStringLiteral("de"));
        while (!bridge.morseLayout()) bridge.cycleLetterLayout();
        for (int i = 0; i < 4; ++i) bridge.morseDash();
        bridge.space();
        QCOMPARE(written(), QStringLiteral("ch"));
        bridge.morseDot();
        bridge.setLanguage(QStringLiteral("ru"));
        QVERIFY(bridge.morseCode().isEmpty());
        QVERIFY(!bridge.morseLayout());                                           // each language its own layout
        while (!bridge.morseLayout()) bridge.cycleLetterLayout();
        bridge.morseDot();
        bridge.morseDash();
        bridge.morseDot();
        bridge.morseDash();
        bridge.space();
        QCOMPARE(written(), QString::fromUtf8("chя"));
        QVERIFY(bridge.letterLayoutChoice());
    }

    void stripShowsTypedWordAndWhatSpaceWillInsert()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setAutoCapitalizationEnabled(false);
        bridge.setSurroundingText(QStringLiteral("Hi. "), 4, 4);   // mid-text
        for (const QChar ch : QStringLiteral("teh")) bridge.tapLetter(QString(ch));
        // Gboard: typed word (quoted in the UI) left, the correction centre.
        QCOMPARE(bridge.autocorrectSuggestion(), QStringLiteral("the"));
        QCOMPARE(bridge.suggestions().mid(0, 2), QStringList({QStringLiteral("teh"), QStringLiteral("the")}));
        bridge.selectSuggestion(QStringLiteral("teh"));              // keep what was typed
        QCOMPARE(backend.commits, QStringList({QStringLiteral("teh")}));
        for (const QChar ch : QStringLiteral("hel")) bridge.tapLetter(QString(ch));
        QVERIFY(bridge.autocorrectSuggestion().isEmpty());          // a prefix, nothing to correct
    }

    void fieldTypeDrivesLayoutLikeGboard()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        QCOMPARE(bridge.inputPurpose(), QStringLiteral("text"));
        bridge.setContentType(0, 3);   QCOMPARE(bridge.inputPurpose(), QStringLiteral("number"));
        bridge.setContentType(0, 2);   QCOMPARE(bridge.inputPurpose(), QStringLiteral("number"));
        bridge.setContentType(0, 4);   QCOMPARE(bridge.inputPurpose(), QStringLiteral("phone"));
        bridge.setContentType(0, 6);   QCOMPARE(bridge.inputPurpose(), QStringLiteral("email"));
        QCOMPARE(bridge.alternatesForKey(QStringLiteral(".")).value(0), QStringLiteral(".com"));
        bridge.setContentType(0, 5);   QCOMPARE(bridge.inputPurpose(), QStringLiteral("url"));
        QVERIFY(bridge.alternatesForKey(QStringLiteral(".")).contains(QStringLiteral(".de")));
        bridge.resetInputContext();    QCOMPARE(bridge.inputPurpose(), QStringLiteral("text"));
        QCOMPARE(bridge.alternatesForKey(QStringLiteral(".")).value(0), QStringLiteral(","));
    }

    void textShortcutsExpandFromTheStrip()
    {
        QTemporaryDir dir;
        QFile file(dir.path() + QStringLiteral("/shortcuts.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("# comment\nadp\tAndroid Police\nomw = on my way\n");
        file.close();
        qputenv("TASTRA_SHORTCUTS_FILE", file.fileName().toUtf8());

        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setAutoCapitalizationEnabled(false);
        for (const QChar ch : QStringLiteral("omw")) bridge.tapLetter(QString(ch));
        QCOMPARE(bridge.suggestions().value(0), QStringLiteral("on my way"));
        bridge.selectSuggestion(QStringLiteral("on my way"));
        // No text-input client here: the typed "omw" is replaced via keys.
        QCOMPARE(backend.backspaceCount, 3);
        QCOMPARE(backend.commits.mid(3), QStringList({QStringLiteral("on my way"), QStringLiteral(" ")}));
        qunsetenv("TASTRA_SHORTCUTS_FILE");
    }

    void wordsAndShortcutsAreAddedFromSettings()
    {
        // Gboard: Settings -> Dictionary -> Personal dictionary -> +: a word
        // and an optional shortcut, typed on the keyboard itself.
        QTemporaryDir dir;
        QFile file(dir.path() + QStringLiteral("/shortcuts.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("# my shortcuts\nomw = on my way\n");
        file.close();
        qputenv("TASTRA_SHORTCUTS_FILE", file.fileName().toUtf8());
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setAutoCapitalizationEnabled(false);
        bridge.activateToolbarAction(QStringLiteral("settings"));
        bridge.startWordEntry();
        QCOMPARE(bridge.wordEntryStep(), QStringLiteral("word"));
        QCOMPARE(bridge.activePanel(), QStringLiteral("typing"));     // the letters come back
        bridge.shift();                                                // capitals as typed
        for (const QChar ch : QStringLiteral("Oldenburx")) bridge.tapLetter(QString(ch));
        bridge.backspace();
        bridge.tapLetter(QStringLiteral("g"));
        bridge.space();                                                // no spaces in a word
        QCOMPARE(bridge.wordEntryText(), QStringLiteral("Oldenburg"));
        bridge.enter();
        QCOMPARE(bridge.wordEntryStep(), QStringLiteral("shortcut"));
        QVERIFY(bridge.wordEntryText().isEmpty());
        for (const QChar ch : QStringLiteral("olb")) bridge.tapLetter(QString(ch));
        bridge.enter();
        QVERIFY(bridge.wordEntryStep().isEmpty());
        QVERIFY(backend.commits.isEmpty());                            // nothing reached the app
        QVERIFY(bridge.userWords().contains(QStringLiteral("Oldenburg")));
        QCOMPARE(bridge.shortcutList(), QStringList({QStringLiteral("olb = Oldenburg"), QStringLiteral("omw = on my way")}));
        QCOMPARE(bridge.activePanel(), QStringLiteral("settings"));   // back where it started
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString saved = QString::fromUtf8(file.readAll());
        file.close();
        QVERIFY2(saved.contains(QStringLiteral("# my shortcuts")) && saved.contains(QStringLiteral("olb = Oldenburg")), qPrintable(saved));
        bridge.closePanel();
        for (const QChar ch : QStringLiteral("olb")) bridge.tapLetter(QString(ch));
        QCOMPARE(bridge.suggestions().value(0), QStringLiteral("Oldenburg"));
        // Removing a shortcut keeps the rest of the file.
        bridge.removeShortcut(QStringLiteral("omw"));
        QCOMPARE(bridge.shortcutList(), QStringList{QStringLiteral("olb = Oldenburg")});
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString after = QString::fromUtf8(file.readAll());
        file.close();
        QVERIFY2(after.contains(QStringLiteral("# my shortcuts")) && !after.contains(QStringLiteral("omw")), qPrintable(after));
        // ✕ adds nothing; an empty word cannot be confirmed.
        bridge.startWordEntry();
        bridge.enter();
        QCOMPARE(bridge.wordEntryStep(), QStringLiteral("word"));
        bridge.tapLetter(QStringLiteral("x"));
        bridge.stopWordEntry();
        QVERIFY(bridge.wordEntryStep().isEmpty());
        QVERIFY(!bridge.userWords().contains(QStringLiteral("x")));
        qunsetenv("TASTRA_SHORTCUTS_FILE");
    }

    void apostropheOnSymbolsReturnsToLetters()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.toggleSymbols();
        QVERIFY(bridge.symbolsActive());
        bridge.tapText(QStringLiteral("'"));
        QVERIFY(!bridge.symbolsActive());                // Gboard 16.7 default
        QCOMPARE(backend.commits.last(), QStringLiteral("'"));
        bridge.toggleSymbols();
        bridge.tapText(QStringLiteral("5"));
        QVERIFY(bridge.symbolsActive());                 // other symbols keep the layer
        bridge.toggleSymbols();
        QVERIFY(bridge.alternatesForKey(QStringLiteral(".")).size() >= 16);   // Gboard: 16 marks
        QVERIFY(bridge.alternatesForKey(QStringLiteral(".")).contains(QStringLiteral("%")));
    }

    void themesFollowGboardIncludingSystem()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        {
            QSettings().setValue(QStringLiteral("amoled"), true);       // pre-1.0 setting
            Tastra::KeyboardUiBridge migrated(controller, model);
            QCOMPARE(migrated.theme(), QStringLiteral("amoled"));
            QSettings().clear();
        }
        Tastra::KeyboardUiBridge bridge(controller, model);
        QCOMPARE(bridge.theme(), QStringLiteral("system"));
        QVERIFY(bridge.effectiveTheme() == QStringLiteral("dark") || bridge.effectiveTheme() == QStringLiteral("light"));
        const QStringList order = {QStringLiteral("light"), QStringLiteral("dark"), QStringLiteral("amoled"), QStringLiteral("system")};
        for (const QString &next : order) {
            bridge.cycleTheme();
            QCOMPARE(bridge.theme(), next);
        }
        bridge.setTheme(QStringLiteral("light"));
        QCOMPARE(bridge.effectiveTheme(), QStringLiteral("light"));
        QVERIFY(!bridge.amoled());
        bridge.setTheme(QStringLiteral("bogus"));
        QCOMPARE(bridge.theme(), QStringLiteral("light"));
        QVERIFY(!bridge.version().isEmpty());
    }

    void tappingAnUnknownTypedWordOffersToSaveIt()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setAutoCapitalizationEnabled(false);

        for (const QChar ch : QStringLiteral("zorgle")) bridge.tapLetter(QString(ch));
        QVERIFY(bridge.typedWordUnknown());
        QCOMPARE(bridge.suggestions().value(0), QStringLiteral("zorgle"));   // shown quoted
        QVERIFY(bridge.saveWordCandidate().isEmpty());
        bridge.selectSuggestion(QStringLiteral("zorgle"));                   // keep it
        QCOMPARE(bridge.saveWordCandidate(), QStringLiteral("zorgle"));      // "＋ Add to dictionary"
        bridge.addWordToDictionary(bridge.saveWordCandidate());
        QVERIFY(bridge.saveWordCandidate().isEmpty());
        QCOMPARE(bridge.userWords(), QStringList({QStringLiteral("zorgle")}));
        for (const QChar ch : QStringLiteral("zor")) bridge.tapLetter(QString(ch));
        QVERIFY(bridge.suggestions().contains(QStringLiteral("zorgle")));

        // The offer disappears with the next key and is not made for known words.
        bridge.space();
        for (const QChar ch : QStringLiteral("blorp")) bridge.tapLetter(QString(ch));
        bridge.selectSuggestion(QStringLiteral("blorp"));
        QCOMPARE(bridge.saveWordCandidate(), QStringLiteral("blorp"));
        bridge.tapLetter(QStringLiteral("x"));
        QVERIFY(bridge.saveWordCandidate().isEmpty());
        bridge.space();
        for (const QChar ch : QStringLiteral("hello")) bridge.tapLetter(QString(ch));
        bridge.selectSuggestion(QStringLiteral("hello"));                     // a known word
        QVERIFY(bridge.saveWordCandidate().isEmpty());
        bridge.removeWordFromDictionary(QStringLiteral("zorgle"));
        QVERIFY(bridge.userWords().isEmpty());
    }

    void savingAnUnknownWordAlsoWorksWithComposition()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setSurroundingText(QStringLiteral("Hi. "), 4, 4);   // mid-text                  // Firefox: preedit mode
        for (const QChar ch : QStringLiteral("zorgle")) bridge.tapLetter(QString(ch));
        QCOMPARE(backend.preedit, QStringLiteral("Zorgle"));         // sentence start
        QVERIFY(bridge.typedWordUnknown());
        QCOMPARE(bridge.suggestions().value(0), QStringLiteral("Zorgle"));
        bridge.selectSuggestion(QStringLiteral("Zorgle"));
        QCOMPARE(bridge.saveWordCandidate(), QStringLiteral("Zorgle"));
        bridge.addWordToDictionary(bridge.saveWordCandidate());
        QCOMPARE(bridge.userWords(), QStringList({QStringLiteral("Zorgle")}));
    }

    void suggestionsFollowTheCaseOfTheTypedWord()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setSurroundingText(QString(), 0, 0);
        for (const QChar ch : QStringLiteral("hel")) bridge.tapLetter(QString(ch));   // "Hel" (auto-cap)
        QVERIFY(bridge.suggestions().contains(QStringLiteral("Hello")));
        QVERIFY(!bridge.suggestions().contains(QStringLiteral("hello")));
    }

    void hideButtonCommitsTheWordAndAsksKWinToHide()
    {
        struct FakeHider final : Tastra::KeyboardHider {
            int calls = 0;
            void hideKeyboard() override { ++calls; }
        } hider;
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setKeyboardHider(&hider);
        bridge.setSurroundingText(QStringLiteral("Hi. "), 4, 4);
        for (const QChar ch : QStringLiteral("wor")) bridge.tapLetter(QString(ch));
        QCOMPARE(backend.preedit, QStringLiteral("Wor"));
        bridge.hideKeyboard();
        QCOMPARE(hider.calls, 1);
        QCOMPARE(backend.commits.last(), QStringLiteral("Wor"));     // nothing lost
        QCOMPARE(backend.preedit, QString());
    }

    void gboardSoundAndTrailPreferencesPersist()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        {
            Tastra::KeyboardUiBridge bridge(controller, model);
            QVERIFY(!bridge.keySound());           // Gboard: off by default
            QVERIFY(bridge.glideTrail());          // Gboard: on by default
            bridge.keyFeedback();                  // silent and harmless while off
            bridge.setKeySound(true);
            bridge.setGlideTrail(false);
            bridge.keyFeedback();                  // must not crash without an audio device
            for (const QString &kind : {QStringLiteral("space"), QStringLiteral("delete"), QStringLiteral("return"),
                                        QStringLiteral("unknown")})
                bridge.keyFeedback(kind);          // LatinIME: own sounds for Space, Delete, Return
            QCOMPARE(bridge.keySoundVolume(), 35);
            bridge.cycleKeySoundVolume();
            QCOMPARE(bridge.keySoundVolume(), 60);
        }
        Tastra::KeyboardUiBridge reloaded(controller, model);
        QCOMPARE(reloaded.keySound(), reloaded.canPlayKeySound());   // only kept if playable
        QCOMPARE(reloaded.keySoundVolume(), 60);
        reloaded.cycleKeySoundVolume();
        reloaded.cycleKeySoundVolume();
        QCOMPARE(reloaded.keySoundVolume(), 15);
        QVERIFY(!reloaded.glideTrail());
        QVERIFY(reloaded.nextWordSuggestions());                     // Gboard: on by default
        reloaded.setNextWordSuggestions(false);
        Tastra::KeyboardUiBridge again(controller, model);
        QVERIFY(!again.nextWordSuggestions());
    }

    void onlyEnabledLanguagesAreInTheRotation()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        {
            Tastra::KeyboardUiBridge bridge(controller, model);
            // The first four are on until a choice is made; the later ones are turned on.
            QCOMPARE(bridge.languageCodes(), (QStringList{QStringLiteral("en"), QStringLiteral("de"),
                                                          QStringLiteral("uk"), QStringLiteral("ru")}));
            QCOMPARE(bridge.allLanguageCodes().size(), 10);
            QVERIFY(!bridge.isLanguageEnabled(QStringLiteral("es")));
            bridge.setPreferredLanguage(QStringLiteral("es-ES"));                // not on: no switch
            QCOMPARE(bridge.languageCode(), QStringLiteral("en"));
            bridge.setLanguageEnabled(QStringLiteral("es"), true);
            bridge.setPreferredLanguage(QStringLiteral("es-ES"));
            QCOMPARE(bridge.languageCode(), QStringLiteral("es"));
            bridge.setLanguageEnabled(QStringLiteral("es"), false);
            bridge.setLanguage(QStringLiteral("ru"));
            bridge.setLanguageEnabled(QStringLiteral("en"), false);
            bridge.setLanguageEnabled(QStringLiteral("de"), false);
            // Layout order (en, de, uk, ru) is kept.
            QCOMPARE(bridge.languageCodes(), QStringList({QStringLiteral("uk"), QStringLiteral("ru")}));
            bridge.nextLanguage();
            QCOMPARE(bridge.languageCode(), QStringLiteral("uk"));
            bridge.nextLanguage();
            QCOMPARE(bridge.languageCode(), QStringLiteral("ru"));
            // Turning off the current language moves to the next enabled one.
            bridge.setLanguageEnabled(QStringLiteral("ru"), false);
            QCOMPARE(bridge.languageCode(), QStringLiteral("uk"));
            // The last one cannot be turned off.
            bridge.setLanguageEnabled(QStringLiteral("uk"), false);
            QVERIFY(bridge.isLanguageEnabled(QStringLiteral("uk")));
            QCOMPARE(bridge.languageCodes(), QStringList({QStringLiteral("uk")}));
        }
        Tastra::KeyboardUiBridge reloaded(controller, model);
        QCOMPARE(reloaded.languageCodes(), QStringList({QStringLiteral("uk")}));
        QCOMPARE(reloaded.languageLabels().size(), 1);
    }

    void upDownAndLongPressDelayLikeGboard()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        {
            Tastra::KeyboardUiBridge bridge(controller, model);
            bridge.moveUp();
            bridge.moveDown();
            QCOMPARE(backend.events.mid(backend.events.size() - 2), QStringList({QStringLiteral("up"), QStringLiteral("down")}));
            QCOMPARE(bridge.longPressDelay(), 300);              // Gboard default
            bridge.cycleLongPressDelay();
            QCOMPARE(bridge.longPressDelay(), 400);
        }
        Tastra::KeyboardUiBridge reloaded(controller, model);
        QCOMPARE(reloaded.longPressDelay(), 400);
        for (int i = 0; i < 4; ++i) reloaded.cycleLongPressDelay();   // 500, 700, 200, 300
        QCOMPARE(reloaded.longPressDelay(), 300);
    }

    void otherEnabledLanguagesOfTheSameScriptAreCompanions()
    {
        // Gboard's multilingual typing: words of these are never "corrected".
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setLanguage(QStringLiteral("en"));
        QCOMPARE(bridge.companionLanguages(), QStringList{QStringLiteral("de")});
        bridge.setLanguage(QStringLiteral("uk"));
        QCOMPARE(bridge.companionLanguages(), QStringList{QStringLiteral("ru")});
        bridge.setLanguageEnabled(QStringLiteral("ru"), false);
        QVERIFY(bridge.companionLanguages().isEmpty());
    }

    void shiftBeforeAGlideCapitalizesTheWord()
    {
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(":/tastra/frequency")});
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setLanguage(QStringLiteral("en"));
        bridge.setAutoCapitalizationEnabled(false);
        QVERIFY(bridge.waitForDictionaryForTesting(10000));
        const auto centres = GlidePaths::centresFor(QStringLiteral("en"));
        QVariantList points;
        for (const QPointF &p : GlidePaths::pathFor(QStringLiteral("hello"), centres, 0.0, 5)) points.append(p);
        QVariantMap keys;
        for (auto it = centres.constBegin(); it != centres.constEnd(); ++it) keys.insert(QString(it.key()), it.value());

        bridge.shift();                                              // one-shot Shift
        QCOMPARE(bridge.endGlidePath(points, keys, GlidePaths::KeyWidth), QStringLiteral("Hello"));
        QVERIFY(!bridge.uppercase());                                // used up by the word
        bridge.shift();
        bridge.shift();                                              // Caps Lock
        QVERIFY(bridge.capsLock());
        QCOMPARE(bridge.endGlidePath(points, keys, GlidePaths::KeyWidth), QStringLiteral("HELLO"));
        QVERIFY(bridge.capsLock());                                  // stays on
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
    }

    void emojiSearchIsTypedOnTheKeyboardItself()
    {
        // Gboard: tapping "Search emoji" brings the letters back and types
        // into the search, not into the app (an input panel never gets
        // keyboard focus, so a text field in it could not be typed into).
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.activateToolbarAction(QStringLiteral("emoji"));
        QCOMPARE(bridge.activePanel(), QStringLiteral("emoji"));
        bridge.startEmojiSearch();
        QVERIFY(bridge.emojiSearchActive());
        QCOMPARE(bridge.activePanel(), QStringLiteral("typing"));     // the letters come back
        QVERIFY(!bridge.uppercase());                                  // lowercase keys, as in Gboard
        for (const QChar ch : QStringLiteral("pizzx")) bridge.tapLetter(QString(ch));
        bridge.backspace();
        bridge.tapLetter(QStringLiteral("a"));
        QCOMPARE(bridge.emojiSearchText(), QStringLiteral("pizza"));
        QVERIFY(backend.commits.isEmpty());                           // nothing reached the app
        QVERIFY(bridge.emojiSearchResults().contains(QStringLiteral("🍕")));
        bridge.insertEmoji(QStringLiteral("🍕"));
        QCOMPARE(backend.commits, QStringList{QStringLiteral("🍕")});
        QVERIFY(!bridge.emojiSearchActive());
        QVERIFY(bridge.emojiSearchText().isEmpty());
        // Enter or ✕ leave the search without typing anything.
        bridge.startEmojiSearch();
        bridge.tapLetter(QStringLiteral("c"));
        bridge.space();
        bridge.enter();
        QVERIFY(!bridge.emojiSearchActive());
        QCOMPARE(backend.enterCount, 0);
        QCOMPARE(backend.commits, QStringList{QStringLiteral("🍕")});
        bridge.startEmojiSearch();
        bridge.stopEmojiSearch();
        QVERIFY(!bridge.emojiSearchActive());
    }

    void emoticonsAreATabOfTextFacesLikeGboard()
    {
        // Gboard's emoji keyboard has a ":-)" tab of text emoticons.
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        QCOMPARE(bridge.emojiCategories().value(bridge.emojiCategories().size() - 1), QStringLiteral(":-)"));
        const QStringList faces = bridge.emojiSearch(QString(), QStringLiteral(":-)"));
        for (const QString &face : {QStringLiteral(":-)"), QStringLiteral(";-)"), QStringLiteral(":-D"),
                                    QStringLiteral("¯\\_(ツ)_/¯"), QStringLiteral("(╯°□°)╯︵ ┻━┻")}) {
            QVERIFY2(faces.contains(face), qPrintable(face));
        }
        QVERIFY(faces.size() >= 40);
        QCOMPARE(QSet<QString>(faces.cbegin(), faces.cend()).size(), faces.size());   // no duplicates
        // A face goes in exactly as shown and is not an emoji for "Recent".
        const QStringList recentBefore = bridge.recentEmojis();
        bridge.insertEmoticon(QStringLiteral("¯\\_(ツ)_/¯"));
        QCOMPARE(backend.commits.join(QString()), QStringLiteral("¯\\_(ツ)_/¯"));
        QCOMPARE(bridge.recentEmojis(), recentBefore);
    }

    void choosingAWrongLayoutFixSwitchesTheLanguage()
    {
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/frequency-foreign")});
        Tastra::LocalLexicon::clearForeignWordCacheForTesting();
        Tastra::LocalLexicon::preloadForeignWordsForTesting(QStringLiteral("ru"));
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);
        bridge.setLanguage(QStringLiteral("en"));
        bridge.setAutoCapitalizationEnabled(false);
        bridge.setSurroundingText(QStringLiteral("Hi. "), 4, 4);
        for (const QChar ch : QStringLiteral("ghbdtn")) bridge.tapLetter(QString(ch));
        QVERIFY2(bridge.suggestions().contains(QStringLiteral("привет")), qPrintable(bridge.suggestions().join(',')));
        bridge.selectSuggestion(QStringLiteral("привет"));
        QVERIFY(backend.commits.contains(QStringLiteral("привет")));
        QCOMPARE(bridge.languageCode(), QStringLiteral("ru"));          // keep typing in Russian
        Tastra::LocalLexicon::setFrequencySearchPaths({QStringLiteral(TASTRA_TEST_DATA "/empty")});
        Tastra::LocalLexicon::clearForeignWordCacheForTesting();
    }

    void tapTextReachesController()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);

        bridge.tapText(QStringLiteral("a"));

        QCOMPARE(backend.commits, QStringList{QStringLiteral("a")});
    }

    void editingCommandsReachController()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);

        bridge.moveLeft();
        bridge.moveRight();
        bridge.moveHome();
        bridge.moveEnd();
        bridge.deleteForward();

        QCOMPARE(backend.moveLeftCount, 1);
        QCOMPARE(backend.moveRightCount, 1);
        QCOMPARE(backend.moveHomeCount, 1);
        QCOMPARE(backend.moveEndCount, 1);
        QCOMPARE(backend.deleteForwardCount, 1);
    }


    void ordinaryTypingDoesNotInvalidateWholeKeyboard()
    {
        FakeBackend backend;
        Tastra::KeyboardController controller(backend);
        Tastra::KeyboardModel model;
        Tastra::KeyboardUiBridge bridge(controller, model);

        // Consume the initial sentence-start capitalization with the first key.
        bridge.tapLetter(QStringLiteral("h"));

        QSignalSpy keyboardSpy(&bridge, &Tastra::KeyboardUiBridge::keyboardStateChanged);
        QSignalSpy suggestionSpy(&bridge, &Tastra::KeyboardUiBridge::suggestionsChanged);

        bridge.tapLetter(QStringLiteral("e"));

        QCOMPARE(keyboardSpy.count(), 0);
        QCOMPARE(suggestionSpy.count(), 1);
    }
};

QTEST_APPLESS_MAIN(KeyboardUiBridgeTest)

#include "tst_keyboarduibridge.moc"
