# Tastra — live test checklist (most important first)

Before: `mkdir -p ~/.local/state/tastra && touch ~/.local/state/tastra/trace.enable`, then relaunch the
keyboard. On any problem send `~/.local/state/tastra/trace.log`.

## A. Core typing (Firefox: claude.ai / ChatGPT, and a KDE app e.g. Kate)
1. Fast two-thumb typing with overlapping taps: no lost letters, no lag.
2. `teh` Space -> `the `; Backspace right after -> `teh` (kept next time).
3. `tge` / `thw` -> `the`; strip shows “teh” + **the** while typing `teh`.
4. Suggestion tap -> word + one space; Space after it -> no period.
5. `hi` Space Space -> `hi. ` and the next letter is uppercase.
6. Word + Enter in a chat: whole word sent.
7. Type a word, switch window, come back, continue: nothing lost.
8. Tap into the middle of text, continue typing: no duplicated letters.

## B. Keys and gestures
9. Long-press q -> 1; e (EN) -> picker (slide to è); е (RU) -> ё; `.` -> 16 marks.
10. `?123` slide to a symbol; Shift slide to a letter; `'` on symbols -> letters.
11. Space drag moves the cursor; Backspace drag deletes; long-press Space -> languages.
12. Glide a word: trail visible, word inserted with one space.

## C. Fields and panels
13. Password field: no underline, no suggestions. E-mail: `@`; URL: `/`, `.com`.
14. Phone/number field: number pad.
15. Emoji: search `улыб`/`lachen`, recent category; long-press comma opens emoji.
16. Settings: theme System/Light/Dark/AMOLED, number row, compact layout,
    symbol hints, auto-space, emoji row; About shows the installed version.

## D. Languages and extras
17. Globe: EN/DE/RU/UK switch instantly; first-letter suggestions per language.
18. Text shortcut: `~/.config/tastra/shortcuts.txt` with `omw = on my way`.
19. Voice (if installed): 🎤, speak, 🎤 -> text in the keyboard language.
20. `tastra-dictionaries --status`, `tastra-voice-setup --status`.
21. Settings → Keyboard layout → Split (landscape): halves apart, thumbs reach the space bar; typing works on both halves.
22. Tap into the middle of a misspelt word (e.g. "wrold"): the strip offers "world"; tapping it replaces the whole word.
23. English active, type "ghbdtn": strip offers "привет"; tap -> inserted, keyboard switches to Russian.
24. Copy text in an app: a 📋 chip appears in the strip; tap pastes it.
25. Settings → Long-press delay; long-press symbols react at the chosen speed.
26. ?123 → currency of the language; "=\\<" → second page with < > { } [ ] \\ |; long-press " → « » „ “.

## E. Tastra name and install (0.6.2)
27. After installing over 0.6.1: `ls -la ~/.config/tastra ~/.local/share/tastra` show your dictionary.txt, shortcuts.txt, tastra.conf and dictionaries; `~/.config/v3-keyboard` is a link.
28. Your theme, pinned clipboard items and added words are still there.
29. `kreadconfig6 --file kwinrc --group Wayland --key InputMethod` names `io.github.m0npet.Tastra.desktop`; after relogin the keyboard still appears.
30. `tastra-update --check` says up to date; `tastra-rollback` brings the previous build back (then `tastra-update` again).


## F. Autocorrect like Android's keyboard (0.6.2)
31. English: `dont` Space -> `don't`; `im` -> `I'm`; `i` -> `I`; `ive` -> `I've`; `cant` stays, strip offers `can't`.
32. Ukrainian: `пять` -> `п'ять`, `звязок` -> `зв'язок`; Russian: `обьект` -> `объект`, `щас` stays `щас`.
33. German: `uber` -> `über`, `strasse` -> `Straße`, `naturlich` -> `natürlich`; Backspace right after undoes it.

