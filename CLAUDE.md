# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

SeqVerse is a JUCE 7.0.9 audio plugin (C++17) that generates MIDI notes and is growing into a generative MIDI sequencer (it was called GenerativeMidi before). It ignores incoming MIDI. While the host transport is playing, every step of the host's PPQ timeline (`Step Rate`, 1/32 to 1/1 with triplets) has a chance (`Chance`, 0-100 %) of playing one note from the pool of notes of the selected `Key` and `Scale` over the `Low Octave`..`High Octave` range, plus (with `Octave Note` on Include, the default) the root of the octave above the range as the top note, so one octave of C major is C D E F G A B C, for the selected `Note Length` (1/32 to 1/1). `Play Mode` is either Arpeggio (walks the pool in the `Arp Direction`) or Random. `Swing` delays every odd step, `Velocity` and `Velocity Variation` set the velocity, `Channel` the MIDI channel, and `Seed` makes the random choices repeatable (0 is "Auto", different every time). `Cycle Length` (0.5 to 64 beats in half-beat steps, default "Endless") makes the output repeat: the step grid restarts every cycle and each cycle plays what the first did. Defaults: C major, arpeggio up, 1/8 steps, 100 %, 1/16th notes, octave 4 only, velocity 100, channel 1, no swing. Nothing is generated while the transport is stopped, and notes still sounding are ended when it stops. There is no linter configured.

## Build

