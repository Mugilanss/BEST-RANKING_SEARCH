#pragma once
#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <thread>
#include <sstream>
#include <chrono>
#include <unordered_map>

class Metrics {
public:
    Metrics();

    // Record latency in ms (adds to histogram + total)
    void observeQueryLatencyMs(uint64_t ms);

    // Record query execution (cacheHit = true if LRUCache hit)
    void observeQuery(bool cacheHit);

    // Increment an arbitrary counter (useful for indexing stats)
    void incrementCounter(const std::string &key, uint64_t delta = 1);
    std::string report() const;
    void startPeriodicReport(int intervalSeconds = 30);
    void stopPeriodicReport();
    ~Metrics();

    uint64_t getQueries()        const { return queries.load(); }
    uint64_t getCacheHits()      const { return cacheHits.load(); }
    uint64_t getTotalLatencyMs() const { return totalLatencyMs.load(); }

private:
    std::atomic<uint64_t> queries{0};
    std::atomic<uint64_t> cacheHits{0};
    std::atomic<uint64_t> totalLatencyMs{0};
    std::vector<uint64_t> histogram;
    mutable std::mutex mu;
    std::unordered_map<std::string, uint64_t> counters;
    std::atomic<bool> running{false};
    std::thread reporter;
    void reporterLoop(int intervalSeconds);
    std::string histogramToString() const;
};
