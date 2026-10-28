#!/usr/bin/env python3
"""Verify ONNX indexing handles more than one bounded inference batch."""

import pathlib
import sqlite3
import subprocess
import sys
import tempfile


def main() -> int:
    executable = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="hypersearch-batch-") as directory:
        repository = pathlib.Path(directory)
        for index in range(33):
            (repository / f"part_{index:02}.cpp").write_text(
                f"void component_{index}() {{ process_component({index}); }}\n",
                encoding="utf-8",
            )
        subprocess.run(
            [str(executable), "index", str(repository)],
            cwd=repository, capture_output=True, text=True, check=True,
        )
        database = repository / ".hypersearch" / "index.sqlite3"
        with sqlite3.connect(database) as connection:
            chunk_count = connection.execute("SELECT COUNT(*) FROM chunks").fetchone()[0]
            embedding_count = connection.execute(
                "SELECT COUNT(*) FROM chunk_embeddings"
            ).fetchone()[0]
        if (chunk_count, embedding_count) != (33, 33):
            raise AssertionError((chunk_count, embedding_count))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
