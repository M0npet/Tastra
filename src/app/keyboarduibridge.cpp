// SPDX-License-Identifier: GPL-3.0-or-later

#include "keyboarduibridge.h"
#include "core/voicecommands.h"
#include "uitranslator.h"
#include "qtsystemclipboard.h"
#include "core/keychords.h"

#include <QQmlEngine>

#include <QDateTime>

#ifdef TASTRA_HAVE_KEY_SOUND
#include <QSoundEffect>
#endif
#include "keyboardhider.h"

#include <QGuiApplication>
#include <QStyleHints>
#include <QStandardPaths>
#include <QFile>
#include "voicecontroller.h"
#include "keyboardlayoutmetrics.h"

#include "core/keyboardcontroller.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QSettings>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QtGlobal>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcBridge, "tastra.voice.commands", QtWarningMsg)

namespace Tastra
{
namespace
{

}

KeyboardUiBridge::KeyboardUiBridge(
    KeyboardController &controller,
    KeyboardModel &model,
    QObject *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_model(model)
    , m_typingEngine(controller)
    , m_toolbarRegistry(ToolbarRegistry::createDefault())
    , m_toolbarModel(m_toolbarRegistry)
{
    QSettings settings;
    m_uiLanguage = settings.value(QStringLiteral("uiLanguage"), QStringLiteral("system")).toString();
    applyUiLanguage();
    for (const QString &code : m_model.languageCodes()) {
        m_model.setLayoutVariant(code, settings.value(QStringLiteral("layoutVariant/") + code).toString());
    }
    m_model.setLanguage(settings.value(QStringLiteral("language"), QStringLiteral("en")).toString());
    m_typingEngine.setLanguage(m_model.languageCode());
    m_emojiCatalog.setKeywordLanguage(m_model.languageCode());
    m_typingEngine.setKeyboardRows({m_model.row1().join(QString()), m_model.row2().join(QString()), m_model.row3().join(QString())});
    updateForeignLayouts();
    if (m_voice) m_voice->setLanguage(m_model.languageCode());
    Q_EMIT userWordsChanged();
    m_amoled = settings.value(QStringLiteral("amoled"), false).toBool();
    {
        const QString stored = settings.value(QStringLiteral("theme")).toString();
        if (stored == QStringLiteral("system") || stored == QStringLiteral("light")
            || stored == QStringLiteral("dark") || stored == QStringLiteral("amoled")) {
            m_theme = stored;
        } else if (m_amoled) {
            m_theme = QStringLiteral("amoled");        // migrate the pre-1.0 toggle
        }
    }
    {
        const QString color = settings.value(QStringLiteral("themeColor")).toString();
        if (themeColors().contains(color)) m_themeColor = color;
        const QString image = settings.value(QStringLiteral("themeImage")).toString();
        if (QFileInfo(QUrl(image).toLocalFile()).isReadable()) m_themeImage = image;   // a deleted photo: plain theme
        m_themeImageDim = qBound(0, settings.value(QStringLiteral("themeImageDim"), 0).toInt(), 60);
        const QString gradient = settings.value(QStringLiteral("themeGradient")).toString();
        for (const QString &entry : themeGradients()) {
            if (entry.section(QLatin1Char(':'), 0, 0) == gradient && m_themeImage.isEmpty()) m_themeGradient = gradient;
        }
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
                this, &KeyboardUiBridge::uiPreferencesChanged);
    }
#endif
    loadShortcuts();
    m_emojiSuggestions = settings.value(QStringLiteral("emojiSuggestions"), true).toBool();
    m_nextWordSuggestions = settings.value(QStringLiteral("nextWordSuggestions"), true).toBool();
    m_typingEngine.setNextWordSuggestionsEnabled(m_nextWordSuggestions);
    m_phraseSuggestions = settings.value(QStringLiteral("phraseSuggestions"), true).toBool();
    m_typingEngine.setPhraseSuggestionsEnabled(m_phraseSuggestions);
    for (const QString &code : settings.value(QStringLiteral("enabledLanguages")).toStringList()) {
        if (m_model.languageCodes().contains(code)) m_enabledLanguages.append(code);
    }
    m_keySound = canPlayKeySound() && settings.value(QStringLiteral("keySound"), false).toBool();
    m_glideTrail = settings.value(QStringLiteral("glideTrail"), true).toBool();
    m_longPressDelay = qBound(150, settings.value(QStringLiteral("longPressDelay"), 300).toInt(), 1000);
    m_keySoundVolume = qBound(10, settings.value(QStringLiteral("keySoundVolume"), 35).toInt(), 100);
    m_autoSpaceAfterPunctuation = settings.value(QStringLiteral("autoSpaceAfterPunctuation"), false).toBool();
    m_typingEngine.setAutoSpaceAfterPunctuation(m_autoSpaceAfterPunctuation);
    m_emojiRow = settings.value(QStringLiteral("emojiRow"), false).toBool();
    m_symbolHints = settings.value(QStringLiteral("symbolHints"), true).toBool();
    m_numberRow = settings.value(QStringLiteral("numberRow"), false).toBool();
    {
        // The toolbar as chosen; unknown ids from other versions are dropped,
        // settings is always there.
        m_toolbarOrder = m_toolbarModel.visibleActionIds();
        if (settings.contains(QStringLiteral("toolbarItems"))) {
            QStringList stored;
            for (const QString &id : settings.value(QStringLiteral("toolbarItems")).toStringList()) {
                const auto *action = m_toolbarRegistry.actionById(id);
                if (action && id != QStringLiteral("language") && !stored.contains(id)) stored.append(id);
            }
            if (!stored.contains(QStringLiteral("settings"))) stored.append(QStringLiteral("settings"));
            m_toolbarOrder = stored;
        }
    }
    m_showEmojiKey = settings.value(QStringLiteral("showEmojiKey"), false).toBool();
    m_showLanguageKey = settings.value(QStringLiteral("showLanguageKey"), true).toBool();
    m_blockOffensive = settings.value(QStringLiteral("blockOffensive"), true).toBool();
    m_typingEngine.setBlockOffensive(m_blockOffensive);
    {
        const QString mode = settings.value(QStringLiteral("layoutMode"), QStringLiteral("full")).toString();
        if (mode == QStringLiteral("left") || mode == QStringLiteral("right")) m_layoutMode = mode;
    }
    m_keyScale = qBound(0.75, settings.value(QStringLiteral("keyScale"), 1.0).toDouble(), 1.40);
    m_keyBorders = settings.value(QStringLiteral("keyBorders"), true).toBool();
    m_keyPopups = settings.value(QStringLiteral("keyPopups"), true).toBool();
    m_glideEnabled = settings.value(QStringLiteral("glideEnabled"), true).toBool();

    m_typingEngine.setSuggestionsEnabled(settings.value(QStringLiteral("suggestionsEnabled"), true).toBool());
    m_typingEngine.setAutocorrectEnabled(settings.value(QStringLiteral("autocorrectEnabled"), true).toBool());
    m_typingEngine.setLearningEnabled(settings.value(QStringLiteral("learningEnabled"), true).toBool());
    m_typingEngine.setAutoCapitalizationEnabled(settings.value(QStringLiteral("autoCapitalizationEnabled"), true).toBool());
    m_typingEngine.setDoubleSpacePeriodEnabled(settings.value(QStringLiteral("doubleSpacePeriodEnabled"), true).toBool());
    m_typingEngine.setCompositionEnabled(settings.value(QStringLiteral("compositionEnabled"), true).toBool());

    setSystemClipboard(nullptr);                        // QClipboard until told otherwise
}

void KeyboardUiBridge::setSystemClipboard(SystemClipboard *clipboard)
{
    if (m_clipboard) disconnect(m_clipboard, nullptr, this, nullptr);
    if (!clipboard) {
        if (!m_qtClipboard) m_qtClipboard = new QtSystemClipboard(this);
        clipboard = m_qtClipboard;
    }
    m_clipboard = clipboard;
    captureClipboard();
    connect(m_clipboard, &SystemClipboard::changed, this, [this]() {
        captureClipboard();
        // A password manager's secret is pasted on request only.
        m_freshClipboard = m_clipboard->isSensitive() ? QString() : clipboardText().trimmed();
        m_freshClipboardMs = QDateTime::currentMSecsSinceEpoch();
        Q_EMIT clipboardChanged();
        Q_EMIT suggestionsChanged();
    });
    Q_EMIT clipboardChanged();
}

bool KeyboardUiBridge::uppercase() const
{
    if (m_emojiSearchActive) return false;            // the query is lowercase
    if (m_wordEntryStep == QStringLiteral("shortcut")) return false;
    if (!m_wordEntryStep.isEmpty()) return m_model.uppercase();      // Shift only, no sentence case
    return m_model.uppercase() || m_typingEngine.wantsAutoUppercase();
}
bool KeyboardUiBridge::capsLock() const { return m_model.capsLock(); }
bool KeyboardUiBridge::symbolsActive() const { return m_model.symbolsActive(); }
QString KeyboardUiBridge::languageCode() const { return m_model.languageCode(); }
QString KeyboardUiBridge::languageLabel() const { return m_model.languageLabel(); }
QStringList KeyboardUiBridge::row1() const { return m_model.row1(); }
QStringList KeyboardUiBridge::row2() const { return m_model.row2(); }
QStringList KeyboardUiBridge::row3() const { return m_model.row3(); }
QString KeyboardUiBridge::activePanel() const { return panelIdName(m_panelManager.activePanel()); }
QStringList KeyboardUiBridge::toolbarActionIds() const { return m_toolbarOrder; }

QStringList KeyboardUiBridge::toolbarChoices() const
{
    QStringList choices = m_toolbarOrder;
    for (const auto &action : m_toolbarRegistry.actions()) {
        if (action.id != QStringLiteral("language") && !choices.contains(action.id)) choices.append(action.id);
    }
    return choices;
}

void KeyboardUiBridge::setToolbarItemShown(const QString &id, bool shown)
{
    if (!m_toolbarRegistry.actionById(id) || id == QStringLiteral("language")) return;
    if (id == QStringLiteral("settings") && !shown) return;
    if (shown == m_toolbarOrder.contains(id)) return;
    if (shown) m_toolbarOrder.insert(qMax(0, m_toolbarOrder.indexOf(QStringLiteral("settings"))), id);
    else m_toolbarOrder.removeAll(id);
    persistToolbar();
}

void KeyboardUiBridge::moveToolbarItem(const QString &id, int delta)
{
    const qsizetype at = m_toolbarOrder.indexOf(id);
    if (at < 0) return;
    const qsizetype to = qBound<qsizetype>(0, at + delta, m_toolbarOrder.size() - 1);
    if (to == at) return;
    m_toolbarOrder.move(at, to);
    persistToolbar();
}

void KeyboardUiBridge::persistToolbar()
{
    persistPreference(QStringLiteral("toolbarItems"), m_toolbarOrder);
    Q_EMIT toolbarStateChanged();
}
QStringList KeyboardUiBridge::allLanguageCodes() const { return m_model.languageCodes(); }
int KeyboardUiBridge::symbolPage() const { return m_model.symbolPage(); }
QStringList KeyboardUiBridge::symbolRow1() const { return m_model.symbolRows().value(0); }
QStringList KeyboardUiBridge::symbolRow2() const { return m_model.symbolRows().value(1); }
QStringList KeyboardUiBridge::symbolRow3() const { return m_model.symbolRows().value(2); }

void KeyboardUiBridge::toggleSymbolPage()
{
    m_model.toggleSymbolPage();
    Q_EMIT keyboardStateChanged();
}
QStringList KeyboardUiBridge::allLanguageLabels() const { return m_model.languageLabels(); }

bool KeyboardUiBridge::isLanguageEnabled(const QString &code) const
{
    return m_enabledLanguages.isEmpty() ? m_model.languageCodes().contains(code) : m_enabledLanguages.contains(code);
}

QStringList KeyboardUiBridge::languageCodes() const
{
    QStringList codes;
    for (const QString &code : m_model.languageCodes()) {
        if (isLanguageEnabled(code)) codes.append(code);
    }
    return codes;
}

void KeyboardUiBridge::setLanguageEnabled(const QString &code, bool enabled)
{
    if (!m_model.languageCodes().contains(code) || isLanguageEnabled(code) == enabled) return;
    QStringList now = languageCodes();
    if (enabled) {
        now.append(code);
    } else {
        if (now.size() <= 1) return;                 // keep at least one language
        now.removeAll(code);
    }
    // Keep the layout order.
    QStringList ordered;
    for (const QString &c : m_model.languageCodes()) if (now.contains(c)) ordered.append(c);
    m_enabledLanguages = ordered;
    persistPreference(QStringLiteral("enabledLanguages"), ordered);
    if (!enabled && m_model.languageCode() == code) nextLanguage();
    updateForeignLayouts();
    Q_EMIT keyboardStateChanged();
}
QStringList KeyboardUiBridge::languageLabels() const
{
    QStringList labels;
    const QStringList codes = m_model.languageCodes();
    const QStringList all = m_model.languageLabels();
    for (int i = 0; i < codes.size() && i < all.size(); ++i) {
        if (isLanguageEnabled(codes.at(i))) labels.append(all.at(i));
    }
    return labels;
}
QString KeyboardUiBridge::currentWord() const { return m_typingEngine.currentWord(); }
QString KeyboardUiBridge::autocorrectSuggestion() const { return m_typingEngine.autocorrectTarget(); }
QString KeyboardUiBridge::inputPurpose() const { return m_inputPurpose; }

void KeyboardUiBridge::setKeyboardHider(KeyboardHider *hider) { m_hider = hider; }
bool KeyboardUiBridge::canHideKeyboard() const { return m_hider != nullptr; }

void KeyboardUiBridge::hideKeyboard()
{
    cancelInlineFields();
    if (!m_hider) return;
    m_typingEngine.commitComposition();      // like a focus change: keep the word
    Q_EMIT suggestionsChanged();
    m_hider->hideKeyboard();
}
QString KeyboardUiBridge::saveWordCandidate() const { return m_saveCandidate; }

void KeyboardUiBridge::updateForeignLayouts()
{
    const QStringList current = KeyboardModel::rowsForLanguage(m_model.languageCode(), m_model.layoutVariant(m_model.languageCode()));
    QList<QPair<QString, QHash<QChar, QChar>>> maps;
    QStringList companions;
    for (const QString &code : languageCodes()) {
        if (code == m_model.languageCode()) continue;
        const QStringList other = KeyboardModel::rowsForLanguage(code, m_model.layoutVariant(code));
        if (other.size() != 3 || current.size() != 3) continue;
        QHash<QChar, QChar> map;
        for (int r = 0; r < 3; ++r) {
            for (qsizetype i = 0; i < qMin(current.at(r).size(), other.at(r).size()); ++i) {
                if (current.at(r).at(i) != other.at(r).at(i)) map.insert(current.at(r).at(i), other.at(r).at(i));
            }
        }
        // Same-script languages (en/de, ru/uk) share most keys: only a
        // different script makes a meaningful wrong-layout reading.
        if (map.size() >= 15) maps.append({code, map});
        // The same script: typed words of that language are left alone.
        else companions.append(code);
    }
    m_typingEngine.setForeignLayouts(maps);
    m_typingEngine.setCompanionLanguages(companions);
}

QString KeyboardUiBridge::clipboardSuggestion() const
{
    if (m_secureInput || m_freshClipboard.isEmpty() || !m_typingEngine.currentWord().isEmpty()) return {};
    if (QDateTime::currentMSecsSinceEpoch() - m_freshClipboardMs > 60 * 1000) return {};
    return m_freshClipboard;
}

void KeyboardUiBridge::pasteClipboardSuggestion()
{
    cancelInlineFields();
    const QString text = clipboardSuggestion();
    m_freshClipboard.clear();
    if (!text.isEmpty()) tapText(text);
    Q_EMIT suggestionsChanged();
}

QStringList KeyboardUiBridge::clipboardParts() const
{
    const QString text = clipboardSuggestion();
    if (text.isEmpty()) return {};
    if (m_clipboardPartsSource != text) {
        m_clipboardPartsSource = text;
        m_clipboardPartsCache = Tastra::clipboardParts(text);
    }
    return m_clipboardPartsCache;
}

void KeyboardUiBridge::pasteClipboardPart(int index)
{
    cancelInlineFields();
    const QStringList parts = clipboardParts();
    if (index < 0 || index >= parts.size()) return;
    m_freshClipboard.clear();
    tapText(parts.at(index));
    Q_EMIT suggestionsChanged();
}
QStringList KeyboardUiBridge::userWords() const { return m_typingEngine.userWords(); }

bool KeyboardUiBridge::typedWordUnknown() const
{
    const QString word = m_typingEngine.currentWord();
    return !m_secureInput && word.size() >= 2 && !m_typingEngine.currentWordKnown();
}

void KeyboardUiBridge::addWordToDictionary(const QString &word)
{
    if (!m_typingEngine.addUserWord(word)) return;
    m_saveCandidate.clear();
    Q_EMIT userWordsChanged();
    Q_EMIT suggestionsChanged();
}

void KeyboardUiBridge::removeWordFromDictionary(const QString &word)
{
    m_typingEngine.removeUserWord(word);
    Q_EMIT userWordsChanged();
    Q_EMIT suggestionsChanged();
}

QStringList KeyboardUiBridge::emojiSkinTones(const QString &glyph) const { return m_emojiCatalog.skinTones(glyph); }

void KeyboardUiBridge::insertEmoji(const QString &glyph)
{
    stopEmojiSearch();               // choosing a result ends the search
    tapText(glyph);
    m_emojiCatalog.noteUsed(glyph);
    Q_EMIT emojiChanged();
}

void KeyboardUiBridge::startEmojiSearch()
{
    m_typingEngine.commitComposition();
    m_emojiSearchActive = true;
    m_emojiSearchText.clear();
    // The letters come back; the strip becomes the search bar.
    if (m_panelManager.closePanel()) Q_EMIT toolbarStateChanged();
    Q_EMIT emojiSearchChanged();
    Q_EMIT keyboardStateChanged();
}

void KeyboardUiBridge::stopEmojiSearch()
{
    if (!m_emojiSearchActive) return;
    m_emojiSearchActive = false;
    m_emojiSearchText.clear();
    Q_EMIT emojiSearchChanged();
    Q_EMIT keyboardStateChanged();
}

QStringList KeyboardUiBridge::emojiSearchResults() const
{
    if (!m_emojiSearchActive) return {};
    const QString query = m_emojiSearchText.trimmed();
    if (query.isEmpty()) {
        const QStringList recent = m_emojiCatalog.glyphs(QStringLiteral("Recent"), {}, 30);
        return recent.isEmpty() ? m_emojiCatalog.glyphs(QStringLiteral("Smileys"), {}, 30) : recent;
    }
    return m_emojiCatalog.glyphs(QStringLiteral("All"), query, 40);
}

bool KeyboardUiBridge::typeIntoField(const QString &text)
{
    if (!m_wordEntryStep.isEmpty()) {
        if (text == QStringLiteral("\b")) m_wordEntryText.chop(1);
        else if (!text.trimmed().isEmpty()) {
            // A word keeps the capitals typed with Shift ("Oldenburg"); a
            // shortcut is matched in lowercase.
            const bool capital = m_wordEntryStep == QStringLiteral("word") && m_model.uppercase();
            m_wordEntryText += capital ? text.toUpper() : text.toLower();
        }
        m_model.consumeShiftAfterLetter();
        Q_EMIT wordEntryChanged();
        Q_EMIT keyboardStateChanged();
        return true;
    }
    if (!m_emojiSearchActive) return false;
    if (text == QStringLiteral("\b")) m_emojiSearchText.chop(1);
    else if (text == QStringLiteral(" ")) {
        if (!m_emojiSearchText.isEmpty() && !m_emojiSearchText.endsWith(QLatin1Char(' '))) m_emojiSearchText += text;
    } else {
        m_emojiSearchText += text.toLower();
    }
    m_model.consumeShiftAfterLetter();
    Q_EMIT emojiSearchChanged();
    return true;
}

void KeyboardUiBridge::startWordEntry()
{
    m_typingEngine.commitComposition();
    if (m_emojiSearchActive) stopEmojiSearch();
    m_wordEntryStep = QStringLiteral("word");
    m_wordEntryText.clear();
    m_wordEntryWord.clear();
    if (m_panelManager.closePanel()) Q_EMIT toolbarStateChanged();
    Q_EMIT wordEntryChanged();
    Q_EMIT keyboardStateChanged();
}

void KeyboardUiBridge::stopWordEntry()
{
    if (m_wordEntryStep.isEmpty()) return;
    m_wordEntryStep.clear();
    m_wordEntryText.clear();
    m_wordEntryWord.clear();
    // Back to the dictionary settings, where the entry started.
    if (m_panelManager.openPanel(PanelId::Settings)) Q_EMIT toolbarStateChanged();
    Q_EMIT wordEntryChanged();
    Q_EMIT keyboardStateChanged();
}

void KeyboardUiBridge::confirmWordEntry()
{
    if (m_wordEntryStep == QStringLiteral("word")) {
        if (m_wordEntryText.isEmpty()) return;
        m_wordEntryWord = m_wordEntryText;
        m_wordEntryText.clear();
        m_wordEntryStep = QStringLiteral("shortcut");
        Q_EMIT wordEntryChanged();
        Q_EMIT keyboardStateChanged();
        return;
    }
    if (m_wordEntryStep != QStringLiteral("shortcut")) return;
    const QString word = m_wordEntryWord;
    const QString shortcut = m_wordEntryText.toLower();
    addWordToDictionary(word);
    if (!shortcut.isEmpty()) {
        m_shortcuts.insert(shortcut, word);
        writeShortcutLine(shortcut, shortcut + QStringLiteral(" = ") + word);
        Q_EMIT shortcutsChanged();
    }
    stopWordEntry();
}

void KeyboardUiBridge::cancelInlineFields()
{
    stopEmojiSearch();
    if (m_wordEntryStep.isEmpty()) return;
    m_wordEntryStep.clear();
    m_wordEntryText.clear();
    m_wordEntryWord.clear();
    Q_EMIT wordEntryChanged();
    Q_EMIT keyboardStateChanged();
}

QStringList KeyboardUiBridge::shortcutList() const
{
    QStringList list;
    for (auto it = m_shortcuts.constBegin(); it != m_shortcuts.constEnd(); ++it) {
        list.append(it.key() + QStringLiteral(" = ") + it.value());
    }
    list.sort();
    return list;
}

void KeyboardUiBridge::removeShortcut(const QString &shortcut)
{
    const QString key = shortcut.trimmed().toLower();
    if (m_shortcuts.remove(key) == 0) return;
    writeShortcutLine(key, QString());
    Q_EMIT shortcutsChanged();
    Q_EMIT suggestionsChanged();
}

QString KeyboardUiBridge::shortcutsFilePath()
{
    const QString path = qEnvironmentVariable("TASTRA_SHORTCUTS_FILE");
    if (!path.isEmpty()) return path;
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/tastra/shortcuts.txt");
}

void KeyboardUiBridge::writeShortcutLine(const QString &key, const QString &line)
{
    QFile file(shortcutsFilePath());
    QStringList lines;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
        file.close();
        if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    }
    bool replaced = false;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines.at(i).trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) continue;
        qsizetype sep = trimmed.indexOf(QLatin1Char('\t'));
        if (sep < 0) sep = trimmed.indexOf(QLatin1Char('='));
        if (sep <= 0 || trimmed.left(sep).trimmed().toLower() != key) continue;
        if (line.isEmpty() || replaced) {
            lines.removeAt(i--);
        } else {
            lines[i] = line;
            replaced = true;
        }
    }
    if (!line.isEmpty() && !replaced) lines.append(line);
    QDir().mkpath(QFileInfo(file.fileName()).absolutePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;
    file.write((lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
}

void KeyboardUiBridge::insertEmoticon(const QString &face)
{
    stopEmojiSearch();
    tapText(face);
}

// Gboard "personal dictionary" shortcuts: one "shortcut<TAB>expansion" or
// "shortcut = expansion" per line in ~/.config/tastra/shortcuts.txt.
void KeyboardUiBridge::loadShortcuts()
{
    m_shortcuts.clear();
    QFile file(shortcutsFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    const QString text = QString::fromUtf8(file.readAll());
    for (QString line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        qsizetype sep = line.indexOf(QLatin1Char('\t'));
        if (sep < 0) sep = line.indexOf(QLatin1Char('='));
        if (sep <= 0) continue;
        const QString key = line.left(sep).trimmed().toLower();
        const QString value = line.mid(sep + 1).trimmed();
        if (!key.isEmpty() && !value.isEmpty()) m_shortcuts.insert(key, value);
    }
}

QStringList KeyboardUiBridge::suggestions() const
{
    QStringList list = m_typingEngine.suggestions();
    const QString expansion = m_shortcuts.value(m_typingEngine.currentWord().toLower());
    if (!expansion.isEmpty()) {
        list.removeAll(expansion);
        list.prepend(expansion);
        while (list.size() > 3) list.removeLast();
        return list;
    }
    const QString target = m_typingEngine.autocorrectTarget();
    if (target.isEmpty() && typedWordUnknown()) {
        const QString typed = m_typingEngine.currentWord();
        list.removeAll(typed);
        list.prepend(typed);
        while (list.size() > 3) list.removeLast();
    }
    if (!target.isEmpty()) {
        // Gboard order: what was typed (kept on tap), then the correction in
        // the middle, then the best remaining alternative.
        const QString typed = m_typingEngine.currentWord();
        QStringList ordered = {typed, target};
        for (const QString &item : list) {
            if (ordered.size() >= 3) break;
            if (!ordered.contains(item)) ordered.append(item);
        }
        list = ordered;
    }
    if (m_emojiSuggestions && !m_typingEngine.currentWord().isEmpty()) {
        const QString emoji = m_emojiCatalog.emojiForWord(m_typingEngine.currentWord());
        if (!emoji.isEmpty()) {
            if (list.size() >= 3) list.removeLast();
            list.append(emoji);
        }
    }
    // The phrase takes the last chip: never the typed word or the correction
    // in the middle, which Space and a tap rely on.
    const QString phrase = m_typingEngine.phraseSuggestion();
    if (!phrase.isEmpty() && !list.contains(phrase)) {
        if (list.size() >= 3) list.removeLast();
        list.append(phrase);
    }
    return list;
}

QString KeyboardUiBridge::phraseSuggestion() const
{
    // A shortcut expansion owns the strip (see suggestions()).
    if (!m_shortcuts.value(m_typingEngine.currentWord().toLower()).isEmpty()) return {};
    return m_typingEngine.phraseSuggestion();
}

int KeyboardUiBridge::phraseHeadLength() const
{
    const QString phrase = phraseSuggestion();
    const qsizetype space = phrase.indexOf(QLatin1Char(' '));
    return space < 0 ? int(phrase.size()) : int(space);
}

bool KeyboardUiBridge::emojiSuggestionsEnabled() const { return m_emojiSuggestions; }
bool KeyboardUiBridge::nextWordSuggestions() const { return m_nextWordSuggestions; }
bool KeyboardUiBridge::phraseSuggestions() const { return m_phraseSuggestions; }

void KeyboardUiBridge::setPhraseSuggestions(bool enabled)
{
    if (m_phraseSuggestions == enabled) return;
    m_phraseSuggestions = enabled;
    m_typingEngine.setPhraseSuggestionsEnabled(enabled);
    persistPreference(QStringLiteral("phraseSuggestions"), enabled);
    Q_EMIT typingPreferencesChanged();
    Q_EMIT suggestionsChanged();
}

void KeyboardUiBridge::setNextWordSuggestions(bool enabled)
{
    if (m_nextWordSuggestions == enabled) return;
    m_nextWordSuggestions = enabled;
    m_typingEngine.setNextWordSuggestionsEnabled(enabled);
    persistPreference(QStringLiteral("nextWordSuggestions"), enabled);
    Q_EMIT typingPreferencesChanged();
    Q_EMIT suggestionsChanged();
}
bool KeyboardUiBridge::keySound() const { return m_keySound; }
bool KeyboardUiBridge::glideTrail() const { return m_glideTrail; }

bool KeyboardUiBridge::canPlayKeySound() const
{
#ifdef TASTRA_HAVE_KEY_SOUND
    return true;
#else
    return false;
#endif
}

void KeyboardUiBridge::setKeySound(bool enabled)
{
    enabled = enabled && canPlayKeySound();
    if (m_keySound == enabled) return;
    m_keySound = enabled;
    persistPreference(QStringLiteral("keySound"), enabled);
    Q_EMIT uiPreferencesChanged();
}

void KeyboardUiBridge::setGlideTrail(bool enabled)
{
    if (m_glideTrail == enabled) return;
    m_glideTrail = enabled;
    persistPreference(QStringLiteral("glideTrail"), enabled);
    Q_EMIT uiPreferencesChanged();
}

void KeyboardUiBridge::keyFeedback(const QString &kind)
{
#ifdef TASTRA_HAVE_KEY_SOUND
    if (!m_keySound) return;
    static const QStringList kinds = {QStringLiteral("space"), QStringLiteral("delete"), QStringLiteral("return")};
    const QString name = kinds.contains(kind) ? kind : QStringLiteral("click");
    QSoundEffect *&sound = m_keySounds[name];
    if (!sound) {
        sound = new QSoundEffect(this);
        sound->setSource(QUrl(QStringLiteral("qrc:/tastra/sounds/%1.wav").arg(name)));
    }
    sound->setVolume(float(m_keySoundVolume) / 100.0f);
    sound->play();
#else
    Q_UNUSED(kind);
#endif
}

int KeyboardUiBridge::keySoundVolume() const { return m_keySoundVolume; }

void KeyboardUiBridge::cycleKeySoundVolume()
{
    static const QList<int> steps = {15, 35, 60, 100};
    const qsizetype at = steps.indexOf(m_keySoundVolume);
    m_keySoundVolume = steps.at((at < 0 ? 1 : at + 1) % steps.size());
    persistPreference(QStringLiteral("keySoundVolume"), m_keySoundVolume);
    Q_EMIT uiPreferencesChanged();
    keyFeedback();                                       // hear the new volume
}
bool KeyboardUiBridge::autoSpaceAfterPunctuation() const { return m_autoSpaceAfterPunctuation; }
bool KeyboardUiBridge::emojiRow() const { return m_emojiRow; }
QStringList KeyboardUiBridge::recentEmojis() const { return m_emojiCatalog.glyphs(QStringLiteral("Recent")).mid(0, 10); }

QStringList KeyboardUiBridge::emojiRowEmojis() const
{
    QStringList row = recentEmojis();
    static const QStringList frequent = {
        QStringLiteral("😂"), QStringLiteral("❤️"), QStringLiteral("👍"), QStringLiteral("😊"), QStringLiteral("🙏"),
        QStringLiteral("😭"), QStringLiteral("😍"), QStringLiteral("🔥"), QStringLiteral("😅"), QStringLiteral("👌"),
    };
    for (const QString &emoji : frequent) {
        if (row.size() >= 10) break;
        if (!row.contains(emoji)) row.append(emoji);
    }
    return row;
}

void KeyboardUiBridge::setAutoSpaceAfterPunctuation(bool enabled)
{
    if (m_autoSpaceAfterPunctuation == enabled) return;
    m_autoSpaceAfterPunctuation = enabled;
    m_typingEngine.setAutoSpaceAfterPunctuation(enabled);
    persistPreference(QStringLiteral("autoSpaceAfterPunctuation"), enabled);
    Q_EMIT typingPreferencesChanged();
}

void KeyboardUiBridge::setEmojiRow(bool enabled)
{
    if (m_emojiRow == enabled) return;
    m_emojiRow = enabled;
    persistPreference(QStringLiteral("emojiRow"), enabled);
    Q_EMIT uiPreferencesChanged();
}
bool KeyboardUiBridge::symbolHints() const { return m_symbolHints; }
bool KeyboardUiBridge::numberRow() const { return m_numberRow; }

void KeyboardUiBridge::setSymbolHints(bool enabled)
{
    if (m_symbolHints == enabled) return;
    m_symbolHints = enabled;
    persistPreference(QStringLiteral("symbolHints"), enabled);
    Q_EMIT uiPreferencesChanged();
}

void KeyboardUiBridge::setNumberRow(bool enabled)
{
    if (m_numberRow == enabled) return;
    m_numberRow = enabled;
    persistPreference(QStringLiteral("numberRow"), enabled);
    Q_EMIT uiPreferencesChanged();
}

void KeyboardUiBridge::setShowEmojiKey(bool show)
{
    if (m_showEmojiKey == show) return;
    m_showEmojiKey = show;
    persistPreference(QStringLiteral("showEmojiKey"), show);
    Q_EMIT uiPreferencesChanged();
}

void KeyboardUiBridge::setShowLanguageKey(bool show)
{
    if (m_showEmojiKey && show) return;           // Gboard: greyed out while the emoji key is on
    if (m_showLanguageKey == show) return;
    m_showLanguageKey = show;
    persistPreference(QStringLiteral("showLanguageKey"), show);
    Q_EMIT uiPreferencesChanged();
}

void KeyboardUiBridge::toggleOneHanded()
{
    const bool oneHanded = m_layoutMode == QStringLiteral("left") || m_layoutMode == QStringLiteral("right");
    setLayoutMode(oneHanded ? QStringLiteral("full") : QStringLiteral("right"));
}

QStringList KeyboardUiBridge::alternatesForKey(const QString &key) const
{
    QStringList list = m_model.alternatesForKey(key);
    if (key == QStringLiteral(".") && (m_inputPurpose == QStringLiteral("url") || m_inputPurpose == QStringLiteral("email"))) {
        // Gboard: long-press the period in address fields for domains.
        const QStringList domains = {QStringLiteral(".com"), QStringLiteral(".de"), QStringLiteral(".ru"),
                                     QStringLiteral(".ua"), QStringLiteral(".org"), QStringLiteral(".net")};
        list = domains + list;
    }
    return list;
}

QString KeyboardUiBridge::symbolHintForKey(const QString &key) const
{
    if (!m_symbolHints) return {};
    for (const QString &choice : m_model.alternatesForKey(key)) {
        if (!choice.isEmpty() && !choice.front().isLetter()) return choice;
    }
    return {};
}

void KeyboardUiBridge::tapAlternateText(const QString &text)
{
    m_saveCandidate.clear();
    if (text.isEmpty()) return;
    if (typeIntoField(text)) return;
    const bool uppercaseBefore = uppercase();
    if (text.front().isLetter()) m_typingEngine.typeLetter(text);
    else m_typingEngine.typeText(text);
    if (text.front().isLetter()) m_model.consumeShiftAfterLetter();
    if (uppercaseBefore != uppercase()) Q_EMIT keyboardStateChanged();
    Q_EMIT suggestionsChanged();
}
bool KeyboardUiBridge::blockOffensive() const { return m_blockOffensive; }

void KeyboardUiBridge::setEmojiSuggestionsEnabled(bool enabled)
{
    if (m_emojiSuggestions == enabled) return;
    m_emojiSuggestions = enabled;
    persistPreference(QStringLiteral("emojiSuggestions"), enabled);
    Q_EMIT typingPreferencesChanged();
    Q_EMIT suggestionsChanged();
}

void KeyboardUiBridge::setBlockOffensive(bool enabled)
{
    if (m_blockOffensive == enabled) return;
    m_blockOffensive = enabled;
    m_typingEngine.setBlockOffensive(enabled);
    persistPreference(QStringLiteral("blockOffensive"), enabled);
    Q_EMIT typingPreferencesChanged();
    Q_EMIT suggestionsChanged();
}

void KeyboardUiBridge::forgetSuggestion(const QString &word)
{
    if (!word.isEmpty() && word == m_typingEngine.phraseSuggestion()) {
        // A phrase: forget the habit, not its words.
        m_typingEngine.forgetPhrase(word);
        Q_EMIT suggestionsChanged();
        return;
    }
    m_typingEngine.forgetWord(word);
    Q_EMIT userWordsChanged();
    Q_EMIT suggestionsChanged();
}

QVariantList KeyboardUiBridge::toolbarActions() const
{
    QVariantList result;
    for (const QString &id : m_toolbarOrder) {
        const ToolbarAction *found = m_toolbarRegistry.actionById(id);
        if (!found) continue;
        const ToolbarAction &action = *found;
        QVariantMap item;
        item.insert(QStringLiteral("id"), action.id);
        item.insert(QStringLiteral("label"), action.label);
        item.insert(QStringLiteral("iconName"), action.iconName);
        item.insert(QStringLiteral("enabled"), action.enabled);
        result.append(item);
    }
    return result;
}

bool KeyboardUiBridge::amoled() const { return effectiveTheme() == QStringLiteral("amoled"); }
QString KeyboardUiBridge::theme() const { return m_theme; }
QString KeyboardUiBridge::version() const { return QStringLiteral(TASTRA_VERSION); }

QString KeyboardUiBridge::effectiveTheme() const
{
    if (m_theme != QStringLiteral("system")) return m_theme;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (qobject_cast<QGuiApplication *>(QCoreApplication::instance())
        && QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Light) {
        return QStringLiteral("light");
    }
#endif
    return QStringLiteral("dark");
}

void KeyboardUiBridge::setTheme(const QString &theme)
{
    if (theme != QStringLiteral("system") && theme != QStringLiteral("light")
        && theme != QStringLiteral("dark") && theme != QStringLiteral("amoled")) return;
    if (m_theme == theme) return;
    m_theme = theme;
    persistPreference(QStringLiteral("theme"), theme);
    Q_EMIT uiPreferencesChanged();
}

QString KeyboardUiBridge::uiLanguageLabel() const
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("en"), QStringLiteral("English")}, {QStringLiteral("de"), QStringLiteral("Deutsch")},
        {QStringLiteral("ru"), QStringLiteral("Русский")}, {QStringLiteral("uk"), QStringLiteral("Українська")},
    };
    const QString shown = names.value(m_translator ? m_translator->language() : QStringLiteral("en"));
    if (m_uiLanguage != QStringLiteral("system")) return shown;
    return tr("System (%1)").arg(shown);
}

