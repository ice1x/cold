# Cold — project instructions for Claude

A layering sampler plugin (AU / VST3 / Standalone) in C++ / JUCE, targeting
Apple Silicon macOS + Logic Pro. Optimize for getting a usable plugin into Logic
early, then iterate.

Read [TASKS.md](TASKS.md) for the live roadmap. Always work the next unchecked
task and tick its checkbox when done.

## Tech stack

- C++20, JUCE (fetched via CMake), CMake + Ninja.
- Catch2 (fetched via CMake) + CTest for unit tests.
- `clang-format` + `clang-tidy` as the quality gate.
- Plugin product name: `Cold`. Manufacturer/plugin 4-char codes live in the
  top-level `CMakeLists.txt` (define once).

## Architecture rule (important)

Keep **DSP classes framework-agnostic** (no JUCE dependency): `dsp/AmpEnvelope`,
`dsp/MorphFilter`, `dsp/Lfo`, `dsp/SampleVoice`, `dsp/Engine`. This lets Catch2
test them with plain buffers and no plugin host. JUCE-dependent code (file
decoding in `io/SampleLoader`) goes in a thin layer tested via a JUCE-linked
target. Signal flow: sample → amp env → morph filter → out, with the LFO/mod
matrix able to target both filter cutoff **and** filter type. Details in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Design choices

- **Polyphony**: each track is **polyphonic** (play chords with samples).
  `dsp/Engine` allocates multiple voices.
- **Playback modes**: **looping** (loop points) **and** a classic **repitch /
  varispeed** mode where pitch changes playback speed (including loop speed
  following pitch). Selectable per track.

## MCP control surface

Cold is controllable by LLM tools (voice control + tuning recommendations) via MCP.
- The plugin embeds a small **control server** (OSC/WebSocket) exposing parameter
  get/set, sample load, state query, and note trigger.
- A companion **MCP server** (`mcp/`, TypeScript/Node) bridges MCP tool calls to that
  control server. Tools: list/get/set parameters, load sample, get state, trigger
  note, and a `recommend_tuning` tool that returns current state for the LLM to reason
  over. See [docs/MCP.md](docs/MCP.md).

## Commands

```sh
# Configure + build (AU/VST3/Standalone)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Test
ctest --test-dir build --output-on-failure

# Lint / format
clang-format --dry-run --Werror $(git ls-files '*.cpp' '*.h')
clang-tidy -p build $(git ls-files 'src/*.cpp')

# Install AU + validate for Logic
cp -R build/**/Cold_artefacts/**/Cold.component ~/Library/Audio/Plug-Ins/Components/
auval -a | grep -i cold
```

## Project skills (`.claude/skills/`)

Use these instead of re-deriving the commands:

- `cold-build` — configure/build the JUCE targets.
- `cold-install` — install AU/VST3, run `auval`, clear the AU cache for Logic.
- `cold-test` — run the Catch2 suite (TDD red/green).
- `cold-lint` — clang-format / clang-tidy gate.
- `cold-logic-smoke` — manual Logic Pro verification checklist.
- `cold-feature` — the end-to-end TDD workflow for adding a feature.

## Logic Pro / AU specifics

- **Logic only loads AU.** Always build & validate the `.component`; VST3 +
  Standalone are byproducts for other DAWs / fast iteration.
- After reinstalling, Logic re-scans AUs on launch; if it caches a bad build,
  clear `~/Library/Caches/AudioUnitCache` and re-validate with `auval`.
- DAW behavior can't be auto-tested — milestone smoke-test tasks are **manual**.
  Report actual results; never silently claim a Logic test passed.

## Workflow rules (from global CLAUDE.md — do not violate)

- **TDD always**: failing test → implement → integration test. No code without tests.
- **Never** commit or push to `main`. Work on `feature/*` or `fix/*` branches;
  push the branch immediately after committing, then provide the PR link.
- Before committing: `git reset HEAD`, then `git add` only the intended files.
- Commit messages need a title **and** a descriptive body. No Claude co-author trailer.
- `cmake build` + `ctest` + clang-format/clang-tidy must pass before every push.
