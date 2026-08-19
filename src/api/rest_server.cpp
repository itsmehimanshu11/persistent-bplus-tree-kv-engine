#include "api/rest_server.h"
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <sstream>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_length_t = int;
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_length_t = socklen_t;
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
#endif

namespace kv_engine {
namespace {

#ifdef _WIN32
class WinsockGuard {
public:
    WinsockGuard() {
        WSADATA data{};
        initialized_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }

    ~WinsockGuard() {
        if (initialized_) {
            WSACleanup();
        }
    }

    bool ok() const {
        return initialized_;
    }

private:
    bool initialized_ = false;
};
#endif

int CloseSocket(socket_t socket) {
#ifdef _WIN32
    return closesocket(socket);
#else
    return close(socket);
#endif
}

bool SendAll(socket_t socket, const char* data, size_t size) {
    size_t sent = 0;

    while (sent < size) {
#ifdef _WIN32
        const int n = send(
            socket,
            data + sent,
            static_cast<int>(size - sent),
            0
        );
#else
        const ssize_t n = send(
            socket,
            data + sent,
            size - sent,
            0
        );
#endif

        if (n <= 0) {
            return false;
        }

        sent += static_cast<size_t>(n);
    }

    return true;
}

std::string JsonEscape(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 16);

    static const char* hex = "0123456789abcdef";

    for (unsigned char ch : value) {
        switch (ch) {
            case '"':
                out += "\\\"";
                break;

            case '\\':
                out += "\\\\";
                break;

            case '\b':
                out += "\\b";
                break;

            case '\f':
                out += "\\f";
                break;

            case '\n':
                out += "\\n";
                break;

            case '\r':
                out += "\\r";
                break;

            case '\t':
                out += "\\t";
                break;

            default:
                if (ch < 0x20) {
                    out += "\\u00";
                    out += hex[(ch >> 4) & 0x0f];
                    out += hex[ch & 0x0f];
                } else {
                    out.push_back(static_cast<char>(ch));
                }
                break;
        }
    }

    return out;
}

int StatusHttpCode(const Status& status) {
    switch (status.code()) {
        case ErrorCode::OK:
            return 200;

        case ErrorCode::NOT_FOUND:
            return 404;

        case ErrorCode::INVALID_ARGUMENT:
            return 400;

        case ErrorCode::ALREADY_EXISTS:
            return 409;

        case ErrorCode::BUSY:
            return 409;

        case ErrorCode::IO_ERROR:
            return 500;

        case ErrorCode::CORRUPTION:
            return 500;

        case ErrorCode::INTERNAL_ERROR:
            return 500;

        default:
            return 500;
    }
}

std::string ReasonPhrase(int code) {
    switch (code) {
        case 200:
            return "OK";

        case 400:
            return "Bad Request";

        case 404:
            return "Not Found";

        case 405:
            return "Method Not Allowed";

        case 409:
            return "Conflict";

        case 413:
            return "Payload Too Large";

        case 500:
            return "Internal Server Error";

        case 501:
            return "Not Implemented";

        default:
            return "Error";
    }
}

std::string ErrorJson(const std::string& message) {
    return "{\"error\":\"" + JsonEscape(message) + "\"}";
}

std::string DecodePathComponent(std::string_view input, bool* ok) {
    std::string out;
    out.reserve(input.size());

    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] != '%') {
            if (input[i] == '?') {
                *ok = false;
                return {};
            }

            out.push_back(input[i]);
            continue;
        }

        if (i + 2 >= input.size()) {
            *ok = false;
            return {};
        }

        auto hex_value = [](char c) -> int {
            if (c >= '0' && c <= '9') {
                return c - '0';
            }

            if (c >= 'a' && c <= 'f') {
                return c - 'a' + 10;
            }

            if (c >= 'A' && c <= 'F') {
                return c - 'A' + 10;
            }

            return -1;
        };

        const int hi = hex_value(input[i + 1]);
        const int lo = hex_value(input[i + 2]);

        if (hi < 0 || lo < 0) {
            *ok = false;
            return {};
        }

        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
    }

    *ok = true;
    return out;
}

