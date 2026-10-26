# Search result excerpts

For questions that list two or more actions, HyperSearch shows a separate
section for each action. For example, a query about where embeddings are
generated, stored, and loaded yields three sections. Each section includes a
source path, line numbers, code, and a brief explanation. Common operation
words can match code names such as `save` or SQL `INSERT` for “stored.” If a
step has no confident match, its section says so.

HyperSearch retrieves indexed code chunks with lexical and semantic search,
then favors code that covers more distinct query terms, followed by comments
and other text that covers them. Fused search rank
breaks ties. It displays the first result as **Most likely match** and up to three
more as **Likely alternative**. These labels describe ranking, not a guarantee
that a particular line answers the question.
Overlapping indexed chunks that select the same source span are displayed
only once. The four displayed matches come from different files.

For each ranked chunk, the formatter looks for query terms in its source
lines. It splits snake_case and camelCase identifiers and recognizes close
word stems (for example, `generate` and `generated`). A matching code line
is preferred over a comment with the same words. When no query term appears
in the chunk, the formatter chooses a nonblank line near the chunk's center
and marks the excerpt **approximate**. This is common for semantic matches
where the query and code use different words.

The excerpt extends from that line to the surrounding blank-line boundaries.
For a question with multiple terms, it may also include a nearby line that
matches a different part of the question.
It is read from the source file, so it may be shorter or longer than the
indexed chunk and may cross chunk boundaries. There is no fixed excerpt line
limit. The output prints the repository-relative file path, exact source
line range, and each original source line with its line number. If the source
file cannot be reopened, the formatter falls back to the indexed chunk.

This changes display ordering only. It does not change chunk sizes, indexed
embeddings, or the SQLite schema. Selecting a precise answer inside a purely
semantic match is still uncertain; the approximate label makes that visible.
