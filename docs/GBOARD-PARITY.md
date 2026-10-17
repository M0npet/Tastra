# Gboard parity — the release criterion

**Tastra 1.0 = everything Gboard does, as far as it can be done offline on
KDE Plasma / KWin**, checked on the tablet (`docs/TEST-PLAN.md`). This list
is the measure; it is updated with every release.

Sources (read 2026-10-08): the Gboard Help Center articles
[Use your keyboard](https://support.google.com/gboard/answer/2842292),
[Word suggestions](https://support.google.com/gboard/answer/7068415),
[Copy & paste sections of text](https://support.google.com/gboard/answer/10742542),
[Theme, sound, vibration](https://support.google.com/gboard/answer/6102154),
[Slide to type](https://support.google.com/gboard/answer/2811346),
[Advanced voice typing](https://support.google.com/gboard/answer/11197787) (re-read 2026-10-17),
[Writing tools](https://support.google.com/gboard/answer/16515540),
[Handwriting](https://support.google.com/gboard/answer/9108773),
[Morse code](https://support.google.com/gboard/answer/9011881),
[Languages](https://support.google.com/gboard/answer/7068494) (Morse code
re-read 2026-10-17); Android Police
(Oct 2025) on Preferences; Androidsis (Sep 2025) on gestures; AlternativeTo
(Aug 2023) on Resize; AOSP LatinIME for behaviour details.

Legend: ✅ done · 🟡 partly · ❌ missing (planned) · 🚫 impossible on KWin ·
☁️ needs Google's servers or a cloud model (the project is offline; an
offline alternative is named where one exists).

## Typing and correction

| Gboard | Tastra |
|---|---|
| Suggestion strip, completions, typed word / correction / alternative | ✅ |
| Next-word suggestions | ✅ (bundled pairs + learning; quality below Gboard's neural model) |
| Emoji suggestions | ✅ |
| Remove a suggestion (touch and hold) | ✅ |
| Autocorrect, undo with Backspace, re-correction | ✅ |
| Auto-capitalization, double-space period, auto-space after punctuation | ✅ |
| Block offensive words | ✅ |
| Personal dictionary with shortcuts | ✅ |
| Learning, "Delete learned words" | ✅ |
| Multilingual typing (several languages at once) | ✅ (en, de, ru, uk) |
| Smart Compose (inline sentence completion, US English) | ✅ 0.7.17 as a chip in the strip, from the user's own habits (all four languages); not inline in the app: an input method on KWin cannot draw into the field (a preedit would be typed text) |
| Spell check underline in apps, "Add to dictionary" from it | 🚫 an input method on KWin cannot mark text in apps |
| Grammar check | ☁️ |
| Proofread "Fix it" (network) | ☁️ |
| Writing tools: rephrase, tone, shorten… | ☁️ (needs a language model far beyond a keyboard's size) |
| Incognito (no learning in private windows, automatic) | 🚫 GTK 3 and text-input-v3 do not tell the keyboard about private windows; learning can be switched off in settings |

## Gestures and keys

| Gboard | Tastra |
|---|---|
| Glide typing, gesture trail, alternatives, one Backspace erases | ✅ |
| Space-bar cursor control | ✅ |
| Gesture delete: slide left from Backspace **selects** words, slide back to take some back, release deletes | ✅ 0.7.13 (with KWin fake input; without it, word by word while sliding) |
| `?123` slide for one symbol, Shift slide for one capital | ✅ |
| Double-tap Shift = Caps Lock | ✅ |
| Touch and hold for accents / symbols, adjustable duration | ✅ |
| Key popup on press | ✅ |
| Long-press space or globe: language list | ✅ |
| Long-press comma: emoji / settings / one-handed | ✅ 0.7.14 |
| Two-thumb rollover typing | ✅ |

## Layouts and size

| Gboard | Tastra |
|---|---|
| Number row | ✅ |
| Split keyboard (tablets) | ✅ |
| One-handed mode left / right | ✅ |
| Floating keyboard | 🚫 KWin places the panel |
| Resize: drag the height, choose the width | ✅ 0.7.15: toolbar ⇕, drag the bar, Reset, ✓ (width: the one-handed and split layouts) |
| Field types: number pad, phone, e-mail `@` `.com`, URL | ✅ |
| Layout choice per language (QWERTY, QWERTZ, AZERTY, Dvorak, Colemak, PC) | ✅ 0.7.14 for English (QWERTY, QWERTZ, AZERTY, Dvorak, Colemak) and German (QWERTZ, QWERTY); PC layout ❌; Russian and Ukrainian have ЙЦУКЕН (and Morse) only |
| Show emoji switch key / language switch key | ✅ 0.7.14 (one or the other next to the comma, as on Gboard) |
| Morse code layout | ✅ 0.7.17 (English, German, Russian, Ukrainian; character and word timeout; the strip shows what the code can still become); key repeat on hold ❌ |
| Handwriting layout | ❌ no light offline recogniser chosen yet |
| More languages | 🟡 4 of Gboard's hundreds; more need dictionaries + frequency lists |

## Panels

| Gboard | Tastra |
|---|---|
| Emoji: categories, recent, search, skin tones, emoticons, fast-access row | ✅ |
| GIFs, stickers, Bitmoji, Emoji Kitchen | ☁️ |
| Clipboard history, pin, expiry, chip with the last copy | ✅ |
| Smart paste of parts (e-mail, URL, phone, number, date from a copied text) | ✅ 0.7.13 (also times; postal addresses ❌, Gboard has them in some countries only) |
| Images and screenshots in the clipboard | ✅ 0.7.18: chip and panel with the pictures, pins; pasted by putting the image on the clipboard and sending Ctrl+V (KWin fake input), so only where the application accepts pasted images |
| Text editing panel (arrows, select, select all, copy, cut, paste, undo, redo) | ✅ |
| Toolbar: choose and arrange its buttons | ✅ 0.7.16 (Settings → Toolbar; also a one-handed button) |
| Translate | ☁️ (an offline alternative exists: Firefox Translations / Bergamot models; large) |
| Google search, Lens | ☁️ |
| Hide keyboard | ✅ |

## Voice

| Gboard | Tastra |
|---|---|
| Voice typing | ✅ optional, offline (whisper.cpp) |
| Voice commands: "delete last word", "clear", "clear all", "send", "new line", "undo", emoji by voice, spoken punctuation | ✅ 0.7.14 in en/de/ru/uk (offline, on the recognised text); "stop" is not needed: recording ends with the mic key |
| Detailed voice edits: "change X to Y", "delete X", "insert X before Y / at the end", "spell X as …", "capitalize X"; "next" (next field) | ✅ 0.7.18 in en/de/ru/uk (Gboard: English US, Pixel 8+), on the text before the cursor |
| Auto punctuation | ✅ (whisper writes it) |
| Rambler, sign-to-text, "Fix it" by voice | ☁️ |

## Look and feedback

| Gboard | Tastra |
|---|---|
| System / light / dark themes, key borders | ✅ (+ AMOLED) |
| Colour themes, own background photo | ✅ 0.7.15: 10 colours over light / dark / AMOLED, a photo from the Pictures folder with brightness; six gradients (0.7.16); Gboard's landscape photos ❌ (licences) |
| Sound on keypress + volume | ✅ |
| Haptic feedback + strength | ❌ needs a vibration motor driven through Linux force feedback; not checked whether the tablet has one |
| Settings like Gboard's (sections) | ✅ |

## Backlog, in order

Done: gesture delete with selection, smart paste of parts (0.7.13); voice
commands, emoji / language switch key, comma menu, letter layouts (0.7.14).

Colour and photo themes, resize (0.7.15). Toolbar choice and order,
gradients (0.7.16). Phrase chip, Morse code (0.7.17). Clipboard images,
detailed voice edits (0.7.18).

1. Handwriting (find an offline recogniser first).
2. More languages; more layouts (PC, Russian phonetic).
3. Offline translation (Bergamot), if its size is acceptable.
4. Haptic feedback, if the tablet has a vibration motor.