struct HttpRequest {
    std::string method;
    std::string target;
    std::string body;
};

bool ParseContentLength(
    std::string_view headers,
    size_t* content_length
) {
    size_t pos = 0;

    while (pos < headers.size()) {
        size_t end = headers.find("\r\n", pos);

        if (end == std::string_view::npos) {
            end = headers.size();
        }

        std::string_view line =
            headers.substr(pos, end - pos);

        const size_t colon = line.find(':');

        if (colon != std::string_view::npos) {
            std::string name(line.substr(0, colon));

            std::transform(
                name.begin(),
                name.end(),
                name.begin(),
                [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                }
            );

            if (name == "content-length") {
                std::string value(line.substr(colon + 1));

                size_t first =
                    value.find_first_not_of(" \t");

                if (first == std::string::npos) {
                    return false;
                }

                size_t last =
                    value.find_last_not_of(" \t");

                value =
                    value.substr(first, last - first + 1);

                try {
                    size_t consumed = 0;

                    *content_length =
                        std::stoull(value, &consumed);

                    return consumed == value.size();
                } catch (...) {
                    return false;
                }
            }
        }

        if (end == headers.size()) {
            break;
        }

        pos = end + 2;
    }

    *content_length = 0;
    return true;
}

bool ReceiveRequest(
    socket_t socket,
    size_t max_body_size,
    HttpRequest* request,
    int* error_code
) {
    constexpr size_t kMaxHeaderSize = 64 * 1024;

    std::string buffer;
    buffer.reserve(4096);

    char chunk[4096];

    size_t header_end = std::string::npos;

    while (buffer.size() < kMaxHeaderSize) {
#ifdef _WIN32
        const int n = recv(
            socket,
            chunk,
            static_cast<int>(sizeof(chunk)),
            0
        );
#else
        const ssize_t n = recv(
            socket,
            chunk,
            sizeof(chunk),
            0
        );
#endif

        if (n <= 0) {
            return false;
        }

        buffer.append(
            chunk,
            static_cast<size_t>(n)
        );

        header_end =
            buffer.find("\r\n\r\n");

        if (header_end != std::string::npos) {
            break;
        }
    }

    if (header_end == std::string::npos) {
        *error_code = 400;
        return false;
    }

    const size_t request_line_end =
        buffer.find("\r\n");

    if (
        request_line_end == std::string::npos ||
        request_line_end > header_end
    ) {
        *error_code = 400;
        return false;
    }

    std::istringstream request_line(
        buffer.substr(0, request_line_end)
    );

    std::string version;

    request_line
        >> request->method
        >> request->target
        >> version;

    if (
        request->method.empty() ||
        request->target.empty() ||
        version != "HTTP/1.1"
    ) {
        *error_code = 400;
        return false;
    }

    const size_t header_start =
        request_line_end + 2;

    const size_t header_length =
        header_end - header_start;

    const std::string_view headers(
        buffer.data() + header_start,
        header_length
    );

    size_t content_length = 0;

    if (!ParseContentLength(
            headers,
            &content_length
        )) {
        *error_code = 400;
        return false;
    }

    if (content_length > max_body_size) {
        *error_code = 413;
        return false;
    }

    const size_t body_start =
        header_end + 4;

    while (
        buffer.size() - body_start <
        content_length
    ) {
#ifdef _WIN32
        const int n = recv(
            socket,
            chunk,
            static_cast<int>(sizeof(chunk)),
            0
        );
#else
        const ssize_t n = recv(
            socket,
            chunk,
            sizeof(chunk),
            0
        );
#endif

        if (n <= 0) {
            *error_code = 400;
            return false;
        }

        buffer.append(
            chunk,
            static_cast<size_t>(n)
        );
    }

    request->body.assign(
        buffer.data() + body_start,
        content_length
    );

    return true;
}

