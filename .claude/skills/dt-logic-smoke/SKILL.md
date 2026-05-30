---
name: dt-logic-smoke
description: Manual checklist for verifying a Cold feature inside Logic Pro on the M1 Mac. Use for the milestone in-DAW smoke-test tasks, since DAW behavior cannot be automated.
---

# dt-logic-smoke

DAW behavior can't be auto-tested, so each milestone has a **manual** Logic Pro
verification step. Run `dt-build` + `dt-install` first (AU must pass `auval`).
Report the actual observed result — never claim a Logic test passed without
running it.

## Setup (once per build)

1. Relaunch Logic Pro so it re-scans the AU (`dt-install` clears the cache).
2. New project → Software Instrument track → insert **Cold** (AU instrument).
3. Confirm the plugin window opens without errors.

## Golden-path checklist (adapt per feature)

- [ ] Plugin loads and UI renders.
- [ ] Drag/load a sample (wav, then aiff, then mp3).
- [ ] Play a single MIDI note → sample plays at correct pitch.
- [ ] Play a **chord** → polyphonic, no stuck/cut voices (Cold deviation).
- [ ] Looping on → sustained note loops cleanly; pitch changes loop speed (repitch mode).
- [ ] Amp envelope: attack/decay audibly shape the sound.
- [ ] Filter: sweep cutoff and **morph the filter type** → smooth, no zipper/clicks.
- [ ] LFO → cutoff and LFO → **filter type** modulate audibly.

## Edge cases

- [ ] Very short / very long samples.
- [ ] Rapid note retriggers (no clicks, correct envelope reset).
- [ ] Max polyphony / voice stealing under a dense chord run.
- [ ] Save/close/reopen the Logic project → state restores (sample + params).
- [ ] Automation of cutoff/type from Logic behaves smoothly.
- [ ] Change Logic sample rate / buffer size → no crashes, correct pitch.

## On failure

Capture: what you did, what you heard/saw vs. expected, any Logic error. Fix at
the DSP/processor level with a regression test in `dt-test`, not in the DAW.
