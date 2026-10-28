# HyperSearch

HyperSearch is a C++20 CLI for finding the source code behind a natural-language
question. It combines SQLite FTS5 keyword search with ONNX embeddings, then
prints line-numbered excerpts. It runs locally: source files and the index stay
on the machine where the command runs.

## Quick start

On Linux, install CMake 3.20+, a C++20 compiler, SQLite3 development files,
OpenMP, Rust, and Cargo. The first full build downloads Rust crates and a
SentencePiece build dependency. The ONNX Runtime library and MiniLM model are
included in the repository.

```sh
git submodule update --init --recursive
cmake --preset full
cmake --build --preset full -j4
ctest --preset full
./build/hypersearch index .
./build/hypersearch query . "where are embeddings generated and stored"
```

The example prints two sections, each with a source path and line number:

```text
Generated here
src/embedding/EmbeddingPipeline.cpp:155
155 |             auto batch = model_->embed_batch(inputs);

Stored here
src/storage/EmbeddingStore.cpp:240-247
240 |     const char* sql = R"(
241 |         INSERT INTO chunk_embeddings
...
```

To inspect another repository, replace `.` with its path. `index` updates the
saved SQLite index; `query` searches it without rescanning files. Use `search`
to do both in one command. Results are retrieval heuristics, not generated
answers. See [result selection](docs/search-selection.md) and
[architecture and limits](docs/architecture.md) for details.

## Build and install

The full build targets Linux with C++20, CMake 3.20+, SQLite3, OpenMP, Rust
and Cargo, the bundled ONNX Runtime, and the bundled MiniLM model. CMake
builds the tokenizer submodule, including its Rust archive. Its first build
downloads Rust crates and the SentencePiece Abseil dependency.

CMake copies the model into the build directory. The executable finds it next
to itself, so it can run from any working directory.

To install a self-contained CLI prefix with the model and ONNX Runtime:

```sh
cmake --install build --prefix "$HOME/.local"
"$HOME/.local/bin/hypersearch" --help
```

The installed binary still uses the host's SQLite, OpenMP, and C++ runtime
libraries. The full build and install have been verified on Linux x86-64.

`index` scans a repository and updates the SQLite index. It reads eligible
files on each run, but skips chunk replacement for files whose content
fingerprint is unchanged. `query` uses the saved index without scanning the
repository. It still loads cached vectors into memory and reads selected
source files to print current excerpts. `search` runs both steps. The original
two-argument form remains an alias for `search`.

For tools, `query --json` and `search --json` emit a versioned object with a
`results` array. Each entry has a repository-relative `file`, inclusive
`start_line` and `end_line`, and reciprocal-rank-fusion `score`. This is the
fused candidate list (up to 100 chunks), before the text formatter chooses
excerpts or coordinated-action sections.

## Tests and benchmarks

The selection tests exercise query parsing, reciprocal rank fusion, FTS
updates, embedding cache invalidation, exact source line output, and the
two-part embedding response. A model-free build runs them without ONNX or the
tokenizer archives; this is also the GitHub Actions check.

```sh
cmake --preset core
cmake --build --preset core -j4
ctest --preset core
./build-core/search_selection_bench 1000
```

`core-sanitized` runs the same core tests with AddressSanitizer and
UndefinedBehaviorSanitizer. It is also a CI job.

For a full local build, `ctest --preset full` also runs the end-to-end CLI
smoke test. The selection benchmark
uses a deterministic 50-plus-50 candidate fixture and times only rank fusion
and formatting. [Benchmark notes](docs/benchmarks.md) report measured results
and explain what each number includes.

The labeled file-level retrieval check lives in `eval/`. Run
`python3 eval/evaluate.py ./build/hypersearch . --index` to print Recall@5,
MRR@10, and nDCG@10 for the included questions. This small self-repository
set is a regression baseline, not a broad measure of search quality on other
repositories.

## Data and current scope

The generated index is stored at `<searched-repository>/.hypersearch/index.sqlite3`.
It is ignored by Git. The `query` command reconstructs the in-memory vector
store from cached embeddings without a repository scan. Similarity search is
an exact, parallel scan across those vectors. Re-run `index` after files change
to refresh the saved index.

The active CLI is assembled in [CMakeLists.txt](CMakeLists.txt) and begins in
[`src/main.cpp`](src/main.cpp). Unlinked prototypes are preserved under
`experiments/legacy/` and excluded from indexing by default.

## Repository layout

- `src/indexer/` scans and chunks source files.
- `src/storage/` persists chunks and embeddings in SQLite.
- `src/embedding/` runs the model and manages in-memory vectors. The optional
  Python backend lives in `src/embedding/python/`.
- `src/search/` analyzes questions, retrieves candidates, and fuses rankings.
- `src/output/` formats source excerpts.
- `tests/unit/` checks individual components; `tests/integration/` exercises
  the CLI. `benchmarks/` measures performance and `eval/` tracks retrieval
  quality.
- `third_party/` contains the model and build dependencies.

Build directories and `.hypersearch/` indexes are generated locally and
ignored by Git. Historical prototypes and snapshots live in
`experiments/legacy/` and are not part of the executable.
