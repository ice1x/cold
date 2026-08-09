# Cold — task roadmap

TDD-first. Every feature: write failing Catch2 tests → implement → integrate →
verify (Standalone, then Logic for in-DAW tasks). Mark `[x]` on completion.
Task IDs increment monotonically (`000001` …) — do not renumber.

Distinctive goals beyond a plain Digitakt clone:
- **Polyphony** (chords with samples).
- **Looping** (Digitakt-style) **and** classic **repitch/varispeed** mode.
- **Continuously morphing filter** with the **filter type as a modulation target**.
- **Octatrack-grade performance & sound design**: **Scenes + crossfader morph**
  (the OT signature), **slice grids** with per-step slice locks, **dual LFO**
  with a custom **LFO designer**, **two insert FX per track**, **live
  resampling**, **timestretch**, and **Parts**.
- **MCP control** for LLM/voice control + tuning recommendations (the crossfader,
  scenes, and morph filter are first-class MCP modulation targets).

> The Octatrack-inspired milestones (M8–M15) intentionally push Cold past the
> Digitakt feature set toward Elektron's flagship sampler. See
> [docs/OCTATRACK_REFERENCE.md](docs/OCTATRACK_REFERENCE.md) for the
> feature → milestone mapping and the deliberate plugin-context adaptations.

---

## M0 — Scaffold & toolchain (empty AU loads in Logic)

- [x] 000001 — Set up top-level CMake with JUCE (FetchContent/submodule), C++20, arm64 macOS
- [x] 000002 — Configure `juce_add_plugin`: AU + VST3 + Standalone instrument, set `Cold` name + manufacturer/plugin codes
- [x] 000003 — Wire Catch2 + CTest test target via FetchContent
- [x] 000004 — Add build/test/lint helper scripts (the `dt-*` skills) and `.clang-format` / `.clang-tidy`
- [ ] 000005 — Implement minimal empty `AudioProcessor`/`Editor`; build, pass `auval`, load empty `Cold` in Logic Pro  _(builds + `auval` PASSED; manual Logic load pending owner)_

## M1 — Minimal playable sampler PoC (FIRST USABLE BUILD)

- [x] 000006 — Tests: sample file decode (wav/aiff/mp3 → float buffer + sample rate)
- [x] 000007 — Implement `io/SampleLoader` via `juce::AudioFormatManager` (wav/aiff/mp3)
- [x] 000008 — Tests: one-shot voice playback (read position, pitch ratio from MIDI note, end-of-sample)
- [x] 000009 — Implement `dsp/SampleVoice` repitch/varispeed playback + MIDI-note trigger
- [x] 000010 — Tests: looping modes (one-shot / forward loop / ping-pong, loop speed follows pitch)
- [x] 000011 — Implement looping (loopStart/loopEnd, modes) in `SampleVoice`
- [x] 000012 — Tests: polyphonic voice allocation (chords, voice stealing)
- [x] 000013 — Implement `dsp/Engine` voice pool + polyphony (play chords with one sample)
- [x] 000014 — Tests: amp envelope (ADSR shape, sample-accurate stages, retrigger)
- [x] 000015 — Implement `dsp/AmpEnvelope` applied per voice
- [x] 000016 — Expose params (load sample, playback mode, loop, amp env, gain, polyphony) via APVTS + minimal generic UI
- [ ] 000017 — Logic smoke test: load a WAV, play a chord, hear enveloped polyphonic playback + looping

## M2 — Continuously morphing multimode filter

- [x] 000018 — Tests: SVF LP/BP/HP/Notch outputs at given cutoff/resonance
- [x] 000019 — Implement `dsp/MorphFilter` SVF core (TPT/Zavalishin, simultaneous outputs)
- [x] 000020 — Tests: continuous type morph (smooth blend across LP→BP→HP→Notch)
- [x] 000021 — Implement morphable `filterType` parameter (Ableton-style blend) + param smoothing
- [x] 000022 — Integrate filter into voice path; expose cutoff/resonance/type params
- [ ] 000023 — Logic smoke test: sweep cutoff and morph type, confirm artifact-free

