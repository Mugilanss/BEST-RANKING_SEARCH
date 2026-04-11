#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include <memory>

#include "tokenizer.h"
#include "indexer.h"

struct QNode {
    enum Type { TERM, PHRASE, AND, OR, NOT } type;
    std::string term;
    std::shared_ptr<QNode> left, right;

    QNode(Type t = TERM, const std::string &v = "") : type(t), term(v) {}

    static std::shared_ptr<QNode> makeTerm(const std::string &t);
    static std::shared_ptr<QNode> makePhrase(const std::string &p);
    static std::shared_ptr<QNode> makeNot(std::shared_ptr<QNode> child);
    static std::shared_ptr<QNode> makeBinary(Type tp, std::shared_ptr<QNode> l, std::shared_ptr<QNode> r);
};

class QueryParser {
public:
    QueryParser(const Tokenizer *tokenizer);

    std::shared_ptr<QNode> parse(const std::string &query) const;

    // Returns sorted vector<int> of matching docIDs — O(n) merge operations
    std::vector<int> evaluate(std::shared_ptr<QNode> node, const Indexer &idx) const;

    // Parallel AND: evaluates children concurrently then intersects
    std::vector<int> evaluateParallel(std::shared_ptr<QNode> node, const Indexer &idx, int maxThreads) const;

    // Legacy adapter: returns unordered_set for callers that need it
    std::unordered_set<int> evaluateSet(std::shared_ptr<QNode> node, const Indexer &idx) const;
    std::unordered_set<int> evaluateParallelSet(std::shared_ptr<QNode> node, const Indexer &idx, int maxThreads) const;

private:
    const Tokenizer *tokenizer;
    std::vector<std::string> tokenizeForParser(const std::string &q) const;

    // Sorted list merge primitives
    static std::vector<int> listAnd(const std::vector<int> &a, const std::vector<int> &b);
    static std::vector<int> listOr (const std::vector<int> &a, const std::vector<int> &b);
    static std::vector<int> listNot(const std::vector<int> &a, int totalDocs);
};
