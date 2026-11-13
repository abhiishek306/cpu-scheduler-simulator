#include "scheduler.hpp"

#include <algorithm>
#include <stdexcept>

FifoScheduler::FifoScheduler(int quantum) : quantum_(quantum) {}

void FifoScheduler::addReady(const ProcessControlBlock& process, int) {
    ready_.push_back(process.pid);
}

std::optional<int> FifoScheduler::peekNext(
    const std::vector<ProcessControlBlock>&, int) {
    if (ready_.empty()) return std::nullopt;
    return ready_.front();
}

std::optional<int> FifoScheduler::selectNext(
    const std::vector<ProcessControlBlock>&, int) {
    if (ready_.empty()) return std::nullopt;
    const int pid = ready_.front();
    ready_.pop_front();
    return pid;
}

void FifoScheduler::onPreempt(const ProcessControlBlock& process, int now) {
    addReady(process, now);
}

void FifoScheduler::onQuantumExpired(const ProcessControlBlock& process, int now) {
    addReady(process, now);
}

bool FifoScheduler::shouldPreempt(const ProcessControlBlock&,
                                  const ProcessControlBlock&) const {
    return false;
}

int FifoScheduler::quantum(int) const {
    return quantum_;
}

FCFSScheduler::FCFSScheduler() : FifoScheduler(0) {}

const char* FCFSScheduler::name() const {
    return "FCFS";
}

RoundRobinScheduler::RoundRobinScheduler(int timeQuantum)
    : FifoScheduler(timeQuantum) {
    if (timeQuantum <= 0) throw std::invalid_argument("time quantum must be positive");
}

const char* RoundRobinScheduler::name() const {
    return "Round Robin";
}

bool PreemptiveSRTFScheduler::LaterEntry::operator()(
    const Entry& left, const Entry& right) const {
    if (left.remaining != right.remaining) return left.remaining > right.remaining;
    if (left.arrival != right.arrival) return left.arrival > right.arrival;
    return left.sequence > right.sequence;
}

void PreemptiveSRTFScheduler::addReady(const ProcessControlBlock& process, int) {
    ready_.push({process.remainingBurst, process.arrivalTime, sequence_++, process.pid});
}

std::optional<int> PreemptiveSRTFScheduler::peekNext(
    const std::vector<ProcessControlBlock>&, int) {
    if (ready_.empty()) return std::nullopt;
    return ready_.top().pid;
}

std::optional<int> PreemptiveSRTFScheduler::selectNext(
    const std::vector<ProcessControlBlock>&, int) {
    if (ready_.empty()) return std::nullopt;
    const int pid = ready_.top().pid;
    ready_.pop();
    return pid;
}

void PreemptiveSRTFScheduler::onPreempt(const ProcessControlBlock& process, int now) {
    addReady(process, now);
}

void PreemptiveSRTFScheduler::onQuantumExpired(
    const ProcessControlBlock& process, int now) {
    addReady(process, now);
}

bool PreemptiveSRTFScheduler::shouldPreempt(
    const ProcessControlBlock& running, const ProcessControlBlock& candidate) const {
    return candidate.remainingBurst < running.remainingBurst;
}

int PreemptiveSRTFScheduler::quantum(int) const {
    return 0;
}

const char* PreemptiveSRTFScheduler::name() const {
    return "Preemptive SRTF";
}

MLFQScheduler::MLFQScheduler(int agingInterval, std::array<int, 3> quanta)
    : quanta_(quanta), agingInterval_(agingInterval) {
    if (agingInterval <= 0) throw std::invalid_argument("aging interval must be positive");
    if (std::any_of(quanta_.begin(), quanta_.end(),
                    [](int value) { return value <= 0; })) {
        throw std::invalid_argument("MLFQ quanta must be positive");
    }
}

void MLFQScheduler::enqueueAtLevel(int pid, int level, int now) {
    level = std::max(0, std::min(2, level));
    levels_[pid] = level;
    readySince_[pid] = now;
    queues_[static_cast<std::size_t>(level)].push_back(pid);
}

void MLFQScheduler::addReady(const ProcessControlBlock& process, int now) {
    enqueueAtLevel(process.pid, 0, now);
}

void MLFQScheduler::age(int now) {
    for (int level = 1; level < 3; ++level) {
        auto& queue = queues_[static_cast<std::size_t>(level)];
        std::deque<int> keep;
        while (!queue.empty()) {
            const int pid = queue.front();
            queue.pop_front();
            const auto since = readySince_.find(pid);
            if (since != readySince_.end() && now - since->second >= agingInterval_) {
                enqueueAtLevel(pid, 0, now);
            } else {
                keep.push_back(pid);
            }
        }
        queue.swap(keep);
    }
}

std::optional<int> MLFQScheduler::peekNext(
    const std::vector<ProcessControlBlock>&, int now) {
    age(now);
    for (const auto& queue : queues_) {
        if (!queue.empty()) return queue.front();
    }
    return std::nullopt;
}

std::optional<int> MLFQScheduler::selectNext(
    const std::vector<ProcessControlBlock>&, int now) {
    age(now);
    for (auto& queue : queues_) {
        if (!queue.empty()) {
            const int pid = queue.front();
            queue.pop_front();
            readySince_.erase(pid);
            return pid;
        }
    }
    return std::nullopt;
}

void MLFQScheduler::onPreempt(const ProcessControlBlock& process, int now) {
    const auto found = levels_.find(process.pid);
    enqueueAtLevel(process.pid, found == levels_.end() ? 0 : found->second, now);
}

void MLFQScheduler::onQuantumExpired(const ProcessControlBlock& process, int now) {
    const auto found = levels_.find(process.pid);
    const int level = found == levels_.end() ? 0 : found->second;
    enqueueAtLevel(process.pid, std::min(2, level + 1), now);
}

bool MLFQScheduler::shouldPreempt(
    const ProcessControlBlock& running, const ProcessControlBlock& candidate) const {
    const auto runLevel = levels_.find(running.pid);
    const auto candidateLevel = levels_.find(candidate.pid);
    if (runLevel == levels_.end() || candidateLevel == levels_.end()) return false;
    return candidateLevel->second < runLevel->second;
}

int MLFQScheduler::quantum(int pid) const {
    const auto found = levels_.find(pid);
    return quanta_[static_cast<std::size_t>(
        found == levels_.end() ? 0 : found->second)];
}

const char* MLFQScheduler::name() const {
    return "MLFQ";
}

std::vector<std::unique_ptr<Scheduler>> makeAllSchedulers() {
    std::vector<std::unique_ptr<Scheduler>> schedulers;
    schedulers.emplace_back(new FCFSScheduler());
    schedulers.emplace_back(new RoundRobinScheduler());
    schedulers.emplace_back(new PreemptiveSRTFScheduler());
    schedulers.emplace_back(new MLFQScheduler());
    return schedulers;
}
