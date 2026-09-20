# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A JUCE 7.0.9 audio plugin (C++17) that takes MIDI in and puts MIDI out, intended to become a generative MIDI sequencer. Currently it turns incoming notes into runs of 1/8th notes in the major scale of the incoming note (see the comment at the top of `src/PluginProcessor.h`); everything that is not a note passes through. The tempo comes from the host playhead, with a 120 BPM fallback. There are no parameters yet and state save/restore is empty. There is no linter configured.

## Build

JUCE and clap-juce-extensions are **not** vendored or fetched; CMake `add_subdirectory`s them from `~/SDKs/JUCE` and `~/SDKs/clap-juce-extensions` and fails at configure time if either lacks a `CMakeLists.txt`. Override with `-DJUCE_DIR=...` / `-DCLAP_JUCE_EXTENSIONS_DIR=...`. Pinned versions are noted at the top of `CMakeLists.txt` (JUCE tag 7.0.9, clap-juce-extensions `54d83e1`).

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target GenerativeMidi_VST3
cmake --build build --target GenerativeMidi_CLAP
```

Artefacts land in `build/GenerativeMidi_artefacts/Release/{VST3,CLAP}`. The JUCE and clap-juce-extensions trees are added `EXCLUDE_FROM_ALL`, so only what the plugin targets depend on gets built from them.

## Testing

`tests/clap_test_host.cpp` is the scripted CLAP test host: a small headless host (no GUI, no audio device) that `dlopen`s the built `.clap` from the build tree, runs a fixed MIDI scenario through `process()` and checks the output. It never touches `~/.clap`. Registered as the CTest test `clap_host_eighths`.

```
cmake --build build --target GenerativeMidi_CLAP clap_test_host
ctest --test-dir build --output-on-failure
./build/clap_test_host build/GenerativeMidi_artefacts/Release/CLAP/GenerativeMidi.clap   # verbose, per-check output
```

It currently asserts: no audio input and one silent stereo output; a C4 held for eight 8ths comes out as C D E F G A B C5 on the 8th grid (the host sends no transport, so 120 BPM, 12000 samples per 8th at 48 kHz) with each note sounding 90% of an 8th; a CC passes through unchanged. When the behaviour changes, change the scenario and expectations in that file rather than adding a second host. Final confirmation in Bitwig is manual, since it can't be driven headlessly.

## Architecture

- Only `VST3` is declared in `juce_add_plugin(FORMATS VST3)`. JUCE 7 has no CLAP format, so `clap_juce_extensions_plugin(TARGET GenerativeMidi ...)` wraps the same target as a CLAP (`GenerativeMidi_CLAP`). Format-specific behaviour should live behind JUCE's `AudioProcessor` API, not in per-format code.
- `GenerativeMidiProcessor` (`src/PluginProcessor.*`) is the `AudioProcessor`; `GenerativeMidiEditor` (`src/PluginEditor.*`) is its UI, created in `createEditor()`. `createPluginFilter()` at the bottom of `PluginProcessor.cpp` is the JUCE entry point.
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
