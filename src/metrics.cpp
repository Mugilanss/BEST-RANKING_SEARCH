#include "metrics.h"
#include "logger.h"
#include <sstream>
#include <thread>
#include <chrono>
using namespace std;

// ----------------------------
// Constructor / Destructor
// ----------------------------
Metrics::Metrics() {
    histogram.resize(32, 0);  // 32 latency buckets (1ms .. 2^31 ms)
}

Metrics::~Metrics() {
    stopPeriodicReport();
}

// ----------------------------
// Record latency
// ----------------------------
void Metrics::observeQueryLatencyMs(uint64_t ms) {
    totalLatencyMs += ms;

    // bucket: first power-of-two >= ms
    uint32_t bucket = 0;
    while (bucket < histogram.size() && (1ULL << bucket) < ms)
        bucket++;

    if (bucket >= histogram.size())
        bucket = histogram.size() - 1;

    lock_guard<mutex> lk(mu);
    histogram[bucket]++;
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
// Arbitrary counters
// ----------------------------
void Metrics::incrementCounter(const string &key, uint64_t delta) {
    lock_guard<mutex> lk(mu);
    counters[key] += delta;
}

// ----------------------------
// Render histogram
// ----------------------------
string Metrics::histogramToString() const {
    ostringstream ss;
    lock_guard<mutex> lk(mu);

    ss << "[latency-buckets: ";
    for (size_t i = 0; i < histogram.size(); ++i) {
        ss << (1ULL << i) << "ms=" << histogram[i];
        if (i + 1 < histogram.size())
            ss << ", ";
    }
    ss << "]";

    return ss.str();
}

// ----------------------------
// Full metrics report
// ----------------------------
string Metrics::report() const {
    uint64_t q = queries.load();
    double avg = (q == 0 ? 0.0 : (double)totalLatencyMs.load() / q);

    ostringstream ss;
    ss << "queries=" << q
       << ", cacheHits=" << cacheHits.load()
       << ", avgLatencyMs=" << avg
       << ", cacheHitRate=" << (q ? (100.0 * cacheHits.load() / q) : 0.0)
       << "\n"
       << histogramToString()
       << "\n";

    lock_guard<mutex> lk(mu);
    ss << "counters: { ";
    for (auto &kv : counters)
        ss << kv.first << "=" << kv.second << " ";
    ss << "}";

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
        this_thread::sleep_for(std::chrono::seconds(intervalSeconds));
        Logger::instance().log(LINFO, "METRICS: " + report());
    }
}
