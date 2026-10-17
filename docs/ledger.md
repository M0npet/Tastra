# Development Ledger

## 2026-09-24 — Baseline

Status:
- Specification v0.1 approved
- Implementation plan approved
- Execution started
- Repository initialized

Current milestone:
- KWin vertical slice

Architecture invariant:
- compositor-specific code stays outside core/model

## 2026-09-24 — RED: basic text commit

Test:
- KeyboardControllerTest::tappingTextKeyCommitsExactlyOnce

Required behavior:
- tapping textual key "a"
- calls backend commitText("a")
- exactly once

RED evidence:
- production InputMethodBackend / KeyboardController do not exist yet

Next:
- implement the minimum core classes required to make this test green

## 2026-09-24 — GREEN: basic text commit

Implementation:
- InputMethodBackend interface added
- KeyboardController added
- tapText() forwards text to commitText()
- core is built as a compositor-independent static library

Verification:
- full build required
- full CTest suite required

## 2026-09-24 — REFACTOR: backend lifetime invariant

Change:
- KeyboardController now requires InputMethodBackend by reference.
- Null backend state is impossible by construction.

Verification:
- full build passed
- full CTest suite passed
- git diff --check passed

## 2026-09-24 — RED: KWin input-method-v1 commit seam

Required behavior:
- active v1 context receives commit text
- commit uses the latest compositor commit_state serial
- no active context means commit is ignored
- deactivated context receives no commits

Design boundary:
- protocol context is abstracted behind InputMethodV1Context
- actual QtWayland protocol object will be added after this seam is green

## 2026-09-24 — GREEN: KWin input-method-v1 commit seam

Implementation:
- KWinInputMethodV1Backend implements the compositor-independent InputMethodBackend interface
- protocol context is isolated behind InputMethodV1Context
- latest commit_state serial is stored explicitly
- commit without an active context is ignored
- deactivation is represented by clearing the active context

Verification:
- full build required
- full CTest suite required
- git diff --check required

Next:
- add the real QtWayland input-method-v1 protocol adapter behind InputMethodV1Context

## 2026-09-24 — RED: KWin input-method-v1 session lifecycle

Required behavior:
- activation binds the active protocol context
- commit_state updates the serial used for commit_string
- deactivation clears only the matching active context
- stale deactivation must not clear a newer context

## 2026-09-24 — GREEN: KWin input-method-v1 session lifecycle

Implementation:
- session owns protocol lifecycle state, not protocol objects
- backend is updated on activation/deactivation
- latest serial is accepted only while a context is active
- stale deactivation is ignored

Next:
- connect this tested session to the real QtWayland input-method-v1 callbacks

## 2026-09-24 — RED: real QtWayland input-method-v1 adapter

Goal:
- compile a concrete KWin input-method-v1 client extension
- keep protocol objects behind the tested backend/session boundary
- use generated QtWayland client bindings from the system wayland-protocols XML

## 2026-09-24 — GREEN: real QtWayland input-method-v1 adapter

Implementation:
- Qt 6 WaylandClient generates client bindings from wayland-protocols
- KWinInputMethodV1Connection implements zwp_input_method_v1
- protocol activation creates a concrete context adapter
- protocol deactivation clears only the matching context
- commit_state is forwarded into the tested session/backend path
- commitText ultimately maps to zwp_input_method_context_v1.commit_string

Boundary:
- generated/protocol-specific types stay in platform/kwin
- core/model remains compositor-independent

Next:
- expose activation state to the application layer
- add input-panel-v1 surface integration
- then build the first minimal QML keyboard executable

### Stage 8 build repair

The input-method-v1 XML references the core `wl_keyboard` interface.
Qt 6.11 generates extension C bindings with `wayland-client-core.h` by
default, which does not declare `wl_keyboard_interface`.

For this protocol target we use `NO_INCLUDE_CORE_ONLY`, causing the generated
binding to include the full Wayland client protocol declarations required by
the referenced core interface.

### Stage 8 build repair #2

The generated `qwayland-input-method-unstable-v1.h` is part of the build tree.
`kwininputmethodv1connection.h` includes it, so build-tree consumers of
`v3keyboard-platform-kwin` also need the binary directory on their include path.

Added `${CMAKE_CURRENT_BINARY_DIR}` as a BUILD_INTERFACE include directory.
This is intentionally build-only; an installed public API should later hide
generated protocol types behind a private implementation boundary.

## 2026-09-24 — RED: QML input bridge

Required behavior:
- QML-facing bridge accepts text taps
- bridge forwards text into KeyboardController
- UI remains independent of KWin protocol types

## 2026-09-24 — GREEN: KWin input panel + minimal QML surface

Implementation:
- QML-facing KeyboardUiBridge added
- KWin input-panel-v1 shell integration added
- input panel surface uses center-bottom toplevel role
- real v3-keyboard executable added
- context activation controls keyboard visibility
- first QML key commits "a" through the tested controller/backend path
- Plasma virtual-keyboard desktop metadata added, not installed yet

Source provenance:
- low-level QtWayland input-panel shell/surface structure is adapted from KDE plasma-keyboard
- original Jan Arne Petersen LGPL-2.1-only attribution retained in adapted files

Safety:
- this stage is build/test only
- executable is NOT launched against the live Plasma session
- desktop entry is NOT installed yet

Next:
- local user install
- select V3 Keyboard in Plasma System Settings
- manual E2E smoke: focus field -> panel appears -> tap A -> "a" committed -> focus leaves -> panel hides

### Stage 9 build repair #2

The input-panel integration intentionally uses Qt private Wayland/QPA headers.
On Arch Linux, the private imported CMake targets are packaged separately from
the public module discovery and must be loaded explicitly.

Added:
- Qt6GuiPrivate
- Qt6WaylandClientPrivate

This keeps the private-API dependency isolated inside the KWin platform module.

### Stage 9 build repair #3

`qt_generate_wayland_protocol_client_sources()` generates both C and C++
sources. The generated C source defines protocol interface symbols such as
`zwp_input_method_v1_interface` and `zwp_input_panel_v1_interface`.

The project initially enabled only CXX, so the generated C protocol object was
not part of the link, leaving those interface symbols undefined.

Enabled both C and CXX at project level.

## 2026-09-24 — Milestone 1 manually verified on Plasma/KWin

Verified in the live user session:
- KWin discovered and launched V3 Keyboard
- focusing a real text field activated input-method-v1
- the input-panel-v1 QML surface appeared
- tapping the test A key committed "a" into the real application
- removing focus hid the keyboard

Milestone 1 status: complete.

## 2026-09-24 — RED: basic keyboard model

Required behavior:
- lowercase by default
- first Shift enables one-shot uppercase
- one-shot Shift is consumed by a letter
- second Shift enables Caps Lock
- Caps Lock survives typed letters
- pressing Shift while Caps Lock is active returns to lowercase

## 2026-09-24 — GREEN: QWERTY + Shift/Caps + Space

Implemented:
- English QWERTY letter rows
- one-shot Shift
- Caps Lock by pressing Shift twice
- Space
- UI reads case state from KeyboardModel
- UI remains independent of KWin/Wayland protocol types

Next:
- semantic Backspace and Enter actions
- map those actions to input-method-v1 custom keysyms

## 2026-09-24 — RED: semantic Backspace and Enter

Required behavior:
- KeyboardController exposes Backspace and Enter as semantic actions
- compositor-specific key generation stays behind InputMethodBackend

## 2026-09-24 — GREEN: semantic Backspace and Enter

Implemented:
- InputMethodBackend now exposes Backspace and Enter semantics
- KeyboardController forwards those actions without compositor knowledge
- KWin input-method-v1 backend maps them to custom keysyms
- both press and release events are emitted with the latest serial
- QML now exposes Backspace and Enter keys

Protocol rationale:
- normal text continues to use commit_string
- custom key events use input-method-v1 keysym, as specified by the protocol

## 2026-09-24 — Milestone 2 manually verified on Plasma/KWin

Verified in the live user session:
- English QWERTY input works
- one-shot Shift works
- Caps Lock works
- Space works
- Backspace works
- Enter works

Milestone 2 status: complete.
This commit is the known-good checkpoint before mobile layout work.

## 2026-09-24 — RED: alphabet/symbol layer state

Required behavior:
- keyboard starts in alphabet layer
- ?123 switches to symbol layer
- switching layer clears Shift/Caps state
- ABC returns to alphabet layer

## 2026-09-24 — GREEN: mobile layout foundation

Implemented:
- responsive panel width based on the active screen
- separate portrait/landscape geometry constants
- centered content with a maximum ergonomic width
- alphabet and ?123 symbol layers
- comma and period on the primary bottom row
- ABC return key on symbol layer
- wider Space, Shift, Backspace and Enter keys
- symbol-layer switching resets Shift/Caps state

Architecture:
- layout state lives in KeyboardModel
- QML is responsible for visual geometry only
- KWin/Wayland integration is unchanged from the known-good checkpoint

## 2026-09-24 — RED: tablet mobile-layout metrics

Visual goals derived from live V3 portrait/landscape screenshots:
- landscape should be materially more compact vertically
- landscape should use more horizontal space than the previous prototype
- portrait should stay close to full width
- geometry must remain explicit and independently testable

## 2026-09-24 — GREEN: Gboard-inspired geometry and interaction pass

Implemented from live V3 screenshots:
- landscape panel reduced to a compact tablet height
- landscape keyboard uses a wider centered content area
- portrait remains near full width
- row geometry now uses consistent 10-key base slots
- second row is naturally centered without arbitrary percentage width
- Shift and Backspace use wider functional-key geometry
- bottom row proportions are closer to a modern mobile keyboard
- original vector icons added for Shift, Caps Lock, Backspace and Enter
- Enter receives an accent treatment
- Space displays the current first language label
- key press animation added
- letter/symbol press popup added

Design note:
- this is Gboard-inspired geometry/interaction, not Google branding or copied assets
- suggestion strip, emoji/language switching and toolbar remain intentionally absent until functional

## 2026-09-24 — Hotfix: QML key-popup anchor

Root cause:
- Stage 16 popup used `bottom: parent.top`
- Rectangle has no standalone `bottom` property
- QML component creation therefore failed and the keyboard process exited before
  input-panel initialization

Fix:
- use the valid anchor property `anchors.bottom: parent.top`

## 2026-09-24 — RED: multi-language layout state on refined tablet UI

Required behavior:
- English QWERTY
- German QWERTZ with Ü/Ö/Ä
- Ukrainian layout with Ї/І/Є
- Russian layout with Ъ/Ы/Э
- deterministic EN -> DE -> UK -> RU cycle
- changing language clears Shift/Caps
- refined tablet geometry and key popup remain untouched

## 2026-09-24 — GREEN: multi-language layouts on refined tablet UI

Implemented:
- English QWERTY
- German QWERTZ with Ü/Ö/Ä
- Ukrainian with Ї/І/Є
- Russian with Ъ/Ы/Э
- globe key cycles EN -> DE -> UK -> RU
- active language is shown on Space
- language rows come from KeyboardModel

Preserved:
- tablet portrait/landscape metrics
- refined key visuals
- corrected key popup anchoring
- KWin/Wayland backend unchanged

Next:
- Gboard-style language chooser popup
- toolbar foundation

## 2026-09-26 — RED: toolbar and primary-panel model foundation

Specified:
- toolbar action IDs must be unique
- default action order is deterministic
- hidden actions are excluded from the visible toolbar model
- disabled actions cannot activate
- language action targets the Language panel
- PanelManager starts in Typing and can return to Typing

Constraints:
- no KWin/Wayland transport changes
- no visible QML toolbar yet
- no dead placeholder buttons exposed

## 2026-09-26 — GREEN: toolbar and primary-panel model foundation

Implemented:
- PanelId + PanelManager
- ToolbarAction + ToolbarActionKind
- ToolbarRegistry with duplicate-ID protection
- ToolbarModel visible-order filtering and activation
- initial registered actions: language, clipboard, emoji, text-editing, settings
- only language is visible at this milestone; unfinished panel actions stay hidden
- KeyboardUiBridge exposes active panel and visible toolbar action IDs

Preserved:
- existing globe/language UI
- existing keyboard QML
- KWin/Wayland transport
- all live typing behavior

Architecture:
- toolbar identity/order/state now lives in C++, not QML
- primary panel state now lives in C++, not QML

## 2026-09-26 — RED: toolbar renderer and language chooser data

Specified:
- ToolbarModel exposes visible action descriptors for the UI
- KeyboardModel exposes supported language codes and labels
- QML must consume those model surfaces instead of duplicating product state

## 2026-09-26 — GREEN: rendered toolbar and language chooser

Implemented:
- toolbar renders C++ ToolbarModel descriptors
- Language is the only visible toolbar action at this milestone
- toolbar Language action opens the Language panel
- bottom globe opens the same Language panel
- chooser renders EN / DE / UK / RU from KeyboardModel data
- selecting a language returns to Typing
- current language remains visible on Space

Preserved:
- existing tablet layout metrics
- key popup behavior
- KWin/Wayland transport
- unfinished toolbar actions remain hidden

## 2026-09-26 — RED: Gboard-aligned language switching UX

Specified:
- Language remains registered as a panel capability but is not a visible toolbar action
- unfinished default toolbar actions stay hidden
- language switching belongs to the bottom-row Globe/Space interaction

