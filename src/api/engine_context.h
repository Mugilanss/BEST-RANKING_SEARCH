#pragma once

#include <iostream>
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include <string>
#include <filesystem>

#include "indexer.h"
#include "tokenizer.h"
#include "bm25.h"
#include "query_parser.h"
#include "lru_cache.h"
#include "metrics.h"
#include "config.h"
#include "wal.h"
#include "logger.h"
#include "query_log.h"
#include "crawler.h"
#include "../db.h"

static inline std::string resolvePath(const std::string &base, const std::string &p) {
    std::filesystem::path fp(p);
    std::filesystem::path resolved = fp.is_absolute() ? fp : (std::filesystem::path(base) / fp);
    return std::filesystem::weakly_canonical(resolved).string();
}

struct RateLimiter {
    int maxRequests;
    int windowSeconds;

    struct Entry {
        int count = 0;
        std::chrono::steady_clock::time_point windowStart;
    };

    mutable std::mutex mu;
    std::unordered_map<std::string, Entry> table;

    RateLimiter(int max = 60, int windowSec = 60)
        : maxRequests(max), windowSeconds(windowSec) {}

    bool allow(const std::string &ip) {
        std::lock_guard<std::mutex> lk(mu);
        auto now = std::chrono::steady_clock::now();
        auto &e = table[ip];
        double elapsed = std::chrono::duration<double>(now - e.windowStart).count();
        if (elapsed >= windowSeconds) {
            e.count = 0;
            e.windowStart = now;
        }
        if (e.count >= maxRequests) return false;
        ++e.count;
        return true;
    }
};

struct EngineContext {
    Config          cfg;
    Tokenizer       tokenizer;
    Indexer         indexer;
    Database        db;
    WAL             wal;
    Metrics         metrics;
    RateLimiter     rateLimiter{60, 60};

    std::unique_ptr<BM25>        bm25;
    std::unique_ptr<QueryParser> qparser;
    std::unique_ptr<LRUCache<std::string, std::vector<ScoredDoc>>> cache;
    std::unique_ptr<QueryLog>    queryLog;

    std::string adminToken;
    mutable std::mutex engineMu;

    void init(const std::string &configPath) {
        cfg.loadFromFile(configPath);

        std::string baseDir = std::filesystem::path(configPath)
                                  .parent_path().string();

        cfg.docsFolder = resolvePath(baseDir, cfg.docsFolder);
        cfg.indexFile  = resolvePath(baseDir, cfg.indexFile);
        cfg.walFile    = resolvePath(baseDir, cfg.walFile);
        cfg.logFile    = resolvePath(baseDir, cfg.logFile);

        std::string stopwordsPath = resolvePath(baseDir, "../data/stopwords.txt");
        std::string queryLogPath  = resolvePath(baseDir, "../query.log");

        Logger::instance().init(cfg.logFile, LINFO, false);

        tokenizer.loadStopwords(stopwordsPath);
        tokenizer.useStopwords = cfg.useStopwords;
        tokenizer.useStemming  = cfg.useStemming;

        indexer.tokenizer   = &tokenizer;
        indexer.useStemming = cfg.useStemming;
        indexer.useBM25     = (cfg.scoring == "bm25");
        indexer.setMaxThreads(cfg.maxThreads);
        indexer.setShardCount(cfg.shardCount);
        if (!cfg.extFilter.empty())
            indexer.setExtensionFilter(cfg.extFilter);

        if (cfg.useWAL)
            wal.open(cfg.walFile);

        // Try DB first
        const char *dbUrl = std::getenv("DATABASE_URL");
        bool useDB = dbUrl && db.connect(dbUrl);

        if (useDB && db.documentCount() > 0) {
            std::cout << "DEBUG: loading from DB, docs=" << db.documentCount() << "\n";
            auto dbDocs = db.loadAllDocuments();
            indexer.buildFromDocuments(dbDocs);
        } else {
            //std::cout << "DEBUG: building from folder\n";
            indexer.buildFromFolderParallel(cfg.docsFolder, true);
            //std::cout << "DEBUG: after build, docs=" << indexer.numDocs() << "\n";
            if (useDB) {
                for (int i = 0; i < indexer.numDocs(); ++i) {
                    const Document &d = indexer.getDoc(i);
                    std::string content = indexer.loadContent(i);
                    db.saveDocument(d.path, content, d.size_bytes, d.mtime);
                }
                std::cout << "DEBUG: saved " << indexer.numDocs() << " docs to DB\n";
            }
            if (!cfg.indexFile.empty())
                indexer.saveIndex(cfg.indexFile);
        }

        indexer.preloadHotDocs(50);

        bm25     = std::make_unique<BM25>(&indexer);
        qparser  = std::make_unique<QueryParser>(&tokenizer);
        cache    = std::make_unique<LRUCache<std::string, std::vector<ScoredDoc>>>(1000);
        queryLog = std::make_unique<QueryLog>(queryLogPath);

        const char *envTok = std::getenv("SEARCH_ADMIN_TOKEN");
        adminToken = envTok ? std::string(envTok) : "admin";

        prewarmCache();
        metrics.startPeriodicReport(30);
    }

    void prewarmCache() {
        auto top = queryLog->topQueries(10);
        for (auto &[q, _] : top) {
            auto qtokens = tokenizer.tokenize(q);
            if (cfg.useStemming)
                for (auto &t : qtokens) t = tokenizer.stem(t);
            std::string key;
            for (size_t i = 0; i < qtokens.size(); ++i) {
                if (i) key += ' ';
                key += qtokens[i];
            }
            if (key.empty()) continue;
            std::vector<ScoredDoc> dummy;
            if (cache->get(key, dummy)) continue;
            auto results = (cfg.scoring == "bm25")
                ? indexer.searchBM25Parallel(qtokens, 100)
                : indexer.searchTFIDF(qtokens, 100);
            cache->put(key, results);
        }
        Logger::instance().log(LINFO, "Cache prewarmed with " +
            std::to_string(top.size()) + " historical queries");
    }
};