#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <ostream>
#include <istream>

namespace varint {

    void encode_u64(uint64_t v, std::vector<uint8_t> &out);
    void write_u64(std::ostream &os, uint64_t v);

    uint64_t decode_u64(const std::vector<uint8_t> &in, size_t &pos);
    uint64_t read_u64(std::istream &is);
}