## 2026-09-26 — GREEN: compact bottom-row language chooser

Changed:
- Language removed from visible top toolbar
- empty Stage 19 toolbar removed from QML
- tap Globe cycles languages
- long-press Globe opens compact language popup
- long-press Space opens the same popup
- popup overlays the keyboard instead of replacing it
- keyboard stays visible while choosing
- oversized Languages page and ABC close button removed
- popup closes with × or after selection

Preserved:
- ToolbarRegistry / ToolbarModel architecture
- EN / DE / UK / RU layouts
- current language on Space
- tablet geometry and key popups
- KWin/Wayland platform layer

## 2026-09-26 — V3 Keyboard Alpha 0.1 integrated pass

Goal:
- stop shipping isolated UI experiments and produce one cohesive daily-test alpha
- preserve the known-good KWin input-method path while making the keyboard useful enough for real tablet testing

Integrated:
- compact top tool row with working Clipboard, Emoji, Text editing and Settings actions
- Language remains exclusively on the bottom Globe / Space interaction; no duplicated language toolbar control
- compact long-press language chooser for EN / DE / UK / RU
- persisted active language via QSettings
- German ß / ẞ, Ukrainian ґ / Ґ and Russian ё / Ё long-press alternates
- current system clipboard preview, paste and clear
- basic emoji panel
- text-editing panel with Left / Right / Home / End / Delete / Backspace
- persisted Dark / AMOLED appearance
- persisted key-size, key-border and key-popup preferences
- panel state returns to Typing when the input context closes
- tablet portrait / landscape geometry preserved and key-height scaling made live

Still deliberately deferred from Alpha 0.1:
- autocorrect and suggestions
- language model / prediction engine
- glide typing
- clipboard history database
- full emoji catalog/search
- split/floating keyboard modes
- LayoutEngine / KeyboardGeometrySnapshot refactor

## 2026-09-26 — Beta 0.2 smart typing and daily-use completion pass

Added a privacy-first local typing engine on top of the Alpha 0.1 shell.

Core:
- LocalLexicon loads system Hunspell dictionaries from `/usr/share/hunspell` when present
- built-in fallback vocabulary keeps suggestions functional without extra packages
- prefix suggestions and one-edit typo correction
- per-language local word learning and previous-word bigram ranking in QSettings
- TypingEngine tracks the current word while preserving the existing immediate-commit KWin transport
- suggestion replacement, autocorrect-on-space, auto-capitalization, double-space period
- local glide decoder API with dictionary/personal/bigram scoring

UI/UX:
- suggestion strip takes over the top row while typing; tool row remains one tap away
- settings toggles for suggestions, autocorrect, local learning, auto-capitalization, double-space period and glide
- swipe Space horizontally for cursor movement
- long-press/drag Backspace for accelerated deletion
- letter-key glide gesture plumbing into the local decoder

Clipboard / emoji:
- persistent local clipboard history with dedupe, paste, per-item remove and clear-history
- clipboard history can be disabled
- emoji catalog loads Unicode `emoji-test.txt` from the system when available
- curated fallback emoji set when system data is absent
- emoji categories and name search surface

Privacy:
- no network calls
- no telemetry
- no Google services
- learning and clipboard history remain local in QSettings

Still outside the KWin functionality-complete target:
- wlroots backend and GNOME bridge portability work
- true compositor-positioned floating keyboard window
- handwriting and voice input

## 2026-09-26 — Input-context awareness and privacy guard

Added:
- KWin input-method-v1 surrounding-text synchronization
- content hint/purpose propagation into the typing engine
- password/PIN/sensitive-field detection with suggestion and learning suppression
- preferred-language propagation from the client context
- composition reset on client reset/deactivation

This closes the main privacy gap in smart typing: secure fields do not expose candidates or feed local learning.

## 2026-09-26 — Beta 0.2.1 autocorrect determinism fix

Root cause from the first full on-device CTest run:
- `doubleSpaceProducesPeriodSpace` observed 3 backspaces instead of 1.
- `hi` was not guaranteed to exist in the fallback dictionary; on systems without a matching Hunspell entry, one-edit autocorrect rewrote the 2-character token before the second Space.

Fix:
- automatic correction now requires at least 3 characters; 2-character tokens are never automatically rewritten
- added a dedicated regression test for short-token preservation
- keeps `teh -> the` autocorrect and double-space punctuation behavior intact
- makes the test suite independent of which system Hunspell dictionaries happen to be installed

## 2026-09-26 — Beta 0.2.2 live-input repair

Live V3 testing exposed three coupled issues in smart typing:
- stale `surrounding_text` echoes from browser/Qt clients could overwrite the local word currently being typed, breaking autocorrect and suggestion replacement
- normal letter taps emitted `keyboardStateChanged`, invalidating all key-row bindings on every character and causing visible lag in Chromium/ChatGPT fields
- learning serialized its full settings maps twice per committed word

Fixes:
- local composition is authoritative while a local edit is awaiting a matching surrounding-text echo
- stale pre-space and partial-word echoes are ignored instead of clobbering composition
- surrounding-text updates that do not change state no longer trigger UI invalidation
- ordinary taps emit only `suggestionsChanged`; full keyboard invalidation is reserved for real case/layout changes
- `.`, `!`, `?` arm capitalization immediately
- word+bigram learning persists once per finalized word
- repeated backspace updates composition/suggestions once per batch
- regression tests cover stale echoes, direct punctuation capitalization, signal hot-path behavior, and batched backspace

## 2026-09-29 — Beta 0.2.3 root-cause pass (supersedes 0.2.2)

Baseline:
- handoff archive checksums verified; `project/` identical to the 0.2.2 payload
- evidence file named 0.2.1 actually contains the 0.2-final run (10/11)
- sandbox: Qt 6.4.2; KWin panel shell needs Qt >= 6.5, so the executable and
  KWin tests are built on-device (KWin input-method layer verified via overlay)

RED (each observed failing before the fix):
- real en_US Hunspell: `teh -> meh` (3 smarttyping failures on 0.2.2)
- `externalCursorMoveAdoptsClientState`: 0.2.2 kept "hel" after a cursor move
- `autocorrectUsesTextChannelOnceClientConfirmedWord`,
  `unconfirmedWordIsNotRewrittenOnTextInputClients`,
  `suggestionAndDoubleSpaceUseTextChannel`: Backspace keysyms used
- bridge: date purpose treated as secret; url/email capitalized; lowercase hint ignored
- QML: `ReferenceError: root is not defined` on suggestion tap
- lexicon fixture tests: inflections rewritten, no background load

GREEN:
- same-channel replacement via delete_surrounding_text (KWin backend, UTF-8 bytes)
- predicted-state echo reconciliation replaces the 0.2.2 pending flag
- libhunspell-backed lexicon, background load, compact index, batched learning
- purpose/hint mapping per text-input-v1 as sent by KWin
- hermetic tests (fixtures + QStandardPaths test mode)
- install-time dictionary provisioning with pinned UK dictionary

Design note:
- 0.2.2 test `staleSurroundingEchoDoesNotClobberLocalWord` now includes the
  confirming echoes: rewriting an unconfirmed word is deliberately skipped.

Verification (sandbox, Qt 6.4.2): 10/10 CTest + 3/3 KWin tests via overlay,
static verify OK. On-device (Qt 6.11.2) build/test/live test pending.

## 2026-09-29 — Beta 0.2.4 composition (preedit)

Live result of 0.2.3 in Firefox: "TEH hhello hi  job" (13/13 tests on device).
Root cause from Firefox/GTK/KWin sources: surrounding-text echoes are one step
stale by construction (see docs/beta-0.2.4-composition.md).

RED:
- `oneStepStaleEchoesDoNotFlipCaseOrWord` (0.2.3 adopted stale echoes)
- composition tests: autocorrect without deletions, double space, suggestion,
  backspace in preedit, Enter order, secure fields, non-text-input clients
- bridge: commit composition before cursor moves / language switch; toggle
- KWin backend: preedit cursor in bytes + commit fallback (compile-RED)
- QML: fail on "is not declared" (only observable on Qt >= 6.11)

GREEN: composition mode, history-based echo reconciliation, explicit QML
handler parameters, opt-in file trace.

Design decision: immediate-commit replaced by composition on text-input
clients (user: "продолжай" after the A/B proposal); immediate mode kept as a
setting.

Verification (sandbox Qt 6.4.2): 10/10 CTest + 3/3 KWin overlay, static OK.

## 2026-09-29 — Beta 0.2.5 suggestion-space fix

Live result of 0.2.4 in Firefox: "the hello. Hi. Hello" — teh->the and
double-space work; tapping a suggestion then Space produced "hello. ".
Root cause: the space a suggestion inserts was flagged as a user Space
(m_lastActionWasSpace), so the next Space triggered the double-space period.
RED: spaceAfterSuggestionIsNotADoubleSpace; 0.2.3 test
suggestionAndDoubleSpaceUseTextChannel encoded the same wrong expectation and
was corrected. GREEN: the first Space after a suggestion only confirms its
space; a further Space makes ". ". Sandbox: all suites green.

## 2026-09-29 — Beta 0.2.6 echo handling from device trace

Live result of 0.2.5: "Slovoi  drugoe okno" after word + Space + window switch.
Trace (privacy-safe, 1133 lines) showed:
  31.265 COMMIT 5 + PRE 1 (pending space)
  31.268 echo 5 bytes -> ADOPT, composition dropped (client still showed it)
  31.541 COMMIT 1 ("i"): KWin's commit clears the preedit -> space lost
Cause: claude.ai's empty composer reports a 1-byte placeholder that vanishes
once text exists, so echo contents never match the keyboard's model.
Measured first-echo latency after own operations: p50 2 ms, p99 6 ms,
max 36 ms (78 samples). When the user clicked "send" mid-word, Firefox
committed the composition itself and KWin sent reset (message arrived intact).
RED: placeholderEchoDuringCompositionKeepsPendingSpace (reproduces "Slovoi"),
selfCausedEchoesAreIgnoredButLaterExternalEditsAdopted.
GREEN: echoes never abandon a live preedit (reset ends it); unknown echoes
within 150 ms of an own operation are self-caused; later ones are adopted.
Design note: externalCursorMoveAdoptsClientState now advances an injected
clock (external edits happen at human speed).
Also live-verified on 0.2.5: teh->the, suggestion without period, double-space
period, Enter keeps last word, translit untouched, email/url no auto-cap,
normal field auto-cap, window switch keeps the word.
Regression caught before release: with the settle window, the echo after a
keyboard cursor move (2-6 ms after committing the composition) was ignored,
leaving stale context (bridge test cursorMovesAndLanguageSwitch... RED: "N"
instead of "n"). Fix: forgetting the text state (cursor move, paste, client
reset) disables the settle window for the next echo. Process note: the first
0.2.6 commit was made while that test was red; it was amended after GREEN.

## 2026-09-29 — Beta 0.2.7 v0.2 completion slice

User: "finish the project, then we test". Scope = make the approved v0.2
milestone solid on the tablet; deferred spec items untouched.
RED (observed before each fix):
- overlappingTwoThumbTapsAreBothCommitted: 1 of 2 letters (MouseArea sees only
  the first touch); after switching to MultiPointTouchArea: 0 of 2 — the glide
  candidate treated two fingers as a swipe; fixed by binding glide to its
  finger. singleFingerGlideAndLongPressStillWork guards the gestures (test
  itself needed a fix: stale EN delegate lookup after a language switch).
- compositionCommitsCorrectedWordWithoutDeletions (updated design),
  backspaceRightAfterAutocorrectRevertsAndRemembers,
  correctionThenDoubleSpaceOrEnterOrPunctuation; revert exposed that the
  curated typo list overrode learned words (learnedWordsBeatTheCuratedTypoList).
- glidedWordBehavesLikeASuggestion.
- Context destroy after deactivate: compile-verified only (no compositor in
  the sandbox); exercised on device by every focus change.
Sandbox: 10/10 CTest + 3/3 KWin overlay, static verify OK.

## 2026-09-29 — 0.3.0 (user: "do the whole project, then we test")

Frequency data: license verified (FrequencyWords content CC BY-SA 4.0; CC's
2015 declaration of one-way compatibility with GPLv3). CLDR: Unicode License V3.
RED: frequencyRanksCompletionsFromTheFirstLetter (core-list bonus outranked real
frequencies -> core prior only without frequency data),
germanNounsFromFrequencyKeepTheirCapital, emojiSearchUnderstandsTheKeyboardLanguage
(ranking tiers), compactModeDocksTheKeysLeftOrRight (maxContentWidth cap; the
first version of the test passed vacuously because labels were uppercase —
made strict: keys must be found, span > 300 px, root follows the view).
Real-data bench (sandbox): suggestions 0.02–0.04 ms/keystroke after rank-first
selection (3–8 ms before); background load EN 0.3 s, RU 0.5 s, DE/UK 1.3–1.5 s.
Sandbox: 10/10 CTest + 3/3 KWin overlay, static verify OK.

## 2026-09-29 — 0.4.0 offline voice input (user: "davai dalshe" after the proposal)

