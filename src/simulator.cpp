#include "simulator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace {
struct Core {
    std::optional<int> running;
    std::optional<int> switchingTo;
    int switchRemaining = 0;
    int quantumRemaining = 0;
    int previousPid = -1;
};
}

Simulator::Simulator(SimulationConfig config) : config_(config) {
    if (config_.numCores <= 0) throw std::invalid_argument("numCores must be positive");
    if (config_.contextSwitchOverhead < 0 ||
        config_.contextSwitchOverhead == std::numeric_limits<int>::max()) {
        throw std::invalid_argument("invalid context-switch overhead");
    }
}

SimulationResult Simulator::run(std::vector<ProcessControlBlock> processes,
                                Scheduler& scheduler) const {
    SimulationResult result;
    result.processes = std::move(processes);
    auto& jobs = result.processes;
    std::sort(jobs.begin(), jobs.end(), [](const auto& left, const auto& right) {
        return left.pid < right.pid;
    });

    std::unordered_map<int, std::size_t> processIndex;
    for (std::size_t i = 0; i < jobs.size(); ++i) {
        if (jobs[i].pid < 0 || (i > 0 && jobs[i - 1].pid == jobs[i].pid)) {
            throw std::invalid_argument("process IDs must be unique and non-negative");
        }
        if (jobs[i].arrivalTime < 0 || jobs[i].cpuBursts.empty() ||
            jobs[i].ioBursts.size() + 1 < jobs[i].cpuBursts.size() ||
            std::any_of(jobs[i].cpuBursts.begin(), jobs[i].cpuBursts.end(),
                        [](int burst) { return burst <= 0; }) ||
            std::any_of(jobs[i].ioBursts.begin(), jobs[i].ioBursts.end(),
                        [](int burst) { return burst <= 0; })) {
            throw std::invalid_argument("invalid process arrival or burst sequence");
        }
        jobs[i].state = ProcessState::New;
        jobs[i].cpuBurstIndex = 0;
        jobs[i].remainingBurst = jobs[i].cpuBursts.front();
        jobs[i].waitTime = 0;
        jobs[i].responseTime = -1;
        jobs[i].turnaroundTime = 0;
        jobs[i].completionTime = 0;
        jobs[i].contextSwitches = 0;
        processIndex.emplace(jobs[i].pid, i);
    }
    const auto indexOf = [&processIndex](int pid) {
        return processIndex.at(pid);
    };

    std::vector<Core> cores(static_cast<std::size_t>(config_.numCores));
    std::vector<std::pair<int, int>> ioCompletions;
    std::vector<int> readyDuration(jobs.size(), 0);
    std::vector<int> longestReadyDuration(jobs.size(), 0);
    int now = 0;
    const int processCount = static_cast<int>(jobs.size());

    while (result.terminated < processCount) {
        for (auto& process : jobs) {
            if (process.state == ProcessState::New && process.arrivalTime <= now) {
                process.state = ProcessState::Ready;
                readyDuration[processIndex.at(process.pid)] = 0;
                scheduler.addReady(process, now);
            }
        }

        auto io = ioCompletions.begin();
        while (io != ioCompletions.end()) {
            if (io->first <= now) {
                auto& process = jobs[indexOf(io->second)];
                process.state = ProcessState::Ready;
                readyDuration[indexOf(process.pid)] = 0;
                scheduler.addReady(process, now);
                io = ioCompletions.erase(io);
            } else {
                ++io;
            }
        }

        for (auto& core : cores) {
            if (!core.switchingTo) continue;
            const auto candidatePid = scheduler.peekNext(jobs, now);
            if (candidatePid &&
                scheduler.shouldPreempt(
                    jobs[indexOf(*core.switchingTo)],
                    jobs[indexOf(*candidatePid)])) {
                auto& process = jobs[indexOf(*core.switchingTo)];
                process.state = ProcessState::Ready;
                scheduler.onPreempt(process, now);
                core.switchingTo.reset();
                core.switchRemaining = 0;
            }
        }

        for (auto& core : cores) {
            if (!core.running) continue;
            auto candidatePid = scheduler.peekNext(jobs, now);
            if (candidatePid &&
                scheduler.shouldPreempt(
                    jobs[indexOf(*core.running)],
                    jobs[indexOf(*candidatePid)])) {
                const int pid = *core.running;
                auto& process = jobs[indexOf(pid)];
                process.state = ProcessState::Ready;
                readyDuration[indexOf(pid)] = 0;
                scheduler.onPreempt(process, now);
                core.previousPid = pid;
                core.running.reset();
                core.quantumRemaining = 0;
            }
        }

        for (auto& core : cores) {
            if (core.running || core.switchingTo) continue;
            const auto selected = scheduler.selectNext(jobs, now);
            if (!selected) continue;
            auto& process = jobs[indexOf(*selected)];
            if (config_.contextSwitchOverhead > 0 && core.previousPid != process.pid) {
                process.state = ProcessState::Switching;
                core.switchingTo = process.pid;
                // Include the current interval; CPU service starts after N full ticks.
                core.switchRemaining = config_.contextSwitchOverhead + 1;
                ++process.contextSwitches;
                ++result.totalContextSwitches;
            } else {
                process.state = ProcessState::Running;
                if (process.responseTime < 0) {
                    process.responseTime = now - process.arrivalTime;
                }
                core.running = process.pid;
                core.quantumRemaining = scheduler.quantum(process.pid);
            }
        }

        for (auto& core : cores) {
            if (!core.switchingTo) continue;
            --core.switchRemaining;
            if (core.switchRemaining == 0) {
                const int pid = *core.switchingTo;
                auto& process = jobs[indexOf(pid)];
                process.state = ProcessState::Running;
                if (process.responseTime < 0) {
                    process.responseTime = now - process.arrivalTime;
                }
                core.running = pid;
                core.quantumRemaining = scheduler.quantum(pid);
                core.switchingTo.reset();
            }
        }

        for (auto& core : cores) {
            if (!core.running) continue;
            auto& process = jobs[indexOf(*core.running)];
            --process.remainingBurst;
            ++result.busyCoreTicks;
            if (core.quantumRemaining > 0) --core.quantumRemaining;

            if (process.remainingBurst == 0) {
                core.previousPid = process.pid;
                core.running.reset();
                const std::size_t nextBurst = process.cpuBurstIndex + 1;
                if (nextBurst < process.cpuBursts.size()) {
                    process.cpuBurstIndex = nextBurst;
                    process.remainingBurst = process.cpuBursts[nextBurst];
                    process.state = ProcessState::WaitingForIo;
                    const int ioDuration = process.ioBursts[nextBurst - 1];
                    ioCompletions.emplace_back(now + 1 + ioDuration, process.pid);
                } else {
                    process.state = ProcessState::Terminated;
                    process.completionTime = now + 1;
                    process.turnaroundTime = process.completionTime - process.arrivalTime;
                    ++result.terminated;
                }
                continue;
            }

            if (core.quantumRemaining == 0 && scheduler.quantum(process.pid) > 0) {
                core.previousPid = process.pid;
                core.running.reset();
                process.state = ProcessState::Ready;
                readyDuration[indexOf(process.pid)] = 0;
                scheduler.onQuantumExpired(process, now + 1);
            }
        }

        for (std::size_t i = 0; i < jobs.size(); ++i) {
            auto& process = jobs[i];
            if (process.state == ProcessState::Ready ||
                process.state == ProcessState::Switching) {
                ++process.waitTime;
                ++readyDuration[i];
                longestReadyDuration[i] =
                    std::max(longestReadyDuration[i], readyDuration[i]);
            }
        }
        ++now;
        if (now == 2147483647) throw std::runtime_error("simulation time overflow");
    }

    result.elapsedTicks = now;
    for (const int waitDuration : longestReadyDuration) {
        if (waitDuration > config_.starvationThreshold) {
            ++result.starvedProcesses;
        }
    }
    return result;
}

