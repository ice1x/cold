# Cold

A **layering drum sampler** plugin for macOS — **AU / VST3 / Standalone**, written
in C++20 with [JUCE](https://juce.com/). Built for **Logic Pro on Apple Silicon**.

Cold takes its sound-design ideas from the Elektron Digitakt (per-trig sample
modulation, a morphing filter, a flexible LFO) and its workflow from NI Battery
(a grid of cells, each a stack of layered samples). It is not affiliated with
either — see [Trademarks](#trademarks).

> **Status:** in active development. The engine is feature-rich and covered by
> tests; the editor UI is being redesigned for clarity. See [TASKS.md](TASKS.md)
> for the roadmap.

## Features

**Cells and layers**
- 16 cells (a 4×4 pad grid), each holding up to **4 layered samples**.
- Layer modes per cell: **Single**, **Stack** (all layers on one hit),
  **Round-Robin** (cycle layers hit to hit) and **Velocity** (layer by how hard you play).
- Per layer: gain, tune, pan, reverse, mute / solo, its **own amp envelope**, its own
  filter, and dedicated low-cut / high-cut.
- Every layer is shown at once as a strip with its waveform, envelope mask and
  draggable start / end.
- Choke (mute) groups, MIDI-channel or **Note Map** addressing, click-to-audition pads.

**Sound shaping (per cell)**
- **Morphing filter** that blends continuously LP → BP → HP → Notch.
- Curved DAHDSR amp envelope, pitch envelope (drum "blip"), mod envelope → filter.
- LFO (sine / triangle / saw / square / random / **custom-drawn**, free / trig /
  hold / one-shot, tempo sync) → cutoff, **filter type**, pitch, **sample select**.
- Velocity → pitch / cutoff, humanize (random pitch and level per hit).
- Insert FX: lo-fi (bit / rate reduction), saturation, compressor; level and pan.

**Mix**
- Delay and reverb sends (tempo-synced delay).
- Master bus: tilt EQ, glue compressor, stereo width, brickwall limiter.
- MIDI pitch bend and mod wheel → cutoff.

**Import**
- Loads `wav` / `aiff` / `mp3`, a whole folder at once, or an **NI Battery 3 `.kit`**
  (samples plus per-cell envelope, tune, pan and key mapping).

Planned: a step sequencer with parameter locks, slice grids, and an MCP control
surface for LLM / voice control (see [docs/MCP.md](docs/MCP.md)).

## Requirements

- macOS on Apple Silicon (arm64).
- Xcode command-line tools.
- [CMake](https://cmake.org/) ≥ 3.22 and [Ninja](https://ninja-build.org/).
- JUCE and Catch2 are fetched automatically by CMake — nothing to install by hand.

## Build

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This produces the AU (`.component`), VST3 (`.vst3`) and Standalone (`.app`) under
`build/src/Cold_artefacts/Release/`.

## Install (Logic Pro)

Logic only loads the AU. Copy it into your user plug-ins folder, re-sign it (copying
invalidates the ad-hoc signature) and validate:

```sh
cp -R build/src/Cold_artefacts/Release/AU/Cold.component ~/Library/Audio/Plug-Ins/Components/
codesign --force --deep -s - ~/Library/Audio/Plug-Ins/Components/Cold.component
auval -v aumu Cld1 Ice1
```

Then add **Cold** as an instrument on a software-instrument track.

## Test

```sh
ctest --test-dir build --output-on-failure
```

The DSP core (`src/dsp/`) is framework-agnostic and unit-tested with plain buffers;
the processor and editor are covered by end-to-end tests. CI builds and tests every
pull request on a GitHub-hosted Apple Silicon runner and enforces `clang-format`.

## Contributing

By submitting a pull request you agree to section 8 of the [license](LICENSE)
(contributions may be included in commercial licenses). Work is test-driven: add a failing Catch2 test, make it pass, then verify in a DAW.
Keep DSP classes free of JUCE so they stay testable without a plugin host. Design
notes live in [`docs/`](docs/):

- [ARCHITECTURE.md](docs/ARCHITECTURE.md) — signal flow and engine design.
- [DSP_NOTES.md](docs/DSP_NOTES.md) — filter, envelope and LFO math.
- [DIGITAKT_REFERENCE.md](docs/DIGITAKT_REFERENCE.md) and
  [OCTATRACK_REFERENCE.md](docs/OCTATRACK_REFERENCE.md) — feature references.

## License

Cold is **source-available** under the [Cold Indie License 1.0](LICENSE) — not an
OSI open-source license. In short:

- **Free** for independent musicians, producers and small studios — anyone whose
  annual revenue is under USD 1,000,000, on projects with a budget under USD 250,000 —
  including commercial work.
- **Your music is yours.** No rights are claimed in audio you make with Cold.
- **Forks are welcome as source code**, free of charge and under the same license.
  Selling Cold, distributing compiled builds of it, or bundling it in another product
  needs written permission.
- Larger companies need a **commercial license** — open an
  [issue](https://github.com/ice1x/cold/issues) to ask.
- Contributions are licensed to the maintainer so they can be included in commercial
  licenses (see section 8 of the license).

The [LICENSE](LICENSE) file is the binding text; this summary is for convenience.

Cold is built on **JUCE**, which is licensed separately (see
[juce.com/legal](https://juce.com/legal/)); building or distributing Cold also means
complying with JUCE's terms.

## Trademarks

Digitakt, Octatrack and Elektron are trademarks of Elektron Music Machines. Battery
and Native Instruments are trademarks of Native Instruments. Logic Pro is a trademark
of Apple Inc. Cold is an independent project and is not affiliated with, endorsed by,
or sponsored by any of them; the names are used only to describe features and file
compatibility.
