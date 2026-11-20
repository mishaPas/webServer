#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <thread>

void run_http_server(uint16_t port, std::size_t threads);

int main(int argc, char* argv[]) {
    uint16_t port = 8080;
    std::size_t threads = std::thread::hardware_concurrency();
    if (threads == 0) threads = 4;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (arg.rfind("--port=", 0) == 0) {
            port = static_cast<uint16_t>(std::atoi(arg.substr(7).data()));
        } else if (arg == "--threads" && i + 1 < argc) {
            threads = static_cast<std::size_t>(std::atoi(argv[++i]));
        } else if (arg.rfind("--threads=", 0) == 0) {
            threads = static_cast<std::size_t>(std::atoi(arg.substr(10).data()));
        }
    }

    try {
        run_http_server(port, threads);
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}