void KeyboardUiBridge::setUiLanguage(const QString &setting)
{
    const QString value = setting == QStringLiteral("en") || UiTranslator::translatedLanguages().contains(setting)
        ? setting : QStringLiteral("system");
    if (value == m_uiLanguage) return;
    m_uiLanguage = value;
    persistPreference(QStringLiteral("uiLanguage"), m_uiLanguage);
    applyUiLanguage();
}

void KeyboardUiBridge::setQmlEngine(QQmlEngine *engine) { m_qmlEngine = engine; }

void KeyboardUiBridge::cycleUiLanguage()
{
    static const QStringList order = {QStringLiteral("system"), QStringLiteral("en"), QStringLiteral("de"),
                                      QStringLiteral("ru"), QStringLiteral("uk")};
    setUiLanguage(order.at((order.indexOf(m_uiLanguage) + 1) % order.size()));
}

void KeyboardUiBridge::applyUiLanguage()
{
    if (!m_translator) m_translator = new UiTranslator(this);   // removes itself when destroyed
    // Out of QCoreApplication while its table changes (the voice thread may
    // translate meanwhile); installing it again sends LanguageChange.
    QCoreApplication::removeTranslator(m_translator);
    m_translator->loadLanguage(UiTranslator::resolve(m_uiLanguage));
    QCoreApplication::installTranslator(m_translator);
    if (m_qmlEngine) m_qmlEngine->retranslate();
    Q_EMIT uiLanguageChanged();
}

