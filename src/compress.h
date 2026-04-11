#pragma once
// Minimal LZ4-inspired compression — no external dependencies.
// Uses a sliding window hash to find back-references.
// Provides ~40-60% compression on posting list data.

#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

namespace compress {

// Compress src into dst. Returns compressed size.
inline std::vector<uint8_t> encode(const uint8_t *src, size_t srcLen) {
    std::vector<uint8_t> dst;
    dst.reserve(srcLen / 2 + 64);

    // Hash table for finding matches: maps 4-byte hash -> last position
    static const int HASH_BITS = 16;
    static const int HASH_SIZE = 1 << HASH_BITS;
    std::vector<int32_t> htable(HASH_SIZE, -1);

    auto hash4 = [](const uint8_t *p) -> uint32_t {
        uint32_t v; memcpy(&v, p, 4);
        return (v * 2654435761u) >> (32 - HASH_BITS);
    };

    size_t ip = 0;       // input position
    size_t anchor = 0;   // start of current literal run

    auto emitLiterals = [&](size_t litStart, size_t litLen, int matchLen, int offset) {
        // Token byte: high nibble = literal count (0-15), low nibble = match extra (0-15)
        uint8_t litNibble = (uint8_t)std::min(litLen, (size_t)15);
        uint8_t matchNibble = (uint8_t)std::min((size_t)(matchLen - 4), (size_t)15);
        dst.push_back((litNibble << 4) | matchNibble);

        // Extra literal length bytes
        if (litLen >= 15) {
            size_t extra = litLen - 15;
            while (extra >= 255) { dst.push_back(255); extra -= 255; }
            dst.push_back((uint8_t)extra);
        }
        // Literal bytes
        for (size_t i = 0; i < litLen; ++i) dst.push_back(src[litStart + i]);

        if (matchLen > 0) {
            // Offset (little-endian 16-bit)
            dst.push_back((uint8_t)(offset & 0xFF));
            dst.push_back((uint8_t)(offset >> 8));
            // Extra match length bytes
            if (matchLen - 4 >= 15) {
                size_t extra = matchLen - 4 - 15;
                while (extra >= 255) { dst.push_back(255); extra -= 255; }
                dst.push_back((uint8_t)extra);
            }
        }
    };

    if (srcLen < 8) {
        // Too small to compress
        emitLiterals(0, srcLen, 0, 0);
        return dst;
    }

    while (ip < srcLen - 4) {
        uint32_t h = hash4(src + ip);
        int32_t ref = htable[h];
        htable[h] = (int32_t)ip;

        int offset = (int)(ip - ref);
        if (ref >= 0 && offset > 0 && offset < 65536 &&
            memcmp(src + ip, src + ref, 4) == 0)
        {
            // Found a match — extend it
            size_t matchLen = 4;
            while (ip + matchLen < srcLen &&
                   src[ip + matchLen] == src[ref + matchLen] &&
                   matchLen < 65535)
                ++matchLen;

            emitLiterals(anchor, ip - anchor, (int)matchLen, offset);
            ip += matchLen;
            anchor = ip;
        } else {
            ++ip;
        }
    }

    // Emit remaining literals (no match at end)
    if (anchor < srcLen) {
        size_t litLen = srcLen - anchor;
        dst.push_back((uint8_t)(std::min(litLen, (size_t)15) << 4));
        if (litLen >= 15) {
            size_t extra = litLen - 15;
            while (extra >= 255) { dst.push_back(255); extra -= 255; }
            dst.push_back((uint8_t)extra);
        }
        for (size_t i = 0; i < litLen; ++i) dst.push_back(src[anchor + i]);
    }

    return dst;
}

// Decompress src into dst (must know original size).
inline bool decode(const uint8_t *src, size_t srcLen,
                   uint8_t *dst, size_t dstLen)
{
    size_t ip = 0, op = 0;

    while (ip < srcLen) {
        uint8_t token = src[ip++];
        size_t litLen = token >> 4;
        size_t matchLen = token & 0x0F;

        // Extra literal length
        if (litLen == 15) {
            uint8_t extra;
            do { if (ip >= srcLen) return false; extra = src[ip++]; litLen += extra; } while (extra == 255);
        }
        // Copy literals
        if (op + litLen > dstLen || ip + litLen > srcLen) return false;
        memcpy(dst + op, src + ip, litLen);
        op += litLen; ip += litLen;

        if (ip >= srcLen) break; // last sequence has no match

        // Read offset
        if (ip + 2 > srcLen) return false;
        uint16_t offset = (uint16_t)(src[ip] | (src[ip+1] << 8)); ip += 2;
        if (offset == 0 || op < offset) return false;

        // Extra match length
        if (matchLen == 15) {
            uint8_t extra;
            do { if (ip >= srcLen) return false; extra = src[ip++]; matchLen += extra; } while (extra == 255);
        }
        matchLen += 4;

        // Copy match (may overlap)
        size_t matchStart = op - offset;
        if (op + matchLen > dstLen) return false;
        for (size_t i = 0; i < matchLen; ++i)
            dst[op++] = dst[matchStart + i];
    }
    return true;
}

// Convenience wrappers for string/vector
inline std::vector<uint8_t> compress(const std::vector<uint8_t> &data) {
    return encode(data.data(), data.size());
}

inline bool decompress(const std::vector<uint8_t> &compressed,
                       std::vector<uint8_t> &out, size_t originalSize) {
    out.resize(originalSize);
    return decode(compressed.data(), compressed.size(), out.data(), originalSize);
}

} // namespace compress
