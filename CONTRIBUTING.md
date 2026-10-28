# Contributing

## Development checks

Use the `core` preset for fast changes to scanning, SQLite persistence, query
analysis, fusion, formatting, or vector search. Use the `full` preset before
merging changes to the CLI or embedding path. The full build needs the
`third_party/tokenizers-cpp` submodule and downloads its first-build Rust and
SentencePiece dependencies.

```sh
cmake --preset core
cmake --build --preset core -j4
ctest --preset core

cmake --preset full
cmake --build --preset full -j4
ctest --preset full
cmake --install build --prefix /tmp/hypersearch-install
python3 tests/integration/cli_smoke.py /tmp/hypersearch-install/bin/hypersearch
python3 eval/evaluate.py ./build/hypersearch . --index
```

Project targets compile with `-Wall -Wextra -Wpedantic -Werror` by default.
Use the repository `.clang-format` for touched C++ code. Add a small regression
test for a fixed behavior, and update the labeled evaluation cases when the
change affects retrieval quality. The 12-case evaluation is a guard against
obvious regressions, not a substitute for testing on other repositories.

The active source tree is `src/`, with tests in `tests/unit/` and
`tests/integration/`. Preserved, unlinked prototypes are in
`experiments/legacy/`. Keep new code out of the latter unless it is explicitly
experimental. Update `eval/cases.json` when moving files used by labeled
retrieval checks.
