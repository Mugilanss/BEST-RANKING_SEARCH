#define CPPHTTPLIB_IMPLEMENTATION
#include "../httplib.h"

#include "server.h"
#include "routes.h"
#include "engine_context.h"
#include "../logger.h"

#include <iostream>
#include <string>

void APIServer::run(const std::string &configPath,
                   const std::string &host,
                   int port)
{
    EngineContext ctx;
    ctx.init(configPath);

    httplib::Server srv;

    srv.set_error_handler([](const httplib::Request &, httplib::Response &res) {
        if (res.status == 404)
            res.set_content("{\"error\":\"not found\"}", "application/json");
        else
            res.set_content("{\"error\":\"internal server error\"}", "application/json");
    });

    srv.set_logger([](const httplib::Request &req, const httplib::Response &res) {
        Logger::instance().log(LINFO,
            req.method + " " + req.path +
            " -> " + std::to_string(res.status));
    });

    register_routes(srv, ctx);

    std::cout << "Search API listening on " << host << ":" << port << "\n";

    if (!srv.listen(host.c_str(), port)) {
        Logger::instance().log(LERROR, "Failed to bind " + host + ":" + std::to_string(port));
        std::cerr << "Failed to bind " << host << ":" << port << "\n";
    }

    ctx.metrics.stopPeriodicReport();
}