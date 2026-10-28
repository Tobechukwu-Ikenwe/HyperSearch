# Architecture and operating model

## Commands

`index <repository>` scans eligible files, updates SQLite chunks and the FTS5
index, and ensures each chunk has a cached ONNX embedding. Files with the same
content fingerprint skip chunk replacement. It reads eligible files again on
each indexing run; there is no filesystem watcher or directory-level skip.

`query <repository> <question>` loads saved chunks and vectors, embeds the
question, retrieves two top-50 lists, combines them with reciprocal rank
fusion, and formats results. It does not rescan the repository, but the text
formatter reopens selected source files for current excerpts. If files changed
since indexing, run `index` first. `search` runs both commands in one process.

## Persistence

The repository-local `.hypersearch/index.sqlite3` database has:

- `chunks`: path, category, original line span, and indexed text.
- `chunks_fts`: external-content FTS5 index synchronized by triggers.
- `chunk_embeddings`: model ID, dimension, and float32 vector per chunk.
- `file_fingerprints`: content fingerprint used to skip unchanged file writes.

Chunk IDs remain stable for unchanged spans. Updating text invalidates its
cached vector; deleting a chunk removes its cache entry. The file fingerprint
is a size plus 64-bit FNV-1a value for change detection, not a security hash.
The currently bundled model has a fixed 384-dimensional output and a fixed
cache model ID.

## Retrieval and output

The lexical branch uses FTS5 BM25. The semantic branch performs exact cosine
search over in-memory normalized vectors. Results are fused by rank, capped at
five chunks per file, and passed to the text formatter. Text output reorders
the candidates using term coverage and displays source excerpts. JSON output
exposes the fused candidate list directly; its scores are fusion values, not
probabilities. See [search-selection.md](search-selection.md) for the detailed
ranking rules.

## Current limits

- The model is a general sentence embedder. Retrieval quality is measured by
  the small labeled set in `eval/`, and has not been validated broadly across
  languages or repositories.
- Vector search is an exact scan and all vectors are reconstructed in memory
  for each query process. No ANN index or resident query service is present.
- Chunks are fixed line windows. They may split a function or mix unrelated
  functions.
- The scanner uses a built-in allowlist and directory exclusions. It does not
  follow symlinks, implement full `.gitignore` semantics, or provide
  comprehensive secret detection.
- Index updates are atomic per file, but a failure during a multi-file run can
  leave some files refreshed and others at their previous version. Retrying
  `index` reconciles them.
