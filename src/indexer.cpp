#include "indexer.h"

#include <filesystem>
#include <sstream>
#include <cmath>
#include <fstream>
#include <algorithm>
#include <numeric>
#include <future>
#include <thread>

#include "util.h"
#include "varint.h"
#include "threadpool.h"
#include "logger.h"
#include "bm25.h"
#include "bk_trie.h"
#include "wal.h"
#include "compress.h"

using namespace std;
namespace fs = std::filesystem;

// ---------------------- StringInterner ----------------------
const std::string *StringInterner::intern(const std::string &s)
{
    auto it = table.find(s);
    if (it != table.end())
        return &*it;
    auto p = table.insert(s);
    return &*p.first;
}

// ---------------------- Helpers ----------------------
static time_t ft_to_time_t(fs::file_time_type ft)
{
    using namespace std::chrono;
    auto sctp = time_point_cast<system_clock::duration>(
        ft - fs::file_time_type::clock::now() + system_clock::now());
    return system_clock::to_time_t(sctp);
}

static uint64_t simple_hash64(const string &s)
{
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s)
    {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

// shard selection by pointer
static inline size_t shardFor(const string *s, int shardCount)
{
    auto h = reinterpret_cast<uintptr_t>(s);
    return (size_t)(h % (uintptr_t)max(1, shardCount));
}

// ---------------------- Indexer ----------------------
Indexer::Indexer()
{
    maxThreads = (int)thread::hardware_concurrency();
    if (maxThreads <= 0)
        maxThreads = 4;
    postingsSharded.resize(shardCount);
    shardMutexes.clear();
    for (int i = 0; i < shardCount; ++i)
        shardMutexes.emplace_back(make_unique<mutex>());
    trie = nullptr;
    bk = nullptr;
}

Indexer::~Indexer()
{
    // ensure complete types are available since we include bk_trie.h above
    if (trie)
    {
        delete trie;
        trie = nullptr;
    }
    if (bk)
    {
        delete bk;
        bk = nullptr;
    }
}

// ---------------------- Config setters/getters ----------------------
void Indexer::setExtensionFilter(const std::string &ext) { extFilter = ext; }
void Indexer::setMaxThreads(int n)
{
    if (n > 0)
        maxThreads = n;
}
int Indexer::getMaxThreads() const { return maxThreads; }
void Indexer::setShardCount(int n)
{
    if (n <= 0)
        return;
    shardCount = n;
    postingsSharded.clear();
    postingsSharded.resize(shardCount);
    shardMutexes.clear();
    for (int i = 0; i < shardCount; ++i)
        shardMutexes.emplace_back(make_unique<mutex>());
}

int Indexer::numDocs() const { return (int)docs.size(); }
const Document &Indexer::getDoc(int id) const { return docs.at(id); }

// ---------------------- File IO helpers ----------------------
string Indexer::loadFileContent(const string &path) const
{
    string lower = path;
    for (auto &c : lower)
        c = (char)tolower((unsigned char)c);
    if (endsWith(lower, ".pdf"))
    {
        string cmd = "pdftotext \"" + path + "\" -";
        return execCommandCapture(cmd);
    }
    if (endsWith(lower, ".doc"))
    {
        string cmd = "antiword \"" + path + "\"";
        return execCommandCapture(cmd);
    }
    if (endsWith(lower, ".docx"))
    {
        string cmd = "docx2txt \"" + path + "\" -";
        return execCommandCapture(cmd);
    }
    return readTextFile(path);
}

uint64_t Indexer::fileHash(const string &path) const
{
    string c = loadFileContent(path);
    return simple_hash64(c);
}

// ---------------------- Worker ----------------------
void Indexer::indexFileWorker(const string &path, int docID,
                              unordered_map<const string *, vector<int>> &localTfPositions)
{
    string content;
    if (tokenizer)
        content = loadFileContent(path);
    else
        content = loadFileContent(path); // still load

    vector<string> tokens;
    if (tokenizer)
        tokens = tokenizer->tokenize(content);

    if (tokenizer && tokenizer->useStemming && useStemming)
    {
        for (auto &t : tokens)
            t = tokenizer->stem(t);
    }

    // update doc metadata — content NOT stored, loaded lazily
    docs[docID].path = path;
    docs[docID].size_bytes = content.size();
    try
    {
        docs[docID].mtime = ft_to_time_t(fs::last_write_time(path));
    }
    catch (...)
    {
        docs[docID].mtime = 0;
    }

    for (int i = 0; i < (int)tokens.size(); ++i)
    {
        const string *pin = interner.intern(tokens[i]);
        localTfPositions[pin].push_back(i);
    }
    docTokenCount[docID] = (int)tokens.size();
}

// ---------------------- Build (parallel) ----------------------
void Indexer::buildFromFolder(const string &folder, bool recursive)
{
    buildFromFolderParallel(folder, recursive);
}

// Build from an explicit file list (used by ShardRouter)
void Indexer::buildFromFileList(const vector<string> &files)
{
    size_t Nfiles = files.size();
    docs.clear();
    docs.resize(Nfiles);
    docTf.clear();
    docTf.resize(Nfiles);
    docTokenCount.assign(Nfiles, 0);
    df.clear();
    vocabMap.clear();
    idf.clear();
    for (int sh = 0; sh < shardCount; ++sh)
        postingsSharded[sh].clear();

    ThreadPool pool((size_t)maxThreads);
    vector<future<unordered_map<const string *, vector<int>>>> futures;
    futures.reserve(Nfiles);
    for (size_t i = 0; i < Nfiles; ++i)
    {
        string path = files[i];
        futures.push_back(pool.enqueue([this, path, i]() -> unordered_map<const string *, vector<int>>
                                       {
            unordered_map<const string*, vector<int>> local;
            indexFileWorker(path, (int)i, local);
            return local; }));
    }
    for (size_t i = 0; i < Nfiles; ++i)
    {
        auto local = futures[i].get();
        int docID = (int)i;
        unordered_set<const string *> seen;
        for (auto &entry : local)
        {
            const string *tp = entry.first;
            double tf = 1.0 + log((double)entry.second.size());
            docTf[docID][tp] = tf;
            vocabMap[*tp] = tp;
            size_t sh = shardFor(tp, shardCount);
            {
                lock_guard<mutex> lk(*shardMutexes[sh]);
                postingsSharded[sh][tp].push_back({docID, entry.second});
            }
            seen.insert(tp);
        }
        {
            lock_guard<mutex> lk(metaMu);
            for (auto tp : seen)
                df[*tp] += 1;
        }
    }
    int N = (int)docs.size();
    {
        lock_guard<mutex> lk(metaMu);
        idf.clear();
        for (auto &p : df)
            idf[p.first] = log((double)(N + 1) / (p.second + 1)) + 1.0;
    }
    for (int i = 0; i < N; ++i)
    {
        double sum = 0.0;
        for (auto &pr : docTf[i])
        {
            auto it = idf.find(*pr.first);
            double iv = (it == idf.end()) ? 0.0 : it->second;
            sum += pr.second * iv * pr.second * iv;
        }
        docs[i].norm = sqrt(sum);
    }
    double tot = 0;
    for (int c : docTokenCount)
        tot += c;
    avgDocLenCache = docTokenCount.empty() ? 0.0 : tot / docTokenCount.size();
    buildVocabStructures();
    pool.shutdown();
    Logger::instance().log(LINFO, "buildFromFileList: indexed " + to_string(N) + " docs");
}

void Indexer::buildFromDocuments(const vector<Document> &dbDocs)
{
    size_t Nfiles = dbDocs.size();
    docs.clear();
    docs.resize(Nfiles);
    docTf.clear();
    docTf.resize(Nfiles);
    docTokenCount.assign(Nfiles, 0);
    df.clear();
    vocabMap.clear();
    idf.clear();
    for (int sh = 0; sh < shardCount; ++sh)
        postingsSharded[sh].clear();

    for (size_t i = 0; i < Nfiles; ++i)
    {
        docs[i] = dbDocs[i];
        if (!dbDocs[i].content.empty())
        {
            lock_guard<mutex> lk(contentCacheMu);
            contentCache[(int)i] = dbDocs[i].content;
        }

        unordered_map<const string *, vector<int>> local;
        vector<string> tokens;
        if (tokenizer)
            tokens = tokenizer->tokenize(dbDocs[i].content);
        if (tokenizer && useStemming)
            for (auto &t : tokens)
                t = tokenizer->stem(t);

        docTokenCount[i] = (int)tokens.size();
        for (int j = 0; j < (int)tokens.size(); ++j)
        {
            const string *pin = interner.intern(tokens[j]);
            local[pin].push_back(j);
        }

        unordered_set<const string *> seen;
        for (auto &entry : local)
        {
            const string *tp = entry.first;
            double tf = 1.0 + log((double)entry.second.size());
            docTf[i][tp] = tf;
            vocabMap[*tp] = tp;
            size_t sh = shardFor(tp, shardCount);
            postingsSharded[sh][tp].push_back({(int)i, entry.second});
            seen.insert(tp);
        }
        for (auto tp : seen)
            df[*tp] += 1;
    }

    int N = (int)docs.size();
    idf.clear();
    for (auto &p : df)
        idf[p.first] = log((double)(N + 1) / (p.second + 1)) + 1.0;

    for (int i = 0; i < N; ++i)
    {
        double sum = 0.0;
        for (auto &pr : docTf[i])
        {
            auto it = idf.find(*pr.first);
            double iv = (it == idf.end()) ? 0.0 : it->second;
            sum += pr.second * iv * pr.second * iv;
        }
        docs[i].norm = sqrt(sum);
    }

    double tot = 0;
    for (int c : docTokenCount)
        tot += c;
    avgDocLenCache = docTokenCount.empty() ? 0.0 : tot / docTokenCount.size();
    buildVocabStructures();
    Logger::instance().log(LINFO, "buildFromDocuments: indexed " + to_string(N) + " docs from DB");
}
void Indexer::buildFromFolderParallel(const string &folder, bool recursive)
{
    Logger::instance().log(LINFO, "Starting parallel indexing on: " + folder);

    vector<string> files;
    if (recursive)
    {
        for (auto &p : fs::recursive_directory_iterator(folder))
        {
            if (!fs::is_regular_file(p.path()))
                continue;
            string ext = p.path().extension().string();
            if (!extFilter.empty() && ext != extFilter)
                continue;
            if (ext == ".txt" || ext == ".md" || ext == ".markdown" || ext == ".pdf" || ext == ".doc" || ext == ".docx")
                files.push_back(p.path().string());
        }
    }
    else
    {
        for (auto &p : fs::directory_iterator(folder))
        {
            if (!fs::is_regular_file(p.path()))
                continue;
            string ext = p.path().extension().string();
            if (!extFilter.empty() && ext != extFilter)
                continue;
            if (ext == ".txt" || ext == ".md" || ext == ".markdown" || ext == ".pdf")
                files.push_back(p.path().string());
        }
    }

    size_t Nfiles = files.size();
    docs.clear();
    docs.resize(Nfiles);
    docTf.clear();
    docTf.resize(Nfiles);
    docTokenCount.assign(Nfiles, 0);

    ThreadPool pool((size_t)maxThreads);
    vector<future<unordered_map<const string *, vector<int>>>> futures;
    futures.reserve(Nfiles);

    for (size_t i = 0; i < Nfiles; ++i)
    {
        string path = files[i];
        futures.push_back(pool.enqueue([this, path, i]() -> unordered_map<const string *, vector<int>>
                                       {
            unordered_map<const string*, vector<int>> local;
            indexFileWorker(path, (int)i, local);
            return local; }));
    }

    for (size_t i = 0; i < Nfiles; ++i)
    {
        auto local = futures[i].get();
        int docID = (int)i;
        unordered_set<const string *> seen;
        for (auto &entry : local)
        {
            const string *termPtr = entry.first;
            vector<int> positions = entry.second;
            double tf = 1.0 + log((double)positions.size());
            docTf[docID][termPtr] = tf;
            vocabMap[*termPtr] = termPtr;
            size_t sh = shardFor(termPtr, shardCount);
            {
                lock_guard<mutex> lk(*shardMutexes[sh]);
                postingsSharded[sh][termPtr].push_back({docID, positions});
            }
            seen.insert(termPtr);
        }
        {
            lock_guard<mutex> lk(metaMu);
            for (const string *tp : seen)
                df[*tp] += 1;
        }
    }

    // compute idf
    int N = (int)docs.size();
    {
        lock_guard<mutex> lk(metaMu);
        idf.clear();
        for (auto &p : df)
            idf[p.first] = log((double)(N + 1) / (p.second + 1)) + 1.0;
    }

    // compute doc norms
    for (int i = 0; i < N; ++i)
    {
        double sum = 0.0;
        for (auto &pr : docTf[i])
        {
            const string *termPtr = pr.first;
            double tfw = pr.second;
            auto it = idf.find(*termPtr);
            double idfval = (it == idf.end()) ? 0.0 : it->second;
            double w = tfw * idfval;
            sum += w * w;
        }
        docs[i].norm = sqrt(sum);
    }

    double tot = 0;
    for (int c : docTokenCount)
        tot += c;
    avgDocLenCache = docTokenCount.empty() ? 0.0 : tot / docTokenCount.size();

    buildVocabStructures();
    pool.shutdown();

    Logger::instance().log(LINFO, "Parallel indexing finished. Docs=" + to_string(N));
}

// ---------------------- Persistence ----------------------
// Helper: serialize index data to memory buffer

bool Indexer::saveIndex(const string &filename) const
{
    try
    {
        // Serialize to memory first
        stringstream ss(ios::binary);

        // Serialize to memory buffer
        stringstream memBuf(ios::binary);
        int shards = shardCount;
        memBuf.write((char *)&shards, sizeof(shards));
        int N = (int)docs.size();
        memBuf.write((char *)&N, sizeof(N));

        for (const auto &d : docs)
        {
            uint64_t plen = (uint64_t)d.path.size();
            memBuf.write((char *)&plen, sizeof(plen));
            memBuf.write(d.path.data(), plen);
            memBuf.write((char *)&d.size_bytes, sizeof(d.size_bytes));
            memBuf.write((char *)&d.mtime, sizeof(d.mtime));
        }

        // persist token counts
        for (int i = 0; i < N; ++i)
        {
            int tc = docTokenCount[i];
            memBuf.write((char *)&tc, sizeof(tc));
        }

        uint64_t vocabCount = df.size();
        memBuf.write((char *)&vocabCount, sizeof(vocabCount));
        for (auto &p : df)
        {
            const string &term = p.first;
            uint64_t tlen = (uint64_t)term.size();
            memBuf.write((char *)&tlen, sizeof(tlen));
            memBuf.write(term.data(), tlen);
            memBuf.write((char *)&p.second, sizeof(p.second));
        }

        for (int sh = 0; sh < shardCount; ++sh)
        {
            lock_guard<mutex> lk(*shardMutexes[sh]);
            uint64_t termsCount = postingsSharded[sh].size();
            memBuf.write((char *)&termsCount, sizeof(termsCount));
            for (auto &entry : postingsSharded[sh])
            {
                const string &term = *entry.first;
                uint64_t tlen = (uint64_t)term.size();
                memBuf.write((char *)&tlen, sizeof(tlen));
                memBuf.write(term.data(), tlen);
                uint64_t plistSize = (uint64_t)entry.second.size();
                memBuf.write((char *)&plistSize, sizeof(plistSize));
                int prevDoc = 0;
                for (auto &post : entry.second)
                {
                    uint64_t gap = (uint64_t)(post.docID - prevDoc);
                    varint::write_u64(memBuf, gap);
                    prevDoc = post.docID;
                    uint64_t posCount = (uint64_t)post.positions.size();
                    varint::write_u64(memBuf, posCount);
                    int prevPos = 0;
                    for (int pos : post.positions)
                    {
                        uint64_t posGap = (uint64_t)(pos - prevPos);
                        varint::write_u64(memBuf, posGap);
                        prevPos = pos;
                    }
                }
            }
        }

        string uncompressed = memBuf.str();
        vector<uint8_t> compressedData = compress::encode(
            (const uint8_t *)uncompressed.data(),
            uncompressed.size());

        // Write to file with versioning header
        ofstream ofs(filename, ios::binary);
        if (!ofs)
            return false;

        // Version 2 header: IDX2 + version number + compression flag
        ofs.write("IDX2", 4);
        uint32_t version = 2;
        ofs.write((char *)&version, sizeof(version));

        // Compression flag (1 = compressed, 0 = uncompressed)
        uint8_t compressed = 1;
        ofs.write((char *)&compressed, sizeof(compressed));

        // Store original size for decompression
        uint64_t originalSize = (uint64_t)uncompressed.size();
        ofs.write((char *)&originalSize, sizeof(originalSize));

        // Store compressed size
        uint64_t compressedSize = (uint64_t)compressedData.size();
        ofs.write((char *)&compressedSize, sizeof(compressedSize));

        // Write compressed data
        ofs.write((const char *)compressedData.data(), compressedSize);

        Logger::instance().log(LINFO,
                               "saveIndex: orig=" + to_string(originalSize) +
                                   "B, compressed=" + to_string(compressedSize) +
                                   "B (ratio=" + to_string(100.0 * compressedSize / originalSize) + "%)");

        return true;
    }
    catch (const exception &e)
    {
        Logger::instance().log(LERROR, "saveIndex exception: " + string(e.what()));
        return false;
    }
}

bool Indexer::loadIndex(const string &filename)
{
    ifstream ifs(filename, ios::binary);
    if (!ifs)
        return false;

    char magic[5];
    magic[4] = 0;
    ifs.read(magic, 4);
    string magicStr(magic);

    // Handle new format (IDX2 with compression)
    if (magicStr == "IDX2")
    {
        uint32_t version = 0;
        ifs.read((char *)&version, sizeof(version));

        if (version != 2)
        {
            Logger::instance().log(LERROR, "Unsupported index version: " + to_string(version));
            return false;
        }

        uint8_t compressed = 0;
        ifs.read((char *)&compressed, sizeof(compressed));

        uint64_t originalSize = 0;
        ifs.read((char *)&originalSize, sizeof(originalSize));

        uint64_t compressedSize = 0;
        ifs.read((char *)&compressedSize, sizeof(compressedSize));

        // Read compressed data
        vector<uint8_t> compressedData(compressedSize);
        ifs.read((char *)compressedData.data(), compressedSize);

        if (!ifs.good())
        {
            Logger::instance().log(LERROR, "Failed to read compressed index data");
            return false;
        }

        // Decompress
        vector<uint8_t> uncompressedData(originalSize);
        if (!compress::decode(compressedData.data(), compressedSize,
                              uncompressedData.data(), originalSize))
        {
            Logger::instance().log(LERROR, "Decompression failed");
            return false;
        }

        Logger::instance().log(LINFO,
                               "loadIndex: decompressed " + to_string(compressedSize) +
                                   "B -> " + to_string(originalSize) +
                                   "B (ratio=" + to_string(100.0 * compressedSize / originalSize) + "%)");

        // Parse uncompressed data from memory
        stringstream ss(ios::binary);
        ss.write((const char *)uncompressedData.data(), originalSize);
        ss.seekg(0);

        return loadIndexFromStream(ss);
    }
    // Handle old format (IDX1 without compression) for backward compatibility
    else if (magicStr == "IDX1")
    {
        Logger::instance().log(LINFO, "Loading legacy index format (IDX1, uncompressed)");

        int shards = 0;
        ifs.read((char *)&shards, sizeof(shards));
        shardCount = shards;

        postingsSharded.clear();
        postingsSharded.resize(shardCount);

        shardMutexes.clear();
        for (int i = 0; i < shardCount; ++i)
            shardMutexes.emplace_back(make_unique<mutex>());

        int N = 0;
        ifs.read((char *)&N, sizeof(N));
        docs.clear();
        docs.resize(N);
        docTf.clear();
        docTf.resize(N);
        docTokenCount.assign(N, 0);

        for (int i = 0; i < N; ++i)
        {
            uint64_t plen;
            ifs.read((char *)&plen, sizeof(plen));
            string path;
            path.resize(plen);
            ifs.read(&path[0], plen);
            docs[i].path = path;
            ifs.read((char *)&docs[i].size_bytes, sizeof(docs[i].size_bytes));
            ifs.read((char *)&docs[i].mtime, sizeof(docs[i].mtime));
        }

        // load token counts
        for (int i = 0; i < N; ++i)
        {
            int tc = 0;
            ifs.read((char *)&tc, sizeof(tc));
            docTokenCount[i] = tc;
        }

        uint64_t vocabCount = 0;
        ifs.read((char *)&vocabCount, sizeof(vocabCount));
        df.clear();
        vocabMap.clear();
        for (uint64_t i = 0; i < vocabCount; ++i)
        {
            uint64_t tlen;
            ifs.read((char *)&tlen, sizeof(tlen));
            string term;
            term.resize(tlen);
            ifs.read(&term[0], tlen);
            int dfv;
            ifs.read((char *)&dfv, sizeof(dfv));
            df[term] = dfv;
            const string *tp = interner.intern(term);
            vocabMap[term] = tp;
        }

        for (int sh = 0; sh < shardCount; ++sh)
        {
            uint64_t termsCount = 0;
            ifs.read((char *)&termsCount, sizeof(termsCount));
            for (uint64_t ti = 0; ti < termsCount; ++ti)
            {
                uint64_t tlen;
                ifs.read((char *)&tlen, sizeof(tlen));
                string term;
                term.resize(tlen);
                ifs.read(&term[0], tlen);
                const string *termPtr = interner.intern(term);
                vocabMap[term] = termPtr;
                uint64_t plistSize = 0;
                ifs.read((char *)&plistSize, sizeof(plistSize));
                vector<Posting> plist;
                plist.reserve((size_t)plistSize);
                int prevDoc = 0;
                for (uint64_t pi = 0; pi < plistSize; ++pi)
                {
                    uint64_t gap = varint::read_u64(ifs);
                    int docID = prevDoc + (int)gap;
                    prevDoc = docID;
                    uint64_t posCount = varint::read_u64(ifs);
                    vector<int> positions;
                    positions.reserve((size_t)posCount);
                    int prevPos = 0;
                    for (uint64_t k = 0; k < posCount; ++k)
                    {
                        uint64_t posGap = varint::read_u64(ifs);
                        int pos = prevPos + (int)posGap;
                        positions.push_back(pos);
                        prevPos = pos;
                    }
                    plist.push_back({docID, positions});
                }
                postingsSharded[sh][termPtr] = move(plist);
            }
        }

        return finalizeLegacyLoad();
    }
    else
    {
        Logger::instance().log(LERROR, "Invalid index magic bytes: " + magicStr);
        return false;
    }
}

// Helper: load index data from a memory stream
bool Indexer::loadIndexFromStream(istream &stream)
{
    int shards = 0;
    stream.read((char *)&shards, sizeof(shards));
    shardCount = shards;

    postingsSharded.clear();
    postingsSharded.resize(shardCount);

    shardMutexes.clear();
    for (int i = 0; i < shardCount; ++i)
        shardMutexes.emplace_back(make_unique<mutex>());

    int N = 0;
    stream.read((char *)&N, sizeof(N));
    docs.clear();
    docs.resize(N);
    docTf.clear();
    docTf.resize(N);
    docTokenCount.assign(N, 0);

    for (int i = 0; i < N; ++i)
    {
        uint64_t plen;
        stream.read((char *)&plen, sizeof(plen));
        string path;
        path.resize(plen);
        stream.read(&path[0], plen);
        docs[i].path = path;
        stream.read((char *)&docs[i].size_bytes, sizeof(docs[i].size_bytes));
        stream.read((char *)&docs[i].mtime, sizeof(docs[i].mtime));
    }

    // load token counts
    for (int i = 0; i < N; ++i)
    {
        int tc = 0;
        stream.read((char *)&tc, sizeof(tc));
        docTokenCount[i] = tc;
    }

    uint64_t vocabCount = 0;
    stream.read((char *)&vocabCount, sizeof(vocabCount));
    df.clear();
    vocabMap.clear();
    for (uint64_t i = 0; i < vocabCount; ++i)
    {
        uint64_t tlen;
        stream.read((char *)&tlen, sizeof(tlen));
        string term;
        term.resize(tlen);
        stream.read(&term[0], tlen);
        int dfv;
        stream.read((char *)&dfv, sizeof(dfv));
        df[term] = dfv;
        const string *tp = interner.intern(term);
        vocabMap[term] = tp;
    }

    for (int sh = 0; sh < shardCount; ++sh)
    {
        uint64_t termsCount = 0;
        stream.read((char *)&termsCount, sizeof(termsCount));
        for (uint64_t ti = 0; ti < termsCount; ++ti)
        {
            uint64_t tlen;
            stream.read((char *)&tlen, sizeof(tlen));
            string term;
            term.resize(tlen);
            stream.read(&term[0], tlen);
            const string *termPtr = interner.intern(term);
            vocabMap[term] = termPtr;
            uint64_t plistSize = 0;
            stream.read((char *)&plistSize, sizeof(plistSize));
            vector<Posting> plist;
            plist.reserve((size_t)plistSize);
            int prevDoc = 0;
            for (uint64_t pi = 0; pi < plistSize; ++pi)
            {
                uint64_t gap = varint::read_u64(stream);
                int docID = prevDoc + (int)gap;
                prevDoc = docID;
                uint64_t posCount = varint::read_u64(stream);
                vector<int> positions;
                positions.reserve((size_t)posCount);
                int prevPos = 0;
                for (uint64_t k = 0; k < posCount; ++k)
                {
                    uint64_t posGap = varint::read_u64(stream);
                    int pos = prevPos + (int)posGap;
                    positions.push_back(pos);
                    prevPos = pos;
                }
                plist.push_back({docID, positions});
            }
            postingsSharded[sh][termPtr] = move(plist);
        }
    }

    return finalizeLegacyLoad();
}

// Helper: finalize loading after data is read
bool Indexer::finalizeLegacyLoad()
{
    // recompute idf and doc norms (docTf may be empty)
    int Nc = (int)docs.size();
    idf.clear();
    for (auto &p : df)
        idf[p.first] = log((double)(Nc + 1) / (p.second + 1)) + 1.0;

    for (int i = 0; i < Nc; ++i)
    {
        double sum = 0.0;
        for (auto &kv : docTf[i])
        {
            const string *tp = kv.first;
            double tfw = kv.second;
            auto it = idf.find(*tp);
            double idfval = (it == idf.end()) ? 0.0 : it->second;
            double w = tfw * idfval;
            sum += w * w;
        }
        docs[i].norm = sqrt(sum);
    }

    double tot = 0.0;
    for (int c : docTokenCount)
        tot += c;
    avgDocLenCache = docTokenCount.empty() ? 0.0 : tot / docTokenCount.size();

    buildVocabStructures();

    for (int i = 0; i < (int)docs.size(); ++i)
    {
        if (!docs[i].path.empty() && !fs::exists(docs[i].path))
        {
            // Only remove if not a crawled doc — crawled docs live in DB
            if (docs[i].path.find("/crawled/") == std::string::npos)
                removeDocument(i);
        }
    }

    return true;
}

// ---------------------- Incremental with WAL ----------------------
void Indexer::incrementalUpdateWithWAL(const string &folder, WAL &wal, bool recursive)
{
    unordered_set<string> known;
    for (auto &d : docs)
        known.insert(d.path);
    vector<string> added;
    if (recursive)
    {
        for (auto &p : fs::recursive_directory_iterator(folder))
        {
            if (!fs::is_regular_file(p.path()))
                continue;
            string path = p.path().string();
            if (!known.count(path))
                added.push_back(path);
        }
    }
    else
    {
        for (auto &p : fs::directory_iterator(folder))
        {
            if (!fs::is_regular_file(p.path()))
                continue;
            string path = p.path().string();
            if (!known.count(path))
                added.push_back(path);
        }
    }
    if (added.empty())
    {
        Logger::instance().log(LINFO, "No new files to add in incremental update");
        return;
    }
    for (auto &p : added)
        wal.appendAdd(p);

    int base = (int)docs.size();
    int newCount = (int)added.size();
    docs.resize(base + newCount);
    docTf.resize(base + newCount);
    docTokenCount.resize(base + newCount);

    ThreadPool pool((size_t)maxThreads);
    vector<future<unordered_map<const string *, vector<int>>>> futures;
    futures.reserve(newCount);

    for (int i = 0; i < newCount; ++i)
    {
        string path = added[i];
        int docID = base + i;
        futures.push_back(pool.enqueue([this, path, docID]()
                                       {
            unordered_map<const string*, vector<int>> local;
            indexFileWorker(path, docID, local);
            return local; }));
    }

    for (int i = 0; i < newCount; ++i)
    {
        auto local = futures[i].get();
        int docID = base + i;
        unordered_set<const string *> seen;
        for (auto &kv : local)
        {
            const string *tp = kv.first;
            vector<int> positions = kv.second;
            double tf = 1.0 + log((double)positions.size());
            docTf[docID][tp] = tf;
            vocabMap[*tp] = tp;
            size_t sh = shardFor(tp, shardCount);
            {
                lock_guard<mutex> lk(*shardMutexes[sh]);
                postingsSharded[sh][tp].push_back({docID, positions});
            }
            seen.insert(tp);
        }
        {
            lock_guard<mutex> lk(metaMu);
            for (auto tp : seen)
                df[*tp] += 1;
        }
    }

    pool.shutdown();

    int N = (int)docs.size();
    {
        lock_guard<mutex> lk(metaMu);
        idf.clear();
        for (auto &e : df)
            idf[e.first] = log((double)(N + 1) / (e.second + 1)) + 1.0;
    }

    for (int i = 0; i < N; ++i)
    {
        double sum = 0.0;
        for (auto &pr : docTf[i])
        {
            const string *tp = pr.first;
            double tfw = pr.second;
            auto it = idf.find(*tp);
            double idfval = (it == idf.end()) ? 0.0 : it->second;
            double w = tfw * idfval;
            sum += w * w;
        }
        docs[i].norm = sqrt(sum);
    }

    double tot2 = 0.0;
    for (int c : docTokenCount)
        tot2 += c;
    avgDocLenCache = docTokenCount.empty() ? 0.0 : tot2 / docTokenCount.size();

    buildVocabStructures();
    Logger::instance().log(LINFO, "Incremental update applied: added " + to_string(newCount) + " documents");
}

// ---------------------- Sync (add + delete + modify) ----------------------
Indexer::SyncResult Indexer::syncFolder(const string &folder, bool recursive)
{
    SyncResult result;

    // 1. Collect all files currently on disk
    unordered_map<string, time_t> onDisk; // path -> mtime
    auto scanDir = [&](auto &iter)
    {
        for (auto &p : iter)
        {
            if (!fs::is_regular_file(p.path()))
                continue;
            string ext = p.path().extension().string();
            if (!extFilter.empty() && ext != extFilter)
                continue;
            if (ext == ".txt" || ext == ".md" || ext == ".markdown" || ext == ".pdf")
            {
                time_t mt = 0;
                try
                {
                    mt = ft_to_time_t(fs::last_write_time(p.path()));
                }
                catch (...)
                {
                }
                onDisk[p.path().string()] = mt;
            }
        }
    };
    try
    {
        if (recursive)
        {
            auto it = fs::recursive_directory_iterator(folder);
            scanDir(it);
        }
        else
        {
            auto it = fs::directory_iterator(folder);
            scanDir(it);
        }
    }
    catch (...)
    {
        Logger::instance().log(LWARN, "syncFolder: cannot scan " + folder);
        return result;
    }

    // 2. Build map of what's currently indexed: path -> {docID, mtime}
    unordered_map<string, int> indexed; // path -> docID
    for (int i = 0; i < (int)docs.size(); ++i)
        if (!docs[i].path.empty())
            indexed[docs[i].path] = i;

    // 3. Remove deleted and re-index modified
    for (auto &[path, docID] : indexed)
    {
        if (!onDisk.count(path))
        {
            // File deleted
            Logger::instance().log(LINFO, "sync: removing deleted " + path);
            removeDocument(docID);
            result.removed++;
        }
        else if (onDisk[path] != docs[docID].mtime && docs[docID].mtime != 0)
        {
            // File modified — remove old entry, will be re-added below
            Logger::instance().log(LINFO, "sync: re-indexing modified " + path);
            removeDocument(docID);
            result.modified++;
        }
    }

    // Rebuild indexed map after removals
    indexed.clear();
    for (int i = 0; i < (int)docs.size(); ++i)
        if (!docs[i].path.empty())
            indexed[docs[i].path] = i;

    // 4. Add new files (and re-add modified ones that were removed above)
    vector<string> toAdd;
    for (auto &[path, mt] : onDisk)
        if (!indexed.count(path))
            toAdd.push_back(path);

    if (!toAdd.empty())
    {
        int base = (int)docs.size();
        int newCount = (int)toAdd.size();
        docs.resize(base + newCount);
        docTf.resize(base + newCount);
        docTokenCount.resize(base + newCount, 0);

        ThreadPool pool((size_t)maxThreads);
        vector<future<unordered_map<const string *, vector<int>>>> futures;
        futures.reserve(newCount);
        for (int i = 0; i < newCount; ++i)
        {
            string path = toAdd[i];
            int docID = base + i;
            futures.push_back(pool.enqueue([this, path, docID]()
                                           {
                unordered_map<const string*, vector<int>> local;
                indexFileWorker(path, docID, local);
                return local; }));
        }
        for (int i = 0; i < newCount; ++i)
        {
            auto local = futures[i].get();
            int docID = base + i;
            unordered_set<const string *> seen;
            for (auto &kv : local)
            {
                const string *tp = kv.first;
                vector<int> positions = kv.second;
                double tf = 1.0 + log((double)positions.size());
                docTf[docID][tp] = tf;
                vocabMap[*tp] = tp;
                size_t sh = shardFor(tp, shardCount);
                {
                    lock_guard<mutex> lk(*shardMutexes[sh]);
                    postingsSharded[sh][tp].push_back({docID, positions});
                }
                seen.insert(tp);
            }
            {
                lock_guard<mutex> lk(metaMu);
                for (auto tp : seen)
                    df[*tp] += 1;
            }
        }
        pool.shutdown();
        result.added += newCount;
    }

    // 5. Recompute IDF, norms, avgDocLen
    int N = (int)docs.size();
    {
        lock_guard<mutex> lk(metaMu);
        idf.clear();
        for (auto &e : df)
            idf[e.first] = log((double)(N + 1) / (e.second + 1)) + 1.0;
    }
    for (int i = 0; i < N; ++i)
    {
        double sum = 0.0;
        for (auto &pr : docTf[i])
        {
            double tfw = pr.second;
            auto it = idf.find(*pr.first);
            double idfv = (it == idf.end()) ? 0.0 : it->second;
            sum += tfw * idfv * tfw * idfv;
        }
        docs[i].norm = sqrt(sum);
    }
    double tot = 0.0;
    for (int c : docTokenCount)
        tot += c;
    avgDocLenCache = docTokenCount.empty() ? 0.0 : tot / docTokenCount.size();

    buildVocabStructures();

    Logger::instance().log(LINFO, "syncFolder done: +" + to_string(result.added) + " -" + to_string(result.removed) + " ~" + to_string(result.modified));
    return result;
}

// ---------------------- Search (BM25 & TF-IDF) ----------------------
// (implementations same as earlier, omitted here for brevity - see previous messages)
// For completeness place the BM25 and TF-IDF functions here exactly as in your working copy.

vector<ScoredDoc> Indexer::searchBM25Parallel(const vector<string> &qterms, int topK) const
{
    // Collect candidate doc IDs from all query terms
    unordered_map<int, unordered_map<string, int>> docTermFreq;
    for (auto &term : qterms)
    {
        auto ids = getDocIDsForTerm(term);
        for (int id : ids)
            docTermFreq[id][term]++;
    }

    // Score each candidate
    vector<ScoredDoc> results;
    results.reserve(docTermFreq.size());

    int N = (int)docs.size();
    double avgLen = avgDocLenCache;
    double k1 = 1.5, b = 0.75;

    for (auto &[docID, qcount] : docTermFreq)
    {
        double score = 0.0;
        double dl = docTokenCount[docID];
        for (auto &[term, qf] : qcount)
        {
            auto it = df.find(term);
            if (it == df.end())
                continue;
            int dfi = it->second;
            double idfVal = log(1.0 + (N - dfi + 0.5) / (dfi + 0.5));
            double freq = termFreqInDoc(term, docID);
            double denom = freq + k1 * (1.0 - b + b * (dl / (avgLen > 0 ? avgLen : 1.0)));
            score += idfVal * (freq * (k1 + 1.0)) / denom * qf;
        }
        results.push_back({docID, score});
    }

    sort(results.begin(), results.end(), [](auto &a, auto &b)
         { return a.score > b.score; });
    if ((int)results.size() > topK)
        results.resize(topK);
    return results;
}

vector<ScoredDoc> Indexer::searchTFIDF(const vector<string> &qterms, int topK) const
{
    unordered_map<int, double> scores;

    for (auto &term : qterms)
    {
        auto it = idf.find(term);
        if (it == idf.end())
            continue;
        double idfVal = it->second;

        auto ids = getDocIDsForTerm(term);
        for (int id : ids)
        {
            auto vit = vocabMap.find(term);
            if (vit == vocabMap.end())
                continue;
            const string *tp = vit->second;
            auto tfit = docTf[id].find(tp);
            if (tfit == docTf[id].end())
                continue;
            double w = tfit->second * idfVal;
            double norm = docs[id].norm > 0 ? docs[id].norm : 1.0;
            scores[id] += w / norm;
        }
    }

    vector<ScoredDoc> results;
    results.reserve(scores.size());
    for (auto &[id, score] : scores)
        results.push_back({id, score});

    sort(results.begin(), results.end(), [](auto &a, auto &b)
         { return a.score > b.score; });
    if ((int)results.size() > topK)
        results.resize(topK);
    return results;
}

// ---------------------- Access helpers ----------------------
const string *Indexer::lookupTermPtr(const string &term) const
{
    auto it = vocabMap.find(term);
    if (it != vocabMap.end())
        return it->second;
    return nullptr;
}

unordered_set<int> Indexer::getDocIDsForTerm(const string &term) const
{
    unordered_set<int> out;
    const string *tp = lookupTermPtr(term);
    if (!tp)
        return out;
    size_t sh = shardFor(tp, shardCount);
    lock_guard<mutex> lk(*shardMutexes[sh]);
    auto it = postingsSharded[sh].find(tp);
    if (it == postingsSharded[sh].end())
        return out;
    for (auto &p : it->second)
        out.insert(p.docID);
    return out;
}

vector<int> Indexer::getDocIDsSorted(const string &term) const
{
    const string *tp = lookupTermPtr(term);
    if (!tp)
        return {};
    size_t sh = shardFor(tp, shardCount);
    lock_guard<mutex> lk(*shardMutexes[sh]);
    auto it = postingsSharded[sh].find(tp);
    if (it == postingsSharded[sh].end())
        return {};
    vector<int> out;
    out.reserve(it->second.size());
    for (auto &p : it->second)
        out.push_back(p.docID);
    sort(out.begin(), out.end());
    return out;
}

const vector<Posting> *Indexer::getPostings(const string &term) const
{
    const string *tp = lookupTermPtr(term);
    if (!tp)
        return nullptr;
    size_t sh = shardFor(tp, shardCount);
    lock_guard<mutex> lk(*shardMutexes[sh]);
    auto it = postingsSharded[sh].find(tp);
    if (it == postingsSharded[sh].end())
        return nullptr;
    return &it->second;
}

bool Indexer::docHasPhrase(int docID, const vector<string> &terms) const
{
    if (terms.empty())
        return false;
    vector<const vector<int> *> posLists;
    for (auto &t : terms)
    {
        const string *tp = lookupTermPtr(t);
        if (!tp)
            return false;
        size_t sh = shardFor(tp, shardCount);
        lock_guard<mutex> lk(*shardMutexes[sh]);
        auto it = postingsSharded[sh].find(tp);
        if (it == postingsSharded[sh].end())
            return false;
        const vector<Posting> &plist = it->second;
        const vector<int> *found = nullptr;
        for (auto &post : plist)
            if (post.docID == docID)
            {
                found = &post.positions;
                break;
            }
        if (!found)
            return false;
        posLists.push_back(found);
    }
    for (int p : *posLists[0])
    {
        bool ok = true;
        for (size_t i = 1; i < posLists.size(); ++i)
        {
            if (find(posLists[i]->begin(), posLists[i]->end(), p + (int)i) == posLists[i]->end())
            {
                ok = false;
                break;
            }
        }
        if (ok)
            return true;
    }
    return false;
}

int Indexer::dfCount(const string &term) const
{
    auto it = df.find(term);
    return it == df.end() ? 0 : it->second;
}

int Indexer::termFreqInDoc(const string &term, int docID) const
{
    const string *tp = lookupTermPtr(term);
    if (!tp)
        return 0;
    size_t sh = shardFor(tp, shardCount);
    lock_guard<mutex> lk(*shardMutexes[sh]);
    auto it = postingsSharded[sh].find(tp);
    if (it == postingsSharded[sh].end())
        return 0;
    for (auto &p : it->second)
        if (p.docID == docID)
            return (int)p.positions.size();
    return 0;
}

double Indexer::docLength(int docID) const { return docTokenCount.at(docID); }
double Indexer::avgDocLength() const { return avgDocLenCache; }

// ---------------------- Remove document ----------------------
void Indexer::removeDocument(int docID)
{
    if (docID < 0 || docID >= (int)docs.size())
        return;
    for (int sh = 0; sh < shardCount; ++sh)
    {
        lock_guard<mutex> lk(*shardMutexes[sh]);
        for (auto &entry : postingsSharded[sh])
        {
            auto &plist = entry.second;
            plist.erase(remove_if(plist.begin(), plist.end(), [&](const Posting &p)
                                  { return p.docID == docID; }),
                        plist.end());
        }
    }
    docTf[docID].clear();
    docTokenCount[docID] = 0;
    df.clear();
    for (int sh = 0; sh < shardCount; ++sh)
    {
        lock_guard<mutex> lk(*shardMutexes[sh]);
        for (auto &entry : postingsSharded[sh])
        {
            const string *tp = entry.first;
            df[*tp] = entry.second.size();
        }
    }
    int N = docs.size();
    idf.clear();
    for (auto &e : df)
        idf[e.first] = log((double)(N + 1) / (e.second + 1)) + 1.0;
    docs[docID].path.clear();
    // Evict from content cache
    {
        lock_guard<mutex> lk(contentCacheMu);
        contentCache.erase(docID);
    }
    buildVocabStructures();
}

// ---------------------- Vocab helper structures ----------------------
void Indexer::buildVocabStructures()
{
    if (trie)
    {
        delete trie;
        trie = nullptr;
    }
    if (bk)
    {
        delete bk;
        bk = nullptr;
    }
    trie = new Trie();
    bk = new BKTree();
    vocabMap.clear();
    for (auto &p : df)
    {
        const string &term = p.first;
        const string *tp = interner.intern(term);
        vocabMap[term] = tp;
        trie->insert(term, p.second);
        bk->add(term);
    }
}

// ---------------------- Lazy content load ----------------------
string Indexer::loadContent(int docID) const
{
    // Check cache first
    {
        lock_guard<mutex> lk(contentCacheMu);
        auto it = contentCache.find(docID);
        if (it != contentCache.end())
            return it->second;
    }

    // Try disk
    const string &path = docs.at(docID).path;
    if (path.empty() || !fs::exists(path))
        return "";

    string content = loadFileContent(path);
    {
        lock_guard<mutex> lk(contentCacheMu);
        if ((int)contentCache.size() >= kContentCacheMax)
            contentCache.erase(contentCache.begin());
        contentCache[docID] = content;
    }
    return content;
}

// ---------------------- Snippet ----------------------
string Indexer::makeSnippet(int docID, const vector<string> &qterms, size_t window) const
{
    const Document &doc = docs.at(docID);

    // Lazy load content
    string content = loadContent(docID);
    if (content.empty())
    {
        if (doc.path.empty() || !fs::exists(doc.path))
            return "[file no longer available]";
        return "[empty document]";
    }

    // Build lowercase content once
    string lower = content;
    transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // Collect all hit positions for every query term
    // hits[i] = char offset in content where a query term starts
    vector<size_t> hits;
    vector<size_t> hitLens; // length of the matched term at each hit

    for (auto &q : qterms)
    {
        if (q.empty())
            continue;
        string ql = q;
        transform(ql.begin(), ql.end(), ql.begin(), ::tolower);
        size_t pos = 0;
        while ((pos = lower.find(ql, pos)) != string::npos)
        {
            hits.push_back(pos);
            hitLens.push_back(ql.size());
            pos += ql.size();
        }
    }

    // No hits at all — return first 200 chars
    if (hits.empty())
    {
        size_t n = min((size_t)200, content.size());
        return content.substr(0, n) + (content.size() > n ? "..." : "");
    }

    // Sort hits by position
    vector<size_t> idx(hits.size());
    iota(idx.begin(), idx.end(), 0);
    sort(idx.begin(), idx.end(), [&](size_t a, size_t b)
         { return hits[a] < hits[b]; });

    // Sliding window: find the window of `window` chars that contains
    // the most distinct query terms
    size_t bestStart = 0;
    int bestScore = 0;
    size_t left = 0;

    // Count distinct terms in current window using a frequency map
    unordered_map<string, int> inWindow;
    int distinctInWindow = 0;

    for (size_t r = 0; r < idx.size(); ++r)
    {
        size_t pos = hits[idx[r]];
        // Find which term this hit belongs to
        string termKey;
        for (auto &q : qterms)
        {
            string ql = q;
            transform(ql.begin(), ql.end(), ql.begin(), ::tolower);
            if (lower.substr(pos, ql.size()) == ql)
            {
                termKey = ql;
                break;
            }
        }
        if (termKey.empty())
            continue;

        if (inWindow[termKey]++ == 0)
            distinctInWindow++;

        // Shrink window from left if it exceeds `window` chars
        while (left < r && pos - hits[idx[left]] > window)
        {
            size_t lpos = hits[idx[left]];
            string lkey;
            for (auto &q : qterms)
            {
                string ql = q;
                transform(ql.begin(), ql.end(), ql.begin(), ::tolower);
                if (lower.substr(lpos, ql.size()) == ql)
                {
                    lkey = ql;
                    break;
                }
            }
            if (!lkey.empty() && --inWindow[lkey] == 0)
                distinctInWindow--;
            ++left;
        }

        if (distinctInWindow > bestScore)
        {
            bestScore = distinctInWindow;
            bestStart = hits[idx[left]];
        }
    }

    // Extract the snippet around bestStart
    size_t start = bestStart > 40 ? bestStart - 40 : 0;
    size_t end = min(content.size(), start + window + 80);

    // Snap to word boundaries
    while (start > 0 && content[start] != ' ' && content[start] != '\n')
        --start;
    while (end < content.size() && content[end] != ' ' && content[end] != '\n')
        ++end;

    string snip = content.substr(start, end - start);
    // Trim leading/trailing whitespace
    size_t s = snip.find_first_not_of(" \t\n\r");
    size_t e = snip.find_last_not_of(" \t\n\r");
    if (s != string::npos)
        snip = snip.substr(s, e - s + 1);

    if (start > 0)
        snip = "..." + snip;
    if (end < content.size())
        snip += "...";

    return snip;
}

// ---------------------- Dashboard helpers ----------------------
vector<pair<string, int>> Indexer::getTopTerms(int k) const
{
    lock_guard<mutex> lk(metaMu);
    vector<pair<string, int>> terms(df.begin(), df.end());
    sort(terms.begin(), terms.end(), [](auto &a, auto &b)
         { return a.second > b.second; });
    if ((int)terms.size() > k)
        terms.resize(k);
    return terms;
}

uint64_t Indexer::indexSizeBytes() const
{
    uint64_t total = 0;
    for (int sh = 0; sh < shardCount; ++sh)
    {
        lock_guard<mutex> lk(*shardMutexes[sh]);
        for (auto &entry : postingsSharded[sh])
            for (auto &p : entry.second)
                total += sizeof(Posting) + p.positions.size() * sizeof(int);
    }
    total += docs.size() * sizeof(Document);
    return total;
}

void Indexer::preloadHotDocs(int k)
{
    vector<pair<int, int>> ranked;
    ranked.reserve(docs.size());
    for (int i = 0; i < (int)docs.size(); ++i)
        ranked.push_back({i, docTokenCount[i]});
    sort(ranked.begin(), ranked.end(), [](auto &a, auto &b)
         { return a.second > b.second; });
    int limit = min(k, (int)ranked.size());
    for (int i = 0; i < limit; ++i)
        loadContent(ranked[i].first); // populates contentCache
    Logger::instance().log(LINFO, "Preloaded " + to_string(limit) + " hot docs into content cache");
}

// ---------------------- Autocomplete / Spell correction ----------------------
vector<pair<string, int>> Indexer::autocomplete(const string &prefix, int k) const
{
    if (!trie)
        return {};
    return trie->autocomplete(prefix, k);
}

vector<string> Indexer::spellCorrect(const string &term, int maxDist) const
{
    if (!bk)
        return {};
    return bk->query(term, maxDist);
}
