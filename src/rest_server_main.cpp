#include "api/rest_server.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char* argv[]) {
    kv_engine::KVEngineOptions engine_options;
    engine_options.enable_wal = true;
    engine_options.wal_options.wal_dir = "./wal_data";
    engine_options.wal_options.sync_on_write = true;

    kv_engine::RestServerOptions server_options;
    if (argc >= 2) {
        try {
            const unsigned long port = std::stoul(argv[1]);
            if (port == 0 || port > 65535) throw std::out_of_range("port");
            server_options.port = static_cast<uint16_t>(port);
        } catch (...) {
            std::cerr << "Usage: kv_rest_server [port] [host]\n";
            return 1;
        }
    }
    if (argc >= 3) server_options.host = argv[2];
    if (argc > 3) {
        std::cerr << "Usage: kv_rest_server [port] [host]\n";
        return 1;
    }

    kv_engine::KVEngine engine(engine_options);
    kv_engine::RestServer server(engine, server_options);
    const kv_engine::Status status = server.Start();
    if (!status.ok()) {
        std::cerr << "Failed to start REST server: " << status.ToString() << '\n';
        return 1;
    }

    std::cout << "KV Engine REST API\n"
              << "Listening on http://" << server_options.host << ':' << server_options.port << "\n"
              << "Endpoints:\n"
              << "  GET    /health\n"
              << "  GET    /kv/{key}\n"
              << "  PUT    /kv/{key}\n"
              << "  DELETE /kv/{key}\n"
              << "  GET    /stats\n"
              << "  POST   /flush\n"
              << "  POST   /sync\n"
              << "Press Ctrl+C to stop.\n";

    // Keep the process alive. The server owns the accept loop in a background thread.
    while (server.IsRunning()) {
#ifdef _WIN32
        Sleep(1000);
#else
        std::this_thread::sleep_for(std::chrono::seconds(1));
#endif
    }
    return 0;
}
