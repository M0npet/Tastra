// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QTest>

#include <QSet>

#include "core/keyboardmodel.h"
#include "core/morsecode.h"
#include "../src/core/panelmanager.h"
#include "../src/core/toolbarregistry.h"
#include "../src/core/toolbarmodel.h"

class KeyboardModelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void longPressAlternatesFollowGboardPositions()
    {
        Tastra::KeyboardModel model;                       // EN QWERTY
        QCOMPARE(model.alternatesForKey(QStringLiteral("q")), QStringList({QStringLiteral("1")}));
        QCOMPARE(model.alternatesForKey(QStringLiteral("p")), QStringList({QStringLiteral("0")}));
        QCOMPARE(model.alternatesForKey(QStringLiteral("a")).value(0), QStringLiteral("@"));
        QVERIFY(model.alternatesForKey(QStringLiteral("a")).contains(QStringLiteral("á")));
        QCOMPARE(model.alternatesForKey(QStringLiteral("e")).mid(0, 2), QStringList({QStringLiteral("3"), QStringLiteral("è")}));
        QCOMPARE(model.alternatesForKey(QStringLiteral("m")), QStringList({QStringLiteral("?")}));
        QCOMPARE(model.alternatesForKey(QStringLiteral(".")).value(0), QStringLiteral(","));
        QVERIFY(model.alternatesForKey(QStringLiteral(".")).contains(QStringLiteral("!")));

        model.setLanguage(QStringLiteral("de"));               // language letter first
        QCOMPARE(model.alternatesForKey(QStringLiteral("s")), QStringList({QStringLiteral("ß"), QStringLiteral("#")}));
        QCOMPARE(model.alternateForKey(QStringLiteral("s")), QStringLiteral("ß"));
        model.setLanguage(QStringLiteral("ru"));
        QCOMPARE(model.alternatesForKey(QStringLiteral("й")), QStringList({QStringLiteral("1")}));
        QCOMPARE(model.alternatesForKey(QStringLiteral("е")), QStringList({QStringLiteral("ё"), QStringLiteral("5")}));
        model.setLanguage(QStringLiteral("uk"));
        QCOMPARE(model.alternatesForKey(QStringLiteral("г")).value(0), QStringLiteral("ґ"));

        model.toggleSymbols();
        // Symbols layer: Gboard's extras (fractions, superscripts) on digits.
        QVERIFY(model.alternatesForKey(QStringLiteral("1")).contains(QStringLiteral("½")));
    }

    void symbolPagesLikeGboard()
    {
        Tastra::KeyboardModel model;
        model.toggleSymbols();
        QCOMPARE(model.symbolPage(), 0);
        // Page 1 (?123): the currency of the language.
        QVERIFY(model.symbolRows().at(1).contains(QStringLiteral("$")));
        model.setLanguage(QStringLiteral("de"));
        QVERIFY(model.symbolRows().at(1).contains(QStringLiteral("€")));
        model.setLanguage(QStringLiteral("ru"));
        QVERIFY(model.symbolRows().at(1).contains(QStringLiteral("₽")));
        model.setLanguage(QStringLiteral("uk"));
        QVERIFY(model.symbolRows().at(1).contains(QStringLiteral("₴")));
        QVERIFY(model.symbolRows().at(1).contains(QStringLiteral("_")));
        // Page 2 (=\<): programming and typographic symbols.
        model.toggleSymbolPage();
        QCOMPARE(model.symbolPage(), 1);
        QString page2;
        for (const QStringList &row : model.symbolRows()) page2 += row.join(QString());
        for (const QString &sym : {QStringLiteral("<"), QStringLiteral(">"), QStringLiteral("["), QStringLiteral("]"),
                                   QStringLiteral("{"), QStringLiteral("}"), QStringLiteral("\\"), QStringLiteral("|"),
                                   QStringLiteral("~"), QStringLiteral("^"), QStringLiteral("="), QStringLiteral("%")}) {
            QVERIFY2(page2.contains(sym), qPrintable(sym));
        }
        for (const QStringList &row : model.symbolRows()) QVERIFY(row.size() >= 8 && row.size() <= 10);
        // Long-press extras on symbol keys.
        QVERIFY(model.alternatesForKey(QStringLiteral("\"")).contains(QStringLiteral("«")));
        QVERIFY(model.alternatesForKey(QStringLiteral("-")).contains(QStringLiteral("—")));
        QVERIFY(model.alternatesForKey(QStringLiteral("₴")).contains(QStringLiteral("€")));
        // Leaving the symbols layer resets to page 1.
        model.toggleSymbols();
        model.toggleSymbols();
        QCOMPARE(model.symbolPage(), 0);
    }

    void startsLowercase()
    {
        Tastra::KeyboardModel model;

        QVERIFY(!model.uppercase());
        QVERIFY(!model.capsLock());
        QCOMPARE(model.textForLetter(QStringLiteral("a")), QStringLiteral("a"));
    }

    void oneShiftUppercasesOneLetter()
    {
        Tastra::KeyboardModel model;

        model.pressShift();

        QVERIFY(model.uppercase());
        QVERIFY(!model.capsLock());
        QCOMPARE(model.textForLetter(QStringLiteral("q")), QStringLiteral("Q"));

        model.consumeShiftAfterLetter();

        QVERIFY(!model.uppercase());
    }

    void secondShiftEnablesCapsLock()
    {
        Tastra::KeyboardModel model;

        model.pressShift();
        model.pressShift();

        QVERIFY(model.uppercase());
        QVERIFY(model.capsLock());

        model.consumeShiftAfterLetter();

        QVERIFY(model.uppercase());
        QVERIFY(model.capsLock());
        QCOMPARE(model.textForLetter(QStringLiteral("z")), QStringLiteral("Z"));
    }

    void shiftCyclesBackToLowercaseFromCapsLock()
    {
        Tastra::KeyboardModel model;

        model.pressShift();
        model.pressShift();
        model.pressShift();

        QVERIFY(!model.uppercase());
        QVERIFY(!model.capsLock());
    }

    void symbolLayerTogglesAndClearsShift()
    {
        Tastra::KeyboardModel model;

        model.pressShift();
        QVERIFY(model.uppercase());

        model.toggleSymbols();

        QVERIFY(model.symbolsActive());
        QVERIFY(!model.uppercase());
        QVERIFY(!model.capsLock());

        model.toggleSymbols();

        QVERIFY(!model.symbolsActive());
        QVERIFY(!model.uppercase());
    }

    void cyclesSupportedLanguages()
    {
        Tastra::KeyboardModel model;

        QCOMPARE(model.languageCode(), QStringLiteral("en"));
        QCOMPARE(model.languageLabel(), QStringLiteral("English"));

        model.nextLanguage();
        QCOMPARE(model.languageCode(), QStringLiteral("de"));
        QCOMPARE(model.languageLabel(), QStringLiteral("Deutsch"));

        model.nextLanguage();
        QCOMPARE(model.languageCode(), QStringLiteral("uk"));
        QCOMPARE(model.languageLabel(), QStringLiteral("Українська"));

        model.nextLanguage();
        QCOMPARE(model.languageCode(), QStringLiteral("ru"));
        QCOMPARE(model.languageLabel(), QStringLiteral("Русский"));

        // The model knows all languages; the bridge skips those turned off.
        for (const char *code : {"es", "fr", "it", "pt", "pl", "nl", "en"}) {
            model.nextLanguage();
            QCOMPARE(model.languageCode(), QString::fromLatin1(code));
        }
    }

    void languagesAddedInTheirGboardLayouts()
    {
        // 0.7.19: Spanish with ñ, French AZERTY with the apostrophe key,
        // Italian, Portuguese, Polish, Dutch QWERTY; their accented letters
        // on a long press (AOSP LatinIME more keys), their currency.
        Tastra::KeyboardModel model;
        model.setLanguage(QStringLiteral("es"));
        QCOMPARE(model.languageLabel(), QString::fromUtf8("Español"));
        QCOMPARE(model.row2().join(QString()), QString::fromUtf8("asdfghjklñ"));
        QVERIFY(model.alternatesForKey(QStringLiteral("a")).contains(QString::fromUtf8("á")));
        QVERIFY(model.alternatesForKey(QStringLiteral("u")).contains(QString::fromUtf8("ü")));
        model.setLanguage(QStringLiteral("fr"));
        QCOMPARE(model.row1().join(QString()), QStringLiteral("azertyuiop"));
        QCOMPARE(model.row3().join(QString()), QStringLiteral("wxcvbn'"));
        QCOMPARE(Tastra::KeyboardModel::layoutVariantLabel(QStringLiteral("fr"), model.layoutVariant(QStringLiteral("fr"))),
                 QStringLiteral("AZERTY"));
        QVERIFY(model.alternatesForKey(QStringLiteral("c")).contains(QString::fromUtf8("ç")));
        QVERIFY(model.alternatesForKey(QStringLiteral("e")).contains(QString::fromUtf8("é")));
        model.setLanguage(QStringLiteral("pl"));
        QCOMPARE(model.alternateForKey(QStringLiteral("a")), QString::fromUtf8("ą"));
        QCOMPARE(model.alternateForKey(QStringLiteral("l")), QString::fromUtf8("ł"));
        QVERIFY(model.alternatesForKey(QStringLiteral("z")).contains(QString::fromUtf8("ź")));
        model.toggleSymbols();
        QVERIFY(model.symbolRows().at(1).contains(QString::fromUtf8("zł")));
        model.toggleSymbols();
        model.setLanguage(QStringLiteral("pt"));
        QVERIFY(model.alternatesForKey(QStringLiteral("a")).contains(QString::fromUtf8("ã")));
        model.setLanguage(QStringLiteral("it"));
        QVERIFY(model.alternatesForKey(QStringLiteral("e")).contains(QString::fromUtf8("è")));
        model.toggleSymbols();
        QVERIFY(model.symbolRows().at(1).contains(QString::fromUtf8("€")));
        model.toggleSymbols();
        model.setLanguage(QStringLiteral("nl"));
        QVERIFY(model.alternatesForKey(QStringLiteral("e")).contains(QString::fromUtf8("ë")));
        // English keeps its own list (no change).
        model.setLanguage(QStringLiteral("en"));
        QVERIFY(model.alternatesForKey(QStringLiteral("n")).contains(QString::fromUtf8("ñ")));
    }

    void exposesExpectedRowsForEachLanguage()
    {
        Tastra::KeyboardModel model;

        QCOMPARE(model.row1().join(QString()), QStringLiteral("qwertyuiop"));
        QCOMPARE(model.row2().join(QString()), QStringLiteral("asdfghjkl"));
        QCOMPARE(model.row3().join(QString()), QStringLiteral("zxcvbnm"));

        model.setLanguage(QStringLiteral("de"));
        QCOMPARE(model.row1().join(QString()), QStringLiteral("qwertzuiopü"));
        QCOMPARE(model.row2().join(QString()), QStringLiteral("asdfghjklöä"));
        QCOMPARE(model.row3().join(QString()), QStringLiteral("yxcvbnm"));

        model.setLanguage(QStringLiteral("uk"));
        QCOMPARE(model.row1().join(QString()), QStringLiteral("йцукенгшщзхї"));
        QCOMPARE(model.row2().join(QString()), QStringLiteral("фівапролджє"));
        QCOMPARE(model.row3().join(QString()), QStringLiteral("ячсмитьбю"));

        model.setLanguage(QStringLiteral("ru"));
        QCOMPARE(model.row1().join(QString()), QStringLiteral("йцукенгшщзхъ"));
        QCOMPARE(model.row2().join(QString()), QStringLiteral("фывапролджэ"));
        QCOMPARE(model.row3().join(QString()), QStringLiteral("ячсмитьбю"));
    }

    void otherLetterLayoutsPerLanguage()
    {
        // Gboard "Select a layout": QWERTY, QWERTZ, AZERTY, Dvorak, Colemak for
        // English, QWERTZ and QWERTY for German; the usual one first, Morse
        // code last (every language here).
        using Tastra::KeyboardModel;
        QCOMPARE(KeyboardModel::layoutVariants(QStringLiteral("en")),
                 (QStringList{QStringLiteral("qwerty"), QStringLiteral("qwertz"), QStringLiteral("azerty"),
                              QStringLiteral("dvorak"), QStringLiteral("colemak"), QStringLiteral("morse")}));
        QCOMPARE(KeyboardModel::layoutVariants(QStringLiteral("de")),
                 (QStringList{QStringLiteral("qwertz"), QStringLiteral("qwerty"), QStringLiteral("morse")}));
        QCOMPARE(KeyboardModel::layoutVariants(QStringLiteral("ru")),
                 (QStringList{QStringLiteral("jcuken"), QStringLiteral("morse")}));
        QCOMPARE(KeyboardModel::layoutVariantLabel(QStringLiteral("ru"), QStringLiteral("morse")), QStringLiteral("Morse"));
        // Morse keeps the first layout's rows for the lexicon.
        QCOMPARE(KeyboardModel::rowsForLanguage(QStringLiteral("ru"), QStringLiteral("morse")),
                 KeyboardModel::rowsForLanguage(QStringLiteral("ru")));
        KeyboardModel model;
        model.setLanguage(QStringLiteral("en"));
        QCOMPARE(model.row1().join(QString()), QStringLiteral("qwertyuiop"));
        model.setLayoutVariant(QStringLiteral("en"), QStringLiteral("azerty"));
        QCOMPARE(model.row1().join(QString()), QStringLiteral("azertyuiop"));
        QCOMPARE(model.row2().join(QString()), QStringLiteral("qsdfghjklm"));
        QCOMPARE(model.row3().join(QString()), QStringLiteral("wxcvbn"));
        // Symbol hints follow the key position: "a" is now where "q" was (1).
        QVERIFY(model.alternatesForKey(QStringLiteral("a")).contains(QStringLiteral("1")));
        model.setLayoutVariant(QStringLiteral("en"), QStringLiteral("colemak"));
        QCOMPARE(model.row2().join(QString()), QStringLiteral("arstdhneio"));
        model.setLayoutVariant(QStringLiteral("en"), QStringLiteral("nonsense"));        // ignored
        QCOMPARE(model.layoutVariant(QStringLiteral("en")), QStringLiteral("colemak"));
        // Each language keeps its own choice.
        model.setLanguage(QStringLiteral("de"));
        QCOMPARE(model.row1().join(QString()), QString::fromUtf8("qwertzuiopü"));
        model.setLayoutVariant(QStringLiteral("de"), QStringLiteral("qwerty"));
        QCOMPARE(model.row3().join(QString()), QStringLiteral("zxcvbnm"));
        model.setLanguage(QStringLiteral("en"));
        QCOMPARE(model.row1().join(QString()), QStringLiteral("qwfpgjluy"));
        QCOMPARE(KeyboardModel::layoutVariantLabel(QStringLiteral("en"), QStringLiteral("dvorak")), QStringLiteral("Dvorak"));
        QCOMPARE(KeyboardModel::layoutVariantLabel(QStringLiteral("uk"), QString::fromUtf8("jcuken")), QString::fromUtf8("ЙЦУКЕН"));
    }

    void morseSignsPerLanguage()
    {
        using namespace Tastra;
        // ITU-R M.1677-1 letters, digits and marks; national letters where
        // the language has them (Wikipedia "Morse code", "Morse code for
        // non-Latin alphabets"; DARC for German).
        QCOMPARE(morseText(QStringLiteral(".-"), QStringLiteral("en")), QStringLiteral("a"));
        QCOMPARE(morseText(QStringLiteral("--.."), QStringLiteral("en")), QStringLiteral("z"));
        QCOMPARE(morseText(QStringLiteral("-----"), QStringLiteral("en")), QStringLiteral("0"));
        QCOMPARE(morseText(QStringLiteral(".-.-.-"), QStringLiteral("en")), QStringLiteral("."));
        QCOMPARE(morseText(QStringLiteral(".--.-."), QStringLiteral("en")), QStringLiteral("@"));
        QCOMPARE(morseText(QStringLiteral("...-..-"), QStringLiteral("en")), QStringLiteral("$"));
        QVERIFY(morseText(QStringLiteral(".-.-"), QStringLiteral("en")).isEmpty());          // ä is German
        QCOMPARE(morseText(QStringLiteral(".-.-"), QStringLiteral("de")), QString::fromUtf8("ä"));
        QCOMPARE(morseText(QStringLiteral("----"), QStringLiteral("de")), QStringLiteral("ch"));
        QCOMPARE(morseText(QStringLiteral(".-.-"), QStringLiteral("ru")), QString::fromUtf8("я"));
        QCOMPARE(morseText(QStringLiteral("---."), QStringLiteral("ru")), QString::fromUtf8("ч"));
        QCOMPARE(morseText(QStringLiteral("-.--"), QStringLiteral("ru")), QString::fromUtf8("ы"));
        QCOMPARE(morseText(QStringLiteral("..-.."), QStringLiteral("ru")), QString::fromUtf8("э"));
        QCOMPARE(morseText(QStringLiteral("-.--"), QStringLiteral("uk")), QString::fromUtf8("и"));
        QCOMPARE(morseText(QStringLiteral(".."), QStringLiteral("uk")), QString::fromUtf8("і"));
        QCOMPARE(morseText(QStringLiteral(".---."), QStringLiteral("uk")), QString::fromUtf8("ї"));
        QCOMPARE(morseText(QStringLiteral("..-.."), QStringLiteral("uk")), QString::fromUtf8("є"));
        QCOMPARE(morseText(QStringLiteral("--..--"), QStringLiteral("ru")), QStringLiteral(","));
        // Every sign of a language has its own code, and every letter of the
        // keyboard has a sign (except ъ, ё, ß, ґ: see morsecode.h).
        for (const QString &code : {QStringLiteral("en"), QStringLiteral("de"), QStringLiteral("ru"), QStringLiteral("uk")}) {
            QSet<QString> codes;
            QSet<QString> texts;
            for (const MorseSign &sign : morseTable(code)) {
                QVERIFY2(!codes.contains(sign.code), qPrintable(code + QLatin1Char(' ') + sign.code));
                codes.insert(sign.code);
                texts.insert(sign.text);
                QVERIFY(sign.code.size() <= MorseKeyer::MaxLength);
            }
            for (const QString &row : KeyboardModel::rowsForLanguage(code)) {
                for (const QChar letter : row) {
                    if (QStringLiteral("ъёßґ").contains(letter)) continue;
                    QVERIFY2(texts.contains(QString(letter)), qPrintable(code + QLatin1Char(' ') + letter));
                }
            }
        }
        // What a code can still become: shortest first.
        const QList<MorseSign> next = morseContinuations(QStringLiteral(".-"), QStringLiteral("en"), 4);
        QCOMPARE(next.size(), 4);
        QCOMPARE(next.at(0).code.size(), 3);                                   // r .-. and w .--
        QCOMPARE(next.at(0).text, QStringLiteral("r"));
        QCOMPARE(next.at(1).text, QStringLiteral("w"));
        QVERIFY(morseContinuations(QStringLiteral("...-..-"), QStringLiteral("en"), 4).isEmpty());

        MorseKeyer keyer;
        QVERIFY(!keyer.pending());
        keyer.dot();
        keyer.dash();
        QCOMPARE(keyer.code(), QStringLiteral(".-"));
        QCOMPARE(keyer.text(), QStringLiteral("a"));
        QVERIFY(keyer.backspace());
        QCOMPARE(keyer.text(), QStringLiteral("e"));
        QCOMPARE(keyer.take(), QStringLiteral("e"));
        QVERIFY(!keyer.pending());
        QVERIFY(!keyer.backspace());
        for (int i = 0; i < MorseKeyer::MaxLength; ++i) QVERIFY(keyer.dash());
        QVERIFY(!keyer.dash());                                                 // longer than any sign
        QVERIFY(keyer.take().isEmpty());                                        // no sign: nothing
        keyer.dot();
        keyer.setLanguage(QStringLiteral("ru"));                                // another language: forgotten
        QVERIFY(!keyer.pending());
    }

    void changingLanguageClearsShiftState()
    {
        Tastra::KeyboardModel model;

        model.pressShift();
        QVERIFY(model.uppercase());

        model.setLanguage(QStringLiteral("uk"));

        QVERIFY(!model.uppercase());
        QVERIFY(!model.capsLock());
    }

    void exposesLanguageSpecificLongPressAlternates()
    {
        Tastra::KeyboardModel model;

        QCOMPARE(model.alternateForKey(QStringLiteral("s")), QString());

        model.setLanguage(QStringLiteral("de"));
        QCOMPARE(model.alternateForKey(QStringLiteral("s")), QStringLiteral("ß"));
        model.pressShift();
        QCOMPARE(model.alternateForKey(QStringLiteral("s")), QStringLiteral("ẞ"));

        model.setLanguage(QStringLiteral("uk"));
        QCOMPARE(model.alternateForKey(QStringLiteral("г")), QStringLiteral("ґ"));

        model.setLanguage(QStringLiteral("ru"));
        QCOMPARE(model.alternateForKey(QStringLiteral("е")), QStringLiteral("ё"));

        model.toggleSymbols();
        QCOMPARE(model.alternateForKey(QStringLiteral("е")), QString());
    }

    void toolbarRegistryRejectsDuplicateIds()
    {
        Tastra::ToolbarRegistry registry;

        Tastra::ToolbarAction first;
        first.id = QStringLiteral("language");
        first.label = QStringLiteral("Language");
        first.kind = Tastra::ToolbarActionKind::OpenPanel;
        first.panel = Tastra::PanelId::Language;

        QVERIFY(registry.registerAction(first));
        QVERIFY(!registry.registerAction(first));
        QCOMPARE(registry.actions().size(), 1);
    }

    void defaultToolbarOrderIsStableAndHiddenActionsAreFiltered()
    {
        const auto registry = Tastra::ToolbarRegistry::createDefault();
        const Tastra::ToolbarModel model(registry);

        QCOMPARE(
            model.visibleActionIds(),
            QStringList({
                QStringLiteral("clipboard"),
                QStringLiteral("emoji"),
                QStringLiteral("text-editing"),
                QStringLiteral("resize"),
                QStringLiteral("settings"),
            })
        );

        const auto all = registry.actions();
        QCOMPARE(all.size(), 7);
        QCOMPARE(all.at(0).id, QStringLiteral("language"));
        QCOMPARE(all.at(1).id, QStringLiteral("clipboard"));
        QCOMPARE(all.at(2).id, QStringLiteral("emoji"));
        QCOMPARE(all.at(3).id, QStringLiteral("text-editing"));
        QCOMPARE(all.at(4).id, QStringLiteral("resize"));
        QCOMPARE(all.at(5).id, QStringLiteral("one-handed"));
        QCOMPARE(all.at(6).id, QStringLiteral("settings"));
    }

    void disabledToolbarActionCannotActivate()
    {
        Tastra::ToolbarRegistry registry;

        Tastra::ToolbarAction action;
        action.id = QStringLiteral("disabled");
        action.label = QStringLiteral("Disabled");
        action.kind = Tastra::ToolbarActionKind::OpenPanel;
        action.panel = Tastra::PanelId::Settings;
        action.enabled = false;

        QVERIFY(registry.registerAction(action));

        const Tastra::ToolbarModel model(registry);
        Tastra::PanelManager panels;

        QVERIFY(!model.activate(QStringLiteral("disabled"), panels));
        QCOMPARE(panels.activePanel(), Tastra::PanelId::Typing);
    }

    void hiddenLanguageToolbarActionDoesNotActivate()
    {
        const auto registry = Tastra::ToolbarRegistry::createDefault();
        const Tastra::ToolbarModel model(registry);
        Tastra::PanelManager panels;

        QCOMPARE(panels.activePanel(), Tastra::PanelId::Typing);
        QVERIFY(!model.activate(QStringLiteral("language"), panels));
        QCOMPARE(panels.activePanel(), Tastra::PanelId::Typing);
    }

    void panelManagerReturnsToTyping()
    {
        Tastra::PanelManager panels;

        QCOMPARE(panels.activePanel(), Tastra::PanelId::Typing);
        QVERIFY(panels.openPanel(Tastra::PanelId::Emoji));
        QCOMPARE(panels.activePanel(), Tastra::PanelId::Emoji);

        QVERIFY(panels.returnToTyping());
        QCOMPARE(panels.activePanel(), Tastra::PanelId::Typing);
        QVERIFY(!panels.returnToTyping());
    }

    void exposesToolbarDescriptorsForUi()
    {
        const auto registry = Tastra::ToolbarRegistry::createDefault();
        const Tastra::ToolbarModel model(registry);

        const auto actions = model.visibleActions();
        QCOMPARE(actions.size(), 5);
        QCOMPARE(actions.at(0).id, QStringLiteral("clipboard"));
        QCOMPARE(actions.at(1).id, QStringLiteral("emoji"));
        QCOMPARE(actions.at(2).id, QStringLiteral("text-editing"));
        QCOMPARE(actions.at(3).id, QStringLiteral("resize"));
        QCOMPARE(actions.at(4).id, QStringLiteral("settings"));
    }

    void exposesSupportedLanguageList()
    {
        Tastra::KeyboardModel model;

        QCOMPARE(
            model.languageCodes(),
            QStringList({
                QStringLiteral("en"),
                QStringLiteral("de"),
                QStringLiteral("uk"),
                QStringLiteral("ru"),
                QStringLiteral("es"),
                QStringLiteral("fr"),
                QStringLiteral("it"),
                QStringLiteral("pt"),
                QStringLiteral("pl"),
                QStringLiteral("nl"),
            })
        );

        QCOMPARE(
            model.languageLabels(),
            QStringList({
                QStringLiteral("English"),
                QStringLiteral("Deutsch"),
                QStringLiteral("Українська"),
                QStringLiteral("Русский"),
                QString::fromUtf8("Español"),
                QString::fromUtf8("Français"),
                QStringLiteral("Italiano"),
                QString::fromUtf8("Português"),
                QStringLiteral("Polski"),
                QStringLiteral("Nederlands"),
            })
        );
    }

    void languageActionStaysOutOfVisibleToolbar()
    {
        const auto registry = Tastra::ToolbarRegistry::createDefault();
        const auto *language = registry.actionById(QStringLiteral("language"));

        QVERIFY(language != nullptr);
        QVERIFY(!language->visible);
        QCOMPARE(language->panel, Tastra::PanelId::Language);
    }
};

QTEST_APPLESS_MAIN(KeyboardModelTest)

#include "tst_keyboardmodel.moc"
