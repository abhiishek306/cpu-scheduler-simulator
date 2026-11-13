# CPU Scheduler & Deadlock Simulator

A small, zero-third-party-dependency C++20 discrete-event simulator. It models
multiple CPU cores, CPU/I/O bursts, context-switch cost, four scheduling
policies, Banker's resource-allocation safety checks, and wait-for-graph
deadlock recovery.

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/scheduler_sim --benchmark
```

On Windows, run `build\scheduler_sim.exe --benchmark` after building.

The standalone deterministic CSV generator uses only the Python standard
library and accepts process counts from 100 through 500:

```sh
python traces/generate_traces.py --processes 200 --seed 20261114
```

It writes one trace per workload under `traces/generated/` by default; override
the destination with `--output-dir`.

The built-in benchmark uses seeded workloads of 200 processes each. All
schedulers receive identical process traces. Waiting time counts time READY
(including context-switch delay); response time ends at first CPU service;
turnaround is completion minus arrival. CPU utilization excludes context-switch
ticks. A process is counted as starved when one uninterrupted READY wait
exceeds 1000 ticks. MLFQ promotes queued jobs after the configured aging interval
and uses quanta of 4, 8, and 16 ticks (default aging interval: 200 ticks).

In the deadlock resource manager, smaller numeric priority values mean higher
priority. On a detected WFG cycle, the manager preempts all allocations held by
the lowest-priority process in that cycle and retries newly satisfiable
requests.
