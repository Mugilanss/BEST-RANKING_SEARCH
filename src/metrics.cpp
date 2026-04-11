#include "metrics.h"
#include "logger.h"
#include <sstream>
#include <thread>
#include <chrono>
#include <iomanip>
#include <algorithm>
using namespace std;

// ----------------------------
// Constructor / Destructor
// ----------------------------
Metrics::Metrics() {
    histogram.resize(32, 0);  // 32 latency buckets (1ms .. 2^31 ms)
    latencyExtremes.resize(5, 0);  // [min, max, p50, p95, p99]
}

Metrics::~Metrics() {
    stopPeriodicReport();
}

// ----------------------------
// Record latency with extremes tracking
// ----------------------------
void Metrics::observeQueryLatencyMs(uint64_t ms) {
    totalLatencyMs += ms;
    updateLatencyExtremes(ms);

    // bucket: first power-of-two >= ms
    uint32_t bucket = 0;
    while (bucket < histogram.size() && (1ULL << bucket) < ms)
        bucket++;

    if (bucket >= histogram.size())
        bucket = histogram.size() - 1;

    lock_guard<mutex> lk(mu);
    histogram[bucket]++;
}

void Metrics::updateLatencyExtremes(uint64_t latencyMs) {
    lock_guard<mutex> lk(mu);
    if (latencyExtremes[0] == 0 || latencyMs < latencyExtremes[0]) {
        latencyExtremes[0] = latencyMs;  // min
    }
    if (latencyMs > latencyExtremes[1]) {
        latencyExtremes[1] = latencyMs;  // max
    }
}

// ----------------------------
// Query count & cache hits
// ----------------------------
void Metrics::observeQuery(bool cacheHit) {
    queries++;
    if (cacheHit)
        cacheHits++;
}

// ----------------------------
// Indexing operations
// ----------------------------
void Metrics::recordIndexingOp(const string &docPath, uint64_t docsProcessed, uint64_t durationMs) {
    docsIndexed += docsProcessed;
    lock_guard<mutex> lk(mu);
    counters["indexing_ops"]++;
    counters["total_docs_indexed"] += docsProcessed;
    Logger::instance().log(LINFO, 
        "Indexed " + to_string(docsProcessed) + " docs in " + to_string(durationMs) + "ms");
}

// ----------------------------
// Error tracking
// ----------------------------
void Metrics::recordError(const string &errorType, const string &errorMsg) {
    errorCount++;
    lock_guard<mutex> lk(mu);
    errors[errorType] = errorMsg;
    Logger::instance().log(LERROR, "Error [" + errorType + "]: " + errorMsg);
}

// ----------------------------
// Index load operation
// ----------------------------
void Metrics::recordIndexLoad(uint64_t sizeBytes, uint64_t compressionRatio, uint64_t durationMs) {
    lock_guard<mutex> lk(mu);
    counters["index_loads"]++;
    gauges["last_index_size_bytes"] = (double)sizeBytes;
    gauges["last_compression_ratio"] = (double)compressionRatio;
    counters["last_load_time_ms"] = durationMs;
}

// ----------------------------
// Arbitrary counters
// ----------------------------
void Metrics::incrementCounter(const string &key, uint64_t delta) {
    lock_guard<mutex> lk(mu);
    counters[key] += delta;
}

// ----------------------------
// Gauges - set metric values
// ----------------------------
void Metrics::setGauge(const string &key, double value) {
    lock_guard<mutex> lk(mu);
    gauges[key] = value;
}

// ----------------------------
// Render histogram
// ----------------------------
string Metrics::histogramToString() const {
    ostringstream ss;
    lock_guard<mutex> lk(mu);

    ss << "[latency-buckets (ms): ";
    for (size_t i = 0; i < histogram.size(); ++i) {
        ss << (1ULL << i) << "=" << histogram[i];
        if (i + 1 < histogram.size())
            ss << ", ";
    }
    ss << "]";

    return ss.str();
}

