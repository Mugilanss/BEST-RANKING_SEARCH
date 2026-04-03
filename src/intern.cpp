#include "intern.h"
using namespace std;

const string* Interner::intern(const string &s) {
    lock_guard<mutex> lk(mu);

    auto it = store.find(s);
    if (it != store.end())
        return &*it;

    auto p = store.insert(s);
    return &*p.first;
}

size_t Interner::size() const {
    lock_guard<mutex> lk(mu);
    return store.size();
}
