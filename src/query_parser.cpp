#include "query_parser.h"
#include <sstream>
#include <stack>
#include <thread>
#include <future>
#include <algorithm>
#include <cctype>
#include <unordered_set>

// ---- QNode factories ----

std::shared_ptr<QNode> QNode::makeTerm(const std::string &t) {
    return std::make_shared<QNode>(QNode::TERM, t);
}
std::shared_ptr<QNode> QNode::makePhrase(const std::string &p) {
    return std::make_shared<QNode>(QNode::PHRASE, p);
}
std::shared_ptr<QNode> QNode::makeNot(std::shared_ptr<QNode> child) {
    auto n = std::make_shared<QNode>(QNode::NOT);
    n->left = child;
    return n;
}
std::shared_ptr<QNode> QNode::makeBinary(Type tp,
                                         std::shared_ptr<QNode> l,
                                         std::shared_ptr<QNode> r) {
    auto n = std::make_shared<QNode>(tp);
    n->left = l; n->right = r;
    return n;
}

QueryParser::QueryParser(const Tokenizer *t) : tokenizer(t) {}

// ---- Sorted list merge primitives ----

// AND: sorted intersection — O(a+b)
std::vector<int> QueryParser::listAnd(const std::vector<int> &a, const std::vector<int> &b) {
    std::vector<int> out;
    out.reserve(std::min(a.size(), b.size()));
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if      (a[i] == b[j]) { out.push_back(a[i]); ++i; ++j; }
        else if (a[i]  < b[j]) ++i;
        else                   ++j;
    }
    return out;
}