QStringList KeyboardUiBridge::themeColors() const
{
    // Blue, teal, green, yellow, orange, red, pink, purple, brown, blue grey.
    return {QStringLiteral("#1a73e8"), QStringLiteral("#00897b"), QStringLiteral("#43a047"), QStringLiteral("#f9a825"),
            QStringLiteral("#ef6c00"), QStringLiteral("#e53935"), QStringLiteral("#d81b60"), QStringLiteral("#8e24aa"),
            QStringLiteral("#6d4c41"), QStringLiteral("#607d8b")};
}

void KeyboardUiBridge::setThemeColor(const QString &color)
{
    if (!color.isEmpty() && !themeColors().contains(color)) return;
    if (m_themeColor == color) return;
    m_themeColor = color;
    persistPreference(QStringLiteral("themeColor"), color);
    Q_EMIT uiPreferencesChanged();
}

QStringList KeyboardUiBridge::themeGradients() const
{
    return {QStringLiteral("sunset:#ff7e5f:#feb47b"), QStringLiteral("ocean:#2193b0:#6dd5ed"),
            QStringLiteral("dusk:#2c3e50:#4ca1af"), QStringLiteral("berry:#8e2de2:#4a00e0"),
            QStringLiteral("forest:#134e5e:#71b280"), QStringLiteral("night:#0f2027:#2c5364")};
}

