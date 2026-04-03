#include "varint.h"
#include <istream>
#include <stdexcept>
using namespace std;

void varint::encode_u64(uint64_t v, std::vector<uint8_t> &out) {
    while (v >= 0x80) {
        out.push_back((uint8_t)(v | 0x80));
        v >>= 7;
    }
    out.push_back((uint8_t)v);
}

void varint::write_u64(std::ostream &os, uint64_t v) {
    while (v >= 0x80) {
        uint8_t b = (uint8_t)(v | 0x80);
        os.put((char)b);
        v >>= 7;
    }
    os.put((char)v);
}

uint64_t varint::decode_u64(const std::vector<uint8_t> &in, size_t &pos) {
    uint64_t result = 0;
    int shift = 0;

    while (pos < in.size()) {
        uint8_t byte = in[pos++];
        result |= (uint64_t)(byte & 0x7F) << shift;
        if (!(byte & 0x80))
            break;
        shift += 7;
    }

    return result;
}

uint64_t varint::read_u64(std::istream &is) {
    uint64_t result = 0;
    int shift = 0;

    while (true) {
        int c = is.get();
        if (c == EOF)
            throw runtime_error("EOF in varint::read_u64");

        uint8_t byte = (uint8_t)c;
        result |= (uint64_t)(byte & 0x7F) << shift;

        if (!(byte & 0x80))
            break;

        shift += 7;
    }

    return result;
}