// ----------------------------
// Prometheus-format output
// ----------------------------
string Metrics::reportPrometheus() const {
    uint64_t q = queries.load();
    double avgLatency = (q == 0 ? 0.0 : (double)totalLatencyMs.load() / q);

    ostringstream ss;
    ss << "# TYPE query_total counter\n";
    ss << "query_total " << q << "\n";
    ss << "# TYPE cache_hits_total counter\n";
    ss << "cache_hits_total " << cacheHits.load() << "\n";
    ss << "# TYPE cache_hit_rate gauge\n";
    ss << fixed << setprecision(2);
    ss << "cache_hit_rate " << getCacheHitRate() << "\n";
    ss << "# TYPE query_latency_ms histogram\n";
    ss << "query_latency_avg_ms " << avgLatency << "\n";
    
    {
        lock_guard<mutex> lk(mu);
        if (latencyExtremes[0] > 0) {
            ss << "query_latency_min_ms " << latencyExtremes[0] << "\n";
            ss << "query_latency_max_ms " << latencyExtremes[1] << "\n";
        }
    }

    ss << "# TYPE errors_total counter\n";
    ss << "errors_total " << errorCount.load() << "\n";
    ss << "# TYPE docs_indexed_total counter\n";
    ss << "docs_indexed_total " << docsIndexed.load() << "\n";

    lock_guard<mutex> lk(mu);
    for (auto &kv : counters) {
        ss << "counter_" << kv.first << " " << kv.second << "\n";
    }
    for (auto &kv : gauges) {
        ss << "gauge_" << kv.first << " " << kv.second << "\n";
    }

    return ss.str();
}

// ----------------------------
// Full metrics report
// ----------------------------
string Metrics::report() const {
    uint64_t q = queries.load();
    double avg = (q == 0 ? 0.0 : (double)totalLatencyMs.load() / q);

    ostringstream ss;
    ss << "QUERY METRICS:\n";
    ss << "  queries=" << q
       << ", cacheHits=" << cacheHits.load()
       << ", avgLatencyMs=" << fixed << setprecision(2) << avg
       << ", cacheHitRate=" << getCacheHitRate() << "%\n";

    ss << "ERROR METRICS:\n";
    ss << "  errorCount=" << errorCount.load() << "\n";

    ss << "INDEXING METRICS:\n";
    ss << "  docsIndexed=" << docsIndexed.load() << "\n";

    ss << histogramToString() << "\n";

    lock_guard<mutex> lk(mu);
    
    if (!errors.empty()) {
        ss << "RECENT ERRORS:\n";
        for (auto &kv : errors) {
            ss << "  [" << kv.first << "] " << kv.second << "\n";
        }
    }

    if (!counters.empty()) {
        ss << "COUNTERS: { ";
        for (auto &kv : counters)
            ss << kv.first << "=" << kv.second << " ";
        ss << "}\n";
    }

    if (!gauges.empty()) {
        ss << "GAUGES: { ";
        for (auto &kv : gauges)
            ss << kv.first << "=" << fixed << setprecision(2) << kv.second << " ";
        ss << "}\n";
    }

    return ss.str();
}

// ----------------------------
// Periodic reporting
// ----------------------------
void Metrics::startPeriodicReport(int intervalSeconds) {
    if (running.exchange(true))
        return; // already running

    reporter = thread([this, intervalSeconds]() {
        reporterLoop(intervalSeconds);
    });
}

void Metrics::stopPeriodicReport() {
    if (!running.exchange(false))
        return; // already stopped

    if (reporter.joinable())
        reporter.join();
}

// ----------------------------
// Reporter loop (background)
// ----------------------------
void Metrics::reporterLoop(int intervalSeconds) {
    while (running.load()) {
        this_thread::sleep_for(chrono::seconds(intervalSeconds));
        Logger::instance().log(LINFO, "METRICS:\n" + report());
    }
}