## M3 — LFO + modulation (filter type as a destination)

- [x] 000024 — Tests: LFO shapes/rate/depth/output range (+ host-sync division)
- [x] 000025 — Implement `dsp/Lfo` (sine/tri/saw/square/random)
- [x] 000026 — Tests: modulation routing LFO→cutoff and LFO→filter-type
- [x] 000027 — Implement modulation matrix with **filter type as a destination**
- [~] 000028 — Expose LFO params + destination select; Logic smoke test of both routings  _(params + routing shipped; in-DAW smoke test pending owner)_

## M4 — Sample bank

- [x] 000029 — Tests: sample bank model (slots, load/replace, metadata)
- [x] 000030 — Implement `io/SampleBank` (multiple samples)
- [x] 000031 — Sample browser UI: load a folder, assign samples to slots
- [x] 000032 — Tests: per-slot sample start/length
- [x] 000033 — Implement sample start/length controls (per slot)
- [ ] 000034 — Logic smoke test: load a bank, switch slots, trigger each

## M5 — Multi-track engine (8 Digitakt-style tracks, each polyphonic)

- [x] 000035 — Tests: 8-track engine (per-track sample + amp + filter + LFO + voice pool)
- [x] 000036 — Implement 8-track architecture (track = full polyphonic voice chain)
- [ ] 000037 — Implement MIDI-channel→track routing (Digitakt-style)
- [ ] 000038 — Per-track UI with track selector
- [ ] 000039 — Logic smoke test: 8 tracks playable via MIDI channels

## M6 — Step sequencer + parameter locks (Digitakt core)

- [ ] 000040 — Tests: 16-step sequencer (steps, host-transport sync, trigs)
- [ ] 000041 — Implement step sequencer synced to Logic transport
- [ ] 000042 — Tests: parameter locks (per-step p-locks)
- [ ] 000043 — Implement p-locks
- [ ] 000044 — Sequencer UI (16 step buttons, per-track patterns)
- [ ] 000045 — Logic smoke test: sequence runs in sync with Logic transport

## M7 — MCP control surface (LLM / voice control + tuning recommendations)

- [ ] 000046 — Tests: in-plugin control server message handling (get/set param, thread-safe, lock-free to audio thread)
- [ ] 000047 — Implement plugin control server (OSC/WebSocket on localhost) bound to APVTS + Engine
- [ ] 000048 — Scaffold `mcp/` TypeScript MCP server (`@modelcontextprotocol/sdk`) with build/run
- [ ] 000049 — Implement MCP tools: list/get/set parameters, get_state
- [ ] 000050 — Implement MCP tools: load_sample, trigger_note
- [ ] 000051 — Implement `recommend_tuning` (return state for LLM reasoning + apply path)
- [ ] 000052 — End-to-end test: drive Cold in Logic by voice via MCP; verify param changes + tuning recommendation

---

# Octatrack-inspired milestones (push beyond the Digitakt)

## M8 — Sample slicing & chops (Octatrack-style slice grids)

- [ ] 000053 — Tests: slice-grid model (up to 64 slices, equal/transient auto-slice, manual edit, per-slice start/length/loop)
- [ ] 000054 — Implement slice grid on the sample (auto + manual), persisted per slot
- [ ] 000055 — Tests: slice playback in `SampleVoice` (trig a slice by index/MIDI note)
- [ ] 000056 — Implement slice playback + per-step **slice p-lock** (each trig selects a slice)
- [ ] 000057 — Slice editor UI (waveform + draggable slice markers)
- [ ] 000058 — Logic smoke test: chop a drum break, sequence different slices per step

## M9 — Dual LFO + LFO designer

- [ ] 000059 — Tests: second LFO per track + trig modes (free / trig / hold / one-shot)
- [ ] 000060 — Implement a 2nd `dsp/Lfo` instance per track wired into the mod matrix
- [ ] 000061 — Tests: custom multi-step LFO shape (designer model + interpolation between steps)
- [ ] 000062 — Implement **LFO designer** (user-drawn multi-step waveform as an LFO source)
- [ ] 000063 — Dual-LFO + designer UI; Logic smoke test of two independent routings