Sources verified: Arch extra whisper-cpp 1.9.1 (MIT); model ggml-base-q5_1.bin
SHA-256 from the official Hugging Face LFS metadata. Built whisper.cpp v1.9.1
in the sandbox (exports CMake config + whisper.pc; pkg-config fallback added).
RED/GREEN: tst_voice (state machine with fake recorder/recognizer),
dictationCommitsPendingWordThenSentenceCasedText (engine-level expectation
corrected: capitalizing typed letters is the bridge's job),
dictationFlowsFromVoiceIntoTheField (bridge), micButtonAppearsOnlyWithVoice...
(UI; first failure was the test clicking before the Row polished its layout),
tst_voiceadapters (PCM conversion, missing model, real whisper run with the
repository test model). Both builds green: voice ON 12/12, voice OFF 11/11.

## 2026-09-29 — 0.5.0 Gboard behaviours (user: "continue, take the logic from Gboard")

Gboard behaviours taken from public descriptions (Computerworld, Android
Authority, MakeTechEasier, Gboard store listing), not code.
Licenses: LDNOOBW CC BY 4.0 (FSF: compatible with all GPL versions).
Evidence for the offensive filter: 20 EN list words within the top 3000 of the
frequency data (ranks 277, 291, 329, ...), DE 9, RU 3.
RED->GREEN: offensiveWordsAreNeverSuggestedButStayTypable,
forgettingASuggestionRemovesItForGood, exactKeywordMapsAWordToItsEmoji,
emojiSuggestionFollowsAnExactWordAndInsertsAfterIt,
longPressAlternatesFollowGboardPositions (compile-RED).
The UI test gboardLongPressPickerNumberRowHintsAndForget was written after the
QML (not observed RED); its assertions are specific (commit "è", hint "1"
disappears, panel height grows, commit "7", no commit on suggestion forget).
A scripted edit aborted on a stale line number (no file written); redone with
content-based positioning.
Sandbox: voice ON 12/12, voice OFF 11/11, KWin 3/3, static verify OK.

## 2026-10-03 — Neighbour-key correction (LatinIME ProximityInfo idea)

Found uncommitted work in the tree (13:22–13:27, not visible in my current
context; most likely an interrupted earlier turn): LatinIME-style normalized
scoring, a re-scaled frequency format and proximity. Its own tests passed, but
the real-data bench regressed badly (sch -> ich/ach/sich, ha -> ja/da/hi,
дя -> до/де/ця, привте -> приюте, wiel -> will). Preserved untouched on branch
wip/latinime-scoring; not shipped.
Re-implemented on the verified 0.5.0 ranking: key centres from the layout
rows; a substitution by a neighbouring key (<= 1.5 key widths) gets the same
bonus as a repeated letter. The two spec tests from the WIP were kept
(neighbouringKeysMakeTheCheapestCorrection, latinImeThresholdGatesAutocorrection).
Acceptance bench (real dictionaries + frequency lists): no regression vs 0.5.0;
new fixes tge/thw/yhe->the, hsus->haus, nivht->nicht, пртвет->привет,
сппсибо->спасибо. Behaviour change: "helo" is no longer auto-corrected (hello
and help are now equally likely: o neighbours p); both are suggested.

## 2026-10-03 — 0.6.0 more Gboard logic (user: "logic and functions from Gboard")

Declined decompiling the Gboard APK (proprietary; terms forbid reverse
engineering; its code/data cannot enter a GPL project). Used Gboard's public
feature descriptions and AOSP LatinIME (Apache-2.0) ideas instead.
RED->GREEN: stripShowsTypedWordAndWhatSpaceWillInsert, fieldTypeDrivesLayoutLikeGboard,
textShortcutsExpandFromTheStrip, recentEmojisComeFirstAndPersist (compile-RED);
UI tests stripQuotesTypedWordAndBoldsTheCorrection, numberFieldsShowANumpadEmailFieldsAnAt,
gesture-trail assertions in the glide test (written with the QML, not observed RED).
Process: one test edit referenced a non-existent fake counter; the "15 passed"
seen at that moment was a stale binary — re-run after the fix: 16/16.
Sandbox: voice ON 12/12, voice OFF 11/11, KWin 3/3, static verify OK.

## 2026-10-03 — 0.7.0 Gboard gestures and shortcuts

Sources: Computerworld (fast symbols from ?123, Shift slide, emoji fast-access
row), 9to5Google (auto-space after punctuation off by default; apostrophe
auto-switch on by default since 16.7), HelpDeskGeek/Hongkiat (16 period marks).
RED->GREEN: autoSpaceAfterPunctuationIsOptInLikeGboard (compile-RED),
apostropheOnSymbolsReturnsToLetters (a first version asserted period choices
while the symbols layer was active — test bug, fixed). UI test
gboardSlideGesturesAndEmojiRow written with the QML; review found its emoji-row
assertion vacuous (insertEmoji had already committed the glyph) — fixed to
assert the click's own commit. A QML property typo (glyphValue vs glideValue)
was caught by reading the diff before running.
Sandbox: voice ON 12/12, voice OFF 11/11, KWin 3/3, static verify OK.

## 2026-10-03 — 1.0.0 (user: "push to 1.0, then test")

1.0 = ready for daily use rather than new features:
- Themes: added System/Light (Gboard-like Material palette) next to Dark and
  AMOLED; ~10 hard-coded colours moved onto the palette; pre-1.0 "amoled"
  setting migrated. System follows QStyleHints::colorScheme (Qt >= 6.5; Qt 6.4
  falls back to dark).
- Found: the repository had no license text despite SPDX headers; added the
  official GPL-3.0-or-later text from SPDX's license-list-data (COPYING).
- About block in Settings; docs + attributions installed; Release build.
- v3kbd-rollback / v3kbd-uninstall [--purge], exercised against a fake home.
- Settings panel was checked for overflow: it already scrolls (Flickable).
RED->GREEN: themesFollowGboardIncludingSystem (compile-RED), palette/about UI
test. A scripted QML edit aborted on a wrong occurrence count (no file
written); redone with occurrence-agnostic replacement scoped to the body.
Process note: the 1.0 commit was made before the final full run; the run afterwards (Release, voice ON 12/12, OFF 11/11, KWin 3/3, static OK) confirmed it.

## 2026-10-03 — 1.0.1 hardening (user: "continue" before the 1.0 live test)

No new features; verification that does not need the tablet:
- Randomised stress test (240 sequences x 250 ops, immediate and composition
  modes, none/fresh/one-step-stale echoes): field text must equal typed text.
  Found: in composition mode "word Space ," dropped the space ("word,") and the
  next word glued on. LatinIME/Gboard swap it ("word, "). Fixed (swap only for
  a space typed right after a word; the next Space is absorbed). A unit test
  from 0.2.7 encoded the old drop and was corrected.
- ASan+UBSan: 13/13 suites clean (incl. a real whisper.cpp run); KWin layer
  clean. TSan: only Qt-internal QWaitCondition reports at process exit
  (uninstrumented libQt6Core), no frames in our code.
- Latency through the full bridge on real dictionaries (1 vCPU sandbox):
  max per keystroke EN 14.5 / DE 19.5 / RU 55.9 / UK 46.2 ms -> 2.5 / 3.7 /
  4.4 / 4.3 ms. Root cause (located by slowest-keystroke context, after two
  refuted hypotheses: disk flush, background-load contention): since 0.6.0 the
  per-keystroke autocorrect target enumerated ~700 variants through Hunspell
  for long unknown words (expensive for invalid words, 5630 SFX rules in uk).
  Fix: per-keystroke preview checks variants against frequency/stems/learned
  /core only; Space keeps Hunspell, with likelihood-ordered variants and a
  250-call budget (capital retry only for German). Space on long unknown
  words: UK 17 -> 3.7, RU 8.9 -> 3.0, EN 7.3 -> 2.6, DE 122 -> 23 ms.
  Real-data quality probes unchanged. Benchmark: tools/bench_keystroke.cpp
  (-DV3KBD_BENCHMARKS=ON).

## 2026-10-03 — 1.1.0 personal dictionary (user: "proper logic for adding custom words")

Problem in the existing logic: any word committed once counted as learned and
was then treated as valid forever (never corrected, offered as a suggestion),
so a single accidental typo was legitimised.
New logic (LatinIME/Gboard model):
- explicit personal dictionary per language (case kept, always valid,
  suggested with a high prior, persisted in settings);
- implicit learning promotes an unknown word only after 3 uses; known words
  are still learned for ranking from the first use;
- undoing an autocorrection promotes the word (explicit signal);
- strip shows an unknown typed word in quotes; tapping it keeps it and offers
  "+ Add to dictionary" (LatinIME "touch again to save");
- ~/.config/v3-keyboard/dictionary.txt adds words for all languages,
  re-read on every field focus;
- forgetting (long-press a suggestion) also removes the explicit entry.
RED->GREEN: oneAccidentalCommitDoesNotLegitimiseATypo, personalDictionaryKeepsCaseAndPersists,
dictionaryFileAddsWordsForAllLanguages, forgettingAlsoRemovesFromThePersonalDictionary,
acceptedWordsBeatTheCuratedTypoList (replaces learnedWordsBeatTheCuratedTypoList,
which encoded the old "one commit protects" rule), engine test
backspaceRightAfterAutocorrectRevertsAndRemembers went RED under the new rule
and GREEN once undo promotes; bridge tappingAnUnknownTypedWordOffersToSaveIt;
UI savingAndRemovingPersonalWordsThroughTheUi (first failure was the test
clicking a chip outside the scrolled settings viewport).

## 2026-10-03 — 1.1.1 first live test of 1.1.0 (Firefox, claude.ai)

Reports and findings:
- Top-row key previews were cut off (screenshot: a sliver above "г"):
  keyboardRows had z 0 under the toolbar (z 120). Raised to 130.
- Suggestions ignored the typed case ("Клево" -> suggestion "клево" would
  lowercase the sentence start). Now follow initial capital / ALL CAPS.
- "teh -> the doesn't work": user data from <= 1.0.1 counted every commit
  (typos typed many times in tests) and now crossed the promotion threshold;
  also the 1.1.0 checklist itself asked to undo the correction (an explicit
  keep). One-time migration (learning schema 2): legacy counts capped below
  the threshold, curated typos dropped.
- "word registration does not work": the flow passes in both immediate and
  composition mode tests; known words (e.g. "клево") intentionally get no
  "+ Add" chip. Asked the user for the word.
- "first letters uppercase / second letter replaces the first" in Firefox:
  not reproducible from code alone; two candidate mechanisms around the empty
  composer re-render. Requested a trace (no fix without evidence).
- "•••" in the strip is the existing toolbar toggle, not a bug.
RED->GREEN: suggestionsFollowTheCaseOfTheTypedWord, legacyLearningDoesNotPromoteOldTypos,
topRowKeyPreviewIsDrawnAboveTheToolbar; tappingSuggestionReplacesTheTypedWord
updated to the case-following strip ("Hello").

## 2026-10-03 — 1.1.2 second live report

- "Holding Backspace deletes only 3 characters": confirmed in code — the long
  press called backspaceRepeated(3) once; there was never an auto-repeat.
  Added a Gboard-like repeat (75 ms, faster after 12 ticks), stopped on
  release, cancel or swipe-delete. UI test: >= 6 deletions while held, none
  after release.
- "After clearing the field the capital does not come back": confirmed by a
  RED engine test — sentence start was only recomputed on adopted echoes.
  Now re-armed after deletions from the known text before the cursor (only
  for clients that report their text).
- "Focus leaves the text field when pressing the keyboard": hypothesis that
  view.requestActivate() steals focus was REFUTED from sources (QtWayland
  forwards to the shell surface, a no-op for our panel; KWin input panels
  never accept focus). Cause unknown — evidence requested.
- "First letter capital, then the second replaces it": still no trace.
  Requested an A/B (composition off) and a plain-field comparison.

## 2026-10-04 — 1.1.3 third live report (build on device: 15/15)

- Typing in Firefox's address bar works (capitals, no lost first letter):
  the "second letter replaces the first" report is specific to claude.ai's
  editor; the composition A/B is still pending (the user could not find the
  toggle: 14th of 19 settings, no scroll affordance -> scroll indicator added).
- Top-row key preview "still broken": the 1.1.1 fix (z-order) addressed only
  part of it and its test checked z, not pixels. Rendering the real QML
  offscreen showed the bubble touching the window top (0.15 px at 1600x560):
  Wayland cannot draw above the panel surface. The preview and the long-press
  picker are now clamped inside (>= 2 px). New geometric test over three panel
  sizes; shown RED on the 1.1.2 geometry (after a first version of the test
  that could not fail was caught and fixed).
- Double-space period now follows AOSP LatinIME exactly (read from source:
  InputLogic.tryPerformDoubleSpacePeriod, config_double_space_period_timeout
  = 1100 ms, canBeFollowedByDoubleSpacePeriod, revertDoubleSpacePeriod):
  second Space within 1100 ms, only after a letter/digit/'"')]}>+%/symbol;
  Backspace right after it turns ". " into " " without a capital next.
  ". " is held in the preedit so the revert needs no deletion in Firefox.
  Three tests encoding the old commit timing were updated; two RED tests
  exposed a real bug in my first implementation (countdown not started on
  every Space path) — fixed by recording every Space press, as LatinIME does.
- Process: a scripted edit wrote the header before failing on the .cpp
  (half-applied change, caught by git diff); scripts now validate everything
  before writing any file.

