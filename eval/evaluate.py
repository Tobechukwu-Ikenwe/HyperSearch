#!/usr/bin/env python3
"""Measure file-level retrieval against a labeled query set."""

import argparse
import json
import math
import pathlib
import subprocess
import sys


def run(executable: pathlib.Path, *arguments: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [str(executable), *arguments],
        capture_output=True,
        text=True,
        check=True,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=pathlib.Path)
    parser.add_argument("repository", type=pathlib.Path)
    parser.add_argument(
        "--cases", type=pathlib.Path,
        default=pathlib.Path(__file__).with_name("cases.json"),
    )
    parser.add_argument("--index", action="store_true", help="refresh the index first")
    parser.add_argument("--min-recall-at-5", type=float, default=0.0)
    args = parser.parse_args()

    executable = args.executable.resolve()
    repository = args.repository.resolve()
    cases = json.loads(args.cases.read_text(encoding="utf-8"))
    if not cases:
        parser.error("case set must not be empty")
    if args.index:
        run(executable, "index", str(repository))

    hits_at_5 = 0
    reciprocal_rank_sum = 0.0
    ndcg_sum = 0.0
    for case in cases:
        output = run(executable, "query", "--json", str(repository), case["query"])
        response = json.loads(output.stdout)
        if response["schema_version"] != 1:
            raise RuntimeError("Unexpected result schema version")
        relevant = set(case["relevant_files"])
        # Multiple chunks from one file should not consume multiple rank slots
        # in a file-level evaluation.
        ranked_files = list(dict.fromkeys(
            result["file"] for result in response["results"]
        ))
        rank = next(
            (index for index, path in enumerate(ranked_files[:10], 1)
             if path in relevant),
            None,
        )
        if rank is not None:
            hits_at_5 += rank <= 5
            reciprocal_rank_sum += 1.0 / rank
            ndcg_sum += 1.0 / math.log2(rank + 1)
        print(f"{rank or '-':>2}  {case['query']}")

    count = len(cases)
    print(f"Recall@5={hits_at_5 / count:.3f} ", end="")
    print(f"MRR@10={reciprocal_rank_sum / count:.3f} ", end="")
    print(f"nDCG@10={ndcg_sum / count:.3f} (n={count})")
    if hits_at_5 / count < args.min_recall_at_5:
        return 1
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        print(error.stderr, file=sys.stderr)
        raise SystemExit(error.returncode) from error