## G. Several languages at once (0.6.3)
34. Settings → Languages: English + German on. English active: `danke` Space stays `danke`, `teh` still -> `the`; no "+ Add to dictionary" for `danke`.
35. Ukrainian + Russian on, Ukrainian active: `привет` stays; `пять` -> `п'ять`.
36. Emoji panel → last chip `:-)`: faces on wide tiles; tap `¯\_(ツ)_/¯` -> inserted as shown (boxes instead of ツ: install `noto-fonts-cjk`); it does not appear under Recent.

## H. Glide typing (0.6.4)
37. Glide `hello`, `keyboard`, `привет`, `спасибо` quickly and with sloppy curves: the right word appears.
38. Glide `too` (the same path as `to`): if `to` appears, the strip shows `too`; tap it -> `to` is replaced by `too`, the space stays.
39. Glide `всё` over е: `всё`/`все` appear (one inserted, the other in the strip).
40. Glide a word, then Backspace once: the whole word (and its space) is gone. Shift, then glide: `Hello`; Caps Lock: `HELLO`.
41. Settings → Next-word suggestions Off: after a word the strip stays empty; completions while typing still appear.
42. Emoji panel → "Search emoji": the panel closes, letters (lowercase) type into the search bar, matching emoji appear; tap one -> inserted, normal typing again; ✕ leaves without typing anything.
43. Settings → Dictionary → "+ Add word": Shift + `oldenburg` -> `Oldenburg`, ✓, shortcut `olb`, ✓ -> back in Settings, word and `olb → Oldenburg` listed; typing `olb` offers `Oldenburg`; tap the shortcut chip -> removed.

## I. Autocorrect (0.6.9)
44. Type fast and sloppily: `otjerd` + Space -> `others`; Russian `ппивеи` + Space -> `привет` (two keys off).
45. `ofthe` + Space -> `of the`; `вобщем` -> `в общем`; `thisnis` -> `this is`; Backspace right after gives back what was typed. German `Haustür` typed as `haustür` is not split.
46. Ukrainian layout: `ка` offers `казав`/`каже`…, never `как`; `зн` never offers `знаешь`.
47. Settings → Interface language: with Plasma in German the button shows `System (Deutsch)` and the settings are German; tap it through English, Deutsch, Русский, Українська: the open panel changes at once; after a restart the choice is kept.
48. Type `окей` (Russian), `naja` (German), `honour` (English) + Space: they stay; `thats` still becomes `that's`.
49. Copy text in Firefox (Ctrl+C or the context menu), open the keyboard: the chip offers it, the clipboard panel lists it, Paste inserts it. (Before 0.7.1 the keyboard could not see the clipboard at all under KWin.)
50. Select a word by touch in a text field, open the text-editing panel: Copy and Cut are enabled; Copy, then paste elsewhere with Ctrl+V on a hardware keyboard or the panel's Paste: the word arrives; Cut removes it from the field. In a password field Copy/Cut stay disabled.
51. Text-editing panel in Firefox and Kate: Select all selects the whole field; Select (button lights up), then ← → Home End extend the selection; Copy, then Paste elsewhere inserts it; Cut removes it. The keyboard stays open throughout.


