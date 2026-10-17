#!/usr/bin/env python3
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
errors = []

def ui_text():
    # The UI is Main.qml plus the panels and components beside it.
    return '\n'.join(p.read_text(encoding='utf-8') for p in sorted((ROOT / 'src/ui').glob('*.qml')))

def read(path):
    return ui_text() if path == 'src/ui/Main.qml' else (ROOT / path).read_text(encoding='utf-8')

def require(path, token, description=None):
    text = read(path)
    if token not in text:
        errors.append(f"{path}: missing {description or token!r}")

def forbid(path, token, description=None):
    text = read(path)
    if token in text:
        errors.append(f"{path}: forbidden {description or token!r}")

# Architecture / model checks.
require('src/core/toolbarregistry.h', 'language.visible = false;', 'hidden toolbar language action')
for action in ('clipboard', 'emoji', 'textEditing', 'settings'):
    require('src/core/toolbarregistry.h', f'{action}.visible = true;', f'visible {action} action')

require('src/core/keyboardmodel.cpp', 'QString KeyboardModel::alternateForKey', 'long-press alternate model')
require('src/core/keyboardmodel.cpp', 'QStringLiteral("ß")')
require('src/core/keyboardmodel.cpp', 'QStringLiteral("ґ")')
require('src/core/keyboardmodel.cpp', 'QStringLiteral("ё")')

for method in ('deleteForward', 'moveLeft', 'moveRight', 'moveHome', 'moveEnd'):
    require('src/core/inputmethodbackend.h', f'virtual void {method}() = 0;', f'backend {method}')
    require('src/core/keyboardcontroller.cpp', f'void KeyboardController::{method}()', f'controller {method}')
    require('src/platform/kwin/kwininputmethodv1backend.cpp', f'void KWinInputMethodV1Backend::{method}()', f'KWin {method}')

# Bridge/persistence checks.
for token in (
    'Q_PROPERTY(bool amoled',
    'Q_PROPERTY(double keyScale',
    'Q_PROPERTY(bool keyBorders',
    'Q_PROPERTY(bool keyPopups',
    'Q_PROPERTY(QString clipboardText',
    'Q_INVOKABLE void pasteClipboard()',
    'Q_INVOKABLE void clearClipboard()',
    'Q_INVOKABLE void tapAlternate',
):
    require('src/app/keyboarduibridge.h', token)

require('src/app/keyboarduibridge.cpp', 'QSettings settings;', 'settings persistence')
require('src/app/qtsystemclipboard.cpp', 'QGuiApplication::clipboard()', 'system clipboard access (QClipboard fallback)')
require('src/platform/kwin/datacontrolclipboard.cpp', 'ext_data_control_device_v1_set_selection', 'clipboard through KWin data control (0.7.1)')
require('tests/tst_datacontrolclipboard.cpp', 'theKeyboardsCopyReachesOtherApplications', 'data-control protocol test')
require('src/platform/kwin/fakeinputkeychords.cpp', 'org_kde_kwin_fake_input_keyboard_key', 'editing shortcuts through KWin fake input (0.7.2)')
require('protocols/fake-input.xml', 'org_kde_kwin_fake_input', 'vendored fake-input protocol')
require('tests/tst_keyboarduibridge.cpp', 'selectSelectAllCopyAndCutAsShortcuts', 'editing shortcut test')
require('src/core/locallexicon.cpp', 'void loadBigrams(', 'bundled word pairs for next-word suggestions (0.7.3)')
require('data/bigrams/ATTRIBUTION.md', 'CC BY-SA 4.0', 'word pair attribution')
for language in ('en', 'de', 'ru', 'uk'):
    require(f'data/bigrams/{language}.txt', 'Built by tools/build-bigrams.py', f'{language} word pairs')
