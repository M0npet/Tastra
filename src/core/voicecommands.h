// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include <optional>

namespace Tastra
{

// Gboard voice typing commands (support.google.com/gboard/answer/11197787):
// said on their own, after a pause, they act instead of being typed.
// English, German, Russian and Ukrainian phrases are understood whatever the
// keyboard language is.
struct VoiceCommand {
    enum Kind {
        None,            // ordinary dictation
        DeleteLastWord,  // "delete last word"
        ClearSentence,   // "clear": the last sentence
        ClearAll,        // "clear all"
        Send,            // "send": the Return key
        NewLine,         // "new line" / "new paragraph": `argument` is the break
        Undo,            // "undo"
        Emoji,           // "smiley emoji": `argument` is what to look up
        NextField,       // "next": the next field (Tab)
        // Gboard's detailed edits (English there; all four languages here),
        // on the text before the cursor; `argument` is the words to find.
        ReplaceText,     // "change X to Y": `second` is Y
        DeleteText,      // "delete X"
        InsertBefore,    // "insert X before Y": `argument` X, `second` Y
        InsertAfter,     // "insert X after Y"
        InsertAtEnd,     // "insert X at the end"
        Capitalize,      // "capitalize X"
        Spell,           // "spell X as K A T Y": `second` the letters
    };
    Kind kind = None;
    QString argument;
    QString second;
};

VoiceCommand parseVoiceCommand(const QString &utterance);

// A detailed edit applied to the text before the cursor: the whole new text,
// or nothing when the words it refers to are not there (then the utterance
// was dictation after all). The last occurrence counts, as whole words,
// whatever their case; a replacement takes the case of what it replaces.
std::optional<QString> applyVoiceEdit(const QString &textBeforeCursor, const VoiceCommand &command);

// The emoji people usually mean by a short spoken name ("heart" is ❤️, not
// the first emoji with the keyword "heart"); empty for other names, which
// are looked up in the emoji keywords.
QString spokenEmojiAlias(const QString &name);

// Spoken punctuation: "question mark", "exclamation mark" (and their German,
// Russian, Ukrainian names) anywhere; "comma" ("Komma", "запятая", "кома")
// and "full stop" only at the very end, where they cannot be meant as words
// ("drei Komma fünf"). "Period", "Punkt", "точка", "крапка" stay words
// ("точка зрения"); Whisper writes the full stop itself.
QString applySpokenPunctuation(const QString &dictation);

}