## J. Predictions and human glides (0.7.3)
52. Settings → Clear learned words, then type `thank` + Space: the strip offers `you`; tap it -> `thank you ` and the strip offers the next word. German `ich` + Space -> `bin`, `habe`…; Russian `я` -> `не`…; Ukrainian `я` -> `не`, `хочу`…
53. After `.`, `!` or `?` and Space, after a double-space period, and after Return: the strip shows the toolbar, not predictions from the sentence before.
54. Text-editing panel: Undo and Redo in Kate and Firefox undo and redo the last edit; in a password field they are hidden.
55. Glide fast with rounded corners, without stopping on the letters: `the`, `world`, `people`, `привет`, `дякую` come out right; when a word misses, it is in the strip.
56. Settings → Sound on keypress On: letters click, Space, Backspace and Return each sound different; Volume on keypress (15 / 35 / 60 / 100 %) changes the loudness, each tap plays the new volume. German `guten` + Space offers `Tag` and `Morgen` with capitals.
57. Type `don`, the apostrophe from the symbols page (the keyboard returns to letters), `t` + Space: `don't` stays one word, and the strip predicts after it (`know`, `want`…). Type `so i` + Space: `I`, and the strip offers words after `I` (`have`, `am`), not `I` again.
58. German keyboard layout in Plasma: in Kate type a word, text-editing panel → Undo removes it, Redo brings it back (not Ctrl+Y). Copy a whole page of text in Firefox, Paste with the panel: all of it arrives and the keyboard stays. Copy a password in KeePassXC: the chip does not show it, the clipboard panel shows dots and no history entry, Paste inserts it. Tablet upright: all buttons of the text-editing panel are inside the panel. In Konsole, Copy copies the terminal's selection and Cut/Undo are greyed out or hidden.
59. claude.ai, empty composer: type the first words quickly; no letter is lost or replaced and only the first letter is a capital. Type `Hello.`, then tap elsewhere in the text, and switch to another window and back: `Hello` is not typed a second time. With the number row on, ?123 and back: the keyboard's top edge stays where it is.
60. Fast two-thumb typing (`the`, `hello`, `привет`) with overlapping presses: letters in the right order, none doubled or lost. Two thumbs on Space quickly: two spaces (or `. `). Tap at the very edge of a key or a bit past it: the key types. Roll a thumb a little on a letter: the letter, not a word. Hold Backspace and hide the keyboard: deleting stops.

## K. Learning (0.7.10)
61. Settings → Clear learned words. In Kate type `quokkas` + Space three times, stay in the field, then System Settings → Virtual Keyboard → None → Apply, and Tastra → Apply again: typing `quo` offers `quokkas` first (the words are saved before KWin ends the keyboard). Type quickly for a few minutes: no pause every 16 words.

## L. Words the cursor touches (0.7.11)
62. In Kate and in claude.ai: type `I need hel`, tap somewhere else in the text, then right after `hel` at the end, type `p`: `help` is underlined again; tap `helpful` in the strip -> `I need helpful `, nothing doubled (before: `helphel`).
63. `cat` + Space, Backspace: the strip shows suggestions for `cat`; `s` + Space -> `cats `. `hello` + Space, Backspace, `,` -> `hello, `.
64. Glide right after a typed word (no Space) or after `Hi.`: a space comes first (`Hi. The`). Tap right after a word in the text and glide: the word stays, the glided word follows it with a space.
65. Settings → Underline word while typing Off; in claude.ai type `hepl` and tap `help` in the strip right away: `help `. On again: start an empty message with `yhe`, tap `the` if the strip offers it: `the`, not `ythe`.

## M. Editing in the middle (0.7.12)
66. In claude.ai and Kate: text `I dogs`, tap right after `I`, type Space + `lov`, tap `love` in the strip, then tap at the end of the text: `I love dogs` with one space. Text `I need.`, tap before the period, Space + `hel`, tap `help`: `I need help.`.
67. Settings → Underline word while typing Off, in claude.ai: glide `to`, tap `too` in the strip: `too ` (not `to too`); glide again and Backspace: the glided word is gone. Tap right after `I` in `I dogs` and glide `the`: `I the dogs`.

