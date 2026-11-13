#pragma once

#include <cstddef>
#include <vector>

class DeadlockManager {
public:
    explicit DeadlockManager(std::vector<int> resourceTotals);

    void registerProcess(int pid, std::vector<int> maximumDemand, int priority = 0);
    bool requestResourcesBanker(int pid, const std::vector<int>& request);
    bool requestResourcesUnprotected(int pid, const std::vector<int>& request);
    void releaseResources(int pid, const std::vector<int>& release);
    int detectAndResolveDeadlock();

    bool hasWaitForCycle() const;
    bool isSafe() const;
    const std::vector<int>& lastSafeSequence() const;
    int deadlocksDetected() const;
    int deadlocksResolved() const;
    const std::vector<int>& available() const;

private:
    struct ProcessResources {
        int pid;
        int priority;
        std::vector<int> maximum;
        std::vector<int> allocation;
        std::vector<int> pending;
        bool blocked = false;
        bool bankerRequest = false;
    };

    std::size_t indexOf(int pid) const;
    bool validVector(const std::vector<int>& values) const;
    bool safeSequence(std::vector<int>* sequence) const;
    bool waitForCycle(std::vector<int>* cycle = nullptr) const;
    void releaseAll(ProcessResources& process);
    void retryBlocked();

    std::vector<int> totals_;
    std::vector<int> available_;
    std::vector<ProcessResources> processes_;
    std::vector<int> lastSafeSequence_;
    int deadlocksDetected_ = 0;
    int deadlocksResolved_ = 0;
};
