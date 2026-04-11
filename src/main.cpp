// src/main.cpp
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>
#include <thread>
#include <sstream>
#include <fstream>
#include <filesystem>

#include "config.h"
#include "logger.h"
#include "metrics.h"
#include "indexer.h"
#include "tokenizer.h"
#include "util.h"
#include "query_parser.h"
#include "bm25.h"
#include "lru_cache.h"
#include "bk_trie.h"
#include "wal.h"

using namespace std;

static string joinTokens(const vector<string> &tokens) {
    string s;
    s.reserve(tokens.size() * 8);
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i) s.push_back(' ');
        s += tokens[i];
    }
    return s;
}

// Sort modes
enum class SortMode { SCORE, DATE, SIZE };

static void printHelp() {
    cout <<
        "Commands:\n"
        "  :exit              quit\n"
        "  :reindex           rebuild index from docs folder\n"
        "  :save              save index to disk\n"
        "  :stats             show metrics + index dashboard\n"
        "  :sort score|date|size  set result sort order (default: score)\n"
        "  :page <N>          show page N of last results (10 per page)\n"
        "  :preload           preload hot docs into memory\n"
        "  :help              show this message\n"
        "  <query>            search (supports AND/OR/NOT, \"phrases\")\n";
}

int main(int /*argc*/, char ** /*argv*/) {

    //----------------------------------------------------------------------
    // Load configuration — find config.ini relative to the executable
    //----------------------------------------------------------------------
    std::string configPath = "src/config.ini";
    {
        for (auto &candidate : {"../src/config.ini", "../../src/config.ini", "src/config.ini"}) {
            std::ifstream t(candidate);
            if (t.good()) { configPath = candidate; break; }
        }
    }

    Config cfg;
    cfg.loadFromFile(configPath);

    // Resolve all relative paths against the config file's directory
    namespace fs = std::filesystem;
    std::string baseDir = fs::path(configPath).parent_path().string();
    auto resolve = [&](const std::string &p) -> std::string {
        fs::path fp(p);
        if (fp.is_absolute()) return p;
        return fs::weakly_canonical(fs::path(baseDir) / fp).string();
    };
    cfg.docsFolder = resolve(cfg.docsFolder);
    cfg.indexFile  = resolve(cfg.indexFile);
    cfg.walFile    = resolve(cfg.walFile);
    cfg.logFile    = resolve(cfg.logFile);
    std::string stopwordsPath = resolve("../data/stopwords.txt");

    Logger::instance().init(cfg.logFile, LINFO);
    Logger::instance().log(LINFO, "Search Engine Starting");

    Metrics metrics;
    metrics.startPeriodicReport(30);

    //----------------------------------------------------------------------
    // Build tokenizer
    //----------------------------------------------------------------------
    Tokenizer *tokenizer = new Tokenizer();
    tokenizer->loadStopwords(stopwordsPath);
    tokenizer->useStopwords = cfg.useStopwords;
    tokenizer->useStemming  = cfg.useStemming;

    //----------------------------------------------------------------------
    // Build indexer
    //----------------------------------------------------------------------
    Indexer indexer;
    indexer.tokenizer  = tokenizer;
    indexer.useStemming = cfg.useStemming;
    indexer.useBM25     = (cfg.scoring == "bm25");
    indexer.setMaxThreads(cfg.maxThreads);
    indexer.setShardCount(cfg.shardCount);

    if (!cfg.extFilter.empty())
        indexer.setExtensionFilter(cfg.extFilter);

    //----------------------------------------------------------------------
    // WAL
    //----------------------------------------------------------------------
    WAL wal;
    if (cfg.useWAL) {
        if (!wal.open(cfg.walFile))
            Logger::instance().log(LWARN, "WAL open failed; continuing without durability.");
    }

    //----------------------------------------------------------------------
    // Load or build index
    //----------------------------------------------------------------------
    if (!cfg.indexFile.empty()) {
        if (indexer.loadIndex(cfg.indexFile)) {
            Logger::instance().log(LINFO, "Loaded index: " + cfg.indexFile);
        } else {
            Logger::instance().log(LINFO, "Building fresh index...");
            indexer.buildFromFolderParallel(cfg.docsFolder, true);
            if (!indexer.saveIndex(cfg.indexFile))
                Logger::instance().log(LWARN, "Failed to save index to " + cfg.indexFile);
        }
    } else {
        indexer.buildFromFolderParallel(cfg.docsFolder, true);
    }

    //----------------------------------------------------------------------
    // WAL replay
    //----------------------------------------------------------------------
    if (cfg.useWAL) {
        wal.replay([&](WalOp op, const string &path) {
            if (op == WalOp::ADD) {
                indexer.incrementalUpdateWithWAL(path, wal, false);
            } else if (op == WalOp::REMOVE) {
                for (int i = 0; i < indexer.numDocs(); ++i) {
                    if (indexer.getDoc(i).path == path) {
                        indexer.removeDocument(i);
                        break;
                    }
                }
            } else if (op == WalOp::REINDEX) {
                indexer.buildFromFolderParallel(cfg.docsFolder, true);
            }
        });
    }

    //----------------------------------------------------------------------
    // Phase-2 components
    //----------------------------------------------------------------------
    QueryParser qparser(tokenizer);
    BM25 bm(&indexer);
    LRUCache<string, vector<ScoredDoc>> cache(1000);

    // Preload hot docs on startup
    indexer.preloadHotDocs(50);

    //----------------------------------------------------------------------
    // Session state
    //----------------------------------------------------------------------
    SortMode sortMode = SortMode::SCORE;
    vector<ScoredDoc> lastResults;   // full sorted result set for pagination
    vector<string>    lastQTokens;   // for snippet rendering across pages

    printHelp();

    string line;
    while (true) {
        cout << "\nquery> ";
        if (!getline(cin, line)) break;
        if (line.empty()) continue;

        //------------------------------------------------------------------
        // Built-in commands
        //------------------------------------------------------------------
        if (line == ":exit") break;

        if (line == ":help") { printHelp(); continue; }

        if (line == ":preload") {
            indexer.preloadHotDocs(50);
            cout << "Hot docs preloaded.\n";
            continue;
        }

        if (line == ":reindex") {
            Logger::instance().log(LINFO, "Manual sync triggered.");
            auto sr = indexer.syncFolder(cfg.docsFolder, true);
            cache.clear();
            lastResults.clear();
            cout << "Sync complete. Docs=" << indexer.numDocs()
                 << " added=" << sr.added
                 << " removed=" << sr.removed
                 << " modified=" << sr.modified << "\n";
            continue;
        }

        if (line == ":save") {
            if (!cfg.indexFile.empty() && indexer.saveIndex(cfg.indexFile))
                cout << "Index saved to " << cfg.indexFile << "\n";
            else
                cout << "Save failed.\n";
            continue;
        }

        // :sort score|date|size
        if (line.rfind(":sort", 0) == 0) {
            string mode = line.size() > 6 ? line.substr(6) : "";
            if (mode == "date")       { sortMode = SortMode::DATE;  cout << "Sort: date\n"; }
            else if (mode == "size")  { sortMode = SortMode::SIZE;  cout << "Sort: size\n"; }
            else                      { sortMode = SortMode::SCORE; cout << "Sort: score\n"; }
            // Re-sort last results if any
            if (!lastResults.empty()) {
                if (sortMode == SortMode::DATE)
                    sort(lastResults.begin(), lastResults.end(), [&](auto &a, auto &b){
                        return indexer.getDoc(a.id).mtime > indexer.getDoc(b.id).mtime; });
                else if (sortMode == SortMode::SIZE)
                    sort(lastResults.begin(), lastResults.end(), [&](auto &a, auto &b){
                        return indexer.getDoc(a.id).size_bytes > indexer.getDoc(b.id).size_bytes; });
                else
                    sort(lastResults.begin(), lastResults.end(), [](auto &a, auto &b){
                        return a.score > b.score; });
                cout << "Results re-sorted. Use :page 1 to view.\n";
            }
            continue;
        }

        // :page N
        if (line.rfind(":page", 0) == 0) {
            int pageNum = 1;
            if (line.size() > 6) {
                try { pageNum = stoi(line.substr(6)); } catch(...) { pageNum = 1; }
            }
            if (lastResults.empty()) { cout << "No results to page through.\n"; continue; }
            const int pageSize = 10;
            int total = (int)lastResults.size();
            int totalPages = (total + pageSize - 1) / pageSize;
            pageNum = max(1, min(pageNum, totalPages));
            int start = (pageNum - 1) * pageSize;
            int end   = min(start + pageSize, total);
            cout << "Page " << pageNum << "/" << totalPages
                 << " (" << total << " total results)\n";
            for (int i = start; i < end; ++i) {
                auto &r = lastResults[i];
                const Document &d = indexer.getDoc(r.id);
                cout << i+1 << ". [score=" << r.score
                     << " size=" << d.size_bytes << "B"
                     << " mtime=" << d.mtime << "] "
                     << d.path << "\n"
                     << "   " << indexer.makeSnippet(r.id, lastQTokens, 300) << "\n";
            }
            continue;
        }

        // :stats — full dashboard
        if (line == ":stats") {
            cout << "=== Metrics ===\n" << metrics.report() << "\n";
            cout << "=== Index ===\n";
            cout << "  Docs        : " << indexer.numDocs() << "\n";
            cout << "  Index size  : " << indexer.indexSizeBytes() / 1024 << " KB\n";
            cout << "  Avg doc len : " << indexer.avgDocLength() << " tokens\n";
            cout << "=== Top 20 Terms ===\n";
            for (auto &[term, freq] : indexer.getTopTerms(20))
                cout << "  " << term << " (" << freq << " docs)\n";
            continue;
        }

        //------------------------------------------------------------------
        // SEARCH PIPELINE
        //------------------------------------------------------------------
        vector<string> qtokens = tokenizer->tokenize(line);
        if (cfg.useStemming)
            for (auto &t : qtokens) t = tokenizer->stem(t);

        string cacheKey = joinTokens(qtokens);

        vector<ScoredDoc> results;
        bool cacheHit = false;

        if (!cacheKey.empty() && cache.get(cacheKey, results)) {
            metrics.observeQueryLatencyMs(0);
            metrics.observeQuery(true);
            cacheHit = true;
        } else {
            auto t0 = chrono::steady_clock::now();

            auto ast = qparser.parse(line);
            if (ast) {
                auto matchedDocs = qparser.evaluateParallel(ast, indexer, indexer.getMaxThreads());
                unordered_map<string,int> qcount;
                for (auto &t : qtokens) if (!t.empty()) qcount[t]++;
                results.reserve(matchedDocs.size());
                for (int d : matchedDocs)
                    results.push_back({d, bm.scoreDoc(d, qcount)});
            } else {
                results = (cfg.scoring == "bm25")
                    ? indexer.searchBM25Parallel(qtokens, 200)
                    : indexer.searchTFIDF(qtokens, 200);
            }

            auto t1 = chrono::steady_clock::now();
            uint64_t ms = chrono::duration_cast<chrono::milliseconds>(t1 - t0).count();
            metrics.observeQueryLatencyMs(ms);
            metrics.observeQuery(false);

            if (!cacheKey.empty()) cache.put(cacheKey, results);
        }

        //------------------------------------------------------------------
        // Apply sort
        //------------------------------------------------------------------
        if (sortMode == SortMode::DATE)
            sort(results.begin(), results.end(), [&](auto &a, auto &b){
                return indexer.getDoc(a.id).mtime > indexer.getDoc(b.id).mtime; });
        else if (sortMode == SortMode::SIZE)
            sort(results.begin(), results.end(), [&](auto &a, auto &b){
                return indexer.getDoc(a.id).size_bytes > indexer.getDoc(b.id).size_bytes; });
        else
            sort(results.begin(), results.end(), [](auto &a, auto &b){
                return a.score > b.score; });

        //------------------------------------------------------------------
        // Zero-results: spell correction + autocomplete
        //------------------------------------------------------------------
        if (results.empty()) {
            cout << "No results for \"" << line << "\".\n";

            // Spell correction per query token
            bool suggested = false;
            for (auto &tok : qtokens) {
                auto corrections = indexer.spellCorrect(tok, 2);
                if (!corrections.empty()) {
                    cout << "  Did you mean (spell): ";
                    for (size_t i = 0; i < min((size_t)3, corrections.size()); ++i)
                        cout << corrections[i] << (i+1 < min((size_t)3, corrections.size()) ? ", " : "");
                    cout << "\n";
                    suggested = true;
                }
                // Autocomplete suggestions
                auto completions = indexer.autocomplete(tok, 5);
                if (!completions.empty()) {
                    cout << "  Autocomplete for \"" << tok << "\": ";
                    for (size_t i = 0; i < completions.size(); ++i)
                        cout << completions[i].first << (i+1 < completions.size() ? ", " : "");
                    cout << "\n";
                    suggested = true;
                }
            }
            if (!suggested)
                cout << "  No suggestions found.\n";
            continue;
        }

        //------------------------------------------------------------------
        // Store for pagination and display page 1
        //------------------------------------------------------------------
        lastResults  = results;
        lastQTokens  = qtokens;

        const int pageSize = 10;
        int total      = (int)results.size();
        int totalPages = (total + pageSize - 1) / pageSize;

        cout << "\nFound " << total << " results"
             << " (cache=" << (cacheHit ? "hit" : "miss") << ")"
             << " | sort=" << (sortMode == SortMode::DATE ? "date" : sortMode == SortMode::SIZE ? "size" : "score")
             << " | page 1/" << totalPages << " (use :page N for more)\n";

        int show = min(pageSize, total);
        for (int i = 0; i < show; ++i) {
            auto &r = results[i];
            const Document &d = indexer.getDoc(r.id);
            cout << i+1 << ". [score=" << r.score
                 << " size=" << d.size_bytes << "B"
                 << " mtime=" << d.mtime << "] "
                 << d.path << "\n"
                 << "   " << indexer.makeSnippet(r.id, qtokens, 300) << "\n";
        }
    }

    metrics.stopPeriodicReport();
    Logger::instance().log(LINFO, "Shutdown complete.");
    delete tokenizer;
    return 0;
}