require('tests/tst_glide.cpp', 'cornersCutAsRealFingersDo', 'human-like glide test (0.7.3)')
require('src/app/keyboarduibridge.cpp', 'cycleKeySoundVolume', 'Gboard volume on keypress')
require('src/core/typingengine.cpp', 'bool isWordConnector(QChar ch)', 'LatinIME word connectors (0.7.5)')
require('src/platform/kwin/fakeinputkeychords.cpp', 'org_kde_kwin_fake_input_keyboard_keysym', 'layout-proof shortcuts (0.7.6)')
require('src/platform/kwin/kwininputmethodv1backend.cpp', 'MaxCommitBytes', 'long commits in pieces (0.7.6)')
require('src/platform/kwin/datacontrolclipboard.cpp', 'x-kde-passwordManagerHint', 'password manager hint (0.7.6)')
require('tests/tst_glide.cpp', 'aShortSlideIsNotALongWord', 'short-slide glide test (0.7.5)')
require('src/platform/kwin/kwininputmethodv1backend.cpp', "clears KWin's pending commit", 'no ghost text from KWin pending commit (0.7.7)')
require('src/app/main.cpp', 'bridge.deactivateInputContext()', 'word kept across a text-input bounce (0.7.7)')
require('src/ui/Main.qml', 'function rollOver(pressedKey)', 'LatinIME press-order rollover (0.7.8)')
require('data/io.github.m0npet.Tastra.desktop', 'X-KDE-Wayland-Interfaces=org_kde_kwin_fake_input', 'fake input on KWin up to 6.7 (0.7.9)')
require('CMakeLists.txt', '"LANGUAGE=en" "LC_ALL=C.UTF-8"', 'tests independent of the session language (0.7.9)')
require('scripts/tastra-update.sh', 'rebuild_reason()', 'update rebuilds a stale install (0.7.9)')
require('src/ui/Main.qml', 'maximumTouchPoints: 5', 'two fingers on one key (0.7.8)')
require('src/core/locallexicon.cpp', 'void saveLearningInBackground(', 'learned words saved off the typing thread (0.7.10)')
require('src/core/locallexicon.cpp', 'void pruneLearned(', 'learned words bounded like LatinIME user history (0.7.10)')
require('src/core/locallexicon.cpp', 'LocalLexicon::learnedFollowers(', 'learned next words looked up, not scanned (0.7.10)')
require('src/app/main.cpp', 'Tastra::saveBeforeTermination(', 'learned words saved before KWin ends the keyboard (0.7.10)')
require('tests/tst_terminationsaver.cpp', 'wordsLearnedSinceTheLastSaveSurviveSigterm', 'SIGTERM save test (0.7.10)')
require('src/core/typingengine.cpp', 'bool TypingEngine::recomposeCurrentWord(', 'typing on a confirmed word composes it again (0.7.11)')
require('src/core/typingengine.cpp', 'void TypingEngine::resumeWordBeforeCursor(', 'Backspace resumes the word, LatinIME restartSuggestions (0.7.11)')
require('src/core/typingengine.cpp', 'void TypingEngine::prepareTextForGlidedWord(', 'LatinIME phantom space before a glide (0.7.11)')
require('src/core/typingengine.cpp', '"text-channel after refresh"', 'suggestion taps on unconfirmed words stay on the text channel (0.7.11)')
require('tests/textinputclient.h', 'class TextInputClient final', 'GTK 3 / Firefox behind KWin client model (0.7.11)')
require('tests/tst_clientsim.cpp', 'randomTypingOnRealClients', 'random typing against the client model (0.7.11)')
require('src/core/typingengine.cpp', 'bool TypingEngine::textFollowsCursor() const', 'no second space before following text (0.7.12)')
require('src/core/typingengine.cpp', 'bool TypingEngine::deleteOwnText(', 'own text deleted after GTK re-reads it (0.7.12)')
require('tests/tst_clientsim.cpp', 'aSuggestionBeforeASpaceAddsNoSecondSpace', 'mid-text suggestion test (0.7.12)')
require('docs/GBOARD-PARITY.md', 'Tastra 1.0 = everything Gboard does', 'release criterion: Gboard parity list (0.7.13)')
require('src/app/keyboarduibridge.cpp', 'void KeyboardUiBridge::deleteGestureTo(', 'Gboard gesture delete selects, release deletes (0.7.13)')
require('src/core/clipboardhistory.cpp', 'QStringList clipboardParts(', 'Gboard paste sections of text (0.7.13)')
require('src/ui/Main.qml', 'keyboardBridge.pasteClipboardPart(index)', 'clipboard part chips (0.7.13)')
require('src/core/voicecommands.cpp', 'VoiceCommand parseVoiceCommand(', 'Gboard voice commands (0.7.14)')
require('src/app/keyboarduibridge.cpp', 'void KeyboardUiBridge::handleRecognizedSpeech(', 'speech results: command or dictation (0.7.14)')
require('src/ui/Main.qml', 'visible: keyboardBridge.showEmojiKey', 'Gboard emoji switch key (0.7.14)')
require('src/ui/Main.qml', 'keyboardBridge.toggleOneHanded()', 'comma long-press menu (0.7.14)')
require('src/core/keyboardmodel.cpp', '"dvorak", "Dvorak"', 'letter layouts per language (0.7.14)')
require('src/ui/Main.qml', 'objectName: "themePhoto"', 'Gboard photo theme (0.7.15)')
require('src/app/keyboarduibridge.cpp', 'QStringList KeyboardUiBridge::themeColors() const', 'Gboard colour themes (0.7.15)')
require('src/ui/Main.qml', 'objectName: "resizeDrag"', 'Gboard resize by dragging (0.7.15)')
require('src/app/keyboarduibridge.cpp', 'void KeyboardUiBridge::moveToolbarItem(', 'Gboard toolbar choice and order (0.7.16)')
require('src/ui/Main.qml', 'objectName: "themeGradient"', 'Gboard gradient themes (0.7.16)')
require('src/core/locallexicon.cpp', 'QStringList LocalLexicon::phraseAfter(', 'phrase completion from habits (0.7.17)')
require('src/ui/Main.qml', 'objectName: phrase ? "phraseChip" : ""', 'phrase chip in the strip (0.7.17)')
require('src/core/morsecode.cpp', 'QList<MorseSign> morseTable(', 'Morse code signs (0.7.17)')
require('src/ui/Main.qml', 'objectName: "morseDotKey"', 'Morse code layout (0.7.17)')
require('src/core/clipboardhistory.cpp', 'void ClipboardHistory::captureImage(', 'clipboard images (0.7.18)')
require('src/app/keyboarduibridge.cpp', 'm_chords->send({EvdevKey::LeftCtrl}, EvdevKey::V);', 'images pasted by the application (0.7.18)')
require('src/core/voicecommands.cpp', 'std::optional<QString> applyVoiceEdit(', 'detailed voice edits (0.7.18)')
require('src/core/languages.cpp', 'const LanguageInfo *languageInfo(', 'six more languages (0.7.19)')
require('src/core/keyboardmodel.cpp', '{"es", "Español", "qwertyuiop", "asdfghjklñ", "zxcvbnm"}', 'Spanish layout with ñ (0.7.19)')
forbid('src/core/locallexicon.cpp', 'if (!it.key().startsWith(prefix)) continue;', 'scan of every learned pair per next-word lookup')
forbid('src/ui/Main.qml', 'scale: mouse.pressed', 'press scale that moved edge releases outside the key')
for sound in ('click', 'space', 'delete', 'return'):
    if not (ROOT / f'data/sounds/{sound}.wav').read_bytes().startswith(b'RIFF'):
        errors.append(f'data/sounds/{sound}.wav: missing {sound} key sound')
require('src/app/main.cpp', 'bridge.resetInputContext();', 'typing/panel reset on context close')
require('CMakeLists.txt', 'Qt6::Gui', 'Qt GUI dependency for clipboard')


# Smart typing / local privacy-first engine checks.
for path, token in (
    ('src/core/locallexicon.cpp', 'LocalLexicon::bestCorrection'),
    ('src/core/locallexicon.cpp', '/usr/share/hunspell'),
    ('src/core/typingengine.cpp', 'TypingEngine::chooseSuggestion'),
    ('src/core/typingengine.cpp', 'TypingEngine::endGlide'),
    ('src/core/clipboardhistory.cpp', 'ClipboardHistory::capture'),
    ('src/core/emojicatalog.cpp', 'emoji-test.txt'),
):
    require(path, token)

