# Benchmark notes

Measured on 2026-10-28 on a Linux x86-64 laptop with an Intel Core i7-1355U
(12 logical CPUs), using the CMake `full` Release preset. These are measurements
of this checkout and machine, not performance guarantees.

| Measurement | Work | Runs | p50 | p95 |
| --- | --- | ---: | ---: | ---: |
| Single-action selection | Fuse 50 lexical and 50 semantic results, then format from 75 unique candidate files | 200 | 1.260 ms | 1.287 ms |
| Three-action selection | Fuse the same candidates, then locate generated, stored, and loaded lines | 200 | 0.460 ms | 1.063 ms |
| Warm saved-index query | Run `query` against this repository after indexing | 10 | 213.765 ms | 255.206 ms |

The selection benchmark uses a deterministic temporary fixture. It measures
fusion and formatting, including source-file reads, but excludes scanning,
SQLite loading, model inference, and vector similarity. It runs 10 untimed
warmups. Repeat it with:

```sh
./build/search_selection_bench 200
```

The CLI measurement starts a new process for every query. It includes loading
the SQLite chunks and cached vectors, initializing the ONNX model, embedding
the question, exact vector search, FTS5 search, fusion, and formatting. It
excludes repository scanning and index updates. It ran two warmups followed by
10 timed invocations. Repeat it with:

```sh
python3 benchmarks/cli_bench.py ./build/hypersearch . \
  "where are embeddings generated and stored" --index --runs 10 --warmups 2
```

`--index` refreshes the index before timing. Repository contents, cache state,
CPU load, and thread count affect the numbers. Measure indexing separately
when evaluating cold-start cost; its work depends heavily on repository size
and the number of embeddings missing from the cache.
