# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A JUCE 7.0.9 audio plugin (C++17) that generates MIDI notes and is growing into a generative MIDI sequencer. It ignores incoming MIDI. While the host transport is playing, every step of the host's PPQ timeline (`Step Rate`, 1/32 to 1/1 with triplets) has a chance (`Chance`, 0-100 %) of playing one note from the pool of notes of the selected `Key` and `Scale` over the `Low Octave`..`High Octave` range, for the selected `Note Length` (1/32 to 1/1). `Play Mode` is either Arpeggio (walks the pool in the `Arp Direction`; `Arp Reset` picks whether it follows the timeline or restarts when the transport starts) or Random. `Swing` delays every odd step, `Velocity` and `Velocity Variation` set the velocity, `Channel` the MIDI channel, and `Seed` makes the random choices repeatable (0 is "Auto", different every time). Defaults: C major, arpeggio up, 1/8 steps, 100 %, 1/16th notes, octave 4 only, velocity 100, channel 1, no swing. Nothing is generated while the transport is stopped, and notes still sounding are ended when it stops. There is no linter configured.

## Build

JUCE and clap-juce-extensions are **not** vendored or fetched; CMake `add_subdirectory`s them from `~/SDKs/JUCE` and `~/SDKs/clap-juce-extensions` and fails at configure time if either lacks a `CMakeLists.txt`. Override with `-DJUCE_DIR=...` / `-DCLAP_JUCE_EXTENSIONS_DIR=...`. Pinned versions are noted at the top of `CMakeLists.txt` (JUCE tag 7.0.9, clap-juce-extensions `54d83e1`).

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target GenerativeMidi_VST3
cmake --build build --target GenerativeMidi_CLAP
```

Artefacts land in `build/GenerativeMidi_artefacts/Release/{VST3,CLAP}`. The JUCE and clap-juce-extensions trees are added `EXCLUDE_FROM_ALL`, so only what the plugin targets depend on gets built from them.

## Testing

`tests/clap_test_host.cpp` is the scripted CLAP test host: a small headless host (no GUI, no audio device) that `dlopen`s the built `.clap` from the build tree, runs scenarios through `process()` and checks the MIDI that comes out. It never touches `~/.clap`. Registered as the CTest test `clap_host_generator`.

```
cmake --build build --target GenerativeMidi_CLAP clap_test_host
ctest --test-dir build --output-on-failure
./build/clap_test_host build/GenerativeMidi_artefacts/Release/CLAP/GenerativeMidi.clap   # verbose, per-check output
```

Each scenario creates a fresh plugin instance, sets parameters by name and text through the CLAP params extension (`text_to_value`, so scale names in the test must match the choice strings exactly), drives the transport (120 BPM, 48 kHz, so a 1/8th step is 12000 samples), and compares note-ons and note-offs with hand-written expectations. It covers: the parameter ranges reported to the host, choices picked by raw index the way a host does, the defaults, stopped and missing transport, a stop while a note sounds, starting between two steps, every arp direction, the same note played again while it sounds, random mode and every scale, chance 0, ignored MIDI input, step rates, octave ranges (including low above high), velocity and variation, channel, swing (also with steps that land on block boundaries in many ways), arp reset, seeds (repeatable, different, and independent of where the transport starts), and the state save/load round trip. The message-thread listener that keeps low octave <= high octave needs a JUCE message loop and is not covered headlessly. When behaviour changes, add or change scenarios in that file rather than adding a second host. A new test should be shown to fail against a deliberately broken plugin before it is trusted.

`tests/ui_test.cpp` (target `GenerativeMidiUiTest`, CTest test `ui_enabled_controls`) builds the real editor and processor without a host or a window, changes parameters, and checks which controls are enabled. Every control's component ID is its parameter ID, so tests find controls with `findChildWithID()`. Given a path prefix it also writes PNG snapshots, which can be looked at with the Read tool, so use it to check any layout or colour change instead of guessing:

```
cmake --build build --target GenerativeMidiUiTest
./build/GenerativeMidiUiTest_artefacts/Release/GenerativeMidiUiTest "$CLAUDE_JOB_DIR/tmp/editor"   # writes editor_arpeggio.png etc.
```

Only the final confirmation in Bitwig itself is manual.

## Architecture

- Only `VST3` is declared in `juce_add_plugin(FORMATS VST3)`. JUCE 7 has no CLAP format, so `clap_juce_extensions_plugin(TARGET GenerativeMidi ...)` wraps the same target as a CLAP (`GenerativeMidi_CLAP`). Format-specific behaviour should live behind JUCE's `AudioProcessor` API, not in per-format code.
- `GenerativeMidiProcessor` (`src/PluginProcessor.*`) is the `AudioProcessor`; `GenerativeMidiEditor` (`src/PluginEditor.*`) is its UI, created in `createEditor()`. `createPluginFilter()` at the bottom of `PluginProcessor.cpp` is the JUCE entry point.
- `src/Music.h` holds the musical tables (keys, scales, play modes, arp directions and resets, step rates, note lengths), `noteNumber()`, `arpIndex()` and the per-step random generator `StepRandom`, with no JUCE dependency; the test host includes it too. The tables' order is the order of the matching parameter's choices, which is what a saved session stores, so add to the end only.
- Generation is locked to the host's PPQ position, not to a running sample counter: a block owns the steps that start in it (an odd step starts one swing delay after its grid position), and the arp index derives from the step number, so loops and jumps keep the pattern in place. Pending note-offs are kept in block-relative samples. Within a block the note-offs are added to the `MidiBuffer` before the note-ons, which is what makes a note end before the same note restarts on the same sample.
- All the randomness of a step (chance, note, velocity variation) comes from one `StepRandom` that is always drawn from in the same order, so changing one setting does not reshuffle the others. With a non-zero `Seed` it depends on the seed and the step number only; with 0 it is seeded from a `juce::Random`.
- `Low Octave` <= `High Octave` is enforced in `processBlock` (it uses the min and max of the two) and by a listener on the message thread: `parameterValueChanged` only records which one changed and triggers an `AsyncUpdater`, whose handler moves the other parameter.
- `CMakeLists.txt` passes `CLAP_USE_JUCE_PARAMETER_RANGES ALL` to `clap_juce_extensions_plugin`. Do not remove it: by default the wrapper reports every parameter to the host as 0..1 while still flagging choice parameters as stepped, and a host such as Bitwig then offers only two values of a 43-entry list (the Scale selector appeared stuck on one scale). With `ALL` the host gets the real range and sends a choice's index as the value. The test host checks the reported ranges and also sends raw indices, because setting parameters by text (`text_to_value`) cannot reveal this kind of bug.
- Editor: a control that has no effect with the current settings is disabled, label included (`updateEnabledControls()` in `PluginEditor.cpp`, hooked to the play mode, chance and velocity variation controls, which the attachments also update on host changes). Arp direction and arp reset need Arpeggio mode. The seed needs something random: Random mode, a chance below 100 %, or a velocity variation, because an arpeggio at 100 % with a fixed velocity has nothing to seed. When a new parameter's effect depends on another, extend that function and `tests/ui_test.cpp`. Slider value text is set to the dark text colour, since the default is white on the light background. The last 70 pixels of the editor are left empty because JUCE's mandatory "Made with JUCE" badge covers the bottom right corner.
- The MIDI input port stays declared but unused (`acceptsMidi()` is true), so hosts such as Bitwig still place the plugin in a note chain.
- State save/restore writes each parameter's normalised value as an XML attribute named by its parameter ID; new parameters are saved automatically, and missing ones keep their defaults.
- The plugin is deliberately declared as an effect (`IS_SYNTH FALSE`, `IS_MIDI_EFFECT FALSE`) with MIDI in and out, and with a **silent stereo output bus and no input bus**. That bus exists because some hosts (Bitwig) do not run a plugin with no audio ports. `isBusesLayoutSupported` enforces "no input, stereo output" and must stay consistent with the constructor's `BusesProperties`.
- Plugin identity (`PLUGIN_MANUFACTURER_CODE Hjuh`, `PLUGIN_CODE Gnmd`, CLAP ID `com.henrik.generative-midi`) is what hosts use to recognise saved sessions, so don't change it casually.
- `JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0` and `JUCE_VST3_CAN_REPLACE_VST2=0` are set as compile definitions; the only JUCE module linked is `juce_audio_utils`.

## Project rules

- **Audio thread** (`processBlock`): no allocations, no locks, no UI calls, and no setting of parameters. Read parameters directly via `AudioParameterInt::get()` and `AudioParameterChoice::getIndex()`.
- **Parameter constraints** (e.g. low <= high) are enforced in both places: in `processBlock` and by a message-thread listener.
- **Testing:** use the scripted CLAP test host before claiming something works.
- **Building:** use CMake and Ninja, and the build must produce no warnings.
- **Final step of every task:** build the `.clap`, run the CTest host test, then copy `build/GenerativeMidi_artefacts/Release/CLAP/GenerativeMidi.clap` to `~/.clap/` so it can be tested manually in Bitwig. This is standing permission, no need to ask. Copy to a temp name in `~/.clap` and `mv` it over the target, because Bitwig may have the old file loaded. Check whether Bitwig is running (`pgrep -ia bitwig`) and mention it, since it needs a plugin rescan.

## Repo conventions

- `build/` is git-ignored. Sibling checkouts under `.claude/worktrees/` are other Claude sessions' worktrees, not part of the source tree.
- The default branch is `main`, but the initial checkout is on `master`.
