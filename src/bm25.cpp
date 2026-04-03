#include "bm25.h"
#include "indexer.h"
#include <cmath>

BM25::BM25(const Indexer* i, BM25Config c) : idx(i), cfg(c) {}

double BM25::scoreTerm(int docID, const std::string &term, int qf) const {
    int df = idx->dfCount(term);
    if (df == 0) return 0.0;

    int N = idx->numDocs();
    double idf = std::log(1.0 + (N - df + 0.5) / (df + 0.5));

    double freq   = idx->termFreqInDoc(term, docID);
    double docLen = idx->docLength(docID);
    double avgLen = idx->avgDocLength();

    double denom = freq + cfg.k1 * (1 - cfg.b + cfg.b * (docLen / avgLen));
    if (denom <= 0.0) return 0.0;

    return idf * ((freq * (cfg.k1 + 1)) / denom) * qf;
}

double BM25::scoreDoc(int docID,
                      const std::unordered_map<std::string,int> &qterms) const
{
    double s = 0;
    for (auto &p : qterms)
        s += scoreTerm(docID, p.first, p.second);
    return s;
}