// OR: sorted union — O(a+b)
std::vector<int> QueryParser::listOr(const std::vector<int> &a, const std::vector<int> &b) {
    std::vector<int> out;
    out.reserve(a.size() + b.size());
    std::merge(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(out));
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// NOT: complement against [0, totalDocs) — O(totalDocs)
std::vector<int> QueryParser::listNot(const std::vector<int> &a, int totalDocs) {
    std::vector<int> out;
    out.reserve(totalDocs - (int)a.size());
    size_t j = 0;
    for (int i = 0; i < totalDocs; ++i) {
        if (j < a.size() && a[j] == i) ++j;
        else out.push_back(i);
    }
    return out;
}

// ---- Parser tokenizer ----

std::vector<std::string>
QueryParser::tokenizeForParser(const std::string &q) const {
    std::vector<std::string> out;
    size_t i = 0, n = q.size();
    while (i < n) {
        if (std::isspace((unsigned char)q[i])) { ++i; continue; }
        if (q[i] == '"') {
            size_t j = i + 1;
            while (j < n && q[j] != '"') ++j;
            out.push_back(j < n ? q.substr(i, j-i+1) : q.substr(i));
            i = (j < n) ? j + 1 : n;
            continue;
        }
        if (q[i] == '(' || q[i] == ')') { out.push_back(std::string(1,q[i])); ++i; continue; }
        size_t j = i;
        while (j < n && !std::isspace((unsigned char)q[j]) && q[j]!='(' && q[j]!=')') ++j;
        std::string tok = q.substr(i, j-i);
        std::string up = tok;
        for (auto &c : up) c = (char)std::toupper((unsigned char)c);
        out.push_back((up=="AND"||up=="OR"||up=="NOT") ? up : tok);
        i = j;
    }
    return out;
}

static int prec(const std::string &op) {
    if (op == "NOT") return 3;
    if (op == "AND") return 2;
    if (op == "OR")  return 1;
    return 0;
}

// ---- Shunting-yard parser ----

std::shared_ptr<QNode>
QueryParser::parse(const std::string &query) const {
    auto toks = tokenizeForParser(query);
    std::vector<std::string> output;
    std::stack<std::string> ops;

    for (auto &t : toks) {
        if (t=="AND"||t=="OR"||t=="NOT") {
            while (!ops.empty() && ops.top()!="(" &&
                   (prec(ops.top())>prec(t) || (prec(ops.top())==prec(t) && t!="NOT")))
            { output.push_back(ops.top()); ops.pop(); }
            ops.push(t);
        } else if (t=="(") {
            ops.push(t);
        } else if (t==")") {
            while (!ops.empty() && ops.top()!="(") { output.push_back(ops.top()); ops.pop(); }
            if (!ops.empty()) ops.pop();
        } else {
            output.push_back(t);
        }
    }
    while (!ops.empty()) { output.push_back(ops.top()); ops.pop(); }

    std::stack<std::shared_ptr<QNode>> st;
    for (auto &tok : output) {
        if (tok=="AND"||tok=="OR") {
            if (st.size() < 2) return nullptr;
            auto r = st.top(); st.pop();
            auto l = st.top(); st.pop();
            st.push(QNode::makeBinary(tok=="AND" ? QNode::AND : QNode::OR, l, r));
        } else if (tok=="NOT") {
            if (st.empty()) return nullptr;
            auto c = st.top(); st.pop();
            st.push(QNode::makeNot(c));
        } else {
            if (tok.size()>=2 && tok.front()=='"' && tok.back()=='"') {
                std::string phrase = tok.substr(1, tok.size()-2);
                std::vector<std::string> parts = tokenizer
                    ? tokenizer->tokenize(phrase) : std::vector<std::string>{phrase};
                std::string joined;
                for (size_t i=0;i<parts.size();++i) { if(i) joined+=' '; joined+=parts[i]; }
                st.push(QNode::makePhrase(joined));
            } else {
                std::vector<std::string> parts = tokenizer
                    ? tokenizer->tokenize(tok) : std::vector<std::string>{tok};
                st.push(QNode::makeTerm(parts.empty() ? tok : parts[0]));
            }
        }
    }
    return (st.size()==1) ? st.top() : nullptr;
}

// ---- Core evaluate — sorted vector<int> ----

std::vector<int>
QueryParser::evaluate(std::shared_ptr<QNode> node, const Indexer &idx) const {
    if (!node) return {};

    switch (node->type) {

        case QNode::TERM: {
            // getPostingsSorted returns a sorted vector<int> of docIDs
            return idx.getDocIDsSorted(node->term);
        }

        case QNode::PHRASE: {
            std::vector<std::string> parts = tokenizer
                ? tokenizer->tokenize(node->term)
                : std::vector<std::string>{node->term};
            if (parts.empty()) return {};
            auto candidates = idx.getDocIDsSorted(parts[0]);
            std::vector<int> result;
            result.reserve(candidates.size());
            for (int id : candidates)
                if (idx.docHasPhrase(id, parts))
                    result.push_back(id);
            return result;
        }

        case QNode::AND: {
            // Evaluate smaller list first (optimization)
            auto L = evaluate(node->left,  idx);
            auto R = evaluate(node->right, idx);
            if (L.size() > R.size()) std::swap(L, R);
            return listAnd(L, R);
        }

        case QNode::OR: {
            auto L = evaluate(node->left,  idx);
            auto R = evaluate(node->right, idx);
            return listOr(L, R);
        }

        case QNode::NOT: {
            auto child = evaluate(node->left, idx);
            return listNot(child, idx.numDocs());
        }
    }
    return {};
}

// ---- Parallel evaluate — AND branches run concurrently ----

std::vector<int>
QueryParser::evaluateParallel(std::shared_ptr<QNode> node, const Indexer &idx, int maxThreads) const {
    if (node && node->type == QNode::AND && maxThreads > 1) {
        auto futL = std::async(std::launch::async,
            [&]{ return evaluate(node->left, idx); });
        auto R = evaluate(node->right, idx);
        auto L = futL.get();
        if (L.size() > R.size()) std::swap(L, R);
        return listAnd(L, R);
    }
    return evaluate(node, idx);
}

// ---- Legacy adapters (for callers that still use unordered_set) ----

std::unordered_set<int>
QueryParser::evaluateSet(std::shared_ptr<QNode> node, const Indexer &idx) const {
    auto v = evaluate(node, idx);
    return std::unordered_set<int>(v.begin(), v.end());
}

std::unordered_set<int>
QueryParser::evaluateParallelSet(std::shared_ptr<QNode> node, const Indexer &idx, int maxThreads) const {
    auto v = evaluateParallel(node, idx, maxThreads);
    return std::unordered_set<int>(v.begin(), v.end());
}
