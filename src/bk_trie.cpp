#include "bk_trie.h"
#include <queue>
#include <algorithm>

Trie::Trie() { nodes.push_back(Node()); }

void Trie::insert(const std::string &word, int freq) {
    int cur = 0;
    for (char c : word) {
        if (!nodes[cur].next.count(c)) {
            nodes[cur].next[c] = (int)nodes.size();
            nodes.push_back(Node());
        }
        cur = nodes[cur].next[c];
    }
    nodes[cur].terminal = true;
    nodes[cur].freq += freq;
}

static bool pair_cmp(const std::pair<std::string,int>&a, const std::pair<std::string,int>&b){
    if (a.second != b.second) return a.second > b.second;
    return a.first < b.first;
}

std::vector<std::pair<std::string,int>> Trie::autocomplete(const std::string &prefix, int k) const {
    std::vector<std::pair<std::string,int>> out;
    int cur = 0;
    std::string curstr = prefix;
    for (char c : prefix) {
        auto it = nodes[cur].next.find(c);
        if (it == nodes[cur].next.end()) return {};
        cur = it->second;
    }
    collect(cur, curstr, out);
    std::sort(out.begin(), out.end(), pair_cmp);
    if ((int)out.size() > k) out.resize(k);
    return out;
}

void Trie::collect(int nodeIdx, std::string &cur, std::vector<std::pair<std::string,int>> &out) const {
    if (nodes[nodeIdx].terminal) out.emplace_back(cur, nodes[nodeIdx].freq);
    for (auto &p : nodes[nodeIdx].next) {
        cur.push_back(p.first);
        collect(p.second, cur, out);
        cur.pop_back();
    }
}

// ---------------------- BK-tree -----------------------
BKTree::BKTree() {}

void BKTree::add(const std::string &term) {
    if (root == -1) {
        nodes.push_back({term, {}});
        root = 0; return;
    }
    int cur = root;
    while (true) {
        int d = editDistance(nodes[cur].term, term);
        if (d == 0) return;
        auto it = nodes[cur].children.find(d);
        if (it == nodes[cur].children.end()) {
            nodes[cur].children[d] = (int)nodes.size();
            nodes.push_back({term, {}});
            return;
        }
        cur = it->second;
    }
}

std::vector<std::string> BKTree::query(const std::string &term, int maxDist) const {
    std::vector<std::string> res;
    if (root == -1) return res;
    std::vector<int> st = {root};
    while (!st.empty()) {
        int cur = st.back(); st.pop_back();
        int d = editDistance(nodes[cur].term, term);
        if (d <= maxDist) res.push_back(nodes[cur].term);
        for (auto &ch : nodes[cur].children) {
            if (ch.first >= d - maxDist && ch.first <= d + maxDist)
                st.push_back(ch.second);
        }
    }
    return res;
}

int BKTree::editDistance(const std::string &a, const std::string &b) {
    int n = (int)a.size(), m = (int)b.size();
    if (n == 0) return m;
    if (m == 0) return n;
    std::vector<int> prev(m+1), cur(m+1);
    for (int j=0;j<=m;++j) prev[j] = j;
    for (int i=1;i<=n;++i) {
        cur[0] = i;
        for (int j=1;j<=m;++j) {
            int cost = (a[i-1] == b[j-1]) ? 0 : 1;
            int v1 = prev[j] + 1;
            int v2 = cur[j-1] + 1;
            int v3 = prev[j-1] + cost;
            cur[j] = std::min({v1, v2, v3});
        }
        prev.swap(cur);
    }
    return prev[m];
}
