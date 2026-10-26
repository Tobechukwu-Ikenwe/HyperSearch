# Benchmark notes

Measured on an Intel Core i7-1355U laptop (12 logical CPUs), Linux x86-64,
with a CMake Release build. These numbers describe this machine and checkout;
they are not performance guarantees.

| Measurement | Work | Runs | p50 | p95 |
| --- | --- | ---: | ---: | ---: |
| Single-action selection | Fuse 50 lexical and 50 semantic results, then format from 75 unique candidate files | 200 | 1.270 ms | 1.290 ms |
| Three-action selection | Fuse the same candidates, then locate generated, stored, and loaded lines | 200 | 0.466 ms | 0.613 ms |
| Warm CLI | Run `./build/hypersearch . "where are embeddings generated and stored"` against the already indexed repository | 10 | 673.3 ms | 714.7 ms |

The selection benchmark includes reciprocal rank fusion and both formatter
paths: ranked excerpts and coordinated actions. It excludes file scanning, SQLite,
model loading, embedding generation, and vector similarity search. Its fixture
is deterministic and is generated in a temporary directory by
`benchmarks/search_bench.cpp`. The run uses 10 untimed warmups. Run it with:

```sh
./build/search_selection_bench 200
```

The warm CLI number includes a process start, source scan, SQLite index
reconciliation, model initialization, cached-vector loading, in-memory vector
store reconstruction, both retrieval branches, fusion, and formatting. It does
not measure a cold embedding pass. The 10 measured runs followed one warmup;
the reported p95 is the largest of 10 sorted samples. Repository contents and
cache state affect it. It was measured by timing ten sequential subprocess
invocations with Python's `time.perf_counter()`.

The large gap between the two rows shows that candidate fusion and formatting
are a small share of warm end-to-end latency on this checkout. To compare
changes fairly, use the same corpus, query, cache state, build type, and CPU
thread configuration. Measure cold indexing separately from warm querying.
