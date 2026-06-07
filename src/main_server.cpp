#include "api/server.h"
#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>

int main(int argc, char **argv) {
    std::string configPath = "src/config.ini";
    std::string host       = "0.0.0.0";

    // Read PORT from environment first (Render sets this)
    int port = 10000;
    const char* port_env = std::getenv("PORT");
    if (port_env) port = std::stoi(port_env);

    // CLI args can still override
    if (argc >= 2) port       = std::stoi(argv[1]);
    if (argc >= 3) host       = argv[2];
    if (argc >= 4) configPath = argv[3];

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