## M10 — Per-track insert FX chain (FX1 + FX2)

- [ ] 000064 — Tests: FX slot abstraction (process block, bypass, param set, latency report)
- [ ] 000065 — Implement core FX: overdrive, bit-reduction, sample-rate reduction, EQ
- [ ] 000066 — Implement time/mod FX: chorus/flanger, comb, host-synced delay, reverb, compressor
- [ ] 000067 — Two ordered insert FX slots per track, fully **p-lockable**
- [ ] 000068 — FX UI; Logic smoke test: stack two FX per track and p-lock them

## M11 — Scenes + crossfader morph (Octatrack signature feature)

- [ ] 000069 — Tests: scene snapshot model (capture/restore any "in-scene" param across all tracks)
- [ ] 000070 — Implement scene store with per-param **scene assignment** + two active slots (A / B)
- [ ] 000071 — Tests: crossfader interpolation A↔B (continuous, per-param curve, smoothed)
- [ ] 000072 — Implement the **crossfader morph engine** (one control morphs whole-mix snapshots)
- [ ] 000073 — Expose crossfader + A/B scene select as automatable params **and** MCP modulation targets
- [ ] 000074 — Scenes/crossfader UI; Logic smoke test: morph a full mix via crossfader automation

## M12 — Live sampling & resampling (track recorders)

- [ ] 000075 — Tests: record buffer (capture, length cap, one-shot/loop, input level/peak)
- [ ] 000076 — Implement input recorder (sidechain/aux input → sample slot), trig-armed
- [ ] 000077 — Tests: internal resampling (capture an engine/track/master bus to a buffer)
- [ ] 000078 — Implement **resample-internal** (track or master output → slot), sequencer-triggerable
- [ ] 000079 — Recorder UI; Logic smoke test: resample a track's output, immediately play it back

## M13 — Timestretch & Flex/Static sample sources

- [ ] 000080 — Tests: timestretch playback (time/beat-based, pitch-independent) vs repitch mode
- [ ] 000081 — Implement timestretch playback mode in `SampleVoice` (host-tempo lock option)
- [ ] 000082 — Tests: streamed (**Static**, disk) vs RAM (**Flex**) sample sources for long files
- [ ] 000083 — Implement disk-streaming sample source for long samples (lock-free read to audio thread)
- [ ] 000084 — Logic smoke test: timestretch a loop to host tempo; stream a multi-minute file

## M14 — Parts, song arranger & multi-out

- [ ] 000085 — Tests: **Parts** (per-bank snapshot of machine/sample/FX/scene assignments)
- [ ] 000086 — Implement Parts (4 per bank) with instant switch + reload
- [ ] 000087 — Tests: pattern **arranger** (chain patterns into a song with repeats/length)
- [ ] 000088 — Implement arranger / song mode (host-transport synced)
- [ ] 000089 — Implement **multi-output** (per-track separate output buses for Logic)
- [ ] 000090 — Logic smoke test: switch Parts live; run an arrangement; route tracks to separate channels

## M15 — Sequencer power (conditional trigs, micro-timing, retrig)

- [ ] 000091 — Tests: **conditional/logic trigs** (probability %, A:B ratio, fill, first/not-first, neighbor, prev)
- [ ] 000092 — Implement trig conditions in the sequencer
- [ ] 000093 — Tests: per-trig **micro-timing** (± tick nudge) + swing
- [ ] 000094 — Implement micro-timing + swing
- [ ] 000095 — Tests: **retrig** (rate division, count/length, velocity ramp)
- [ ] 000096 — Implement retrig
- [ ] 000097 — Sequencer UI for conditions/retrig; Logic smoke test of conditional + retrig patterns

## Backlog (later; assign IDs when scheduled)

Pattern chains, separate filter envelope, preset/kit management, higher-quality
interpolation (Hermite/sinc), per-trig **sample-lock**, **Thru / effects-processor
mode** (process incoming audio), **Pickup / looper machines**, master-track FX bus,
dedicated **MIDI sequencer tracks**, **neighbor machine**, cue/preview output.
