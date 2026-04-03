#pragma once
#include <string>
#include <vector>
#include <unordered_map>

struct BM25Config {
    double k1 = 1.5;
    double b = 0.75;
};

class Indexer; // forward-declare

class BM25 {
public:
    BM25(const Indexer* idx, BM25Config cfg = {});
    double scoreTerm(int docID, const std::string &term, int qf = 1) const;
    double scoreDoc(int docID, const std::unordered_map<std::string,int> &qterms) const;

private:
    const Indexer* idx;
    BM25Config cfg;
};
