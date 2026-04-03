#pragma once
#include <string>
#include <vector>
#include <unordered_set>

class Tokenizer {
public:
    Tokenizer();

    void loadStopwords(const std::string &path);

    std::vector<std::string> tokenize(const std::string &text) const;

    std::string stem(const std::string &term) const;

    bool useStopwords = true;
    bool useStemming = false;

private:
    std::unordered_set<std::string> stopwords;

    static std::string toLower(const std::string &s);
};