// CPPHTTPLIB_IMPLEMENTATION must be defined in exactly one .cpp file.
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

    // Global error handler
    srv.set_error_handler([](const httplib::Request &, httplib::Response &res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        if (res.status == 404)
            res.set_content("{\"error\":\"not found\"}", "application/json");
        else
            res.set_content("{\"error\":\"internal server error\"}", "application/json");
    });

    // Request logger
    srv.set_logger([](const httplib::Request &req, const httplib::Response &res) {
        Logger::instance().log(LINFO,
            req.method + " " + req.path +
            " -> " + std::to_string(res.status));
    });

    register_routes(srv, ctx);

    std::cout << "Search API listening on " << host << ":" << port << "\n";
    std::cout << "Endpoints:\n"
              << "  GET  /search?q=<query>&k=<n>&sort=<score|date|size>\n"
              << "  GET  /document/<id>\n"
              << "  GET  /stats\n"
              << "  GET  /autocomplete?q=<prefix>&k=<n>\n"
              << "  POST /index/rebuild\n"
              << "  POST /index/update\n";

    if (!srv.listen(host.c_str(), port)) {
        Logger::instance().log(LERROR, "Failed to bind " + host + ":" + std::to_string(port));
        std::cerr << "Failed to bind " << host << ":" << port << "\n";
    }

    ctx.metrics.stopPeriodicReport();
}
