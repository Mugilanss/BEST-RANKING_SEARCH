#pragma once
#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <thread>
#include <sstream>
#include <chrono>
#include <unordered_map>
#include <map>

class Metrics {
public:
    Metrics();

    // Record latency in ms (adds to histogram + total)
    void observeQueryLatencyMs(uint64_t ms);

    // Record query execution (cacheHit = true if LRUCache hit)
    void observeQuery(bool cacheHit);
    
    // Record indexing operations
    void recordIndexingOp(const std::string &docPath, uint64_t docsProcessed, uint64_t durationMs);
    
    // Record error
    void recordError(const std::string &errorType, const std::string &errorMsg);
    
    // Record index load operation
    void recordIndexLoad(uint64_t sizeBytes, uint64_t compressionRatio, uint64_t durationMs);

    // Increment an arbitrary counter (useful for indexing stats)
    void incrementCounter(const std::string &key, uint64_t delta = 1);
    
    // Gauge: set a metric value (memory usage, index size, etc)
    void setGauge(const std::string &key, double value);
    
    // Get metrics in Prometheus format
    std::string reportPrometheus() const;
    
    // Get metrics in plain text format
    std::string report() const;
    
    void startPeriodicReport(int intervalSeconds = 30);
    void stopPeriodicReport();
    ~Metrics();

    // Getters
    uint64_t getQueries()          const { return queries.load(); }
    uint64_t getCacheHits()        const { return cacheHits.load(); }
    uint64_t getTotalLatencyMs()   const { return totalLatencyMs.load(); }
    uint64_t getErrors()           const { return errorCount.load(); }
    uint64_t getDocsIndexed()      const { return docsIndexed.load(); }
    double   getAvgQueryLatencyMs() const {
        auto q = queries.load();
        return q == 0 ? 0.0 : (double)totalLatencyMs.load() / q;
    }
    double   getCacheHitRate() const {
        auto q = queries.load();
        return q == 0 ? 0.0 : (100.0 * cacheHits.load() / q);
    }

private:
    std::atomic<uint64_t> queries{0};
    std::atomic<uint64_t> cacheHits{0};
    std::atomic<uint64_t> totalLatencyMs{0};
    std::atomic<uint64_t> errorCount{0};
    std::atomic<uint64_t> docsIndexed{0};
    
    std::vector<uint64_t> histogram;  // 32 latency buckets
    std::vector<uint64_t> latencyExtremes;  // [min, max, p50, p95, p99]
    
    mutable std::mutex mu;
    std::unordered_map<std::string, uint64_t> counters;
    std::unordered_map<std::string, double> gauges;
    std::map<std::string, std::string> errors;  // errorType -> last error message
    
    std::atomic<bool> running{false};
    std::thread reporter;
    
    void reporterLoop(int intervalSeconds);
    std::string histogramToString() const;
    void updateLatencyExtremes(uint64_t latencyMs);
};

