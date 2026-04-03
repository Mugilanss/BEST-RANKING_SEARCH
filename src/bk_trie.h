#pragma once
#include <string>
#include <vector>
#include <unordered_map>

class Trie {
public:
    struct Node {
        std::unordered_map<char,int> next;
        bool terminal = false;
        int freq = 0;
    };
    Trie();
    void insert(const std::string &word, int freq=1);
    std::vector<std::pair<std::string,int>> autocomplete(const std::string &prefix, int k=10) const;

private:
    std::vector<Node> nodes;
    void collect(int nodeIdx, std::string &cur, std::vector<std::pair<std::string,int>> &out) const;
};

class BKTree {
public:
    BKTree();
    void add(const std::string &term);
    std::vector<std::string> query(const std::string &term, int maxDist) const;

private:
    struct Node { std::string term; std::unordered_map<int,int> children; };
    std::vector<Node> nodes;
    int root = -1;
    static int editDistance(const std::string &a, const std::string &b);
};