JUCE and clap-juce-extensions are **not** vendored or fetched; CMake `add_subdirectory`s them from `~/SDKs/JUCE` and `~/SDKs/clap-juce-extensions` and fails at configure time if either lacks a `CMakeLists.txt`. Override with `-DJUCE_DIR=...` / `-DCLAP_JUCE_EXTENSIONS_DIR=...`. Pinned versions are noted at the top of `CMakeLists.txt` (JUCE tag 7.0.9, clap-juce-extensions `54d83e1`).

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target SeqVerse_VST3
cmake --build build --target SeqVerse_CLAP
```

Artefacts land in `build/SeqVerse_artefacts/Release/{VST3,CLAP}`. The JUCE and clap-juce-extensions trees are added `EXCLUDE_FROM_ALL`, so only what the plugin targets depend on gets built from them.

## Testing

`tests/clap_test_host.cpp` is the scripted CLAP test host: a small headless host (no GUI, no audio device) that `dlopen`s the built `.clap` from the build tree, runs scenarios through `process()` and checks the MIDI that comes out. It never touches `~/.clap`. Registered as the CTest test `clap_host_generator`.

```
cmake --build build --target SeqVerse_CLAP clap_test_host
ctest --test-dir build --output-on-failure
./build/clap_test_host build/SeqVerse_artefacts/Release/CLAP/SeqVerse.clap   # verbose, per-check output
```

Each scenario creates a fresh plugin instance, sets parameters by name and text through the CLAP params extension (`text_to_value`, so scale names in the test must match the choice strings exactly), drives the transport (120 BPM, 48 kHz, so a 1/8th step is 12000 samples), and compares note-ons and note-offs with hand-written expectations. Scenarios run with `Octave Note` set to Exclude unless they set `octaveNote = true` (or the parameter) themselves, because most were written for one octave of seven notes and are about something else; the "default settings" and "octave note" scenarios test the real default. It covers: the parameter ranges reported to the host, choices picked by raw index the way a host does, the defaults, stopped and missing transport, a stop while a note sounds, starting between two steps, every one of the 15 arp directions (hand-written patterns, and for pools of 1 to 96 notes each stays inside the pool and plays all of it), the same note played again while it sounds, random mode and every scale, chance 0, ignored MIDI input, step rates, octave ranges (including low above high), velocity and variation, channel, swing (also with steps that land on block boundaries in many ways), seeds (repeatable, different, and independent of where the transport starts), cycle lengths (arp pattern, steps that do not fit the cycle, starting inside a cycle, and random choices, chance, velocity and swing repeating every cycle), the state save/load round trip, and loading a session saved by the earlier version of the state (fractions of the range). The message-thread listener that keeps low octave <= high octave needs a JUCE message loop and is not covered headlessly. When behaviour changes, add or change scenarios in that file rather than adding a second host. A new test should be shown to fail against a deliberately broken plugin before it is trusted.

`tests/ui_test.cpp` (target `SeqVerseUiTest`, CTest test `ui_enabled_controls`) builds the real editor and processor without a host or a window, changes parameters, and checks which controls are enabled. Every control's component ID is its parameter ID, so tests find controls with `findChildWithID()`. Given a path prefix it also writes PNG snapshots, which can be looked at with the Read tool, so use it to check any layout or colour change instead of guessing:

```
cmake --build build --target SeqVerseUiTest
./build/SeqVerseUiTest_artefacts/Release/SeqVerseUiTest "$CLAUDE_JOB_DIR/tmp/editor"   # writes editor_arpeggio.png etc.
```

Only the final confirmation in Bitwig itself is manual.

## Architecture

- Only `VST3` is declared in `juce_add_plugin(FORMATS VST3)`. JUCE 7 has no CLAP format, so `clap_juce_extensions_plugin(TARGET SeqVerse ...)` wraps the same target as a CLAP (`SeqVerse_CLAP`). Format-specific behaviour should live behind JUCE's `AudioProcessor` API, not in per-format code.
- `SeqVerseProcessor` (`src/PluginProcessor.*`) is the `AudioProcessor`; `SeqVerseEditor` (`src/PluginEditor.*`) is its UI, created in `createEditor()`. `createPluginFilter()` at the bottom of `PluginProcessor.cpp` is the JUCE entry point.
- `src/Music.h` holds the musical tables (keys, scales, play modes, arp directions, step rates, note lengths), `noteNumber()`, `arpIndex()` and the per-step random generator `StepRandom`, with no JUCE dependency; the test host includes it too. The tables' order is the order of the matching parameter's choices, and a saved session stores the index, so add to the end only (see the note on state below). The 15 arp directions are all pure functions of the step number and the pool size in `arpIndex()`; the random-based ways of choosing notes (shuffle and the like) are deliberately not among them, because Random mode covers randomness.
- Generation is locked to the host's PPQ position, not to a running sample counter: a block owns the steps that start in it (an odd step starts one swing delay after its grid position), and the arp index derives from the step number, so loops and jumps keep the pattern in place. Pending note-offs are kept in block-relative samples. Within a block the note-offs are added to the `MidiBuffer` before the note-ons, which is what makes a note end before the same note restarts on the same sample.
- All the randomness of a step (chance, note, velocity variation) comes from one `StepRandom` that is always drawn from in the same order, so changing one setting does not reshuffle the others. With a non-zero `Seed` it depends on the seed and the step number only; with 0 it is seeded from a `juce::Random`.
- Cycle length is counted in half beats (0 is endless). There is **no recording buffer**: with a cycle, everything about a step (note, chance, velocity variation, swing, arp position) is a pure function of its number within the cycle and of a cycle seed, so every cycle repeats the last one exactly, and there is nothing to allocate, no edge case for starting inside a cycle, and loops or jumps of the transport keep the pattern. Cycles start at multiples of the cycle length on the host timeline and the step grid restarts in each, so a cycle that is not a whole number of steps ends with a shorter last step. The cycle seed is `Seed` when set; on Auto it is rolled when the transport starts and when the cycle length changes (`autoSeed`). Because nothing is recorded, changing key, scale or octave range while cycling changes the loop live (a transposition), while changing chance or velocity variation changes which steps play and how hard. Endless mode does not go through this path. Outside a cycle the arp position is the step number on the host timeline, so a transport that starts mid-song joins the pattern where it would be. There is deliberately no "restart on play" option: an `Arp Reset` parameter existed and was removed, since a cycle length that matches the bar does the same job.
- `Low Octave` <= `High Octave` is enforced in `processBlock` (it uses the min and max of the two) and by a listener on the message thread: `parameterValueChanged` only records which one changed and triggers an `AsyncUpdater`, whose handler moves the other parameter.
- `CMakeLists.txt` passes `CLAP_USE_JUCE_PARAMETER_RANGES ALL` to `clap_juce_extensions_plugin`. Do not remove it: by default the wrapper reports every parameter to the host as 0..1 while still flagging choice parameters as stepped, and a host such as Bitwig then offers only two values of a 43-entry list (the Scale selector appeared stuck on one scale). With `ALL` the host gets the real range and sends a choice's index as the value. The test host checks the reported ranges and also sends raw indices, because setting parameters by text (`text_to_value`) cannot reveal this kind of bug.
- Editor: a control that has no effect with the current settings is disabled, label included (`updateEnabledControls()` in `PluginEditor.cpp`, hooked to the play mode, chance and velocity variation controls, which the attachments also update on host changes). Arp direction needs Arpeggio mode. The seed needs something random: Random mode, a chance below 100 %, or a velocity variation, because an arpeggio at 100 % with a fixed velocity has nothing to seed. When a new parameter's effect depends on another, extend that function and `tests/ui_test.cpp`. The editor has a dark theme: `src/DarkLookAndFeel.h` holds the palette (`theme::`), the knob drawing (thin ring, glowing teal value arc, white end dot, dimmed when disabled) and the colours of dropdowns, popup menus, labels and value boxes. Editor code should not set colours itself (only the title does), so a restyle stays in that one file. The editor calls `sendLookAndFeelChange()` at the end of its constructor, because the sliders' value boxes are created before the sliders are children of the editor and would otherwise keep JUCE's default look. `tools/knob_styles.cpp` (target `KnobStyles`) renders sample controls in several alternative knob styles as PNGs, for trying a different look before changing the real one. The last 70 pixels of the editor are left empty because JUCE's mandatory "Made with JUCE" badge covers the bottom right corner.
- The MIDI input port stays declared but unused (`acceptsMidi()` is true), so hosts such as Bitwig still place the plugin in a note chain.
- State save/restore writes each parameter's **value** (the index of a choice, the number of an integer parameter) as an XML attribute named by its parameter ID, plus a `version` attribute (currently 2); new parameters are saved automatically, and missing ones keep their defaults. It must not store fractions of the range: a list that gets another entry would then change what a saved value means, which is what happened to the first version of the state (version 1, no attribute), stored as fractions. `setStateInformation` still reads version 1, and migrates the arp direction, which had 7 entries then (`kNumArpDirectionsInFirstVersion`). When a choice list grows it grows at the end, and never gets reordered or shortened; if a parameter's meaning has to change in another way, raise the state version and migrate in `setStateInformation`.
- The plugin is deliberately declared as an effect (`IS_SYNTH FALSE`, `IS_MIDI_EFFECT FALSE`) with MIDI in and out, and with a **silent stereo output bus and no input bus**. That bus exists because some hosts (Bitwig) do not run a plugin with no audio ports. `isBusesLayoutSupported` enforces "no input, stereo output" and must stay consistent with the constructor's `BusesProperties`.
- Plugin identity (`PLUGIN_MANUFACTURER_CODE Hjuh`, `PLUGIN_CODE Sqvs`, CLAP ID `com.henrik.seqverse`) is what hosts use to recognise saved sessions, so don't change it casually.
- `JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0` and `JUCE_VST3_CAN_REPLACE_VST2=0` are set as compile definitions; the only JUCE module linked is `juce_audio_utils`.

## Project rules

- **Audio thread** (`processBlock`): no allocations, no locks, no UI calls, and no setting of parameters. Read parameters directly via `AudioParameterInt::get()` and `AudioParameterChoice::getIndex()`.
- **Parameter constraints** (e.g. low <= high) are enforced in both places: in `processBlock` and by a message-thread listener.
- **Testing:** use the scripted CLAP test host before claiming something works.
- **Building:** use CMake and Ninja, and the build must produce no warnings.
- **Final step of every task:** build the `.clap`, run the CTest host test, then copy `build/SeqVerse_artefacts/Release/CLAP/SeqVerse.clap` to `~/.clap/` so it can be tested manually in Bitwig. This is standing permission, no need to ask. Copy to a temp name in `~/.clap` and `mv` it over the target, because Bitwig may have the old file loaded. Check whether Bitwig is running (`pgrep -ia bitwig`) and mention it, since it needs a plugin rescan.

## Repo conventions

- `build/` is git-ignored. Sibling checkouts under `.claude/worktrees/` are other Claude sessions' worktrees, not part of the source tree.
- The default branch is `main`, but the initial checkout is on `master`.
