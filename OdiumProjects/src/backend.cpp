#include "backend.h"
#include "generator.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <ctime>
#include <regex>
#include <set>

using Microsoft::WRL::ComPtr;

namespace {

std::wstring normPath(std::wstring p) {
    p = trim(p);
    if (p.size() >= 2 && p.front() == L'"' && p.back() == L'"') p = p.substr(1, p.size() - 2);
    if (p.empty()) return {};
    fs::path q = fs::path(p).lexically_normal();
    std::wstring s = q.make_preferred().wstring();
    while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
    return s;
}

bool isDir(const std::wstring &p) { std::error_code ec; return fs::is_directory(fs::path(p), ec); }
bool pathExists(const fs::path &p) { std::error_code ec; return fs::exists(p, ec); }

bool looksLikeGradleProject(const fs::path &d) {
    return pathExists(d / L"settings.gradle") || pathExists(d / L"settings.gradle.kts") ||
           pathExists(d / L"build.gradle") || pathExists(d / L"build.gradle.kts");
}

std::wstring readNamespace(const fs::path &dir) {
    for (const wchar_t *f : {L"app/build.gradle", L"app/build.gradle.kts"}) {
        std::string text = readFile(dir / f);
        std::smatch m;
        if (std::regex_search(text, m, std::regex("namespace\\s*=?\\s*['\"]([^'\"]+)['\"]"))) return widen(m[1].str());
    }
    return {};
}

fs::path findStudio() {
    std::vector<fs::path> c;
    const std::wstring pf = envVar(L"ProgramFiles");
    const std::wstring local = envVar(L"LOCALAPPDATA");
    if (!pf.empty()) {
        c.push_back(fs::path(pf) / L"Android/Android Studio/bin/studio64.exe");
        c.push_back(fs::path(pf) / L"Android/Android Studio Preview/bin/studio64.exe");
    }
    if (!local.empty()) {
        c.push_back(fs::path(local) / L"Programs/Android Studio/bin/studio64.exe");
        // JetBrains Toolbox: AndroidStudio/ch-*/<version>/bin/studio64.exe (newest version first)
        for (const wchar_t *app : {L"AndroidStudio", L"android-studio"}) {
            fs::path base = fs::path(local) / L"JetBrains/Toolbox/apps" / app;
            std::error_code ec;
            std::vector<fs::path> vers;
            for (auto &ch : fs::directory_iterator(base, ec)) {
                if (!ch.is_directory(ec)) continue;
                for (auto &v : fs::directory_iterator(ch.path(), ec))
                    if (v.is_directory(ec)) vers.push_back(v.path());
            }
            std::sort(vers.rbegin(), vers.rend());
            for (auto &v : vers) c.push_back(v / L"bin/studio64.exe");
        }
    }
    // The official installer records its install folder in the registry.
    for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        for (const wchar_t *sub : {L"SOFTWARE\\Android Studio", L"SOFTWARE\\WOW6432Node\\Android Studio"}) {
            wchar_t buf[MAX_PATH * 2];
            DWORD size = sizeof(buf);
            if (RegGetValueW(root, sub, L"Path", RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS)
                c.push_back(fs::path(buf) / L"bin/studio64.exe");
        }
    }
    wchar_t found[MAX_PATH];
    if (SearchPathW(nullptr, L"studio64.exe", nullptr, MAX_PATH, found, nullptr)) c.push_back(found);

    for (auto &p : c) {
        std::error_code ec;
        if (fs::exists(p, ec)) return p.lexically_normal().make_preferred();
    }
    return {};
}

fs::path studioExe(const std::wstring &configured) {
    fs::path p(configured);
    std::error_code ec;
    if (fs::is_directory(p, ec)) {
        if (fs::exists(p / L"bin/studio64.exe", ec)) return p / L"bin/studio64.exe";
        if (fs::exists(p / L"studio64.exe", ec)) return p / L"studio64.exe";
    }
    return p;
}

const std::set<std::string> &reservedWords() {
    static const std::set<std::string> w = {
        "abstract", "assert", "boolean", "break", "byte", "case", "catch", "char", "class", "const", "continue",
        "default", "do", "double", "else", "enum", "extends", "final", "finally", "float", "for", "goto", "if",
        "implements", "import", "instanceof", "int", "interface", "long", "native", "new", "package", "private",
        "protected", "public", "return", "short", "static", "strictfp", "super", "switch", "synchronized", "this",
        "throw", "throws", "transient", "try", "void", "volatile", "while", "true", "false", "null"};
    return w;
}

// Native folder / file picker. Returns an empty string if cancelled.
std::wstring pickPath(HWND owner, bool folder, const std::wstring &start, const wchar_t *title) {
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return {};
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : 0));
    dlg->SetTitle(title);
    if (!folder) {
        COMDLG_FILTERSPEC f[] = {{L"Programs (*.exe)", L"*.exe"}, {L"All files", L"*.*"}};
        dlg->SetFileTypes(2, f);
    }
    if (!start.empty() && isDir(start)) {
        ComPtr<IShellItem> item;
        if (SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), nullptr, IID_PPV_ARGS(&item)))) dlg->SetFolder(item.Get());
    }
    if (FAILED(dlg->Show(owner))) return {};
    ComPtr<IShellItem> res;
    if (FAILED(dlg->GetResult(&res))) return {};
    PWSTR p = nullptr;
    if (FAILED(res->GetDisplayName(SIGDN_FILESYSPATH, &p)) || !p) return {};
    std::wstring out(p);
    CoTaskMemFree(p);
    return out;
}

} // namespace

