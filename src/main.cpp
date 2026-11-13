#include "deadlock_manager.hpp"
#include "scheduler.hpp"
#include "simulator.hpp"

#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
struct Workload {
    std::string name;
    std::vector<ProcessControlBlock> processes;
};

Workload makeWorkload(const std::string& name, unsigned seed) {
    std::mt19937 random(seed);
    std::uniform_int_distribution<int> arrival(0, 100);
    std::vector<ProcessControlBlock> processes;
    processes.reserve(200);
    for (int pid = 0; pid < 200; ++pid) {
        ProcessControlBlock process;
        process.pid = pid;
        process.arrivalTime = arrival(random);
        process.priority = static_cast<int>(random() % 8);

        int cpuCount = 1;
        int cpuLow = 4;
        int cpuHigh = 12;
        int ioLow = 1;
        int ioHigh = 4;
        if (name == "IO_BOUND") {
            cpuCount = 4;
            cpuLow = 1;
            cpuHigh = 3;
            ioLow = 8;
            ioHigh = 16;
        } else if (name == "MIXED") {
            cpuCount = 3;
            cpuLow = 2;
            cpuHigh = 12;
            ioLow = 2;
            ioHigh = 9;
        } else if (name == "DEADLOCK_CONTENTION") {
            cpuCount = 2;
            cpuLow = 3;
            cpuHigh = 10;
            ioLow = 1;
            ioHigh = 5;
        } else {
            cpuLow = 10;
            cpuHigh = 24;
        }

        for (int burst = 0; burst < cpuCount; ++burst) {
            process.cpuBursts.push_back(
                cpuLow + static_cast<int>(random() % (cpuHigh - cpuLow + 1)));
            if (burst + 1 < cpuCount) {
                process.ioBursts.push_back(
                    ioLow + static_cast<int>(random() % (ioHigh - ioLow + 1)));
            }
        }
        processes.push_back(std::move(process));
    }
    return {name, std::move(processes)};
}

void printSchedulerBenchmark(const std::vector<Workload>& workloads) {
    std::cout << "\nCPU scheduling benchmark (200 processes per workload)\n";
    std::cout << std::left << std::setw(22) << "Workload"
              << std::setw(19) << "Scheduler"
              << std::right << std::setw(11) << "Avg Wait"
              << std::setw(12) << "Avg Resp"
              << std::setw(12) << "Avg TAT"
              << std::setw(12) << "p95 TAT"
              << std::setw(12) << "CPU %"
              << std::setw(13) << "Switches"
              << std::setw(11) << "Starved" << '\n';
    std::cout << std::string(112, '-') << '\n';

    for (const auto& workload : workloads) {
        for (auto& scheduler : makeAllSchedulers()) {
            const auto result = Simulator().run(workload.processes, *scheduler);
            std::cout << std::left << std::setw(22) << workload.name
                      << std::setw(19) << scheduler->name()
                      << std::right << std::fixed << std::setprecision(2)
                      << std::setw(11) << result.averageWaitingTime()
                      << std::setw(12) << result.averageResponseTime()
                      << std::setw(12) << result.averageTurnaroundTime()
                      << std::setw(12) << result.p95TurnaroundTime()
                      << std::setw(12) << result.cpuUtilization(2)
                      << std::setw(13) << result.totalContextSwitches
                      << std::setw(11) << result.starvedProcesses << '\n';
        }
    }
}

void printDeadlockBenchmark() {
    std::cout << "\nDeadlock benchmark (4 processes, 4 single-instance resources)\n";
    DeadlockManager unprotected({1, 1, 1, 1});
    for (int pid = 0; pid < 4; ++pid) {
        std::vector<int> maximum(4, 0);
        maximum[static_cast<std::size_t>(pid)] = 1;
        maximum[static_cast<std::size_t>((pid + 1) % 4)] = 1;
        unprotected.registerProcess(pid, maximum, pid);
    }
    for (int pid = 0; pid < 4; ++pid) {
        std::vector<int> own(4, 0);
        own[static_cast<std::size_t>(pid)] = 1;
        unprotected.requestResourcesUnprotected(pid, own);
    }
    for (int pid = 0; pid < 4; ++pid) {
        std::vector<int> next(4, 0);
        next[static_cast<std::size_t>((pid + 1) % 4)] = 1;
        unprotected.requestResourcesUnprotected(pid, next);
    }
    const int resolved = unprotected.detectAndResolveDeadlock();

    DeadlockManager banker({1, 1, 1, 1});
    for (int pid = 0; pid < 4; ++pid) {
        std::vector<int> maximum(4, 0);
        maximum[static_cast<std::size_t>(pid)] = 1;
        maximum[static_cast<std::size_t>((pid + 1) % 4)] = 1;
        banker.registerProcess(pid, maximum, pid);
    }
    int safeSequences = 0;
    for (int pid = 0; pid < 4; ++pid) {
        std::vector<int> own(4, 0);
        own[static_cast<std::size_t>(pid)] = 1;
        if (banker.requestResourcesBanker(pid, own) && banker.isSafe()) {
            ++safeSequences;
        }
    }
    std::cout << "Allocation strategy | Deadlocks detected | Resolved | Safe sequences verified\n";
    std::cout << "Unprotected         | " << std::setw(18)
              << unprotected.deadlocksDetected() << " | " << std::setw(8) << resolved
              << " | " << std::setw(23) << 0 << '\n';
    std::cout << "Banker's Algorithm  | " << std::setw(18)
              << banker.deadlocksDetected() << " | " << std::setw(8) << 0
              << " | " << std::setw(23) << safeSequences << '\n';
}

void runBenchmark() {
    const std::vector<Workload> workloads = {
        makeWorkload("CPU_BOUND", 202601),
        makeWorkload("IO_BOUND", 202602),
        makeWorkload("MIXED", 202603),
        makeWorkload("DEADLOCK_CONTENTION", 202604)
    };
    printSchedulerBenchmark(workloads);
    printDeadlockBenchmark();
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--benchmark") {
            runBenchmark();
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--help") {
            std::cout << "Usage: scheduler_sim --benchmark\n";
            return 0;
        }
        std::cerr << "Usage: scheduler_sim --benchmark\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "Simulation error: " << error.what() << '\n';
        return 1;
    }
}