## 2026-10-04 — 1.2.0 touch-aware correction (user handed GBOARD_CLAUDE_HANDOFF.md)

The user supplied third-party notes about Gboard 18.4.1's architecture
(component and feature names from the APK). Used only at the architecture
level, clean-room, as the notes themselves recommend; I did not decompile
anything and no Google code or data is in the project. The notes describe
Beta 0.2.1 symptoms that were fixed in 0.2.3–1.0.1. Mapping to 1.1.3: composing
state, literal candidate, separate correction gate, short-token guard,
suggestion taps through the engine, undo-on-Backspace memory, user n-grams and
field policies already exist; async decoding is unnecessary at <= 4.4 ms per
keystroke. Missing and implemented now: the touch point inside the key as a
correction signal (also how AOSP LatinIME scores proximity): a neighbouring-key
slip gets 0..200 instead of a flat 100 depending on whether the finger landed
on the edge facing that key. QML passes the touch-down point; the engine keeps
one offset per letter of the current word.
RED->GREEN: wherTheFingerLandedDecidesBetweenNeighbours (lexicon),
touchOffsetsTravelWithTheWord (engine), touchPointInsideTheKeyReachesCorrection
(UI; shown to fail without the QML wiring: "bst,bat,bet"). The first UI test
version wrongly expected the correction at position 0 — the literal is first by
design since 1.1.0. Real-data bench unchanged. Not done: typing-speed signal.

## 2026-10-04 — 1.2.1 web research on the two open Firefox/claude.ai bugs

Sources (user asked to search the web):
- ProseMirror changelog: composition regressions "when starting composition on
  an empty line"; Yjs forum: IME typing in an empty ProseMirror paragraph "ends
  up in the first character", intermittent, fine once the paragraph has text;
  the empty paragraph carries a placeholder (is-empty) that is re-rendered on
  the first input. claude.ai's composer is such an editor; our 0.2.5 trace
  showed its empty composer reporting a 1-byte placeholder.
- ubports morph-browser #468: on rich-editor sites the on-screen keyboard
  disappears/"bounces" (text input briefly disabled and re-enabled).
Changes (mitigations; still to be confirmed on the device):
- A word started in an empty paragraph (model empty or ending in "\n") commits
  its first letter before composing the rest; suggestions/corrections keep that
  letter. Unit test + stress test with new paragraphs and stale echoes (all
  variants green). Composition tests now run mid-text ("Hi. ") by default; the
  claude.ai placeholder regression test now asserts the visible text.
- The panel hides only after a 250 ms grace time and stays if the field is
  re-activated meanwhile (PanelVisibility, own test, trace category
  v3keyboard.panel). Dropped view.requestActivate() (no-op for panels).

## 2026-10-04 — 1.3.0 more Gboard/LatinIME logic (user: "take as much as possible")

- Sentence capitalisation is now a port of AOSP LatinIME
  CapsModeUtils.getCapsMode (Apache-2.0, read from the LineageOS mirror):
  abbreviations (e.g., U.S., т.е.), American-typography closing quotes,
  opening punctuation, paragraph starts, German rules (dates "3. ", no capital
  on a line after a comma). Used whenever the client reports its text; other
  clients keep the event-based rule. Unit test with LatinIME's own vectors
  (CapsModeUtilsTests: en, fr-style, de) + engine test shown to fail without
  the wiring ("U.S. ").
- Gboard's hide button (⌄) at the right of the toolbar: commits the word being
  composed, then asks KWin over D-Bus (org.kde.KWin /VirtualKeyboard
  org.kde.kwin.VirtualKeyboard.active = false — verified in KWin's
  virtualkeyboard_dbus.{h,cpp}). Injectable KeyboardHider; bridge + UI tests;
  render checked (no overlap with the strip). Needs Qt6::DBus (qt6-base).

## 2026-10-05 — 1.4.0 more Gboard/LatinIME behaviour (user: "go on, test at the end")

- Backspace right after a punctuation swap restores the typed order (AOSP
  LatinIME revertSwapPunctuation: "word, " -> "word ,"). The swapped mark is
  held with the space in the preedit so no committed text is deleted (Firefox).
  A confirming Space ends the undo window — found by the stress test (it
  disagreed with the reference on Space-then-Backspace), decided in favour of
  "Backspace deletes the space the user confirmed"; reference updated.
- Re-correction (Gboard): the last 20 autocorrections are remembered; when the
  cursor returns to the end of such a word (KWin reset + echo), the typed
  original is offered first; choosing it restores and promotes it. The first
  test version omitted KWin's reset on a cursor tap (echoes are ignored while
  a preedit lives) — fixed in the test, then the real RED was observed.
- Settings: "Gesture trail" (on) and "Sound on keypress" (off; Qt Multimedia
  only, QSoundEffect created lazily; click generated by tools/make-click-wav.py,
  own work). The first trail-off UI check was vacuous (second glide in German
  never happened) — rewritten to require the glide's commit.

## 2026-10-05 — version reset to 0.5.0 (user: "the keyboard is at most half done")

Renumbered before anything is published: builds 0.5.0–1.4.0 -> internal
0.4.1–0.4.13 (git tags moved accordingly), current state = 0.5.0. Deploy uses
a new worktree name (release-0.5.0) so it can never collide with an old v0.5.0
worktree on the device. docs/TEST-1.0.md -> docs/TEST-PLAN.md.

Also in 0.5.0:
- Clipboard history as on Gboard: unpinned items expire after one hour and live
  in memory only; pinned items (long-press) persist. Previously the whole
  history (including copied passwords) was written to disk indefinitely; the
  old key is removed on load.
- Found while testing the pin UI: panels were transparent to touch — a tap on
  a non-interactive part of an open panel reached the key underneath (the
  first test run typed "&" through the clipboard panel). Panels now swallow
  presses. The check taps the panel header over a key; shown RED with the
  blocker disabled ("G" typed) and GREEN with it. A first version of that
  check (bottom-right corner, no key underneath) could not fail and was
  replaced.

## 2026-10-05 — 0.5.1 settings like Gboard

- Settings grouped into Gboard-style sections (Languages, Preferences, Theme,
  Text correction, Glide typing, Clipboard, Dictionary, Advanced, About). The
  regrouping script mapped every existing block by its label and refused to
  write if any block was unclassified or missing. UI test checks the section
  order and that representative settings sit under the right section; the
  personal-dictionary UI test (scrolls to the section) still passes; render
  checked.
- Languages: choose which languages the globe and the language panel offer
  (Gboard "Languages"); at least one stays on; switching off the current one
  moves to the next enabled one; persisted. A first test expected the wrong
  order (the layout order is en, de, uk, ru) — debugged with state output.

## 2026-10-05 — 0.5.2 split keyboard (Gboard on tablets)

Settings → Keyboard layout: Full width → Split → Compact left → Compact right.
Split moves the two halves apart (gap 28 % of the width) for thumb typing on
the 14" tablet; keys are shifted with a Translate (input follows the
transform), the bottom row instead widens the space bar across the gap so both
thumbs reach it; number pads stay whole. RED->GREEN UI test (t|y gap, asdfg|hjkl,
space spans the gap, a tap on a shifted key types it, number pad unsplit).
The first version kept the centre key of odd rows ("g") in place (it formally
spans the centre); fixed by exempting only wide keys — found from geometry
debug output; render checked twice (space bar first sat inside the gap).

## 2026-10-05 — 0.5.3 options for the word under the cursor (Gboard)

Tapping into the middle of a word: the strip offers options for the whole word
(also the typed original of an autocorrected word); choosing one replaces the
whole word with one delete_surrounding_text (before = -index, after = index +
length, UTF-8 bytes) + commit, only on confirmed client text (otherwise
nothing happens, a stale echo must never cut a word). No auto-space when a
space or punctuation already follows. Autocorrect is off for such a word, and a
Space inside it splits it without correcting the left part — the first version
of that check used "te|h" and could not fail (words < 3 letters are never
corrected); rewritten as "teh|x", observed RED, then fixed.
Typing-speed signal deliberately not added: without data it would be a guess
that could suppress good corrections after a thinking pause.

## 2026-10-05 — 0.5.4 emoji skin tones (Gboard)

Found: with the system emoji-test.txt (Arch package unicode-emoji) the grid
listed every fully-qualified line, i.e. ~2900 skin-tone variants next to their
bases. Now tone variants are grouped under their base (key without tone
modifiers and FE0F, so "☝️" = 261D FE0F matches "☝🏻" = 261D 1F3FB);
mixed-tone sequences are left out; long-press an emoji for the picker (base +
5 tones), a tap outside closes it. Fixture: real lines from Unicode's official
emoji-test.txt 19.0 (unicode-org/unicodetools). Catalog + UI tests.

## 2026-10-05 — 0.6.0 batch (user: "work in big batches until fully featured")

- Text-editing panel: Up/Down added. Investigated Select / Select all / Copy /
  Cut from KWin's source (inputmethod.cpp keysymReceived -> forwardKeySym): the
  compositor replaces modifiers with exactly those needed for the keysym and
  resets them afterwards, so an input method cannot send Ctrl+C or Shift+Arrow
  on KWin. Not implementable here (documented in README); Paste already
  commits the clipboard text directly.
- Long-press delay setting (Gboard default 300 ms; was the 800 ms system
  default): 200/300/400/500/700 ms.
- Clipboard chip (Gboard): text copied in the last minute is offered in the
  idle strip; one tap pastes; typing dismisses it; not in secure fields.
- Wrong layout ("ghbdtn" -> "привет"): unknown words are read through the
  other-script layouts (key position mapping between our layouts; same-script
  pairs en/de, ru/uk are skipped) and checked against that language's bundled
  frequency list, loaded lazily on first use. Measured on the real lists: 37
  false readings among 6671 unknown EN prefixes (incl. an offensive word) ->
  0 after two rules: not while the letters are still a prefix of a word of the
  current language, and the other language's offensive words are removed.
  Choosing the offer switches the keyboard language.

Performance follow-up in the same batch (measured, not guessed):
- The lazy foreign-list load cost 116 ms once on the typing path -> moved to
  a background thread (QThreadPool; lookups never block; TSan clean).
- Two hypotheses about remaining slow keystrokes (Hunspell validity checks)
  were tested and did not explain the numbers; callgrind then showed 97 % of
  the suggestion work in correction-variant generation: isCore() was a linear
  QStringList scan per variant (since 0.2.x) and the variant set rehashed per
  keystroke (since 1.0.1, when only Space was re-measured). Fixed (hash set,
  reserve). Knownness is now computed once per keystroke, cheap-first.
- Sandbox wall-clock numbers on one vCPU are too noisy for sub-10 ms work; the
  deterministic count for the whole keystroke path (RU) is ~1 M instructions
  per keystroke (well under 1 ms on the tablet).
- Emoji suggestions scanned the whole catalog per strip read (on the device
  with the system emoji list, ~1900 entries); now an exact-keyword index built
  once per language.

## 2026-10-06 — 0.6.1 symbol pages (Gboard "?123" / "=\<")

The symbols layer had a single page (~27 symbols): no _ = < > [ ] { } \ | ~ ^,
no € £ ₴ ₽, no « » „ “, no dashes. Now two pages like Gboard: page 1 carries
the language's currency ($ / € / ₽ / ₴) and "_"; page 2 has programming and
typographic symbols; long-press extras on symbol keys (" -> « » „ “ ”,
- -> – —, currency -> other currencies, digits -> fractions/superscripts,
< > ( ) = …). The page key replaces Shift on the symbols layer. Render check
showed the third row wider than the others (its width formula assumed no key
on the left); fixed and covered by a row-width assertion in the UI test.

## 2026-10-06 — 0.6.2 Tastra: name, GitHub, one-command install, memory

The cloud workspace was reset before this release; the project was restored
from the published git bundle (main + all tags at 0.6.1, verified) and the
lost, uncommitted memory work was redone and re-measured.

- Name: Tastra. Checked for clashes by web search; "Klavo" was dropped as too
  close to Klavaro (an existing Linux typing tutor). Mechanical rename of
  namespaces, targets, resource prefix, log categories, scripts and paths;
  history documents keep the old name. A static check now fails on the old
  name outside the migration and cleanup code.
- Migration (src/core/legacymigration.cpp, 5 tests, written RED first): moves
  ~/.config, ~/.local/share and ~/.local/state "v3-keyboard" folders, merges
  into folders that already exist without overwriting, copies the QSettings
  file (old organisation/application name) to ~/.config/tastra/tastra.conf,
  and leaves links behind for a rolled-back binary. Runs in main() before
  anything reads settings.
- KWin stores the chosen keyboard as the path of its .desktop file
  (kwinrc [Wayland] InputMethod; confirmed in a KDE Discuss thread quoting
  kwinrc). The installer rewrites that key when it names the old file and
  keeps `v3-keyboard` as a link to the new binary until the next login.
- scripts/tastra-install.sh (installs only after static check, fresh Release
  build and full ctest) and tastra-update (fast-forward from GitHub, then the
  new installer). End-to-end run in the sandbox on a fake home holding a
  0.6.1 installation: folders moved and linked, settings copied, old helpers
  and .desktop removed, kwinrc rewritten, previous binary backed up,
  tastra-rollback restores it.
