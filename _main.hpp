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