# Cold — task roadmap

TDD-first. Every feature: write failing Catch2 tests → implement → integrate →
verify (Standalone, then Logic for in-DAW tasks). Mark `[x]` on completion.
Task IDs increment monotonically (`000001` …) — do not renumber.

Distinctive goals beyond a plain Digitakt clone:
- **Polyphony** (chords with samples).
- **Looping** (Digitakt-style) **and** classic **repitch/varispeed** mode.
- **Continuously morphing filter** with the **filter type as a modulation target**.
- **MCP control** for LLM/voice control + tuning recommendations.

---

## M0 — Scaffold & toolchain (empty AU loads in Logic)

- [x] 000001 — Set up top-level CMake with JUCE (FetchContent/submodule), C++20, arm64 macOS
- [x] 000002 — Configure `juce_add_plugin`: AU + VST3 + Standalone instrument, set `Cold` name + manufacturer/plugin codes
- [x] 000003 — Wire Catch2 + CTest test target via FetchContent
- [x] 000004 — Add build/test/lint helper scripts (the `dt-*` skills) and `.clang-format` / `.clang-tidy`
- [ ] 000005 — Implement minimal empty `AudioProcessor`/`Editor`; build, pass `auval`, load empty `Cold` in Logic Pro  _(builds + `auval` PASSED; manual Logic load pending owner)_

## M1 — Minimal playable sampler PoC (FIRST USABLE BUILD)

- [ ] 000006 — Tests: sample file decode (wav/aiff/mp3 → float buffer + sample rate)
- [ ] 000007 — Implement `io/SampleLoader` via `juce::AudioFormatManager` (wav/aiff/mp3)
- [ ] 000008 — Tests: one-shot voice playback (read position, pitch ratio from MIDI note, end-of-sample)
- [ ] 000009 — Implement `dsp/SampleVoice` repitch/varispeed playback + MIDI-note trigger
- [ ] 000010 — Tests: looping modes (one-shot / forward loop / ping-pong, loop speed follows pitch)
- [ ] 000011 — Implement looping (loopStart/loopEnd, modes) in `SampleVoice`
- [ ] 000012 — Tests: polyphonic voice allocation (chords, voice stealing)
- [ ] 000013 — Implement `dsp/Engine` voice pool + polyphony (play chords with one sample)
- [ ] 000014 — Tests: amp envelope (ADSR shape, sample-accurate stages, retrigger)
- [ ] 000015 — Implement `dsp/AmpEnvelope` applied per voice
- [ ] 000016 — Expose params (load sample, playback mode, loop, amp env, gain, polyphony) via APVTS + minimal generic UI
- [ ] 000017 — Logic smoke test: load a WAV, play a chord, hear enveloped polyphonic playback + looping

## M2 — Continuously morphing multimode filter

- [ ] 000018 — Tests: SVF LP/BP/HP/Notch outputs at given cutoff/resonance
- [ ] 000019 — Implement `dsp/MorphFilter` SVF core (TPT/Zavalishin, simultaneous outputs)
- [ ] 000020 — Tests: continuous type morph (smooth blend across LP→BP→HP→Notch)
- [ ] 000021 — Implement morphable `filterType` parameter (Ableton-style blend) + param smoothing
- [ ] 000022 — Integrate filter into voice path; expose cutoff/resonance/type params
- [ ] 000023 — Logic smoke test: sweep cutoff and morph type, confirm artifact-free

## M3 — LFO + modulation (filter type as a destination)

- [ ] 000024 — Tests: LFO shapes/rate/depth/output range (+ host-sync division)
- [ ] 000025 — Implement `dsp/Lfo` (sine/tri/saw/square/random)
- [ ] 000026 — Tests: modulation routing LFO→cutoff and LFO→filter-type
- [ ] 000027 — Implement modulation matrix with **filter type as a destination**
- [ ] 000028 — Expose LFO params + destination select; Logic smoke test of both routings

## M4 — Sample bank

- [ ] 000029 — Tests: sample bank model (slots, load/replace, metadata)
- [ ] 000030 — Implement `io/SampleBank` (multiple samples)
- [ ] 000031 — Sample browser UI: load a folder, assign samples to slots
- [ ] 000032 — Tests: per-slot sample start/length
- [ ] 000033 — Implement sample start/length controls (per slot)
- [ ] 000034 — Logic smoke test: load a bank, switch slots, trigger each

## M5 — Multi-track engine (8 Digitakt-style tracks, each polyphonic)

- [ ] 000035 — Tests: 8-track engine (per-track sample + amp + filter + LFO + voice pool)
- [ ] 000036 — Implement 8-track architecture (track = full polyphonic voice chain)
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

## Backlog (later; assign IDs when scheduled)

Patterns & chains, sample chop/slice, retrig, trig conditions/probability,
velocity & micro-timing, overdrive/bit-reduction, sample-rate reduction,
separate filter envelope, send FX (delay/reverb), per-track overdrive,
time-stretch playback mode, sample recording/resampling, preset management,
higher-quality interpolation (Hermite/sinc).
