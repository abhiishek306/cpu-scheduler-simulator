#!/usr/bin/env python3
"""Generate reproducible CSV workloads for the CPU scheduler simulator."""

import argparse
import csv
import random
from pathlib import Path


WORKLOADS = {
    "CPU_BOUND": (1, 10, 24, 1, 4),
    "IO_BOUND": (4, 1, 3, 8, 16),
    "MIXED": (3, 2, 12, 2, 9),
    "DEADLOCK_CONTENTION": (2, 3, 10, 1, 5),
}


def generate_workload(name, process_count, seed):
    rng = random.Random(seed)
    burst_count, cpu_min, cpu_max, io_min, io_max = WORKLOADS[name]
    processes = []
    for pid in range(process_count):
        cpu_bursts = [rng.randint(cpu_min, cpu_max) for _ in range(burst_count)]
        io_bursts = [
            rng.randint(io_min, io_max) for _ in range(burst_count - 1)
        ]
        processes.append(
            {
                "pid": pid,
                "arrival_time": rng.randint(0, 100),
                "priority": rng.randrange(8),
                "cpu_bursts": ";".join(map(str, cpu_bursts)),
                "io_bursts": ";".join(map(str, io_bursts)),
            }
        )
    return processes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--processes", type=int, default=200)
    parser.add_argument("--seed", type=int, default=20261114)
    parser.add_argument(
        "--output-dir", type=Path, default=Path("traces/generated")
    )
    args = parser.parse_args()
    if not 100 <= args.processes <= 500:
        parser.error("--processes must be between 100 and 500")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    columns = ("pid", "arrival_time", "priority", "cpu_bursts", "io_bursts")
    for index, name in enumerate(WORKLOADS):
        path = args.output_dir / f"{name.lower()}.csv"
        with path.open("w", newline="", encoding="utf-8") as trace_file:
            writer = csv.DictWriter(trace_file, fieldnames=columns)
            writer.writeheader()
            writer.writerows(
                generate_workload(name, args.processes, args.seed + index)
            )
        print(f"Wrote {args.processes} processes: {path}")


if __name__ == "__main__":
    main()
