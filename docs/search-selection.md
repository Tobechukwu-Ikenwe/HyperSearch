# How search results are selected

This describes the current `hypersearch` executable. It is a retrieval system
with rule-based output; it does not ask a language model to choose or explain
results.

## Candidate generation

1. Each eligible file is split into 40-line chunks with five lines of overlap.
   Every chunk keeps its repository-relative path and original line span.
2. The SQLite FTS5 branch returns up to 50 chunks ordered by BM25. Natural
   language queries join non-stopword terms with `OR`; code-like queries use
   `AND`. Identifiers are split at punctuation and underscores when constructing
   FTS phrases. This branch needs textual matches.
3. The semantic branch embeds the entire query with the same model used for
   chunks. It compares that vector with every in-memory chunk vector using
   cosine similarity and returns the top 50. This branch can find related code
   with different words, but it scans all vectors on every query.

## Fusion

BM25 and cosine scores have different scales, so `ResultFusion` uses list
positions instead of comparing their raw scores. A chunk at 1-based rank `r`
gets `1 / (60 + r)` from a list. If it appears in both lists, the contributions
add. For example, a chunk ranked 1st lexically and 3rd semantically scores
`1/61 + 1/63`; a chunk found only at rank 1 scores `1/61`. The former wins.

Chunks are deduplicated by file path and indexed line span. Fusion permits at
most five chunks per file and returns at most 100 candidates to the formatter.
The `100` is an internal candidate pool, not the number printed.

## Display ordering and excerpt choice

For questions with a single action, the formatter counts **distinct query terms** present
in each candidate chunk. Terms in code lines carry more weight than terms only
in comments or prose. It recognizes camelCase, snake_case, and some words with
a common prefix. The fused rank breaks equal display scores. This is a
heuristic, so a string literal that repeats a question can still rank well.

The formatter displays the first candidate and up to three alternatives, with
at most one shown excerpt per file. For each candidate it reopens the source
file, chooses the best matching line within the indexed chunk, and expands to
nearby blank-line boundaries. It may include another nearby line that matches
a different query term. If no term matches, it selects a nonblank line and
labels that excerpt `approximate`. Every printed line uses its source line
number. If the source file cannot be opened, it uses the cached chunk text.

For a question that coordinates two or more actions, such as “where are
embeddings generated, stored, and loaded,” the formatter creates one section
per action. It carries the shared subject into each section, then scores code
lines in retrieved files against that subject and the action. A small general
operation vocabulary connects common wording to implementation terms, such as
“stored” to `save` or SQL `INSERT`. Each section prints its own source line and
a short explanation based on the selected call or SQL statement. If one step
has no supported evidence, that section says so. The explanations are
templates backed by lexical evidence; this is heuristic query parsing, not a
language model or proof that the implementation behaves as described.

## Why a result can be wrong

- Retrieval can miss the right chunk if it is outside both top-50 lists.
- Overlapping chunks can repeat evidence; fusion and display remove some, but
  not every form of near duplicate.
- The display heuristic values term coverage, so a diagnostic string or test
  can outrank the implementation.
- Coordinated-action parsing handles common `and` and comma-separated verb
  lists. Unusual sentence structures and operations outside its vocabulary
  can still produce weak or missing sections.
- The scanner includes nested example repositories unless their directory
  names are on its ignore list.

The selection tests in `tests/search_selection_test.cpp` cover the individual
rules. `benchmarks/search_bench.cpp` times fusion plus formatting on a fixed
synthetic pool. It does not include model inference, scanning, or SQLite work.