Backend::Backend(const fs::path &exeDir) : m_exeDir(exeDir) {
    PWSTR roaming = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)) && roaming) {
        m_dataDir = fs::path(roaming) / L"OdiumProjects";
        CoTaskMemFree(roaming);
    } else {
        m_dataDir = exeDir / L"data";
    }
    m_projectsFile = m_dataDir / L"projects.tsv";
    m_settingsFile = m_dataDir / L"settings.txt";

    m_location = (fs::path(envVar(L"USERPROFILE")) / L"AndroidStudioProjects").make_preferred().wstring();
    m_prefix = L"com.example";
    m_accent = L"#ff2d4d";
    loadSettings();
    load();

    if (!studioFound()) {
        fs::path f = findStudio();
        if (!f.empty()) { m_studio = f.wstring(); saveSettings(); }
    }
}

bool Backend::studioFound() const { return !m_studio.empty() && pathExists(studioExe(m_studio)); }

void Backend::loadSettings() {
    std::istringstream in(readFile(m_settingsFile));
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        std::wstring v = widen(line.substr(eq + 1));
        if (k == "studio") m_studio = v;
        else if (k == "location" && !v.empty()) m_location = v;
        else if (k == "prefix" && !v.empty()) m_prefix = v;
        else if (k == "accent" && !v.empty()) m_accent = v;
    }
}

void Backend::saveSettings() const {
    std::string s = "studio=" + narrow(m_studio) + "\nlocation=" + narrow(m_location) + "\nprefix=" + narrow(m_prefix) +
                    "\naccent=" + narrow(m_accent) + "\n";
    writeFile(m_settingsFile, s);
}

void Backend::load() {
    m_projects.clear();
    std::istringstream in(readFile(m_projectsFile));
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto f = split(widen(line), L'\t');
        if (f.size() < 5 || f[1].empty()) continue;
        Project p;
        p.name = f[0].empty() ? fs::path(f[1]).filename().wstring() : f[0];
        p.path = f[1];
        p.package = f[2];
        p.created = _wtoi64(f[3].c_str());
        p.opened = _wtoi64(f[4].c_str());
        m_projects.push_back(p);
    }
}

void Backend::save() const {
    std::wstring out;
    for (const Project &p : m_projects) {
        auto clean = [](std::wstring s) { for (auto &c : s) if (c == L'\t' || c == L'\n' || c == L'\r') c = L' '; return s; };
        out += clean(p.name) + L"\t" + clean(p.path) + L"\t" + clean(p.package) + L"\t" + std::to_wstring(p.created) + L"\t" +
               std::to_wstring(p.opened) + L"\n";
    }
    writeFile(m_projectsFile, narrow(out));
}

Project *Backend::find(const std::wstring &path) {
    const std::wstring key = lower(path);
    for (auto &p : m_projects)
        if (lower(p.path) == key) return &p;
    return nullptr;
}

std::wstring Backend::stateJson() const {
    std::wstring s = L"{\"projects\":[";
    bool first = true;
    for (const Project &p : m_projects) {
        if (!first) s += L",";
        first = false;
        s += L"{\"name\":" + jsonStr(p.name) + L",\"path\":" + jsonStr(p.path) + L",\"pkg\":" + jsonStr(p.package) +
             L",\"created\":" + std::to_wstring(p.created) + L",\"opened\":" + std::to_wstring(p.opened) +
             L",\"missing\":" + (isDir(p.path) ? L"false" : L"true") + L"}";
    }
    s += L"],\"settings\":{\"studioPath\":" + jsonStr(m_studio) + L",\"studioFound\":" + (studioFound() ? L"true" : L"false") +
         L",\"location\":" + jsonStr(m_location) + L",\"prefix\":" + jsonStr(m_prefix) + L",\"accent\":" + jsonStr(m_accent) +
         L"},\"dataPath\":" + jsonStr(m_dataDir.wstring()) + L"}";
    return s;
}

