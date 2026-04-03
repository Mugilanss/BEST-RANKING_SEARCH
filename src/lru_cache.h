#pragma once
#include <list>
#include <unordered_map>
#include <mutex>

template<typename K, typename V>
class LRUCache {
public:
    explicit LRUCache(size_t capacity = 1000) : cap(capacity) {}

    bool get(const K &k, V &out);
    void put(const K &k, const V &v);
    void clear();

private:
    size_t cap;
    std::list<std::pair<K,V>> items;
    std::unordered_map<K, typename std::list<std::pair<K,V>>::iterator> mp;
    std::mutex mu;
};

template<typename K, typename V>
bool LRUCache<K,V>::get(const K &k, V &out) {
    std::lock_guard<std::mutex> lk(mu);
    auto it = mp.find(k);
    if (it == mp.end()) return false;

    items.splice(items.begin(), items, it->second);
    out = it->second->second;
    return true;
}

template<typename K, typename V>
void LRUCache<K,V>::put(const K &k, const V &v) {
    std::lock_guard<std::mutex> lk(mu);

    auto it = mp.find(k);
    if (it != mp.end()) {
        it->second->second = v;
        items.splice(items.begin(), items, it->second);
        return;
    }

    items.emplace_front(k, v);
    mp[k] = items.begin();

    if (mp.size() > cap) {
        auto last = std::prev(items.end());
        mp.erase(last->first);
        items.pop_back();
    }
}

template<typename K, typename V>
void LRUCache<K,V>::clear() {
    std::lock_guard<std::mutex> lk(mu);
    items.clear();
    mp.clear();
}