void KeyboardUiBridge::setThemeGradient(const QString &name)
{
    bool known = name.isEmpty();
    for (const QString &entry : themeGradients()) known = known || entry.section(QLatin1Char(':'), 0, 0) == name;
    if (!known || m_themeGradient == name) return;
    m_themeGradient = name;
    persistPreference(QStringLiteral("themeGradient"), name);
    if (!name.isEmpty() && !m_themeImage.isEmpty()) {
        m_themeImage.clear();                         // one background at a time
        persistPreference(QStringLiteral("themeImage"), QString());
    }
    Q_EMIT uiPreferencesChanged();
}

void KeyboardUiBridge::setThemeImage(const QString &image)
{
    QString url;
    if (!image.isEmpty()) {
        const QString path = image.startsWith(QLatin1String("file:")) ? QUrl(image).toLocalFile() : image;
        static const QStringList kinds = {QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
                                          QStringLiteral("webp")};
        const QFileInfo info(path);
        if (!info.isFile() || !info.isReadable() || !kinds.contains(info.suffix().toLower())) return;
        url = QUrl::fromLocalFile(info.absoluteFilePath()).toString();
    }
    if (m_themeImage == url) return;
    m_themeImage = url;
    persistPreference(QStringLiteral("themeImage"), url);
    if (!url.isEmpty() && !m_themeGradient.isEmpty()) {
        m_themeGradient.clear();                      // one background at a time
        persistPreference(QStringLiteral("themeGradient"), QString());
    }
    Q_EMIT uiPreferencesChanged();
}