std::string BuildResponse(
    int code,
    const std::string& body,
    const std::string& content_type = "application/json"
) {
    std::ostringstream response;

    response
        << "HTTP/1.1 "
        << code
        << ' '
        << ReasonPhrase(code)
        << "\r\n"
        << "Content-Type: "
        << content_type
        << "\r\n"
        << "Content-Length: "
        << body.size()
        << "\r\n"
        << "Connection: close\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Access-Control-Allow-Methods: GET, PUT, DELETE, POST, OPTIONS\r\n"
        << "Access-Control-Allow-Headers: Content-Type\r\n"
        << "\r\n"
        << body;

    return response.str();
}

std::string StatsJson(const KVEngine& engine) {
    const auto wal = engine.GetWalStats();

    std::ostringstream body;

    body
        << "{"
        << "\"keys\":"
        << engine.Size()
        << ','
        << "\"nodes\":"
        << engine.NodeCount()
        << ','
        << "\"height\":"
        << engine.Height()
        << ','
        << "\"memory_bytes\":"
        << engine.MemoryUsage()
        << ','
        << "\"wal\":{"
        << "\"total_writes\":"
        << wal.total_writes
        << ','
        << "\"total_bytes_written\":"
        << wal.total_bytes_written
        << ','
        << "\"total_syncs\":"
        << wal.total_syncs
        << ','
        << "\"current_file_number\":"
        << wal.current_file_number
        << ','
        << "\"current_file_size\":"
        << wal.current_file_size
        << "}"
        << "}";

    return body.str();
}

/*
 * REST range scan.
 *
 * Example:
 *
 * GET /scan?start=user:1001&limit=user:1004
 *
 * The range is [start, limit):
 *
 * user:1001 -> included
 * user:1002 -> included
 * user:1003 -> included
 * user:1004 -> excluded
 */
std::string RangeScanJson(
    KVEngine& engine,
    const std::string& start,
    const std::string& limit,
    int* status_code
) {
    Slice start_slice(start);
    Slice limit_slice(limit);

   std::unique_ptr<BPlusTree::Iterator> iterator(
        engine.NewIterator(
            start_slice,
            limit_slice
        )
    );

    std::ostringstream body;

    body << "{\"items\":[";

    bool first = true;
    size_t count = 0;

    while (iterator->Valid()) {
        if (!first) {
            body << ",";
        }

        body
            << "{\"key\":\""
            << JsonEscape(
                iterator->key().ToString()
            )
            << "\",\"value\":\""
            << JsonEscape(
                iterator->value().ToString()
            )
            << "\"}";

        first = false;
        ++count;

        iterator->Next();
    }

    const Status iterator_status =
        iterator->status();

    if (!iterator_status.ok()) {
        *status_code =
            StatusHttpCode(iterator_status);

        return ErrorJson(
            iterator_status.ToString()
        );
    }

    body
        << "],"
        << "\"count\":"
        << count
        << "}";

    *status_code = 200;

    return body.str();
}

/*
 * Parse query parameters from:
 *
 * /scan?start=...&limit=...
 *
 * Returns true when parsing succeeds.
 */
bool ParseRangeQuery(
    const std::string& target,
    std::string* start,
    std::string* limit,
    std::string* error
) {
    constexpr std::string_view prefix = "/scan?";

    if (
        target.rfind(prefix.data(), 0) != 0
    ) {
        *error = "invalid scan endpoint";
        return false;
    }

    const std::string query =
        target.substr(prefix.size());

    std::stringstream query_stream(query);

    std::string parameter;

    while (
        std::getline(
            query_stream,
            parameter,
            '&'
        )
    ) {
        if (parameter.empty()) {
            continue;
        }

        const size_t equals =
            parameter.find('=');

        if (equals == std::string::npos) {
            *error =
                "invalid query parameter";

            return false;
        }

        const std::string name =
            parameter.substr(0, equals);

        const std::string encoded_value =
            parameter.substr(equals + 1);

        bool decode_ok = false;

        const std::string value =
            DecodePathComponent(
                encoded_value,
                &decode_ok
            );

        if (!decode_ok) {
            *error =
                "invalid query parameter encoding";

            return false;
        }

        if (name == "start") {
            *start = value;
        } else if (name == "limit") {
            *limit = value;
        }
    }

    if (start->empty()) {
        *error = "start parameter is required";
        return false;
    }

    if (limit->empty()) {
        *error = "limit parameter is required";
        return false;
    }

    if (*start >= *limit) {
        *error =
            "start must be less than limit";

        return false;
    }

    return true;
}