- Memory (tools/bench_memory, each language in a fresh process, system
  Hunspell dictionaries): resident after loading en 31.1 -> 23.9 MB,
  de 30.8 -> 23.7, ru 46.8 -> 35.2, uk 80.4 -> 60.8 with malloc_trim(0) after
  a load. The trim runs in the loader thread (measured 13 ms for uk; would
  have been on the typing thread). With the trim, the foreign word list for
  wrong-layout detection showed its real cost (+4.4 MB as QSet<QString>);
  now sorted 64-bit FNV-1a hashes: +1.1 MB. Totals with that list:
  en 31.9 -> 25.0, ru 47.9 -> 36.3, uk 81.2 -> 62.1 MB.
- GitHub: main pushed to M0npet/Tastra; GitHub Actions (Arch, Qt 6.11) green
  on 0.6.1. At the user's request every commit is authored by him (GitHub
  noreply address, so no private e-mail in the public history; Claude as
  Co-Authored-By); history rewritten with filter-branch, all 32 trees checked
  identical, then force-pushed (the repository was hours old). Pushing tags is refused by this workspace's git proxy (HTTP 403),
  so versions are marked in commit subjects and CHANGELOG.

### 0.6.2 — LatinIME error model (apostrophes, accents, length-aware confidence)

Found by probing autocorrect on the real system dictionaries and bundled
lists (a probe binary linked without the qrc lists first gave misleading
results; rerun with the lists linked in):
- "dont", "isnt", "thats", "youre", "ive" were left alone or turned into the
  wrong word ("inst", "that", "your", "vie"); "пять", "мясо", "звязок" stayed;
  "uber" became "aber"; "щас" became "вас"; "naturlich", "пожалуйсто",
  "обьект" were only suggested.
- Root causes: (1) FrequencyWords splits tokens at apostrophes, so the lists
  had no "don't"/"п'ять" and ranked fragments ("don" 28th, "isn", "ясо");
  (2) the error model had no notion of an omitted apostrophe or a missing
  accent; (3) confidence needed rank <= 3000 regardless of word length, and
  a core word made any short substitution confident.
- Data: tools/merge-apostrophe-words.py takes the apostrophe words from
  wordfreq (CC BY-SA 4.0, SUBTLEX credited in ATTRIBUTION.md) and places them
  by wordfreq rank, mapped to list positions by counting (a running-maximum
  mapping was tried first and rejected: one outlier drags everything to the
  front). Fragments are moved only when their apostrophe forms are used more
  than the word itself (a "twice as high" rule was tried first and moved
  ordinary words such as "oh" and "father"). en +4100 words, 35 fragments
  moved; uk +1118, 92 moved; pure fragments ("isn", "ясо") dropped.
- Model (LatinIME): apostrophe insertion = intentional omission (+260,
  confident when the word is known); base letter for accented letter, German
  digraphs ue/oe/ae and ss -> ß, up to two steps (+220, confident up to rank
  20000); a same-length substitution in words of <= 4 letters is never
  confident unless the user typed the word; one edit in words of >= 6 letters
  is confident up to rank 20000 with a clear margin; corrections return the
  dictionary's spelling (capital for "I'm", German nouns); English "i" -> "I"
  (AOSP: "i" is not_a_word with the shortcut "I").
- Tests written first (RED): 5 lexicon tests on new en/de/uk/ru fixtures. The
  first Ukrainian run passed vacuously (tiny fixture lists made every word
  top-5); fixtures now expand "~N" into N non-word lines so words get their
  real ranks, and the test went RED as on real data. The "i" test also passed
  vacuously until the fixture got a lowercase "i" like the real en_US.
  Engine: a correction equal to what was typed ("I" -> "I") no longer
  deletes and re-types the word on clients without composition (RED: events
  showed del:I commit:I).
- Real-data probe after the change: dont->don't, im->I'm, youre->you're,
  isnt->isn't, thats->that's, didnt->didn't, ive->I've, i->I; uber->über,
  grosse->große, naturlich->natürlich, strasse->Straße; обьект->объект,
  пожалуйсто->пожалуйста, щас unchanged; пять->п'ять, мясо->м'ясо,
  звязок->зв'язок, обєкт->об'єкт. cant/wont/its/ill stay, with the
  contraction in the strip.
- Cost (callgrind, typing thread only, dictionary loaded explicitly; a first
  attempt counted the background loader thread and was discarded): lexicon
  work per keystroke de 2.36 -> 2.60 M instructions, ru 2.37 -> 2.46 M,
  uk 2.12 -> 2.38 M; full bridge path en 108 -> 110 k.
- Sanitizers on the final tree: ASan+UBSan full suite 18/18 clean. TSan
  (lexicon, smarttyping, typingstress, keyboarduibridge, legacymigration)
  first failed on reports that were all inside uninstrumented libraries
  (QWaitCondition in QThread, the QTest watchdog, QSoundEffect -> glib
  eventfd), none with a project frame; with tools/tsan-qt.supp (those
  libraries only) 5/5 clean.
- The user renamed the repository to M0npet/Tastra-Keyboard after 0.6.2 was
  pushed; README, tastra-update and CHANGELOG point there (GitHub redirects
  the old name). GitHub Actions on 0.6.2 (039033a): green.

## 2026-10-06 — 0.6.3 multilingual typing (Gboard: several languages at once)

Real-data probe with en+de and uk+ru enabled showed same-script words of the
other language being "corrected": danke->dance, bitte->bite, nicht->night,
ist->sit, wie->we (en active); what->hat, yes->es, nice->nie (de active);
привет->привіт, спасибо->спасибі, тоже->отже (uk active). Gboard types
several enabled languages without switching. Now the other enabled languages
of the same script are "companions": their bundled word lists (hashes, loaded
in the background as for wrong-layout detection) make a typed word known, so
it is neither corrected nor offered for the personal dictionary. Different
scripts keep wrong-layout detection. RED->GREEN: bridge
otherEnabledLanguagesOfTheSameScriptAreCompanions, engine
aWordOfAnotherEnabledLanguageIsKnownAndKept, lexicon tests; then
aNearlyFreeFixInTheActiveLanguageBeatsAnotherLanguagesWord (RED: "пять" is a
Russian word, but on the Ukrainian layout the left-out apostrophe is the
likelier story; apostrophe/accent edits now still win). Probe after: all the
words above kept; teh->the, dont->don't, nciht->nicht, uber->über,
пять->п'ять still corrected.

The session hit its usage limit right after this; the work was resumed from
the session log (uncommitted tree intact, full suite green).

## 2026-10-06 — 0.6.3 emoticon tab (Gboard ":-)")

Gboard's emoji keyboard has a ":-)" tab of text emoticons; ours had only
emoji categories. Added 58 faces (own selection of generic short faces:
classic ASCII first, then kaomoji) as the last category chip. Tiles are a
third (portrait) or fifth (landscape) of the width, and long faces shrink to
fit (Text.HorizontalFit). Faces are inserted as typed text and are not added
to the recent emoji. RED->GREEN: bridge emoticonsAreATabOfTextFacesLikeGboard
(category last, required faces, no duplicates, exact insert, recent
untouched); UI emoticonTabShowsWideTilesAndInsertsTheFace (chip by
objectName, tile >= 100 px wide where an emoji tile is 58, tap commits). The
first UI version tapped the shrug, which is item 38 and below the visible
grid (click landed outside the window); the test now taps ";-)" in the first
row. Rendered the tab and all 58 faces offscreen: every face draws with the
Noto fonts; the few with Katakana need a CJK font on the device.

## 2026-10-06 — 0.6.3 release checks; glide decoder weakness found

ASan/UBSan: 17/18 clean; qmlkeyboard's singleFingerGlideAndLongPressStillWork
failed 4/8 runs alone under ASan (1/8 with companion loading disabled, so the
extra background load only made a pre-existing timing effect more likely).
Traced: QML side identical in good and bad runs; the key sequence differed
(h g r e t j l o -> "hello"; h g r e r t j k l o -> nothing). The decoder
scores Levenshtein distance to the raw key sequence, so every key the finger
merely crosses costs as much as a wrong letter, and "hello" fell below the
threshold. A real finger crosses more keys than this straight-line test, so
glide typing would often fail on the device. TSan: smarttyping, lexicon and
bridge clean (0 races). Next (0.6.4): a geometric glide decoder (path vs. the
word's ideal path through key centres, as in SHARK2 / LatinIME's gesture
recogniser) instead of key-sequence edit distance.

## 2026-10-07 — 0.6.4 glide typing from the finger's path (SHARK2 / LatinIME)

Root cause from the 0.6.3 ASan finding: the decoder took the sequence of keys
under the finger and scored Levenshtein distance to each word, so every key
merely crossed counted as an error. Measured on synthetic glides (finger path
through the word's keys, corners up to 0.25/0.45 key widths off centre,
seeded): the old decoder got 7/200 (en), 11/200 (ru), 6/200 (de), 8/200 (uk)
of the 200 most frequent words.

New decoder (src/core/glidegeometry.cpp, LocalLexicon::
decodeGlidePathCandidates): QML sends the whole path and the centres of the
letter keys; candidates start and end at keys near the touch-down and
lift-off points (frequency list, core, personal and user words); a word is
dropped if one of its keys stays more than a key width from the path; the
rest are scored on SHARK2's location channel (32 evenly resampled points, in
key widths), its shape channel (centroid and scale normalised), the mean
distance of the word's keys to the path, plus 0.6 x the frequency/personal
prior. Grid search over the weights (L 300-600, K 200-450, S 150-400,
prior 0.25-1.0) on 4 x 200 words x 2 noise levels: a flat plateau
1527-1530/1600; kept the first setting. Result: en 194/190, ru 198/195,
de 192/187, uk 187/183 (jitter 0.25/0.45). What is left is mostly paths
that are the same for two words (too/to, dass/das, всё/все, them/then) and,
for uk, Russian words from the subtitle list that are not Ukrainian (all
"valid: 0" in Hunspell uk; the uk layout has no ы/э). For those Gboard shows
the other readings: the engine now keeps the next 3 readings (within 220
points of the best) and shows them in the strip right after the glide;
choosing one deletes the glided word on the text channel (Backspace key
events would arrive after the commit in Firefox) and takes back its
learning. Any input, a client reset or an adopted external edit drops them.
Letters without a key (ё, ß) are glided over their base key.

Tests: tst_glide (8: resampling, ideal path, crossing keys, sloppy start and
end, 40 en words at 0.12 and 0.32 jitter >= 95 % / 85 %, 16 ru words, empty
input) RED -> GREEN; engine glideOffersTheOtherReadingsAndReplacesTheWord
(RED: key-fallback "bs bs" until the deletion used the text channel) and
glideAlternativeAlsoReplacesWithoutAPreedit. The ASan glide UI test that
failed 4/8 now passes 10/10. Cost: 1.6-6 ms per glide in the 1-vCPU sandbox
(once per word, on release).

## 2026-10-07 — 0.6.5 glide polish, next-word setting

- Gboard after a glide: "tap Backspace once. That'll erase the entire word"
  (Computerworld, Gboard shortcuts by JR Raphael). Implemented for path and
  key-sequence glides: deletes the word (and its committed space, or drops the
  held one) on the text channel, takes back its learning, restores the
  previous word; the next Backspace is ordinary. RED->GREEN
  oneBackspaceErasesTheWholeGlidedWord (with and without preedit).
- Shift / Caps Lock before a glide: capitalized / all caps (the case rule
  for tapped letters; the one-shot Shift is used up). RED->GREEN engine
  shiftAndCapsLockApplyToTheGlidedWord, bridge
  shiftBeforeAGlideCapitalizesTheWord.
- Next-word suggestions setting (Gboard Text correction). RED->GREEN engine
  nextWordSuggestionsCanBeTurnedOff, persistence in the bridge test.

## 2026-10-07 — startup time measured (no change made)

Offscreen, 1-vCPU sandbox, from process start: QGuiApplication 1 ms, bridge
1-2 ms, Main.qml loaded ~300 ms, first frame ~320 ms. Bisected: without the
panel surface the first frame comes at ~130 ms; of the ~190 ms, settings
~110, clipboard ~45, emoji ~45, text editing ~30. Wrapping each panel in an
inactive Loader with an inline Component saved only ~25 ms: the cost is
compiling the definitions, not creating the hidden objects (the QML disk
cache made no difference either). Moving the panels into separate .qml files
(loaded on first open) would save it, but needs Key and PanelButton moved out
of Main.qml too; on the tablet's CPU that is roughly 60 ms once per session,
so it was left for later. Resident memory at the first frame ~75 MB with the
English dictionary loading in the background.

## 2026-10-07 — 0.6.6 emoji search on the keyboard's own letters

The emoji panel's search was a QML TextInput ("physical keyboard or category
chips"). KWin never gives an input panel keyboard focus (verified earlier in
KWin's inputmethod.cpp), so on the tablet it could not be typed into at all.
Gboard: tapping search brings the letters back and types the query. Now the
bridge has an emoji search mode: letters, alternates, symbols, Space and
Backspace edit the query instead of reaching the app; glides, cursor drags
and Delete are ignored; Enter, ✕, choosing an emoji, opening a panel, paste,
hiding the keyboard or a new input context end it; the keys show lowercase.
The toolbar shows ✕, the query and the matching emoji (recent or smileys
while the query is empty). RED->GREEN: bridge
emojiSearchIsTypedOnTheKeyboardItself, UI
emojiSearchTypesOnTheLettersAndShowsResults (tap the field, type p-i-z-z-a on
the keys, tap 🍕 in the bar). Rendered: bar, query and results above
lowercase keys. Static check now forbids a text field in the panel.

