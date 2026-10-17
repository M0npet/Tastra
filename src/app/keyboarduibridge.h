// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/clipboardhistory.h"
#include "core/emojicatalog.h"
#include "core/keyboardmodel.h"
#include "core/panelmanager.h"
#include "core/toolbarregistry.h"
#include "core/toolbarmodel.h"
#include "core/typingengine.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class QSoundEffect;
class QQmlEngine;

namespace Tastra
{

class KeyboardController;

class KeyboardUiBridge final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool uppercase READ uppercase NOTIFY keyboardStateChanged)
    Q_PROPERTY(bool capsLock READ capsLock NOTIFY keyboardStateChanged)
    Q_PROPERTY(bool symbolsActive READ symbolsActive NOTIFY keyboardStateChanged)
    Q_PROPERTY(QString languageCode READ languageCode NOTIFY keyboardStateChanged)
    Q_PROPERTY(QString languageLabel READ languageLabel NOTIFY keyboardStateChanged)
    Q_PROPERTY(QStringList row1 READ row1 NOTIFY keyboardStateChanged)
    Q_PROPERTY(QStringList row2 READ row2 NOTIFY keyboardStateChanged)
    Q_PROPERTY(QStringList row3 READ row3 NOTIFY keyboardStateChanged)
    Q_PROPERTY(QString activePanel READ activePanel NOTIFY toolbarStateChanged)
    Q_PROPERTY(QStringList toolbarActionIds READ toolbarActionIds NOTIFY toolbarStateChanged)
    Q_PROPERTY(QVariantList toolbarActions READ toolbarActions NOTIFY toolbarStateChanged)
    // Gboard: which features sit on the toolbar, in which order (settings
    // always stays: it is the way back to this choice).
    Q_PROPERTY(QStringList toolbarChoices READ toolbarChoices NOTIFY toolbarStateChanged)
    Q_PROPERTY(QStringList languageCodes READ languageCodes NOTIFY keyboardStateChanged)
    Q_PROPERTY(QStringList languageLabels READ languageLabels NOTIFY keyboardStateChanged)
    // Gboard symbol pages ("?123" / "=\<").
    Q_PROPERTY(int symbolPage READ symbolPage NOTIFY keyboardStateChanged)
    Q_PROPERTY(QStringList symbolRow1 READ symbolRow1 NOTIFY keyboardStateChanged)
    Q_PROPERTY(QStringList symbolRow2 READ symbolRow2 NOTIFY keyboardStateChanged)
    Q_PROPERTY(QStringList symbolRow3 READ symbolRow3 NOTIFY keyboardStateChanged)
    // Gboard: the user chooses which languages the globe cycles through.
    Q_PROPERTY(QStringList allLanguageCodes READ allLanguageCodes CONSTANT)
    Q_PROPERTY(QStringList allLanguageLabels READ allLanguageLabels CONSTANT)
    Q_PROPERTY(QString currentWord READ currentWord NOTIFY suggestionsChanged)
    Q_PROPERTY(QString autocorrectSuggestion READ autocorrectSuggestion NOTIFY suggestionsChanged)
    // Personal dictionary (Gboard "Touch again to save" flow).
    Q_PROPERTY(bool typedWordUnknown READ typedWordUnknown NOTIFY suggestionsChanged)
    Q_PROPERTY(QString saveWordCandidate READ saveWordCandidate NOTIFY suggestionsChanged)
    // Gboard: just-copied text offered for pasting (one minute, idle strip).
    // What the chip shows of the just-copied text (the start of it; a long
    // text would take the layout seconds). Tapping it pastes all of it.
    Q_PROPERTY(QString clipboardSuggestion READ clipboardSuggestionPreview NOTIFY suggestionsChanged)
    // Gboard "paste sections": e-mail, web address, phone number, date, time
    // or number found in what was just copied, each as its own chip.
    Q_PROPERTY(QStringList clipboardParts READ clipboardParts NOTIFY suggestionsChanged)
    Q_PROPERTY(QStringList userWords READ userWords NOTIFY userWordsChanged)
    // "text", "email", "url", "number" or "phone" — drives Gboard-like layouts.
    Q_PROPERTY(QString inputPurpose READ inputPurpose NOTIFY inputContextChanged)
    Q_PROPERTY(QStringList suggestions READ suggestions NOTIFY suggestionsChanged)
    // The chip of the strip that completes a phrase (Gboard Smart Compose),
    // and the length of its first word, shown as on any chip; the habitual
    // words after it are dimmed, like Gboard's grey inline completion.
    Q_PROPERTY(QString phraseSuggestion READ phraseSuggestion NOTIFY suggestionsChanged)
    Q_PROPERTY(int phraseHeadLength READ phraseHeadLength NOTIFY suggestionsChanged)
    Q_PROPERTY(bool secureInput READ secureInput NOTIFY inputContextChanged)

    Q_PROPERTY(bool amoled READ amoled WRITE setAmoled NOTIFY uiPreferencesChanged)
    // "system" (follows Plasma light/dark), "light", "dark" or "amoled".
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY uiPreferencesChanged)
    // Gboard theme gallery: a colour over the light / dark theme ("" = the
    // plain one), or an own photo behind translucent keys, dimmed by
    // themeImageDim percent.
    Q_PROPERTY(QString themeColor READ themeColor WRITE setThemeColor NOTIFY uiPreferencesChanged)
    Q_PROPERTY(QStringList themeColors READ themeColors CONSTANT)
    Q_PROPERTY(QString themeImage READ themeImage WRITE setThemeImage NOTIFY uiPreferencesChanged)
    Q_PROPERTY(int themeImageDim READ themeImageDim NOTIFY uiPreferencesChanged)
    Q_PROPERTY(QString picturesFolder READ picturesFolder CONSTANT)
    // Gboard "Gradients": a gradient behind translucent keys, by name ("" =
    // none). A gradient and a photo exclude each other.
    Q_PROPERTY(QString themeGradient READ themeGradient WRITE setThemeGradient NOTIFY uiPreferencesChanged)
    Q_PROPERTY(QStringList themeGradients READ themeGradients CONSTANT)
    Q_PROPERTY(QString effectiveTheme READ effectiveTheme NOTIFY uiPreferencesChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    // Interface language: "system" (Plasma's language if Tastra has it,
    // else English), "en", "de", "ru" or "uk". Label for the settings row.
    Q_PROPERTY(QString uiLanguage READ uiLanguage NOTIFY uiLanguageChanged)
    Q_PROPERTY(QString uiLanguageLabel READ uiLanguageLabel NOTIFY uiLanguageChanged)
    Q_PROPERTY(bool symbolHints READ symbolHints WRITE setSymbolHints NOTIFY uiPreferencesChanged)
    Q_PROPERTY(bool numberRow READ numberRow WRITE setNumberRow NOTIFY uiPreferencesChanged)
    // Gboard Preferences: the key next to the comma is the emoji key or the
    // language (globe) key; with the emoji key on, the language key is off.
    Q_PROPERTY(bool showEmojiKey READ showEmojiKey WRITE setShowEmojiKey NOTIFY uiPreferencesChanged)
    // Gboard "Select a layout": QWERTY, QWERTZ, AZERTY, Dvorak, Colemak…
    // for the current language.
    Q_PROPERTY(QString letterLayout READ letterLayout NOTIFY keyboardStateChanged)
    Q_PROPERTY(bool letterLayoutChoice READ letterLayoutChoice NOTIFY keyboardStateChanged)
    Q_PROPERTY(bool showLanguageKey READ showLanguageKey WRITE setShowLanguageKey NOTIFY uiPreferencesChanged)
    Q_PROPERTY(bool autoSpaceAfterPunctuation READ autoSpaceAfterPunctuation WRITE setAutoSpaceAfterPunctuation NOTIFY typingPreferencesChanged)
    // Gboard "Emoji fast-access row": recent emoji above the keys.
    Q_PROPERTY(bool emojiRow READ emojiRow WRITE setEmojiRow NOTIFY uiPreferencesChanged)
    Q_PROPERTY(QStringList recentEmojis READ recentEmojis NOTIFY emojiChanged)
    Q_PROPERTY(QStringList emojiRowEmojis READ emojiRowEmojis NOTIFY emojiChanged)
    Q_PROPERTY(bool emojiSearchActive READ emojiSearchActive NOTIFY emojiSearchChanged)
    Q_PROPERTY(QString emojiSearchText READ emojiSearchText NOTIFY emojiSearchChanged)
    Q_PROPERTY(QStringList emojiSearchResults READ emojiSearchResults NOTIFY emojiSearchChanged)
    Q_PROPERTY(QString wordEntryStep READ wordEntryStep NOTIFY wordEntryChanged)
    Q_PROPERTY(QString wordEntryText READ wordEntryText NOTIFY wordEntryChanged)
    Q_PROPERTY(QStringList shortcutList READ shortcutList NOTIFY shortcutsChanged)
    // Gboard "Sound on keypress" (off by default) and "Show gesture trail".
    Q_PROPERTY(bool keySound READ keySound WRITE setKeySound NOTIFY uiPreferencesChanged)
    // Gboard "Key long press delay" (default 300 ms).
    Q_PROPERTY(int longPressDelay READ longPressDelay NOTIFY uiPreferencesChanged)
    Q_PROPERTY(int keySoundVolume READ keySoundVolume NOTIFY uiPreferencesChanged)
    Q_PROPERTY(bool canPlayKeySound READ canPlayKeySound CONSTANT)
    Q_PROPERTY(bool glideTrail READ glideTrail WRITE setGlideTrail NOTIFY uiPreferencesChanged)
    Q_PROPERTY(bool emojiSuggestionsEnabled READ emojiSuggestionsEnabled WRITE setEmojiSuggestionsEnabled NOTIFY typingPreferencesChanged)
    Q_PROPERTY(bool nextWordSuggestions READ nextWordSuggestions WRITE setNextWordSuggestions NOTIFY typingPreferencesChanged)
    Q_PROPERTY(bool phraseSuggestions READ phraseSuggestions WRITE setPhraseSuggestions NOTIFY typingPreferencesChanged)
    Q_PROPERTY(bool blockOffensive READ blockOffensive WRITE setBlockOffensive NOTIFY typingPreferencesChanged)
    Q_PROPERTY(QString layoutMode READ layoutMode WRITE setLayoutMode NOTIFY uiPreferencesChanged)
    Q_PROPERTY(bool voiceBuilt READ voiceBuilt NOTIFY voiceChanged)
    Q_PROPERTY(QString voiceState READ voiceState NOTIFY voiceChanged)
    Q_PROPERTY(QString voiceMessage READ voiceMessage NOTIFY voiceChanged)
    Q_PROPERTY(double keyScale READ keyScale WRITE setKeyScale NOTIFY uiPreferencesChanged)
    Q_PROPERTY(bool keyBorders READ keyBorders WRITE setKeyBorders NOTIFY uiPreferencesChanged)
    Q_PROPERTY(bool keyPopups READ keyPopups WRITE setKeyPopups NOTIFY uiPreferencesChanged)
    Q_PROPERTY(bool suggestionsEnabled READ suggestionsEnabled WRITE setSuggestionsEnabled NOTIFY typingPreferencesChanged)
    Q_PROPERTY(bool autocorrectEnabled READ autocorrectEnabled WRITE setAutocorrectEnabled NOTIFY typingPreferencesChanged)
    Q_PROPERTY(bool learningEnabled READ learningEnabled WRITE setLearningEnabled NOTIFY typingPreferencesChanged)
    Q_PROPERTY(bool autoCapitalizationEnabled READ autoCapitalizationEnabled WRITE setAutoCapitalizationEnabled NOTIFY typingPreferencesChanged)
    Q_PROPERTY(bool doubleSpacePeriodEnabled READ doubleSpacePeriodEnabled WRITE setDoubleSpacePeriodEnabled NOTIFY typingPreferencesChanged)
    Q_PROPERTY(bool glideEnabled READ glideEnabled WRITE setGlideEnabled NOTIFY typingPreferencesChanged)
    Q_PROPERTY(bool compositionEnabled READ compositionEnabled WRITE setCompositionEnabled NOTIFY typingPreferencesChanged)
    Q_PROPERTY(bool clipboardHistoryEnabled READ clipboardHistoryEnabled WRITE setClipboardHistoryEnabled NOTIFY clipboardChanged)

    Q_PROPERTY(QString clipboardText READ clipboardText NOTIFY clipboardChanged)
    Q_PROPERTY(QString clipboardPreview READ clipboardPreview NOTIFY clipboardChanged)
    Q_PROPERTY(bool hasClipboardText READ hasClipboardText NOTIFY clipboardChanged)
    // Text selected in the application (for Copy / Cut).
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    // Copy / Cut possible: a reported selection, or the editing shortcuts
    // (KWin fake input) to let the application copy its own selection.
    Q_PROPERTY(bool canCopy READ canCopy NOTIFY selectionChanged)
    Q_PROPERTY(bool canCut READ canCut NOTIFY selectionChanged)
    // Gboard's Select: the arrow keys extend the selection (Shift+arrows).
    Q_PROPERTY(bool canSelect READ canSelect NOTIFY selectionChanged)
    Q_PROPERTY(bool selectMode READ selectMode NOTIFY selectionChanged)
    // Gboard gesture delete: sliding left from Backspace selects words in the
    // application (KWin fake input), sliding back deselects, release deletes.
    Q_PROPERTY(bool deleteGestureSelects READ deleteGestureSelects NOTIFY selectionChanged)
    Q_PROPERTY(QStringList clipboardHistory READ clipboardHistory NOTIFY clipboardChanged)
    Q_PROPERTY(QStringList emojiItems READ emojiItems CONSTANT)
    Q_PROPERTY(QStringList emojiCategories READ emojiCategories NOTIFY emojiChanged)

public:
    explicit KeyboardUiBridge(
        KeyboardController &controller,
        KeyboardModel &model,
        QObject *parent = nullptr);

    bool uppercase() const;
    bool capsLock() const;
    bool symbolsActive() const;

    QString languageCode() const;
    QString languageLabel() const;
    QStringList row1() const;
    QStringList row2() const;
    QStringList row3() const;

    QString activePanel() const;
    QStringList toolbarActionIds() const;
    QVariantList toolbarActions() const;
    QStringList languageCodes() const;
    QStringList companionLanguages() const { return m_typingEngine.companionLanguages(); }
    bool waitForDictionaryForTesting(int timeoutMs) { return m_typingEngine.waitForDictionaryForTesting(timeoutMs); }
    QStringList languageLabels() const;
    QString currentWord() const;
    QString autocorrectSuggestion() const;
    bool typedWordUnknown() const;
    QString saveWordCandidate() const;
    QString clipboardSuggestion() const;
    QString clipboardSuggestionPreview() const;
    Q_INVOKABLE void pasteClipboardSuggestion();
    QStringList clipboardParts() const;
    Q_INVOKABLE void pasteClipboardPart(int index);
    QStringList userWords() const;
    QString inputPurpose() const;
    QStringList suggestions() const;

    bool amoled() const;
    QString theme() const;
    QString effectiveTheme() const;
    QString version() const;
    bool emojiSuggestionsEnabled() const;
    bool nextWordSuggestions() const;
    bool phraseSuggestions() const;
    QString phraseSuggestion() const;
    int phraseHeadLength() const;
    bool keySound() const;
    bool canPlayKeySound() const;
    bool glideTrail() const;
    bool autoSpaceAfterPunctuation() const;
    bool emojiRow() const;
    QStringList recentEmojis() const;
    // The fast-access row: recent emoji, filled up with frequent ones.
    QStringList emojiRowEmojis() const;
    bool symbolHints() const;
    bool numberRow() const;
    bool showEmojiKey() const { return m_showEmojiKey; }
    bool showLanguageKey() const { return m_showLanguageKey && !m_showEmojiKey; }
    Q_INVOKABLE void setShowEmojiKey(bool show);
    Q_INVOKABLE void setShowLanguageKey(bool show);
    // Gboard: long-press the comma for the one-handed keyboard.
    Q_INVOKABLE void toggleOneHanded();
    // Every feature that can sit on the toolbar: the shown ones in their
    // order first, then the others.
    QStringList toolbarChoices() const;
    Q_INVOKABLE bool isToolbarItemShown(const QString &id) const { return m_toolbarOrder.contains(id); }
    Q_INVOKABLE void setToolbarItemShown(const QString &id, bool shown);
    Q_INVOKABLE void moveToolbarItem(const QString &id, int delta);
    QString themeColor() const { return m_themeColor; }
    QStringList themeColors() const;
    Q_INVOKABLE void setThemeColor(const QString &color);
    QString themeImage() const { return m_themeImage; }
    QString themeGradient() const { return m_themeGradient; }
    // Each gradient as "name:#top:#bottom".
    QStringList themeGradients() const;
    Q_INVOKABLE void setThemeGradient(const QString &name);
    // A local picture (file URL or path); anything else is refused.
    Q_INVOKABLE void setThemeImage(const QString &image);
    int themeImageDim() const { return m_themeImageDim; }
    Q_INVOKABLE void cycleThemeImageDim();
    QString picturesFolder() const;
    // The newest pictures in the Pictures folder (file URLs), for choosing.
    Q_INVOKABLE QStringList pictureFiles() const;
    void setPicturesFolderForTesting(const QString &folder) { m_picturesFolderOverride = folder; }
    QString letterLayout() const;
    bool letterLayoutChoice() const;
    Q_INVOKABLE void cycleLetterLayout();
    bool blockOffensive() const;
    QString layoutMode() const;
    bool voiceBuilt() const;
    QString voiceState() const;
    QString voiceMessage() const;
    // Optional: only set when the binary was built with offline voice input.
    void setVoiceController(class VoiceController *voice);
    // What the speech recogniser heard: a Gboard voice command ("delete last
    // word", "send", "smiley emoji"…) or dictation to type.
    void handleRecognizedSpeech(const QString &text);
    void setKeyboardHider(class KeyboardHider *hider);
    // Gboard ⌄: commit the word being composed, then hide the keyboard.
    Q_INVOKABLE void hideKeyboard();
    Q_PROPERTY(bool canHideKeyboard READ canHideKeyboard CONSTANT)
    bool canHideKeyboard() const;
    double keyScale() const;
    bool keyBorders() const;
    bool keyPopups() const;
    bool suggestionsEnabled() const;
    bool autocorrectEnabled() const;
    bool learningEnabled() const;
    bool autoCapitalizationEnabled() const;
    bool doubleSpacePeriodEnabled() const;
    bool glideEnabled() const;
    bool compositionEnabled() const;
    bool secureInput() const;
    bool clipboardHistoryEnabled() const;

    QString clipboardText() const;
    // The start of the clipboard text for display; dots for a password.
    QString clipboardPreview() const;
    bool hasClipboardText() const;
    QStringList clipboardHistory() const;
    QStringList emojiItems() const;
    QStringList emojiCategories() const;

    Q_INVOKABLE QVariantMap layoutMetrics(bool portrait) const;
    Q_INVOKABLE QString alternateForKey(const QString &text) const;
    // Gboard's ":-)" tab: a text face goes in as typed, not into "Recent".
    Q_INVOKABLE void insertEmoticon(const QString &face);
    // Gboard emoji search: the keyboard's own letters type the query (an
    // input panel never gets keyboard focus, so a text field in it could not
    // be typed into). Keys go to the query until an emoji is chosen, Enter
    // or the close button.
    bool emojiSearchActive() const { return m_emojiSearchActive; }
    QString emojiSearchText() const { return m_emojiSearchText; }
    QStringList emojiSearchResults() const;
    Q_INVOKABLE void startEmojiSearch();
    Q_INVOKABLE void stopEmojiSearch();
    // Gboard "add word": a personal-dictionary word, then an optional
    // shortcut for it, typed on the keyboard itself (Settings -> Dictionary).
    QString wordEntryStep() const { return m_wordEntryStep; }    // "", "word" or "shortcut"
    QString wordEntryText() const { return m_wordEntryText; }
    QStringList shortcutList() const;                              // "shortcut = expansion", sorted
    Q_INVOKABLE void startWordEntry();
    Q_INVOKABLE void stopWordEntry();                              // ✕: nothing is added
    Q_INVOKABLE void confirmWordEntry();                           // Enter / ✓
    Q_INVOKABLE void removeShortcut(const QString &shortcut);
    Q_INVOKABLE QStringList emojiSearch(const QString &query, const QString &category = QStringLiteral("All")) const;

    Q_INVOKABLE void tapLetter(const QString &letter);
    // dx/dy: touch point relative to the key centre, in key sizes.
    Q_INVOKABLE void tapLetterAt(const QString &letter, qreal dx, qreal dy);
    Q_INVOKABLE void tapAlternate(const QString &base);
    Q_INVOKABLE void tapText(const QString &text);
    Q_INVOKABLE void selectSuggestion(const QString &word);
    Q_INVOKABLE void shift();
    // A Shift slide that ended on no letter: back to lowercase (not to Caps
    // Lock, where a second Shift press would go).
    Q_INVOKABLE void cancelOneShotShift();
    Q_INVOKABLE void toggleSymbols();
    Q_INVOKABLE void nextLanguage();
    Q_INVOKABLE void toggleSymbolPage();
    int symbolPage() const;
    QStringList symbolRow1() const;
    QStringList symbolRow2() const;
    QStringList symbolRow3() const;
    Q_INVOKABLE bool isLanguageEnabled(const QString &code) const;
    Q_INVOKABLE void setLanguageEnabled(const QString &code, bool enabled);
    QStringList allLanguageCodes() const;
    QStringList allLanguageLabels() const;
    Q_INVOKABLE void setLanguage(const QString &code);
    Q_INVOKABLE void activateToolbarAction(const QString &id);
    Q_INVOKABLE void openLanguagePanel();
    Q_INVOKABLE void closePanel();
    Q_INVOKABLE void space();
    Q_INVOKABLE void backspace();
    Q_INVOKABLE void backspaceRepeated(int count);
    Q_INVOKABLE void deleteForward();
    Q_INVOKABLE void moveLeft();
    Q_INVOKABLE void moveRight();
    Q_INVOKABLE void moveCursor(int delta);
    Q_INVOKABLE void moveHome();
    Q_INVOKABLE void moveEnd();
    Q_INVOKABLE void enter();

    Q_INVOKABLE void beginGlide(const QString &key);
    Q_INVOKABLE void glideThrough(const QString &key);
    Q_INVOKABLE QString endGlide();
    // Glide from the finger's path (root coordinates) and the letter keys'
    // centres ({"a": point, ...}); falls back to the key sequence.
    Q_INVOKABLE QString endGlidePath(const QVariantList &points, const QVariantMap &keyCentres, double keyWidth);

    Q_INVOKABLE void pasteClipboard();
    Q_INVOKABLE void copySelection();
    Q_INVOKABLE void cutSelection();
    Q_INVOKABLE void selectAll();
    // Ctrl+Z / Ctrl+Shift+Z in the application (with the editing shortcuts).
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void toggleSelectMode();
    bool hasSelection() const { return !m_selectedText.isEmpty() && !m_secureInput; }
    // With the editing shortcuts Copy and Cut work on the application's own
    // selection; an application that reports its text says whether there is
    // one (Cut with nothing selected cuts the line in some editors).
    bool canCopy() const { return !m_secureInput && (!m_selectedText.isEmpty() || (m_chords && !m_surroundingKnown)); }
    // Not in terminals: Ctrl+X, Ctrl+A, Ctrl+Z mean something else there.
    bool canCut() const { return canCopy() && !m_terminal; }
    bool canSelect() const { return m_chords != nullptr && !m_terminal; }
    bool selectMode() const { return m_selectMode && m_chords; }
    bool deleteGestureSelects() const { return canSelect() && !m_secureInput; }
    // `words`: how many words before the cursor are selected now (from the
    // start of the slide); the application's selection follows.
    Q_INVOKABLE void deleteGestureTo(int words);
    // The finger lifted: deletes what is selected. Cancelled: deselects.
    Q_INVOKABLE void endDeleteGesture();
    Q_INVOKABLE void cancelDeleteGesture();
    // Sends editing shortcuts as keyboard input (KWin fake input). Not owned.
    void setKeyChordSender(class KeyChordSender *sender);
    // The clipboard the keyboard reads and owns: KWin's data control in the
    // app; null restores QClipboard. Not owned.
    void setSystemClipboard(class SystemClipboard *clipboard);
    Q_INVOKABLE void pasteClipboardHistory(int index);
    Q_INVOKABLE void removeClipboardHistory(int index);
    Q_INVOKABLE void clearClipboard();
    Q_INVOKABLE void clearClipboardHistory();

    Q_INVOKABLE void setAmoled(bool enabled);
    Q_INVOKABLE void setTheme(const QString &theme);
    Q_INVOKABLE void cycleTheme();
    Q_INVOKABLE void cycleUiLanguage();
    Q_INVOKABLE void setUiLanguage(const QString &setting);
    QString uiLanguage() const { return m_uiLanguage; }
    QString uiLanguageLabel() const;
    // The engine whose qsTr() texts are re-evaluated when the language changes.
    void setQmlEngine(QQmlEngine *engine);
    Q_INVOKABLE void setEmojiSuggestionsEnabled(bool enabled);
    Q_INVOKABLE void setNextWordSuggestions(bool enabled);
    Q_INVOKABLE void setPhraseSuggestions(bool enabled);
    // Gboard clipboard: pinned items stay (and are the only ones on disk).
    Q_INVOKABLE bool isClipboardPinned(const QString &text) const;
    Q_INVOKABLE void toggleClipboardPin(const QString &text);
    Q_INVOKABLE void setKeySound(bool enabled);
    Q_INVOKABLE void cycleLongPressDelay();
    Q_INVOKABLE void moveUp();
    Q_INVOKABLE void moveDown();
    int longPressDelay() const;
    // Gboard "Volume on keypress", in percent.
    int keySoundVolume() const;
    Q_INVOKABLE void cycleKeySoundVolume();
    Q_INVOKABLE void setGlideTrail(bool enabled);
    // The key's sound: "space", "delete", "return" or a letter key's click
    // (LatinIME plays a different system sound for each).
    Q_INVOKABLE void keyFeedback(const QString &kind = QString());
    Q_INVOKABLE void setAutoSpaceAfterPunctuation(bool enabled);
    Q_INVOKABLE void setEmojiRow(bool enabled);
    Q_INVOKABLE void insertEmoji(const QString &glyph);
    Q_INVOKABLE QStringList emojiSkinTones(const QString &glyph) const;
    Q_INVOKABLE void setSymbolHints(bool enabled);
    Q_INVOKABLE void setNumberRow(bool enabled);
    Q_INVOKABLE QStringList alternatesForKey(const QString &key) const;
    // The symbol drawn in the key corner ("" when hints are off).
    Q_INVOKABLE QString symbolHintForKey(const QString &key) const;
    Q_INVOKABLE void tapAlternateText(const QString &text);
    Q_INVOKABLE void setBlockOffensive(bool enabled);
    // Gboard: long-press a suggestion to remove it.
    Q_INVOKABLE void forgetSuggestion(const QString &word);
    Q_INVOKABLE void addWordToDictionary(const QString &word);
    Q_INVOKABLE void removeWordFromDictionary(const QString &word);
    // "full", "left" or "right" (compact keyboard docked to one side).
    Q_INVOKABLE void setLayoutMode(const QString &mode);
    Q_INVOKABLE void cycleLayoutMode();
    Q_INVOKABLE void toggleVoice();
    Q_INVOKABLE void setKeyScale(double scale);
    Q_INVOKABLE void setKeyBorders(bool enabled);
    Q_INVOKABLE void setKeyPopups(bool enabled);
    Q_INVOKABLE void setSuggestionsEnabled(bool enabled);
    Q_INVOKABLE void setAutocorrectEnabled(bool enabled);
    Q_INVOKABLE void setLearningEnabled(bool enabled);
    Q_INVOKABLE void setAutoCapitalizationEnabled(bool enabled);
    Q_INVOKABLE void setDoubleSpacePeriodEnabled(bool enabled);
    Q_INVOKABLE void setGlideEnabled(bool enabled);
    Q_INVOKABLE void setCompositionEnabled(bool enabled);
    Q_INVOKABLE void setClipboardHistoryEnabled(bool enabled);
    Q_INVOKABLE void clearLearnedWords();
    // Saves the words learned since the last save (before the keyboard ends).
    void saveLearning();

    void resetInputContext();
    // The text input was switched off: remember a word still being composed
    // (TypingEngine::rememberCompositionBeforeDeactivation), then reset.
    void deactivateInputContext();
    void setSurroundingText(const QString &text, int cursorByte, int anchorByte);
    void setContentType(quint32 hint, quint32 purpose);
    void resetCompositionFromClient();
    void setPreferredLanguage(const QString &language);

Q_SIGNALS:
    void emojiSearchChanged();
    void wordEntryChanged();
    void shortcutsChanged();
    void keyboardStateChanged();
    // The input context went away: the UI ends every press and gesture.
    void pressesCancelled();
    void toolbarStateChanged();
    void uiPreferencesChanged();
    void voiceChanged();
    void emojiChanged();
    void userWordsChanged();
    void typingPreferencesChanged();
    void suggestionsChanged();
    void clipboardChanged();
    void inputContextChanged();
    void uiLanguageChanged();
    void selectionChanged();

private:
    void persistLanguage() const;
    void persistPreference(const QString &key, const QVariant &value) const;
    void typingStateDidChange();
    void captureClipboard();

    KeyboardController &m_controller;
    KeyboardModel &m_model;
    TypingEngine m_typingEngine;
    ClipboardHistory m_clipboardHistory;
    EmojiCatalog m_emojiCatalog;
    ToolbarRegistry m_toolbarRegistry;
    ToolbarModel m_toolbarModel;
    QStringList m_toolbarOrder;
    void persistToolbar();
    PanelManager m_panelManager;

    bool m_amoled = false;
    QString m_theme = QStringLiteral("system");
    QString m_themeColor;
    QString m_themeImage;
    QString m_themeGradient;
    int m_themeImageDim = 0;
    QString m_picturesFolderOverride;
    bool m_emojiSuggestions = true;
    bool m_nextWordSuggestions = true;
    bool m_phraseSuggestions = true;
    bool m_emojiSearchActive = false;
    QString m_emojiSearchText;
    QString m_wordEntryStep;
    QString m_wordEntryText;
    QString m_wordEntryWord;
    // Keys typed while the emoji search or the word entry is open go there.
    bool typeIntoField(const QString &text);
    bool inlineFieldActive() const { return m_emojiSearchActive || !m_wordEntryStep.isEmpty(); }
    // Ends the emoji search and the word entry without saving (another
    // panel, paste, hiding, a new input field).
    void cancelInlineFields();
    static QString shortcutsFilePath();
    // Rewrites shortcuts.txt keeping comments and other lines as they are:
    // the line for `key` is replaced by `line` (removed if empty) or appended.
    void writeShortcutLine(const QString &key, const QString &line);
    bool m_keySound = false;
    int m_longPressDelay = 300;
    int m_keySoundVolume = 35;
    bool m_glideTrail = true;
    QHash<QString, QSoundEffect *> m_keySounds;   // created on first use only
    bool m_autoSpaceAfterPunctuation = false;
    bool m_emojiRow = false;
    QString m_inputPurpose = QStringLiteral("text");
    QStringList m_enabledLanguages;         // empty = all
    QString m_saveCandidate;
    void updateForeignLayouts();
    QString m_freshClipboard;
    qint64 m_freshClipboardMs = 0;
    QHash<QString, QString> m_shortcuts;
    void loadShortcuts();
    bool m_symbolHints = true;
    bool m_numberRow = false;
    bool m_showEmojiKey = false;
    bool m_showLanguageKey = true;
    bool m_blockOffensive = true;
    QString m_layoutMode = QStringLiteral("full");
    class VoiceController *m_voice = nullptr;
    class KeyboardHider *m_hider = nullptr;
    double m_keyScale = 1.0;
    bool m_keyBorders = true;
    bool m_keyPopups = true;
    bool m_glideEnabled = true;
    bool m_secureInput = false;
    void applyUiLanguage();
    QString m_uiLanguage = QStringLiteral("system");
    class UiTranslator *m_translator = nullptr;
    QPointer<QQmlEngine> m_qmlEngine;
    class SystemClipboard *m_clipboard = nullptr;
    class QtSystemClipboard *m_qtClipboard = nullptr;
    QString m_selectedText;
    bool m_surroundingKnown = false;   // the application reports text and selection
    bool m_terminal = false;           // content purpose terminal
    class KeyChordSender *m_chords = nullptr;
    bool m_selectMode = false;
    bool m_deleteGestureActive = false;
    mutable QString m_clipboardPartsSource;
    mutable QStringList m_clipboardPartsCache;
    int m_deleteGestureWords = 0;
    // Finishes the word being typed before a shortcut acts on the text.
    void settleForShortcut();
    void setSelectMode(bool on);
};

}
