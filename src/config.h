#pragma once
#include <string>

class Config {
public:
    std::string docsFolder = "docs";
    std::string indexFile = "index.bin";
    std::string walFile = "wal.log";
    std::string logFile = "engine.log";

    int maxThreads = 4;
    int shardCount = 8;

    bool useWAL = false;
    bool useStemming = false;
    bool useStopwords = true;

    std::string scoring = "bm25";
    std::string extFilter = "";

    int indexBatchSize = 1000;
    int maxDocs = 1000000;

    bool loadFromFile(const std::string &path);

private:
    static bool parseBool(const std::string &val);
    static bool parseInt(const std::string &val, int &out);
};