void KeyboardUiBridge::cycleThemeImageDim()
{
    m_themeImageDim = (m_themeImageDim + 20) % 80;          // 0, 20, 40, 60 % darker
    persistPreference(QStringLiteral("themeImageDim"), m_themeImageDim);
    Q_EMIT uiPreferencesChanged();
}

QString KeyboardUiBridge::picturesFolder() const
{
    return m_picturesFolderOverride.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                                              : m_picturesFolderOverride;
}

QStringList KeyboardUiBridge::pictureFiles() const
{
    QDir folder(picturesFolder());
    const QFileInfoList found = folder.entryInfoList(
        {QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.png"), QStringLiteral("*.webp"),
         QStringLiteral("*.JPG"), QStringLiteral("*.JPEG"), QStringLiteral("*.PNG")},
        QDir::Files | QDir::Readable, QDir::Time);
    QStringList urls;
    for (const QFileInfo &info : found) {
        urls.append(QUrl::fromLocalFile(info.absoluteFilePath()).toString());
        if (urls.size() == 30) break;
    }
    return urls;
}

void KeyboardUiBridge::cycleTheme()
{
    static const QStringList order = {QStringLiteral("system"), QStringLiteral("light"),
                                      QStringLiteral("dark"), QStringLiteral("amoled")};
    setTheme(order.at((order.indexOf(m_theme) + 1) % order.size()));
}
QString KeyboardUiBridge::layoutMode() const { return m_layoutMode; }
bool KeyboardUiBridge::voiceBuilt() const { return m_voice != nullptr; }
QString KeyboardUiBridge::voiceState() const { return m_voice ? m_voice->state() : QStringLiteral("unavailable"); }
QString KeyboardUiBridge::voiceMessage() const { return m_voice ? m_voice->message() : QString(); }

void KeyboardUiBridge::setVoiceController(VoiceController *voice)
{
    m_voice = voice;
    if (!m_voice) return;
    m_voice->setLanguage(m_model.languageCode());
    connect(m_voice, &VoiceController::stateChanged, this, &KeyboardUiBridge::voiceChanged);
    connect(m_voice, &VoiceController::textRecognized, this, &KeyboardUiBridge::handleRecognizedSpeech);
    Q_EMIT voiceChanged();
}

void KeyboardUiBridge::handleRecognizedSpeech(const QString &text)
{
    cancelInlineFields();
    const bool uppercaseBefore = uppercase();
    const VoiceCommand command = m_secureInput ? VoiceCommand{} : parseVoiceCommand(text);
    qCDebug(lcBridge) << "speech" << text.size() << "chars, command" << int(command.kind);
    switch (command.kind) {
    case VoiceCommand::DeleteLastWord:
        m_typingEngine.deleteLastWord();
        break;
    case VoiceCommand::ClearSentence:
        m_typingEngine.deleteLastSentence();
        break;
    case VoiceCommand::ClearAll:
        if (canSelect()) {
            // The whole field, not only what the keyboard knows of it.
            settleForShortcut();
            m_chords->send({EvdevKey::LeftCtrl}, EvdevKey::A);
            m_chords->send({}, EvdevKey::BackSpace);
        } else {
            m_typingEngine.deleteKnownText();
        }
        break;
    case VoiceCommand::Send:
        enter();
        return;
    case VoiceCommand::NewLine:
        m_typingEngine.insertLineBreak(command.argument);
        break;
    case VoiceCommand::Undo:
        undo();
        return;
    case VoiceCommand::Emoji: {
        QString emoji = spokenEmojiAlias(command.argument);
        if (emoji.isEmpty()) emoji = m_emojiCatalog.emojiForWord(command.argument);
        if (emoji.isEmpty()) emoji = m_emojiCatalog.emojiForWord(command.argument.section(QLatin1Char(' '), -1));
        if (emoji.isEmpty()) {
            m_typingEngine.insertDictation(applySpokenPunctuation(text));   // not an emoji name after all
        } else {
            m_typingEngine.typeText(emoji);
            m_emojiCatalog.noteUsed(emoji);
            Q_EMIT emojiChanged();
        }
        break;
    }
    case VoiceCommand::None:
        m_typingEngine.insertDictation(applySpokenPunctuation(text));
        break;
    }
    if (uppercaseBefore != uppercase()) Q_EMIT keyboardStateChanged();
    Q_EMIT suggestionsChanged();
}

void KeyboardUiBridge::toggleVoice()
{
    if (m_voice) m_voice->toggle();
}
double KeyboardUiBridge::keyScale() const { return m_keyScale; }
bool KeyboardUiBridge::keyBorders() const { return m_keyBorders; }
bool KeyboardUiBridge::keyPopups() const { return m_keyPopups; }
bool KeyboardUiBridge::suggestionsEnabled() const { return m_typingEngine.suggestionsEnabled(); }
bool KeyboardUiBridge::autocorrectEnabled() const { return m_typingEngine.autocorrectEnabled(); }
bool KeyboardUiBridge::learningEnabled() const { return m_typingEngine.learningEnabled(); }
bool KeyboardUiBridge::autoCapitalizationEnabled() const { return m_typingEngine.autoCapitalizationEnabled(); }
bool KeyboardUiBridge::doubleSpacePeriodEnabled() const { return m_typingEngine.doubleSpacePeriodEnabled(); }
bool KeyboardUiBridge::glideEnabled() const { return m_glideEnabled; }
bool KeyboardUiBridge::compositionEnabled() const { return m_typingEngine.compositionEnabled(); }
bool KeyboardUiBridge::secureInput() const { return m_secureInput; }
bool KeyboardUiBridge::clipboardHistoryEnabled() const { return m_clipboardHistory.enabled(); }

QString KeyboardUiBridge::clipboardText() const
{
    return m_clipboard ? m_clipboard->text() : QString();
}

namespace
{
constexpr qsizetype PreviewLength = 300;
}

QString KeyboardUiBridge::clipboardPreview() const
{
    if (!m_clipboard || m_clipboard->text().isEmpty()) return {};
    if (m_clipboard->isSensitive()) return QString(8, QChar(0x2022));
    return m_clipboard->text().left(PreviewLength);
}

bool KeyboardUiBridge::hasClipboardText() const { return m_clipboard && !m_clipboard->text().isEmpty(); }

QString KeyboardUiBridge::clipboardSuggestionPreview() const { return clipboardSuggestion().left(PreviewLength); }

QStringList KeyboardUiBridge::clipboardHistory() const { return m_clipboardHistory.items(); }
bool KeyboardUiBridge::isClipboardPinned(const QString &text) const { return m_clipboardHistory.isPinned(text); }

void KeyboardUiBridge::toggleClipboardPin(const QString &text)
{
    m_clipboardHistory.setPinned(text, !m_clipboardHistory.isPinned(text));
    Q_EMIT clipboardChanged();
}
QStringList KeyboardUiBridge::emojiItems() const { return m_emojiCatalog.glyphs(QStringLiteral("All"), {}, 240); }
QStringList KeyboardUiBridge::emojiCategories() const { return m_emojiCatalog.categories(); }
QStringList KeyboardUiBridge::emojiSearch(const QString &query, const QString &category) const
{
    return m_emojiCatalog.glyphs(category, query, 240);
}

QVariantMap KeyboardUiBridge::layoutMetrics(bool portrait) const
{
    auto metrics = portrait ? KeyboardLayoutMetrics::portrait() : KeyboardLayoutMetrics::landscape();
    metrics.keyHeight *= m_keyScale;
    metrics.fontSize *= qBound(0.92, m_keyScale, 1.12);
    metrics.popupHeight *= m_keyScale;
    metrics.keyRadius *= qBound(0.95, m_keyScale, 1.08);
    return {
        {QStringLiteral("contentWidthRatio"), metrics.contentWidthRatio},
        {QStringLiteral("maxContentWidth"), metrics.maxContentWidth},
        {QStringLiteral("keyHeight"), metrics.keyHeight},
        {QStringLiteral("keyGap"), metrics.keyGap},
        {QStringLiteral("outerMargin"), metrics.outerMargin},
        {QStringLiteral("topPadding"), metrics.topPadding},
        {QStringLiteral("bottomPadding"), metrics.bottomPadding},
        {QStringLiteral("keyRadius"), metrics.keyRadius},
        {QStringLiteral("fontSize"), metrics.fontSize},
        {QStringLiteral("popupHeight"), metrics.popupHeight},
        {QStringLiteral("panelHeight"), metrics.panelHeight()},
    };
}

QString KeyboardUiBridge::alternateForKey(const QString &text) const
{
    QString alternate = m_model.alternateForKey(text);
    if (!alternate.isEmpty() && uppercase()) alternate = alternate.toUpper();
    return alternate;
}

void KeyboardUiBridge::typingStateDidChange()
{
    Q_EMIT keyboardStateChanged();
    Q_EMIT suggestionsChanged();
}

void KeyboardUiBridge::tapLetter(const QString &letter) { tapLetterAt(letter, 0, 0); }

