#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <cwctype>
#include <sstream>
namespace Security {
    using namespace std;
    #pragma region Types
    enum class FileType : uint8_t {
        File = 1 << 0,
        Directory = 1 << 1
    };
    enum class SecurityPermission : uint8_t {
        Read = 1 << 0,
        Write = 1 << 1,

        Default = Read | Write
    };
    constexpr SecurityPermission operator|(SecurityPermission lhs, SecurityPermission rhs) {
        return static_cast<SecurityPermission>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
    }
    constexpr SecurityPermission operator&(SecurityPermission lhs, SecurityPermission rhs) {
        return static_cast<SecurityPermission>(static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
    }
    struct SecurityKey {
        wstring key;
        SecurityPermission permission = SecurityPermission::Read | SecurityPermission::Write;
    };
    struct SecurityInformation {
        vector<SecurityKey> keys;
        uint8_t GetPermissionFor(const wstring& key) const {
            SecurityKey* default_key = nullptr;
            for (const auto& k : keys) {
                if (k.key == key) {
                    return static_cast<uint8_t>(k.permission);
                }
                if (k.key == L"default") default_key = const_cast<SecurityKey*>(&k);
            }
            if (default_key) return static_cast<uint8_t>(default_key->permission);
            return 0;
        }
        bool HasPermission(const wstring& key, SecurityPermission permission) const {
            uint8_t perm = GetPermissionFor(key);
            return (perm & static_cast<uint8_t>(permission)) == static_cast<uint8_t>(permission);
        }
        static SecurityInformation Default() {
            SecurityInformation info;
            info.keys.push_back(SecurityKey{ L"default", SecurityPermission::Read | SecurityPermission::Write });
            return info;
        }
    };
    struct SecureNode {
        wstring data;
        SecureNode* parent = nullptr;
        vector<SecureNode> children;
        FileType type;
        SecurityInformation security;
        wstring basePath = L"/";  // containing directory, used when there is no parent (listing results)
        void AddChild(SecureNode child) {
            child.parent = this;
            children.push_back(std::move(child));
        }
        wstring toWString() const {
            wstring result = data == L"/" ? L"" : data;
            if (type == FileType::Directory) result += L"/";
            result += L'[';
            bool inherits = parent != nullptr && security.keys.size() == parent->security.keys.size();
            if (inherits) {
                for (size_t k = 0; k < security.keys.size(); ++k) {
                    if (security.keys[k].key != parent->security.keys[k].key ||
                        security.keys[k].permission != parent->security.keys[k].permission) { inherits = false; break; }
                }
            }
            if (inherits || security.keys.empty()) result += L"#";
            else for (const auto& key : security.keys) {
                result += key.key + L"#" + to_wstring(static_cast<uint8_t>(key.permission)) + L',';
            }
            result += L']';
            for (const auto& child : children) {
                result += L"\n" + child.toWString();
            }
            result += L"#";
            return result;
        }
        SecureNode* FindChild(const wstring& name) {
            for (auto& child : children) {
                if (child.data == name) return &child;
            }
            return nullptr;
        }
        SecureNode* FindChildRecursive(const wstring& path) {
            if (path.empty() || path == L"/") return this;
            size_t pos = path.find(L'/');
            wstring next = (pos == wstring::npos) ? path : path.substr(0, pos);
            wstring remaining = (pos == wstring::npos) ? L"" : path.substr(pos + 1);
            SecureNode* child = FindChild(next);
            if (!child) return nullptr;
            return child->FindChildRecursive(remaining);
        }
        const SecureNode* FindChildRecursive(const wstring& path) const {
            if (path.empty() || path == L"/") return this;
            size_t pos = path.find(L'/');
            wstring next = (pos == wstring::npos) ? path : path.substr(0, pos);
            wstring remaining = (pos == wstring::npos) ? L"" : path.substr(pos + 1);
            for (const auto& child : children) {
                if (child.data == next) return child.FindChildRecursive(remaining);
            }
            return nullptr;
        }
        wstring ToJson() const {
            wstring result = L"{";
            wstring escaped;
            for (wchar_t ch : data) {
                if (ch == L'"' || ch == L'\\') escaped += L'\\';
                escaped += ch;
            }
            result += L"\"name\":\"" + escaped + L"\",";
            result += L"\"isDirectory\":";
            result += (type == FileType::Directory ? L"true" : L"false");
            result += L',';
            result += L"\"fullpath\":\"" + GetFullPath() + L"\"";
            result += L"}";
            return result;
        }
        wstring GetPath() const {
            if (!parent) return basePath;
            wstring path = parent->GetFullPath();
            if (path.back() != L'/') path += L'/';
            return path;
        }
        wstring GetFullPath() const {
            return GetPath() + data + (type == FileType::Directory ? L"/" : L"");
        }
    };
    struct SecurityDetails {
        SecureNode node = SecureNode{L"/", nullptr, {}, FileType::Directory, SecurityInformation::Default()};
        wstring toWString() const {
            return node.toWString();
        }
        bool HasPermissionFor(const wstring& subpath, const wstring& key, SecurityPermission permission) const {
            const SecureNode* current = &node;
            const SecureNode* target = current->FindChildRecursive(subpath);
            if (!target) return false;
            for (const auto& k : target->security.keys) {
                if (k.key == key && (k.permission & permission) == permission) return true;
            }
            return false;
        }
    };
    #pragma endregion
    SecurityDetails ReadSecurityDetails();
    static Node<wstring>* UnifiedStructure;
    static SecurityDetails UnifiedSecurityDetails;
    void FixParents(SecureNode& node);
    void Initialize() {
        UnifiedStructure = fs::GetUnifiedDirectoryTree();
        UnifiedSecurityDetails = ReadSecurityDetails();
        FixParents(UnifiedSecurityDetails.node);  // the copy out of ReadSecurityDetails leaves stale parent pointers
    }
    // Lists the real contents of `path` (merged across all drives), keeping only entries `key` may read.
    // Entries without their own security node inherit from the nearest ancestor that has one.
    vector<SecureNode> RequestAccessToContentOfDirectory(const wstring& path, const wstring& key) {
        vector<wstring> parts;
        wstring part;
        for (size_t i = 0; i <= path.size(); ++i) {
            if (i == path.size() || path[i] == L'/') {
                if (part == L"." || part == L"..") return {};
                if (!part.empty()) parts.push_back(part);
                part.clear();
            } else if (path[i] == L'\\' || path[i] == L':') {
                return {};
            } else {
                part += path[i];
            }
        }

        const SecureNode* node = &UnifiedSecurityDetails.node;
        const SecurityInformation* info = &node->security;
        for (const auto& p : parts) {
            const SecureNode* next = node ? node->FindChildRecursive(p) : nullptr;
            if (next) info = &next->security;
            node = next;
        }
        if (!info->HasPermission(key, SecurityPermission::Read)) return {};

        wstring basePath = L"/";
        for (const auto& p : parts) basePath += p + L"/";
        vector<SecureNode> result;
        for (const auto& drive : fs::drives) {
            std::filesystem::path dir(drive.path);
            for (const auto& p : parts) dir /= p;
            error_code ec;
            if (!std::filesystem::is_directory(dir, ec)) continue;
            std::filesystem::directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec), end;
            for (; !ec && it != end; it.increment(ec)) {
                error_code ec2;
                const bool isDir = it->is_directory(ec2);
                const wstring name = it->path().filename().wstring();
                bool duplicate = false;
                for (const auto& r : result) if (r.data == name) { duplicate = true; break; }
                if (duplicate) continue;
                const SecureNode* childNode = node ? node->FindChildRecursive(name) : nullptr;
                const SecurityInformation& childInfo = childNode ? childNode->security : *info;
                if (!childInfo.HasPermission(key, SecurityPermission::Read)) continue;
                result.push_back(SecureNode{ name, nullptr, {}, isDir ? FileType::Directory : FileType::File, childInfo, basePath });
            }
        }
        return result;
    }
    wstring GetContentsOfFile(const wstring& path) {
        wifstream file{std::filesystem::path(path)};
        wstringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
    // `i` must point at '['; on return it points at the matching ']'.
    SecurityInformation ReadSecurityInformation(const wstring& content, size_t& i, const SecurityInformation& parentInfo) {
        SecurityInformation info;
        if (i >= content.size() || content[i] != L'[') return parentInfo;
        ++i;
        wstring buffer;
        while (i < content.size() && content[i] != L']') {
            const wchar_t c = content[i];
            if (c == L'\n' || c == L'\r' || c == L' ' || c == L',') { ++i; continue; }
            if (c == L'#') {
                if (buffer.empty()) {  // "[#]" = inherit from parent
                    info = parentInfo;
                } else {
                    ++i;
                    int permission = (i < content.size() && iswdigit(content[i])) ? content[i] - L'0' : 3;
                    SecurityKey newKey{ buffer, static_cast<SecurityPermission>(permission) };
                    bool replaced = false;
                    for (auto& existing : info.keys) {
                        if (existing.key == newKey.key) { existing = newKey; replaced = true; break; }
                    }
                    if (!replaced) info.keys.push_back(newKey);
                    buffer.clear();
                }
            } else {
                buffer += c;
            }
            ++i;
        }
        if (!buffer.empty()) {
            bool found = false;
            for (auto& existing : info.keys) {
                if (existing.key == buffer) { existing.permission = SecurityPermission::Default; found = true; break; }
            }
            if (!found) info.keys.push_back(SecurityKey{ buffer, SecurityPermission::Default });
        }
        return info;
    }
    void FixParents(SecureNode& node) {
        for (auto& child : node.children) {
            child.parent = &node;
            FixParents(child);
        }
    }
    SecurityDetails ReadSecurityDetails() {
        static wstring Path = L"security.ini";
        SecurityDetails details;
        wstring content = GetContentsOfFile(Path);
        wstring currentPath = L"/";
        wstring buffer;
        SecureNode* topNode = &details.node;
        for (size_t i = 0; i < content.size(); ++i) {
            const wchar_t c = content[i];
            if (c == L'\n' || c == L'\r' || c == L' ') continue;
            if (c == L'/' || c == L'[') {
                if (c == L'/') ++i;
                SecurityInformation info = ReadSecurityInformation(content, i, topNode->security);
                if (!buffer.empty()) {
                    SecureNode child = SecureNode{buffer, topNode, {}, c == L'[' ? FileType::File : FileType::Directory, info};
                    topNode->AddChild(std::move(child));
                    topNode = &topNode->children.back();
                    buffer.clear();
                } else if (topNode == &details.node) {  // leading "/[...]" describes the root
                    topNode->security = info;
                }
            } else if (c == L'#') {  // go up 1
                if (topNode->parent) {
                    topNode = topNode->parent;
                }
            } else {
                buffer += c;
            }
        }
        FixParents(details.node);
        return details;
    }
    void WriteSecurityDetails(const SecurityDetails& details) {
        static wstring Path = L"security_test.ini";
        const wstring content = details.node.toWString();
        wofstream file{std::filesystem::path(Path)};
        if (file.is_open()) {
            file << content;
            file.close();
        } else {
            wcerr << L"Failed to open file for writing: " << Path << endl;
        }
    }
}