double SimulationResult::averageWaitingTime() const {
    if (processes.empty()) return 0.0;
    long long total = 0;
    for (const auto& process : processes) total += process.waitTime;
    return static_cast<double>(total) / processes.size();
}

double SimulationResult::averageResponseTime() const {
    if (processes.empty()) return 0.0;
    long long total = 0;
    for (const auto& process : processes) total += std::max(0, process.responseTime);
    return static_cast<double>(total) / processes.size();
}

double SimulationResult::averageTurnaroundTime() const {
    if (processes.empty()) return 0.0;
    long long total = 0;
    for (const auto& process : processes) total += process.turnaroundTime;
    return static_cast<double>(total) / processes.size();
}

double SimulationResult::p95TurnaroundTime() const {
    if (processes.empty()) return 0.0;
    std::vector<int> values;
    values.reserve(processes.size());
    for (const auto& process : processes) values.push_back(process.turnaroundTime);
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(
        std::ceil(0.95 * static_cast<double>(values.size())) - 1);
    return values[index];
}

double SimulationResult::cpuUtilization(int cores) const {
    if (cores <= 0 || elapsedTicks == 0) return 0.0;
    return 100.0 * static_cast<double>(busyCoreTicks) /
           (static_cast<double>(elapsedTicks) * cores);
}