void KeyboardUiBridge::tapLetterAt(const QString &letter, qreal dx, qreal dy)
{
    if (!letter.isEmpty() && !letter.front().isLetter()) {     // Dvorak's ' , . keys
        tapText(letter);
        return;
    }
    if (typeIntoField(letter)) return;
    m_saveCandidate.clear();
    m_freshClipboard.clear();                       // typing dismisses the paste offer
    const bool oneShotWasActive = m_model.uppercase() && !m_model.capsLock();
    const bool autoUpperWasActive = m_typingEngine.wantsAutoUppercase();
    const QString output = uppercase() ? letter.toUpper() : letter.toLower();
    m_typingEngine.typeLetter(output, QPointF(qBound(-0.5, dx, 0.5), qBound(-0.5, dy, 0.5)));
    m_model.consumeShiftAfterLetter();

    // Normal typing changes suggestions on every tap, but it does not need to
    // invalidate every row/key binding. Keeping keyboardStateChanged off the
    // hot path removes a large amount of QML work in Chromium text fields.
    if (oneShotWasActive || autoUpperWasActive) Q_EMIT keyboardStateChanged();
    if (m_typingEngine.suggestionsEnabled()) Q_EMIT suggestionsChanged();
}

void KeyboardUiBridge::tapAlternate(const QString &base)
{
    QString alternate = m_model.alternateForKey(base);
    if (alternate.isEmpty()) return;
    if (typeIntoField(alternate)) return;
    const bool autoUpperWasActive = m_typingEngine.wantsAutoUppercase();
    if (uppercase()) alternate = alternate.toUpper();
    const bool oneShotWasActive = m_model.uppercase() && !m_model.capsLock();
    m_typingEngine.typeLetter(alternate);
    m_model.consumeShiftAfterLetter();
    if (oneShotWasActive || autoUpperWasActive) Q_EMIT keyboardStateChanged();
    if (m_typingEngine.suggestionsEnabled()) Q_EMIT suggestionsChanged();
}

void KeyboardUiBridge::tapText(const QString &text)
{
    if (typeIntoField(text)) return;
    m_saveCandidate.clear();
    const bool uppercaseBefore = uppercase();
    m_typingEngine.typeText(text);
    // Gboard (16.7+, on by default): an apostrophe on the symbols layer
    // switches straight back to letters to finish "don't" / "it's".
    if (text == QStringLiteral("'") && m_model.symbolsActive()) {
        toggleSymbols();
        return;
    }
    if (uppercaseBefore != uppercase()) Q_EMIT keyboardStateChanged();
    Q_EMIT suggestionsChanged();
}

void KeyboardUiBridge::selectSuggestion(const QString &word)
{
    const bool uppercaseBefore = uppercase();
    if (!word.isEmpty() && !word.front().isLetter()) {
        // Emoji suggestion: keep the typed word, add the emoji after it.
        m_typingEngine.commitComposition();
        m_typingEngine.typeText(QStringLiteral(" ") + word);
    } else {
        const bool keepUnknown = word == m_typingEngine.currentWord() && typedWordUnknown();
        const QString switchTo = word == m_typingEngine.layoutSuggestionWord() ? m_typingEngine.layoutSuggestionLanguage() : QString();
        m_typingEngine.chooseSuggestion(word);
        m_saveCandidate = keepUnknown ? word : QString();
        if (!switchTo.isEmpty()) setLanguage(switchTo);      // keep typing in the meant language
    }
    if (uppercaseBefore != uppercase()) Q_EMIT keyboardStateChanged();
    Q_EMIT suggestionsChanged();
}

void KeyboardUiBridge::cancelOneShotShift()
{
    if (!m_model.uppercase() || m_model.capsLock()) return;
    m_model.consumeShiftAfterLetter();
    Q_EMIT keyboardStateChanged();
}

void KeyboardUiBridge::shift()
{
    m_model.pressShift();
    Q_EMIT keyboardStateChanged();
}

void KeyboardUiBridge::toggleSymbols()
{
    m_model.toggleSymbols();
    Q_EMIT keyboardStateChanged();
}

void KeyboardUiBridge::nextLanguage()
{
    m_typingEngine.commitComposition();
    // Only languages the user enabled take part in the rotation.
    for (int guard = 0; guard < m_model.languageCodes().size(); ++guard) {
        m_model.nextLanguage();
        if (isLanguageEnabled(m_model.languageCode())) break;
    }
    m_typingEngine.setLanguage(m_model.languageCode());
    m_emojiCatalog.setKeywordLanguage(m_model.languageCode());
    m_typingEngine.setKeyboardRows({m_model.row1().join(QString()), m_model.row2().join(QString()), m_model.row3().join(QString())});
    updateForeignLayouts();
    if (m_voice) m_voice->setLanguage(m_model.languageCode());
    Q_EMIT userWordsChanged();
    persistLanguage();
    typingStateDidChange();
}

void KeyboardUiBridge::setLanguage(const QString &code)
{
    m_typingEngine.commitComposition();
    m_model.setLanguage(code);
    m_typingEngine.setLanguage(m_model.languageCode());
    m_emojiCatalog.setKeywordLanguage(m_model.languageCode());
    m_typingEngine.setKeyboardRows({m_model.row1().join(QString()), m_model.row2().join(QString()), m_model.row3().join(QString())});
    updateForeignLayouts();
    if (m_voice) m_voice->setLanguage(m_model.languageCode());
    Q_EMIT userWordsChanged();
    persistLanguage();
    typingStateDidChange();
}

QString KeyboardUiBridge::letterLayout() const
{
    return KeyboardModel::layoutVariantLabel(m_model.languageCode(), m_model.layoutVariant(m_model.languageCode()));
}

bool KeyboardUiBridge::letterLayoutChoice() const
{
    return KeyboardModel::layoutVariants(m_model.languageCode()).size() > 1;
}

void KeyboardUiBridge::cycleLetterLayout()
{
    const QString code = m_model.languageCode();
    const QStringList ids = KeyboardModel::layoutVariants(code);
    if (ids.size() < 2) return;
    m_typingEngine.commitComposition();
    const QString next = ids.at((ids.indexOf(m_model.layoutVariant(code)) + 1) % ids.size());
    m_model.setLayoutVariant(code, next);
    persistPreference(QStringLiteral("layoutVariant/") + code, next);
    m_typingEngine.setKeyboardRows({m_model.row1().join(QString()), m_model.row2().join(QString()), m_model.row3().join(QString())});
    updateForeignLayouts();
    typingStateDidChange();
}

void KeyboardUiBridge::activateToolbarAction(const QString &id)
{
    cancelInlineFields();
    setSelectMode(false);
    if (id == QStringLiteral("one-handed")) {
        closePanel();
        toggleOneHanded();
        return;
    }
    if (m_toolbarModel.activate(id, m_panelManager)) {
        if (id == QStringLiteral("clipboard")) Q_EMIT clipboardChanged();
        Q_EMIT toolbarStateChanged();
    }
}

void KeyboardUiBridge::openLanguagePanel()
{
    cancelInlineFields();
    if (m_panelManager.openPanel(PanelId::Language)) Q_EMIT toolbarStateChanged();
}

void KeyboardUiBridge::closePanel()
{
    setSelectMode(false);
    if (m_panelManager.closePanel()) Q_EMIT toolbarStateChanged();
}

void KeyboardUiBridge::space()
{
    if (typeIntoField(QStringLiteral(" "))) return;
    m_saveCandidate.clear();
    const bool uppercaseBefore = uppercase();
    m_typingEngine.space();
    if (uppercaseBefore != uppercase()) Q_EMIT keyboardStateChanged();
    Q_EMIT suggestionsChanged();
}

void KeyboardUiBridge::backspace()
{
    if (typeIntoField(QStringLiteral("\b"))) return;
    m_saveCandidate.clear();
    const bool uppercaseBefore = uppercase();
    m_typingEngine.backspace();
    if (uppercaseBefore != uppercase()) Q_EMIT keyboardStateChanged();
    Q_EMIT suggestionsChanged();
}

void KeyboardUiBridge::backspaceRepeated(int count)
{
    if (inlineFieldActive()) {
        for (int i = 0; i < count; ++i) typeIntoField(QStringLiteral("\b"));
        return;
    }
    const bool uppercaseBefore = uppercase();
    m_typingEngine.backspaceRepeated(count);
    if (uppercaseBefore != uppercase()) Q_EMIT keyboardStateChanged();
    Q_EMIT suggestionsChanged();
}

void KeyboardUiBridge::deleteForward()
{
    if (inlineFieldActive()) return;
    m_typingEngine.commitComposition();
    m_typingEngine.resetComposition();
    m_controller.deleteForward();
    typingStateDidChange();
}

void KeyboardUiBridge::moveUp()
{
    if (selectMode()) {                              // Shift+Up: extend the selection
        settleForShortcut();
        m_chords->send({EvdevKey::LeftShift}, EvdevKey::Up);
        typingStateDidChange();
        return;
    }
    m_typingEngine.commitComposition();
    m_typingEngine.resetComposition();
    m_controller.moveUp();
    typingStateDidChange();
}

void KeyboardUiBridge::moveDown()
{
    if (selectMode()) {                              // Shift+Down: extend the selection
        settleForShortcut();
        m_chords->send({EvdevKey::LeftShift}, EvdevKey::Down);
        typingStateDidChange();
        return;
    }
    m_typingEngine.commitComposition();
    m_typingEngine.resetComposition();
    m_controller.moveDown();
    typingStateDidChange();
}

int KeyboardUiBridge::longPressDelay() const { return m_longPressDelay; }

void KeyboardUiBridge::cycleLongPressDelay()
{
    static const QList<int> steps = {200, 300, 400, 500, 700};
    const qsizetype at = steps.indexOf(m_longPressDelay);
    m_longPressDelay = steps.at((at < 0 ? 1 : at + 1) % steps.size());
    persistPreference(QStringLiteral("longPressDelay"), m_longPressDelay);
    Q_EMIT uiPreferencesChanged();
}

void KeyboardUiBridge::moveLeft()
{
    if (selectMode()) {                              // Shift+Left: extend the selection
        settleForShortcut();
        m_chords->send({EvdevKey::LeftShift}, EvdevKey::Left);
        typingStateDidChange();
        return;
    }
    m_typingEngine.commitComposition();
    m_typingEngine.resetComposition();
    m_controller.moveLeft();
    typingStateDidChange();
}

