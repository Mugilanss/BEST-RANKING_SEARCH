#include "util.h"
#include <fstream>
#include <sstream>
#include <array>
#include <cstdio>
#include <algorithm>

std::string readTextFile(const std::string &path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return "";

    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

std::string execCommandCapture(const std::string &cmd) {
    std::array<char, 128> buffer;
    std::string result;

#ifdef _WIN32
    FILE *pipe = _popen(cmd.c_str(), "r");
#else
    FILE *pipe = popen(cmd.c_str(), "r");
#endif

    if (!pipe) return "";

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    return result;
}

std::string htmlEscape(const std::string &s) {
    std::string out;
    out.reserve(static_cast<size_t>(s.size() * 1.2));

    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += c;
        }
    }
    return out;
}

bool endsWith(const std::string &s, const std::string &suffix) {
    if (s.size() < suffix.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
}