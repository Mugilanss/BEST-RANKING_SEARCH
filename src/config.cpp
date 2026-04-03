#include "config.h"
#include <fstream>
#include <algorithm>
#include <cctype>

static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool Config::parseBool(const std::string &val) {
    std::string v = val;
    std::transform(v.begin(), v.end(), v.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    return (v == "1" || v == "true" || v == "yes" || v == "on");
}

bool Config::parseInt(const std::string &val, int &out) {
    try {
        size_t idx;
        int parsed = std::stoi(val, &idx);
        if (idx != val.size()) return false;
        out = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool Config::loadFromFile(const std::string &path) {
    std::ifstream ifs(path);
    if (!ifs) return false;

    std::string line;
    while (std::getline(ifs, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        int tmpInt;

        if (key == "docsfolder") docsFolder = val;
        else if (key == "indexfile") indexFile = val;
        else if (key == "walfile") walFile = val;
        else if (key == "logfile") logFile = val;
        else if (key == "extfilter") extFilter = val;

        else if (key == "maxthreads" && parseInt(val, tmpInt))
            maxThreads = tmpInt;

        else if (key == "shardcount" && parseInt(val, tmpInt))
            shardCount = tmpInt;

        else if (key == "indexbatchsize" && parseInt(val, tmpInt))
            indexBatchSize = tmpInt;

        else if (key == "maxdocs" && parseInt(val, tmpInt))
            maxDocs = tmpInt;

        else if (key == "usestemming")
            useStemming = parseBool(val);

        else if (key == "usestopwords")
            useStopwords = parseBool(val);

        else if (key == "usewal")
            useWAL = parseBool(val);

        else if (key == "scoring")
            scoring = val;
    }

    return true;
}