std::wstring Backend::toJson(int id, const Reply &r) const {
    std::wstring s = L"{\"id\":" + std::to_wstring(id) + L",\"ok\":" + (r.ok ? L"true" : L"false");
    if (!r.error.empty()) s += L",\"error\":" + jsonStr(r.error);
    if (!r.toast.empty()) s += L",\"toast\":{\"text\":" + jsonStr(r.toast) + L",\"error\":" + (r.toastError ? L"true" : L"false") + L"}";
    if (r.hasValue) s += L",\"value\":" + jsonStr(r.value);
    if (r.withState) s += L",\"state\":" + stateJson();
    return s + L"}";
}

std::wstring Backend::launchStudio(const std::wstring &projectPath) const {
    if (!studioFound()) return L"Android Studio wasn't found. Set its location in Settings.";
    fs::path exe = studioExe(m_studio);
    std::wstring args = L"\"" + projectPath + L"\"";
    HINSTANCE h = ShellExecuteW(nullptr, L"open", exe.c_str(), args.c_str(), exe.parent_path().c_str(), SW_SHOWNORMAL);
    if ((INT_PTR)h <= 32) return L"Couldn't launch Android Studio. Check its location in Settings.";
    return {};
}

void Backend::openProject(const std::wstring &path, Reply &r) {
    Project *p = find(path);
    const std::wstring name = p ? p->name : fs::path(path).filename().wstring();
    if (!isDir(path)) {
        r.toast = name + L" isn't there anymore. Move it back or remove it from the list.";
        r.toastError = true;
        return;
    }
    std::wstring err = launchStudio(path);
    if (!err.empty()) { r.toast = err; r.toastError = true; return; }
    if (p) { p->opened = (long long)std::time(nullptr); save(); }
    r.toast = L"Opening " + name + L" in Android Studio…";
}

void Backend::addExisting(const std::wstring &folder, Reply &r) {
    const std::wstring path = normPath(folder);
    if (path.empty()) return;
    if (!isDir(path) || !looksLikeGradleProject(path)) {
        r.toast = L"That folder doesn't look like an Android Studio project.";
        r.toastError = true;
        return;
    }
    if (find(path)) { r.toast = L"That project is already in your list."; return; }
    Project p;
    p.name = fs::path(path).filename().wstring();
    if (p.name.empty()) p.name = path;
    p.path = path;
    p.package = readNamespace(path);
    p.created = (long long)std::time(nullptr);
    m_projects.push_back(p);
    save();
    r.toast = L"Added " + p.name;
}

void Backend::deleteProject(const std::wstring &path, Reply &r) {
    std::error_code ec;
    if (isDir(path)) {
        const fs::path fp(path);
        const bool isRoot = (fp == fp.root_path());
        const bool isHome = (lower(path) == lower(normPath(envVar(L"USERPROFILE"))));
        if (!looksLikeGradleProject(fp) || isRoot || isHome) {
            r.toast = L"That doesn't look like an Android project, so I won't delete it. Remove it from the list instead.";
            r.toastError = true;
            return;
        }
        fs::remove_all(fp, ec);
        if (ec.value() != 0 || pathExists(fp)) {
            r.toast = L"Couldn't delete everything. Close Android Studio and try again.";
            r.toastError = true;
            return;
        }
    }
    m_projects.erase(std::remove_if(m_projects.begin(), m_projects.end(),
                                    [&](const Project &p) { return lower(p.path) == lower(path); }), m_projects.end());
    save();
    r.toast = L"Project deleted from disk.";
}

std::wstring Backend::validate(const std::wstring &name, const std::wstring &pkgIn, const std::wstring &location, fs::path *dest) const {
    const std::wstring n = trim(name);
    if (n.empty()) return L"Give your app a name.";
    if (n.size() > 40) return L"That name is too long (40 characters max).";
    const std::wstring folder = projectFolderName(n);
    if (folder.empty()) return L"The name needs at least one letter or number.";

    const std::wstring pkg = trim(pkgIn);
    if (!std::regex_match(narrow(pkg), std::regex("^[a-z][a-z0-9_]*(\\.[a-z][a-z0-9_]*)+$")))
        return L"Package name should look like com.example.myapp (lowercase letters, numbers, underscores).";
    for (const auto &seg : split(pkg, L'.'))
        if (reservedWords().count(narrow(seg)))
            return L"\"" + seg + L"\" is a reserved word and can't be used in a package name.";

    if (trim(location).empty()) return L"Choose where to save the project.";
    const std::wstring loc = normPath(location);
    std::error_code ec;
    if (pathExists(loc) && !isDir(loc)) return L"The save location is a file, not a folder.";

    const fs::path d = fs::path(loc) / folder;
    if (pathExists(d)) {
        bool empty = fs::is_directory(d, ec) && fs::directory_iterator(d, ec) == fs::directory_iterator();
        if (!empty) return L"A folder named \"" + folder + L"\" already exists there. Pick another name or location.";
    }
    if (dest) *dest = d;
    return {};
}

