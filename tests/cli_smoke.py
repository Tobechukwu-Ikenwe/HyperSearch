#!/usr/bin/env python3
"""Exercise the complete CLI with a tiny temporary repository."""

import pathlib
import subprocess
import sys
import tempfile


def main() -> int:
    executable = pathlib.Path(sys.argv[1]).resolve()
    project = pathlib.Path(sys.argv[2]).resolve()
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
        completed = subprocess.run(
            [str(executable), str(repository),
             "where are embeddings generated and stored"],
            cwd=project,
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
    print("CLI smoke test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