void KeyboardUiBridge::moveRight()
{
    if (selectMode()) {                              // Shift+Right: extend the selection
        settleForShortcut();
        m_chords->send({EvdevKey::LeftShift}, EvdevKey::Right);
        typingStateDidChange();
        return;
    }
    m_typingEngine.commitComposition();
    m_typingEngine.resetComposition();
    m_controller.moveRight();
    typingStateDidChange();
}

void KeyboardUiBridge::moveCursor(int delta)
{
    if (inlineFieldActive()) return;
    m_typingEngine.commitComposition();
    m_typingEngine.resetComposition();
    const int bounded = qBound(-48, delta, 48);
    if (bounded < 0) for (int i = 0; i < -bounded; ++i) m_controller.moveLeft();
    if (bounded > 0) for (int i = 0; i < bounded; ++i) m_controller.moveRight();
    typingStateDidChange();
}

void KeyboardUiBridge::moveHome()
{
    if (selectMode()) {                              // Shift+Home: extend the selection
        settleForShortcut();
        m_chords->send({EvdevKey::LeftShift}, EvdevKey::Home);
        typingStateDidChange();
        return;
    }
    m_typingEngine.commitComposition();
    m_typingEngine.resetComposition();
    m_controller.moveHome();
    typingStateDidChange();
}

void KeyboardUiBridge::moveEnd()
{
    if (selectMode()) {                              // Shift+End: extend the selection
        settleForShortcut();
        m_chords->send({EvdevKey::LeftShift}, EvdevKey::End);
        typingStateDidChange();
        return;
    }
    m_typingEngine.commitComposition();
    m_typingEngine.resetComposition();
    m_controller.moveEnd();
    typingStateDidChange();
}

void KeyboardUiBridge::enter()
{
    if (!m_wordEntryStep.isEmpty()) {
        confirmWordEntry();
        return;
    }
    if (m_emojiSearchActive) {
        stopEmojiSearch();
        return;
    }
    m_saveCandidate.clear();
    const bool uppercaseBefore = uppercase();
    m_typingEngine.enter();
    if (uppercaseBefore != uppercase()) Q_EMIT keyboardStateChanged();
    Q_EMIT suggestionsChanged();
}

void KeyboardUiBridge::beginGlide(const QString &key)
{
    if (inlineFieldActive()) return;
    if (m_glideEnabled && !m_model.symbolsActive()) m_typingEngine.beginGlide(key);
}

void KeyboardUiBridge::glideThrough(const QString &key)
{
    if (inlineFieldActive()) return;
    if (m_glideEnabled && !m_model.symbolsActive()) m_typingEngine.glideThrough(key);
}

QString KeyboardUiBridge::endGlide()
{
    if (inlineFieldActive()) return {};
    if (!m_glideEnabled || m_model.symbolsActive()) return {};
    const bool uppercaseBefore = uppercase();
    const QString word = m_typingEngine.endGlide();
    if (!word.isEmpty()) {
        if (uppercaseBefore != uppercase()) Q_EMIT keyboardStateChanged();
        Q_EMIT suggestionsChanged();
    }
    return word;
}

QString KeyboardUiBridge::endGlidePath(const QVariantList &points, const QVariantMap &keyCentres, double keyWidth)
{
    if (inlineFieldActive()) return {};
    if (!m_glideEnabled || m_model.symbolsActive()) return {};
    QVector<QPointF> path;
    path.reserve(points.size());
    for (const QVariant &point : points) path.append(point.toPointF());
    QHash<QChar, QPointF> centres;
    for (auto it = keyCentres.constBegin(); it != keyCentres.constEnd(); ++it) {
        if (it.key().size() == 1) centres.insert(it.key().front().toLower(), it.value().toPointF());
    }
    const bool uppercaseBefore = uppercase();
    // Shift before the glide capitalizes the word, Caps Lock writes it in
    // capitals (as for tapped letters); otherwise sentence case.
    const TypingEngine::GlideCase glideCase = m_model.capsLock() ? TypingEngine::GlideCase::AllCaps
        : m_model.uppercase() ? TypingEngine::GlideCase::Capitalized
                              : TypingEngine::GlideCase::Auto;
    const QString word = m_typingEngine.endGlidePath(path, centres, keyWidth, glideCase);
    if (!word.isEmpty()) {
        m_model.consumeShiftAfterLetter();
        if (uppercaseBefore != uppercase() || glideCase == TypingEngine::GlideCase::Capitalized) Q_EMIT keyboardStateChanged();
        Q_EMIT suggestionsChanged();
    }
    return word;
}

void KeyboardUiBridge::captureClipboard()
{
    if (m_clipboard && m_clipboard->isSensitive()) return;   // never kept
    m_clipboardHistory.capture(clipboardText());
}

void KeyboardUiBridge::pasteClipboard()
{
    cancelInlineFields();
    const QString text = clipboardText();
    if (!text.isEmpty()) {
        m_typingEngine.commitComposition();
        m_typingEngine.resetComposition();
        m_controller.tapText(text);
        Q_EMIT suggestionsChanged();
    }
}

void KeyboardUiBridge::pasteClipboardHistory(int index)
{
    cancelInlineFields();
    const auto items = m_clipboardHistory.items();
    if (index < 0 || index >= items.size()) return;
    m_typingEngine.commitComposition();
    m_typingEngine.resetComposition();
    m_controller.tapText(items.at(index));
    Q_EMIT suggestionsChanged();
}

void KeyboardUiBridge::removeClipboardHistory(int index)
{
    m_clipboardHistory.removeAt(index);
    Q_EMIT clipboardChanged();
}

void KeyboardUiBridge::clearClipboard()
{
    if (m_clipboard) m_clipboard->clear();
    Q_EMIT clipboardChanged();
}

void KeyboardUiBridge::setKeyChordSender(KeyChordSender *sender)
{
    m_chords = sender;
    Q_EMIT selectionChanged();
}

void KeyboardUiBridge::settleForShortcut()
{
    cancelInlineFields();
    m_typingEngine.commitComposition();
    m_typingEngine.resetComposition();
}

// LatinIME onMoveDeletePointer / onUpWithDeletePointerActive: the slide
// selects whole words before the cursor, release deletes the selection, and
// sliding back to the start deletes nothing. Word steps are the
// application's own (Ctrl+Shift+Left / Right).
void KeyboardUiBridge::deleteGestureTo(int words)
{
    if (!deleteGestureSelects()) return;
    const int target = qBound(0, words, 64);
    if (!m_deleteGestureActive) {
        if (target == 0) return;
        settleForShortcut();
        m_deleteGestureActive = true;
        m_deleteGestureWords = 0;
    }
    while (m_deleteGestureWords < target) {
        m_chords->send({EvdevKey::LeftCtrl, EvdevKey::LeftShift}, EvdevKey::Left);
        ++m_deleteGestureWords;
    }
    while (m_deleteGestureWords > target) {
        m_chords->send({EvdevKey::LeftCtrl, EvdevKey::LeftShift}, EvdevKey::Right);
        --m_deleteGestureWords;
    }
}

void KeyboardUiBridge::endDeleteGesture()
{
    if (!m_deleteGestureActive) return;
    m_deleteGestureActive = false;
    if (m_deleteGestureWords > 0 && m_chords) m_chords->send({}, EvdevKey::BackSpace);
    m_deleteGestureWords = 0;
    typingStateDidChange();
}

void KeyboardUiBridge::cancelDeleteGesture()
{
    if (!m_deleteGestureActive) return;
    deleteGestureTo(0);
    m_deleteGestureActive = false;
    typingStateDidChange();
}

void KeyboardUiBridge::setSelectMode(bool on)
{
    if (m_selectMode == on) return;
    m_selectMode = on;
    Q_EMIT selectionChanged();
}

void KeyboardUiBridge::toggleSelectMode()
{
    if (!canSelect()) return;
    setSelectMode(!m_selectMode);
}

void KeyboardUiBridge::selectAll()
{
    if (!canSelect()) return;
    settleForShortcut();
    m_chords->send({EvdevKey::LeftCtrl}, EvdevKey::A);
    typingStateDidChange();
}

void KeyboardUiBridge::undo()
{
    if (!canSelect() || m_secureInput) return;
    settleForShortcut();
    m_chords->send({EvdevKey::LeftCtrl}, EvdevKey::Z);
    typingStateDidChange();
}

void KeyboardUiBridge::redo()
{
    if (!canSelect() || m_secureInput) return;
    settleForShortcut();
    m_chords->send({EvdevKey::LeftCtrl, EvdevKey::LeftShift}, EvdevKey::Z);
    typingStateDidChange();
}

void KeyboardUiBridge::copySelection()
{
    // Gboard's Copy. With the editing shortcuts the application copies its
    // own selection (Ctrl+C; the clipboard then reports it); otherwise the
    // selection KWin passes with the surrounding text is put on the
    // clipboard directly.
    if (m_secureInput) return;
    if (m_chords) {
        settleForShortcut();
        // Ctrl+C would interrupt the program running in a terminal.
        if (m_terminal) m_chords->send({EvdevKey::LeftCtrl, EvdevKey::LeftShift}, EvdevKey::C);
        else m_chords->send({EvdevKey::LeftCtrl}, EvdevKey::C);
        return;
    }
    if (m_selectedText.isEmpty() || !m_clipboard) return;
    m_clipboard->setText(m_selectedText);
}

void KeyboardUiBridge::cutSelection()
{
    if (!canCut()) return;
    if (m_chords) {
        settleForShortcut();
        m_chords->send({EvdevKey::LeftCtrl}, EvdevKey::X);
        setSelectMode(false);
        typingStateDidChange();
        return;
    }
    if (m_selectedText.isEmpty() || !m_clipboard) return;
    m_clipboard->setText(m_selectedText);
    m_controller.backspace();                       // deletes the selection
    m_selectedText.clear();
    Q_EMIT selectionChanged();
}

void KeyboardUiBridge::clearClipboardHistory()
{
    m_clipboardHistory.clear();
    Q_EMIT clipboardChanged();
}

void KeyboardUiBridge::setAmoled(bool enabled)
{
    // Kept for compatibility; themes are chosen with setTheme().
    setTheme(enabled ? QStringLiteral("amoled") : QStringLiteral("dark"));
}

