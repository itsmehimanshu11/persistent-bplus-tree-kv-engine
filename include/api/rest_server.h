#pragma once

#include "kv_engine.h"
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace kv_engine {

struct RestServerOptions {
    std::string host = "127.0.0.1";
    uint16_t port = 8080;
    size_t max_body_size = 1024 * 1024;
};

// Small dependency-free HTTP/1.1 REST adapter for KVEngine.
// The storage engine remains independent of HTTP and can still be used directly.
class RestServer {
public:
    RestServer(KVEngine& engine, const RestServerOptions& options = RestServerOptions());
    ~RestServer();

    RestServer(const RestServer&) = delete;
    RestServer& operator=(const RestServer&) = delete;

    Status Start();
    void Stop();
    bool IsRunning() const { return running_.load(); }

private:
    void AcceptLoop();
    void HandleClient(int client_socket);

    KVEngine& engine_;
    RestServerOptions options_;
    std::atomic<bool> running_{false};
    int server_socket_ = -1;
    std::thread accept_thread_;
};

} // namespace kv_engine