for token in (
    'Q_PROPERTY(QStringList suggestions',
    'Q_PROPERTY(bool autocorrectEnabled',
    'Q_PROPERTY(bool learningEnabled',
    'Q_PROPERTY(bool glideEnabled',
    'Q_PROPERTY(QStringList clipboardHistory',
    'Q_INVOKABLE void selectSuggestion',
    'Q_INVOKABLE QString endGlide',
):
    require('src/app/keyboarduibridge.h', token)

require('tests/tst_smarttyping.cpp', 'autocorrectReplacesCommittedWordBeforeSpace')
require('tests/tst_smarttyping.cpp', 'shortTokensAreNotAggressivelyAutocorrected')
require('src/core/locallexicon.cpp', 'typed.size() < 3', 'conservative short-token autocorrect guard')
require('tests/tst_smarttyping.cpp', 'glideDecoderCanResolveSimpleTrace')
require('tests/tst_clipboardemoji.cpp', 'clipboardHistoryDeduplicatesAndKeepsNewestFirst')

# UI checks.
qml_path = ROOT / 'src/ui/Main.qml'
qml = ui_text()
for token in (
    'id: topToolbar',
    'model: keyboardBridge.toolbarActions',
    'id: languageChooserPanel',
    'onLongPressed: keyboardBridge.openLanguagePanel()',
    'keyboardBridge.alternateForKey(modelData)',
    'keyboardBridge.tapAlternateText(alternates[0])',
    'keyboardBridge.hasClipboardText',
    'keyboardBridge.emojiSearch',
    'keyboardBridge.suggestions',
    'keyboardBridge.selectSuggestion',
    'keyboardBridge.clipboardHistory',
    'keyboardBridge.backspaceRepeated',
    'keyboardBridge.moveCursor',
    'keyboardBridge.moveLeft()',
    'keyboardBridge.deleteForward()',
    'keyboardBridge.cycleTheme()',
    'keyboardBridge.setKeyScale',
    'keyboardBridge.setKeyBorders',
    'keyboardBridge.setKeyPopups',
):
    if token not in qml:
        errors.append(f'src/ui/Main.qml: missing {token!r}')

if 'Language' in re.sub(r'Keyboard language', '', qml) and 'languageChooserPanel' not in qml:
    errors.append('src/ui/Main.qml: language UI unexpectedly missing')

# Cheap syntax sanity: braces outside strings/comments must balance.
def balance_braces(text: str):
    depth = 0
    i = 0
    in_str = None
    line_comment = False
    block_comment = False
    while i < len(text):
        c = text[i]
        n = text[i+1] if i + 1 < len(text) else ''
        if line_comment:
            if c == '\n':
                line_comment = False
            i += 1
            continue
        if block_comment:
            if c == '*' and n == '/':
                block_comment = False
                i += 2
                continue
            i += 1
            continue
        if in_str:
            if c == '\\':
                i += 2
                continue
            if c == in_str:
                in_str = None
            i += 1
            continue
        if c == '/' and n == '/':
            line_comment = True
            i += 2
            continue
        if c == '/' and n == '*':
            block_comment = True
            i += 2
            continue
        if c in ('"', "'"):
            in_str = c
        elif c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth < 0:
                return False, depth
        i += 1
    return depth == 0 and not in_str and not block_comment, depth

for _qml_file in sorted((ROOT / 'src/ui').glob('*.qml')):
    _ok, _depth = balance_braces(_qml_file.read_text(encoding='utf-8'))
    if not _ok:
        errors.append(f'{_qml_file.relative_to(ROOT)}: brace/string sanity failed (depth={_depth})')
ok, depth = balance_braces((ROOT / 'src/ui/Main.qml').read_text(encoding='utf-8'))
if not ok:
    errors.append(f'src/ui/Main.qml: brace/string sanity failed (depth={depth})')

# Test intent checks.
require('tests/tst_keyboardmodel.cpp', 'exposesLanguageSpecificLongPressAlternates')
require('tests/tst_keyboardmodel.cpp', 'QStringLiteral("clipboard")')
require('tests/tst_specialkeys.cpp', 'controllerForwardsEditingKeys')
require('tests/tst_keyboarduibridge.cpp', 'editingCommandsReachController')

# Secure/input-context integration checks.
for _file, _marker in [
    ("src/app/keyboarduibridge.h", "Q_PROPERTY(bool secureInput"),
    ("src/app/keyboarduibridge.cpp", "setSurroundingText"),
    ("src/app/keyboarduibridge.cpp", "setContentType"),
    ("src/platform/kwin/kwininputmethodv1connection.h", "surroundingTextChanged"),
    ("src/platform/kwin/kwininputmethodv1connection.h", "contentTypeChanged"),
    ("src/app/main.cpp", "preferredLanguageChanged"),
]:
    if _marker not in read(_file):
        errors.append(f"secure/input-context marker missing: {_file}: {_marker}")


# Beta 0.2.2 live-input repair checks.
require('src/core/typingengine.h', 'bool syncSurroundingText', 'state-aware surrounding-text sync')
require('src/core/typingengine.cpp', 'm_history', 'history-based stale echo reconciliation (0.2.4 replaces 0.2.3 predicted states)')
require('src/core/typingengine.cpp', 'void TypingEngine::backspaceRepeated', 'batched repeated backspace')
require('src/core/locallexicon.cpp', 'learnWordWithContext', 'single-persist contextual learning')
require('tests/tst_smarttyping.cpp', 'staleSurroundingEchoDoesNotClobberLocalWord')
require('tests/tst_smarttyping.cpp', 'terminalPunctuationImmediatelyArmsCapitalization')
require('tests/tst_smarttyping.cpp', 'repeatedBackspaceUpdatesCompositionInOneOperation')
require('tests/tst_keyboarduibridge.cpp', 'ordinaryTypingDoesNotInvalidateWholeKeyboard')

