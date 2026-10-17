# Tastra

A Gboard-style on-screen keyboard for KDE Plasma 6 on Wayland (KWin), made for
touch tablets such as the Minisforum V3. Native C++ / Qt 6 / QML (no Java,
Electron or browser engine), fully offline, light on memory. English, German,
Ukrainian and Russian; Spanish, French, Italian, Portuguese, Polish and Dutch
can be turned on in Settings → Languages. GPL-3.0-or-later (see `COPYING`).

Status: **0.7.18, in development**. Version 1.0 means everything Gboard
does, as far as it can be done offline on KWin; what is done and what is
missing is listed in `docs/GBOARD-PARITY.md`. Checking on the device:
`docs/TEST-PLAN.md` (a short first test in Russian: `docs/FIRST-TEST.ru.md`).
The keyboard
was called "V3 Keyboard" up to 0.6.1; its settings and words move over
automatically.

## Install

On Arch Linux:

    sudo pacman -Syu --needed git base-devel cmake ninja pkgconf python hunspell \
        qt6-base qt6-declarative qt6-wayland qt6-svg qt6-multimedia qt6-multimedia-ffmpeg \
        wayland wayland-protocols libxkbcommon
    git clone https://github.com/M0npet/Tastra-Keyboard.git ~/.local/src/tastra
    ~/.local/src/tastra/scripts/tastra-install.sh
    kcmshell6 kcm_virtualkeyboard      # None -> Apply -> Tastra -> Apply

The installer runs a static check, a fresh Release build and the full test
suite (about a minute), and installs into `~/.local` only when everything is
green. The previous binary is kept for rolling back. The helpers below are in
`~/.local/bin`; Arch does not put that on the `PATH` by default, so either
type `~/.local/bin/tastra-update` and so on, or add it once:
`echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc`.

- Update to the latest version: `tastra-update` (or `tastra-update --check`).
  It also rebuilds when Qt was updated (the keyboard uses Qt's private Wayland
  classes, so run it after a system upgrade that brings a new Qt), when voice
  input or key sounds were set up, and after a rollback; `--force` always
  rebuilds.
- Roll back to the previous build: `tastra-rollback` (again to go further
  back).
- Dictionaries: `tastra-dictionaries [--status]` (EN/DE/RU and ES/FR/IT/PL/NL
  from pacman, UK and PT from pinned, checksummed upstream files; the
  languages turned on in settings are included). Without a dictionary a
  language still completes and suggests words, but Space does not correct.
- Offline voice input (optional): `tastra-voice-setup`, then `tastra-update`.
- Uninstall: `tastra-uninstall [--purge]`.

## Typing

- Words are composed underlined and committed by Space, punctuation, a
  suggestion or glide, so autocorrect works reliably even in Firefox.
- Autocorrect: Hunspell + word frequency + where on the key you touched, also
  for several slipped keys in one word, and two words typed without the space
  ("ofthe" -> "of the"). It is conservative, and Backspace right after a
  correction undoes it and remembers your word. Tap back into a corrected word
  to get what you typed back.
- Suggestion strip: your word, the **correction** (what Space inserts) and the
  next best; completions; next-word predictions from the first word on
  (bundled word pairs from open corpora), adapting to your own typing;
  emoji suggestions; long-press a suggestion to forget it. Offensive words are
  never suggested.
- Several languages at once: with English and German (or Ukrainian and
  Russian) enabled, words of the other one are left as typed, so `danke` is
  not "corrected" while English is active.
- Typed in the wrong layout? `ghbdtn` offers `привет`; picking it switches the
  language.
- Tap into the middle of a word for suggestions for the whole word.
- Capital letters follow Android's keyboard rules: sentence starts, but not
  after abbreviations such as "e.g."; quotes are handled, and German letters
  and dates too (no capital on the line after "Liebe Anna," or after "3.").
  Double Space types ". ", and a space is added after punctuation.
- Glide typing matched on the shape of the finger's path, with the other
  readings offered in the strip afterwards, one Backspace to erase a glided
  word, Shift or Caps Lock for capitals; a trail (can be turned off);
  two-thumb typing,
  long-press for accents and symbols (ß, ё, ґ, digits) with an adjustable delay
  (200–700 ms), two symbol pages with your currency, number row, key popups,
  optional key click.
- Gestures: drag Space to move the cursor, drag Backspace to delete, hold
  Backspace to repeat, slide from `?123` for one symbol or from Shift for one
  capital.
