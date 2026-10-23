#include <vector>
#include <algorithm>
#include <string>
#include <filesystem>

#include "tree.hpp"

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