# Beta 0.2.3 root-cause fixes.
for path, token, why in (
    ('src/core/inputmethodbackend.h', 'virtual bool deleteBeforeCursor', 'same-channel deletion seam'),
    ('src/platform/kwin/kwininputmethodv1backend.cpp', 'deleteSurroundingText(-static_cast<qint32>(bytes), bytes)', 'delete_surrounding_text in UTF-8 bytes'),
    ('src/core/typingengine.cpp', 'bool TypingEngine::replaceBeforeCursor', 'replacement via text channel'),
    ('src/core/locallexicon.cpp', '#include <hunspell/hunspell.hxx>', 'affix-aware validity via libhunspell'),
    ('src/core/locallexicon.cpp', 'std::thread(', 'background dictionary loading'),
    ('src/core/locallexicon.cpp', 'if (!hasSystemDictionary()) return {};', 'no autocorrect without a validity oracle'),
    ('src/app/keyboarduibridge.cpp', 'Date = 9', 'text-input-v1 purpose enum (9 is date, not PIN)'),
    ('src/ui/Main.qml', 'root.toolbarExpanded = false\n                                keyboardBridge.selectSuggestion(modelData)', 'no access after delegate self-destruction'),
    ('tests/tst_smarttyping.cpp', 'externalCursorMoveAdoptsClientState', 'external edit regression'),
    ('tests/tst_smarttyping.cpp', 'autocorrectUsesTextChannelOnceClientConfirmedWord', 'text-channel replacement'),
    ('tests/tst_lexicon.cpp', 'inflectedFormsAreValidAndNeverRewritten', 'no rewriting of valid inflections'),
    ('tests/tst_qmlkeyboard.cpp', 'tappingSuggestionReplacesTheTypedWord', 'UI-level suggestion tap'),
    ('tests/tst_keyboarduibridge.cpp', 'dateFieldIsNotTreatedAsSecret', 'purpose mapping'),
    ('scripts/tastra-dictionaries.sh', 'sha256sum', 'pinned dictionary provisioning'),
):
    require(path, token, why)
forbid('src/app/keyboarduibridge.cpp', 'pinPurpose = 9', 'wrong PIN purpose value')

