# Cold

A software clone of the **Elektron Digitakt**, built as an audio plugin
(**AU / VST3 / Standalone**) in C++ with [JUCE](https://juce.com/).

The primary target is an **Apple Silicon (M1) MacBook running Logic Pro**.
Development is incremental: the goal is a usable plugin as early as possible,
then iterative growth toward Digitakt feature parity.

> Status: **early scaffold / PoC**. See [TASKS.md](TASKS.md) for the live roadmap.

## What it is (and the plan)

A sampler in the spirit of the Digitakt:

- Load a bank of samples (`wav`, `aiff`, `mp3`).
- Trigger them via MIDI — **polyphonically** (play chords with samples, unlike
  the mono-per-track Digitakt).
- Sample playback with **looping** (Digitakt-style loop points) and a classic
  **repitch / varispeed** mode where pitch changes playback speed (old-sampler
  behavior).
- Shape each with an **amp envelope** and a **multimode filter**.
- The filter type **morphs continuously** between low-pass / band-pass /
  high-pass / notch (Ableton-style), and the **filter type is itself a
  modulation destination** for the LFO.
- **LLM/voice control via MCP**: an MCP server lets AI tools control parameters,
  load samples, and recommend tuning by voice.
- Grow toward 8 tracks, a step sequencer with parameter locks, and the rest of
  the Digitakt feature set.

Start small, ship often. The full milestone breakdown lives in
[TASKS.md](TASKS.md).

## Requirements

- macOS on Apple Silicon (arm64).
- Xcode command-line tools (`clang`, `clang-format`, `clang-tidy`).
- [CMake](https://cmake.org/) ≥ 3.22 and [Ninja](https://ninja-build.org/).
- Logic Pro (for the in-DAW smoke tests; **AU is the only format Logic loads**).
- JUCE and Catch2 are fetched automatically by CMake — no manual install.

## Build

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This produces the AU (`.component`), VST3 (`.vst3`) and Standalone (`.app`)
artifacts under `build/`.

## Install (into Logic)

Copy the AU into the user plug-ins folder and let macOS re-scan it:

```sh
cp -R build/**/Cold_artefacts/**/Cold.component ~/Library/Audio/Plug-Ins/Components/
auval -a | grep -i cold        # confirm it's registered
```

Then launch Logic Pro and add **Cold** as an instrument on a track.

## Test

```sh
ctest --test-dir build --output-on-failure
```

## Development workflow

This project follows **TDD**: write failing Catch2 tests first, implement the
minimum to pass, then verify in Logic. Pure-DSP classes are kept
framework-agnostic so they can be tested without a plugin host.

See [CLAUDE.md](CLAUDE.md) for the full workflow, and `docs/` for design notes:

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — signal flow & engine design.
- [docs/DIGITAKT_REFERENCE.md](docs/DIGITAKT_REFERENCE.md) — Digitakt features mapped to milestones.
- [docs/DSP_NOTES.md](docs/DSP_NOTES.md) — filter / envelope / LFO math.
- [docs/MCP.md](docs/MCP.md) — MCP control surface for LLM/voice control & tuning.

## License

See [LICENSE](LICENSE).
