#pragma once

#include "engine_context.h"

// Forward-declare httplib Server to avoid pulling the full header here
namespace httplib { class Server; }

// Registers all REST endpoints on the server.
// Called once from server.cpp before srv.listen().
void register_routes(httplib::Server &srv, EngineContext &ctx);
