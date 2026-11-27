#include "mcp.hpp"
#include "core.hpp"

#include <microhttpd.h>
#include <openssl/crypto.h>

#include <arpa/inet.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <strings.h>
#include <thread>

namespace {
using namespace std::chrono_literals;
using std::string_view;
using Clock = std::chrono::steady_clock;
volatile std::sig_atomic_t stopping = 0;

void stop(int) { stopping = 1; }

struct Settings {
    std::filesystem::path directory;
    std::string host, origin, bearer;
    double tokens = 24;
    Clock::time_point checked = Clock::now();

    bool take_request_slot() {
        const auto now = Clock::now();
        tokens = std::min(24.0, tokens + 12 * std::chrono::duration<double>(now - checked).count());
        checked = now;
        if (tokens < 1) return false;
        tokens -= 1;
        return true;
    }
};

struct Request {
    std::string body;
    Clock::time_point started = Clock::now();
};

struct Headers {
    const char *host = nullptr, *origin = nullptr, *auth = nullptr, *version = nullptr;
    const char *type = nullptr, *accept = nullptr, *length = nullptr;
    bool duplicate = false;
};

MHD_Result collect_headers(void* context, MHD_ValueKind, const char* key, const char* value) {
    auto& headers = *static_cast<Headers*>(context);
    const auto capture = [&](const char* name, const char*& dest) {
        if (strcasecmp(key, name) != 0) return;
        if (dest) headers.duplicate = true;
        dest = value;
    };
    capture("Host", headers.host);
    capture("Origin", headers.origin);
    capture("Authorization", headers.auth);
    capture("MCP-Protocol-Version", headers.version);
    capture("Content-Type", headers.type);
    capture("Accept", headers.accept);
    capture("Content-Length", headers.length);
    return MHD_YES;
}

string_view trim(string_view value) {
    const auto start = value.find_first_not_of(" \t");
    return start == string_view::npos ? string_view{} : value.substr(start, value.find_last_not_of(" \t") - start + 1);
}

bool accepts(string_view values, string_view media) {
    while (!values.empty()) {
        const auto comma = values.find(',');
        const auto item = values.substr(0, comma);
        if (trim(item) == media) return true;
        if (comma == string_view::npos) break;
        values.remove_prefix(comma + 1);
    }
    return false;
}

MHD_Result send(MHD_Connection* connection, unsigned status, const std::string& body = {},
                const char* header = nullptr, const char* value = nullptr) {
    auto* response = MHD_create_response_from_buffer(body.size(), const_cast<char*>(body.data()), MHD_RESPMEM_MUST_COPY);
    if (!response) return MHD_NO;
    MHD_add_response_header(response, "Cache-Control", "no-store");
    if (!body.empty()) MHD_add_response_header(response, "Content-Type", "application/json");
    if (header) MHD_add_response_header(response, header, value);
    const auto result = MHD_queue_response(connection, status, response);
    MHD_destroy_response(response);
    return result;
}

MHD_Result deny(MHD_Connection* connection, unsigned status, const char* reason,
                const char* header = nullptr, const char* value = nullptr) {
    std::cerr << "mcp_http denied " << reason << '\n';
    return send(connection, status, mcp::Json{{"error", reason}}.dump(), header, value);
}

MHD_Result access(void* config, MHD_Connection* connection, const char* path, const char* method,
                  const char*, const char* upload, std::size_t* length, void** context) {
    auto& settings = *static_cast<Settings*>(config);
    try {
        if (!*context) {
            Headers headers;
            MHD_get_connection_values(connection, MHD_HEADER_KIND, collect_headers, &headers);
            if (headers.duplicate) return deny(connection, 400, "duplicate_header");
            if (!headers.host || headers.host != settings.host
                || (headers.origin && (settings.origin.empty() || headers.origin != settings.origin)))
                return deny(connection, 403, "host_or_origin_denied");
            if (!headers.auth || std::strlen(headers.auth) != settings.bearer.size()
                || CRYPTO_memcmp(headers.auth, settings.bearer.data(), settings.bearer.size()) != 0)
                return deny(connection, 401, "authentication_required", "WWW-Authenticate", "Bearer realm=\"logistics-mcp\"");
            if (string_view(path) != "/mcp" || MHD_get_connection_values(connection, MHD_GET_ARGUMENT_KIND, nullptr, nullptr))
                return deny(connection, 404, "route_not_found");
            if (string_view(method) != "POST") return send(connection, 405, {}, "Allow", "POST");
            if (headers.version && !mcp::supported_version(headers.version))
                return deny(connection, 400, "unsupported_protocol_version");
            if (!headers.accept || !accepts(headers.accept, "application/json") || !accepts(headers.accept, "text/event-stream"))
                return deny(connection, 406, "unsupported_accept");
            if (!headers.type || trim(string_view(headers.type).substr(0, string_view(headers.type).find(';'))) != "application/json")
                return deny(connection, 415, "unsupported_content_type");
            if (headers.length) {
                const auto* end = headers.length + std::strlen(headers.length);
                std::size_t bytes = 0;
                const auto parsed = std::from_chars(headers.length, end, bytes);
                if (parsed.ec == std::errc::result_out_of_range || bytes > mcp::max_request)
                    return deny(connection, 413, "request_too_large");
                if (parsed.ec != std::errc{} || parsed.ptr != end)
                    return deny(connection, 400, "invalid_content_length");
            }
            if (!settings.take_request_slot()) return deny(connection, 429, "rate_limited");
            *context = new Request;
            return MHD_YES;
        }
        auto& request = *static_cast<Request*>(*context);
        if (Clock::now() - request.started > 5s) return deny(connection, 408, "request_timeout");
        if (*length) {
            if (*length > mcp::max_request - request.body.size()) {
                std::cerr << "mcp_http denied request_too_large\n";
                return MHD_NO; // Abort streaming input once it is above budget.
            }
            request.body.append(upload, *length);
            *length = 0;
            return MHD_YES;
        }

        const auto reply = mcp::process(settings.directory, request.body, nullptr);
        if (!reply) return send(connection, 202);
        const auto result = reply->find("result");
        const bool error = reply->contains("error") ||
            (result != reply->end() && result->value("isError", false));
        if (error) {
            const bool denied = result != reply->end() && result->contains("structuredContent") &&
                (*result)["structuredContent"].value("error", "") == "scope_denied";
            std::cerr << "mcp_http " << (denied ? "scope_denied" : "request_error") << '\n';
        } else {
            std::cerr << "mcp_http ok\n";
        }
        return send(connection, 200, reply->dump());
    } catch (const std::exception&) {
        return deny(connection, 500, "request_failed");
    }
}

void completed(void*, MHD_Connection*, void** context, MHD_RequestTerminationCode) {
    delete static_cast<Request*>(*context);
    *context = nullptr;
}

bool valid_key(string_view token) {
    return token.size() == 64 && token.find_first_not_of("0123456789abcdefABCDEF") == string_view::npos;
}
}