std::string HandleRequest(
    KVEngine& engine,
    const HttpRequest& request,
    int* status_code
) {
    if (request.method == "OPTIONS") {
        *status_code = 200;
        return "{}";
    }

    if (
        request.target == "/health" &&
        request.method == "GET"
    ) {
        *status_code = 200;
        return "{\"status\":\"ok\"}";
    }

    if (
        request.target == "/stats" &&
        request.method == "GET"
    ) {
        *status_code = 200;
        return StatsJson(engine);
    }

    if (
        request.target == "/flush" &&
        request.method == "POST"
    ) {
        const Status status =
            engine.Flush();

        *status_code =
            StatusHttpCode(status);

        return status.ok()
            ? "{\"status\":\"ok\"}"
            : ErrorJson(status.ToString());
    }

    if (
        request.target == "/sync" &&
        request.method == "POST"
    ) {
        const Status status =
            engine.Sync();

        *status_code =
            StatusHttpCode(status);

        return status.ok()
            ? "{\"status\":\"ok\"}"
            : ErrorJson(status.ToString());
    }

    /*
     * Range scan endpoint.
     *
     * Example:
     *
     * GET /scan?start=user%3A1001&limit=user%3A1004
     */
    if (
        request.method == "GET" &&
        request.target.rfind(
            "/scan?",
            0
        ) == 0
    ) {
        std::string start;
        std::string limit;
        std::string error;

        if (!ParseRangeQuery(
                request.target,
                &start,
                &limit,
                &error
            )) {
            *status_code = 400;
            return ErrorJson(error);
        }

        return RangeScanJson(
            engine,
            start,
            limit,
            status_code
        );
    }

    constexpr std::string_view prefix =
        "/kv/";

    if (
        request.target.rfind(
            prefix.data(),
            0
        ) == 0 &&
        request.target.size() >
            prefix.size()
    ) {
        bool decode_ok = false;

        const std::string key =
            DecodePathComponent(
                std::string_view(request.target)
                    .substr(prefix.size()),
                &decode_ok
            );

        if (!decode_ok || key.empty()) {
            *status_code = 400;
            return ErrorJson(
                "invalid key encoding"
            );
        }

        if (request.method == "GET") {
            std::string value;

            const Status status =
                engine.Get(
                    Slice(key),
                    &value
                );

            if (!status.ok()) {
                *status_code =
                    StatusHttpCode(status);

                return ErrorJson(
                    status.ToString()
                );
            }

            *status_code = 200;

            return
                "{\"key\":\"" +
                JsonEscape(key) +
                "\",\"value\":\"" +
                JsonEscape(value) +
                "\"}";
        }

        if (request.method == "PUT") {
            const Status status =
                engine.Put(
                    Slice(key),
                    Slice(request.body)
                );

            *status_code =
                StatusHttpCode(status);

            if (!status.ok()) {
                return ErrorJson(
                    status.ToString()
                );
            }

            return
                "{\"status\":\"ok\",\"key\":\"" +
                JsonEscape(key) +
                "\"}";
        }

        if (request.method == "DELETE") {
            const Status status =
                engine.Delete(
                    Slice(key)
                );

            *status_code =
                StatusHttpCode(status);

            if (!status.ok()) {
                return ErrorJson(
                    status.ToString()
                );
            }

            return
                "{\"status\":\"ok\",\"key\":\"" +
                JsonEscape(key) +
                "\"}";
        }

        *status_code = 405;

        return ErrorJson(
            "method not allowed"
        );
    }

    *status_code = 404;

    return ErrorJson(
        "endpoint not found"
    );
}

} // namespace