# Beta 0.2.4: composition (preedit) + history-based echo reconciliation.
for path, token, why in (
    ('src/core/inputmethodbackend.h', 'virtual bool setPreedit', 'preedit seam'),
    ('src/platform/kwin/kwininputmethodv1backend.cpp', 'm_context->preeditString(m_latestSerial, text, text)', 'preedit with commit fallback for KWin commitPendingText'),
    ('src/core/typingengine.cpp', 'm_pendingSpace', 'double-space without deletion'),
    ('src/core/typingengine.cpp', 'm_history', 'stale echoes of older states are never adopted'),
    ('src/core/typingengine.cpp', '!m_sensitiveContext && !m_preeditRejected', 'no preedit in secret fields'),
    ('src/app/tracelog.cpp', 'trace.enable', 'opt-in file trace'),
    ('tests/tst_smarttyping.cpp', 'oneStepStaleEchoesDoNotFlipCaseOrWord', 'Firefox stale-echo regression'),
    ('tests/tst_smarttyping.cpp', 'compositionCommitsCorrectedWordWithoutDeletions', 'composition autocorrect'),
    ('tests/tst_qmlkeyboard.cpp', 'is not declared', 'no implicit handler parameters'),
    ('src/core/typingengine.cpp', 'm_spaceFromSuggestion', 'Space after a suggestion is not a double-space (0.2.5)'),
    ('tests/tst_smarttyping.cpp', 'spaceAfterSuggestionIsNotADoubleSpace', '0.2.5 live regression'),
    ('src/core/typingengine.cpp', 'ignored while composing', 'echoes never abandon a live preedit (0.2.6)'),
    ('src/core/typingengine.cpp', 'constexpr qint64 SettleMs = 150;', 'settle window sized from device trace'),
    ('tests/tst_smarttyping.cpp', 'placeholderEchoDuringCompositionKeepsPendingSpace', '0.2.6 trace regression'),
    ('src/ui/Main.qml', 'MultiPointTouchArea {', 'per-key touch handling for two-thumb typing (0.2.7)'),
    ('src/ui/Main.qml', 'if (item !== glideStartItem) return', 'glide bound to the finger that started it'),
    ('tests/tst_qmlkeyboard.cpp', 'overlappingTwoThumbTapsAreBothCommitted', 'two-thumb regression'),
    ('src/core/typingengine.cpp', 'm_pendingOriginal', 'revertible autocorrection'),
    ('tests/tst_smarttyping.cpp', 'backspaceRightAfterAutocorrectRevertsAndRemembers', 'undo autocorrect'),
    ('src/platform/kwin/kwininputmethodv1connection.cpp', '~ProtocolInputMethodV1Context() override', 'contexts destroyed after deactivate'),
    ('data/frequency/ATTRIBUTION.md', 'CC BY-SA 4.0', 'frequency data attribution (0.3.0)'),
    ('data/emoji/ATTRIBUTION.md', 'Unicode License V3', 'CLDR attribution'),
    ('src/core/locallexicon.cpp', 'std::partial_sort', 'rank-first completion selection'),
    ('src/core/emojicatalog.cpp', 'void EmojiCatalog::setKeywordLanguage', 'localized emoji search'),
    ('src/app/keyboarduibridge.cpp', 'void KeyboardUiBridge::setLayoutMode', 'compact layout mode'),
    ('tests/tst_lexicon.cpp', 'germanNounsFromFrequencyKeepTheirCapital', 'frequency + speller capitals'),
    ('tests/tst_qmlkeyboard.cpp', 'compactModeDocksTheKeysLeftOrRight', 'compact layout'),
    ('src/app/voicecontroller.cpp', 'MinimumSamples', 'voice controller (0.4.0)'),
    ('src/voice/whisperrecognizer.cpp', 'm_idleTimer.setInterval(60000)', 'model released when idle'),
    ('src/voice/qtaudiorecorder.cpp', 'MaxSeconds = 30', 'bounded in-memory recording'),
    ('scripts/tastra-voice-setup.sh', '422f1ae452ade6f30a004d7e5c6a43195e4433bc370bf23fac9cc591f01a8898', 'pinned model checksum'),
    ('src/app/keyboarduibridge.cpp', 'if (m_voice) m_voice->cancel();', 'dictation never lands in a new field'),
    ('tests/tst_voice.cpp', 'emptyResultsAndCancelledRecognitionInsertNothing', 'voice state machine'),
    ('data/blocklist/ATTRIBUTION.md', 'CC BY 4.0', 'offensive-word list attribution (0.5.0)'),
    ('src/core/locallexicon.cpp', 'bool LocalLexicon::suggestible', 'offensive/forgotten words never suggested'),
    ('src/core/keyboardmodel.cpp', 'QStringList KeyboardModel::alternatesForKey', 'Gboard long-press alternates'),
    ('src/ui/Main.qml', 'id: choicePicker', 'slide-to-choose picker'),
    ('src/ui/Main.qml', 'onPressAndHold: {', 'long-press suggestion to remove'),
    ('src/core/emojicatalog.cpp', 'QString EmojiCatalog::emojiForWord', 'emoji suggestions'),
    ('tests/tst_qmlkeyboard.cpp', 'gboardLongPressPickerNumberRowHintsAndForget', 'Gboard behaviours UI test'),
    ('src/core/locallexicon.cpp', 'bool LocalLexicon::neighbours', 'neighbour-key corrections (0.6.0)'),
    ('src/core/typingengine.cpp', 'QString TypingEngine::autocorrectTarget', 'strip shows what Space inserts'),
    ('src/app/keyboarduibridge.cpp', 'void KeyboardUiBridge::loadShortcuts', 'text shortcuts'),
    ('src/core/emojicatalog.cpp', 'void EmojiCatalog::noteUsed', 'recent emojis'),
    ('src/ui/Main.qml', 'numpadShown', 'number pad in number/phone fields'),
    ('src/ui/Main.qml', 'id: glideTrail', 'gesture trail'),
    ('tests/tst_qmlkeyboard.cpp', 'numberFieldsShowANumpadEmailFieldsAnAt', 'field-type layouts'),
    ('src/core/typingengine.cpp', 'm_autoSpacePending', 'auto-space after punctuation (0.7.0)'),
    ('src/app/keyboarduibridge.cpp', 'text == QStringLiteral("\'") && m_model.symbolsActive()', 'apostrophe returns to letters'),
    ('src/ui/Main.qml', 'function keyAt(x, y)', 'slide gestures hit-test'),
    ('src/ui/Main.qml', 'objectName: "emojiRowKey_" + modelData', 'emoji fast-access row'),
    ('tests/tst_qmlkeyboard.cpp', 'gboardSlideGesturesAndEmojiRow', 'slide gestures UI test'),
    ('COPYING', 'GNU GENERAL PUBLIC LICENSE', 'license text shipped (1.0)'),
    ('src/app/keyboarduibridge.cpp', 'QString KeyboardUiBridge::effectiveTheme', 'system/light/dark/AMOLED themes'),
    ('src/ui/Main.qml', 'objectName: "aboutText"', 'about: version + attributions'),
    ('tests/tst_qmlkeyboard.cpp', 'lightDarkAndAmoledPalettes', 'theme palettes UI test'),
    ('tests/tst_typingstress.cpp', 'typedTextSurvivesRandomSequences', 'randomised no-lost-letters stress test (1.0.1)'),
    ('src/core/typingengine.cpp', 'm_pendingSpaceAfterWord', 'LatinIME space/punctuation swap'),
    ('src/core/locallexicon.cpp', 'QString LocalLexicon::correctionPreview', 'cheap per-keystroke correction preview'),
    ('src/core/locallexicon.cpp', 'int hunspellBudget = 250;', 'bounded Hunspell work at Space'),
    ('src/core/locallexicon.cpp', 'bool LocalLexicon::isUserWord', 'personal dictionary + promotion threshold (1.1.0)'),
    ('src/core/typingengine.cpp', 'm_lexicon.promoteWord(original);', 'undoing autocorrect promotes the word'),
    ('src/ui/Main.qml', 'objectName: "saveWordChip"', 'touch-again-to-save chip'),
    ('tests/tst_lexicon.cpp', 'oneAccidentalCommitDoesNotLegitimiseATypo', 'typos are not learned from one commit'),
    ('tests/tst_qmlkeyboard.cpp', 'savingAndRemovingPersonalWordsThroughTheUi', 'personal dictionary UI test'),
    ('src/ui/Main.qml', '        z: 130', 'key rows above the toolbar (top-row previews visible, 1.1.1)'),
    ('src/core/locallexicon.cpp', 'learning/%1/schema', 'legacy learning migration'),
    ('src/core/typingengine.cpp', 'suggestions follow the case of what was typed', 'case-following suggestions'),
    ('tests/tst_lexicon.cpp', 'legacyLearningDoesNotPromoteOldTypos', 'migration regression'),
    ('src/ui/Main.qml', 'id: backspaceRepeat', 'Backspace auto-repeat (1.1.2)'),
    ('src/core/typingengine.cpp', 'void TypingEngine::rearmSentenceStartFromText', 'capitalisation after clearing the field'),
    ('tests/tst_qmlkeyboard.cpp', 'holdingBackspaceKeepsDeletingUntilRelease', 'Backspace hold UI test'),
    ('src/core/typingengine.cpp', 'bool TypingEngine::doubleSpaceAllowedAfter', 'LatinIME double-space rules (1.1.3)'),
    ('tests/tst_smarttyping.cpp', 'doubleSpacePeriodFollowsLatinIME', 'LatinIME double-space test'),
    ('src/ui/Main.qml', 'objectName: "keyPreview"', 'key preview clamped inside the panel'),
    ('tests/tst_qmlkeyboard.cpp', 'topRowKeyPreviewStaysInsideThePanel', 'geometric preview test'),
    ('src/ui/Main.qml', 'objectName: "settingsScrollIndicator"', 'settings scroll indicator'),
    ('src/core/locallexicon.cpp', 'int LocalLexicon::neighbourBonus', 'touch-aware neighbour correction (1.2.0)'),
    ('src/ui/Main.qml', 'keyboardBridge.tapLetterAt(modelData', 'touch point passed from the keys'),
    ('tests/tst_qmlkeyboard.cpp', 'touchPointInsideTheKeyReachesCorrection', 'touch point end-to-end test'),
    ('src/core/typingengine.cpp', 'bool TypingEngine::atEmptyParagraph', 'empty-paragraph first letter (1.2.1)'),
    ('tests/tst_smarttyping.cpp', 'emptyParagraphCommitsTheFirstLetterBeforeComposing', 'empty-paragraph test'),
    ('src/app/panelvisibility.cpp', 'PanelVisibility::setActive', 'panel hide grace time'),
    ('tests/tst_panelvisibility.cpp', 'showsAtOnceAndRidesOutAShortDeactivation', 'panel grace-time test'),
    ('src/core/capsmode.cpp', 'bool sentenceCaps(', 'LatinIME capitalisation port (1.3.0)'),
    ('tests/tst_capsmode.cpp', 'englishFollowsLatinIME', 'LatinIME caps test vectors'),
    ('src/app/keyboardhider.cpp', 'org.kde.kwin.VirtualKeyboard', 'hide keyboard via KWin D-Bus'),
    ('src/ui/Main.qml', 'objectName: "hideKeyboardButton"', 'Gboard hide button'),
    ('src/core/typingengine.cpp', 'LatinIME revertSwapPunctuation: back to the typed order', 'swap revert (1.4.0)'),
    ('src/core/typingengine.cpp', 'void TypingEngine::rememberCorrection', 'Gboard re-correction memory'),
    ('tests/tst_smarttyping.cpp', 'returningToAnAutocorrectedWordOffersWhatWasTyped', 're-correction test'),
    ('src/app/keyboarduibridge.cpp', 'void KeyboardUiBridge::keyFeedback', 'sound on keypress'),
    ('tools/make-click-wav.py', 'Own work, no samples used', 'click sound provenance'),
    ('src/core/clipboardhistory.cpp', 'void ClipboardHistory::setPinned', 'clipboard pins + 1 h expiry (0.5.0)'),
    ('src/ui/Main.qml', 'objectName: "panelInputBlocker"', 'panels opaque to touch'),
    ('tests/tst_qmlkeyboard.cpp', 'clipboardItemsArePinnedWithALongPress', 'clipboard pin + panel-through-tap UI test'),
    ('src/ui/Main.qml', 'objectName: "settingsSection_Text correction"', 'settings grouped like Gboard (0.5.1)'),
    ('src/app/keyboarduibridge.cpp', 'void KeyboardUiBridge::setLanguageEnabled', 'choose languages for the globe'),
    ('tests/tst_qmlkeyboard.cpp', 'settingsAreGroupedLikeGboard', 'settings sections UI test'),
    ('src/ui/Main.qml', 'readonly property real splitShift', 'split layout for thumb typing (0.5.2)'),
    ('tests/tst_qmlkeyboard.cpp', 'splitLayoutForThumbTypingLikeGboardOnTablets', 'split layout UI test'),
    ('src/core/typingengine.cpp', 'bool TypingEngine::replaceWordAroundCursor', 'whole-word options mid-word (0.5.3)'),
    ('src/platform/kwin/kwininputmethodv1backend.cpp', 'bool KWinInputMethodV1Backend::deleteAroundCursor', 'delete around the cursor'),
    ('tests/tst_smarttyping.cpp', 'tappingIntoAWordOffersSuggestionsForTheWholeWord', 'mid-word test'),
    ('src/core/emojicatalog.cpp', 'QStringList EmojiCatalog::skinTones', 'skin tones behind the base emoji (0.5.4)'),
    ('src/ui/Main.qml', 'objectName: "emojiTonePicker"', 'skin-tone picker'),
    ('tests/tst_qmlkeyboard.cpp', 'longPressOnAnEmojiOffersSkinTones', 'skin-tone UI test'),
    ('src/core/typingengine.cpp', 'Typed with the wrong layout', 'wrong-layout detection (0.6.0)'),
    ('src/core/locallexicon.cpp', 'bool LocalLexicon::knownInLanguage', 'lazy foreign word lists'),
    ('src/app/keyboarduibridge.cpp', 'QString KeyboardUiBridge::clipboardSuggestion', 'paste-what-you-copied chip'),
    ('src/app/keyboarduibridge.cpp', 'void KeyboardUiBridge::cycleLongPressDelay', 'long-press delay setting'),
    ('src/platform/kwin/kwininputmethodv1backend.cpp', 'void KWinInputMethodV1Backend::moveUp', 'Up/Down keys'),
    ('tests/tst_qmlkeyboard.cpp', 'justCopiedTextIsOfferedForPasting', 'clipboard chip UI test'),
    ('src/core/keyboardmodel.cpp', 'QList<QStringList> KeyboardModel::symbolRows', 'two symbol pages + locale currency (0.6.1)'),
    ('src/ui/Main.qml', 'objectName: "symbolPageKey"', 'symbol page key'),
    ('tests/tst_qmlkeyboard.cpp', 'secondSymbolPageAndSymbolExtras', 'symbol pages UI test'),
    ('src/core/legacymigration.cpp', 'QStringList migrateLegacyPaths', 'V3 Keyboard -> Tastra file migration (0.6.2)'),
    ('src/app/main.cpp', 'Tastra::migrateLegacyPaths(Tastra::defaultMigrationRoots())', 'migration before settings load'),
    ('tests/tst_legacymigration.cpp', 'neverOverwritesWhatTastraAlreadyHas', 'migration tests'),
    ('src/core/locallexicon.cpp', 'malloc_trim(0)', 'freed load memory handed back'),
    ('src/core/locallexicon.cpp', 'class WordHashes', 'foreign word lists kept as hashes'),
    ('tools/bench_memory.cpp', 'VmRSS', 'memory benchmark'),
    ('data/io.github.m0npet.Tastra.desktop', 'X-KDE-Wayland-VirtualKeyboard=true', 'KWin virtual keyboard entry'),
    ('src/core/locallexicon.cpp', 'QStringList LocalLexicon::apostropheVariants', 'LatinIME intentional omission (apostrophe)'),
    ('src/core/locallexicon.cpp', 'QStringList LocalLexicon::accentVariants', 'LatinIME accents + German digraphs'),
    ('src/core/locallexicon.cpp', 'const bool shortDistantSubstitution', 'length-aware confidence'),
    ('tests/tst_lexicon.cpp', 'germanUmlautsDigraphsAndSharpS', 'error-model tests'),
    ('src/core/locallexicon.cpp', 'bool LocalLexicon::knownInCompanionLanguage', 'multilingual typing: other enabled language kept (0.6.3)'),
    ('tests/tst_keyboarduibridge.cpp', 'otherEnabledLanguagesOfTheSameScriptAreCompanions', 'companion languages test'),
    ('tests/tst_lexicon.cpp', 'aNearlyFreeFixInTheActiveLanguageBeatsAnotherLanguagesWord', 'free edit beats companion word'),
    ('src/core/emojicatalog.cpp', 'QStringList EmojiCatalog::emoticons', "Gboard ':-)' emoticon tab"),
    ('tests/tst_qmlkeyboard.cpp', 'emoticonTabShowsWideTilesAndInsertsTheFace', 'emoticon tab UI test'),
    ('src/core/glidegeometry.cpp', 'double shapeDistance', 'glide decoder: SHARK2 shape channel (0.6.4)'),
    ('src/core/locallexicon.cpp', 'QStringList LocalLexicon::decodeGlidePathCandidates', 'glide decoder from the finger path'),
    ('src/ui/Main.qml', 'keyboardBridge.endGlidePath(glidePath', 'QML passes the glide path and key centres'),
    ('src/core/typingengine.cpp', 'void TypingEngine::replaceGlidedWord', 'Gboard: other readings of a glide in the strip'),
    ('tests/tst_glide.cpp', 'commonEnglishWordsFromCleanAndShakyPaths', 'glide accuracy tests'),
    ('tests/tst_smarttyping.cpp', 'glideOffersTheOtherReadingsAndReplacesTheWord', 'glide alternatives test'),
    ('src/core/typingengine.cpp', 'void TypingEngine::eraseGlidedWord', 'Gboard: one Backspace erases a glided word (0.6.5)'),
    ('src/app/keyboarduibridge.cpp', 'TypingEngine::GlideCase::Capitalized', 'Shift / Caps Lock apply to glides'),
    ('src/ui/Main.qml', 'objectName: "nextWordSuggestionsButton"', 'Gboard next-word suggestions setting'),
    ('tests/tst_smarttyping.cpp', 'oneBackspaceErasesTheWholeGlidedWord', 'glide backspace test'),
    ('src/app/keyboarduibridge.cpp', 'bool KeyboardUiBridge::typeIntoField', 'Gboard emoji search typed on the keyboard (0.6.6)'),
    ('src/app/keyboarduibridge.cpp', 'void KeyboardUiBridge::confirmWordEntry', 'Gboard add word + shortcut from settings (0.6.7)'),
    ('src/ui/Main.qml', 'objectName: "wordEntryBar"', 'word entry bar'),
    ('tests/tst_keyboarduibridge.cpp', 'wordsAndShortcutsAreAddedFromSettings', 'add word / shortcut test'),
    ('src/ui/Main.qml', 'objectName: "emojiSearchBar"', 'emoji search bar in place of the strip'),
    ('tests/tst_qmlkeyboard.cpp', 'emojiSearchTypesOnTheLettersAndShowsResults', 'emoji search UI test'),
    ('tests/tst_qmlkeyboard.cpp', 'glideAcrossTheSplitGapFindsTheWord', 'split glide UI test'),
    ('tools/merge-apostrophe-words.py', 'wordfreq', 'apostrophe words back in the frequency lists'),
    ('data/frequency/ATTRIBUTION.md', 'SUBTLEX', 'wordfreq / SUBTLEX credit'),
    ('src/core/locallexicon.cpp', 'void LocalLexicon::addNeighbourKeyCandidates', 'LatinIME proximity: several slipped keys (0.6.9)'),
    ('src/core/locallexicon.cpp', 'void LocalLexicon::addSplitCandidates', 'LatinIME space omission / mistyped space'),
    ('src/core/locallexicon.cpp', 'if (hasFrequencyList && !rankedOrOwn(v)) c.score -= 70;', 'unlisted words count as rare'),
    ('tools/drop-foreign-words.py', 'zipf_frequency', 'Russian words dropped from the Ukrainian list'),
    ('tests/tst_lexicon.cpp', 'bundledUkrainianListHasNoRussianWords', 'Ukrainian list test'),
    ('tests/tst_smarttyping.cpp', 'twoWordsRunTogetherAreSplitAndLearnedAsWords', 'split correction engine test'),
    ('src/app/uitranslator.cpp', 'bool UiTranslator::loadLanguage', 'interface in de/ru/uk (0.7.0)'),
    ('src/ui/SettingsPanel.qml', 'objectName: "uiLanguageButton"', 'interface language setting'),
    ('tests/tst_qmlkeyboard.cpp', 'interfaceTextsFollowTheChosenLanguage', 'interface language UI test'),
    ('src/core/locallexicon.cpp', 'bool LocalLexicon::listedButNotInDictionary', 'listed words Hunspell lacks are kept'),
):
    require(path, token, why)