Also: a UI test for glides across the split keyboard's gap (real QML key
centres). It first failed only because the QML tests run without word lists
and "heart" is not a built-in core word; replaying the captured path through
the decoder gave heart, heat, hat, beat, ... — the test now loads the bundled
list.

## 2026-10-07 — 0.6.7 add word + shortcut in settings (Gboard dictionary)

Gboard's personal dictionary has "+": a word and an optional shortcut. Ours
needed the strip ("tap the typed word, then +") or editing dictionary.txt /
shortcuts.txt. Same mechanism as the emoji search: the keyboard types into a
bar above the keys (word step keeps Shift capitals and refuses spaces;
shortcut step is lowercase), Enter/✓ goes on, ✕ adds nothing; afterwards
Settings open again with the new word listed. The shortcut is written into
shortcuts.txt by rewriting only its own line (comments and other lines stay),
and removing one deletes only its line. RED->GREEN: bridge
wordsAndShortcutsAreAddedFromSettings (capitals, Backspace, space refused,
two steps, nothing reaches the app, file content, the shortcut works,
removal, cancel, empty word cannot be confirmed); UI
addWordFromSettingsTypedOnTheKeys. Rendered the bar ("New word | Oldenburg
| ✓").

## 2026-10-07 — panels in their own QML files: start 365 -> 172 ms

Follow-up to the startup measurement: the panels (settings, emoji,
clipboard, text editing) and PanelButton moved from Main.qml into their own
files, loaded by Loader { source } only while open. Sandbox, offscreen, 4
runs each: first frame 344-439 -> 160-172 ms; resident memory 3 s after
start 78-82 -> 72.4 MB. Cost: the first opening of a panel compiles it
(settings 52 ms, emoji 26 ms, clipboard 8 ms; later openings 1-8 ms).
Checked: the panels load from the application's qrc resources (all four
Loaders Ready, sized), renders of the emoji, settings, text-editing and
clipboard panels before and after are pixel-identical, full suite green.
The static check now reads all QML files in src/ui and balances braces per
file.

## 2026-10-07 — 0.6.8 checks: keystroke frame cost, qmllint, CI QML warnings

- Keystroke cost on the QML side (offscreen, synthetic taps on the real
  keys, event + bindings + rendered frame): p50 1.3 ms, p90 2.6 ms, max
  13-18 ms (Space with the full Hunspell check), over 64 keys. Nothing to fix.
- qmllint 6.4 on all QML files: 465 "unqualified access" (root and
  keyboardBridge through the context, by design) and 25 others, all from
  the old linter (Timer and Image.sourceSize unresolved, GridView.view typed
  as ItemView, a JS array's concat); no real defect.
- The CI's Qt is the tablet's (6.11), but its logs cannot be fetched from
  here (they are served from blob storage the proxy refuses). CI now runs
  ctest verbosely and turns QML runtime warnings into annotations
  (scripts/ci-qml-warnings.py), which the repository API returns.


## 2026-10-07 — 0.6.9 autocorrect measured against seeded typos

Evaluation tools (sandbox only, not in the repository): 1500 frequent words
per language x 2 rounds with one seeded single-edit typo each (neighbouring
key, swap, missing letter, extra neighbouring letter; no touch data); the
same for wordfreq words outside the bundled list (3-15 % of what is typed,
weighted by that share); 3000 words typed with Gaussian tap noise around the
key centres (sigma 0.18 / 0.25 / 0.35 key widths, offsets passed on as from
the screen); 1500 random pairs of frequent words typed without the space or
with a letter above the space bar for it. Every run also checks that no
valid word (2000 frequent, ~700 rare) is changed: 0 throughout.

- A LatinIME-style extra-letter penalty was tried first and rejected: it
  moved errors between typo kinds without lowering them (right 80.1 ->
  77.4 %).
- Hunspell-only words now score 70 below listed ones (a rank beyond the
  list, as wordfreq ranks them): single-edit typos right 80.1 -> 82.3 %,
  wrong 4.5 -> 3.9 %; typos of unlisted words wrong 2.4 -> 3.1 %. RED->GREEN
  aWordMissingFromTheFrequencyListIsRare ("ohers" -> "hoers" before).
- The Ukrainian frequency list held 15 503 Russian words ("что" rank 8,
  "как" 34). tools/drop-foreign-words.py drops words the Russian Hunspell
  accepts and either the Ukrainian one rejects or wordfreq finds ten times
  more common in Russian, plus words with ы/э/ъ/ё. Checked by sampling every
  60th dropped word (all Russian; a few homographs such as "тем" lose only
  their rank, Hunspell still accepts them). The Russian list has 12 such
  words and the English list's German words are parts of English phrases
  ("de facto", "et cetera"), so only Ukrainian was cleaned. RED->GREEN
  bundledUkrainianListHasNoRussianWords.
- Several slipped keys (LatinIME proximity search): before, words with two
  or more keys off were never corrected (0 % of them). Trie walk by binary
  search over the sorted list; 2 slips from 4 letters, 3 from 6, 4 from 9;
  each extra slip costs 260 (grid 120-400, lengths 3-10). Tap noise sigma
  0.25: right 69.6 -> 93.1 %, wrong 0.80 -> 0.35 %; sigma 0.35: 36 -> 80 %;
  sigma 0.18: 93 -> 96 %. Single-edit typos: 82.4 -> 81.9 %, wrong 3.9 ->
  4.0 %. Keystroke benchmark against 0.6.8: p50/p95 within run-to-run noise.
- Words run together (LatinIME space omission / mistyped space): two words
  back on Space in 95/97 % (en), 60/64 % (de, compounds kept), 92/97 % (ru),
  89/93 % (uk), before 0; wrong 0.2-2.3 % (before 0.3-6.5 %: other
  corrections of the joined word). Single-edit typos 81.9 -> 81.7 %. The
  eval's mistyped-space cases that are ambiguous by construction ("thendark")
  are skipped. Engine: a two-word correction is learned as two words and the
  next word follows the second (RED: previous word was "and the").
  RED->GREEN wordsRunTogetherAreSplit (compound guard shown to fail without
  the rule), twoWordsRunTogetherAreSplitAndLearnedAsWords.

## 2026-10-07 — 0.7.0 interface languages; words the dictionary lacks

- Real-data probe of chat words (a few dozen per language typed as written)
  found "окей" -> "коей", "naja" -> "Anja", "tja" -> "ja", "honour" ->
  "honor", "omg" -> "mog". The frequency lists know these words but
  Hunspell does not, so they were dropped at load. The first 20 000 of them
  per list are now kept as hashes and handled like a word of another
  enabled language (never offered, only an apostrophe/accent fix applies).
  Threshold measured: 20 000 -> seeded typos right 81.72 -> 81.62 %, wrong
  4.00 -> 3.88 %; 50 000 left 0.4 % more typos uncorrected (the lists' tail
  holds subtitle typos). Unlisted 3-letter swaps are no longer confident
  (no cost on the evals). RED->GREEN listedWordsHunspellLacksAreKept.
  After the change the chat probe changes 0 of 73 English, 1 of 70 German
  ("joa"), 2 of 74 Russian ("кек", "ваще": neighbour-key slang) and 1 of 72
  Ukrainian words ("жесть").
- German nouns typed lowercase ("haus") are left as typed with "Haus"
  first in the strip; whether Gboard capitalizes them on Space could not be
  confirmed from a reliable source, so the behaviour was not changed.
- Interface languages: 78 texts in data/i18n/{de,ru,uk}.tsv, read by a
  QTranslator subclass (no Qt Linguist tools needed). The language follows
  QLocale::system().uiLanguages() (first of de/ru/uk/en), or the setting.
  verify-static checks completeness and placeholders both ways. Overflow
  check over all panels at 800 and 1280 px in four languages: no text wider
  than its space; settings rendered and inspected in German and Russian.
  The panel titles ("Settings", ...) were missed by the first text search
  and found on the render.
- Follow-up: the user checked Gboard (German) on a phone: "haus" is only
  offered as "Haus", Space does not change it. That is Tastra's behaviour;
  pinned by germanNounTypedLowercaseIsOfferedNotForced.

## 2026-10-07 — 0.7.1 clipboard through KWin data control; Copy / Cut

- Question checked from KWin's source (GitHub mirror of KDE/kwin, master of
  2026-10-07): src/wayland/seat.cpp offers the wl_data_device selection only
  to the devices of the client set by setFocusedDataDeviceSurface (the focused
  window) plus every data-control device; set_selection without keyboard
  focus is refused (also reported on kwin@kde.org, 2023-09). An input panel is
  never focused, so QClipboard in the keyboard saw nothing: clipboard history,
  the "just copied" chip and Paste could not have worked on the device (never
  tested there yet). src/wayland_server.cpp allowInterface() returns true for
  the input-method connection, so ext_data_control_manager_v1 (KWin since
  Plasma 6.4, commit 78c5e546 2025-04; wlr-data-control dropped 2025-05;
  marked restricted for sandboxed clients 2026-09) is available to Tastra.
- DataControlClipboard: plain libwayland proxies on Qt's display (Qt's event
  loop dispatches them), reading other applications' text asynchronously
  through a pipe and owning the clipboard for Copy/Cut; its own offers are
  recognised by a private MIME type so it never reads from itself. Fallback
  to QClipboard without the protocol. Tests: an in-process compositor
  (libwayland-server, tests/fakecompositor.h) with two clients (read,
  Unicode, 60 KB in several reads, own copy reaching another client, source
  cancelled, clear, image-only offer, no data control); and the same through
  Qt's Wayland platform plugin against that compositor (the keyboard's real
  path). First runs found a shared MIME marker between two instances (now
  per process and object) and a blocking test pump (now non-blocking).
- Copy / Cut: the selection comes with KWin's surrounding text (inputmethod.cpp
  sends cursor and anchor); Copy puts it on the clipboard, Cut also sends
  BackSpace (no modifier needed). Selecting from the keyboard stays
  impossible (Shift is replaced by KWin). Never in password fields. Bridge
  and UI tests.
- CI (Qt 6.11) aborted tst_datacontrolqtdisplay without output; the CI now
  annotates failed tests with their output (logs cannot be fetched from the
  sandbox). Reproduced with Qt 6.11.2 libraries from PySide6 6.11.2: Qt's
  Wayland platform refuses to start without a shell ("Loading shell
  integration failed"), unlike the sandbox's Qt 6.4. The test compositor now
  offers stub wl_compositor and xdg_wm_base globals. With the Qt 6.11.2
  libraries the keyboard's path (DataControlClipboard on Qt's display) reads
  another client's text and owns the clipboard.

## 2026-10-07 — 0.7.2 Select / Select all / Copy / Cut as real shortcuts

- From KWin's source (GitHub mirror of KDE/kwin): fakeinputbackend.cpp
  accepts every authenticate request ("TODO: make secure") and turns
  keyboard_key into ordinary keyboard input (modifiers kept, focused window);
  since 2024-07 restricted protocols are only hidden from sandboxed clients,
  and since 2026-07 everything is announced to the input method. The
  fake-input device is not a libinput device, so it does not end tablet mode
  (tabletmodemanager.cpp blocksTabletMode). inputmethod.cpp has no hiding on
  key input.
- FakeInputKeyChords (protocol XML vendored from plasma-wayland-protocols,
  LGPL-2.1-or-later): Ctrl+A, Ctrl+C, Ctrl+X, Shift+arrows/Home/End as evdev
  key presses. The bridge finishes the word being typed first, leaves Select
  mode on Cut and when the panel closes, never acts in password fields, and
  keeps the 0.7.1 path (selection from the surrounding text + data control)
  when fake input is missing.
- Tests: the test compositor records fake-input keys (order and
  authentication); bridge and UI tests. A render showed the panel's 13
  buttons running past the panel at 800 px (also true for the old 8 at that
  width): the grid now takes as many columns as fit.

## 2026-10-07 — 0.7.3 Bundled word pairs, human-like glides, Undo/Redo

- Glides: a generator of human-like paths (Gaussian corner scatter, corners
  cut toward the neighbours' midpoint by up to 0.6 key widths, Catmull-Rom
  curves; now `GlidePaths::humanPathFor` in tests/glidepaths.h), 600
  frequent and 600 rarer words per language, three styles (clean, cut,
  sloppy), weighted 0.7/0.3. A word was dropped when one of its keys lay
  more than a key width from the path; 1.5 and a letters weight of 180
  (was 260) gave top-1 86.0 -> 87.4 %, exact paths unchanged, sloppy
  77.5 -> 81.2 % (frequent) and 64.1 -> 68.1 % (rarer). Tried and dropped:
  a path-length channel, a turn-count channel, a stronger prior (0.8: +1.2
  on frequent words, -0.7 on rarer ones). tst_glide cornersCutAsRealFingersDo:
  40 English words tidy 92.5 -> 97.5 %, sloppy 75 -> 85 %, 16 Russian
  93.8 -> 100 %.
