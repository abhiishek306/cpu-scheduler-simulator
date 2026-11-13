#include "deadlock_manager.hpp"
#include "scheduler.hpp"
#include "simulator.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<ProcessControlBlock> cpuWorkload(int count, int burst) {
    std::vector<ProcessControlBlock> processes;
    for (int pid = 0; pid < count; ++pid) {
        ProcessControlBlock process;
        process.pid = pid;
        process.arrivalTime = 0;
        process.cpuBursts = {burst};
        processes.push_back(process);
    }
    return processes;
}

void allSchedulersTerminateEveryProcess() {
    for (auto& scheduler : makeAllSchedulers()) {
        const auto result = Simulator().run(cpuWorkload(24, 13), *scheduler);
        require(result.terminated == 24, "not all processes terminated");
    }
}

void mlfqAgingBoundsWaitingTime() {
    MLFQScheduler scheduler(8);
    SimulationConfig config;
    config.numCores = 1;
    config.contextSwitchOverhead = 0;
    config.starvationThreshold = 100;
    const auto result = Simulator(config).run(cpuWorkload(12, 160), scheduler);
    require(result.terminated == 12, "MLFQ workload did not complete");
    require(result.starvedProcesses == 0, "MLFQ aging did not prevent starvation");
}

void bankerRejectsUnsafeGrant() {
    DeadlockManager manager({3});
    manager.registerProcess(1, {3});
    manager.registerProcess(2, {3});
    require(manager.requestResourcesBanker(1, {2}), "safe allocation rejected");
    require(!manager.requestResourcesBanker(2, {1}),
            "Banker's algorithm granted an unsafe request");
    manager.releaseResources(1, {1});
    require(manager.available().front() == 2,
            "unsafe pending Banker request bypassed the safety check");
    manager.releaseResources(1, {1});
    require(manager.available().front() == 2,
            "safe pending request was not granted after resources were released");
}

void waitForGraphFindsAndResolvesCycle() {
    DeadlockManager manager({1, 1});
    manager.registerProcess(1, {1, 1}, 0);
    manager.registerProcess(2, {1, 1}, 5);
    require(manager.requestResourcesUnprotected(1, {1, 0}), "resource X not granted");
    require(manager.requestResourcesUnprotected(2, {0, 1}), "resource Y not granted");
    require(!manager.requestResourcesUnprotected(1, {0, 1}), "P1 should block");
    require(!manager.requestResourcesUnprotected(2, {1, 0}), "P2 should block");
    require(manager.hasWaitForCycle(), "WFG missed a circular wait");
    require(manager.detectAndResolveDeadlock() == 1, "deadlock was not resolved");
    require(!manager.hasWaitForCycle(), "cycle remains after victim recovery");
}
}

int main() {
    try {
        allSchedulersTerminateEveryProcess();
        mlfqAgingBoundsWaitingTime();
        bankerRejectsUnsafeGrant();
        waitForGraphFindsAndResolvesCycle();
        std::cout << "All scheduler and deadlock invariants passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Invariant test failed: " << error.what() << '\n';
        return 1;
    }
}
