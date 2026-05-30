# Octatrack reference → roadmap mapping

Inventory of Elektron **Octatrack MkII** features and where each lands in Cold's
roadmap ([../TASKS.md](../TASKS.md)). The Octatrack is Elektron's flagship
performance sampler; these features push Cold beyond the Digitakt set (M8+).
Some OT features are adapted to the plugin/Logic context — those are called out.

## Performance / scenes

| Octatrack feature | Cold | Milestone |
| --- | --- | --- |
| **Scenes** (per-param snapshots) | Yes — "in-scene" param assignment | M11 |
| **Crossfader morph A↔B** | Yes — one control morphs whole-mix snapshots | M11 |
| Parts (per-bank sound snapshots) | Yes — 4 per bank, instant switch | M14 |
| Arranger / song mode | Yes — chain patterns (host-synced) | M14 |

> The crossfader is the OT signature. In Cold it doubles as a first-class
> **MCP / voice** target, and pairs naturally with the continuously morphing
> filter (M2) and mod matrix (M3).

## Sampling

| Octatrack feature | Cold | Milestone |
| --- | --- | --- |
| Slice grid (up to 64 slices) | Yes | M8 |
| Per-step slice lock | Yes — slice p-lock | M8 |
| Live recording / track recorders | Yes — input recorder | M12 |
| Resampling (internal output) | Yes — resample track/master bus | M12 |
| Timestretch (pitch-independent) | Yes — timestretch playback mode | M13 |
| **Flex** (RAM) vs **Static** (disk-streamed) | Yes — both sample sources | M13 |
| Rate / start / length / loop per slot | Yes (start/length M4, slices M8) | M4/M8 |

## Sound shaping

| Octatrack feature | Cold | Milestone |
| --- | --- | --- |
| Multimode filter | **Continuous morph LP→BP→HP→Notch** | M2 |
| FX1 + FX2 per track (insert chain) | Yes — two ordered, p-lockable slots | M10 |
| Overdrive / bit / sample-rate reduction | Yes — core FX | M10 |
| Comb / delay / reverb / compressor / chorus | Yes — time/mod FX | M10 |

## Modulation

| Octatrack feature | Cold | Milestone |
| --- | --- | --- |
| Two/three LFOs per track | Dual LFO per track | M9 |
| LFO designer (custom waveform) | Yes — user-drawn multi-step LFO | M9 |
| LFO trig modes (free/trig/hold/one) | Yes | M9 |
| LFO → filter type (morph) | Yes (Cold extension) | M3 |

## Sequencer

| Octatrack feature | Cold | Milestone |
| --- | --- | --- |
| 16-step sequencer, host sync | Yes | M6 |
| Parameter locks | Yes | M6 |
| Conditional / logic trigs | Yes — probability, A:B, fill, neighbor… | M15 |
| Micro-timing + swing | Yes | M15 |
| Retrig | Yes | M15 |
| Sample-lock per trig | Backlog | later |
| Dedicated MIDI sequencer tracks | Backlog | later |

## Routing / IO

| Octatrack feature | Cold | Milestone |
| --- | --- | --- |
| Separate outputs | Multi-out (per-track buses for Logic) | M14 |
| Cue / preview output | Backlog | later |
| **Thru machine** (process live input) | Backlog — effects-processor mode | later |
| **Pickup machine** (looper) | Backlog | later |
| Neighbor machine | Backlog | later |

## Plugin-context adaptations (deliberate)

- **DAW does the macro-arrangement.** Logic already arranges audio, so the
  arranger/song mode (M14) is lighter than the OT's — it's for pattern chaining,
  not a replacement for the DAW timeline.
- **Cue output** is low priority: in a DAW you monitor via the host, not a
  separate headphone cue bus.
- **Thru / Pickup machines** (use the unit as a live FX processor / looper) are
  backlogged — in a plugin you'd typically insert Cold on the target track
  instead. They become interesting once live sampling (M12) exists.
- **Polyphony** (Cold-wide deviation) applies on top of all of the above: every
  track is a polyphonic voice chain, unlike the mono-per-track OT.
