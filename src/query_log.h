#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <fstream>
#include <algorithm>

// Logs every query with timestamp, persists to a file, exposes top-N popular terms.
class QueryLog {
public:
    explicit QueryLog(const std::string &path = "") : logPath(path) {
        if (!path.empty()) load();
    }

    void record(const std::string &query) {
        std::lock_guard<std::mutex> lk(mu);
        counts[query]++;
        if (!logPath.empty()) {
            std::ofstream ofs(logPath, std::ios::app);
            ofs << query << "\n";
        }
    }

    // Returns top-k queries by frequency
    std::vector<std::pair<std::string,int>> topQueries(int k = 10) const {
        std::lock_guard<std::mutex> lk(mu);
        std::vector<std::pair<std::string,int>> v(counts.begin(), counts.end());
        std::sort(v.begin(), v.end(), [](auto &a, auto &b){ return a.second > b.second; });
        if ((int)v.size() > k) v.resize(k);
        return v;
    }

    // Returns queries that start with prefix (for auto-suggest)
    std::vector<std::string> suggest(const std::string &prefix, int k = 5) const {
        std::lock_guard<std::mutex> lk(mu);
        std::vector<std::pair<std::string,int>> matches;
        for (auto &[q, c] : counts)
            if (q.size() >= prefix.size() && q.substr(0, prefix.size()) == prefix)
                matches.push_back({q, c});
        std::sort(matches.begin(), matches.end(), [](auto &a, auto &b){ return a.second > b.second; });
        std::vector<std::string> out;
        for (int i = 0; i < (int)matches.size() && i < k; ++i)
            out.push_back(matches[i].first);
        return out;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lk(mu);
        return counts.size();
    }

private:
    void load() {
        std::ifstream ifs(logPath);
        std::string line;
        while (std::getline(ifs, line))
            if (!line.empty()) counts[line]++;
    }

    std::string logPath;
    mutable std::mutex mu;
    std::unordered_map<std::string,int> counts;
};
