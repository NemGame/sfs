#if !defined(_WIN32) && !defined(__linux__)
#error "Unsupported platform"
#endif

#include <cinttypes>

#define DEBUG 1

#include "socket.hpp"
#include "_main.hpp"
#include "filesystem.hpp"
#include "security.hpp"

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
        Security::Initialize();
    } else if (command[0] == 'P') {  // path requests
        std::wstring path;
        std::wstring key;
        bool flip = false;
        for (size_t i = 1; i < command.size(); ++i) {
            if (command[i] == '\r' || command[i] == '\n') continue;
            if (!flip && command[i] == ':') {
                flip = true;
                continue;
            }
            if (flip) key += command[i];
            else path += command[i];
        }
        std::vector<Security::SecureNode> nodes = Security::RequestAccessToContentOfDirectory(path, key);
        if (nodes.empty()) {  // return "{contents:[]}"
            const char* empty_reply = "{\"contents\":[]}\n";
            send(client_socket, empty_reply, strlen(empty_reply), 0);
            return;
        }
        send(client_socket, "{\"contents\":[", strlen("{\"contents\":["), 0);
        for (size_t i = 0; i < nodes.size(); ++i) {
            std::string utf8_reply = wide_to_utf8(nodes[i].ToJson(), true);
            send(client_socket, utf8_reply.c_str(), utf8_reply.size(), 0);
            if (i < nodes.size() - 1) send(client_socket, ",", 1, 0);
        }
        send(client_socket, "]}\n", strlen("]}\n"), 0);
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

    Security::Initialize();

    Security::WriteSecurityDetails(Security::ReadSecurityDetails());

    run_server(6969);

    Security::WriteSecurityDetails(Security::UnifiedSecurityDetails);

    return 0;
}