#include "generator.h"

#include <map>
#include <set>
#include <utility>

namespace {

const std::set<std::string> kTextSuffixes = {"gradle", "xml", "java", "kt", "properties", "md",
                                             "yml", "yaml", "pro", "gitignore"};

std::string xmlEscape(std::string s) {
    std::string o;
    for (char c : s) {
        switch (c) {
        case '&': o += "&amp;"; break;
        case '<': o += "&lt;"; break;
        case '>': o += "&gt;"; break;
        case '"': o += "\\\""; break;
        case '\'': o += "\\'"; break;
        default: o += c;
        }
    }
    if (!o.empty() && (o[0] == '@' || o[0] == '?')) o.insert(o.begin(), '\\');
    return o;
}

struct Vars {
    std::vector<std::pair<std::string, std::string>> inlineVars;
    std::map<std::string, std::string> blockVars; // a line that is exactly the key is replaced (or dropped if empty)
};

std::string trimA(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

void replaceAll(std::string &s, const std::string &from, const std::string &to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::string processText(const std::string &text, const Vars &v) {
    std::string out;
    size_t start = 0;
    bool first = true;
    for (;;) {
        size_t nl = text.find('\n', start);
        std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        bool keep = true;
        auto b = v.blockVars.find(trimA(line));
        if (b != v.blockVars.end()) {
            if (b->second.empty()) keep = false; else line = b->second;
        } else {
            for (const auto &kv : v.inlineVars) replaceAll(line, kv.first, kv.second);
        }
        if (keep) {
            if (!first) out += '\n';
            out += line;
            first = false;
        }
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

std::string suffixOf(const std::string &filename) {
    size_t d = filename.rfind('.');
    if (d == std::string::npos) return {};
    std::string s = filename.substr(d + 1);
    for (auto &c : s) c = (char)tolower((unsigned char)c);
    return s;
}

std::wstring copyTree(const fs::path &srcRoot, const fs::path &dstRoot, const std::string &packagePath, const Vars &vars) {
    std::error_code ec;
    if (!fs::is_directory(srcRoot, ec))
        return L"The project template is missing next to the app (" + srcRoot.wstring() + L"). Rebuild so the template folder is copied.";

    for (auto it = fs::recursive_directory_iterator(srcRoot, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) return L"Couldn't read the project template.";
        if (!it->is_regular_file(ec)) continue;

        // relative path, with dot_ prefixes turned into dots and __PACKAGE_PATH__ expanded
        std::string rel;
        for (const auto &part : fs::relative(it->path(), srcRoot, ec)) {
            std::string p = part.generic_string();
            if (p.rfind("dot_", 0) == 0) p = "." + p.substr(4);
            if (!rel.empty()) rel += '/';
            rel += p;
        }
        replaceAll(rel, "__PACKAGE_PATH__", packagePath);

        fs::path dst = dstRoot / fs::path(widen(rel));
        std::string data = readFile(it->path());
        if (kTextSuffixes.count(suffixOf(dst.filename().string())))
            data = processText(data, vars);
        if (!writeFile(dst, data))
            return L"Couldn't write " + widen(rel) + L".";
    }
    return {};
}

} // namespace

std::wstring projectFolderName(const std::wstring &name) {
    std::wstring out;
    for (wchar_t c : trim(name))
        if (iswalnum(c) || c == L'_' || c == L'-') out += c;
    return out;
}

std::wstring generateProject(const fs::path &templateRoot, const GenOptions &o) {
    std::error_code ec;
    const bool existed = fs::exists(o.dest, ec);
    fs::create_directories(o.dest, ec);
    if (ec) return L"Couldn't create the project folder.";

    Vars v;
    const std::string name = narrow(o.name);
    v.inlineVars = {
        {"__APP_NAME_XML__", xmlEscape(name)},
        {"__APP_NAME__", name},
        {"__PACKAGE__", narrow(o.package)},
        {"__PROJECT_NAME__", narrow(projectFolderName(o.name))},
        {"__MIN_SDK__", std::to_string(o.minSdk)},
    };
    if (o.kotlin) {
        v.blockVars["__KOTLIN_ROOT_PLUGIN__"] = "    id 'org.jetbrains.kotlin.android' version '1.9.24' apply false";
        v.blockVars["__KOTLIN_APP_PLUGIN__"] = "    id 'org.jetbrains.kotlin.android'";
        v.blockVars["__KOTLIN_OPTIONS__"] = "    kotlinOptions {\n        jvmTarget = '17'\n    }";
    } else {
        v.blockVars["__KOTLIN_ROOT_PLUGIN__"] = "";
        v.blockVars["__KOTLIN_APP_PLUGIN__"] = "";
        v.blockVars["__KOTLIN_OPTIONS__"] = "";
    }

    std::string packagePath = narrow(o.package);
    for (auto &c : packagePath) if (c == '.') c = '/';

    std::wstring err = copyTree(templateRoot / "common", o.dest, packagePath, v);
    if (err.empty()) err = copyTree(templateRoot / (o.kotlin ? "kotlin" : "java"), o.dest, packagePath, v);
    if (err.empty() && o.workflow) err = copyTree(templateRoot / "workflow", o.dest, packagePath, v);

    if (!err.empty()) {
        if (!existed) fs::remove_all(o.dest, ec);
        return err;
    }
    return {};
}