- Word pairs: LatinIME and Gboard ship a language model; Tastra had none
  because no GPL-compatible pair data was known. Licences were checked in
  each corpus' README/LICENSE: UD English EWT and ESLSpok, German GSD,
  Russian Taiga and GSD, Ukrainian ParlaMint are CC BY-SA 4.0, UA-GEC is
  CC BY 4.0; GUM, LinES, SynTagRus, Ukrainian IU and BRUK are NC, German
  HDT's text is for academic use only, the PUD treebanks and Google Books
  Ngrams are CC 3.0. tools/build-bigrams.py counts adjacent words of the
  train/dev parts (both in the bundled frequency list, within a sentence,
  across commas and dashes), pairs seen at least twice: en 20 849, de
  15 031, ru 84 464, uk 31 099.
- Measured on the held-out test parts (18 297 / 8 982 / 10 044 / 24 906
  word pairs): the next word is among three suggestions for 22.1 / 13.7 /
  15.4 / 16.6 % (none before for a new user). A prior of 30 per doubling of
  the pair count: glides (human-like, prev word given) 88.4 -> 89.7 %,
  autocorrect of noisy taps 76.5 -> 76.8 % right, 0.65 -> 0.72 % wrong,
  clean words never changed; 60 and 100 added wrong corrections. The chat
  probe is unchanged (0/1/2/1). Russian with pairs seen three times: -1.2
  points of next-word hits for -1.5 MB, so twice it stays.
- Memory (tools/bench_memory): +0.6 / +1.0 / +3.3 / +0.6 MB, load +~90 ms
  for Russian on the loader thread.
- A sentence end now clears the previous word (typed `.!?`, double-space
  period, Return, or the application's text across `.!?…` or a line
  break), as LatinIME's beginning-of-sentence context.

## 2026-10-07 — 0.7.4 Common Voice pairs, German capitals, key sounds

- Common Voice (github.com/common-voice/common-voice, server/data, CC0 per
  its LICENSE): sentence collector and contributors' sets per language.
  Held out every tenth line (and the UD/UA-GEC test parts as before). Next
  word among three, UD/UA-GEC test and Common Voice held-out:
  UD-only pairs en 22.1/16.5, de 13.7/9.9, ru 15.4/14.0, uk 16.6/10.2;
  with all of Common Voice except Wikipedia: 22.4/22.0, 16.8/16.3,
  15.8/22.5, 16.2/13.7 (de 211 248 pairs, uk 113 107); without German
  Europarl (3.7 M words of parliament) and Ukrainian ukrlib (older prose):
  de 15.3/17.2 with 40 263 pairs, uk 16.9/12.6 with 38 626. Taken without
  them. Capping German at 60 000 / 100 000 pairs of the larger set cost
  2.0 / 1.0 points.
- Glides with the previous word 89.7 -> 90.0 % (no context 88.4), autocorrect
  76.8 % right, 0.70 % wrong (no context 76.5 / 0.65), clean words never
  changed (36 296).
- Pairs stored as hash + 16-bit count; memory against 0.7.2: en +2.6,
  de +1.2, ru +3.9, uk +0.5 MB.
- German capital per pair: "das Unternehmen" / "wir unternehmen" (Hunspell
  accepts the lowercase verb, so the dictionary alone shows "unternehmen").
  Russian/Ukrainian capitals left to the dictionary: "спасибо Вам" was the
  corpus' polite style, not a name.
- Key sounds: LatinIME AudioAndHapticFeedbackManager plays FX_KEYPRESS_DELETE,
  _RETURN, _SPACEBAR and _STANDARD; tools/make-click-wav.py generates four
  sounds (letter click byte-identical), volume 15/35/60/100 %.

## 2026-10-07 — 0.7.5 fixes from an independent review

- A separate agent reviewed the 0.7.3-0.7.4 diff with probes against the
  library. Confirmed and fixed: (1) a correction held with its space left
  the previous word stale: predictions after "so i" + Space came from "so"
  and offered "I" again, and glide contexts used it (contextWord()); (2)
  undoing a double-space period lost the previous word (now kept in
  m_wordBeforePeriod); (3) the 1.5 key-width glide limit let short slides
  read as long words ("jk" -> "junk", "ва" -> "вывеска"): a word's ideal
  path may be at most 1.6x the finger's plus one key (human-like grid
  87.44 -> 87.36 %; 2.0x kept some junk without Hunspell, e.g. "ро" ->
  "ролло"); (4) dictation ending in a closing quote or "…" did not end the
  sentence.
- Also taken from the review: apostrophes and hyphens between letters are
  word connectors (LatinIME config_word_connectors), typed or read from the
  application; the pair builder drops stress marks and single letters that
  are not words; tests load the bundled pairs only where they ask for them.
- Left as is: German capitals from the bundled pairs also apply to a pair
  the user typed in lowercase ("haben sie" shows "Sie"); learned pairs keep
  no case.

## 2026-10-07 — 0.7.6 fixes from a review of the clipboard and editing code

- Second independent review (0.7.0-0.7.3), with probes: the test
  compositor, xkbcommon keymaps (us/de/fr: evdev KEY_Z is "y" on de, KEY_A
  "q" on fr), the real QML at 800x1280, 1200x1920, 1280x800.
- Fixed: shortcuts as keysyms when KWin offers fake input 6 (KWin commit
  a8aa9184, 2025-09-18: keycodeFromKeysym in the current layout, modifiers
  kept); commit_string split at 3000 bytes between characters (sandbox
  libwayland 1.22 aborted the client on a 4 KiB message); data control
  clears the old text when a new selection is read, swaps sources without an
  empty selection (test compositor counts empty selections), bounds writes
  to 0.5 s with a non-blocking pipe; x-kde-passwordManagerHint makes the
  text sensitive (no history, no chip, dots in the panel); previews of 300
  characters (4 MB chip layout measured at 1.4 s); editing panel rows from
  the panel height (portrait 3 rows x 5 columns instead of 5 x 3 that ran
  off the panel); Copy/Cut need a reported selection when the application
  reports text; terminal purpose: Copy = Ctrl+Shift+C, nothing else.
- Also: the translator is removed from QCoreApplication while reloading;
  platform objects are detached on aboutToQuit.
- Not changed: the fallback Cut (no fake input) deletes the whole selection
  while a toolkit may report a trimmed one; data control "finished" leaves
  the last text (KWin does not send it in practice).

## 2026-10-07 — 0.7.7 the input-method path against KWin, GTK 3 and Firefox

- Third independent review, with a simulator that drives the real
  TypingEngine and KWin backend against a model of KWin master
  (inputmethod.cpp, textinput_v3.cpp), GTK 3 imwayland.c and Firefox
  IMContextWrapper.cpp (all read from source). Plain typing in an empty
  claude.ai composer is right in the model; every failure needs a
  client-side event between letters.