# The old name may only appear where the old installation is migrated or
# cleaned up (and in the history: CHANGELOG, ledger, old release notes).
OLD_NAME = re.compile(r'(?i)v3[ _.-]?(keyboard|kbd)')
LEGACY_OK = {
    'src/core/legacymigration.h', 'src/core/legacymigration.cpp', 'tests/tst_legacymigration.cpp',
    'scripts/tastra-uninstall.sh', 'scripts/tastra-rollback.sh', 'scripts/tastra-install.sh',
    'scripts/verify-static.py', 'docs/TEST-PLAN.md',   # checks the move from the old folders
}
for pattern in ('src/**/*', 'tests/**/*', 'tools/**/*', 'scripts/*', 'data/*.desktop', 'CMakeLists.txt',
                '.github/**/*', 'README.md', 'docs/TEST-PLAN.md', 'docs/architecture.md'):
    for file in ROOT.glob(pattern):
        rel = file.relative_to(ROOT).as_posix()
        if not file.is_file() or rel in LEGACY_OK or file.suffix in ('.wav', '.png', '.svg', '.dic', '.aff'):
            continue
        try:
            text = file.read_text(encoding='utf-8')
        except UnicodeDecodeError:
            continue
        if OLD_NAME.search(text) and rel not in ('README.md',):
            errors.append(f"{rel}: old name 'V3 Keyboard' outside the migration code")