RestServer::RestServer(
    KVEngine& engine,
    const RestServerOptions& options
)
    : engine_(engine),
      options_(options) {}

RestServer::~RestServer() {
    Stop();
}

Status RestServer::Start() {
    if (running_.load()) {
        return Status::AlreadyExists(
            "REST server is already running"
        );
    }

    if (options_.port == 0) {
        return Status::InvalidArgument(
            "port must be between 1 and 65535"
        );
    }

#ifdef _WIN32
    static WinsockGuard winsock;

    if (!winsock.ok()) {
        return Status::IOError(
            "WSAStartup failed"
        );
    }
#endif

    const socket_t socket_fd =
        socket(
            AF_INET,
            SOCK_STREAM,
            IPPROTO_TCP
        );

    if (socket_fd == kInvalidSocket) {
        return Status::IOError(
            "failed to create server socket"
        );
    }

    int reuse = 1;

    setsockopt(
        socket_fd,
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse),
        sizeof(reuse)
    );

    sockaddr_in address{};

    address.sin_family = AF_INET;
    address.sin_port =
        htons(options_.port);

    if (
        inet_pton(
            AF_INET,
            options_.host.c_str(),
            &address.sin_addr
        ) != 1
    ) {
        CloseSocket(socket_fd);

        return Status::InvalidArgument(
            "host must be an IPv4 address"
        );
    }

    if (
        bind(
            socket_fd,
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)
        ) != 0
    ) {
        CloseSocket(socket_fd);

        return Status::IOError(
            "failed to bind " +
            options_.host +
            ":" +
            std::to_string(options_.port)
        );
    }

    if (
        listen(
            socket_fd,
            64
        ) != 0
    ) {
        CloseSocket(socket_fd);

        return Status::IOError(
            "failed to listen on server socket"
        );
    }

    server_socket_ =
        static_cast<int>(socket_fd);

    running_.store(true);

    accept_thread_ =
        std::thread(
            &RestServer::AcceptLoop,
            this
        );

    return Status::OK();
}

void RestServer::Stop() {
    if (!running_.exchange(false)) {
        return;
    }

    const socket_t socket_fd =
        static_cast<socket_t>(
            server_socket_
        );

    if (socket_fd != kInvalidSocket) {
        CloseSocket(socket_fd);
        server_socket_ = -1;
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
}

void RestServer::AcceptLoop() {
    while (running_.load()) {
        sockaddr_in client_address{};

        socket_length_t client_length =
            sizeof(client_address);

        const socket_t client_socket =
            accept(
                static_cast<socket_t>(
                    server_socket_
                ),
                reinterpret_cast<sockaddr*>(
                    &client_address
                ),
                &client_length
            );

        if (
            client_socket ==
            kInvalidSocket
        ) {
            if (running_.load()) {
                continue;
            }

            break;
        }

        std::thread(
            &RestServer::HandleClient,
            this,
            static_cast<int>(
                client_socket
            )
        ).detach();
    }
}

void RestServer::HandleClient(
    int client_socket
) {
    const socket_t socket_fd =
        static_cast<socket_t>(
            client_socket
        );

    HttpRequest request;

    int parse_error = 400;

    if (
        !ReceiveRequest(
            socket_fd,
            options_.max_body_size,
            &request,
            &parse_error
        )
    ) {
        const std::string body =
            ErrorJson(
                parse_error == 413
                    ? "request body too large"
                    : "malformed HTTP request"
            );

        const std::string response =
            BuildResponse(
                parse_error,
                body
            );

        SendAll(
            socket_fd,
            response.data(),
            response.size()
        );

        CloseSocket(socket_fd);

        return;
    }

    int status_code = 500;

    const std::string body =
        HandleRequest(
            engine_,
            request,
            &status_code
        );

    const std::string response =
        BuildResponse(
            status_code,
            body
        );

    SendAll(
        socket_fd,
        response.data(),
        response.size()
    );

    CloseSocket(socket_fd);
}

} // namespace kv_engine