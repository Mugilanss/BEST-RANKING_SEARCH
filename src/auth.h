#pragma once
#include <string>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>

class Auth
{
public:
    static std::string b64urlEncode(const std::string &s)
    {
        static const char *chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        int val = 0, valb = -6;
        for (unsigned char c : s)
        {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0)
            {
                out.push_back(chars[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }
        if (valb > -6)
            out.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
        while (out.size() % 4)
            out.push_back('=');
        for (auto &c : out)
        {
            if (c == '+') c = '-';
            if (c == '/') c = '_';
        }
        out.erase(std::remove(out.begin(), out.end(), '='), out.end());
        return out;
    }

    static std::string b64urlDecode(const std::string &input)
    {
        std::string s = input;
        for (auto &c : s)
        {
            if (c == '-') c = '+';
            if (c == '_') c = '/';
        }
        while (s.size() % 4)
            s += '=';

        std::string out;
        int val = 0, valb = -8;
        for (unsigned char c : s)
        {
            if (c == '=') break;
            int v = -1;
            if (c >= 'A' && c <= 'Z') v = c - 'A';
            else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
            else if (c >= '0' && c <= '9') v = c - '0' + 52;
            else if (c == '+') v = 62;
            else if (c == '/') v = 63;
            if (v == -1) break;
            val = (val << 6) + v;
            valb += 6;
            if (valb >= 0)
            {
                out.push_back((char)((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return out;
    }

    static std::string hashPassword(const std::string &password)
    {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256((unsigned char *)password.c_str(), password.size(), hash);
        std::ostringstream oss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
            oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        return oss.str();
    }

    static std::string generateToken(const std::string &username,
                                     const std::string &role,
                                     const std::string &secret)
    {
        long exp = time(nullptr) + 86400;
        std::string header  = b64urlEncode("{\"alg\":\"HS256\",\"typ\":\"JWT\"}");
        std::string payload = b64urlEncode("{\"sub\":\"" + username +
                                           "\",\"role\":\"" + role +
                                           "\",\"exp\":" + std::to_string(exp) + "}");
        std::string sigInput = header + "." + payload;

        unsigned char *sig = HMAC(EVP_sha256(),
                                  secret.c_str(), secret.size(),
                                  (unsigned char *)sigInput.c_str(), sigInput.size(),
                                  nullptr, nullptr);
        std::string sigStr(sig, sig + 32);
        std::string signature = b64urlEncode(sigStr);

        return sigInput + "." + signature;
    }

    static bool verifyToken(const std::string &token,
                            const std::string &secret,
                            std::string &outUsername,
                            std::string &outRole)
    {
        auto p1 = token.find('.');
        auto p2 = token.rfind('.');
        if (p1 == std::string::npos || p1 == p2)
            return false;

        std::string sigInput  = token.substr(0, p2);
        std::string signature = token.substr(p2 + 1);

        unsigned char *sig = HMAC(EVP_sha256(),
                                  secret.c_str(), secret.size(),
                                  (unsigned char *)sigInput.c_str(), sigInput.size(),
                                  nullptr, nullptr);
        std::string sigStr(sig, sig + 32);
        std::string expectedSig = b64urlEncode(sigStr);
        if (expectedSig != signature)
            return false;

        std::string payloadB64 = token.substr(p1 + 1, p2 - p1 - 1);
        std::string decoded    = b64urlDecode(payloadB64);

        auto extract = [&](const std::string &key)
        {
            std::string k = "\"" + key + "\":\"";
            auto pos = decoded.find(k);
            if (pos == std::string::npos) return std::string{};
            pos += k.size();
            auto end = decoded.find('"', pos);
            return decoded.substr(pos, end - pos);
        };

        auto extractNum = [&](const std::string &key) -> long
        {
            std::string k = "\"" + key + "\":";
            auto pos = decoded.find(k);
            if (pos == std::string::npos) return 0;
            pos += k.size();
            return std::stol(decoded.substr(pos));
        };

        long exp = extractNum("exp");
        if (exp < (long)time(nullptr))
            return false;

        outUsername = extract("sub");
        outRole     = extract("role");
        return !outUsername.empty();
    }
};