#!/usr/bin/env python3
"""Measure warm saved-index CLI query latency on a chosen repository."""

import argparse
import math
import pathlib
import statistics
import subprocess
import time


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=pathlib.Path)
    parser.add_argument("repository", type=pathlib.Path)
    parser.add_argument("query")
    parser.add_argument("--runs", type=int, default=10)
    parser.add_argument("--warmups", type=int, default=2)
    parser.add_argument("--index", action="store_true", help="refresh the index first")
    args = parser.parse_args()
    if args.runs < 1 or args.warmups < 0:
        parser.error("runs must be positive and warmups must be nonnegative")

    executable = args.executable.resolve()
    repository = args.repository.resolve()
    if args.index:
        subprocess.run([str(executable), "index", str(repository)], check=True,
                       capture_output=True, text=True)
    command = [str(executable), "query", str(repository), args.query]
    for _ in range(args.warmups):
        subprocess.run(command, check=True, capture_output=True, text=True)

    samples = []
    for _ in range(args.runs):
        start = time.perf_counter()
        subprocess.run(command, check=True, capture_output=True, text=True)
        samples.append((time.perf_counter() - start) * 1000)
    samples.sort()
    p95 = samples[math.ceil(0.95 * len(samples)) - 1]
    print(f"runs={len(samples)} p50={statistics.median(samples):.3f} ms "
          f"p95={p95:.3f} ms min={samples[0]:.3f} ms "
          f"max={samples[-1]:.3f} ms")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