# Interface texts: every qsTr() / translate("Tastra", ...) / tr() text needs
# a line in each data/i18n table with the same placeholders, and no table
# line may be left over (0.7.0).
def interface_texts():
    found = set()
    for file in (ROOT / 'src/ui').glob('*.qml'):
        found |= set(re.findall(r'qsTr\("((?:[^"\\]|\\.)*)"\)', file.read_text(encoding='utf-8')))
    for file in (ROOT / 'src').rglob('*.cpp'):
        text = file.read_text(encoding='utf-8')
        found |= set(re.findall(r'translate\("Tastra", "((?:[^"\\]|\\.)*)"\)', text))
        found |= set(re.findall(r'\btr\("((?:[^"\\]|\\.)*)"\)', text))
    return found
texts = interface_texts()
if not texts:
    errors.append('no qsTr() texts found')
for code in ('de', 'ru', 'uk'):
    table = ROOT / f'data/i18n/{code}.tsv'
    if not table.exists():
        errors.append(f'{table.relative_to(ROOT)} missing')
        continue
    rows = {}
    for line in table.read_text(encoding='utf-8').splitlines():
        if not line or line.startswith('#'):
            continue
        if line.count('\t') != 1:
            errors.append(f'data/i18n/{code}.tsv: malformed line {line[:40]!r}')
            continue
        source, translation = line.split('\t')
        rows[source] = translation
        for placeholder in ('%1', '%2'):
            if source.count(placeholder) != translation.count(placeholder):
                errors.append(f'data/i18n/{code}.tsv: {placeholder} differs in {source[:40]!r}')
    for missing in sorted(texts - rows.keys()):
        errors.append(f'data/i18n/{code}.tsv: no translation for {missing[:60]!r}')
    for unused in sorted(rows.keys() - texts):
        errors.append(f'data/i18n/{code}.tsv: unused line {unused[:60]!r}')

