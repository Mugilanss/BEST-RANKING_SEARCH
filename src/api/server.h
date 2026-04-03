#pragma once

#include "engine_context.h"

class APIServer {
public:
    // Initializes engine from configPath, then listens on host:port.
    // Blocks until the server is stopped.
    void run(const std::string &configPath, const std::string &host, int port);
};
