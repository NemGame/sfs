#include <iostream>
#include <string>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
    typedef int SOCKET;
    const SOCKET INVALID_SOCKET = -1;
    const int SOCKET_ERROR = -1;
#endif

void process_command(const std::string& command, SOCKET client_socket);

void run_server(int port) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Listen on localhost
    address.sin_port = htons(port);       // Port to talk to

    bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd, 3);

    while (true) {
        socklen_t addrlen = sizeof(address);
        SOCKET client_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen);

        if (client_socket != INVALID_SOCKET) {
            // Keep the connection open and serve commands until the client disconnects
            bool stop = false;
            while (true) {
                char buffer[1024] = {0};
#ifdef _WIN32
                int n = recv(client_socket, buffer, 1023, 0);
#else
                int n = read(client_socket, buffer, 1023);
#endif
                if (n <= 0) break;
                std::string command(buffer, n);
                while (!command.empty() && (command.back() == '\n' || command.back() == '\r')) command.pop_back();

                process_command(command, client_socket);
                if (command == "STOP") {
                    stop = true;
                    break;
                }
            }
            if (stop) break; // Exit background process loop

#ifdef _WIN32
            closesocket(client_socket);
#else
            close(client_socket);
#endif
        }
    }

#ifdef _WIN32
    closesocket(server_fd);
    WSACleanup();
#else
    close(server_fd);
#endif
}

void kill_socket(SOCKET sock) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

// encoded -> \n => %0A
std::string wide_to_utf8(const std::wstring& w, bool encoded = false) {
    std::string out;
    for (size_t i = 0; i < w.size(); i++) {
        uint32_t c = static_cast<uint32_t>(w[i]);
        if (sizeof(wchar_t) == 2 && c >= 0xD800 && c <= 0xDBFF && i + 1 < w.size()) {
            uint32_t lo = static_cast<uint32_t>(w[i + 1]);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                i++;
            }
        }
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    if (encoded) {
        std::string encoded_out;
        for (char c : out) {
            if (c == '\n') encoded_out += "%0A";
            else encoded_out += c;
        }
        return encoded_out;
    }
    return out;
}