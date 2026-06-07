#pragma once

#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>
#include <ctime>

#include "tokenizer.h"
#include "wal.h"

class Trie;
class BKTree;

struct Posting {
    int docID;
    std::vector<int> positions;
};

struct Document {
    std::string path;
    uint64_t size_bytes = 0;
    time_t mtime = 0;
    double norm = 0.0;
    // content is NOT stored in memory — loaded on demand via loadContent()
};

struct ScoredDoc {
    int id;
    double score;
};

class StringInterner {
public:
    const std::string* intern(const std::string &s);
private:
    std::set<std::string> table;
};

class Indexer {
public:
    Indexer();
    ~Indexer();

    // Build
    void buildFromFolder(const std::string &folder, bool recursive = true);
    void buildFromFolderParallel(const std::string &folder, bool recursive = true);
    void buildFromFileList(const std::vector<std::string> &files);  // for ShardRouter
    void incrementalUpdateWithWAL(const std::string &folder, WAL &wal, bool recursive = true);

    // Sync: detects added, deleted, and modified files — no WAL needed
    struct SyncResult { int added = 0; int removed = 0; int modified = 0; };
    SyncResult syncFolder(const std::string &folder, bool recursive = true);

    // Persistence
    bool saveIndex(const std::string &filename) const;
    bool loadIndex(const std::string &filename);

    // Search
    std::vector<ScoredDoc> searchBM25Parallel(const std::vector<std::string> &qterms, int topK) const;
    std::vector<ScoredDoc> searchTFIDF(const std::vector<std::string> &qterms, int topK) const;

    // Postings access
    std::unordered_set<int> getDocIDsForTerm(const std::string &term) const;
    std::vector<int>        getDocIDsSorted(const std::string &term) const;  // sorted, for merge ops
    const std::vector<Posting>* getPostings(const std::string &term) const;
    bool docHasPhrase(int docID, const std::vector<std::string> &terms) const;

    // Document access
    int numDocs() const;
    const Document& getDoc(int id) const;
    void removeDocument(int docID);
    std::string makeSnippet(int docID, const std::vector<std::string> &qterms, size_t window) const;
    // Lazy content load — reads from disk, caches in contentCache
    std::string loadContent(int docID) const;

    // BM25 helpers
    int dfCount(const std::string &term) const;
    int termFreqInDoc(const std::string &term, int docID) const;
    double docLength(int docID) const;
    double avgDocLength() const;

    // Metrics / dashboard helpers
    std::vector<std::pair<std::string,int>> getTopTerms(int k = 20) const;
    uint64_t indexSizeBytes() const;

    // Hot-doc preload: loads content of top-k most-referenced docs into memory
    void preloadHotDocs(int k = 50);

    // Autocomplete / spell correction (expose trie/bk)
    std::vector<std::pair<std::string,int>> autocomplete(const std::string &prefix, int k = 5) const;
    std::vector<std::string> spellCorrect(const std::string &term, int maxDist = 2) const;

    // Config
    void setExtensionFilter(const std::string &ext);
    void setMaxThreads(int n);
    int  getMaxThreads() const;
    void setShardCount(int n);

    // Public fields set by main
    Tokenizer* tokenizer = nullptr;
    bool useStemming = false;
    bool useBM25 = true;

private:
    void indexFileWorker(const std::string &path, int docID,
        std::unordered_map<const std::string*, std::vector<int>> &localTfPositions);
    void buildVocabStructures();
    std::string loadFileContent(const std::string &path) const;
    uint64_t fileHash(const std::string &path) const;
    const std::string* lookupTermPtr(const std::string &term) const;
    
    // Persistence helpers for versioning and compression
    bool loadIndexFromStream(std::istream &stream);
    bool finalizeLegacyLoad();

    std::vector<Document> docs;
    std::vector<std::unordered_map<const std::string*, double>> docTf;
    std::vector<int> docTokenCount;

    std::unordered_map<std::string, int> df;
    std::unordered_map<std::string, const std::string*> vocabMap;
    std::unordered_map<std::string, double> idf;

    int shardCount = 8;
    std::vector<std::unique_ptr<std::mutex>> shardMutexes;
    mutable std::mutex metaMu;

    std::vector<std::unordered_map<const std::string*, std::vector<Posting>>> postingsSharded;

    StringInterner interner;

    int maxThreads = 4;
    double avgDocLenCache = 0.0;
    std::string extFilter;

    // LRU content cache: stores recently accessed doc content (max 200 docs)
    mutable std::unordered_map<int, std::string> contentCache;
    mutable std::mutex contentCacheMu;
    static constexpr int kContentCacheMax = 200;

    Trie*   trie = nullptr;
    BKTree* bk   = nullptr;
};
