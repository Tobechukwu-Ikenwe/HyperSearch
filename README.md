# HyperSearch

HyperSearch is a C++ command-line tool for finding source code that answers a
question. It indexes line-numbered code chunks in SQLite, caches 384-dimensional
embeddings, combines full-text and semantic retrieval, and prints source
excerpts with file paths and line numbers.

```sh
./build/hypersearch . "where are embeddings generated and stored"
```

For that question, the output separates embedding generation from the SQLite
write and gives a short explanation for each. Ordinary questions show one
most likely match and up to three alternatives. These are search heuristics,
not answers from a language model. The precise selection rules and their
limits are in [How search results are selected](docs/search-selection.md).

## Build

The current full build targets Linux with C++20, CMake 3.20+, SQLite3, OpenMP,
the bundled ONNX Runtime, and the bundled MiniLM model. It currently expects
`libtokenizers_cpp.a` and `libtokenizers_c.a` under
`third_party/tokenizers-cpp/build/`. The source is recorded as a Git submodule,
but the full build does not yet build those Rust/C++ archives automatically.

```sh
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

If CMake reports missing tokenizer archives, build tokenizers-cpp in that
submodule first. The model and tokenizer paths in the executable are relative
to the working directory, so run `./build/hypersearch` from this repository's
root. The repository path argument can point elsewhere.

## Tests and benchmarks

The selection tests exercise query parsing, reciprocal rank fusion, FTS
updates, embedding cache invalidation, exact source line output, and the
two-part embedding response. A model-free build runs them without ONNX or the
tokenizer archives; this is also the GitHub Actions check.

```sh
cmake -S . -B build-core -DHYPERSEARCH_CORE_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-core -j4
ctest --test-dir build-core --output-on-failure
./build-core/search_selection_bench 1000
```

For a full local build, `ctest --test-dir build --output-on-failure` runs both
the selection tests and the query analyzer checks. The selection benchmark
uses a deterministic 50-plus-50 candidate fixture and times only rank fusion
and formatting. [Benchmark notes](docs/benchmarks.md) report measured results
and explain what each number includes.

## Data and current scope

The generated index is stored at `<searched-repository>/.hypersearch/index.sqlite3`.
It is ignored by Git. Each invocation scans the repository and rebuilds the
in-memory vector store; cached embeddings avoid regenerating unchanged chunk
vectors. Similarity search is an exact, parallel scan across those vectors.

The active CLI is assembled in [CMakeLists.txt](CMakeLists.txt) and begins in
[`src/main.cpp`](src/main.cpp). Directories such as `src/runtime` and
`src/search` contain exploratory code that is not linked into this executable.
