---
name: cold-feature
description: End-to-end TDD workflow for adding a Cold feature (next TASKS.md item) — failing tests, implement, integrate, build, verify in Logic, lint, commit on a feature branch. Use when implementing any roadmap task.
---

# cold-feature

The standard loop for delivering a roadmap item from [TASKS.md](../../../TASKS.md).
Encodes the project's TDD + git rules so every feature is consistent.

## Steps

1. **Pick the task** — the next unchecked `000xxx` in `TASKS.md`. Create a branch:
   `git checkout -b feature/<short-desc>` (never work on `main`).

2. **Red (unit)** — write failing Catch2 **unit** tests first in `tests/unit/`,
   targeting the framework-agnostic class in `dsp/` or `io/`. Tag them `[unit]`.
   Run `cold-test`; confirm they fail for the right reason.

3. **Green** — implement the minimum to pass. Keep DSP JUCE-free; put host/file/
   control-server glue in `src/` or `io/`. Re-run `cold-test` to green.

4. **Cover (integration + e2e)** — a feature is **not done with only unit tests**.
   Add the higher levels (see `cold-test` for the level/target/label map):
   - **Integration** (`tests/integration/`, tag `[integration]`): several units
     in a realistic workflow — e.g. multi-format sample-bank load, file↔stream
     parity, an end-to-end DSP render.
   - **E2E** (`tests/e2e/`, tag `[e2e]`): once the feature touches the plugin,
     wire it into `PluginProcessor` / `Engine` / APVTS and drive the real
     `ColdAudioProcessor` (prepare → processBlock with MIDI → state/editor),
     asserting the audible/observable result.
   Reuse `tests/support/SignalFixtures.h` for deterministic audio.

5. **Build & install** — `cold-build`, then `cold-install` for in-DAW tasks.

6. **Verify** — Standalone for quick listening; `cold-logic-smoke` for the milestone's
   manual Logic check. Report real results.

7. **Gate** — `cold-lint` clean and full `cold-test` green.

8. **Commit & push** — `git reset HEAD`, `git add` only intended files, commit with
   a **title + descriptive body** (no Claude co-author trailer), then
   `git push -u origin feature/<short-desc>`. Provide the PR creation link.

9. **Tick the box** — mark the task `[x]` in `TASKS.md` (its own commit or part of
   the feature commit).

## Reminders

- No code without tests. No commit/push to `main`.
- Audio thread stays allocation-free and lock-free.
- Honor the Cold deviations: polyphony, repitch/looping modes, morphing filter
  with filter-type as a mod target, MCP control.
