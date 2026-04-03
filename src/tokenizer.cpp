#include "tokenizer.h"
#include <fstream>
#include <algorithm>
#include <cctype>

Tokenizer::Tokenizer() {}

std::string Tokenizer::toLower(const std::string &s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return out;
}

void Tokenizer::loadStopwords(const std::string &path) {
    stopwords.clear();
    std::ifstream f(path);
    if (!f) return;

    std::string w;
    while (std::getline(f, w)) {
        if (!w.empty()) {
            stopwords.insert(toLower(w));
        }
    }
}

std::vector<std::string> Tokenizer::tokenize(const std::string &text) const {
    std::vector<std::string> out;
    std::string cur;
    cur.reserve(32);

    auto flush = [&]() {
        if (cur.empty()) return;

        std::string term = toLower(cur);

        if (useStemming)
            term = stem(term);

        if (!useStopwords || stopwords.count(term) == 0)
            out.push_back(term);

        cur.clear();
    };

    for (unsigned char c : text) {
        if (std::isalnum(c)) {
            cur.push_back(static_cast<char>(c));
        } else {
            flush();
        }
    }

    flush();
    return out;
}

std::string Tokenizer::stem(const std::string &term) const {
    if (!useStemming) return term;

    std::string t = term;

    static const std::string suffixes[] = {
        "ing", "ed", "ly", "es", "s"
    };

    for (const auto &s : suffixes) {
        if (t.size() > s.size() + 2 &&
            t.compare(t.size() - s.size(), s.size(), s) == 0) {
            t.erase(t.size() - s.size());
            break;
        }
    }

    return t;
}