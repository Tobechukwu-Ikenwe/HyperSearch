# Experiments

`legacy/` preserves earlier implementations, fixtures, and a historical
project snapshot that are not part of the current HyperSearch build. Active
code and tests live in `src/` and `tests/`. The scanner demo is preserved in
`legacy/src/indexer/` and is not built by default.
Changes to legacy code have no effect on the `hypersearch` executable unless
they are deliberately integrated into [CMakeLists.txt](../CMakeLists.txt).
