#!/usr/bin/env python3
"""Exercise the complete CLI with a tiny temporary repository."""

import pathlib
import json
import sqlite3
import subprocess
import sys
import tempfile


def main() -> int:
    executable = pathlib.Path(sys.argv[1]).resolve()
    help_result = subprocess.run(
        [str(executable), "--help"], capture_output=True, text=True, check=True,
    )
    if "query [--json]" not in help_result.stdout:
        raise AssertionError(help_result.stdout)
    with tempfile.TemporaryDirectory(prefix="hypersearch-cli-smoke-") as directory:
        repository = pathlib.Path(directory)
        (repository / "pipeline.cpp").write_text(
            "// embeddings are generated for missing texts\n"
            "auto batch = model_->embed_batch(missing_texts);\n",
            encoding="utf-8",
        )
        (repository / "store.cpp").write_text(
            "// embeddings are stored persistently\n"
            "const char* sql = R\"(\n"
            "INSERT INTO chunk_embeddings\n"
            "(chunk_id, embedding) VALUES (?, ?)\n"
            ")\";\n",
            encoding="utf-8",
        )
        missing = subprocess.run(
            [str(executable), "query", str(repository), "embeddings"],
            cwd=repository, capture_output=True, text=True,
        )
        if missing.returncode != 1 or "Index does not exist" not in missing.stderr:
            raise AssertionError(missing.stderr)

        initial_index = subprocess.run(
            [str(executable), "index", str(repository)],
            cwd=repository, capture_output=True, text=True, check=True,
        )
        if "2 files (2 changed), 2 chunks" not in initial_index.stdout:
            raise AssertionError(initial_index.stdout)
        completed = subprocess.run(
            [str(executable), "query", str(repository),
             "where are embeddings generated and stored"],
            cwd=repository,
            capture_output=True,
            text=True,
            check=True,
        )
        output = completed.stdout
        if not output.startswith("Generated here\npipeline.cpp:2"):
            raise AssertionError(output)
        if "Stored here\nstore.cpp:2-5" not in output:
            raise AssertionError(output)
        if "Calls `embed_batch`" not in output or "This INSERT writes" not in output:
            raise AssertionError(output)
        if not (repository / ".hypersearch" / "index.sqlite3").exists():
            raise AssertionError("index database was not created")
        legacy = subprocess.run(
            [str(executable), str(repository),
             "where are embeddings generated and stored"],
            cwd=repository, capture_output=True, text=True, check=True,
        )
        if legacy.stdout != output:
            raise AssertionError("legacy search and saved-index query differ")
        structured = subprocess.run(
            [str(executable), "query", "--json", str(repository), "embeddings"],
            cwd=repository, capture_output=True, text=True, check=True,
        )
        parsed = json.loads(structured.stdout)
        if parsed["schema_version"] != 1 or not parsed["results"]:
            raise AssertionError(structured.stdout)
        first = parsed["results"][0]
        if not {"file", "start_line", "end_line", "score"} <= first.keys():
            raise AssertionError(first)

        database = repository / ".hypersearch" / "index.sqlite3"
        with sqlite3.connect(database) as connection:
            before = connection.execute(
                "SELECT id, content FROM chunks WHERE file_path = 'pipeline.cpp'"
            ).fetchone()
            cached_before = connection.execute(
                "SELECT COUNT(*) FROM chunk_embeddings WHERE chunk_id = ?",
                (before[0],),
            ).fetchone()[0]
        unchanged_index = subprocess.run(
            [str(executable), "index", str(repository)],
            cwd=repository, capture_output=True, text=True, check=True,
        )
        if "2 files (0 changed), 2 chunks" not in unchanged_index.stdout:
            raise AssertionError(unchanged_index.stdout)
        with sqlite3.connect(database) as connection:
            after = connection.execute(
                "SELECT id, content FROM chunks WHERE file_path = 'pipeline.cpp'"
            ).fetchone()
            cached_after = connection.execute(
                "SELECT COUNT(*) FROM chunk_embeddings WHERE chunk_id = ?",
                (after[0],),
            ).fetchone()[0]
        if before != after or cached_before != cached_after or not cached_after:
            raise AssertionError("unchanged indexing did not retain cached data")

        (repository / "pipeline.cpp").write_text(
            "auto changed = model_->embed_batch(new_texts);\n", encoding="utf-8"
        )
        changed_index = subprocess.run(
            [str(executable), "index", str(repository)],
            cwd=repository, capture_output=True, text=True, check=True,
        )
        if "2 files (1 changed), 2 chunks" not in changed_index.stdout:
            raise AssertionError(changed_index.stdout)
        with sqlite3.connect(database) as connection:
            updated = connection.execute(
                "SELECT content FROM chunks WHERE file_path = 'pipeline.cpp'"
            ).fetchone()[0]
        if "new_texts" not in updated or "missing_texts" in updated:
            raise AssertionError("changed file was not reindexed")
    print("CLI smoke test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