## N. Gboard gaps closed (0.7.13)
68. In Kate and claude.ai, after `one two three four`: touch Backspace and slide left: `four`, then `three four` become selected (nothing deleted yet); slide back a bit: only `four` stays selected; lift: `four` is deleted. Slide left and all the way back, lift: nothing deleted. In a password field the slide deletes as before.
69. Copy `Write to anna@example.org today` (or an SMS-style `Your code: 583920`), open the keyboard: next to the 📋 chip there is `anna@example.org` (`583920`); tap it: only that is inserted.
70. Voice input (if set up): dictate `Hello world.`, then say only `Delete last word`: `world.` goes; `Send`: the message is sent; `New line`: a line break; `Heart emoji`: ❤️; `How are you question mark`: `How are you?`. In Russian: `Удали последнее слово`, `Отправить`, `Эмодзи сердце`.
71. Settings → Show emoji switch key On: an emoji key replaces the globe next to the comma, Show language switch key is greyed out; both Off: the space bar is longer; long-press Space still lists the languages.
72. Touch and hold the comma, slide onto ⚙ and release: settings; onto ✋: the one-handed keyboard (again: full width); release on 😊: emoji.
73. Settings → Letter layout (English): QWERTY → QWERTZ → AZERTY → Dvorak → Colemak; the keys change at once, glide and correction still work, the choice stays after a restart; German offers QWERTZ and QWERTY.
74. Settings → Theme → Colour: each colour tints the keyboard (light and dark); the "A" circle goes back to the plain theme. Put a photo into ~/Pictures, open Settings again: Background photo shows it; tap it: the photo is behind the keys, the strip shows it too; Photo brightness darkens it; None removes it.
75. Toolbar ⇕: a frame covers the keys; drag the bar up: the keyboard grows under the finger, down: it shrinks; Reset: back to normal; ✓: typing again. Settings → Key size shows the new value.
76. Settings → Toolbar: switch One-handed mode On and move it to the top with ↑: the toolbar shows ✋ first and it switches the one-handed keyboard; Clipboard Off: its button goes; Settings cannot be switched off; after a restart the toolbar stays as chosen. Settings → Theme → Gradients: each gradient is behind the keys; choosing a photo removes the gradient and the other way round.

## O. Phrases and Morse (0.7.17)
77. In Kate type `see you soon.` + Space three times. Then `se`: the strip's last chip is `see you soon` with `you soon` dimmed; tap it: `see you soon ` is typed. `see` + Space: the chip `you soon`. Touch and hold the chip: it goes and does not come back after `see `; `see` and `soon` are still suggested. Settings → Text correction → Phrase suggestions Off: no phrase chips.
78. Settings → Letter layout (English) → Morse code: a big dot key, a dash key and Backspace instead of the letters. Tap `• • •` and wait: `s` (`S` at the start of a sentence); `• —`, then Space: `a` at once; while keying, the strip shows `•— a` and `r •—•`, `w •——`…; tap `r` there: `r`. Backspace right after a dot removes only the dot. Settings → Morse: character timeout Never: only Space types the letter; Morse: word timeout 1.0 s: a pause after a word adds the space and corrects it. In Russian: Morse code layout, `•—•—` → `я`.

## P. Clipboard images, voice edits (0.7.18)
79. Take a screenshot with Spectacle and copy it (Ctrl+C), open a chat in Firefox (e.g. a web messenger that accepts pasted images) and the keyboard: the strip shows a chip with the picture; tap it: the image is pasted. Copy some text, open the clipboard panel: the screenshot is listed as a picture; tap it: pasted again. Long-press pins it; after a restart it is still there. In Kate (plain text) a tap pastes nothing, and nothing breaks.
80. Voice input (if set up), in Kate: dictate `See you tomorrow at the station.`, then say `Change tomorrow to today`: `today`; `Insert main before station`; `Delete today`; `Capitalize station`. Say `Change Monday to Friday` (not in the text): it is typed as words. In Russian: `Замени станция на вокзал`. In a form, `Next`: the next field.

## Q. More languages (0.7.19)
81. Settings → Languages: turn on Español and Français; run `tastra-dictionaries`. Long-press the globe: both are listed. Spanish: the ñ key, `cancion` + Space → `canción`, `graci` offers `gracias`; long-press `a`: á à ä â. French: AZERTY with an apostrophe key, `cest` + Space → `c'est`, `tres` → `très`; Settings → Letter layout offers QWERTY. Turn them off again: the globe goes back to the four.