Backend::Reply Backend::create(const std::vector<std::wstring> &a) {
    Reply r;
    // name, package, location, language, minSdk, workflow, openAfter
    if (a.size() < 7) { r.ok = false; r.error = L"Bad request."; r.withState = false; return r; }
    fs::path dest;
    std::wstring err = validate(a[0], a[1], a[2], &dest);
    if (!err.empty()) { r.ok = false; r.error = err; r.withState = false; return r; }

    GenOptions o;
    o.name = trim(a[0]);
    o.package = trim(a[1]);
    o.dest = dest;
    o.kotlin = (lower(a[3]) == L"kotlin");
    o.minSdk = _wtoi(a[4].c_str());
    if (o.minSdk < 21) o.minSdk = 26;
    o.workflow = (a[5] == L"1");

    err = generateProject(m_exeDir / L"template", o);
    if (!err.empty()) { r.ok = false; r.error = err; r.withState = false; return r; }

    Project p;
    p.name = o.name;
    p.path = normPath(dest.wstring());
    p.package = o.package;
    p.created = (long long)std::time(nullptr);
    if (Project *old = find(p.path)) *old = p; else m_projects.push_back(p);
    save();

    if (a[6] == L"1") openProject(p.path, r);
    else r.toast = L"Created " + o.name;
    return r;
}

std::wstring Backend::handle(int id, const std::wstring &cmd, const std::vector<std::wstring> &a, HWND owner) {
    Reply r;
    auto arg = [&](size_t i) { return i < a.size() ? a[i] : std::wstring(); };

    if (cmd == L"state") {
        // nothing else: just return the current state
    } else if (cmd == L"open") {
        openProject(normPath(arg(0)), r);
    } else if (cmd == L"reveal") {
        const std::wstring p = normPath(arg(0));
        if (isDir(p)) ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        else { r.toast = L"That folder doesn't exist anymore."; r.toastError = true; }
        r.withState = false;
    } else if (cmd == L"remove") {
        const std::wstring p = normPath(arg(0));
        m_projects.erase(std::remove_if(m_projects.begin(), m_projects.end(),
                                        [&](const Project &q) { return lower(q.path) == lower(p); }), m_projects.end());
        save();
        r.toast = L"Removed from the list. Files were left untouched.";
    } else if (cmd == L"delete") {
        deleteProject(normPath(arg(0)), r);
    } else if (cmd == L"import") {
        const std::wstring p = pickPath(owner, true, m_location, L"Select an Android Studio project folder");
        if (!p.empty()) addExisting(p, r);
    } else if (cmd == L"pickFolder") {
        r.value = pickPath(owner, true, normPath(arg(0)), L"Choose a folder");
        r.hasValue = true;
        r.withState = false;
    } else if (cmd == L"pickStudio") {
        const std::wstring p = pickPath(owner, false, m_studio.empty() ? L"" : fs::path(m_studio).parent_path().wstring(), L"Select studio64.exe");
        if (!p.empty()) { m_studio = normPath(p); saveSettings(); }
    } else if (cmd == L"set") {
        const std::wstring k = arg(0), v = arg(1);
        if (k == L"studio") m_studio = normPath(v);
        else if (k == L"location") { std::wstring n = normPath(v); if (!n.empty()) m_location = n; }
        else if (k == L"prefix") {
            std::wstring n = lower(trim(v));
            while (!n.empty() && n.front() == L'.') n.erase(n.begin());
            while (!n.empty() && n.back() == L'.') n.pop_back();
            m_prefix = n.empty() ? L"com.example" : n;
        } else if (k == L"accent") m_accent = v;
        saveSettings();
    } else if (cmd == L"detect") {
        fs::path f = findStudio();
        if (f.empty()) { r.toast = L"Couldn't find Android Studio. Set its location manually."; r.toastError = true; }
        else { m_studio = f.wstring(); saveSettings(); r.toast = L"Android Studio found."; }
    } else if (cmd == L"create") {
        r = create(a);
    } else {
        r.ok = false;
        r.error = L"Unknown command.";
        r.withState = false;
    }
    return toJson(id, r);
}
