#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include <memory>

#include "tokenizer.h"
#include "indexer.h"

struct QNode {
    enum Type { TERM, PHRASE, AND, OR, NOT } type;
    std::string term; // normalized term or phrase
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

    // parse query into AST
    std::shared_ptr<QNode> parse(const std::string &query) const;

    // Evaluate AST sequentially
    std::unordered_set<int> evaluate(std::shared_ptr<QNode> node, const Indexer &idx) const;

    // Evaluate AST in parallel (AND intersection parallelized)
    std::unordered_set<int> evaluateParallel(std::shared_ptr<QNode> node, const Indexer &idx, int maxThreads) const;

private:
    const Tokenizer *tokenizer;
    std::vector<std::string> tokenizeForParser(const std::string &q) const;
};
