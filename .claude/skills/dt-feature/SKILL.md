---
name: dt-feature
description: End-to-end TDD workflow for adding a Cold feature (next TASKS.md item) — failing tests, implement, integrate, build, verify in Logic, lint, commit on a feature branch. Use when implementing any roadmap task.
---

# dt-feature

The standard loop for delivering a roadmap item from [TASKS.md](../../../TASKS.md).
Encodes the project's TDD + git rules so every feature is consistent.

## Steps

1. **Pick the task** — the next unchecked `000xxx` in `TASKS.md`. Create a branch:
   `git checkout -b feature/<short-desc>` (never work on `main`).

2. **Red** — write failing Catch2 tests first in `tests/`, targeting the
   framework-agnostic class in `dsp/` or `io/`. Run `dt-test`; confirm they fail
   for the right reason.

3. **Green** — implement the minimum to pass. Keep DSP JUCE-free; put host/file/
   control-server glue in `src/` or `io/`. Re-run `dt-test` to green.

4. **Integrate** — wire the class into `PluginProcessor` / `Engine` / APVTS and add
   an integration test exercising a realistic workflow (one of: CBT journal, story
   editor, IT task manager, ERP, bug tracker — per global rules) where applicable;
   for DSP, an end-to-end render test.

5. **Build & install** — `dt-build`, then `dt-install` for in-DAW tasks.

6. **Verify** — Standalone for quick listening; `dt-logic-smoke` for the milestone's
   manual Logic check. Report real results.

7. **Gate** — `dt-lint` clean and full `dt-test` green.

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
