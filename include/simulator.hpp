#pragma once

#include "pcb.hpp"
#include "scheduler.hpp"

#include <cstddef>
#include <vector>

struct SimulationConfig {
    int numCores = 2;
    int contextSwitchOverhead = 1;
    int starvationThreshold = 1000;
};

struct SimulationResult {
    std::vector<ProcessControlBlock> processes;
    int terminated = 0;
    int totalContextSwitches = 0;
    int elapsedTicks = 0;
    long long busyCoreTicks = 0;
    int starvedProcesses = 0;

    double averageWaitingTime() const;
    double averageResponseTime() const;
    double averageTurnaroundTime() const;
    double p95TurnaroundTime() const;
    double cpuUtilization(int cores) const;
};

class Simulator {
public:
    explicit Simulator(SimulationConfig config = {});
    SimulationResult run(std::vector<ProcessControlBlock> processes,
                         Scheduler& scheduler) const;

private:
    SimulationConfig config_;
};
