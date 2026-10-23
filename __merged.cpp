#if !defined(_WIN32) && !defined(__linux__)
#error "Unsupported platform"
#endif

#include <cinttypes>

#define DEBUG 1

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
int __main();
void __pre_main() {
    // is <this file name> already running?

}
#if defined(_WIN32)
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
void KillPreviousInstances(const std::wstring& processName) {
    // A saját programunk Process ID-jának lekérése, hogy ezt NE zárjuk be
    DWORD currentPid = GetCurrentProcessId();

    // Pillanatkép készítése a rendszerről
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        std::wcerr << L"Nem sikerult elerni a folyamatok listajat." << std::endl;
        return;
    }

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(snapshot, &entry)) {
        do {
            // Név egyezés ellenőrzése és annak kiszűrése, hogy ne a saját PID-ünk legyen
            if (_wcsicmp(entry.szExeFile, processName.c_str()) == 0 && entry.th32ProcessID != currentPid) {
                std::wcout << L"Korabbi peldany megtalalva (PID: " << entry.th32ProcessID << L"). Bezaras..." << std::endl;

                // A folyamat megnyitása leállítási (TERMINATE) jogosultsággal
                HANDLE processHandle = OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID);
                if (processHandle != NULL) {
                    // A folyamat kényszerített bezárása (0-s kilépési kóddal)
                    if (TerminateProcess(processHandle, 0)) {
                        std::wcout << L"Sikeresen bezarva." << std::endl;
                    } else {
                        std::wcerr << L"Hiba a bezaras soran. Hibakod: " << GetLastError() << std::endl;
                    }
                    // Mindig be kell zárni a megnyitott handle-t
                    CloseHandle(processHandle);
                } else {
                    std::wcerr << L"Nem sikerult megnyitni a folyamatot. Jogosultsag hiba? Hibakod: " << GetLastError() << std::endl;
                }
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
}
int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    int argc;
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc > 0) {
        KillPreviousInstances(args[0]);
    }
    LocalFree(args);
    return __main();
}
#else
#include <fstream>
#include <unistd.h>
#include <signal.h>
#include <filesystem>
#include <algorithm>
void KillPreviousInstances(const char* processName) {
    pid_t currentPid = getpid();

    try {
        // Végigmegyünk a /proc mappa tartalmán
        for (const auto& entry : std::filesystem::directory_iterator("/proc")) {
            if (!entry.is_directory()) continue;

            // Csak azokat a mappákat nézzük, amik tisztán számok (ezek a PID-ek)
            std::string folderName = entry.path().filename().string();
            if (!std::all_of(folderName.begin(), folderName.end(), ::isdigit)) continue;

            pid_t pid = std::stoi(folderName);
            if (pid == currentPid) continue; // Saját magunkat átugorjuk

            // Olvassuk be a folyamat indítási parancsát / nevét
            std::ifstream cmdFile(entry.path() / "cmdline");
            std::string cmdline;
            if (std::getline(cmdFile, cmdline)) {
                // A cmdline gyakran a teljes elérési utat tartalmazza, vagy null-byte-al elválasztott argumentumokat
                // Kivonjuk belőle csak a futtatható fájl nevét
                size_t lastSlash = cmdline.find_last_of('/');
                std::string exeName = (lastSlash == std::string::npos) ? cmdline : cmdline.substr(lastSlash + 1);

                // A cmdline végén vagy az argumentumok között null karakterek lehetnek, vágjuk le a szemetet
                size_t nullTerminator = exeName.find('\0');
                if (nullTerminator != std::string::npos) {
                    exeName = exeName.substr(0, nullTerminator);
                }

                // Ha egyezik a keresett névvel
                if (exeName == processName) {
                    std::cout << "Korabbi peldany megtalalva (PID: " << pid << "). Bezaras..." << std::endl;

                    // 1. lépés: Küldünk egy barátságos SIGTERM (15) jelet a tiszta bezáráshoz
                    if (kill(pid, SIGTERM) == 0) {
                        std::cout << "SIGTERM elkuldve." << std::endl;
                    } else {
                        std::cerr << "Nem sikerult leallitani. Hiba: " << strerror(errno) << std::endl;
                    }
                    
                    // Opcionális: Ha nagyon makacs a folyamat, a SIGKILL (9) azonnal kinyírja:
                    // kill(pid, SIGKILL);
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Hiba a /proc bejarasa kozben: " << e.what() << std::endl;
    }

}
void make_daemon(int argc, char* argv[]) {
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    if (setsid() < 0) exit(EXIT_FAILURE);

    chdir("/");

    #if not DEBUG
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    #endif
    // for (int x = sysconf(_SC_OPEN_MAX); x>=0; x--) {
    //     close(x);
    // }
    if (argc > 0) {
        KillPreviousInstances(argv[0]);
    }
    __main();
}
int main(int argc, char* argv[]) {
    make_daemon(argc, argv);
    return 0;
}
#endif
#include <vector>
#include <algorithm>
#include <string>
#include <filesystem>

#include <vector>

template<typename T>
struct Node {
    T data;
    std::vector<Node<T>*> next;
    Node(const T& d) : data(d) {}
    ~Node() {
        for (auto& child : next) {
            delete child;
        }
    }
    size_t getDepth() const {
        if (next.empty()) return 1;
        size_t max_depth = 0;
        for (const auto& child : next) {
            if (child) {
                max_depth = std::max(max_depth, child->getDepth());
            }
        }
        return max_depth + 1;
    }
    size_t countNodes() const {
        size_t count = 1; // Count the current node
        for (const auto& child : next) {
            if (child) {
                count += child->countNodes();
            }
        }
        return count;
    }
};

#define EXESSIVE_ERRORCHECK 1

namespace fs {
    using namespace std;
    namespace fs = filesystem;

    static const wstring TARGET_NAME = L"__fs_public";
    #ifdef _WIN32
    static const wstring BASE_FOLDER = L"D:\\MyApps\\akoss\\_drive\\sfs\\fs_test";
    static const wstring SEPARATOR = L"\\";
    #else
    static const wstring BASE_FOLDER = L"/mnt";
    static const wstring SEPARATOR = L"/";
    #endif

    #if DEBUG
    wstring FormatSize(size_t size);
    #endif

    struct Drive {
        wstring path;
        size_t max_space;
        size_t available_space;
        Drive(const wstring& p) : path(p) {
            auto space_info = fs::space(path);
            max_space = space_info.capacity;
            available_space = space_info.available;
        }
        size_t GetUsedSpace() const { return max_space - available_space; }
    };
    static vector<wstring> roots;
    static vector<Drive> drives;
    wstring RecurseTargetFolderPath(size_t depth, const wstring& base) {
        if (depth == 0) return wstring(L"");
        error_code ec;
        fs::directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end;
        if (ec) return L"";
        vector<wstring> subdirs;
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            error_code ec2;
            if (!it->is_directory(ec2)) continue;
            if (it->path().filename() == TARGET_NAME) return it->path().wstring();
            subdirs.push_back(it->path().wstring());
        }
        if (depth > 1) {
            for (const auto& sub : subdirs) {
                wstring found = RecurseTargetFolderPath(depth - 1, sub);
                if (!found.empty()) return found;
            }
        }
        return L"";
        // throw runtime_error("Target folder not found");
    }
    void RediscoverRoots() {
        roots.clear();
        error_code ec;
        fs::directory_iterator it(BASE_FOLDER, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            wstring fn = RecurseTargetFolderPath(1, it->path().wstring());
            if (!fn.empty()) roots.push_back(fn);
        }
        #if DEBUG
        wcout << L"Discovered roots:" << endl;
        for (const auto& root : roots) {
            wcout << L"  " << root << endl;
        }
        #endif
    }
    void ReloadDrives() {
        RediscoverRoots();
        drives.clear();
        for (const auto& root : roots) {
            drives.emplace_back(root);
        }
        #if DEBUG
        wcout << L"Reloaded drives:" << endl;
        for (const auto& drive : drives) {
            wcout << L"  " << drive.path << L" (" << FormatSize(drive.GetUsedSpace()) << L" / " << FormatSize(drive.max_space) << L")" << endl;
        }
        #endif
    }
    size_t GetDriveSize(const Drive& drive) {
        size_t total_size = 0;
        for (const auto& entry : fs::recursive_directory_iterator(drive.path)) {
            if (entry.is_regular_file()) {
                total_size += entry.file_size();
            }
        }
        return total_size;
    }
    wstring FormatSize(size_t size) {
        const wstring units[] = {L"B", L"KB", L"MB", L"GB", L"TB", L"PB"};
        size_t unit_index = 0;
        double size_d = static_cast<double>(size);
        while (size_d >= 1024 && unit_index < 5) {
            size_d /= 1024;
            ++unit_index;
        }
        wchar_t buffer[64];
        swprintf(buffer, 64, L"%.2f %s", size_d, units[unit_index].c_str());
        return wstring(buffer);
    }
    wstring GetDrivesFormatted() {
        wstring result;
        for (const auto& drive : drives) {
            result += drive.path + L": " + FormatSize(drive.GetUsedSpace()) + L" / " + FormatSize(drive.max_space) + L"\n";
        }
        result = result.substr(0, result.size() - 1); // Remove the trailing newline
        return result;
    }
    Node<wstring>* getNodeForPath(const wstring& path) {
        vector<wstring> paths;
        for (const auto& entry : fs::directory_iterator(path)) {
            if (entry.is_directory()) {
                paths.push_back(entry.path().wstring());
            }
        }
        Node<wstring>* node = new Node(path);
        for (const auto& subpath : paths) {
            node->next.push_back(new Node(subpath));
        }
        return node;
    }
    Node<wstring>* GetMasterDirectoryTree() {
        vector<Node<wstring>*> nodes;
        for (const auto& drive : drives) {
            vector<Node<wstring>*> subnodes;
            for (const auto& entry : fs::directory_iterator(drive.path)) {
                if (entry.is_directory()) {
                    subnodes.push_back(getNodeForPath(entry.path().wstring()));
                }
            }
            Node<wstring>* drive_node = new Node(drive.path);
            drive_node->next = subnodes;
            nodes.push_back(drive_node);
        }
        Node<wstring>* root = new Node(TARGET_NAME);
        root->next = nodes;
        return root;
    }
    void UnifyDirectoryTree(const wstring& subpath) {
        // Create in every drive first (with parents), then collect children, then recurse,
        // so no directory is created while iterating and parents always exist.
        vector<wstring> children;
        for (auto& drive : drives) {
            fs::create_directories(drive.path + SEPARATOR + subpath);
        }
        for (auto& drive : drives) {
            for (const auto& entry : fs::directory_iterator(drive.path + SEPARATOR + subpath)) {
                if (entry.is_directory()) {
                    wstring child = subpath + SEPARATOR + entry.path().filename().wstring();
                    if (find(children.begin(), children.end(), child) == children.end())
                        children.push_back(child);
                }
            }
        }
        for (const auto& child : children) {
            UnifyDirectoryTree(child);
        }
    }
    void FixDirectoryTree() {
        vector<wstring> tops;
        for (const auto& drive : drives) {
            for (const auto& entry : fs::directory_iterator(drive.path)) {
                if (entry.is_directory()) {
                    wstring name = entry.path().filename().wstring();
                    if (find(tops.begin(), tops.end(), name) == tops.end()) tops.push_back(name);
                }
            }
        }
        for (const auto& name : tops) UnifyDirectoryTree(name);
    }
}

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

    try {
        fs::FixDirectoryTree();
    } catch (const std::exception& e) {
        std::cerr << "FixDirectoryTree: " << e.what() << std::endl;
        return 1;
    }
    run_server(6969);

    return 0;
}