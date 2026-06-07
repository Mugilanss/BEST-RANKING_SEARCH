#include "routes.h"

#include <sstream>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <thread>

#include "../httplib.h"
#include "../crawler.h"

using namespace std;

// -----------------------------------------------------------------------
// JSON helpers
// -----------------------------------------------------------------------
static string jsonStr(const string &s)
{
    string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char c : s)
    {
        if (c == '"')
            out += "\\\"";
        else if (c == '\\')
            out += "\\\\";
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else if (c == '\t')
            out += "\\t";
        else
            out += c;
    }
    out += '"';
    return out;
}

static string jsonKV(const string &k, const string &v, bool isStr = true)
{
    return jsonStr(k) + ":" + (isStr ? jsonStr(v) : v);
}

// -----------------------------------------------------------------------
// CORS
// -----------------------------------------------------------------------
static void addCORS(httplib::Response &res)
{
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
}

// -----------------------------------------------------------------------
// Rate limit guard
// -----------------------------------------------------------------------
static bool checkRate(const httplib::Request &req,
                      httplib::Response &res,
                      EngineContext &ctx)
{
    // Create key combining IP + endpoint path for granular rate limiting
    string key = req.remote_addr + ":" + req.path;
    
    if (!ctx.rateLimiter.allow(key))
    {
        addCORS(res);
        res.status = 429;
        res.set_content("{\"error\":\"rate limit exceeded\",\"retry_after\":60}", "application/json");
        res.set_header("Retry-After", "60");
        ctx.metrics.recordError("rate_limit", "Request rate limit exceeded for: " + key);
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------
// 6.5 Token auth guard — checks Authorization: Bearer <token>
// Returns false and writes 401 if token missing or wrong.
// -----------------------------------------------------------------------
static bool checkAuth(const httplib::Request &req,
                      httplib::Response &res,
                      EngineContext &ctx)
{
    auto it = req.headers.find("Authorization");
    if (it == req.headers.end())
    {
        addCORS(res);
        res.status = 401;
        res.set_content("{\"error\":\"missing Authorization header\"}", "application/json");
        ctx.metrics.recordError("auth_missing", "No Authorization header");
        return false;
    }
    const string &hdr = it->second;
    string token;
    if (hdr.size() > 7 && hdr.substr(0, 7) == "Bearer ")
        token = hdr.substr(7);
    
    if (token.empty() || token != ctx.adminToken)
    {
        addCORS(res);
        res.status = 403;
        res.set_content("{\"error\":\"invalid or missing token\"}", "application/json");
        ctx.metrics.recordError("auth_invalid", "Invalid/missing token from: " + req.remote_addr);
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------
// 6.5 Input sanitization — strip control chars, limit length
// -----------------------------------------------------------------------
static string sanitizeInput(const string &s, size_t maxLen = 512)
{
    string out;
    out.reserve(min(s.size(), maxLen));
    for (char c : s)
    {
        if (out.size() >= maxLen)
            break;
        // Keep printable characters and common punctuation, drop control chars
        if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7F)
            out += c;
        else if (c == '\t' || c == '\n' || c == '\r')
            out += ' ';  // replace whitespace
    }
    return out;
}

// -----------------------------------------------------------------------
// Input validation helper — check for SQL injection-like patterns
// -----------------------------------------------------------------------
static bool isValidInput(const string &s, size_t maxLen = 512)
{
    return s.size() <= maxLen;
}

// -----------------------------------------------------------------------
// Search helper
// -----------------------------------------------------------------------
static string runSearch(const string &rawQuery, int topK,
                        const string &sortBy,
                        EngineContext &ctx,
                        uint64_t &latencyMs)
{
    string query = sanitizeInput(rawQuery);

    vector<string> qtokens = ctx.tokenizer.tokenize(query);
    if (ctx.cfg.useStemming)
        for (auto &t : qtokens)
            t = ctx.tokenizer.stem(t);

    string cacheKey;
    for (size_t i = 0; i < qtokens.size(); ++i)
    {
        if (i)
            cacheKey += ' ';
        cacheKey += qtokens[i];
    }

    vector<ScoredDoc> results;
    bool hit = false;

    if (!cacheKey.empty() && ctx.cache->get(cacheKey, results))
    {
        hit = true;
        latencyMs = 0;
        ctx.metrics.observeQuery(true);
        ctx.metrics.observeQueryLatencyMs(0);
    }
    else
    {
        auto t0 = chrono::steady_clock::now();
        auto ast = ctx.qparser->parse(query);
        if (ast) {
            auto matched = ctx.qparser->evaluateParallel(
                ast, ctx.indexer, ctx.indexer.getMaxThreads());
            unordered_map<string,int> qcount;
            for (auto &t : qtokens) if (!t.empty()) qcount[t]++;
            results.reserve(matched.size());
            for (int d : matched)
                results.push_back({d, ctx.bm25->scoreDoc(d, qcount)});
        } else {
            results = (ctx.cfg.scoring == "bm25")
                          ? ctx.indexer.searchBM25Parallel(qtokens, topK * 10)
                          : ctx.indexer.searchTFIDF(qtokens, topK * 10);
        }
        auto t1 = chrono::steady_clock::now();
        latencyMs = (uint64_t)chrono::duration_cast<chrono::milliseconds>(t1 - t0).count();
        ctx.metrics.observeQueryLatencyMs(latencyMs);
        ctx.metrics.observeQuery(false);
        if (!cacheKey.empty())
            ctx.cache->put(cacheKey, results);
    }

    // 6.3 Log every query
    if (!query.empty())
        ctx.queryLog->record(query);

    if (sortBy == "date")
        sort(results.begin(), results.end(), [&](auto &a, auto &b)
             { return ctx.indexer.getDoc(a.id).mtime > ctx.indexer.getDoc(b.id).mtime; });
    else if (sortBy == "size")
        sort(results.begin(), results.end(), [&](auto &a, auto &b)
             { return ctx.indexer.getDoc(a.id).size_bytes > ctx.indexer.getDoc(b.id).size_bytes; });
    else
        sort(results.begin(), results.end(), [](auto &a, auto &b)
             { return a.score > b.score; });

    if ((int)results.size() > topK)
        results.resize(topK);

    ostringstream js;
    js << "{"
       << jsonKV("query", query) << ","
       << jsonKV("cache_hit", hit ? "true" : "false", false) << ","
       << jsonKV("latency_ms", to_string(latencyMs), false) << ","
       << "\"results\":[";

    for (size_t i = 0; i < results.size(); ++i)
    {
        auto &r = results[i];
        const Document &d = ctx.indexer.getDoc(r.id);
        string snippet = ctx.indexer.makeSnippet(r.id, qtokens, 300);
        double score = isfinite(r.score) ? r.score : 0.0;
        if (i)
            js << ",";
        js << "{"
           << jsonKV("docID", to_string(r.id), false) << ","
           << jsonKV("path", d.path) << ","
           << jsonKV("score", to_string(score), false) << ","
           << jsonKV("size", to_string(d.size_bytes), false) << ","
           << jsonKV("mtime", to_string(d.mtime), false) << ","
           << jsonKV("snippet", snippet)
           << "}";
    }
    js << "]}";
    return js.str();
}

// -----------------------------------------------------------------------
// register_routes
// -----------------------------------------------------------------------
void register_routes(httplib::Server &srv, EngineContext &ctx)
{

    // OPTIONS pre-flight
    srv.Options(".*", [](const httplib::Request &, httplib::Response &res)
                {
        addCORS(res); res.status = 204; });

    // ------------------------------------------------------------------
    // GET /search
    // ------------------------------------------------------------------
    srv.Get("/search", [&ctx](const httplib::Request &req, httplib::Response &res)
            {
        addCORS(res);
        if (!checkRate(req, res, ctx)) return;

        string query  = req.has_param("q")    ? req.get_param_value("q")              : "";
        int    topK   = req.has_param("k")    ? stoi(req.get_param_value("k"))        : 10;
        string sortBy = req.has_param("sort") ? req.get_param_value("sort")           : "score";
        topK = max(1, min(topK, 100));

        if (query.empty()) {
            res.status = 400;
            res.set_content("{\"error\":\"missing parameter q\"}", "application/json");
            return;
        }
        
        // Validate input (check for injection patterns)
        if (!isValidInput(query, 1000)) {
            res.status = 400;
            res.set_content("{\"error\":\"invalid query - suspicious characters detected\"}", "application/json");
            ctx.metrics.recordError("input_validation", "Invalid query detected");
            return;
        }
        
        // Validate sortBy parameter
        if (sortBy != "score" && sortBy != "date" && sortBy != "size") {
            sortBy = "score";
        }
        
        uint64_t latencyMs = 0;
        res.set_content(runSearch(query, topK, sortBy, ctx, latencyMs), "application/json"); });


    // ------------------------------------------------------------------
    // GET /document/<id>
    // ------------------------------------------------------------------
    srv.Get(R"(/document/(\d+))", [&ctx](const httplib::Request &req, httplib::Response &res)
            {
        addCORS(res);
        if (!checkRate(req, res, ctx)) return;

        int id = stoi(req.matches[1]);
        if (id < 0 || id >= ctx.indexer.numDocs()) {
            res.status = 404;
            res.set_content("{\"error\":\"document not found\"}", "application/json");
            return;
        }
        const Document &d = ctx.indexer.getDoc(id);
        string content = ctx.indexer.loadContent(id);
        ostringstream js;
        js << "{"
           << jsonKV("docID",   to_string(id),          false) << ","
           << jsonKV("path",    d.path)                        << ","
           << jsonKV("size",    to_string(d.size_bytes), false) << ","
           << jsonKV("mtime",   to_string(d.mtime),     false) << ","
           << jsonKV("content", content)
           << "}";
        res.set_content(js.str(), "application/json"); });

    // ------------------------------------------------------------------
    // GET /stats
    // ------------------------------------------------------------------
    srv.Get("/stats", [&ctx](const httplib::Request &req, httplib::Response &res)
            {
        addCORS(res);
        if (!checkRate(req, res, ctx)) return;

        auto topTerms = ctx.indexer.getTopTerms(20);
        ostringstream termsArr;
        termsArr << "[";
        for (size_t i = 0; i < topTerms.size(); ++i) {
            if (i) termsArr << ",";
            termsArr << "{" << jsonKV("term", topTerms[i].first) << ","
                     << jsonKV("df", to_string(topTerms[i].second), false) << "}";
        }
        termsArr << "]";

        ostringstream js;
        js << "{"
           << jsonKV("docs",           to_string(ctx.indexer.numDocs()),             false) << ","
           << jsonKV("index_kb",       to_string(ctx.indexer.indexSizeBytes()/1024), false) << ","
           << jsonKV("avg_doc_len",    to_string(ctx.indexer.avgDocLength()),        false) << ","
           << jsonKV("scoring",        ctx.cfg.scoring)                                     << ","
           << jsonKV("queries",        to_string(ctx.metrics.getQueries()),          false) << ","
           << jsonKV("cache_hits",     to_string(ctx.metrics.getCacheHits()),        false) << ","
           << jsonKV("avg_latency_ms", to_string(ctx.metrics.getQueries() > 0
                ? (double)ctx.metrics.getTotalLatencyMs()/ctx.metrics.getQueries() : 0.0), false) << ","
           << jsonKV("cache_hit_rate", to_string(ctx.metrics.getQueries() > 0
                ? 100.0*ctx.metrics.getCacheHits()/ctx.metrics.getQueries() : 0.0), false) << ","
           << jsonKV("query_log_size", to_string(ctx.queryLog->size()),              false) << ","
           << "\"top_terms\":" << termsArr.str()
           << "}";
        res.set_content(js.str(), "application/json"); });

    // ------------------------------------------------------------------
    // GET /autocomplete — trie prefix + query-log suggestions (6.3)
    // ------------------------------------------------------------------
    srv.Get("/autocomplete", [&ctx](const httplib::Request &req, httplib::Response &res)
            {
        addCORS(res);
        if (!checkRate(req, res, ctx)) return;

        string prefix = req.has_param("q") ? sanitizeInput(req.get_param_value("q")) : "";
        int k = req.has_param("k") ? stoi(req.get_param_value("k")) : 5;
        k = max(1, min(k, 20));

        auto termSuggestions  = ctx.indexer.autocomplete(prefix, k);
        auto querySuggestions = ctx.queryLog->suggest(prefix, k);  // 6.3

        ostringstream js;
        js << "{" << jsonKV("prefix", prefix) << ",\"suggestions\":[";
        for (size_t i = 0; i < termSuggestions.size(); ++i) {
            if (i) js << ",";
            js << "{" << jsonKV("term", termSuggestions[i].first) << ","
               << jsonKV("freq", to_string(termSuggestions[i].second), false) << ","
               << jsonKV("source", "index") << "}";
        }
        js << "],\"query_suggestions\":[";
        for (size_t i = 0; i < querySuggestions.size(); ++i) {
            if (i) js << ",";
            js << jsonStr(querySuggestions[i]);
        }
        js << "]}";
        res.set_content(js.str(), "application/json"); });

    // ------------------------------------------------------------------
    // GET /popular — top queries from query log (6.3)
    // ------------------------------------------------------------------
    srv.Get("/popular", [&ctx](const httplib::Request &req, httplib::Response &res)
            {
        addCORS(res);
        if (!checkRate(req, res, ctx)) return;

        int k = req.has_param("k") ? stoi(req.get_param_value("k")) : 10;
        k = max(1, min(k, 50));
        auto top = ctx.queryLog->topQueries(k);

        ostringstream js;
        js << "{\"popular\":[";
        for (size_t i = 0; i < top.size(); ++i) {
            if (i) js << ",";
            js << "{" << jsonKV("query", top[i].first) << ","
               << jsonKV("count", to_string(top[i].second), false) << "}";
        }
        js << "]}";
        res.set_content(js.str(), "application/json"); });

    // ------------------------------------------------------------------
    // POST /index/rebuild  — ADMIN (6.5 token auth)
    // ------------------------------------------------------------------
    srv.Post("/index/rebuild", [&ctx](const httplib::Request &req, httplib::Response &res)
             {
        addCORS(res);
        if (!checkRate(req, res, ctx)) return;
        if (!checkAuth(req, res, ctx)) return;

        lock_guard<mutex> lk(ctx.engineMu);
        auto t0 = chrono::steady_clock::now();
        ctx.indexer.buildFromFolderParallel(ctx.cfg.docsFolder, true);
        if (!ctx.cfg.indexFile.empty()) ctx.indexer.saveIndex(ctx.cfg.indexFile);
        ctx.cache->clear();
        ctx.prewarmCache();
        auto ms = chrono::duration_cast<chrono::milliseconds>(
            chrono::steady_clock::now() - t0).count();

        ostringstream js;
        js << "{" << jsonKV("status","ok") << ","
           << jsonKV("docs", to_string(ctx.indexer.numDocs()), false) << ","
           << jsonKV("elapsed_ms", to_string(ms), false) << "}";
        res.set_content(js.str(), "application/json"); });

    // ------------------------------------------------------------------
    // POST /index/update -- ADMIN: full sync (add + delete + modify)
    srv.Post("/index/update", [&ctx](const httplib::Request &req, httplib::Response &res) {
        addCORS(res);
        if (!checkRate(req, res, ctx)) return;
        if (!checkAuth(req, res, ctx)) return;

        lock_guard<mutex> lk(ctx.engineMu);
        auto t0 = chrono::steady_clock::now();
        auto sr = ctx.indexer.syncFolder(ctx.cfg.docsFolder, true);
        if (!ctx.cfg.indexFile.empty()) ctx.indexer.saveIndex(ctx.cfg.indexFile);
        ctx.cache->clear();
        auto ms = chrono::duration_cast<chrono::milliseconds>(
            chrono::steady_clock::now() - t0).count();

        ostringstream js;
        js << "{" << jsonKV("status","ok") << ","
           << jsonKV("docs",       to_string(ctx.indexer.numDocs()), false) << ","
           << jsonKV("added",      to_string(sr.added),              false) << ","
           << jsonKV("removed",    to_string(sr.removed),            false) << ","
           << jsonKV("modified",   to_string(sr.modified),           false) << ","
           << jsonKV("elapsed_ms", to_string(ms),                    false) << "}";
        res.set_content(js.str(), "application/json");
    });

    // POST /crawl -- ADMIN (6.4 + 6.5)
    srv.Post("/crawl", [&ctx](const httplib::Request &req, httplib::Response &res) {
        addCORS(res);
        if (!checkRate(req, res, ctx)) return;
        if (!checkAuth(req, res, ctx)) return;

        string seedUrl = req.has_param("url")   ? req.get_param_value("url")         : "";
        int depth      = req.has_param("depth") ? stoi(req.get_param_value("depth")) : 2;
        int pages      = req.has_param("pages") ? stoi(req.get_param_value("pages")) : 20;

        seedUrl = sanitizeInput(seedUrl, 1024);
        depth = max(1, min(depth, 5));
        pages = max(1, min(pages, 100));

        if (seedUrl.empty()) {
            res.status = 400;
            res.set_content("{\"error\":\"missing parameter url\"}", "application/json");
            return;
        }

        string outputDir = ctx.cfg.docsFolder + "/crawled";
        thread([&ctx, seedUrl, depth, pages, outputDir]() {
            CrawlerConfig ccfg;
            ccfg.maxPages  = pages;
            ccfg.maxDepth  = depth;
            ccfg.delayMs   = 500;
            ccfg.outputDir = outputDir;
            Crawler crawler(ccfg);

            int crawled = crawler.crawl(seedUrl, [&ctx](const string &filepath) {
                lock_guard<mutex> lk(ctx.engineMu);
                ctx.indexer.incrementalUpdateWithWAL(filepath, ctx.wal, false);
            });

            if (!ctx.cfg.indexFile.empty())
                ctx.indexer.saveIndex(ctx.cfg.indexFile);

            Logger::instance().log(LINFO,
                "Crawl complete: " + to_string(crawled) + " pages from " + seedUrl);
        }).detach();

        ostringstream js;
        js << "{" << jsonKV("status","crawl started") << ","
           << jsonKV("seed",      seedUrl)                 << ","
           << jsonKV("max_pages", to_string(pages), false) << ","
           << jsonKV("max_depth", to_string(depth), false) << "}";
        res.set_content(js.str(), "application/json");
    });

    // ------------------------------------------------------------------
    // GET /metrics — Prometheus-format metrics (ADMIN)
    // ------------------------------------------------------------------
    srv.Get("/metrics", [&ctx](const httplib::Request &req, httplib::Response &res) {
        addCORS(res);
        if (!checkRate(req, res, ctx)) return;
        if (!checkAuth(req, res, ctx)) return;
        
        res.set_header("Content-Type", "text/plain; charset=utf-8");
        res.set_content(ctx.metrics.reportPrometheus(), "text/plain; charset=utf-8");
    });

    // ------------------------------------------------------------------
    // GET /health — Health check endpoint (public, no auth required)
    // ------------------------------------------------------------------
    srv.Get("/health", [&ctx](const httplib::Request &req, httplib::Response &res) {
        addCORS(res);
        
        ostringstream js;
        js << "{"
           << jsonKV("status", "ok") << ","
           << jsonKV("docs", to_string(ctx.indexer.numDocs()), false) << ","
           << jsonKV("queries", to_string(ctx.metrics.getQueries()), false) << ","
           << jsonKV("errors", to_string(ctx.metrics.getErrors()), false)
           << "}";
        res.set_content(js.str(), "application/json");
    });
}