forbid('src/ui/Main.qml', 'onPressStarted: root.beginGlideCandidate', 'implicit signal parameters (deprecated in Qt 6.11)')
forbid('src/ui/Main.qml', 'id: emojiSearchInput', 'a text field in the panel (input panels never get keyboard focus)')

if errors:
    print('STATIC VERIFY: FAILED')
    for error in errors:
        print(f' - {error}')
    sys.exit(1)

print('STATIC VERIFY: OK')
wav = ROOT / 'data/sounds/click.wav' if 'ROOT' in globals() else Path(__file__).resolve().parent.parent / 'data/sounds/click.wav'
if not wav.exists() or wav.read_bytes()[:4] != b'RIFF':
    print('STATIC VERIFY: FAILED')
    print(' - data/sounds/click.wav missing or not a WAV file')
    sys.exit(1)
print('v0.7.19 markers present: six more languages, clipboard images, voice edits, phrase chip, Morse code, toolbar choice, gradients, colour and photo themes, resize, voice commands, emoji/language switch key, comma menu, letter layouts, Gboard parity list, selecting gesture delete, paste sections, no second space mid-text, own text deleted after a re-read, words the cursor touches composed again, Backspace resumes words, phantom space before glides, ordered suggestion taps, client model tests, background learning saves, bounded learning, indexed learned pairs, save on SIGTERM, install path fixes, touch rollover/two-finger/edge fixes, ghost-text fix, bounce recovery, constant height, keysym shortcuts, split commits, password-safe clipboard, word connectors, held-correction context, Common Voice pairs, key sounds + volume, bundled next-word pairs, human-like glides, undo/redo, select/select all via fake input, clipboard through data control, copy/cut, interface languages, chat words kept, several slipped keys, words run together, clean Ukrainian list, add word + shortcut in settings, emoji search on the keyboard, split glide test, glide backspace/shift, next-word setting, path-based glide typing + alternatives, emoticon tab, multilingual typing, Tastra name + V3 Keyboard migration, memory release, hashed foreign lists, LatinIME apostrophes/accents/length-aware confidence, symbol pages, wrong layout, clipboard chip, long-press delay, up/down, skin tones, mid-word options, split layout, settings sections, language choice, clipboard pins, opaque panels, swap revert, re-correction, key sound, trail toggle, LatinIME caps, hide button, empty paragraph, panel grace, touch-aware correction, LatinIME double space, preview clamp, backspace repeat, live-test fixes, personal dictionary, stress-tested, latency-bounded, themes, about, license, slide gestures, auto-space, emoji row, proximity, Gboard strip/fields/shortcuts/trail, Gboard behaviours, offline voice, composition, echo handling, two-thumb input, undoable autocorrect, frequency ranking, localized emoji, compact layout; QML braces balanced.')
