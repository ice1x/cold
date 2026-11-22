---
name: cold-test
description: Run the Cold Catch2 unit/integration/e2e tests via CTest for the TDD red/green loop. Use when asked to run tests, check the suite, verify DSP behavior, or check coverage across levels.
---

# cold-test

Run the Catch2 suites. Used constantly in the TDD loop: write a failing test,
watch it go red here, implement, watch it go green.

## Test levels (every feature is covered at all applicable levels)

The suites are split into three levels, each its own CTest **label** and (for
the JUCE ones) its own executable. Catch2 tags (`[unit]` / `[integration]` /
`[e2e]`) mirror the labels.

| Level | What it proves | Target(s) | Dir |
|-------|----------------|-----------|-----|
| **unit** | one class in isolation, fast | `ColdTests` (JUCE-free DSP), `ColdIoTests` (io) | `tests/unit/` |
| **integration** | several units in a realistic workflow (e.g. multi-format bank load, file↔stream parity) | `ColdIntegrationTests` | `tests/integration/` |
| **e2e** | the real `ColdAudioProcessor` driven as a host does (prepare / processBlock / MIDI / state / editor) | `ColdE2ETests` | `tests/e2e/` |

Shared deterministic fixtures (signal generators + format writers) live in
`tests/support/SignalFixtures.h`. Framework-agnostic DSP stays in `ColdTests`
(no JUCE); everything touching files/host links JUCE.

## Run everything

```sh
cmake --build build --target ColdTests ColdIoTests ColdIntegrationTests ColdE2ETests
ctest --test-dir build --output-on-failure
```

## Run one level

```sh
ctest --test-dir build -L unit          # also: integration | e2e
```

## Iterate on one suite / case

Run a test binary directly with a Catch2 filter:

```sh
TESTBIN=$(find build -name 'ColdIoTests' -type f | head -1)
"$TESTBIN" "[sampleloader]"        # by tag
"$TESTBIN" "[integration]"         # by level tag
"$TESTBIN" --list-tests
```

## TDD discipline

1. Write the failing test first (it must compile and fail for the right reason).
2. Implement the minimum in the framework-agnostic `dsp/` or `io/` class.
3. Re-run until green.
4. **Add the higher levels**: an integration test for the realistic workflow
   the unit feeds, and — when the feature reaches the plugin — an e2e test that
   drives `ColdAudioProcessor`. No feature is "done" with only unit tests.

DSP classes are JUCE-free so unit tests need no plugin host. io / processor
suites link JUCE.

Before committing, the full suite must be green (with `cold-lint`).
