#pragma once
#include <string>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <sstream>
#include <iomanip>

class Auth {
public:
    static std::string hashPassword(const std::string &password) {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256((unsigned char*)password.c_str(), password.size(), hash);
        std::ostringstream oss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
            oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        return oss.str();
    }

    static std::string generateToken(const std::string &username,
                                     const std::string &role,
                                     const std::string &secret) {
        // Simple HS256 JWT without jwt-cpp dependency
        // Header: {"alg":"HS256","typ":"JWT"}
        // Payload: {"sub":"username","role":"role","exp":timestamp}
        auto b64url = [](const std::string &s) {
            // base64url encode
            static const char *chars =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string out;
            int val = 0, valb = -6;
            for (unsigned char c : s) {
                val = (val << 8) + c;
                valb += 8;
                while (valb >= 0) {
                    out.push_back(chars[(val >> valb) & 0x3F]);
                    valb -= 6;
                }
            }
            if (valb > -6) out.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
            while (out.size() % 4) out.push_back('=');
            for (auto &c : out) {
                if (c == '+') c = '-';
                if (c == '/') c = '_';
            }
            out.erase(remove(out.begin(), out.end(), '='), out.end());
            return out;
        };

        long exp = time(nullptr) + 86400; // 24h
        std::string header = b64url("{\"alg\":\"HS256\",\"typ\":\"JWT\"}");
        std::string payload = b64url("{\"sub\":\"" + username + 
                                     "\",\"role\":\"" + role + 
                                     "\",\"exp\":" + std::to_string(exp) + "}");
        std::string sigInput = header + "." + payload;

        // HMAC-SHA256
        unsigned char *sig = HMAC(EVP_sha256(),
            secret.c_str(), secret.size(),
            (unsigned char*)sigInput.c_str(), sigInput.size(),
            nullptr, nullptr);
        std::string sigStr(sig, sig + 32);
        std::string signature = b64url(sigStr);

        return sigInput + "." + signature;
    }

    static bool verifyToken(const std::string &token,
                            const std::string &secret,
                            std::string &outUsername,
                            std::string &outRole) {
        // Split token
        auto p1 = token.find('.');
        auto p2 = token.rfind('.');
        if (p1 == std::string::npos || p1 == p2) return false;

        std::string sigInput = token.substr(0, p2);
        std::string signature = token.substr(p2 + 1);

        // Verify signature
        unsigned char *sig = HMAC(EVP_sha256(),
            secret.c_str(), secret.size(),
            (unsigned char*)sigInput.c_str(), sigInput.size(),
            nullptr, nullptr);

        auto b64url = [](const std::string &s) {
            static const char *chars =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string out;
            int val = 0, valb = -6;
            for (unsigned char c : s) {
                val = (val << 8) + c;
                valb += 8;
                while (valb >= 0) {
                    out.push_back(chars[(val >> valb) & 0x3F]);
                    valb -= 6;
                }
            }
            if (valb > -6) out.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
            while (out.size() % 4) out.push_back('=');
            for (auto &c : out) {
                if (c == '+') c = '-';
                if (c == '/') c = '_';
            }
            out.erase(remove(out.begin(), out.end(), '='), out.end());
            return out;
        };

        std::string sigStr(sig, sig + 32);
        if (b64url(sigStr) != signature) return false;

        // Decode payload
        std::string payloadB64 = token.substr(p1 + 1, p2 - p1 - 1);
        // Add padding
        while (payloadB64.size() % 4) payloadB64 += '=';
        for (auto &c : payloadB64) {
            if (c == '-') c = '+';
            if (c == '_') c = '/';
        }
        // Base64 decode
        static const int T[256] = {
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
            -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
            -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        };
        std::string decoded;
        int val = 0, valb = -8;
        for (unsigned char c : payloadB64) {
            if (T[c] == -1) break;
            val = (val << 6) + T[c];
            valb += 6;
            if (valb >= 0) {
                decoded.push_back((val >> valb) & 0xFF);
                valb -= 8;
            }
        }

        // Extract sub and role from JSON string
        auto extract = [&](const std::string &key) {
            std::string k = "\"" + key + "\":\"";
            auto pos = decoded.find(k);
            if (pos == std::string::npos) return std::string{};
            pos += k.size();
            auto end = decoded.find('"', pos);
            return decoded.substr(pos, end - pos);
        };

        auto extractNum = [&](const std::string &key) -> long {
            std::string k = "\"" + key + "\":";
            auto pos = decoded.find(k);
            if (pos == std::string::npos) return 0;
            pos += k.size();
            return std::stol(decoded.substr(pos));
        };

        long exp = extractNum("exp");
        if (exp < time(nullptr)) return false; // expired

        outUsername = extract("sub");
        outRole = extract("role");
        return !outUsername.empty();
    }
};