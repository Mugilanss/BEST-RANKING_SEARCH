#pragma once
#include <string>
#include <unordered_set>
#include <mutex>

class Interner {
public:
    Interner() = default;
    const std::string* intern(const std::string &s);
    size_t size() const;
private:
    std::unordered_set<std::string> store;
    mutable std::mutex mu;
};