- Field-aware: number pad in number and phone fields, `@` and `.com` in e-mail
  and URL fields, no autocorrect or learning in passwords, URLs and e-mail.
- Layouts: full, split for thumb typing, and one-handed left/right. Choose
  which languages the globe key cycles through. A button hides the keyboard.
- Personal dictionary: tap an unknown word (shown in quotes), then
  "+ Add to dictionary", or Settings → "+ Add word" with an optional shortcut.
  Manage both in Settings, or add many words at once in
  `~/.config/tastra/dictionary.txt`. Words you type 3 times become yours; a
  single typo is never learned.
- Text shortcuts from Settings or `~/.config/tastra/shortcuts.txt` (`omw = on my way`).
- Emoji panel with search in all four languages (typed on the keyboard itself),
  a `:-)` tab of text faces, recent emoji, skin tones on long-press, and an
  optional emoji row.
- Clipboard: a chip offers what you just copied; history keeps items for an
  hour; long-press to pin an item for good.
- Text-editing panel as in Gboard: arrows, Home/End, Select (the arrows then
  extend the selection), Select all, Copy, Cut, Paste.
- Settings grouped like Gboard; System/Light/Dark/AMOLED themes. The interface
  is in English, German, Russian and Ukrainian (follows the system language).

## Platform limits (KDE Plasma / KWin)

- KWin replaces the modifiers of keys sent by an input method, so the
  text-editing panel sends Select (Shift+arrows), Select all, Copy and Cut as
  keyboard input through KDE's fake-input protocol (the one KDE Connect uses);
  KWin up to 6.7 offers it only to programs whose desktop file asks for it,
  as the installed one does.
  Where that protocol is missing, Copy and Cut work on text selected by touch.
  Plasma 6.5 and newer take the shortcuts as key symbols, so they follow your
  layout; older KWin gets US key positions (with QWERTZ, Undo would arrive as
  Ctrl+Y). In terminals only Copy is sent (as Ctrl+Shift+C).
- The clipboard is read and set through KWin's data-control protocol (as
  Klipper does), because KWin offers the regular Wayland clipboard only to the
  focused window, which an on-screen keyboard never is. Needs Plasma 6.4 or
  newer (and wayland-protocols 1.39 when building).
- KWin decides where the panel goes, so there is no floating keyboard.

## Memory and speed

One language is in memory at a time, and its dictionary loads in the
background. The panels (settings, emoji, clipboard) are built when first
opened, so the keyboard itself starts in under 0.2 s. Resident memory with the dictionary loaded (`tools/bench_memory`):
English about 27 MB, German about 25 MB, Russian about 39 MB, Ukrainian about 62 MB
(Hunspell's Ukrainian dictionary alone is about 39 MB). A keystroke costs
about one million instructions (`tools/bench_keystroke`). Learned words are
kept to 10 000 words and 30 000 word pairs per language, as in LatinIME's
user history, and saved in the background.

## Privacy

Nothing leaves the device. Settings and learned words are kept in
`~/.config/tastra`. Dictation audio stays in memory only (30 s at most) and is
erased after recognition. The optional trace
(`mkdir -p ~/.local/state/tastra && touch ~/.local/state/tastra/trace.enable`
before starting the keyboard)
records sizes and decisions only, never text.

## Third-party data

FrequencyWords, wordfreq and Universal Dependencies treebanks (CC BY-SA 4.0), UA-GEC
(CC BY 4.0), Common Voice sentences (CC0), Unicode CLDR annotations (Unicode License V3),
LDNOOBW word lists (CC BY 4.0), Hunspell dictionaries (system packages),
LibreOffice uk_UA (MPL-1.1) and pt_BR (LGPLv3/MPL) dictionaries, optional whisper.cpp + ggml model
(MIT), KDE's fake-input protocol description (LGPL-2.1-or-later,
`protocols/`). Details are in `data/*/ATTRIBUTION.md` and `protocols/README.md`,
installed to `~/.local/share/doc/tastra`.

## Development

    cmake -S . -B build -G Ninja && cmake --build build
    QT_QPA_PLATFORM=offscreen ctest --test-dir build
    python3 scripts/verify-static.py
    cmake -S . -B build -DTASTRA_BENCHMARKS=ON    # tools/bench_keystroke, tools/bench_memory

Continuous integration (`.github/workflows/ci.yml`) builds and runs the full
test suite on Arch Linux for every push. History, evidence and decisions are in
`CHANGELOG.md`, `docs/ledger.md` and `docs/TEST-PLAN.md`.