int main(int argc, char** argv) {
    if (argc != 4 || string_view(argv[1]) != "--enable-loopback") {
        std::cerr << "Usage: logistics-mcp-http --enable-loopback PORT DATA_DIR\n";
        return 2;
    }
    const char* token = std::getenv("LTP_MCP_GATEWAY_TOKEN");
    if (!token || !valid_key(token)) {
        std::cerr << "Configure LTP_MCP_GATEWAY_TOKEN with 64 hexadecimal digits\n";
        return 2;
    }
    try {
        const auto port = sim::number(argv[2], 1024, 65535);
        const auto* origin = std::getenv("LTP_MCP_ALLOWED_ORIGIN");
        Settings settings{std::filesystem::u8path(argv[3]), "127.0.0.1:" + std::to_string(port),
                          origin ? origin : "", "Bearer " + std::string(token)};
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(static_cast<std::uint16_t>(port));
        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
        const std::unique_ptr<MHD_Daemon, decltype(&MHD_stop_daemon)> daemon(
            MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, static_cast<std::uint16_t>(port), nullptr, nullptr,
                access, &settings,
                MHD_OPTION_SOCK_ADDR, reinterpret_cast<sockaddr*>(&address),
                MHD_OPTION_NOTIFY_COMPLETED, completed, nullptr,
                MHD_OPTION_CONNECTION_LIMIT, 12U,
                MHD_OPTION_CONNECTION_MEMORY_LIMIT, std::size_t{32 * 1024},
                MHD_OPTION_CONNECTION_TIMEOUT, 5U,
                MHD_OPTION_END),
            MHD_stop_daemon);
        if (!daemon) {
            std::cerr << "Cannot start loopback MCP HTTP\n";
            return 1;
        }
        std::cerr << "MCP HTTP listening on IPv4 loopback\n";
        while (!stopping) std::this_thread::sleep_for(50ms);
        return 0;
    } catch (const std::exception&) {
        std::cerr << "MCP HTTP startup failed\n";
        return 2;
    }
}
