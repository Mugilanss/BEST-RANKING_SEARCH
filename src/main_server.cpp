#include "api/server.h"
#include <iostream>
#include <fstream>
#include <string>

int main(int argc, char **argv) {
    std::string configPath = "src/config.ini";  // default: relative to cwd
    std::string host       = "0.0.0.0";
    int         port       = 8080;

    // Allow overrides: search_server [port] [host] [configPath]
    if (argc >= 2) port       = std::stoi(argv[1]);
    if (argc >= 3) host       = argv[2];
    if (argc >= 4) configPath = argv[3];

    // If running from build/bin/, walk up to find src/config.ini
    {
        std::ifstream test(configPath);
        if (!test.good()) {
            for (auto &candidate : {"../../src/config.ini", "../src/config.ini", "src/config.ini"}) {
                std::ifstream t2(candidate);
                if (t2.good()) { configPath = candidate; break; }
            }
        }
    }

    APIServer server;
    server.run(configPath, host, port);
    return 0;
}
