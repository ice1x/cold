---
name: dt-lint
description: Run clang-format and clang-tidy on the Cold sources as the pre-commit quality gate. Use before committing or when asked to lint/format the code.
---

# dt-lint

The formatting + static-analysis gate. Must pass before every commit/push (the
project's analog of `cargo fmt --check` + `cargo clippy`).

## Format check

```sh
clang-format --dry-run --Werror $(git ls-files '*.cpp' '*.h' '*.hpp')
```

Auto-fix:

```sh
clang-format -i $(git ls-files '*.cpp' '*.h' '*.hpp')
```

## Static analysis

`clang-tidy` needs the compile database, so configure with it on once:

```sh
cmake -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
clang-tidy -p build $(git ls-files 'src/*.cpp')
```

## Notes

- Config lives in `.clang-format` and `.clang-tidy` at the repo root.
- Don't lint fetched dependencies (JUCE/Catch2 under `build/`) — only `src/`,
  `tests/`.
- Fix warnings rather than suppressing them; only suppress with a justified
  inline comment.