void KeyboardUiBridge::setLayoutMode(const QString &mode)
{
    if (mode != QStringLiteral("full") && mode != QStringLiteral("split") && mode != QStringLiteral("left")
        && mode != QStringLiteral("right")) return;
    if (m_layoutMode == mode) return;
    m_layoutMode = mode;
    persistPreference(QStringLiteral("layoutMode"), mode);
    Q_EMIT uiPreferencesChanged();
}

void KeyboardUiBridge::cycleLayoutMode()
{
    // Full width -> Split (Gboard on tablets: thumb typing) -> Compact left -> right.
    static const QStringList order = {QStringLiteral("full"), QStringLiteral("split"),
                                      QStringLiteral("left"), QStringLiteral("right")};
    setLayoutMode(order.at((order.indexOf(m_layoutMode) + 1) % order.size()));
}

void KeyboardUiBridge::setKeyScale(double scale)
{
    const double bounded = qBound(0.75, scale, 1.40);
    if (qAbs(m_keyScale - bounded) < 0.001) return;
    m_keyScale = bounded;
    persistPreference(QStringLiteral("keyScale"), bounded);
    Q_EMIT uiPreferencesChanged();
}

void KeyboardUiBridge::setKeyBorders(bool enabled)
{
    if (m_keyBorders == enabled) return;
    m_keyBorders = enabled;
    persistPreference(QStringLiteral("keyBorders"), enabled);
    Q_EMIT uiPreferencesChanged();
}

void KeyboardUiBridge::setKeyPopups(bool enabled)
{
    if (m_keyPopups == enabled) return;
    m_keyPopups = enabled;
    persistPreference(QStringLiteral("keyPopups"), enabled);
    Q_EMIT uiPreferencesChanged();
}

void KeyboardUiBridge::setSuggestionsEnabled(bool enabled)
{
    if (m_typingEngine.suggestionsEnabled() == enabled) return;
    m_typingEngine.setSuggestionsEnabled(enabled);
    persistPreference(QStringLiteral("suggestionsEnabled"), enabled);
    Q_EMIT typingPreferencesChanged();
    Q_EMIT suggestionsChanged();
}

void KeyboardUiBridge::setAutocorrectEnabled(bool enabled)
{
    if (m_typingEngine.autocorrectEnabled() == enabled) return;
    m_typingEngine.setAutocorrectEnabled(enabled);
    persistPreference(QStringLiteral("autocorrectEnabled"), enabled);
    Q_EMIT typingPreferencesChanged();
}

void KeyboardUiBridge::setLearningEnabled(bool enabled)
{
    if (m_typingEngine.learningEnabled() == enabled) return;
    m_typingEngine.setLearningEnabled(enabled);
    persistPreference(QStringLiteral("learningEnabled"), enabled);
    Q_EMIT typingPreferencesChanged();
}

void KeyboardUiBridge::setAutoCapitalizationEnabled(bool enabled)
{
    if (m_typingEngine.autoCapitalizationEnabled() == enabled) return;
    m_typingEngine.setAutoCapitalizationEnabled(enabled);
    persistPreference(QStringLiteral("autoCapitalizationEnabled"), enabled);
    Q_EMIT typingPreferencesChanged();
    Q_EMIT keyboardStateChanged();
}

void KeyboardUiBridge::setDoubleSpacePeriodEnabled(bool enabled)
{
    if (m_typingEngine.doubleSpacePeriodEnabled() == enabled) return;
    m_typingEngine.setDoubleSpacePeriodEnabled(enabled);
    persistPreference(QStringLiteral("doubleSpacePeriodEnabled"), enabled);
    Q_EMIT typingPreferencesChanged();
}

void KeyboardUiBridge::setGlideEnabled(bool enabled)
{
    if (m_glideEnabled == enabled) return;
    m_glideEnabled = enabled;
    persistPreference(QStringLiteral("glideEnabled"), enabled);
    Q_EMIT typingPreferencesChanged();
}

void KeyboardUiBridge::setCompositionEnabled(bool enabled)
{
    if (m_typingEngine.compositionEnabled() == enabled) return;
    m_typingEngine.setCompositionEnabled(enabled);
    persistPreference(QStringLiteral("compositionEnabled"), enabled);
    Q_EMIT typingPreferencesChanged();
    Q_EMIT suggestionsChanged();
}

void KeyboardUiBridge::setClipboardHistoryEnabled(bool enabled)
{
    if (m_clipboardHistory.enabled() == enabled) return;
    m_clipboardHistory.setEnabled(enabled);
    Q_EMIT clipboardChanged();
}

void KeyboardUiBridge::clearLearnedWords()
{
    m_typingEngine.clearLearning();
    Q_EMIT suggestionsChanged();
}

void KeyboardUiBridge::saveLearning()
{
    m_typingEngine.saveLearning();
}

void KeyboardUiBridge::deactivateInputContext()
{
    m_typingEngine.rememberCompositionBeforeDeactivation();
    // A delete slide ends with the field: no keys go to whatever comes next.
    m_deleteGestureActive = false;
    m_deleteGestureWords = 0;
    Q_EMIT pressesCancelled();          // no key repeats or glides into nothing
    resetInputContext();
}

void KeyboardUiBridge::resetInputContext()
{
    m_deleteGestureActive = false;
    m_deleteGestureWords = 0;
    cancelInlineFields();
    if (m_voice) m_voice->cancel();                 // never type into a new field
    loadShortcuts();                                 // pick up edits without a restart
    m_typingEngine.reloadUserDictionaryFile();
    m_saveCandidate.clear();
    if (m_inputPurpose != QStringLiteral("text")) {
        m_inputPurpose = QStringLiteral("text");
        Q_EMIT inputContextChanged();
    }
    m_typingEngine.setSensitiveContext(false);
    m_typingEngine.resetInputContext();
    if (m_secureInput) {
        m_secureInput = false;
        Q_EMIT inputContextChanged();
    }
    if (!m_selectedText.isEmpty() || m_surroundingKnown || m_terminal) {
        m_selectedText.clear();
        m_surroundingKnown = false;
        m_terminal = false;
        Q_EMIT selectionChanged();
    }
    setSelectMode(false);
    m_panelManager.returnToTyping();
    Q_EMIT keyboardStateChanged();
    Q_EMIT suggestionsChanged();
    Q_EMIT toolbarStateChanged();
}

void KeyboardUiBridge::setSurroundingText(const QString &text, int cursorByte, int anchorByte)
{
    // The selection (cursor != anchor), for Copy and Cut.
    const QByteArray utf8 = text.toUtf8();
    const int from = qBound(0, qMin(cursorByte, anchorByte), int(utf8.size()));
    const int to = qBound(0, qMax(cursorByte, anchorByte), int(utf8.size()));
    const QString selected = cursorByte >= 0 && anchorByte >= 0 ? QString::fromUtf8(utf8.mid(from, to - from)) : QString();
    // The application reports its selection: Copy and Cut follow it.
    const bool known = cursorByte >= 0 && anchorByte >= 0;
    if (selected != m_selectedText || known != m_surroundingKnown) {
        m_selectedText = selected;
        m_surroundingKnown = known;
        Q_EMIT selectionChanged();
    }
    const bool uppercaseBefore = uppercase();
    if (!m_typingEngine.syncSurroundingText(text, cursorByte, anchorByte)) return;
    if (uppercaseBefore != uppercase()) Q_EMIT keyboardStateChanged();
    Q_EMIT suggestionsChanged();
}

void KeyboardUiBridge::setContentType(quint32 hint, quint32 purpose)
{
    // KWin forwards text-input-v1 enums here and maps PIN to password (8).
    constexpr quint32 lowercaseHint = 0x8;
    constexpr quint32 hiddenTextHint = 0x40;
    constexpr quint32 sensitiveDataHint = 0x80;
    enum : quint32 { Digits = 2, Number = 3, Phone = 4, Url = 5, Email = 6, Password = 8,
                     Date = 9, Time = 10, DateTime = 11, Terminal = 12 };

    const bool secure = (hint & (hiddenTextHint | sensitiveDataHint)) != 0 || purpose == Password;
    // Addresses, numbers and terminals must not be autocorrected, capitalized
    // or learned from, but they are not secrets.
    const bool restricted = purpose == Digits || purpose == Number || purpose == Phone
        || purpose == Url || purpose == Email || purpose == Date || purpose == Time
        || purpose == DateTime || purpose == Terminal;

    const QString purposeName = (purpose == Digits || purpose == Number) ? QStringLiteral("number")
        : purpose == Phone ? QStringLiteral("phone")
        : purpose == Url ? QStringLiteral("url")
        : purpose == Email ? QStringLiteral("email")
        : QStringLiteral("text");
    const bool purposeChanged = purposeName != m_inputPurpose;
    m_inputPurpose = purposeName;
    if (m_terminal != (purpose == Terminal)) {
        m_terminal = purpose == Terminal;
        Q_EMIT selectionChanged();
    }

    const bool uppercaseBefore = uppercase();
    m_typingEngine.setSensitiveContext(secure || restricted);
    m_typingEngine.setAutoCapitalizationAllowed((hint & lowercaseHint) == 0 && !restricted);
    if (m_secureInput != secure || purposeChanged) {
        m_secureInput = secure;
        Q_EMIT inputContextChanged();
        Q_EMIT selectionChanged();
    }
    if (uppercaseBefore != uppercase()) Q_EMIT keyboardStateChanged();
    Q_EMIT suggestionsChanged();
}

void KeyboardUiBridge::resetCompositionFromClient()
{
    cancelInlineFields();
    m_typingEngine.resetComposition();
    typingStateDidChange();
}

void KeyboardUiBridge::setPreferredLanguage(const QString &language)
{
    const QString lowered = language.toLower();
    QString code;
    if (lowered.startsWith(QStringLiteral("de"))) code = QStringLiteral("de");
    else if (lowered.startsWith(QStringLiteral("uk"))) code = QStringLiteral("uk");
    else if (lowered.startsWith(QStringLiteral("ru"))) code = QStringLiteral("ru");
    else if (lowered.startsWith(QStringLiteral("en"))) code = QStringLiteral("en");
    if (!code.isEmpty() && code != m_model.languageCode()) setLanguage(code);
}

void KeyboardUiBridge::persistLanguage() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("language"), m_model.languageCode());
}

void KeyboardUiBridge::persistPreference(const QString &key, const QVariant &value) const
{
    QSettings settings;
    settings.setValue(key, value);
}

}
