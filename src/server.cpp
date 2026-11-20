#include "http.hpp"
#include "router.hpp"
#include "storage.hpp"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {
std::atomic<bool> g_running{false};
int g_listen_fd = -1;

void handle_signal(int) {
    g_running.store(false);
    if (g_listen_fd >= 0) {
        shutdown(g_listen_fd, SHUT_RDWR);
    }
}

bool send_all(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            if (errno == EINTR) continue;
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

void handle_client(int client_fd, const Router& router) {
    int nodelay = 1;
    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    std::string buffer;
    char chunk[4096];

    while (g_running.load()) {
        while (true) {
            ParseResult parsed = try_parse_http_request(buffer);
            if (parsed.status == ParseStatus::Incomplete) {
                break;
            }
            if (parsed.status == ParseStatus::Error) {
                HttpResponse err{400, "{\"error\":\"bad request\"}", false};
                send_all(client_fd, serialize_http_response(err));
                close(client_fd);
                return;
            }

            buffer.erase(0, parsed.bytes_consumed);
            HttpResponse resp = router.handle(parsed.request);
            if (!send_all(client_fd, serialize_http_response(resp))) {
                close(client_fd);
                return;
            }
            if (!resp.keep_alive || !g_running.load()) {
                close(client_fd);
                return;
            }
        }

        ssize_t r = recv(client_fd, chunk, sizeof(chunk), 0);
        if (r > 0) {
            buffer.append(chunk, static_cast<std::size_t>(r));
        } else if (r == 0) {
            break;
        } else {
            if (errno == EINTR) continue;
            break;
        }
    }

    close(client_fd);
}
} // namespace

void run_http_server(uint16_t port, std::size_t /*threads*/) {
    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        throw std::runtime_error("socket() failed");
    }

    int opt = 1;
    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(g_listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
        throw std::runtime_error("bind() failed");
    }

    if (listen(g_listen_fd, SOMAXCONN) < 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
        throw std::runtime_error("listen() failed");
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::signal(SIGPIPE, SIG_IGN);

    Storage storage;
    Router router(storage);

    g_running.store(true);
    std::cout << "v0.2 server listening on port " << port << "\n";

    while (g_running.load()) {
        int client_fd = accept(g_listen_fd, nullptr, nullptr);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            break;
        }
        handle_client(client_fd, router);
    }

    if (g_listen_fd >= 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }
}