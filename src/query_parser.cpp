#include "query_parser.h"
#include <sstream>
#include <stack>
#include <thread>
#include <future>
#include <algorithm>
#include <cctype>

//
// QNode factory implementations
//

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
    n->left = l;
    n->right = r;
    return n;
}

QueryParser::QueryParser(const Tokenizer *t) : tokenizer(t) {}

//
// Tokenizer for parser (handles phrases, parentheses, operators)
//

std::vector<std::string>
QueryParser::tokenizeForParser(const std::string &q) const {
    std::vector<std::string> out;
    size_t i = 0, n = q.size();

    while (i < n) {
        if (std::isspace(static_cast<unsigned char>(q[i]))) {
            ++i;
            continue;
        }

        // Quoted phrase
        if (q[i] == '"') {
            size_t j = i + 1;
            while (j < n && q[j] != '"') ++j;

            if (j < n) {
                out.push_back(q.substr(i, j - i + 1));
                i = j + 1;
            } else {
                out.push_back(q.substr(i));
                break;
            }
            continue;
        }

        // Parentheses
        if (q[i] == '(' || q[i] == ')') {
            out.push_back(std::string(1, q[i]));
            ++i;
            continue;
        }

        // Normal token
        size_t j = i;
        while (j < n &&
               !std::isspace(static_cast<unsigned char>(q[j])) &&
               q[j] != '(' && q[j] != ')') {
            ++j;
        }

        std::string tok = q.substr(i, j - i);
        std::string up = tok;

        for (auto &c : up)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        if (up == "AND" || up == "OR" || up == "NOT")
            out.push_back(up);
        else
            out.push_back(tok);

        i = j;
    }

    return out;
}

//
// Operator precedence
//

static int prec(const std::string &op) {
    if (op == "NOT") return 3;
    if (op == "AND") return 2;
    if (op == "OR")  return 1;
    return 0;
}

//
// Parse using shunting-yard → RPN → AST
//

std::shared_ptr<QNode>
QueryParser::parse(const std::string &query) const {

    auto toks = tokenizeForParser(query);
    std::vector<std::string> output;
    std::stack<std::string> ops;

    for (auto &t : toks) {

        if (t == "AND" || t == "OR" || t == "NOT") {

            while (!ops.empty() &&
                   ops.top() != "(" &&
                   ((prec(ops.top()) > prec(t)) ||
                    (prec(ops.top()) == prec(t) && t != "NOT"))) {

                output.push_back(ops.top());
                ops.pop();
            }

            ops.push(t);
        }

        else if (t == "(") {
            ops.push(t);
        }

        else if (t == ")") {
            while (!ops.empty() && ops.top() != "(") {
                output.push_back(ops.top());
                ops.pop();
            }
            if (!ops.empty() && ops.top() == "(")
                ops.pop();
        }

        else {
            output.push_back(t);
        }
    }

    while (!ops.empty()) {
        output.push_back(ops.top());
        ops.pop();
    }

    //
    // RPN → AST
    //

    std::stack<std::shared_ptr<QNode>> st;

    for (auto &tok : output) {

        if (tok == "AND" || tok == "OR") {

            if (st.size() < 2)
                return nullptr;

            auto r = st.top(); st.pop();
            auto l = st.top(); st.pop();

            st.push(QNode::makeBinary(
                tok == "AND" ? QNode::AND : QNode::OR, l, r));
        }

        else if (tok == "NOT") {

            if (st.empty())
                return nullptr;

            auto c = st.top(); st.pop();
            st.push(QNode::makeNot(c));
        }

        else {
            // Phrase
            if (tok.size() >= 2 &&
                tok.front() == '"' &&
                tok.back() == '"') {

                std::string phrase =
                    tok.substr(1, tok.size() - 2);

                std::vector<std::string> parts =
                    tokenizer ? tokenizer->tokenize(phrase)
                              : std::vector<std::string>{phrase};

                std::string joined;

                for (size_t i = 0; i < parts.size(); ++i) {
                    if (i) joined += ' ';
                    joined += parts[i];
                }

                st.push(QNode::makePhrase(joined));
            }
            else {
                std::vector<std::string> parts =
                    tokenizer ? tokenizer->tokenize(tok)
                              : std::vector<std::string>{tok};

                if (parts.empty())
                    st.push(QNode::makeTerm(tok));
                else
                    st.push(QNode::makeTerm(parts[0]));
            }
        }
    }

    if (st.size() != 1)
        return nullptr;

    return st.top();
}

// Evaluate AST against index
std::unordered_set<int>
QueryParser::evaluate(std::shared_ptr<QNode> node, const Indexer &idx) const {
    if (!node) return {};

    switch (node->type) {
        case QNode::TERM: {
            return idx.getDocIDsForTerm(node->term);
        }
        case QNode::PHRASE: {
            std::vector<std::string> parts =
                tokenizer ? tokenizer->tokenize(node->term)
                          : std::vector<std::string>{node->term};
            if (parts.empty()) return {};
            auto ids = idx.getDocIDsForTerm(parts[0]);
            std::unordered_set<int> result;
            for (int id : ids)
                if (idx.docHasPhrase(id, parts))
                    result.insert(id);
            return result;
        }
        case QNode::AND: {
            auto L = evaluate(node->left, idx);
            auto R = evaluate(node->right, idx);
            std::unordered_set<int> result;
            for (int id : L)
                if (R.count(id)) result.insert(id);
            return result;
        }
        case QNode::OR: {
            auto L = evaluate(node->left, idx);
            auto R = evaluate(node->right, idx);
            L.insert(R.begin(), R.end());
            return L;
        }
        case QNode::NOT: {
            auto child = evaluate(node->left, idx);
            std::unordered_set<int> result;
            int N = idx.numDocs();
            for (int i = 0; i < N; ++i)
                if (!child.count(i)) result.insert(i);
            return result;
        }
    }
    return {};
}

std::unordered_set<int>
QueryParser::evaluateParallel(std::shared_ptr<QNode> node, const Indexer &idx, int maxThreads) const {
    // For AND nodes, evaluate children in parallel
    if (node && node->type == QNode::AND && maxThreads > 1) {
        std::unordered_set<int> L, R;
        auto futL = std::async(std::launch::async, [&]{ return evaluate(node->left, idx); });
        R = evaluate(node->right, idx);
        L = futL.get();
        std::unordered_set<int> result;
        for (int id : L)
            if (R.count(id)) result.insert(id);
        return result;
    }
    return evaluate(node, idx);
}