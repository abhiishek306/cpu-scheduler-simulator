#include "deadlock_manager.hpp"

#include <algorithm>
#include <functional>
#include <numeric>
#include <stdexcept>
#include <utility>

DeadlockManager::DeadlockManager(std::vector<int> resourceTotals)
    : totals_(std::move(resourceTotals)), available_(totals_) {
    if (totals_.empty() ||
        std::any_of(totals_.begin(), totals_.end(),
                    [](int amount) { return amount < 0; })) {
        throw std::invalid_argument("resource totals must be non-empty and non-negative");
    }
}

bool DeadlockManager::validVector(const std::vector<int>& values) const {
    return values.size() == totals_.size() &&
           std::all_of(values.begin(), values.end(),
                       [](int amount) { return amount >= 0; });
}

std::size_t DeadlockManager::indexOf(int pid) const {
    const auto found = std::find_if(
        processes_.begin(), processes_.end(),
        [pid](const ProcessResources& process) { return process.pid == pid; });
    if (found == processes_.end()) throw std::out_of_range("unknown process ID");
    return static_cast<std::size_t>(found - processes_.begin());
}

void DeadlockManager::registerProcess(int pid, std::vector<int> maximumDemand,
                                      int priority) {
    if (pid < 0 || !validVector(maximumDemand)) {
        throw std::invalid_argument("invalid process ID or maximum-demand vector");
    }
    for (std::size_t i = 0; i < maximumDemand.size(); ++i) {
        if (maximumDemand[i] > totals_[i]) {
            throw std::invalid_argument("maximum demand exceeds resource totals");
        }
    }
    if (std::any_of(processes_.begin(), processes_.end(),
                    [pid](const ProcessResources& process) { return process.pid == pid; })) {
        throw std::invalid_argument("process ID is already registered");
    }
    processes_.push_back({pid, priority, std::move(maximumDemand),
                          std::vector<int>(totals_.size(), 0),
                          std::vector<int>(totals_.size(), 0), false});
}

bool DeadlockManager::safeSequence(std::vector<int>* sequence) const {
    std::vector<int> work = available_;
    std::vector<bool> finish(processes_.size(), false);
    std::vector<int> order;

    bool progress = true;
    while (progress) {
        progress = false;
        for (std::size_t i = 0; i < processes_.size(); ++i) {
            if (finish[i]) continue;
            const auto& process = processes_[i];
            bool canFinish = true;
            for (std::size_t r = 0; r < totals_.size(); ++r) {
                const int need = process.maximum[r] - process.allocation[r];
                if (need > work[r]) {
                    canFinish = false;
                    break;
                }
            }
            if (!canFinish) continue;
            for (std::size_t r = 0; r < totals_.size(); ++r) {
                work[r] += process.allocation[r];
            }
            finish[i] = true;
            order.push_back(process.pid);
            progress = true;
        }
    }
    const bool safe = std::all_of(finish.begin(), finish.end(),
                                  [](bool value) { return value; });
    if (safe && sequence != nullptr) *sequence = std::move(order);
    return safe;
}

bool DeadlockManager::requestResourcesBanker(
    int pid, const std::vector<int>& request) {
    if (!validVector(request)) throw std::invalid_argument("invalid resource request");
    auto& process = processes_[indexOf(pid)];
    if (process.blocked) throw std::logic_error("process already has a pending request");
    for (std::size_t r = 0; r < request.size(); ++r) {
        if (request[r] > process.maximum[r] - process.allocation[r]) {
            throw std::invalid_argument("request exceeds declared maximum demand");
        }
        if (request[r] > available_[r]) {
            process.pending = request;
            process.blocked = true;
            process.bankerRequest = true;
            return false;
        }
    }

    for (std::size_t r = 0; r < request.size(); ++r) {
        available_[r] -= request[r];
        process.allocation[r] += request[r];
    }
    std::vector<int> sequence;
    if (!safeSequence(&sequence)) {
        for (std::size_t r = 0; r < request.size(); ++r) {
            available_[r] += request[r];
            process.allocation[r] -= request[r];
        }
        process.pending = request;
        process.blocked = true;
        process.bankerRequest = true;
        return false;
    }
    process.pending.assign(request.size(), 0);
    process.blocked = false;
    process.bankerRequest = false;
    lastSafeSequence_ = std::move(sequence);
    return true;
}

bool DeadlockManager::requestResourcesUnprotected(
    int pid, const std::vector<int>& request) {
    if (!validVector(request)) throw std::invalid_argument("invalid resource request");
    auto& process = processes_[indexOf(pid)];
    if (process.blocked) throw std::logic_error("process already has a pending request");
    for (std::size_t r = 0; r < request.size(); ++r) {
        if (request[r] > process.maximum[r] - process.allocation[r]) {
            throw std::invalid_argument("request exceeds declared maximum demand");
        }
        if (request[r] > available_[r]) {
            process.pending = request;
            process.blocked = true;
            process.bankerRequest = false;
            return false;
        }
    }
    for (std::size_t r = 0; r < request.size(); ++r) {
        available_[r] -= request[r];
        process.allocation[r] += request[r];
    }
    process.pending.assign(request.size(), 0);
    process.blocked = false;
    process.bankerRequest = false;
    return true;
}

