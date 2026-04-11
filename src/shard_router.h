#pragma once
// ShardRouter: distributes documents across N local Indexer shards,
// fans queries in parallel across all shards, merges results.
// Each shard owns a disjoint subset of documents determined by
// consistent hashing on the file path.
// This is the scalability layer — for true multi-node distribution,
// replace LocalShard with a network stub (gRPC/HTTP).

#include <vector>
#include <memory>
#include <string>
#include <future>
#include <algorithm>
#include <numeric>
#include <functional>
#include <filesystem>

#include "indexer.h"
#include "tokenizer.h"
#include "wal.h"
#include "logger.h"

class ShardRouter {
public:
    explicit ShardRouter(int numShards = 4) {
        shards.resize(numShards);
        for (auto &s : shards) s = std::make_unique<Indexer>();
    }

    // Configure all shards identically
    void configure(Tokenizer *tok, bool stemming, bool bm25,
                   int threads, const std::string &extFilter = "") {
        for (auto &s : shards) {
            s->tokenizer   = tok;
            s->useStemming = stemming;
            s->useBM25     = bm25;
            s->setMaxThreads(std::max(1, threads / (int)shards.size()));
            if (!extFilter.empty()) s->setExtensionFilter(extFilter);
        }
    }

    // Build: partition files across shards by consistent hash
    void buildFromFolder(const std::string &folder, bool recursive = true) {
        namespace fs = std::filesystem;
        std::vector<std::string> files;
        auto collect = [&](auto &iter) {
            for (auto &p : iter) {
                if (!fs::is_regular_file(p.path())) continue;
                std::string ext = p.path().extension().string();
                if (ext==".txt"||ext==".md"||ext==".markdown"||ext==".pdf")
                    files.push_back(p.path().string());
            }
        };
        try {
            if (recursive) { auto it = fs::recursive_directory_iterator(folder); collect(it); }
            else           { auto it = fs::directory_iterator(folder);            collect(it); }
        } catch (...) { return; }

        // Partition files into per-shard lists
        int N = (int)shards.size();
        std::vector<std::vector<std::string>> partitions(N);
        for (auto &f : files) {
            size_t h = std::hash<std::string>{}(f) % (size_t)N;
            partitions[h].push_back(f);
        }

        // Build each shard in parallel
        std::vector<std::future<void>> futs;
        for (int i = 0; i < N; ++i) {
            futs.push_back(std::async(std::launch::async, [&, i]() {
                shards[i]->buildFromFileList(partitions[i]);
            }));
        }
        for (auto &f : futs) f.get();
        Logger::instance().log(LINFO, "ShardRouter: built " + std::to_string(N) +
            " shards from " + std::to_string(files.size()) + " files");
    }

    // Sync all shards
    void syncFolder(const std::string &folder, bool recursive = true) {
        int N = (int)shards.size();
        std::vector<std::future<void>> futs;
        for (int i = 0; i < N; ++i)
            futs.push_back(std::async(std::launch::async,
                [&, i]{ shards[i]->syncFolder(folder, recursive); }));
        for (auto &f : futs) f.get();
    }

    // Fan-out BM25 search across all shards, merge top-K
    std::vector<ScoredDoc> searchBM25(const std::vector<std::string> &qtokens, int topK) const {
        return fanSearch([&](Indexer &idx) {
            return idx.searchBM25Parallel(qtokens, topK);
        }, topK);
    }

    // Fan-out TF-IDF search
    std::vector<ScoredDoc> searchTFIDF(const std::vector<std::string> &qtokens, int topK) const {
        return fanSearch([&](Indexer &idx) {
            return idx.searchTFIDF(qtokens, topK);
        }, topK);
    }

    int numShards() const { return (int)shards.size(); }
    int totalDocs() const {
        int n = 0;
        for (auto &s : shards) n += s->numDocs();
        return n;
    }

    // Access shard by index (for direct operations)
    Indexer& shard(int i) { return *shards[i]; }
    const Indexer& shard(int i) const { return *shards[i]; }

    // Save/load all shards
    bool saveAll(const std::string &baseFile) const {
        for (int i = 0; i < (int)shards.size(); ++i)
            if (!shards[i]->saveIndex(baseFile + "." + std::to_string(i)))
                return false;
        return true;
    }

    bool loadAll(const std::string &baseFile) {
        for (int i = 0; i < (int)shards.size(); ++i)
            if (!shards[i]->loadIndex(baseFile + "." + std::to_string(i)))
                return false;
        return true;
    }

private:
    std::vector<std::unique_ptr<Indexer>> shards;

    // Fan query to all shards in parallel, merge and return top-K
    std::vector<ScoredDoc> fanSearch(
        std::function<std::vector<ScoredDoc>(Indexer&)> fn, int topK) const
    {
        int N = (int)shards.size();
        std::vector<std::future<std::vector<ScoredDoc>>> futs;
        futs.reserve(N);
        for (int i = 0; i < N; ++i)
            futs.push_back(std::async(std::launch::async,
                [&, i]{ return fn(*shards[i]); }));

        // Collect and merge
        std::vector<ScoredDoc> merged;
        for (auto &f : futs) {
            auto partial = f.get();
            merged.insert(merged.end(), partial.begin(), partial.end());
        }

        // Sort by score descending, keep top-K
        std::sort(merged.begin(), merged.end(),
            [](auto &a, auto &b){ return a.score > b.score; });
        if ((int)merged.size() > topK) merged.resize(topK);
        return merged;
    }
};
