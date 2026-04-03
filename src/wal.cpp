#include "wal.h"
#include <iostream>
using namespace std;

WAL::~WAL() {
    if (ofs.is_open())
        ofs.close();
}

bool WAL::open(const string &p) {
    path = p;
    ofs.open(path, ios::binary | ios::app);
    return ofs.good();
}

void WAL::appendAdd(const string &p) {
    lock_guard<mutex> lk(mu);
    if (!ofs.is_open()) return;

    uint8_t t = (uint8_t)WalOp::ADD;
    uint64_t len = p.size();

    ofs.write((char*)&t, 1);
    ofs.write((char*)&len, sizeof(len));
    ofs.write(p.data(), p.size());
    ofs.flush();
}

void WAL::appendRemove(const string &p) {
    lock_guard<mutex> lk(mu);
    if (!ofs.is_open()) return;

    uint8_t t = (uint8_t)WalOp::REMOVE;
    uint64_t len = p.size();

    ofs.write((char*)&t, 1);
    ofs.write((char*)&len, sizeof(len));
    ofs.write(p.data(), p.size());
    ofs.flush();
}

void WAL::appendReindex() {
    lock_guard<mutex> lk(mu);
    if (!ofs.is_open()) return;

    uint8_t t = (uint8_t)WalOp::REINDEX;
    uint64_t zero = 0;

    ofs.write((char*)&t, 1);
    ofs.write((char*)&zero, sizeof(zero));
    ofs.flush();
}

bool WAL::replay(function<void(WalOp, const string&)> handler) {
    lock_guard<mutex> lk(mu);

    if (!ofs.is_open()) {
        // try to open for reading
        ifstream ifs(path, ios::binary);
        if (!ifs) return false;

        while (ifs.peek() != EOF) {
            uint8_t t;
            ifs.read((char*)&t, 1);

            uint64_t len;
            ifs.read((char*)&len, sizeof(len));

            string s;
            if (len) {
                s.resize(len);
                ifs.read(&s[0], len);
            }

            handler((WalOp)t, s);
        }
        return true;
    } else {
        // ofs open in append -> open a read handle
        ifstream ifs(path, ios::binary);
        if (!ifs) return false;

        while (ifs.peek() != EOF) {
            uint8_t t;
            ifs.read((char*)&t, 1);

            uint64_t len;
            ifs.read((char*)&len, sizeof(len));

            string s;
            if (len) {
                s.resize(len);
                ifs.read(&s[0], len);
            }

            handler((WalOp)t, s);
        }
        return true;
    }
}