void DeadlockManager::releaseResources(int pid, const std::vector<int>& release) {
    if (!validVector(release)) throw std::invalid_argument("invalid release vector");
    auto& process = processes_[indexOf(pid)];
    for (std::size_t r = 0; r < release.size(); ++r) {
        if (release[r] > process.allocation[r]) {
            throw std::invalid_argument("cannot release unallocated resources");
        }
    }
    for (std::size_t r = 0; r < release.size(); ++r) {
        process.allocation[r] -= release[r];
        available_[r] += release[r];
    }
    retryBlocked();
}

void DeadlockManager::releaseAll(ProcessResources& process) {
    for (std::size_t r = 0; r < totals_.size(); ++r) {
        available_[r] += process.allocation[r];
        process.allocation[r] = 0;
    }
    process.blocked = false;
    process.bankerRequest = false;
    process.pending.assign(totals_.size(), 0);
}

bool DeadlockManager::waitForCycle(std::vector<int>* cycle) const {
    const std::size_t count = processes_.size();
    std::vector<std::vector<std::size_t>> graph(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& waiter = processes_[i];
        if (!waiter.blocked) continue;
        for (std::size_t j = 0; j < count; ++j) {
            const auto& holder = processes_[j];
            bool edge = false;
            for (std::size_t r = 0; r < totals_.size(); ++r) {
                if (waiter.pending[r] > 0 && holder.allocation[r] > 0) {
                    edge = true;
                    break;
                }
            }
            if (edge) graph[i].push_back(j);
        }
    }

    std::vector<int> color(count, 0);
    std::vector<std::size_t> parent(count, count);
    std::vector<std::size_t> foundCycle;
    std::function<bool(std::size_t)> visit = [&](std::size_t node) {
        color[node] = 1;
        for (const std::size_t next : graph[node]) {
            if (color[next] == 0) {
                parent[next] = node;
                if (visit(next)) return true;
            } else if (color[next] == 1) {
                foundCycle.push_back(next);
                for (std::size_t cursor = node; cursor != next; cursor = parent[cursor]) {
                    if (cursor >= count || parent[cursor] >= count) break;
                    foundCycle.push_back(cursor);
                }
                return true;
            }
        }
        color[node] = 2;
        return false;
    };

    for (std::size_t i = 0; i < count; ++i) {
        if (color[i] == 0 && visit(i)) break;
    }
    if (foundCycle.empty()) return false;
    if (cycle != nullptr) {
        cycle->clear();
        for (const auto index : foundCycle) cycle->push_back(processes_[index].pid);
    }
    return true;
}

void DeadlockManager::retryBlocked() {
    bool progress = true;
    while (progress) {
        progress = false;
        for (auto& process : processes_) {
            if (!process.blocked) continue;
            bool available = true;
            for (std::size_t r = 0; r < totals_.size(); ++r) {
                if (process.pending[r] > available_[r]) {
                    available = false;
                    break;
                }
            }
            if (!available) continue;
            for (std::size_t r = 0; r < totals_.size(); ++r) {
                available_[r] -= process.pending[r];
                process.allocation[r] += process.pending[r];
            }
            process.blocked = false;
            if (process.bankerRequest && !safeSequence(nullptr)) {
                process.blocked = true;
                for (std::size_t r = 0; r < totals_.size(); ++r) {
                    available_[r] += process.pending[r];
                    process.allocation[r] -= process.pending[r];
                }
                continue;
            }
            process.pending.assign(totals_.size(), 0);
            process.blocked = false;
            process.bankerRequest = false;
            progress = true;
        }
    }
}

int DeadlockManager::detectAndResolveDeadlock() {
    int resolved = 0;
    std::vector<int> cycle;
    while (waitForCycle(&cycle)) {
        ++deadlocksDetected_;
        auto victim = std::find_if(
            processes_.begin(), processes_.end(),
            [&cycle](const ProcessResources& process) {
                return std::find(cycle.begin(), cycle.end(), process.pid) != cycle.end();
            });
        for (auto it = processes_.begin(); it != processes_.end(); ++it) {
            if (std::find(cycle.begin(), cycle.end(), it->pid) != cycle.end() &&
                (victim == processes_.end() || it->priority > victim->priority)) {
                victim = it;
            }
        }
        if (victim == processes_.end()) break;
        releaseAll(*victim);
        ++deadlocksResolved_;
        ++resolved;
        retryBlocked();
    }
    return resolved;
}

bool DeadlockManager::hasWaitForCycle() const {
    return waitForCycle();
}

bool DeadlockManager::isSafe() const {
    return safeSequence(nullptr);
}

const std::vector<int>& DeadlockManager::lastSafeSequence() const {
    return lastSafeSequence_;
}

int DeadlockManager::deadlocksDetected() const {
    return deadlocksDetected_;
}

int DeadlockManager::deadlocksResolved() const {
    return deadlocksResolved_;
}

const std::vector<int>& DeadlockManager::available() const {
    return available_;
}
