---
name: dt-test
description: Run the Cold Catch2 unit tests via CTest for the TDD red/green loop. Use when asked to run tests, check the suite, or verify DSP behavior.
---

# dt-test

Run the Catch2 suites. Used constantly in the TDD loop: write a failing test,
watch it go red here, implement, watch it go green.

## Run everything

```sh
cmake --build build --target ColdTests
ctest --test-dir build --output-on-failure
```

## Iterate on one suite / case

Run the test binary directly with a Catch2 filter:

```sh
TESTBIN=$(find build -name 'ColdTests' -type f | head -1)
"$TESTBIN" "[morphfilter]"        # by tag
"$TESTBIN" "SampleVoice loops"    # by test name
"$TESTBIN" --list-tests
```

## TDD discipline

1. Write the failing test first (it must compile and fail for the right reason).
2. Implement the minimum in the framework-agnostic `dsp/` or `io/` class.
3. Re-run until green.
4. Add integration coverage for realistic workflows.

DSP classes are JUCE-free so these tests need no plugin host. File-decode tests
(`SampleLoader`) link JUCE.

Before committing, the full suite must be green (with `dt-lint`).
