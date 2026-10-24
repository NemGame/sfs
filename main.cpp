#if !defined(_WIN32) && !defined(__linux__)
#error "Unsupported platform"
#endif

#include <cinttypes>

#define DEBUG 1

#include "socket.hpp"
#include "_main.hpp"
#include "filesystem.hpp"

void process_command(const std::string& command, SOCKET client_socket) {
    if (command == "PING") {
        const char* reply = "PONG\n";
        send(client_socket, reply, strlen(reply), 0);
    } else if (command == "SPACE") {
        const std::string drives_info = wide_to_utf8(fs::GetDrivesFormatted(), true) + "\n";
        send(client_socket, drives_info.c_str(), drives_info.size(), 0);
        std::wcout << L"space req" << std::endl;
    } else if (command == "RELOAD") {
        try {
            fs::ReloadDrives();
        } catch (const std::exception& e) {
            std::cerr << "ReloadDrives: " << e.what() << std::endl;
        }
    }
    else if (command == "STOP") {
        kill_socket(client_socket);
    }
}

int __main() {
    try {
        fs::ReloadDrives();
    } catch (const std::exception& e) {
        std::cerr << "ReloadDrives: " << e.what() << std::endl;
        return 1;
    }

    run_server(6969);

    return 0;
}