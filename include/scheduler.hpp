#pragma once

#include "pcb.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>

class Scheduler {
public:
    virtual ~Scheduler() = default;
    virtual void addReady(const ProcessControlBlock& process, int now) = 0;
    virtual std::optional<int> peekNext(
        const std::vector<ProcessControlBlock>& processes, int now) = 0;
    virtual std::optional<int> selectNext(
        const std::vector<ProcessControlBlock>& processes, int now) = 0;
    virtual void onPreempt(const ProcessControlBlock& process, int now) = 0;
    virtual void onQuantumExpired(const ProcessControlBlock& process, int now) = 0;
    virtual bool shouldPreempt(const ProcessControlBlock& running,
                               const ProcessControlBlock& candidate) const = 0;
    virtual int quantum(int pid) const = 0;
    virtual const char* name() const = 0;
};

class FifoScheduler : public Scheduler {
public:
    explicit FifoScheduler(int quantum);
    void addReady(const ProcessControlBlock& process, int now) override;
    std::optional<int> peekNext(
        const std::vector<ProcessControlBlock>& processes, int now) override;
    std::optional<int> selectNext(
        const std::vector<ProcessControlBlock>& processes, int now) override;
    void onPreempt(const ProcessControlBlock& process, int now) override;
    void onQuantumExpired(const ProcessControlBlock& process, int now) override;
    bool shouldPreempt(const ProcessControlBlock& running,
                       const ProcessControlBlock& candidate) const override;
    int quantum(int pid) const override;

protected:
    std::deque<int> ready_;
    int quantum_;
};

class FCFSScheduler final : public FifoScheduler {
public:
    FCFSScheduler();
    const char* name() const override;
};

class RoundRobinScheduler final : public FifoScheduler {
public:
    explicit RoundRobinScheduler(int timeQuantum = 4);
    const char* name() const override;
};

class PreemptiveSRTFScheduler final : public Scheduler {
public:
    void addReady(const ProcessControlBlock& process, int now) override;
    std::optional<int> peekNext(
        const std::vector<ProcessControlBlock>& processes, int now) override;
    std::optional<int> selectNext(
        const std::vector<ProcessControlBlock>& processes, int now) override;
    void onPreempt(const ProcessControlBlock& process, int now) override;
    void onQuantumExpired(const ProcessControlBlock& process, int now) override;
    bool shouldPreempt(const ProcessControlBlock& running,
                       const ProcessControlBlock& candidate) const override;
    int quantum(int pid) const override;
    const char* name() const override;

private:
    struct Entry {
        int remaining;
        int arrival;
        std::uint64_t sequence;
        int pid;
    };
    struct LaterEntry {
        bool operator()(const Entry& left, const Entry& right) const;
    };
    std::priority_queue<Entry, std::vector<Entry>, LaterEntry> ready_;
    std::uint64_t sequence_ = 0;
};

class MLFQScheduler final : public Scheduler {
public:
    explicit MLFQScheduler(int agingInterval = 200,
                           std::array<int, 3> quanta = {{4, 8, 16}});
    void addReady(const ProcessControlBlock& process, int now) override;
    std::optional<int> peekNext(
        const std::vector<ProcessControlBlock>& processes, int now) override;
    std::optional<int> selectNext(
        const std::vector<ProcessControlBlock>& processes, int now) override;
    void onPreempt(const ProcessControlBlock& process, int now) override;
    void onQuantumExpired(const ProcessControlBlock& process, int now) override;
    bool shouldPreempt(const ProcessControlBlock& running,
                       const ProcessControlBlock& candidate) const override;
    int quantum(int pid) const override;
    const char* name() const override;

private:
    void age(int now);
    void enqueueAtLevel(int pid, int level, int now);
    std::array<std::deque<int>, 3> queues_;
    std::unordered_map<int, int> levels_;
    std::unordered_map<int, int> readySince_;
    std::array<int, 3> quanta_;
    int agingInterval_;
};

std::vector<std::unique_ptr<Scheduler>> makeAllSchedulers();
