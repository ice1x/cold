---
name: dt-build
description: Configure and build the Cold plugin (AU / VST3 / Standalone) with CMake + Ninja on Apple Silicon. Use when asked to build, recompile, or produce plugin artifacts.
---

# dt-build

Build the Cold JUCE plugin for arm64 macOS.

## First-time configure

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

Use `-DCMAKE_BUILD_TYPE=Debug` while developing/debugging. JUCE and Catch2 are
fetched automatically — the first configure downloads them and is slow.

## Build

```sh
cmake --build build
```

Build a single target while iterating:

```sh
cmake --build build --target Cold_Standalone   # fast DSP listening, no DAW
cmake --build build --target Cold_AU            # the format Logic loads
```

## Artifacts

After a build they live under `build/` (JUCE puts them in
`*_artefacts/<config>/`):

- AU: `Cold.component`
- VST3: `Cold.vst3`
- Standalone: `Cold.app`

Print the paths:

```sh
find build -name 'Cold.component' -o -name 'Cold.vst3' -o -name 'Cold.app'
```

## Notes

- If CMake cache is stale or you changed the JUCE/Catch2 pin, delete `build/` and
  reconfigure.
- Keep DSP code JUCE-free so it also compiles into the test target (see `dt-test`).
- Next step after a successful build is usually `dt-install` (Logic) or `dt-test`.
