#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

inline std::wstring widen(const std::string &s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

inline std::string narrow(const std::wstring &w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

inline std::wstring jsonStr(const std::wstring &s) {
    std::wstring o = L"\"";
    for (wchar_t c : s) {
        switch (c) {
        case L'"': o += L"\\\""; break;
        case L'\\': o += L"\\\\"; break;
        case L'\n': o += L"\\n"; break;
        case L'\r': o += L"\\r"; break;
        case L'\t': o += L"\\t"; break;
        default:
            if (c < 0x20) { wchar_t b[8]; swprintf_s(b, L"\\u%04x", (unsigned)c); o += b; }
            else o += c;
        }
    }
    return o + L"\"";
}

inline std::vector<std::wstring> split(const std::wstring &s, wchar_t sep) {
    std::vector<std::wstring> out;
    size_t start = 0;
    for (;;) {
        size_t p = s.find(sep, start);
        if (p == std::wstring::npos) { out.push_back(s.substr(start)); break; }
        out.push_back(s.substr(start, p - start));
        start = p + 1;
    }
    return out;
}

inline std::wstring trim(const std::wstring &s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return {};
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

inline std::wstring lower(std::wstring s) {
    for (auto &c : s) c = (wchar_t)towlower(c);
    return s;
}

inline std::string readFile(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline bool writeFile(const fs::path &p, const std::string &data) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), (std::streamsize)data.size());
    return (bool)f;
}

inline std::wstring envVar(const wchar_t *name) {
    wchar_t buf[2048];
    DWORD n = GetEnvironmentVariableW(name, buf, 2048);
    return (n > 0 && n < 2048) ? std::wstring(buf, n) : std::wstring();
}
