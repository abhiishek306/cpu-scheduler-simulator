#pragma once

#include <vector>
#include <cstddef>

enum class ProcessState {
    New,
    Ready,
    Switching,
    Running,
    WaitingForIo,
    BlockedForResource,
    Terminated
};

struct ProcessControlBlock {
    int pid = -1;
    int arrivalTime = 0;
    int priority = 0;
    std::vector<int> cpuBursts;
    std::vector<int> ioBursts;
    int remainingBurst = 0;
    int waitTime = 0;
    int responseTime = -1;
    int turnaroundTime = 0;
    int contextSwitches = 0;

    ProcessState state = ProcessState::New;
    std::size_t cpuBurstIndex = 0;
    int completionTime = 0;
};
