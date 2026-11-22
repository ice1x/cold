# Architecture

Cold is a polyphonic, layering sampler plugin. This document describes
the signal flow and the engine design.

## Layering

```
            ┌─────────────────────────────────────────────┐
  Logic /   │  PluginProcessor (JUCE AudioProcessor)        │
  host  ───▶│   - APVTS parameters                          │
  MIDI      │   - MIDI in → Engine                          │
            │   - control server (OSC/WebSocket) ◀── MCP    │
            └───────────────────┬──────────────────────────┘
                                │
                       ┌────────▼────────┐
                       │   dsp/Engine     │  per-track chains + voice alloc
                       └───┬─────────┬────┘
                  track 0  │   ...   │  track N
                ┌──────────▼──┐    ┌─▼───────────┐
                │  Track       │    │  Track       │
                │  (poly)      │    │  (poly)      │
                └──────────────┘    └──────────────┘
```

The **DSP layer** (`dsp/*`) has **no JUCE dependency** so it is unit-testable with
plain float buffers. JUCE-only concerns (file decode, parameter tree, host glue,
control server) live in `PluginProcessor` and `io/*`.

## Signal flow (per voice)

```
sample read ──▶ amp envelope ──▶ morph filter ──▶ voice out ──▶ track sum ──▶ out
   ▲                                  ▲
   │ pitch / loop / playback mode     │ cutoff, resonance, type
   └──────────────── modulation matrix (LFO, env) ──────────────┘
```

## Voices & polyphony

**Each track is polyphonic**: a track
owns a pool of `SampleVoice`s and an allocator that assigns an idle/stolen voice to
each incoming note. This lets you play chords with a single sample. Polyphony count
is a per-track parameter (1 = mono as a special case).

### SampleVoice

Holds a read position into the track's sample buffer and renders one note.
Responsibilities:

- **Pitch**: MIDI note → playback ratio relative to a root note.
- **Playback modes** (per track):
  - *Repitch / varispeed* (classic sampler): pitch changes playback speed via
    resampling; a loop plays faster/slower with pitch. This is the default.
  - (Future) *Time-stretch / fixed*: pitch independent of speed.
- **Looping**: one-shot, forward loop, or ping-pong, with `loopStart`/`loopEnd`
  points. In repitch mode the loop speed follows pitch.
- **Sample window**: `start` / `length`.
- **Envelope**: amp ADSR applied to its output.
- **Filter**: a per-voice `MorphFilter` instance.

Interpolation for fractional read positions uses linear (PoC) → later Hermite/
sinc for quality.

## Modulation

A small modulation matrix routes sources (LFO, amp/filter envelope) to
destinations. Required destinations include **filter cutoff** and, distinctively,
**filter type** (the continuous morph parameter) — so an LFO can sweep the filter
character itself. See [DSP_NOTES.md](DSP_NOTES.md).

## Tracks & engine

`Engine` owns N `Track`s. A track = sample + playback config + amp env + morph
filter + LFO + voice pool. MIDI routing maps note/channel to a track
(one MIDI channel per track) — implemented in M5.

## Parameters

All host-automatable state lives in a JUCE `AudioProcessorValueTreeState`
(APVTS), the single source of truth shared by the UI, the host, and the MCP
control server.

## MCP control surface

The processor hosts a lightweight control server that exposes parameter get/set,
sample load, state query, and note trigger. A companion MCP server bridges these to
MCP tool calls so LLM/voice tools can drive and tune the plugin. See
[MCP.md](MCP.md).