- Fixed (checked in KWin's source myself): KWin's InputMethod keeps the
  commit argument of the last preedit_string (m_pendingText) and commits it
  in commitPendingText on a touch in the active window (input.cpp touchDown
  filter), a hardware key without keyboard grab, or before the keyboard
  focus changes; commitString does not clear it. After every commit the
  backend now sends preedit_string("", "") when its last preedit was not
  empty. Simulator: "hello." + window switch gave "hello.hello", "Hello" +
  Return (claude.ai sends) + window switch left "ello"; both right now.
- Fixed: Firefox disables and re-enables its text input without a focus
  change when field attributes change (IMContextWrapper.cpp, "bounce");
  GTK 3 drops the preedit on disable, and KWin, deactivating the input
  method, clears its pending text without committing it. The engine
  remembers a word still composed at deactivate (KWin resets us before
  deactivating when it commits the word itself, so a live composition at
  deactivate means nobody kept it) and composes it again on the next letter
  if, within 2 s and nothing typed since, the client's text does not hold
  it; auto-capitals are held back meanwhile. Simulator: "h" | bounce | "i"
  gave "I" (pre-1.2.1 shape, the reported symptom), "hi" | bounce | "s"
  gave "Hs", a stale cache after the bounce "HIs"; all "His" now.
- Not fixed: a suggestion tap on a word that was not composed and not yet
  confirmed by the client goes out as BackSpace keys + commit, which GTK
  applies in the wrong order (simulator "HelpHelpful"); Firefox ending a
  composition itself without a KWin reset (a script on the page) makes KWin
  re-send our preedit. Both need the device to see whether they happen.
- Focus loss (B): nothing in our code moves focus; the panel height changed
  with ?123 when the number row was on (the edge moved under the finger):
  now constant, the symbol rows share the space. The emoji row likewise
  appears at once (frequent emoji) instead of after the first emoji.

## 2026-10-08 — 0.7.8 touch handling (fourth review)

- A review drove the real Main.qml with QTest touch sequences (offscreen,
  Qt 6.4): 10 confirmed bugs. Fixed: commit order on rolling presses (a new
  press commits older plain taps, LatinIME PointerTracker
  releaseAllPointersOlderThan; not for Shift, ?123, the symbol page key and
  the globe); maximumTouchPoints 1 made MultiPointTouchArea ignore every
  event while two fingers were on one key (now 5, the key follows one
  pointId and a second finger commits the first one's tap); the hold timer
  now ignores presses that became a gesture (consumeRelease) or drifted a
  quarter key from the press point; glide start at 0.5 key widths (0.9
  within 500 ms of a tap, after LatinIME's dynamic gesture threshold) instead
  of 18 px, and an empty glide shorter than 1.2 keys types its start key
  (the engine now commits the word being typed only when a glide gives a
  word); the press scale (0.965 on the key, the touch area's parent) moved
  releases near the edge outside: removed; touch areas reach half the gap,
  releases count up to half a gap + 6 px outside; picker inserts only near
  itself and ends the glide candidate first; TouchCancel resets the glide;
  a bridge signal at deactivate and the window's hide end every press;
  Shift slide starts half a gap + 10 px past the edge and undoes only the
  one-shot it set (new cancelOneShotShift); space-bar cursor drag has a
  dead zone of half a key; the language panel blocks touches.
- The old two-thumb QML test sent a new touch sequence per step, which in
  Qt 6.4 puts the stationary point at 0,0; rewritten as one sequence, it
  fails on 0.7.7 ("l" before "a") and passes now. The review's probes:
  all single-sequence ones pass; three multi-sequence ones remain as
  harness artefacts.

## 2026-10-08 — 0.7.9 the install path (fifth review)

- A review ran the installer end to end with a throwaway HOME and read KWin
  v6.4.5, v6.7.5, v6.7.91 and master, kservice, plasma-keyboard,
  plasma-workspace and the Arch package pages. Current Arch: KWin 6.7.5,
  Qt 6.12 rolling in. My earlier ledger entry (0.7.2) read KWin master:
  there the input method gets fake input; in 6.7.5 org_kde_kwin_fake_input
  is on interfacesBlackList and allowed only when the program's desktop file
  (found by its canonical Exec path, utils/serviceutils.h) lists it in
  X-KDE-Wayland-Interfaces. Added to the desktop file; the installer writes
  an absolute Exec already.
- Reproduced and fixed: the QML test failed in a de/ru/uk session
  (UiTranslator follows LANGUAGE, which Plasma exports); all tests now get
  LANGUAGE=en, LC_ALL=C.UTF-8, QT_QPA_OFFSCREEN_NO_GLX=1 from CTest. Checked:
  the whole install as a normal user with LANG=ru_RU / LANGUAGE=ru (21/21),
  a tastra-update rebuild after a simulated Qt change with LANGUAGE=uk
  (21/21; the rebuilt binary was byte-identical, so no backup), rollback
  walking back 0.7.8 -> 0.7.7 -> 0.7.6 -> "no older build", and the update
  check after a rollback.
- tastra-update compares a record of the installed build (source, commit,
  Qt version, voice, key sounds, binary checksum) instead of only the
  commit: before, a failed update, a rollback or tastra-voice-setup left
  the old binary in place with "up to date".
- During testing a command chain failed and an unprefixed `$U` ran
  tastra-update as root in the sandbox: it cloned the public repository to
  /root/.local/src/tastra and stopped at the build; removed again. Scenario
  scripts now run from files through runuser.


## 2026-10-08 — 0.7.10 learning off the typing path

- Measured (sandbox, RelWithDebInfo, 160 learned words, `LocalLexicon`
  directly): with 10 000 learned words and 30 000 pairs, 0.7.9 spent up to
  222 ms on the typing thread on every 16th word (one QSettings write of
  everything; 290 ms at 12 000 / 36 000). Now the lists are copied (shared
  until changed) and written by one background thread: at most 2.6–4.7 ms
  on the typing thread (the detach and, at the limit, the pruning), mean
  0.1–0.2 ms. Saves queued while one is written collapse into one (ten
  queued saves took 1.4 s to drain before that change, two now).
  `waitForLearningWrites()` runs before loading (language switch, a second
  lexicon), before clearing and at destruction; the test that loads in a
  second lexicon fails 3 of 5 runs without that wait.
- Bounds checked in LatinIME (AOSP main): HeaderPolicy
  DEFAULT_MAX_NGRAM_COUNTS = {10000, 30000, ...};
  ForgettingCurveUtils::ENTRY_COUNT_HARD_LIMIT_WEIGHT = 1.2 triggers
  truncation; LanguageModelDictContent::truncateEntries removes the
  lowest-priority entries down to the maximum. Tastra does the same with
  the count as priority; LatinIME's priority also decays with time
  (levels drop every 15 days), which plain counts do not, so a word typed
  often long ago keeps its place. Older unbounded lists are cut on load.
- nextWords() scanned every learned pair (startsWith on each key) after
  every word: 2.3 ms at 30 000 pairs. An index by previous word, kept in
  step on learn, rebuilt after removals: 0.15 ms. Test:
  learnedNextWordsFollowLearningAndForgetting.
- KWin's InputMethod::stopInputMethod() (master, 0f621cc) sends SIGTERM,
  waits up to 30 s, then SIGKILL; it runs when another virtual keyboard is
  chosen and when KWin exits. Tastra had no handler, so up to 15 learned
  words since the last field change were lost. Now a self-pipe handler
  saves on the event loop, then re-raises the signal with the default
  action, so the process ends exactly as before (no destructors run).
  tst_terminationsaver starts itself as a child, learns 3 words, sends
  SIGTERM/SIGINT: the words are saved and the child dies by the signal;
  without the handler they are not saved. Under TSan the child cannot be
  started (libtsan CHECK in ForkAfter with Qt 6.4's clone); ASan passes.
- Qt 6.12 (Arch is moving to it): the Wayland client sits in qtbase
  (src/plugins/platforms/wayland, in 6.11 too). Diffed 6.11 and 6.12 headers that
  Tastra includes (qwaylandwindow_p.h, qwaylandshellsurface_p.h,
  qwaylandshellintegration_p.h, qwaylandscreen_p.h): source compatible
  (an optional parent argument, copies disabled, QScopedPointer ->
  std::unique_ptr), but the class layout changed, so a build against 6.11
  must be rebuilt for 6.12: tastra-update does that since 0.7.9.
- Checks: 19/19 sandbox tests, the 3 KWin protocol tests in the overlay,
  the full suite under ASan/UBSan (the new test on its own), lexicon/smarttyping/typingstress/
  bridge/legacymigration under TSan (Qt's own reports suppressed), static
  verify. Mutation checks: no pruning -> the bounds test fails; no wait
  before loading -> the background-save test fails.

## 2026-10-08 — 0.7.11 words the cursor touches (sixth review)

- Rebuilt the sandbox after a reset (Ubuntu Qt 6.4.2; the input-panel shell
  call is stubbed in a copy only, as before) and re-read the sources the
  0.7.7 simulator was built from: GTK 3 gtk-3-24 modules/input/imwayland.c,
  KWin master src/inputmethod.cpp and src/wayland/textinput_v3.cpp, Firefox
  widget/gtk/IMContextWrapper.cpp. That simulator was lost with the old
  sandbox; tests/textinputclient.h is a new one kept in the repository, and
  tests/tst_clientsim.cpp drives the real TypingEngine against it.
- Facts from the sources that decide the design: KWin sends every
  commit_string, preedit_string and delete_surrounding_text to a v3 client
  with its own done, and keysyms as wl_keyboard keys; GTK applies text-input
  events while reading them and GDK's queued keys afterwards; GTK turns the
  bytes of delete_surrounding_text into characters with the surrounding text
  it last retrieved from the widget (cursor_pointer - before_length: a
  deletion longer than that text reads before it), and retrieves it again at
  every done with a commit (even an empty one), a changed preedit or a
  deletion, when the serial matches; Firefox answers from a content cache
  with an emulated caret (DispatchCompositionCommitEvent) and deletes from
  the cached selection, failing when the cache is short.
- Reproduced and fixed: typing on at the end of an old word committed the
  letters, and a suggestion tap sent Backspace keys + commit: Firefox model
  "I need helphel ", "heloh". The word is now composed again on the first
  letter (delete_surrounding_text + preedit, both text-input, only when the
  client has confirmed the text, so GTK measures the deletion on the right
  text; at a paragraph start the first letter stays committed, as for new
  words since 0.4.x).
- Reproduced and fixed: Backspace into a word left no current word: no
  suggestions for it, and letters typed on started a new word ("hello",
  Space, Backspace, "s" was "s" for corrections). Now the word is resumed
  (LatinIME restartSuggestionsOnWordTouchedByCursor, no autocorrection of the
  resumed word itself, as LatinIME's recorrection suggestions); a letter
  composes it again when the client confirmed it, otherwise starts a new word
  as before; Space after an unchanged resumed word is held like after a typed
  word. tst_typingstress's reference follows this rule now (600 seeds x 6
  modes pass).
- Reproduced and fixed: with composition off, a tap before the client's
  answer used Backspace keys (GTK model "we help yuoy", Firefox "we heplh").
  The replacement now goes out as an empty commit (GTK reads the widget's
  text again), delete_surrounding_text and the commit. Autocorrection at
  Space still needs a confirmed word (a Space can follow a letter within
  milliseconds, before Firefox's cache has the letter).
- Found and fixed while testing: a suggestion that changes a paragraph's
  committed first letter gave "ythe"; glides were glued to the word or
  period before them ("a catthe", "hi.The") and replaced a word the cursor
  had been put after. Now LatinIME's phantom space (onStartBatchInput:
  letter, digit or symbols_followed_by_space before the cursor).
- Checks: full suite 23/23 (new clientsim: 8 functions, 21 cases in GTK
  and Firefox modes, 60 random sequences each with suggestion taps; 800
  passed once), ASan + UBSan 23/23. 0.7.10's engine fails 14 of the 21.
- Not changed, needs the device: Firefox ending a composition by itself (a
  page script): KWin forwards no change cause and sends no reset, and what a
  client reports while composing differs (Firefox leaves the composition out,
  Chromium may not), so the keyboard cannot tell safely.

## 2026-10-08 — 0.7.12 editing in the middle of the text

- The client model now does what KWin does on a tap in the text: commit the
  keyboard's last preedit (m_pendingText) and reset the keyboard
  (InputMethod::commitPendingText), then report the new cursor.
- Reproduced and fixed: a suggestion or glide in front of a space left the
  keyboard's held space behind as a second one ("I love  dogs", "I help ."
  before a period; the re-correction of an old word likewise). The word now
  gets no space when the client reported a space or punctuation right after
  the cursor (the text after the cursor does not change while typing before
  it; cleared on every reset), as the word-around-cursor path did since
  0.6.x. One Backspace and the other readings of a glide follow what was
  actually committed after the word (none, held or committed space).
- Reproduced and fixed: with composition off, Firefox model, empty field: the
  other reading of a glide gave "to too", Backspace after it nothing: the
  deletion was measured by GTK on text from before the glide. deleteOwnText
  sends the empty commit first (0.7.11's re-read) unless the client has
  confirmed the text.
- Diagnostic for the open Firefox question: the trace marks an echo during a
  composition whose text already holds the composed word (a flag, no text).
- Checks: 23/23, ASan + UBSan 23/23; clientsim 35 cases, 0.7.11 fails 9.

## 2026-10-08 — 0.7.13 the release criterion; gesture delete; paste sections

- User: "критерий готовности — полная копия гугл клавы". Written down as
  docs/GBOARD-PARITY.md from the Gboard Help Center (Use your keyboard, Word
  suggestions, Copy & paste sections, Theme/sound/vibration, Slide to type,
  Advanced voice typing, Writing tools, Handwriting, Morse code, Languages;
  read 2026-10-08) plus Android Police (2025-10) and Androidsis (2025-09),
  each feature marked done / partly / missing / impossible on KWin / needs
  Google's servers, with a backlog.
- Gesture delete: LatinIME onMoveDeletePointer selects word-wise and
  onUpWithDeletePointerActive deletes the selection; Tastra deleted as it
  slid. Now Ctrl+Shift+Left/Right per step and one BackSpace on release, all
  through KWin fake input (one channel; BackSpace is keysym 0xff08 on fake
  input 6). The word being typed is finished first. Deactivation mid-slide
  forgets the slide without sending keys. QML test: Qt Quick merges touch
  updates within a frame, so the test moves once per 40 ms.
- Paste sections: e-mail, web address, phone (7-15 digits, not a numeric
  date), numeric date, time, number of 3+ digits; nothing contained in an
  earlier part, never the whole text, at most three; first 20 000 characters
  of a copy. Chips beside the 📋 chip share the strip (render checked).
- Checks: 23/23, ASan + UBSan 23/23.

## 2026-10-09 — 0.7.14 voice commands, switch keys, comma menu, letter layouts

- Voice commands from the Gboard Help Center list (Advanced voice typing):
  whole-utterance phrases in en/de/ru/uk after normalising Whisper's
  capital and full stop; "stop" is left out (recording ends with the mic
  key); detailed edits ("change X to Y") are not done yet. Deletions are
  measured on the text the keyboard knows and go through
  replaceBeforeCursor's deliberate path (0.7.11 re-read), checked in the
  GTK and Firefox client models; "clear all" uses Ctrl+A + BackSpace with
  fake input, else the known text. Spoken punctuation: only names that are
  never words anywhere; "comma"/"full stop" only at the end; a mark Whisper
  wrote next to the spoken one is absorbed ("ладно, запятая." -> "ладно,").
  Never in password fields.
- Incognito: GTK 3's imwayland translate_hints maps no private-window hint
  and text-input-v3 has none, so Gboard's automatic incognito cannot be
  done; marked 🚫 in the parity list (learning can still be turned off).
- Switch keys as in Gboard Preferences (the language key is greyed out while
  the emoji key is on, Gboard Help "Type in a different language"); the
  language list then opens above the space bar. Comma long-press as a
  three-item picker (emoji, settings, one-handed right / back to full).
- Letter layouts: en QWERTY/QWERTZ/AZERTY/Dvorak/Colemak, de QWERTZ/QWERTY,
  per language in settings (layoutVariant/<code>); symbol hints follow the
  position; Dvorak's ' , . keys type marks; wrong-layout maps use the
  chosen layouts. Rendered AZERTY and Dvorak checked.
- Checks: 23/23, ASan + UBSan 23/23.

## 2026-10-09 — 0.7.15 colour and photo themes, resize

- Themes: Gboard's theme gallery (Colors, My themes with brightness; Gboard
  Help "Change your keyboard theme"). Colours tint the light/dark palette
  with Qt.tint; a photo is chosen from the Pictures folder inside the
  settings panel (no file dialog: the panel cannot type into a dialog of its
  own), decoded at the keyboard's size; keys 72 % opaque over it, the strip
  transparent; settings validate the file (jpg/jpeg/png/webp, readable) and
  drop a photo that is gone. Rendered dark-red, light-teal and photo checked.
- Resize: Gboard's toolbar Resize drags the top edge. KWin keeps the input
  panel's bottom on the screen's bottom, so the finger's distance from the
  window's bottom is its screen position; the keys grow by that distance
  over the number of rows. A first test version fed back (the offscreen
  window grows downwards) and was rewritten to emulate the fixed bottom.
  Range 0.75–1.40 for both resize and Key size.
- Found by the full run: with five toolbar buttons the middle one sat under
  the "Add to dictionary" chip and took its tap (with four, the chip's centre
  fell between buttons); the buttons now hide while the chip shows. The mic
  stays (it ends a recording).
- Checks: 23/23, ASan + UBSan 23/23.

## 2026-10-17 — 0.7.16 toolbar choice, gradients

- Found uncommitted in the workspace (written 2026-10-09, not released):
  toolbar choice and order (Gboard lets one pick the toolbar's features),
  a one-handed toolbar item and gradient themes, with tests. Reviewed,
  rendered (settings section, ocean gradient, one-handed toolbar), full
  suite 23/23 and ASan + UBSan 23/23, released as is.
- Toolbar: the order is kept as "toolbarItems"; ids a later or earlier
  version does not know are dropped; settings is always there (it is the way
  back to the choice). Gradients and a photo exclude each other.

## 2026-10-17 — 0.7.17 phrase chip, Morse code

- Phrase completion: measured first on held-out text with the bundled word
  pairs: the predicted next word was right in about 30 % of the offers
  (60–90 % only for a handful of pairs), too often wrong for a chip that
  types several words. So only the user's own pairs count, and only a habit:
  typed at least three times and at least 60 % of what followed the word.
  While typing, the first strip candidate with a habit is completed; after a
  Space only habits of two words or more (one word is the next-word chip).
  Not inside a word, after a glide, across a sentence, with a wrong-layout
  or re-correction offer, in password fields. Long-press removes the pairs
  of the phrase, not its words.
- Tested on the GTK and Firefox client models (at the end, before a period,
  composing and not) and with 30 random sessions per model of whole words
  from a small vocabulary with the chip taken whenever offered (>50 phrases
  each).
- Morse: Gboard Help "Write in Morse code" (read 2026-10-17) documents the
  dot and dash keys, Character timeout ("Never": Space sends), Word timeout
  (off by default) and normal correction; its default character timeout is
  not documented, 0.8 s here. Tables: Wikipedia "Morse code" (ITU-R
  M.1677-1 and the usual non-ITU marks), "Morse code for non-Latin
  alphabets" (Russian, Ukrainian), DARC page by DK5KE for German (ä ö ü ch;
  ß has no sign). Ukrainian ґ, Russian ъ and ё have no sign in those tables.
- The Morse layout is a letter layout of each language, so the choice is
  kept per language, the lexicon keeps the language's first rows, and the
  symbol page and bottom row are unchanged. The bridge unit test has no
  event loop (QTEST_APPLESS_MAIN), so the timeouts are tested in the QML
  test.
- Checks: 23/23, ASan + UBSan 23/23.
