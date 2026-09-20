# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A JUCE 7.0.9 audio plugin (C++17) that generates MIDI notes and is growing into a generative MIDI sequencer. It ignores incoming MIDI. While the host transport is playing, every 1/8th step of the host's PPQ timeline has a chance (`Chance`, 0-100 %) of playing one note from the selected `Key` and `Scale`, for the selected `Note Length` (1/32 to 1/1). `Play Mode` is either Arpeggio (walks the scale in the `Arp Direction`) or Random. Defaults: C major, arpeggio up, 100 %, 1/16th. Notes are in the octave of C4, one MIDI channel, fixed velocity. Nothing is generated while the transport is stopped, and notes still sounding are ended when it stops. There is no linter configured.

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

Each scenario creates a fresh plugin instance, sets parameters by name and text through the CLAP params extension (`text_to_value`, so scale names in the test must match the choice strings exactly), drives the transport (120 BPM, 48 kHz, so a 1/8th step is 12000 samples), and compares note-ons and note-offs with hand-written expectations. It covers: the defaults, stopped and missing transport, a stop while a note sounds, starting between two steps, every arp direction, the same note played again while it sounds, random mode and every scale, chance 0, ignored MIDI input, and the state save/load round trip. When behaviour changes, add or change scenarios in that file rather than adding a second host. A new test should be shown to fail against a deliberately broken plugin before it is trusted. Final confirmation in Bitwig, including the editor, is manual, since neither can be driven headlessly.

## Architecture

- Only `VST3` is declared in `juce_add_plugin(FORMATS VST3)`. JUCE 7 has no CLAP format, so `clap_juce_extensions_plugin(TARGET GenerativeMidi ...)` wraps the same target as a CLAP (`GenerativeMidi_CLAP`). Format-specific behaviour should live behind JUCE's `AudioProcessor` API, not in per-format code.
- `GenerativeMidiProcessor` (`src/PluginProcessor.*`) is the `AudioProcessor`; `GenerativeMidiEditor` (`src/PluginEditor.*`) is its UI, created in `createEditor()`. `createPluginFilter()` at the bottom of `PluginProcessor.cpp` is the JUCE entry point.
- `src/Music.h` holds the musical tables (keys, scales, play modes, arp directions, note lengths) and `arpIndex()`, with no JUCE dependency; the test host includes it too. The tables' order is the order of the matching parameter's choices, which is what a saved session stores, so add to the end only. The step grid (1/8th) is a constant in `PluginProcessor.cpp`, not a parameter.
- Generation is locked to the host's PPQ position, not to a running sample counter: the steps in a block are those of the timeline between the block's start and end, and the arp index derives from the step number, so loops and jumps keep the pattern in place. Pending note-offs are kept in block-relative samples. Within a block the note-offs are added to the `MidiBuffer` before the note-ons, which is what makes a note end before the same note restarts on the same sample.
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
